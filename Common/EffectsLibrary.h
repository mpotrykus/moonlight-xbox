#pragma once

#include <d3d11.h>
#include <d3dcompiler.h>
#include <DirectXMath.h>

class EffectsLibrary {

public:

    static void Initialize(ID3D11Device* device, ID3D11DeviceContext* context);

    static ID3D11ShaderResourceView* Blur(ID3D11ShaderResourceView* src, float sigma);

    static ID3D11ShaderResourceView* Glow(ID3D11ShaderResourceView* src, float radius) {

        return Blur(src, radius);

    }

    static ID3D11Device* GetDevice() { return m_device; }

    static ID3D11DeviceContext* GetContext() { return m_context; }

private:

    static ID3D11Device* m_device;

    static ID3D11DeviceContext* m_context;

    static ID3D11VertexShader* m_vs;

    static ID3D11PixelShader* m_blurPS;

    static ID3D11InputLayout* m_inputLayout;

    static ID3D11Buffer* m_quadVB;

    static ID3D11Buffer* m_cb;

    static ID3D11SamplerState* m_sampler;

    struct BlurCB {

        DirectX::XMFLOAT2 texSize;

        float sigma;

        int direction;

    };

};