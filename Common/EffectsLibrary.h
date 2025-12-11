#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>
// SoftwareBitmap type used by CPU fallback blur
#include <windows.graphics.imaging.h>
#include <mutex>

class EffectsLibrary {

public:

    static void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    static ID3D11ShaderResourceView* Blur(ID3D11ShaderResourceView* src, float sigma);

    // CPU fallback blur operating on SoftwareBitmap (UWP-safe). Returns true on success.
    static bool BoxBlurSoftwareBitmap(Windows::Graphics::Imaging::SoftwareBitmap^ bitmap, int radius);

    // Attempt to run blur on GPU and return a new SoftwareBitmap containing the blurred result.
    // Returns nullptr on failure (caller should fall back to CPU).
    // If enableDiagnostics is true, diagnostic PNGs may be emitted to LocalFolder.
    // If returnPadded is true the returned SoftwareBitmap will be the padded blurred canvas
    // (padded by radius*2 on each side) instead of the cropped center region.
    static Windows::Graphics::Imaging::SoftwareBitmap^ GpuBoxBlurSoftwareBitmap(Windows::Graphics::Imaging::SoftwareBitmap^ bitmap, int radius, bool enableDiagnostics = false, bool returnPadded = false);

    static ID3D11ShaderResourceView* Glow(ID3D11ShaderResourceView* src, float radius) {

        return Blur(src, radius);

    }

    static ID3D11Device* GetDevice() { return m_device; }

    static ID3D11DeviceContext* GetContext() { return m_context; }

private:

    static ID3D11Device* m_device;

    static ID3D11DeviceContext* m_context;
    static ID3D11Multithread* m_multithread;
    // True if the library created the device/context via EnsureDeviceInitialized
    // and therefore is responsible for releasing them. If false, the
    // device/context are owned by the application and must not be released
    // by the EffectsLibrary.
    static bool m_ownsDevice;

    static ID3D11VertexShader* m_vs;

    static ID3D11PixelShader* m_blurPS;

    static ID3D11InputLayout* m_inputLayout;

    static ID3D11Buffer* m_quadVB;

    static ID3D11Buffer* m_cb;

    static ID3D11SamplerState* m_sampler;

    // Mutex to protect lazy initialization and device-bound resource creation/binding
    static std::mutex m_mutex;

    struct BlurCB {

        DirectX::XMFLOAT2 texSize;

        float sigma;

        int direction;

    };

    // Ensure D3D device/context are available (lazy init fallback)
    static bool EnsureDeviceInitialized();

};