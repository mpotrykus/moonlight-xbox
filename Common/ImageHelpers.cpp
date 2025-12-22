#include "pch.h"
#include "ImageHelpers.h"
#include "../Utils.hpp"
#include "EffectsLibrary.h"

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
    try { moonlight_xbox_dx::Utils::Logf("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: path='%S'\n", path->Data()); } catch(...) {}
    const wchar_t* raw = path->Data();
    try {
        // Avoid trying to decode SVG files with BitmapDecoder (no SVG codec via WIC).
        size_t rawLen = wcslen(raw);
        if (rawLen >= 4) {
            const wchar_t* ext = raw + rawLen - 4;
            // case-insensitive compare for ".svg"
            if (_wcsicmp(ext, L".svg") == 0) {
                moonlight_xbox_dx::Utils::Logf("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: skipping SVG path='%S'\n", raw);
                return task_from_result<SoftwareBitmap^>(nullptr);
            }
        }
        // ms-appx and ms-appdata
        if (wcsncmp(raw, L"ms-appx://", 10) == 0 || wcsncmp(raw, L"ms-appdata://", 12) == 0) {
            auto uri = ref new Windows::Foundation::Uri(path);
            // Chain continuations but catch any exceptions from each async stage to avoid
            // unhandled Platform::COMException bubbling out into the Rendering callback.
            return create_task(StorageFile::GetFileFromApplicationUriAsync(uri))
            .then([](task<StorageFile^> fileTask) -> task<IRandomAccessStream^> {
                try {
                    StorageFile^ file = fileTask.get();
                    return create_task(file->OpenReadAsync()).then([](task<IRandomAccessStreamWithContentType^> sTask) -> IRandomAccessStream^ {
                        try { auto s = sTask.get(); return safe_cast<IRandomAccessStream^>(s); } catch(...) { return nullptr; }
                    });
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: GetFileFromApplicationUriAsync failed\n");
                    return task_from_result<IRandomAccessStream^>(nullptr);
                }
            }).then([](task<IRandomAccessStream^> streamTask) -> task<SoftwareBitmap^> {
                try {
                    auto stream = streamTask.get();
                    if (stream == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                    return create_task(BitmapDecoder::CreateAsync(stream)).then([](BitmapDecoder^ decoder) -> task<SoftwareBitmap^> {
                        if (decoder == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                        return create_task(decoder->GetSoftwareBitmapAsync()).then([](SoftwareBitmap^ sb) -> SoftwareBitmap^ {
                            try { return EnsureBgra8Premultiplied(sb); } catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: EnsureBgra8Premultiplied failed\n"); return nullptr; }
                        });
                    }).then([](task<SoftwareBitmap^> sbTask) -> SoftwareBitmap^ {
                        try { return sbTask.get(); }
                        catch (Platform::COMException^ ex) { moonlight_xbox_dx::Utils::Logf("ImageHelpers: BitmapDecoder/GetSoftwareBitmap failed hr=0x%08x\n", ex->HResult); return nullptr; }
                        catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: BitmapDecoder/GetSoftwareBitmap unknown error\n"); return nullptr; }
                    });
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: stream task failed\n");
                    return task_from_result<SoftwareBitmap^>(nullptr);
                }
            });
        }

        // file system path
        if ((wcslen(raw) >= 2 && raw[1] == L':') || (wcslen(raw) >= 2 && raw[0] == L'\\' && raw[1] == L'\\')) {
            // If this path is inside our app LocalFolder, prefer using ApplicationData::Current->LocalFolder->GetFileAsync
            try {
                auto localPath = Windows::Storage::ApplicationData::Current->LocalFolder->Path->Data();
                size_t lpLen = wcslen(localPath);
                if (_wcsnicmp(raw, localPath, lpLen) == 0) {
                    // compute relative path (skip leading separator if present)
                    const wchar_t* rel = raw + lpLen;
                    if (*rel == L'\\' || *rel == L'/') ++rel;
                    Platform::String^ relStr = ref new Platform::String(rel);
                    return create_task(Windows::Storage::ApplicationData::Current->LocalFolder->GetFileAsync(relStr)).then([](StorageFile^ file) -> task<IRandomAccessStream^> {
                        if (file == nullptr) return task_from_result<IRandomAccessStream^>(nullptr);
                        return create_task(file->OpenReadAsync()).then([](IRandomAccessStreamWithContentType^ s) -> IRandomAccessStream^ { return safe_cast<IRandomAccessStream^>(s); });
                    }).then([](task<IRandomAccessStream^> streamTask) -> task<SoftwareBitmap^> {
                        try {
                            auto stream = streamTask.get();
                            if (stream == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                            return create_task(BitmapDecoder::CreateAsync(stream)).then([](BitmapDecoder^ decoder) -> task<SoftwareBitmap^> {
                                if (decoder == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                                return create_task(decoder->GetSoftwareBitmapAsync()).then([](SoftwareBitmap^ sb) -> SoftwareBitmap^ {
                                    try { return EnsureBgra8Premultiplied(sb); } catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: EnsureBgra8Premultiplied failed\n"); return nullptr; }
                                });
                            }).then([](task<SoftwareBitmap^> sbTask) -> SoftwareBitmap^ {
                                try { return sbTask.get(); }
                                catch (Platform::COMException^ ex) { moonlight_xbox_dx::Utils::Logf("ImageHelpers: BitmapDecoder/GetSoftwareBitmap failed hr=0x%08x\n", ex->HResult); return nullptr; }
                                catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: BitmapDecoder/GetSoftwareBitmap unknown error\n"); return nullptr; }
                            });
                        } catch(...) {
                            moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: localFolder stream task failed\n");
                            return task_from_result<SoftwareBitmap^>(nullptr);
                        }
                    });
                }
            } catch(...) {}

            // Fallback to generic GetFileFromPathAsync
            return create_task(StorageFile::GetFileFromPathAsync(path)).then([](task<StorageFile^> fileTask) -> task<IRandomAccessStream^> {
                try {
                    StorageFile^ file = fileTask.get();
                    return create_task(file->OpenReadAsync()).then([](task<IRandomAccessStreamWithContentType^> sTask) -> IRandomAccessStream^ {
                        try { auto s = sTask.get(); return safe_cast<IRandomAccessStream^>(s); } catch(...) { return nullptr; }
                    });
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: GetFileFromPathAsync failed\n");
                    return task_from_result<IRandomAccessStream^>(nullptr);
                }
            }).then([](task<IRandomAccessStream^> streamTask) -> task<SoftwareBitmap^> {
                try {
                    auto stream = streamTask.get();
                    if (stream == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                    return create_task(BitmapDecoder::CreateAsync(stream)).then([](BitmapDecoder^ decoder) -> task<SoftwareBitmap^> {
                        if (decoder == nullptr) return task_from_result<SoftwareBitmap^>(nullptr);
                        return create_task(decoder->GetSoftwareBitmapAsync()).then([](SoftwareBitmap^ sb) -> SoftwareBitmap^ {
                            try { return EnsureBgra8Premultiplied(sb); } catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: EnsureBgra8Premultiplied failed\n"); return nullptr; }
                        });
                    }).then([](task<SoftwareBitmap^> sbTask) -> SoftwareBitmap^ {
                        try { return sbTask.get(); }
                        catch (Platform::COMException^ ex) { moonlight_xbox_dx::Utils::Logf("ImageHelpers: BitmapDecoder/GetSoftwareBitmap failed hr=0x%08x\n", ex->HResult); return nullptr; }
                        catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers: BitmapDecoder/GetSoftwareBitmap unknown error\n"); return nullptr; }
                    });
                } catch(...) {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::LoadSoftwareBitmapFromUriOrPathAsync: stream task failed (file path)\n");
                    return task_from_result<SoftwareBitmap^>(nullptr);
                }
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
        try { moonlight_xbox_dx::Utils::Logf("ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync: encoding bitmap %u x %u\n", bitmap->PixelWidth, bitmap->PixelHeight); } catch(...) {}
        auto stream = ref new InMemoryRandomAccessStream();
        // Ensure encoder receives a straight-alpha bitmap. Many viewers expect straight alpha.
        SoftwareBitmap^ encodeBitmap = nullptr;
        try {
            unsigned int w = bitmap->PixelWidth;
            unsigned int h = bitmap->PixelHeight;
            // Ensure we have a premultiplied BGRA source to read from
            SoftwareBitmap^ src = EnsureBgra8Premultiplied(bitmap);
            if (src == nullptr) src = bitmap;

            // Create output straight-alpha bitmap
            auto out = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, (int)w, (int)h, BitmapAlphaMode::Straight);

            // Try fast-path buffer access for both src and out
            bool fastPath = false;
            try {
                auto srcBuf = src->LockBuffer(BitmapBufferAccessMode::Read);
                auto outBuf = out->LockBuffer(BitmapBufferAccessMode::Write);
                auto srcRef = srcBuf->CreateReference();
                auto outRef = outBuf->CreateReference();
                Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> srcAccess;
                Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> outAccess;
                IUnknown* srcUnk = reinterpret_cast<IUnknown*>(srcRef);
                IUnknown* outUnk = reinterpret_cast<IUnknown*>(outRef);
                BYTE* srcData = nullptr; UINT32 srcCap = 0;
                BYTE* outData = nullptr; UINT32 outCap = 0;
                if (srcUnk != nullptr && outUnk != nullptr && SUCCEEDED(srcUnk->QueryInterface(IID_PPV_ARGS(&srcAccess))) && SUCCEEDED(outUnk->QueryInterface(IID_PPV_ARGS(&outAccess)))) {
                    if (SUCCEEDED(srcAccess->GetBuffer(&srcData, &srcCap)) && SUCCEEDED(outAccess->GetBuffer(&outData, &outCap))) {
                        auto srcDesc = srcBuf->GetPlaneDescription(0);
                        auto outDesc = outBuf->GetPlaneDescription(0);
                        for (unsigned int y = 0; y < h; ++y) {
                            uint8_t* srow = srcData + srcDesc.StartIndex + (size_t)y * srcDesc.Stride;
                            uint8_t* orow = outData + outDesc.StartIndex + (size_t)y * outDesc.Stride;
                            for (unsigned int x = 0; x < w; ++x) {
                                uint8_t pb = srow[x*4 + 0];
                                uint8_t pg = srow[x*4 + 1];
                                uint8_t pr = srow[x*4 + 2];
                                uint8_t pa = srow[x*4 + 3];
                                if (pa == 0) {
                                    orow[x*4 + 0] = 0; orow[x*4 + 1] = 0; orow[x*4 + 2] = 0; orow[x*4 + 3] = 0;
                                } else {
                                    // un-premultiply: original = premult*255 / alpha
                                    uint32_t r = (uint32_t)pr * 255 + (pa / 2);
                                    uint32_t g = (uint32_t)pg * 255 + (pa / 2);
                                    uint32_t b = (uint32_t)pb * 255 + (pa / 2);
                                    uint8_t orr = (uint8_t)std::min<uint32_t>(255, r / pa);
                                    uint8_t org = (uint8_t)std::min<uint32_t>(255, g / pa);
                                    uint8_t orb = (uint8_t)std::min<uint32_t>(255, b / pa);
                                    orow[x*4 + 0] = (uint8_t)orb;
                                    orow[x*4 + 1] = (uint8_t)org;
                                    orow[x*4 + 2] = (uint8_t)orr;
                                    orow[x*4 + 3] = pa;
                                }
                            }
                        }
                        fastPath = true;
                    }
                }
            } catch(...) { fastPath = false; }

            if (!fastPath) {
                // Fallback: read source into a temp buffer then create an out buffer with unpremultiplied pixels
                try {
                    unsigned int sw = src->PixelWidth;
                    unsigned int sh = src->PixelHeight;
                    auto srcBuf2 = src->LockBuffer(BitmapBufferAccessMode::Read);
                    auto srcDesc2 = srcBuf2->GetPlaneDescription(0);
                    std::vector<uint8_t> srcTmp((size_t)sh * (size_t)srcDesc2.Stride);
                    try {
                        auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)srcTmp.size());
                        src->CopyToBuffer(ib);
                        auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                        reader->ReadBytes(Platform::ArrayReference<uint8_t>(srcTmp.data(), (unsigned int)srcTmp.size()));
                    } catch(...) { /* fall through */ }

                    auto outDesc = out->LockBuffer(BitmapBufferAccessMode::Write)->GetPlaneDescription(0);
                    std::vector<uint8_t> outTmp((size_t)h * (size_t)outDesc.Stride);
                    for (unsigned int y = 0; y < h; ++y) {
                        uint8_t* srow = srcTmp.data() + (size_t)y * srcDesc2.Stride;
                        uint8_t* orow = outTmp.data() + (size_t)y * outDesc.Stride;
                        for (unsigned int x = 0; x < w; ++x) {
                            uint8_t pb = srow[x*4 + 0];
                            uint8_t pg = srow[x*4 + 1];
                            uint8_t pr = srow[x*4 + 2];
                            uint8_t pa = srow[x*4 + 3];
                            if (pa == 0) {
                                orow[x*4 + 0] = 0; orow[x*4 + 1] = 0; orow[x*4 + 2] = 0; orow[x*4 + 3] = 0;
                            } else {
                                uint32_t r = (uint32_t)pr * 255 + (pa / 2);
                                uint32_t g = (uint32_t)pg * 255 + (pa / 2);
                                uint32_t b = (uint32_t)pb * 255 + (pa / 2);
                                uint8_t orr = (uint8_t)std::min<uint32_t>(255, r / pa);
                                uint8_t org = (uint8_t)std::min<uint32_t>(255, g / pa);
                                uint8_t orb = (uint8_t)std::min<uint32_t>(255, b / pa);
                                orow[x*4 + 0] = orb; orow[x*4 + 1] = org; orow[x*4 + 2] = orr; orow[x*4 + 3] = pa;
                            }
                        }
                    }
                    // Write outTmp into out SoftwareBitmap
                    try {
                        auto writer = ref new Windows::Storage::Streams::DataWriter();
                        writer->WriteBytes(Platform::ArrayReference<uint8_t>(outTmp.data(), (unsigned int)outTmp.size()));
                        auto buf = writer->DetachBuffer();
                        out->CopyFromBuffer(buf);
                    } catch(...) {}
                } catch(...) {}
            }

            encodeBitmap = out;
        } catch(...) {
            // If anything fails, fall back to passing the original bitmap (may result in opaque background)
            encodeBitmap = bitmap;
        }

            // Prepare a straight-alpha pixel buffer from the straight-alpha encodeBitmap up-front.
        Platform::Array<unsigned char>^ pixelArr = nullptr;
        unsigned int outW = bitmap->PixelWidth;
        unsigned int outH = bitmap->PixelHeight;
        try {
            // Use the encodeBitmap we constructed above (straight alpha) as the source for pixel extraction.
            SoftwareBitmap^ src = EnsureBgra8Premultiplied(encodeBitmap != nullptr ? encodeBitmap : bitmap);
            auto buf = src->LockBuffer(BitmapBufferAccessMode::Read);
            auto desc = buf->GetPlaneDescription(0);
            std::vector<uint8_t> srcBytes((size_t)outH * (size_t)desc.Stride);
            try {
                auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)srcBytes.size());
                src->CopyToBuffer(ib);
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                reader->ReadBytes(Platform::ArrayReference<uint8_t>(srcBytes.data(), (unsigned int)srcBytes.size()));
            } catch(...) { srcBytes.clear(); }

            if (!srcBytes.empty()) {
                size_t pixCount = (size_t)outW * (size_t)outH;
                std::vector<uint8_t> outPixels(pixCount * 4);
                for (unsigned int y = 0; y < outH; ++y) {
                    uint8_t* srow = srcBytes.data() + (size_t)y * desc.Stride;
                    for (unsigned int x = 0; x < outW; ++x) {
                        uint8_t pb = srow[x*4 + 0];
                        uint8_t pg = srow[x*4 + 1];
                        uint8_t pr = srow[x*4 + 2];
                        uint8_t pa = srow[x*4 + 3];
                        size_t idx = ((size_t)y * outW + x) * 4;
                        if (pa == 0) {
                            outPixels[idx + 0] = 0;
                            outPixels[idx + 1] = 0;
                            outPixels[idx + 2] = 0;
                            outPixels[idx + 3] = 0;
                        } else {
                            uint32_t r = (uint32_t)pr * 255 + (pa / 2);
                            uint32_t g = (uint32_t)pg * 255 + (pa / 2);
                            uint32_t b = (uint32_t)pb * 255 + (pa / 2);
                            uint8_t orr = (uint8_t)std::min<uint32_t>(255, r / pa);
                            uint8_t org = (uint8_t)std::min<uint32_t>(255, g / pa);
                            uint8_t orb = (uint8_t)std::min<uint32_t>(255, b / pa);
                            outPixels[idx + 0] = orb;
                            outPixels[idx + 1] = org;
                            outPixels[idx + 2] = orr;
                            outPixels[idx + 3] = pa;
                        }
                    }
                }
                // copy into Platform::Array for encoder
                pixelArr = ref new Platform::Array<unsigned char>((unsigned int)outPixels.size());
                memcpy(pixelArr->Data, outPixels.data(), outPixels.size());
                // Log first pixel to help debugging
                if (pixelArr->Length >= 4) {
                    try { ::moonlight_xbox_dx::Utils::Logf("EncodeSoftwareBitmapToPngStreamAsync: prepared straight-alpha first pixel BGRA=%u,%u,%u,%u\n", pixelArr->Data[0], pixelArr->Data[1], pixelArr->Data[2], pixelArr->Data[3]); } catch(...) {}
                }
            }
        } catch(...) { pixelArr = nullptr; }

        // Capture encodeBitmap so the fallback SetSoftwareBitmap can use the straight-alpha SoftwareBitmap
        return concurrency::create_task(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId, stream)).then([pixelArr, outW, outH, stream, encodeBitmap](BitmapEncoder^ encoder) -> concurrency::task<void> {
            bool usedSetPixelData = false;
            try {
                if (pixelArr != nullptr) {
                    encoder->SetPixelData(BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight, outW, outH, 96.0, 96.0, pixelArr);
                    usedSetPixelData = true;
                }
            } catch(...) { usedSetPixelData = false; }

            if (!usedSetPixelData) {
                // Fallback: let encoder convert the SoftwareBitmap (may lose alpha if bitmap is premultiplied)
                try {
                    // Prefer passing the straight-alpha SoftwareBitmap we prepared earlier so alpha is preserved.
                    if (encodeBitmap != nullptr) encoder->SetSoftwareBitmap(encodeBitmap);
                    else encoder->SetSoftwareBitmap(EnsureBgra8Premultiplied(ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, outW, outH, BitmapAlphaMode::Premultiplied)));
                } catch(...) {}
            }
            return concurrency::create_task(encoder->FlushAsync());
        }).then([stream]() -> IRandomAccessStream^ {
            try {
                stream->Seek(0);
                try { moonlight_xbox_dx::Utils::Logf("ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync: encoded stream size=%llu\n", stream->Size); } catch(...) {}
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("ImageHelpers::EncodeSoftwareBitmapToPngStreamAsync: exception seeking stream\n");
            }
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
            try {
                moonlight_xbox_dx::Utils::Logf("ImageHelpers::CompositeWithMask: mask size differs from base (mask=%u x %u, base=%d x %d)\n", mdesc.Width, mdesc.Height, width, height);
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: mask size differs from base\n");
            }
            // Attempt to resize mask to match base before compositing
            try {
                auto resized = ResizeSoftwareBitmap(mask, (unsigned int)width, (unsigned int)height);
                if (resized != nullptr) {
                    mask = resized;
                    // update mask buffer/desc to resized values
                    mbuf = mask->LockBuffer(BitmapBufferAccessMode::Read);
                    mdesc = mbuf->GetPlaneDescription(0);
                } else {
                    moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: failed to resize mask, aborting composite\n");
                    return base;
                }
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: exception while resizing mask\n");
                return base;
            }
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
        uint64_t nonZeroCount = 0;
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
                    if (alpha[y*width + x] != 0) ++nonZeroCount;
                } else {
                    alpha[y*width + x] = a;
                    if (a != 0) ++nonZeroCount;
                }
            }
        }

        // If mask has no non-zero alpha/luminance, skip composite (mask probably empty)
        try {
            if (nonZeroCount == 0) {
                try { moonlight_xbox_dx::Utils::Logf("ImageHelpers::CompositeWithMask: mask alpha is empty (mask=%u x %u, base=%d x %d), skipping composite\n", mdesc.Width, mdesc.Height, width, height); } catch(...) { moonlight_xbox_dx::Utils::Log("ImageHelpers::CompositeWithMask: mask alpha is empty, skipping composite\n"); }
                return base;
            } else {
                try { moonlight_xbox_dx::Utils::Logf("ImageHelpers::CompositeWithMask: mask non-zero coverage=%llu/%d (%.2f%%) (mask=%u x %u, base=%d x %d)\n", nonZeroCount, (uint64_t)width*(uint64_t)height, (double)nonZeroCount * 100.0 / ((double)width*(double)height), mdesc.Width, mdesc.Height, width, height); } catch(...) {}
            }
        } catch(...) {}

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

// Combine mask with rounded-rect mask (alpha multiplication). Returns BGRA8 premultiplied mask.
SoftwareBitmap^ ImageHelpers::CombineMaskWithRoundedRect(SoftwareBitmap^ mask, unsigned int width, unsigned int height, float radiusPx) {
    try {
        // Diagnostics
        try { moonlight_xbox_dx::Utils::Logf("CombineMaskWithRoundedRect: mask=%p target=%u x %u radius=%.2f\n", mask, width, height, radiusPx); } catch(...) {}
        // If no mask, just create rounded mask sized to requested dimensions
        if (mask == nullptr) {
            try { moonlight_xbox_dx::Utils::Log("CombineMaskWithRoundedRect: no input mask, generating rounded mask only\n"); } catch(...) {}
            return CreateRoundedRectMask(width, height, radiusPx);
        }

        // Ensure both are BGRA8 premultiplied
        auto m = EnsureBgra8Premultiplied(mask);
        // Save original input mask for inspection
        try { SaveSoftwareBitmapToLocalPngAsync(m, ref new Platform::String(L"input_mask_original.png")); } catch(...) {}
        try { SaveMaskVisualToLocalPngAsync(m, ref new Platform::String(L"input_mask_original_vis.png")); } catch(...) {}

        // Resize mask if needed
        auto mbuf = m->LockBuffer(BitmapBufferAccessMode::Read);
        auto mdesc = mbuf->GetPlaneDescription(0);
        if ((unsigned int)mdesc.Width != width || (unsigned int)mdesc.Height != height) {
            try {
                auto resized = ResizeSoftwareBitmap(m, width, height);
                if (resized != nullptr) {
                    m = resized;
                    mbuf = m->LockBuffer(BitmapBufferAccessMode::Read);
                    mdesc = mbuf->GetPlaneDescription(0);
                }
            } catch(...) {}
        }

        // Create rounded mask of target size
        auto rounded = CreateRoundedRectMask(width, height, radiusPx);
        if (rounded == nullptr) return m;

        // Read alpha channels from both masks
        std::vector<uint8_t> alphaA((size_t)height * (size_t)width);
        std::vector<uint8_t> alphaB((size_t)height * (size_t)width);

        // Read mask m into alphaA
        {
            auto mref = mbuf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> mba;
            IUnknown* unk = reinterpret_cast<IUnknown*>(mref);
            if (unk != nullptr && SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&mba)))) {
                BYTE* data = nullptr; UINT32 cap = 0;
                auto raw = mba.Get();
                if (raw != nullptr && SUCCEEDED(raw->GetBuffer(&data, &cap))) {
                    // mdesc.Stride bytes per row, assume BGRA
                    for (unsigned int y = 0; y < (unsigned int)mdesc.Height && y < height; ++y) {
                        BYTE* row = data + mdesc.StartIndex + y * mdesc.Stride;
                        for (unsigned int x = 0; x < (unsigned int)mdesc.Width && x < width; ++x) {
                            uint8_t a = row[x*4 + 3];
                            if (a == 0) {
                                uint8_t b = row[x*4 + 0];
                                uint8_t g = row[x*4 + 1];
                                uint8_t r = row[x*4 + 2];
                                uint16_t lum = (uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11;
                                a = (uint8_t)(lum / 100);
                            }
                            alphaA[y * width + x] = a;
                        }
                    }
                }
            } else {
                try {
                    auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)mdesc.Height * mdesc.Stride);
                    m->CopyToBuffer(ib);
                    auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                    std::vector<uint8_t> tmp((size_t)mdesc.Height * mdesc.Stride);
                    reader->ReadBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
                    for (unsigned int y = 0; y < (unsigned int)mdesc.Height && y < height; ++y) {
                        uint8_t* row = tmp.data() + y * mdesc.Stride;
                        for (unsigned int x = 0; x < (unsigned int)mdesc.Width && x < width; ++x) {
                            uint8_t a = row[x*4 + 3];
                            if (a == 0) {
                                uint8_t b = row[x*4 + 0];
                                uint8_t g = row[x*4 + 1];
                                uint8_t r = row[x*4 + 2];
                                uint16_t lum = (uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11;
                                a = (uint8_t)(lum / 100);
                            }
                            alphaA[y * width + x] = a;
                        }
                    }
                    // Save resized mask for inspection
                    try { SaveSoftwareBitmapToLocalPngAsync(m, ref new Platform::String(L"input_mask_resized.png")); } catch(...) {}
                    try { SaveMaskVisualToLocalPngAsync(m, ref new Platform::String(L"input_mask_resized_vis.png")); } catch(...) {}
                } catch(...) {}
            }
        }

        // Read rounded mask's alpha
        {
            auto r = EnsureBgra8Premultiplied(rounded);
            auto rbuf = r->LockBuffer(BitmapBufferAccessMode::Read);
            auto rdesc = rbuf->GetPlaneDescription(0);
            auto rref = rbuf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> rma;
            IUnknown* runk = reinterpret_cast<IUnknown*>(rref);
            if (runk != nullptr && SUCCEEDED(runk->QueryInterface(IID_PPV_ARGS(&rma)))) {
                BYTE* data = nullptr; UINT32 cap = 0;
                auto raw = rma.Get();
                if (raw != nullptr && SUCCEEDED(raw->GetBuffer(&data, &cap))) {
                    for (unsigned int y = 0; y < (unsigned int)rdesc.Height && y < height; ++y) {
                        BYTE* row = data + rdesc.StartIndex + y * rdesc.Stride;
                        for (unsigned int x = 0; x < (unsigned int)rdesc.Width && x < width; ++x) {
                            uint8_t a = row[x*4 + 3];
                            if (a == 0) {
                                uint8_t b = row[x*4 + 0];
                                uint8_t g = row[x*4 + 1];
                                uint8_t r = row[x*4 + 2];
                                uint16_t lum = (uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11;
                                a = (uint8_t)(lum / 100);
                            }
                            alphaB[y * width + x] = a;
                        }
                    }
                }
            } else {
                try {
                    auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)rdesc.Height * rdesc.Stride);
                    r->CopyToBuffer(ib);
                    auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                    std::vector<uint8_t> tmp((size_t)rdesc.Height * rdesc.Stride);
                    reader->ReadBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
                    for (unsigned int y = 0; y < (unsigned int)rdesc.Height && y < height; ++y) {
                        uint8_t* row = tmp.data() + y * rdesc.Stride;
                        for (unsigned int x = 0; x < (unsigned int)rdesc.Width && x < width; ++x) {
                                uint8_t a = row[x*4 + 3];
                                if (a == 0) {
                                    uint8_t b = row[x*4 + 0];
                                    uint8_t g = row[x*4 + 1];
                                    uint8_t r = row[x*4 + 2];
                                    uint16_t lum = (uint16_t)r * 30 + (uint16_t)g * 59 + (uint16_t)b * 11;
                                    a = (uint8_t)(lum / 100);
                                }
                                alphaB[y * width + x] = a;
                            }
                    }
                    // try { SaveSoftwareBitmapToLocalPngAsync(r, ref new Platform::String(L"rounded_mask.png")); } catch(...) {}
                    // try { SaveMaskVisualToLocalPngAsync(r, ref new Platform::String(L"rounded_mask_vis.png")); } catch(...) {}
                } catch(...) {}
            }
        }

        // Log alpha non-zero counts for inputs
        try {
            uint64_t aCount = 0, bCount = 0;
            for (size_t i = 0; i < alphaA.size(); ++i) { if (alphaA[i] != 0) ++aCount; if (alphaB[i] != 0) ++bCount; }
            moonlight_xbox_dx::Utils::Logf("CombineMaskWithRoundedRect: input mask nonzero=%llu, rounded nonzero=%llu (total=%u)\n", aCount, bCount, width*height);
        } catch(...) {}

        // Create output mask (BGRA8 premultiplied) with alpha = (a*b)/255 and white RGB
        try {
            auto out = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
            auto outBuf = out->LockBuffer(BitmapBufferAccessMode::Write);
            auto plane = outBuf->GetPlaneDescription(0);
            auto outRef = outBuf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> oba;
            IUnknown* ounk = reinterpret_cast<IUnknown*>(outRef);
            bool wroteFast = false;
            if (ounk != nullptr && SUCCEEDED(ounk->QueryInterface(IID_PPV_ARGS(&oba)))) {
                BYTE* data = nullptr; UINT32 cap = 0;
                auto raw = oba.Get();
                if (raw != nullptr && SUCCEEDED(raw->GetBuffer(&data, &cap))) {
                    uint64_t nonzeroOut = 0;
                    for (unsigned int y = 0; y < height; ++y) {
                        BYTE* row = data + plane.StartIndex + y * plane.Stride;
                        for (unsigned int x = 0; x < width; ++x) {
                            uint8_t a = alphaA[y*width + x];
                            uint8_t b = alphaB[y*width + x];
                            uint8_t outa = (uint8_t)((uint16_t)a * (uint16_t)b / 255);
                            if (outa == 0) {
                                row[x*4 + 0] = 0; row[x*4 + 1] = 0; row[x*4 + 2] = 0; row[x*4 + 3] = 0;
                            } else {
                                row[x*4 + 0] = 255; row[x*4 + 1] = 255; row[x*4 + 2] = 255; row[x*4 + 3] = outa;
                            }
                            if (outa != 0) ++nonzeroOut;
                        }
                    }
                    try { moonlight_xbox_dx::Utils::Logf("CombineMaskWithRoundedRect: output nonzero coverage=%llu/%u\n", nonzeroOut, width*height); } catch(...) {}
                    // Save mask for inspection (non-blocking)
                    try { SaveSoftwareBitmapToLocalPngAsync(out, ref new Platform::String(L"combined_mask_fast.png")); } catch(...) {}
                    try { SaveMaskVisualToLocalPngAsync(out, ref new Platform::String(L"combined_mask_fast_vis.png")); } catch(...) {}
                    wroteFast = true;
                    return out;
                }
            }

            // Fallback: if direct buffer access isn't available, build a temporary buffer and CopyFromBuffer
            try {
                auto outDesc = outBuf->GetPlaneDescription(0);
                std::vector<uint8_t> tmp((size_t)height * (size_t)outDesc.Stride);
                uint64_t nonzeroOut = 0;
                for (unsigned int y = 0; y < height; ++y) {
                    uint8_t* row = tmp.data() + y * outDesc.Stride;
                    for (unsigned int x = 0; x < width; ++x) {
                        uint8_t a = alphaA[y*width + x];
                        uint8_t b = alphaB[y*width + x];
                        uint8_t outa = (uint8_t)((uint16_t)a * (uint16_t)b / 255);
                        if (outa == 0) {
                            row[x*4 + 0] = 0; row[x*4 + 1] = 0; row[x*4 + 2] = 0; row[x*4 + 3] = 0;
                        } else {
                            row[x*4 + 0] = 255; row[x*4 + 1] = 255; row[x*4 + 2] = 255; row[x*4 + 3] = outa;
                        }
                        if (outa != 0) ++nonzeroOut;
                    }
                    if (outDesc.Stride > width*4) memset(tmp.data() + y*outDesc.Stride + width*4, 0, outDesc.Stride - width*4);
                }
                // Release locks then copy
                outRef = nullptr; outBuf = nullptr;
                try {
                    auto writer = ref new Windows::Storage::Streams::DataWriter();
                    writer->WriteBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
                    auto buf = writer->DetachBuffer();
                    out->CopyFromBuffer(buf);
                    try { moonlight_xbox_dx::Utils::Logf("CombineMaskWithRoundedRect: fallback output nonzero coverage=%llu/%u\n", nonzeroOut, width*height); } catch(...) {}
                    try { SaveSoftwareBitmapToLocalPngAsync(out, ref new Platform::String(L"combined_mask_fallback.png")); } catch(...) {}
                    try { SaveMaskVisualToLocalPngAsync(out, ref new Platform::String(L"combined_mask_fallback_vis.png")); } catch(...) {}
                    return out;
                } catch(...) {
                    try { moonlight_xbox_dx::Utils::Log("CombineMaskWithRoundedRect: fallback CopyFromBuffer failed\n"); } catch(...) {}
                }
            } catch(...) {}
        } catch(...) {}

        return nullptr;
    } catch(...) {
        return nullptr;
    }
}

concurrency::task<void> ImageHelpers::SaveSoftwareBitmapToLocalPngAsync(SoftwareBitmap^ bmp, Platform::String^ filename) {
    if (bmp == nullptr || filename == nullptr) return concurrency::task_from_result();
    return concurrency::create_task([bmp, filename]() {
        try {
            auto local = ApplicationData::Current->LocalFolder;
            // Use GenerateUniqueName to avoid file-in-use collisions during repeated saves
            StorageFile^ file = concurrency::create_task(local->CreateFileAsync(filename, CreationCollisionOption::GenerateUniqueName)).get();
            try {
                IRandomAccessStream^ stream = concurrency::create_task(file->OpenAsync(FileAccessMode::ReadWrite)).get();
                BitmapEncoder^ encoder = concurrency::create_task(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId, stream)).get();

            SoftwareBitmap^ toEncode = bmp;
            try {
                if (bmp->BitmapPixelFormat != BitmapPixelFormat::Bgra8 || bmp->BitmapAlphaMode != BitmapAlphaMode::Straight) {
                    toEncode = SoftwareBitmap::Convert(bmp, BitmapPixelFormat::Bgra8, BitmapAlphaMode::Straight);
                }
            } catch(...) { toEncode = bmp; }

                try { encoder->SetSoftwareBitmap(toEncode); } catch(...) {}
                concurrency::create_task(encoder->FlushAsync()).get();
                try { moonlight_xbox_dx::Utils::Logf("SaveSoftwareBitmapToLocalPngAsync: saved %S\n", file->Name->Data()); } catch(...) {}
            } catch (Platform::Exception^ ex) {
                try { moonlight_xbox_dx::Utils::Logf("SaveSoftwareBitmapToLocalPngAsync: file open/encode failed %ls (%08x)\n", ex->Message->Data(), ex->HResult); } catch(...) {}
            }
        } catch (Platform::Exception^ ex) {
            try { moonlight_xbox_dx::Utils::Logf("SaveSoftwareBitmapToLocalPngAsync: exception %ls\n", ex->Message->Data()); } catch(...) {}
        } catch (...) {
            try { moonlight_xbox_dx::Utils::Log("SaveSoftwareBitmapToLocalPngAsync: unknown exception\n"); } catch(...) {}
        }
    });
}

concurrency::task<void> ImageHelpers::SaveMaskVisualToLocalPngAsync(SoftwareBitmap^ mask, Platform::String^ filename) {
    if (mask == nullptr || filename == nullptr) return concurrency::task_from_result();
    return concurrency::create_task([mask, filename]() {
        try {
            SoftwareBitmap^ m = EnsureBgra8Premultiplied(mask);
            unsigned int w = m->PixelWidth; unsigned int h = m->PixelHeight;
            auto buf = m->LockBuffer(BitmapBufferAccessMode::Read);
            auto desc = buf->GetPlaneDescription(0);
            std::vector<uint8_t> src((size_t)h * desc.Stride);
            try {
                auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)src.size());
                m->CopyToBuffer(ib);
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                reader->ReadBytes(Platform::ArrayReference<uint8_t>(src.data(), (unsigned int)src.size()));
            } catch(...) { return; }

            // Create a visual bitmap where RGB = alpha, alpha = 255
            auto vis = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, (int)w, (int)h, BitmapAlphaMode::Premultiplied);
            auto outBuf = vis->LockBuffer(BitmapBufferAccessMode::Write);
            auto outDesc = outBuf->GetPlaneDescription(0);
            std::vector<uint8_t> tmp((size_t)h * outDesc.Stride);
            for (unsigned int y = 0; y < h; ++y) {
                uint8_t* row = src.data() + y * desc.Stride;
                uint8_t* dst = tmp.data() + y * outDesc.Stride;
                for (unsigned int x = 0; x < w; ++x) {
                    uint8_t a = row[x*4 + 3];
                    dst[x*4 + 0] = a; dst[x*4 + 1] = a; dst[x*4 + 2] = a; dst[x*4 + 3] = 255;
                }
            }
            try {
                auto writer = ref new Windows::Storage::Streams::DataWriter();
                writer->WriteBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
                auto bufOut = writer->DetachBuffer();
                outBuf = nullptr; // release
                vis->CopyFromBuffer(bufOut);
                // Save vis
                auto local = ApplicationData::Current->LocalFolder;
                StorageFile^ file = concurrency::create_task(local->CreateFileAsync(filename, CreationCollisionOption::GenerateUniqueName)).get();
                IRandomAccessStream^ stream = concurrency::create_task(file->OpenAsync(FileAccessMode::ReadWrite)).get();
                BitmapEncoder^ encoder = concurrency::create_task(BitmapEncoder::CreateAsync(BitmapEncoder::PngEncoderId, stream)).get();
                try { encoder->SetSoftwareBitmap(vis); } catch(...) {}
                concurrency::create_task(encoder->FlushAsync()).get();
            } catch(...) {}
        } catch(...) {}
    });
}


SoftwareBitmap^ ImageHelpers::ResizeSoftwareBitmap(SoftwareBitmap^ src, unsigned int width, unsigned int height) {
    if (src == nullptr) return nullptr;
    try {
        src = EnsureBgra8Premultiplied(src);
        unsigned int srcW = src->PixelWidth;
        unsigned int srcH = src->PixelHeight;
        if (srcW == width && srcH == height) return src;

        auto outBmp = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);

        // Lock buffers
        auto srcBuf = src->LockBuffer(BitmapBufferAccessMode::Read);
        auto srcDesc = srcBuf->GetPlaneDescription(0);
        auto dstBuf = outBmp->LockBuffer(BitmapBufferAccessMode::Write);
        auto dstDesc = dstBuf->GetPlaneDescription(0);

        // Try fast-path access to both buffers
        auto srcRef = srcBuf->CreateReference();
        auto dstRef = dstBuf->CreateReference();
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> srcAccess;
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> dstAccess;
        IUnknown* srcUnk = reinterpret_cast<IUnknown*>(srcRef);
        IUnknown* dstUnk = reinterpret_cast<IUnknown*>(dstRef);
        BYTE* srcData = nullptr; UINT32 srcCap = 0;
        BYTE* dstData = nullptr; UINT32 dstCap = 0;
        bool haveFast = false;
        if (srcUnk != nullptr && dstUnk != nullptr && SUCCEEDED(srcUnk->QueryInterface(IID_PPV_ARGS(&srcAccess))) && SUCCEEDED(dstUnk->QueryInterface(IID_PPV_ARGS(&dstAccess)))) {
            if (SUCCEEDED(srcAccess->GetBuffer(&srcData, &srcCap)) && SUCCEEDED(dstAccess->GetBuffer(&dstData, &dstCap))) {
                haveFast = true;
            }
        }

        if (haveFast) {
            for (unsigned int y = 0; y < height; ++y) {
                unsigned int sy = (unsigned int)((uint64_t)y * srcH / height);
                uint8_t* srcRow = srcData + srcDesc.StartIndex + sy * srcDesc.Stride;
                uint8_t* dstRow = dstData + dstDesc.StartIndex + y * dstDesc.Stride;
                for (unsigned int x = 0; x < width; ++x) {
                    unsigned int sx = (unsigned int)((uint64_t)x * srcW / width);
                    uint8_t* pSrc = srcRow + sx * 4;
                    uint8_t* pDst = dstRow + x * 4;
                    pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = pSrc[3];
                }
            }
            return outBmp;
        }

        // Fallback: read src into a temporary buffer then write scaled into dst via DataWriter
        std::vector<uint8_t> srcBufData((size_t)srcH * (size_t)srcDesc.Stride);
        try {
            auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)srcBufData.size());
            src->CopyToBuffer(ib);
            auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
            reader->ReadBytes(Platform::ArrayReference<uint8_t>(srcBufData.data(), (unsigned int)srcBufData.size()));
        } catch(...) {
            moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmap: failed to read src buffer\n");
            return nullptr;
        }

        std::vector<uint8_t> dstTmp((size_t)height * (size_t)dstDesc.Stride);
        for (unsigned int y = 0; y < height; ++y) {
            unsigned int sy = (unsigned int)((uint64_t)y * srcH / height);
            uint8_t* srcRow = srcBufData.data() + sy * srcDesc.Stride;
            uint8_t* dstRow = dstTmp.data() + y * dstDesc.Stride;
            for (unsigned int x = 0; x < width; ++x) {
                unsigned int sx = (unsigned int)((uint64_t)x * srcW / width);
                uint8_t* pSrc = srcRow + sx * 4;
                uint8_t* pDst = dstRow + x * 4;
                pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = pSrc[3];
            }
        }

        try {
            auto writer = ref new Windows::Storage::Streams::DataWriter();
            writer->WriteBytes(Platform::ArrayReference<uint8_t>(dstTmp.data(), (unsigned int)dstTmp.size()));
            auto buf = writer->DetachBuffer();
            // release buffer locks
            dstRef = nullptr; dstBuf = nullptr;
            outBmp->CopyFromBuffer(buf);
            return outBmp;
        } catch(...) {
            moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmap: failed to write dst buffer\n");
            return nullptr;
        }
    } catch(...) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmap: exception during resize\n");
        return nullptr;
    }
}

// Center-crop src to match target aspect ratio (UniformToFill), then scale to target size.
SoftwareBitmap^ ImageHelpers::ResizeSoftwareBitmapUniformToFill(SoftwareBitmap^ src, unsigned int width, unsigned int height) {
    if (src == nullptr) return nullptr;
    try {
        unsigned int srcW = src->PixelWidth;
        unsigned int srcH = src->PixelHeight;
        if (srcW == 0 || srcH == 0) return nullptr;
        double srcAspect = (double)srcW / (double)srcH;
        double dstAspect = (double)width / (double)height;

        // Determine crop rectangle in source pixels
        unsigned int cropX = 0, cropY = 0, cropW = srcW, cropH = srcH;
        if (srcAspect > dstAspect) {
            // source is wider: crop horizontally
            cropH = srcH;
            cropW = (unsigned int)std::ceil(dstAspect * (double)cropH);
            if (cropW > srcW) cropW = srcW;
            cropX = (srcW - cropW) / 2;
            cropY = 0;
        } else if (srcAspect < dstAspect) {
            // source is taller: crop vertically
            cropW = srcW;
            cropH = (unsigned int)std::ceil((double)cropW / dstAspect);
            if (cropH > srcH) cropH = srcH;
            cropY = (srcH - cropH) / 2;
            cropX = 0;
        } else {
            // same aspect: no crop
        }

        // If no crop needed and size matches, delegate to ResizeSoftwareBitmap
        if (cropX == 0 && cropY == 0 && cropW == srcW && cropH == srcH) {
            return ResizeSoftwareBitmap(src, width, height);
        }

        // Create an intermediate SoftwareBitmap for the cropped area
        auto cropped = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, cropW, cropH, BitmapAlphaMode::Premultiplied);
        // Lock buffers and copy pixels from src to cropped
        auto srcBuf = src->LockBuffer(BitmapBufferAccessMode::Read);
        auto srcDesc = srcBuf->GetPlaneDescription(0);
        auto dstBuf = cropped->LockBuffer(BitmapBufferAccessMode::Write);
        auto dstDesc = dstBuf->GetPlaneDescription(0);

        try {
            // Fallback path: read src into temp and write cropped
            std::vector<uint8_t> srcData((size_t)srcH * (size_t)srcDesc.Stride);
            try {
                auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)srcData.size());
                src->CopyToBuffer(ib);
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
                reader->ReadBytes(Platform::ArrayReference<uint8_t>(srcData.data(), (unsigned int)srcData.size()));
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmapUniformToFill: failed to read src buffer\n");
                return nullptr;
            }

            std::vector<uint8_t> dstTmp((size_t)cropH * (size_t)dstDesc.Stride);
            for (unsigned int y = 0; y < cropH; ++y) {
                uint8_t* srcRow = srcData.data() + (size_t)(cropY + y) * srcDesc.Stride;
                uint8_t* dstRow = dstTmp.data() + (size_t)y * dstDesc.Stride;
                for (unsigned int x = 0; x < cropW; ++x) {
                    uint8_t* pSrc = srcRow + (size_t)(cropX + x) * 4;
                    uint8_t* pDst = dstRow + (size_t)x * 4;
                    pDst[0] = pSrc[0]; pDst[1] = pSrc[1]; pDst[2] = pSrc[2]; pDst[3] = pSrc[3];
                }
            }

            try {
                auto writer = ref new Windows::Storage::Streams::DataWriter();
                writer->WriteBytes(Platform::ArrayReference<uint8_t>(dstTmp.data(), (unsigned int)dstTmp.size()));
                auto buf = writer->DetachBuffer();
                dstBuf = nullptr; // release lock
                cropped->CopyFromBuffer(buf);
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmapUniformToFill: failed to write cropped buffer\n");
                return nullptr;
            }
        } catch(...) {
            moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmapUniformToFill: unexpected exception\n");
            return nullptr;
        }

        // Now scale cropped to target size using existing ResizeSoftwareBitmap
        return ResizeSoftwareBitmap(cropped, width, height);
    } catch(...) {
        moonlight_xbox_dx::Utils::Log("ImageHelpers::ResizeSoftwareBitmapUniformToFill: exception during operation\n");
        return nullptr;
    }
}

// Create a rounded-rect alpha mask. White RGB, alpha equals mask coverage (0..255).
SoftwareBitmap^ ImageHelpers::CreateRoundedRectMask(unsigned int width, unsigned int height, float radiusPx) {
    if (width == 0 || height == 0) return nullptr;
    try {
        auto out = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
        auto outBuf = out->LockBuffer(BitmapBufferAccessMode::Write);
        auto outDesc = outBuf->GetPlaneDescription(0);
        auto outRef = outBuf->CreateReference();
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> outAccess;
        IUnknown* outUnk = reinterpret_cast<IUnknown*>(outRef);
        BYTE* outData = nullptr; UINT32 outCap = 0;
        if (outUnk != nullptr && SUCCEEDED(outUnk->QueryInterface(IID_PPV_ARGS(&outAccess)))) {
            if (SUCCEEDED(outAccess->GetBuffer(&outData, &outCap)) && outData != nullptr) {
                // Fill with transparent black first
                size_t stride = outDesc.Stride;
                for (unsigned int y = 0; y < height; ++y) {
                    uint8_t* row = outData + outDesc.StartIndex + (size_t)y * stride;
                    for (unsigned int x = 0; x < width; ++x) {
                        row[x*4 + 0] = 0; // B
                        row[x*4 + 1] = 0; // G
                        row[x*4 + 2] = 0; // R
                        row[x*4 + 3] = 0; // A
                    }
                }

                // Draw filled rounded rect by sampling distance to corner and filling inside
                // For simplicity use integer rasterization: mark pixels whose center is inside rounded rect
                float rx = radiusPx;
                float ry = radiusPx;
                for (unsigned int y = 0; y < height; ++y) {
                    for (unsigned int x = 0; x < width; ++x) {
                        float fx = (float)x + 0.5f;
                        float fy = (float)y + 0.5f;
                        bool inside = false;
                        // Inner rect
                        if (fx >= rx && fx <= (float)width - rx && fy >= ry && fy <= (float)height - ry) inside = true;
                        else {
                            // check corner circles
                            float cx = fx < rx ? rx : (fx > (float)width - rx ? (float)width - rx : fx);
                            float cy = fy < ry ? ry : (fy > (float)height - ry ? (float)height - ry : fy);
                            float dx = fx - cx;
                            float dy = fy - cy;
                            if (dx*dx + dy*dy <= rx*rx) inside = true;
                        }
                        if (inside) {
                            uint8_t* p = outData + outDesc.StartIndex + (size_t)y * outDesc.Stride + (size_t)x * 4;
                            p[0] = 255; p[1] = 255; p[2] = 255; p[3] = 255;
                        }
                    }
                }
                // Release buffer locks/references before returning the SoftwareBitmap
                outAccess = nullptr;
                outUnk = nullptr;
                outRef = nullptr;
                outBuf = nullptr;
                return out;
            }
        }
        // Fallback: create buffer via DataWriter
        try {
            std::vector<uint8_t> tmp((size_t)height * (size_t)outDesc.Stride);
            for (unsigned int y = 0; y < height; ++y) {
                uint8_t* row = tmp.data() + (size_t)y * outDesc.Stride;
                for (unsigned int x = 0; x < width; ++x) {
                    row[x*4 + 0] = 0; row[x*4 + 1] = 0; row[x*4 + 2] = 0; row[x*4 + 3] = 0;
                }
            }
            float rx = radiusPx; float ry = radiusPx;
            for (unsigned int y = 0; y < height; ++y) {
                for (unsigned int x = 0; x < width; ++x) {
                    float fx = (float)x + 0.5f;
                    float fy = (float)y + 0.5f;
                    bool inside = false;
                    if (fx >= rx && fx <= (float)width - rx && fy >= ry && fy <= (float)height - ry) inside = true;
                    else {
                        float cx = fx < rx ? rx : (fx > (float)width - rx ? (float)width - rx : fx);
                        float cy = fy < ry ? ry : (fy > (float)height - ry ? (float)height - ry : fy);
                        float dx = fx - cx; float dy = fy - cy;
                        if (dx*dx + dy*dy <= rx*rx) inside = true;
                    }
                    if (inside) {
                        uint8_t* p = tmp.data() + (size_t)y * outDesc.Stride + (size_t)x * 4;
                        p[0] = 255; p[1] = 255; p[2] = 255; p[3] = 255;
                    }
                }
            }
            auto writer = ref new Windows::Storage::Streams::DataWriter();
            writer->WriteBytes(Platform::ArrayReference<uint8_t>(tmp.data(), (unsigned int)tmp.size()));
            auto buf = writer->DetachBuffer();
            // Ensure any existing locks/references are released before calling CopyFromBuffer
            outRef = nullptr; outBuf = nullptr; outAccess = nullptr; outUnk = nullptr;
            try {
                out->CopyFromBuffer(buf);
                return out;
            } catch(...) {
                return nullptr;
            }
        } catch(...) {
            return nullptr;
        }
    } catch(...) {
        return nullptr;
    }
}

unsigned int ImageHelpers::CreateAverageColorArgb(SoftwareBitmap^ src) {
    if (src == nullptr) return 0;
    try {
        auto s = EnsureBgra8Premultiplied(src);
        auto buf = s->LockBuffer(BitmapBufferAccessMode::Read);
        auto desc = buf->GetPlaneDescription(0);
        std::vector<uint8_t> data((size_t)s->PixelHeight * desc.Stride);
        try {
            auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)data.size());
            s->CopyToBuffer(ib);
            auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
            reader->ReadBytes(Platform::ArrayReference<uint8_t>(data.data(), (unsigned int)data.size()));
        } catch(...) { return 0; }

        uint64_t totalR = 0, totalG = 0, totalB = 0, totalA = 0;
        uint64_t count = 0;
        unsigned int w = s->PixelWidth; unsigned int h = s->PixelHeight;
        for (unsigned int y = 0; y < h; ++y) {
            uint8_t* row = data.data() + (size_t)y * desc.Stride;
            for (unsigned int x = 0; x < w; ++x) {
                uint8_t b = row[x*4 + 0];
                uint8_t g = row[x*4 + 1];
                uint8_t r = row[x*4 + 2];
                uint8_t a = row[x*4 + 3];
                // ignore fully transparent pixels from averaging
                if (a == 0) continue;
                // Un-premultiply to approximate original color
                uint32_t ur = (uint32_t)r * 255 / std::max<unsigned int>(1, a);
                uint32_t ug = (uint32_t)g * 255 / std::max<unsigned int>(1, a);
                uint32_t ub = (uint32_t)b * 255 / std::max<unsigned int>(1, a);
                totalR += ur; totalG += ug; totalB += ub; totalA += a; ++count;
            }
        }
        if (count == 0) return 0xFF000000; // default black
        uint8_t avgR = (uint8_t)std::min<uint64_t>(255, totalR / count);
        uint8_t avgG = (uint8_t)std::min<uint64_t>(255, totalG / count);
        uint8_t avgB = (uint8_t)std::min<uint64_t>(255, totalB / count);
        // Use averaged alpha approximate (clamp to 255)
        uint8_t avgA = (uint8_t)std::min<uint64_t>(255, totalA / count);
        unsigned int argb = ((unsigned int)avgA << 24) | ((unsigned int)avgR << 16) | ((unsigned int)avgG << 8) | (unsigned int)avgB;
        return argb;
    } catch(...) { return 0; }
}

// Helper: convert RGB [0,255] to HSL (h in [0,360), s,l in [0,1])
static void RgbToHsl(uint8_t r, uint8_t g, uint8_t b, float &h, float &s, float &l) {
    float rf = r / 255.0f; float gf = g / 255.0f; float bf = b / 255.0f;
    float maxv = std::max(std::max(rf, gf), bf);
    float minv = std::min(std::min(rf, gf), bf);
    l = (maxv + minv) * 0.5f;
    if (maxv == minv) { h = 0.0f; s = 0.0f; return; }
    float d = maxv - minv;
    s = l > 0.5f ? d / (2.0f - maxv - minv) : d / (maxv + minv);
    if (maxv == rf) h = (gf - bf) / d + (gf < bf ? 6.0f : 0.0f);
    else if (maxv == gf) h = (bf - rf) / d + 2.0f;
    else h = (rf - gf) / d + 4.0f;
    h *= 60.0f;
}

static float HueToRgb(float p, float q, float t) {
    if (t < 0.0f) t += 1.0f;
    if (t > 1.0f) t -= 1.0f;
    if (t < 1.0f/6.0f) return p + (q - p) * 6.0f * t;
    if (t < 1.0f/2.0f) return q;
    if (t < 2.0f/3.0f) return p + (q - p) * (2.0f/3.0f - t) * 6.0f;
    return p;
}

static void HslToRgb(float h, float s, float l, uint8_t &r, uint8_t &g, uint8_t &b) {
    if (s == 0.0f) {
        uint8_t v = (uint8_t)std::round(l * 255.0f);
        r = g = b = v; return;
    }
    float hf = h / 360.0f;
    float q = l < 0.5f ? l * (1.0f + s) : l + s - l * s;
    float p = 2.0f * l - q;
    float rf = HueToRgb(p, q, hf + 1.0f/3.0f);
    float gf = HueToRgb(p, q, hf);
    float bf = HueToRgb(p, q, hf - 1.0f/3.0f);
    r = (uint8_t)std::round(std::min(1.0f, std::max(0.0f, rf)) * 255.0f);
    g = (uint8_t)std::round(std::min(1.0f, std::max(0.0f, gf)) * 255.0f);
    b = (uint8_t)std::round(std::min(1.0f, std::max(0.0f, bf)) * 255.0f);
}

bool ImageHelpers::AdjustSaturation(SoftwareBitmap^ bmp, float saturation) {
    if (bmp == nullptr) return false;
    try {
        auto b = EnsureBgra8Premultiplied(bmp);
        if (b == nullptr) return false;
        auto buf = b->LockBuffer(BitmapBufferAccessMode::ReadWrite);
        auto desc = buf->GetPlaneDescription(0);
        unsigned int w = b->PixelWidth; unsigned int h = b->PixelHeight;
        // Quick exit when saturation is identity
        if (saturation == 1.0f) return true;

        // We'll use a fast luma-interpolation approach instead of full HSL conversion.
        // new = gray + sat*(orig - gray) where gray = 0.299*R + 0.587*G + 0.114*B
        const float satf = saturation;

        // Try fast-path direct memory access via IMemoryBufferByteAccess while buffer is locked
        try {
            auto ref = buf->CreateReference();
            Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> access;
            IUnknown* unk = reinterpret_cast<IUnknown*>(ref);
            if (unk != nullptr && SUCCEEDED(unk->QueryInterface(IID_PPV_ARGS(&access)))) {
                BYTE* raw = nullptr; UINT32 cap = 0;
                if (SUCCEEDED(access->GetBuffer(&raw, &cap)) && raw != nullptr) {
                    for (unsigned int y = 0; y < h; ++y) {
                        uint8_t* row = raw + desc.StartIndex + (size_t)y * desc.Stride;
                        for (unsigned int x = 0; x < w; ++x) {
                            uint8_t b_px = row[x*4 + 0];
                            uint8_t g_px = row[x*4 + 1];
                            uint8_t r_px = row[x*4 + 2];
                            uint8_t a_px = row[x*4 + 3];
                            if (a_px == 0) continue;

                            // Un-premultiply
                            unsigned int invA = std::max<unsigned int>(1, a_px);
                            float ur = (float)((uint32_t)r_px * 255u) / (float)invA;
                            float ug = (float)((uint32_t)g_px * 255u) / (float)invA;
                            float ub = (float)((uint32_t)b_px * 255u) / (float)invA;

                            // Luminance (perceptual)
                            float gray = ur * 0.299f + ug * 0.587f + ub * 0.114f;

                            // Interpolate towards/away from gray
                            float nrf = gray + satf * (ur - gray);
                            float ngf = gray + satf * (ug - gray);
                            float nbf = gray + satf * (ub - gray);

                            // Clamp and premultiply back
                            int nri = (int)std::round(nrf);
                            int ngi = (int)std::round(ngf);
                            int nbi = (int)std::round(nbf);
                            nri = nri < 0 ? 0 : (nri > 255 ? 255 : nri);
                            ngi = ngi < 0 ? 0 : (ngi > 255 ? 255 : ngi);
                            nbi = nbi < 0 ? 0 : (nbi > 255 ? 255 : nbi);

                            uint8_t pr = (uint8_t)((uint32_t)nri * a_px / 255u);
                            uint8_t pg = (uint8_t)((uint32_t)ngi * a_px / 255u);
                            uint8_t pb = (uint8_t)((uint32_t)nbi * a_px / 255u);

                            row[x*4 + 0] = pb; row[x*4 + 1] = pg; row[x*4 + 2] = pr; row[x*4 + 3] = a_px;
                        }
                    }
                    return true;
                }
            }
        } catch(...) {}

        // Fallback: copy to a buffer, process, then copy back
        try {
            buf = nullptr; // release the lock before CopyToBuffer
            std::vector<uint8_t> data((size_t)h * desc.Stride);
            auto ib = ref new Windows::Storage::Streams::Buffer((unsigned int)data.size());
            b->CopyToBuffer(ib);
            auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ib);
            reader->ReadBytes(Platform::ArrayReference<uint8_t>(data.data(), (unsigned int)data.size()));

            for (unsigned int y = 0; y < h; ++y) {
                uint8_t* row = data.data() + (size_t)y * desc.Stride;
                for (unsigned int x = 0; x < w; ++x) {
                    uint8_t b_px = row[x*4 + 0];
                    uint8_t g_px = row[x*4 + 1];
                    uint8_t r_px = row[x*4 + 2];
                    uint8_t a_px = row[x*4 + 3];
                    if (a_px == 0) continue;

                    unsigned int invA = std::max<unsigned int>(1, a_px);
                    float ur = (float)((uint32_t)r_px * 255u) / (float)invA;
                    float ug = (float)((uint32_t)g_px * 255u) / (float)invA;
                    float ub = (float)((uint32_t)b_px * 255u) / (float)invA;

                    float gray = ur * 0.299f + ug * 0.587f + ub * 0.114f;

                    float nrf = gray + satf * (ur - gray);
                    float ngf = gray + satf * (ug - gray);
                    float nbf = gray + satf * (ub - gray);

                    int nri = (int)std::round(nrf);
                    int ngi = (int)std::round(ngf);
                    int nbi = (int)std::round(nbf);
                    nri = nri < 0 ? 0 : (nri > 255 ? 255 : nri);
                    ngi = ngi < 0 ? 0 : (ngi > 255 ? 255 : ngi);
                    nbi = nbi < 0 ? 0 : (nbi > 255 ? 255 : nbi);

                    uint8_t pr = (uint8_t)((uint32_t)nri * a_px / 255u);
                    uint8_t pg = (uint8_t)((uint32_t)ngi * a_px / 255u);
                    uint8_t pb = (uint8_t)((uint32_t)nbi * a_px / 255u);

                    row[x*4 + 0] = pb; row[x*4 + 1] = pg; row[x*4 + 2] = pr; row[x*4 + 3] = a_px;
                }
            }

            // Write back without any locks
            auto writer = ref new Windows::Storage::Streams::DataWriter();
            writer->WriteBytes(Platform::ArrayReference<uint8_t>(data.data(), (unsigned int)data.size()));
            auto outBuf = writer->DetachBuffer();
            b->CopyFromBuffer(outBuf);
            return true;
        } catch(...) { return false; }
    } catch(...) { return false; }
}

concurrency::task<Windows::Storage::Streams::IRandomAccessStream^> ImageHelpers::CreateMaskedBlurredPngStreamAsync(
    SoftwareBitmap^ src,
    SoftwareBitmap^ mask,
    unsigned int targetW,
    unsigned int targetH,
    double dpi,
    float blurDip) {
    if (src == nullptr) return task_from_result<IRandomAccessStream^>(nullptr);

    try {
        // Determine target size
        unsigned int tW = targetW != 0 ? targetW : src->PixelWidth;
        unsigned int tH = targetH != 0 ? targetH : src->PixelHeight;

        // Resize source to target if needed
        auto raster = ResizeSoftwareBitmapUniformToFill(src, tW, tH);
        if (raster != nullptr) src = raster;

        // Compute blur radius in pixels from DIP
        unsigned int radiusPx = 0;
        try { radiusPx = (unsigned int)std::round((double)blurDip * dpi / 96.0); } catch(...) { radiusPx = (unsigned int)std::round((double)blurDip); }

        // Apply mask if provided, otherwise generate rounded rect mask
        SoftwareBitmap^ preBlurBitmap = src;
        if (mask != nullptr) {
            unsigned int ml=0, mt=0, mw=0, mh=0;
            // Try to detect non-empty alpha bounds in mask by reusing CompositeWithMask path (it checks sizes)
			// preBlurBitmap = CompositeWithMask(src, mask, radiusPx);
        } else {
            auto maskGen = CreateRoundedRectMask(src->PixelWidth, src->PixelHeight, (float)radiusPx);
            if (maskGen != nullptr) {
				// preBlurBitmap = CompositeWithMask(src, maskGen, radiusPx);
            }
        }

        // Try GPU blur first
        try {
            auto gpuResult = ::EffectsLibrary::GpuBoxBlurSoftwareBitmap(preBlurBitmap, (int)radiusPx, false, true);
            if (gpuResult != nullptr) {
                // Adjust saturation on the blurred result to allow styling without Win2D
                try { AdjustSaturation(gpuResult, 1.0f); } catch(...) {}
                return create_task(EncodeSoftwareBitmapToPngStreamAsync(gpuResult)).then([](IRandomAccessStream^ s) -> IRandomAccessStream^ {
                    if (s != nullptr) { try { s->Seek(0); } catch(...) {} }
                    return s;
                });
            }
        } catch(...) {}

        // CPU fallback
        try {
            auto cpuTarget = preBlurBitmap;
            ::EffectsLibrary::BoxBlurSoftwareBitmap(cpuTarget, (int)radiusPx);
            try { AdjustSaturation(cpuTarget, 1.0f); } catch(...) {}
            return create_task(EncodeSoftwareBitmapToPngStreamAsync(cpuTarget)).then([](IRandomAccessStream^ s) -> IRandomAccessStream^ {
                if (s != nullptr) { try { s->Seek(0); } catch(...) {} }
                return s;
            });
        } catch(...) {}

        return task_from_result<IRandomAccessStream^>(nullptr);
    } catch(...) {
        return task_from_result<IRandomAccessStream^>(nullptr);
    }
}
