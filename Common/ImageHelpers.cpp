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
