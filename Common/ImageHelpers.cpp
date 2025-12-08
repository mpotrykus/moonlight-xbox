#include "pch.h"
#include "ImageHelpers.h"
#include "../Utils.hpp"

// Helper interface for efficient access to SoftwareBitmap underlying bytes
struct DECLSPEC_UUID("5B0D3235-4DBA-4D44-865E-8F1D0ED9F3E4") IMemoryBufferByteAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

using namespace Platform;
using namespace Windows::Storage;
using namespace Windows::Storage::Streams;
using namespace Windows::Graphics::Imaging;
using namespace concurrency;

concurrency::task<SoftwareBitmap^> ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync(String^ path) {
    if (path == nullptr) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: path is null\n");
        return task_from_result<SoftwareBitmap^>(nullptr);
    }
    const wchar_t* raw = path->Data();
    try {
        // ms-appx and ms-appdata
        if (wcsncmp(raw, L"ms-appx://", 10) == 0 || wcsncmp(raw, L"ms-appdata://", 12) == 0) {
            auto uri = ref new Windows::Foundation::Uri(path);
            return create_task(StorageFile::GetFileFromApplicationUriAsync(uri))
            .then([](StorageFile^ file) -> task<IRandomAccessStream^> {
                return create_task(file->OpenReadAsync()).then([](IRandomAccessStreamWithContentType^ s) -> IRandomAccessStream^ { return safe_cast<IRandomAccessStream^>(s); });
            }).then([](IRandomAccessStream^ stream) -> task<BitmapDecoder^> {
                return create_task(BitmapDecoder::CreateAsync(stream));
            }).then([](BitmapDecoder^ decoder) -> task<SoftwareBitmap^> {
                return create_task(decoder->GetSoftwareBitmapAsync());
            }).then([](SoftwareBitmap^ softwareBitmap) -> SoftwareBitmap^ {
                return EnsureBgra8Premultiplied(softwareBitmap);
            });
        }

        // file system path
        if ((wcslen(raw) >= 2 && raw[1] == L':') || (wcslen(raw) >= 2 && raw[0] == L'\\' && raw[1] == L'\\')) {
            return create_task(StorageFile::GetFileFromPathAsync(path)).then([](StorageFile^ file) -> task<IRandomAccessStream^> {
                return create_task(file->OpenReadAsync()).then([](IRandomAccessStreamWithContentType^ s) -> IRandomAccessStream^ { return safe_cast<IRandomAccessStream^>(s); });
            }).then([](IRandomAccessStream^ stream) -> task<BitmapDecoder^> {
                return create_task(BitmapDecoder::CreateAsync(stream));
            }).then([](BitmapDecoder^ decoder) -> task<SoftwareBitmap^> {
                return create_task(decoder->GetSoftwareBitmapAsync());
            }).then([](SoftwareBitmap^ softwareBitmap) -> SoftwareBitmap^ {
                return EnsureBgra8Premultiplied(softwareBitmap);
            });
        }

        moonlight_xbox_dx::Utils::Logf("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: unsupported path='%S'\n", raw);
        return task_from_result<SoftwareBitmap^>(nullptr);
    } catch(...) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: exception during load\n");
        return task_from_result<SoftwareBitmap^>(nullptr);
    }
}

concurrency::task<IRandomAccessStream^> ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync(SoftwareBitmap^ bitmap) {
    if (bitmap == nullptr) return task_from_result<IRandomAccessStream^>(nullptr);
    try {
        auto stream = ref new InMemoryRandomAccessStream();
        return create_task(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId, stream)).then([bitmap, stream](BitmapEncoder^ encoder) -> task<void> {
            encoder->SetSoftwareBitmap(bitmap);
            return create_task(encoder->FlushAsync()).then([]() {});
        }).then([stream]() -> IRandomAccessStream^ {
            stream->Seek(0);
            return stream;
        });
    } catch(...) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync: exception encoding\n");
        return task_from_result<IRandomAccessStream^>(nullptr);
    }
}

SoftwareBitmap^ ImageHelpers::EnsureBgra8Premultiplied(SoftwareBitmap^ bitmap) {
    if (bitmap == nullptr) return nullptr;
    if (bitmap->BitmapPixelFormat != BitmapPixelFormat::Bgra8 || bitmap->BitmapAlphaMode != BitmapAlphaMode::Premultiplied) {
        try {
            auto conv = SoftwareBitmap::Convert(bitmap, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Premultiplied);
            return conv;
        } catch(...) {
            moonlight_xbox_dx::Utils::Log("ImageHelpers::EnsureBgra8Premultiplied: conversion failed\n");
            return bitmap;
        }
    }
    return bitmap;
}

SoftwareBitmap^ ImageHelpers::CompositeWithMask(SoftwareBitmap^ base, SoftwareBitmap^ mask, int featherRadius) {
    if (base == nullptr) return nullptr;
    if (mask == nullptr) return base;
    try {
        base = EnsureBgra8Premultiplied(base);
        mask = EnsureBgra8Premultiplied(mask);

        // Quick size check
        auto bbuf = base->LockBuffer(BitmapBufferAccessMode::Read);
        auto bdesc = bbuf->GetPlaneDescription(0);
        auto mbuf = mask->LockBuffer(BitmapBufferAccessMode::Read);
        auto mdesc = mbuf->GetPlaneDescription(0);
        int width = bdesc.Width;
        int height = bdesc.Height;
        if (mdesc.Width != width || mdesc.Height != height) {
            moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: mask size differs from base\n");
            return base;
        }

        // Read mask alpha into single-channel buffer
        std::vector<uint8_t> maskAlpha((size_t)height * (size_t)mdesc.Stride);
        // Try fast-path memory access
        {
            auto mref = mbuf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> mba;
            IUnknown* unk = reinterpret_cast<IUnknown*>(mref);
            if (unk != nullptr && SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&mba)))) {
                BYTE* data = nullptr; UINT32 cap = 0;
                auto raw = mba.Get();
                if (raw != nullptr && SUCCEEDED(raw->GetBuffer(&data, &cap))) {
                    memcpy(maskAlpha.data(), data + mdesc.StartIndex, (size_t)height * (size_t)mdesc.Stride);
                }
            } else {
                try {
                    auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)height * mdesc.Stride);
                    mask->CopyToBuffer(ib);
                    auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                    reader->ReadBytes(Platform::ArrayReference<uint8_t>(maskAlpha.data(), (unsigned int)maskAlpha.size()));
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: failed to read mask buffer\n");
                    return base;
                }
            }
        }

        // Extract luminance/alpha from mask BGRA into single-channel alpha (0..255)
        std::vector<uint8_t> alpha((size_t)width * (size_t)height);
        for (int y = 0; y < height; ++y) {
            uint8_t* row = maskAlpha.data() + y * mdesc.Stride;
            for (int x = 0; x < width; ++x) {
                uint8_t b = row[x*4 + 0];
                uint8_t g = row[x*4 + 1];
                uint8_t r = row[x*4 + 2];
                // Use luminance as mask; if mask has alpha channel, prefer it
                uint8_t a = row[x*4 + 3];
                if (a == 0) {
                    // approximate luminance
                    uint16_t lum = (uint16_t)((uint16_t)r * 77 + (uint16_t)g * 150 + (uint16_t)b * 29) >> 8; // 0..255
                    alpha[y*width + x] = (uint8_t)lum;
                } else {
                    alpha[y*width + x] = a;
                }
            }
        }

        // Feather alpha via separable box blur (radius = featherRadius)
        if (featherRadius > 0) {
            std::vector<uint8_t> tmp(alpha.size());
            // horizontal pass
            for (int y = 0; y < height; ++y) {
                int sum = 0;
                int w = width;
                int r = featherRadius;
                int row = y * width;
                int win = r*2 + 1;
                // initialize
                for (int i = -r; i <= r; ++i) {
                    int xi = std::min(w-1, std::max(0, i));
                    sum += alpha[row + xi];
                }
                tmp[row + 0] = (uint8_t)(sum / win);
                for (int x = 1; x < w; ++x) {
                    int add = std::min(w-1, x + r);
                    int sub = std::max(0, x - r - 1);
                    sum += alpha[row + add] - alpha[row + sub];
                    tmp[row + x] = (uint8_t)(sum / win);
                }
            }
            // vertical pass
            std::vector<uint8_t> blurred(alpha.size());
            for (int x = 0; x < width; ++x) {
                int sum = 0;
                int r = featherRadius;
                int win = r*2 + 1;
                for (int i = -r; i <= r; ++i) {
                    int yi = std::min(height-1, std::max(0, i));
                    sum += tmp[yi*width + x];
                }
                blurred[x] = (uint8_t)(sum / win);
                for (int y = 1; y < height; ++y) {
                    int add = std::min(height-1, y + r);
                    int sub = std::max(0, y - r - 1);
                    sum += tmp[add*width + x] - tmp[sub*width + x];
                    blurred[y*width + x] = (uint8_t)(sum / win);
                }
            }
            alpha.swap(blurred);
        }

        // Read base pixels and write out premultiplied BGRA using alpha mask
        std::vector<uint8_t> baseBuf((size_t)height * (size_t)bdesc.Stride);
        {
            auto bref = bbuf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> bba;
            IUnknown* unk = reinterpret_cast<IUnknown*>(bref);
            if (unk != nullptr && SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&bba)))) {
                BYTE* data = nullptr; UINT32 cap = 0;
                auto raw = bba.Get();
                if (raw != nullptr && SUCCEEDED(raw->GetBuffer(&data, &cap))) {
                    memcpy(baseBuf.data(), data + bdesc.StartIndex, (size_t)height * (size_t)bdesc.Stride);
                }
            } else {
                try {
                    auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)height * bdesc.Stride);
                    base->CopyToBuffer(ib);
                    auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                    reader->ReadBytes(Platform::ArrayReference<uint8_t>(baseBuf.data(), (unsigned int)baseBuf.size()));
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: failed to read base buffer\n");
                    return base;
                }
            }
        }

        // Create output bitmap
        auto outBmp = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
        auto outBuf = outBmp->LockBuffer(BitmapBufferAccessMode::Write);
        auto outRef = outBuf->CreateReference();
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> outAccess;
        IUnknown* outUnk = reinterpret_cast<IUnknown*>(outRef);
        if (outUnk != nullptr) outUnk->QueryInterface(IID_PPV_ARGS(&outAccess));
        if (outAccess != nullptr) {
            BYTE* outData = nullptr; UINT32 outCap = 0;
            auto rawOut = outAccess.Get();
            if (rawOut != nullptr) rawOut->GetBuffer(&outData, &outCap);
            auto outDesc = outBuf->GetPlaneDescription(0);
            for (int y = 0; y < height; ++y) {
                uint8_t* srcRow = baseBuf.data() + y * bdesc.Stride;
                uint8_t* dstRow = outData + outDesc.StartIndex + y * outDesc.Stride;
                for (int x = 0; x < width; ++x) {
                    uint8_t b = srcRow[x*4 + 0];
                    uint8_t g = srcRow[x*4 + 1];
                    uint8_t r = srcRow[x*4 + 2];
                    uint8_t a = alpha[y*width + x];
                    // premultiply
                    uint8_t pr = (uint8_t)((r * a + 127) / 255);
                    uint8_t pg = (uint8_t)((g * a + 127) / 255);
                    uint8_t pb = (uint8_t)((b * a + 127) / 255);
                    dstRow[x*4 + 0] = pb;
                    dstRow[x*4 + 1] = pg;
                    dstRow[x*4 + 2] = pr;
                    dstRow[x*4 + 3] = a;
                }
            }
            return outBmp;
        }
        // Fallback: write via CopyFromBuffer
        {
            auto outDesc = outBuf->GetPlaneDescription(0);
            std::vector<uint8_t> tmp((size_t)height * (size_t)outDesc.Stride);
            for (int y = 0; y < height; ++y) {
                uint8_t* srcRow = baseBuf.data() + y * bdesc.Stride;
                uint8_t* dstRow = tmp.data() + y * outDesc.Stride;
                for (int x = 0; x < width; ++x) {
                    uint8_t b = srcRow[x*4 + 0];
                    uint8_t g = srcRow[x*4 + 1];
                    uint8_t r = srcRow[x*4 + 2];
                    uint8_t a = alpha[y*width + x];
                    uint8_t pr = (uint8_t)((r * a + 127) / 255);
                    uint8_t pg = (uint8_t)((g * a + 127) / 255);
                    uint8_t pb = (uint8_t)((b * a + 127) / 255);
                    dstRow[x*4 + 0] = pb;
                    dstRow[x*4 + 1] = pg;
                    dstRow[x*4 + 2] = pr;
                    dstRow[x*4 + 3] = a;
                }
                if (outDesc.Stride > (unsigned int)width*4) memset(tmp.data() + y*outDesc.Stride + width*4, 0, outDesc.Stride - width*4);
            }
            auto writer = ref new Windows::Storage::Streams::DataWriter();
            writer->WriteBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
            auto buf = writer->DetachBuffer();
            // release buffer locks
            outRef = nullptr; outBuf = nullptr;
            outBmp->CopyFromBuffer(buf);
            return outBmp;
        }
    } catch(...) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: unexpected exception\n");
        return base;
    }
}
