#include "pch.h"
#include "EffectsLibrary.h"
#include <DirectXColors.h>
#include <wrl.h>
#include <robuffer.h>
#include "../Utils.hpp"
#include "DirectXHelper.h"
#include <chrono>
#include <vector>
#include <mutex>
#include "ImageHelpers.h"
using namespace Windows::Graphics::Imaging;

// Helper to access buffer bytes for SoftwareBitmap
struct DECLSPEC_UUID("5B0D3235-4DBA-4D44-865E-8F1D0ED9F3E4") IMemoryBufferByteAccess : IUnknown {
    virtual HRESULT STDMETHODCALLTYPE GetBuffer(BYTE** value, UINT32* capacity) = 0;
};

ID3D11Device* EffectsLibrary::m_device = nullptr;
ID3D11DeviceContext* EffectsLibrary::m_context = nullptr;
ID3D11Multithread* EffectsLibrary::m_multithread = nullptr;
ID3D11VertexShader* EffectsLibrary::m_vs = nullptr;
ID3D11PixelShader* EffectsLibrary::m_blurPS = nullptr;
ID3D11InputLayout* EffectsLibrary::m_inputLayout = nullptr;
ID3D11Buffer* EffectsLibrary::m_quadVB = nullptr;
ID3D11Buffer* EffectsLibrary::m_cb = nullptr;
ID3D11SamplerState* EffectsLibrary::m_sampler = nullptr;
std::mutex EffectsLibrary::m_mutex;
bool EffectsLibrary::m_ownsDevice = false;

void EffectsLibrary::Initialize(ID3D11Device* device, ID3D11DeviceContext* context)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    // Idempotent GPU init - if same device/context, no-op.
    if (m_device == device && m_context == context)
        return;

    // If we previously created the device/context ourselves (m_ownsDevice==true)
    // then we are responsible for releasing the COM pointers. If the device
    // was supplied by the host (m_ownsDevice==false), do not release it here.
    if (m_vs) { m_vs->Release(); m_vs = nullptr; }
    if (m_blurPS) { m_blurPS->Release(); m_blurPS = nullptr; }
    if (m_inputLayout) { m_inputLayout->Release(); m_inputLayout = nullptr; }
    if (m_quadVB) { m_quadVB->Release(); m_quadVB = nullptr; }
    if (m_cb) { m_cb->Release(); m_cb = nullptr; }
    if (m_sampler) { m_sampler->Release(); m_sampler = nullptr; }

    // If we previously owned a lazily-created device/context, release them
    if (m_ownsDevice) {
        if (m_context) { m_context->Release(); m_context = nullptr; }
        if (m_device) { m_device->Release(); m_device = nullptr; }
        if (m_multithread) { m_multithread->SetMultithreadProtected(FALSE); m_multithread->Release(); m_multithread = nullptr; }
        m_ownsDevice = false;
    }

    m_device = device;
    m_context = context;
    // Device provided by host: we do not own it.
    m_ownsDevice = false;
    if (m_multithread) { m_multithread->SetMultithreadProtected(FALSE); m_multithread->Release(); m_multithread = nullptr; }
    moonlight_xbox_dx::Utils::Logf("EffectsLibrary::Initialize called. device=%p context=%p\n", (void*)device, (void*)context);
}

// Try to create a local D3D11 device/context if none has been provided by the app.
bool EffectsLibrary::EnsureDeviceInitialized()
{
    // Prefer host-provided device/context. If absent, allow a single-thread
    // safe lazy creation so GPU path can still work in scenarios where the
    // host doesn't call Initialize(). This path is guarded by m_mutex and
    // sets m_ownsDevice so the library can release the device later.
    if (m_device != nullptr && m_context != nullptr) return true;

    std::lock_guard<std::mutex> lock(m_mutex);
    if (m_device != nullptr && m_context != nullptr) return true;

    // Create device/context once per-process if needed. Use BGRA support and
    // feature level 11_0 minimum to match app expectations.
    D3D_FEATURE_LEVEL featureLevels[] = { D3D_FEATURE_LEVEL_11_0, D3D_FEATURE_LEVEL_10_1, D3D_FEATURE_LEVEL_10_0 };
    ID3D11Device* dev = nullptr;
    ID3D11DeviceContext* ctx = nullptr;
    D3D_FEATURE_LEVEL createdFL = D3D_FEATURE_LEVEL_11_0;
    UINT flags = D3D11_CREATE_DEVICE_BGRA_SUPPORT;
#if defined(_DEBUG)
    flags |= D3D11_CREATE_DEVICE_DEBUG;
#endif
    HRESULT hr = D3D11CreateDevice(nullptr, D3D_DRIVER_TYPE_HARDWARE, nullptr, flags, featureLevels, _countof(featureLevels),
        D3D11_SDK_VERSION, &dev, &createdFL, &ctx);
    if (FAILED(hr) || dev == nullptr || ctx == nullptr) {
        moonlight_xbox_dx::Utils::Logf("EffectsLibrary: lazy D3D11CreateDevice failed hr=0x%08x\n", hr);
        // leave GPU path disabled
        return false;
    }
    // Store and mark ownership
    m_device = dev;
    m_context = ctx;
    // Enable multithread protection for the device we own so D3D runtime
    // performs internal synchronization. Store the interface for later.
    Microsoft::WRL::ComPtr<ID3D11Multithread> mt;
    if (SUCCEEDED(m_device->QueryInterface(__uuidof(ID3D11Multithread), reinterpret_cast<void**>(mt.GetAddressOf())))) {
        mt->SetMultithreadProtected(TRUE);
        // keep a raw pointer ref for cleanup
        mt.Get()->AddRef();
        m_multithread = mt.Get();
    }
    m_ownsDevice = true;
    moonlight_xbox_dx::Utils::Logf("EffectsLibrary: lazy-created device=%p context=%p\n", (void*)m_device, (void*)m_context);
    return true;
}

// Helper: ensure SoftwareBitmap is Bgra8 Premultiplied
static SoftwareBitmap^ EnsureBgra8Premultiplied(Windows::Graphics::Imaging::SoftwareBitmap^ bmp)
{
    if (bmp == nullptr) return nullptr;
    if (bmp->BitmapPixelFormat == BitmapPixelFormat::Bgra8 && bmp->BitmapAlphaMode == BitmapAlphaMode::Premultiplied)
        return bmp;
    return ImageHelpers::EnsureBgra8Premultiplied(bmp);
}

// CPU two-pass separable box blur using sliding-window (integral-like) method
bool EffectsLibrary::BoxBlurSoftwareBitmap(Windows::Graphics::Imaging::SoftwareBitmap^ bitmap, int radius) {
    using namespace Windows::Graphics::Imaging;
    if (bitmap == nullptr) return false;
    try {
        auto buffer = bitmap->LockBuffer(BitmapBufferAccessMode::ReadWrite);
        auto reference = buffer->CreateReference();
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> bufferByteAccess;
        HRESULT hr = S_OK;
        IUnknown* unk = reinterpret_cast<IUnknown*>(reference);
        if (unk == nullptr) {
            moonlight_xbox_dx::Utils::Log("BoxBlurSoftwareBitmap: CreateReference returned null IUnknown\n");
            return false;
        }
        hr = unk->QueryInterface(IID_PPV_ARGS(&bufferByteAccess));
        BYTE* data = nullptr; UINT32 capacity = 0;
        bool usedFastPath = false;
        if (!FAILED(hr) && bufferByteAccess != nullptr) {
            hr = bufferByteAccess->GetBuffer(&data, &capacity);
            if (!FAILED(hr) && data != nullptr) {
                usedFastPath = true;
            } else {
                moonlight_xbox_dx::Utils::Logf("BoxBlurSoftwareBitmap: GetBuffer failed hr=0x%08x\n", hr);
            }
        } else {
            moonlight_xbox_dx::Utils::Logf("BoxBlurSoftwareBitmap: QueryInterface failed hr=0x%08x\n", hr);
        }
        auto desc = buffer->GetPlaneDescription(0);
        int width = desc.Width;
        int height = desc.Height;
        int stride = desc.Stride;
        int start = desc.StartIndex;
        if (width <= 0 || height <= 0 || stride <= 0) return false;
        size_t planeSize = (size_t)height * (size_t)stride;
        std::vector<uint8_t> src(planeSize);
        if (usedFastPath) {
            memcpy(src.data(), data + start, planeSize);
        } else {
            try {
                reference = nullptr;
                buffer = nullptr;
            } catch(...) {}
            try {
                auto ibuf = ref new Windows::Storage::Streams::Buffer((unsigned int)planeSize);
                bitmap->CopyToBuffer(ibuf);
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ibuf);
                reader->ReadBytes(Platform::ArrayReference<uint8_t>(src.data(), (unsigned int)planeSize));
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("BoxBlurSoftwareBitmap: fallback CopyToBuffer/DataReader failed\n");
                return false;
            }
        }
        // Separable two-pass box blur (horizontal then vertical) using prefix sums.
        std::vector<uint8_t> tmp(src.size());
        // Timing: measure blur duration
        auto t0 = std::chrono::high_resolution_clock::now();
        std::vector<uint8_t> dst(src.size());
        // Horizontal pass: for each row compute prefix sums per-channel and write averaged pixels to tmp
        for (int y = 0; y < height; ++y) {
            const uint8_t* row = src.data() + y * stride;
            std::vector<uint32_t> prefB(width + 1, 0), prefG(width + 1, 0), prefR(width + 1, 0), prefA(width + 1, 0);
            for (int x = 0; x < width; ++x) {
                uint8_t b = row[x * 4 + 0];
                uint8_t g = row[x * 4 + 1];
                uint8_t r = row[x * 4 + 2];
                uint8_t a = row[x * 4 + 3];
                prefB[x + 1] = prefB[x] + b;
                prefG[x + 1] = prefG[x] + g;
                prefR[x + 1] = prefR[x] + r;
                prefA[x + 1] = prefA[x] + a;
            }
            for (int x = 0; x < width; ++x) {
                int x0 = std::max(0, x - radius);
                int x1 = std::min(width - 1, x + radius);
                int count = x1 - x0 + 1;
                uint32_t sumB = prefB[x1 + 1] - prefB[x0];
                uint32_t sumG = prefG[x1 + 1] - prefG[x0];
                uint32_t sumR = prefR[x1 + 1] - prefR[x0];
                uint32_t sumA = prefA[x1 + 1] - prefA[x0];
                uint8_t* outPx = tmp.data() + y * stride + x * 4;
                outPx[0] = (uint8_t)(sumB / count);
                outPx[1] = (uint8_t)(sumG / count);
                outPx[2] = (uint8_t)(sumR / count);
                outPx[3] = (uint8_t)(sumA / count);
            }
        }

        // Vertical pass: for each column compute prefix sums over rows from tmp and write averaged pixels to dst
        for (int x = 0; x < width; ++x) {
            std::vector<uint32_t> prefB(height + 1, 0), prefG(height + 1, 0), prefR(height + 1, 0), prefA(height + 1, 0);
            for (int y = 0; y < height; ++y) {
                uint8_t* px = tmp.data() + y * stride + x * 4;
                prefB[y + 1] = prefB[y] + px[0];
                prefG[y + 1] = prefG[y] + px[1];
                prefR[y + 1] = prefR[y] + px[2];
                prefA[y + 1] = prefA[y] + px[3];
            }
            for (int y = 0; y < height; ++y) {
                int y0 = std::max(0, y - radius);
                int y1 = std::min(height - 1, y + radius);
                int count = y1 - y0 + 1;
                uint32_t sumB = prefB[y1 + 1] - prefB[y0];
                uint32_t sumG = prefG[y1 + 1] - prefG[y0];
                uint32_t sumR = prefR[y1 + 1] - prefR[y0];
                uint32_t sumA = prefA[y1 + 1] - prefA[y0];
                uint8_t* outPx = dst.data() + y * stride + x * 4;
                outPx[0] = (uint8_t)(sumB / count);
                outPx[1] = (uint8_t)(sumG / count);
                outPx[2] = (uint8_t)(sumR / count);
                outPx[3] = (uint8_t)(sumA / count);
            }
        }
        if (usedFastPath) {
            memcpy(data + start, dst.data(), planeSize);
        } else {
            try {
                auto writer = ref new Windows::Storage::Streams::DataWriter();
                writer->WriteBytes(Platform::ArrayReference<uint8_t>(dst.data(), (unsigned int)planeSize));
                auto outBuf = writer->DetachBuffer();
                bitmap->CopyFromBuffer(outBuf);
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("BoxBlurSoftwareBitmap: fallback CopyFromBuffer failed\n");
                moonlight_xbox_dx::Utils::Log("BoxBlurSoftwareBitmap: fallback CopyFromBuffer failed\n");
                return false;
            }
        }

        auto t1 = std::chrono::high_resolution_clock::now();
        double ms = std::chrono::duration_cast<std::chrono::duration<double, std::milli>>(t1 - t0).count();
        if (usedFastPath) moonlight_xbox_dx::Utils::Logf("BoxBlurSoftwareBitmap: fast-path blur completed in %.2f ms\n", ms);
        else moonlight_xbox_dx::Utils::Logf("BoxBlurSoftwareBitmap: fallback blur completed in %.2f ms\n", ms);

        return true;
    } catch(...) {
    moonlight_xbox_dx::Utils::Log("BoxBlurSoftwareBitmap: exception while blurring\n");
        return false;
    }
}

// GPU-path stub (kept for compatibility)
SoftwareBitmap^ EffectsLibrary::GpuBoxBlurSoftwareBitmap(SoftwareBitmap^ bitmap, int radius)
{
    using namespace Windows::Graphics::Imaging;
    if (bitmap == nullptr) return nullptr;
    if (m_device == nullptr || m_context == nullptr) {
        // Attempt lazy initialization if the host hasn't provided a device/context yet.
        if (!EnsureDeviceInitialized()) {
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: D3D device/context not initialized\n");
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur aborted (no device)\n");
            return nullptr;
        }
    }

    moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: entry bitmap=%p radius=%d device=%p context=%p\n", (void*)bitmap, radius, (void*)m_device, (void*)m_context);

    // Ensure format
    bitmap = EnsureBgra8Premultiplied(bitmap);

    // Read bitmap bytes (reuse CPU fast-path code)
    try {
        auto buffer = bitmap->LockBuffer(BitmapBufferAccessMode::Read);
        auto reference = buffer->CreateReference();
        Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> bufferByteAccess;
        HRESULT hr = S_OK;
        IUnknown* unk = reinterpret_cast<IUnknown*>(reference);
        BYTE* data = nullptr; UINT32 capacity = 0;
        bool usedFastPath = false;
        if (unk != nullptr) hr = unk->QueryInterface(IID_PPV_ARGS(&bufferByteAccess));
        if (!FAILED(hr) && bufferByteAccess != nullptr) {
            hr = bufferByteAccess->GetBuffer(&data, &capacity);
            if (!FAILED(hr) && data != nullptr) usedFastPath = true;
        }

        auto desc = buffer->GetPlaneDescription(0);
        int width = desc.Width;
        int height = desc.Height;
        int stride = desc.Stride;
        int start = desc.StartIndex;
        if (width <= 0 || height <= 0 || stride <= 0) return nullptr;
        size_t planeSize = (size_t)height * (size_t)stride;
        std::vector<uint8_t> src(planeSize);
        if (usedFastPath) {
            memcpy(src.data(), data + start, planeSize);
        } else {
            try {
                reference = nullptr;
                buffer = nullptr;
            } catch(...) {}
            try {
                auto ibuf = ref new Windows::Storage::Streams::Buffer((unsigned int)planeSize);
                bitmap->CopyToBuffer(ibuf);
                auto reader = Windows::Storage::Streams::DataReader::FromBuffer(ibuf);
                reader->ReadBytes(Platform::ArrayReference<uint8_t>(src.data(), (unsigned int)planeSize));
            } catch(...) {
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: fallback CopyToBuffer failed\n");
                return nullptr;
            }
        }

        // Prepare D3D resources
        Microsoft::WRL::ComPtr<ID3D11Texture2D> srcTex;
        D3D11_TEXTURE2D_DESC texDesc = {};
        texDesc.Width = width;
        texDesc.Height = height;
        texDesc.MipLevels = 1;
        texDesc.ArraySize = 1;
        texDesc.Format = DXGI_FORMAT_B8G8R8A8_UNORM;
        texDesc.SampleDesc.Count = 1;
        texDesc.Usage = D3D11_USAGE_DEFAULT;
        texDesc.BindFlags = D3D11_BIND_SHADER_RESOURCE | D3D11_BIND_RENDER_TARGET;
        texDesc.CPUAccessFlags = 0;

        D3D11_SUBRESOURCE_DATA initData = {};
        initData.pSysMem = src.data();
        initData.SysMemPitch = stride;

        hr = m_device->CreateTexture2D(&texDesc, &initData, srcTex.GetAddressOf());
        if (FAILED(hr) || srcTex == nullptr) {
            moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateTexture2D(src) failed hr=0x%08x\n", hr);
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (CreateTexture2D src)\n");
            return nullptr;
        }

        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srcSRV;
        hr = m_device->CreateShaderResourceView(srcTex.Get(), nullptr, srcSRV.GetAddressOf());
        if (FAILED(hr) || srcSRV == nullptr) {
            moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateShaderResourceView(src) failed hr=0x%08x\n", hr);
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (CreateShaderResourceView src)\n");
            return nullptr;
        }

        // Create two ping-pong render targets
        Microsoft::WRL::ComPtr<ID3D11Texture2D> rtA, rtB;
        Microsoft::WRL::ComPtr<ID3D11RenderTargetView> rtvA, rtvB;
        Microsoft::WRL::ComPtr<ID3D11ShaderResourceView> srvA, srvB;
        D3D11_TEXTURE2D_DESC rtDesc = texDesc;
        rtDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;
        rtDesc.Usage = D3D11_USAGE_DEFAULT;
        rtDesc.CPUAccessFlags = 0;

        hr = m_device->CreateTexture2D(&rtDesc, nullptr, rtA.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateTexture2D(rtA) failed hr=0x%08x\n", hr); return nullptr; }
        hr = m_device->CreateTexture2D(&rtDesc, nullptr, rtB.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateTexture2D(rtB) failed hr=0x%08x\n", hr); return nullptr; }
        hr = m_device->CreateRenderTargetView(rtA.Get(), nullptr, rtvA.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateRenderTargetView(rtA) failed hr=0x%08x\n", hr); return nullptr; }
        hr = m_device->CreateRenderTargetView(rtB.Get(), nullptr, rtvB.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateRenderTargetView(rtB) failed hr=0x%08x\n", hr); return nullptr; }
        hr = m_device->CreateShaderResourceView(rtA.Get(), nullptr, srvA.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateSRV(rtA) failed hr=0x%08x\n", hr); return nullptr; }
        hr = m_device->CreateShaderResourceView(rtB.Get(), nullptr, srvB.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateSRV(rtB) failed hr=0x%08x\n", hr); return nullptr; }

        // Compile shaders on first use
        Microsoft::WRL::ComPtr<ID3DBlob> vsBlob, psBlob, errBlob;
        const char* vsSrc = "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
            "VSOut VS(uint vid : SV_VertexID) { VSOut o; float2 pos[3] = { float2(-1,-1), float2(-1,3), float2(3,-1) }; o.pos = float4(pos[vid], 0.0f, 1.0f); o.uv = pos[vid] * 0.5f + 0.5f; return o; }\n";
        const char* psSrc = "Texture2D srcTex : register(t0); SamplerState samp : register(s0); cbuffer BlurCB : register(b0) { float2 texSize; float sigma; int direction; };\n"
            "struct VSOut { float4 pos : SV_POSITION; float2 uv : TEXCOORD0; };\n"
            "float4 PS(VSOut i) : SV_TARGET { int radius = (int)sigma; float2 texel = float2(1.0/texSize.x, 1.0/texSize.y); float2 step = (direction==0) ? float2(texel.x,0) : float2(0,texel.y); float4 sum = float4(0,0,0,0); for (int k = -radius; k <= radius; ++k) { sum += srcTex.SampleLevel(samp, i.uv + step * k, 0); } float inv = 1.0 / (float)(radius*2+1); return sum * inv; }\n";

        {
            std::lock_guard<std::mutex> lock(m_mutex);
            if (m_vs == nullptr || m_blurPS == nullptr) {
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: compiling shaders\n");
                UINT flags = D3DCOMPILE_OPTIMIZATION_LEVEL3;
                hr = D3DCompile(vsSrc, strlen(vsSrc), nullptr, nullptr, nullptr, "VS", "vs_4_0", flags, 0, vsBlob.GetAddressOf(), errBlob.GetAddressOf());
            if (FAILED(hr)) {
                if (errBlob) moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: VS compile error: %s\n", (const char*)errBlob->GetBufferPointer());
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (VS compile)\n");
                return nullptr;
            }
            hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);
            if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateVertexShader failed hr=0x%08x\n", hr); moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (CreateVertexShader)\n"); return nullptr; }

            hr = D3DCompile(psSrc, strlen(psSrc), nullptr, nullptr, nullptr, "PS", "ps_4_0", flags, 0, psBlob.GetAddressOf(), errBlob.GetAddressOf());
            if (FAILED(hr)) {
                if (errBlob) moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: PS compile error: %s\n", (const char*)errBlob->GetBufferPointer());
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (PS compile)\n");
                return nullptr;
            }
            hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_blurPS);
            if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreatePixelShader failed hr=0x%08x\n", hr); moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (CreatePixelShader)\n"); return nullptr; }

            // Create sampler
            if (m_sampler == nullptr) {
                D3D11_SAMPLER_DESC sd = {};
                sd.Filter = D3D11_FILTER_MIN_MAG_MIP_POINT;
                sd.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;
                sd.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;
                sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
                sd.ComparisonFunc = D3D11_COMPARISON_NEVER;
                sd.MinLOD = 0;
                sd.MaxLOD = D3D11_FLOAT32_MAX;
                m_device->CreateSamplerState(&sd, &m_sampler);
            }

            // Create constant buffer
            if (m_cb == nullptr) {
                D3D11_BUFFER_DESC cbd = {};
                cbd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
                cbd.Usage = D3D11_USAGE_DYNAMIC;
                cbd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
                cbd.ByteWidth = sizeof(BlurCB);
                m_device->CreateBuffer(&cbd, nullptr, &m_cb);
            }
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: shader/sampler/CB setup complete\n");
        }
        }

        // Setup viewport
        D3D11_VIEWPORT vp = {};
        vp.TopLeftX = 0.f;
        vp.TopLeftY = 0.f;
        vp.Width = (float)width;
        vp.Height = (float)height;
        vp.MinDepth = 0.f;
        vp.MaxDepth = 1.f;

        // Bind shaders and related state while holding mutex so D3D calls
        // do not run concurrently on the same device/context.
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_context->IASetInputLayout(nullptr);
            m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
            m_context->VSSetShader(m_vs, nullptr, 0);
            m_context->PSSetShader(m_blurPS, nullptr, 0);
            ID3D11SamplerState* samps[1] = { m_sampler };
            m_context->PSSetSamplers(0, 1, samps);

            // Prepare constant buffer data structure
            BlurCB cbdata;
            cbdata.texSize = DirectX::XMFLOAT2((float)width, (float)height);
            cbdata.sigma = (float)radius;

            // First pass: horizontal
            cbdata.direction = 0;
            D3D11_MAPPED_SUBRESOURCE mapped = {};
            if (m_cb) {
                if (SUCCEEDED(m_context->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, &cbdata, sizeof(cbdata));
                    m_context->Unmap(m_cb, 0);
                }
                ID3D11Buffer* cbs[1] = { m_cb };
                m_context->PSSetConstantBuffers(0, 1, cbs);
            }

            // Bind source SRV and render target A
            ID3D11ShaderResourceView* nullSRV[1] = { nullptr };
            ID3D11ShaderResourceView* srvSrc = srcSRV.Get();
            m_context->PSSetShaderResources(0, 1, &srvSrc);
            ID3D11RenderTargetView* rtv = rtvA.Get();
            m_context->OMSetRenderTargets(1, &rtv, nullptr);
            m_context->RSSetViewports(1, &vp);
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: issuing first draw (horizontal)\n");
            m_context->Draw(3, 0);

            // Unbind the render target we just rendered to so we can safely bind
            // its shader resource view in the next pass without causing a hazard.
            ID3D11RenderTargetView* nullRTVArr[1] = { nullptr };
            m_context->OMSetRenderTargets(1, nullRTVArr, nullptr);

            // Second pass: vertical (use srvA as source, render to rtB)
            // Unbind previous SRV from PS to avoid binding as both SRV and RTV
            m_context->PSSetShaderResources(0, 1, nullSRV);
            cbdata.direction = 1;
            if (m_cb) {
                if (SUCCEEDED(m_context->Map(m_cb, 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped))) {
                    memcpy(mapped.pData, &cbdata, sizeof(cbdata));
                    m_context->Unmap(m_cb, 0);
                }
                ID3D11Buffer* cbs2[1] = { m_cb };
                m_context->PSSetConstantBuffers(0, 1, cbs2);
            }
            ID3D11ShaderResourceView* srvAptr = srvA.Get();
            m_context->PSSetShaderResources(0, 1, &srvAptr);
            ID3D11RenderTargetView* rtvBptr = rtvB.Get();
            m_context->OMSetRenderTargets(1, &rtvBptr, nullptr);
            m_context->RSSetViewports(1, &vp);
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: issuing second draw (vertical)\n");
            m_context->Draw(3, 0);

            // Unbind SRV before copy/readback
            m_context->PSSetShaderResources(0, 1, nullSRV);
            // Also unbind render targets to avoid hazards when creating SRVs from them later
            ID3D11RenderTargetView* nullRTV[1] = { nullptr };
            m_context->OMSetRenderTargets(1, nullRTV, nullptr);
        }

        

        // Read back rtB into staging texture (guarded by mutex)
        Microsoft::WRL::ComPtr<ID3D11Texture2D> staging;
        D3D11_TEXTURE2D_DESC stagingDesc = texDesc;
        stagingDesc.Usage = D3D11_USAGE_STAGING;
        stagingDesc.BindFlags = 0;
        stagingDesc.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
        stagingDesc.MiscFlags = 0;
        hr = m_device->CreateTexture2D(&stagingDesc, nullptr, staging.GetAddressOf());
        if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: CreateTexture2D(staging) failed hr=0x%08x\n", hr); return nullptr; }
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            // Copy from rtB to staging
            moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: copying rtB -> staging\n");
            m_context->CopyResource(staging.Get(), rtB.Get());

            D3D11_MAPPED_SUBRESOURCE mapSR = {};
            hr = m_context->Map(staging.Get(), 0, D3D11_MAP_READ, 0, &mapSR);
            if (FAILED(hr)) { moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: Map staging failed hr=0x%08x\n", hr); moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (Map staging)\n"); return nullptr; }

            moonlight_xbox_dx::Utils::Logf("GpuBoxBlurSoftwareBitmap: staging mapped RowPitch=%u\n", mapSR.RowPitch);

            // We'll unmap below after copying out the data, but keep protection for the read
            // Copy-out operations below assume mapSR is valid.
            
            // Create output SoftwareBitmap and copy data in
            try {
                auto outBmp = ref new SoftwareBitmap(BitmapPixelFormat::Bgra8, width, height, BitmapAlphaMode::Premultiplied);
                auto outBuf = outBmp->LockBuffer(BitmapBufferAccessMode::Write);
                auto outRef = outBuf->CreateReference();
                Microsoft::WRL::ComPtr<IMemoryBufferByteAccess> outAccess;
                IUnknown* outUnk = reinterpret_cast<IUnknown*>(outRef);
                if (outUnk != nullptr) outUnk->QueryInterface(IID_PPV_ARGS(&outAccess));
                BYTE* outData = nullptr; UINT32 outCap = 0;
                if (outAccess != nullptr) {
                    outAccess->GetBuffer(&outData, &outCap);
                    auto outDesc = outBuf->GetPlaneDescription(0);
                    uint8_t* dstPtr = outData + outDesc.StartIndex;
                    uint8_t* srcPtr = (uint8_t*)mapSR.pData;
                    size_t srcRowPitch = mapSR.RowPitch;
                    size_t dstRowPitch = outDesc.Stride;
                    for (int y = 0; y < height; ++y) {
                        memcpy(dstPtr + y * dstRowPitch, srcPtr + y * srcRowPitch, std::min(srcRowPitch, dstRowPitch));
                    }
                    m_context->Unmap(staging.Get(), 0);
                    moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: success (fast path outAccess)\n");
                    return outBmp;
                }

                // If outAccess QI fails (some WinRT contexts don't support IMemoryBufferByteAccess),
                // use a fallback: create a temporary IBuffer and CopyFromBuffer into the SoftwareBitmap.
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: outAccess QueryInterface failed, using CopyFromBuffer fallback\n");
                    try {
                    size_t srcRowPitch = mapSR.RowPitch;
                    // Determine destination row pitch from the output buffer description
                    auto outDesc = outBuf->GetPlaneDescription(0);
                    size_t dstRowPitch = outDesc.Stride;
                    std::vector<uint8_t> tmpBuf((size_t)height * dstRowPitch);
                    uint8_t* srcPtr = (uint8_t*)mapSR.pData;
                    for (int y = 0; y < height; ++y) {
                        size_t copyBytes = std::min(srcRowPitch, dstRowPitch);
                        memcpy(tmpBuf.data() + y * dstRowPitch, srcPtr + y * srcRowPitch, copyBytes);
                        if (dstRowPitch > copyBytes) {
                            // Zero any remaining padding in the destination row to be safe
                            memset(tmpBuf.data() + y * dstRowPitch + copyBytes, 0, dstRowPitch - copyBytes);
                        }
                    }
                    m_context->Unmap(staging.Get(), 0);
                    // Release any locks on the output bitmap before calling CopyFromBuffer
                    outAccess = nullptr;
                    outUnk = nullptr;
                    outRef = nullptr;
                    outBuf = nullptr;
                    auto writer = ref new Windows::Storage::Streams::DataWriter();
                    writer->WriteBytes(Platform::ArrayReference<uint8_t>(tmpBuf.data(), (unsigned int)tmpBuf.size()));
                    auto outIBuf = writer->DetachBuffer();
                    outBmp->CopyFromBuffer(outIBuf);
                    moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: success (fallback CopyFromBuffer)\n");
                    return outBmp;
                } catch(...) {
                    m_context->Unmap(staging.Get(), 0);
                    moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: fallback CopyFromBuffer failed\n");
                    moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (fallback CopyFromBuffer)\n");
                    return nullptr;
                }
            } catch(...) {
                m_context->Unmap(staging.Get(), 0);
                moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: exception while creating output bitmap\n");
                return nullptr;
            }
        }

    } catch(...) {
        moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: unexpected exception\n");
        moonlight_xbox_dx::Utils::Log("GpuBoxBlurSoftwareBitmap: GPU blur failed (unexpected exception)\n");
        return nullptr;
    }
}

ID3D11ShaderResourceView* EffectsLibrary::Blur(ID3D11ShaderResourceView* src, float sigma)
{
    // GPU blur not implemented in this change
    return src;
}
