#include "pch.h"
#include "EffectsLibrary.h"
#include <DirectXColors.h>

ID3D11Device* EffectsLibrary::m_device = nullptr;

ID3D11DeviceContext* EffectsLibrary::m_context = nullptr;

ID3D11VertexShader* EffectsLibrary::m_vs = nullptr;

ID3D11PixelShader* EffectsLibrary::m_blurPS = nullptr;

ID3D11InputLayout* EffectsLibrary::m_inputLayout = nullptr;

ID3D11Buffer* EffectsLibrary::m_quadVB = nullptr;

ID3D11Buffer* EffectsLibrary::m_cb = nullptr;

ID3D11SamplerState* EffectsLibrary::m_sampler = nullptr;

void EffectsLibrary::Initialize(ID3D11Device* device, ID3D11DeviceContext* context) {

    if (m_device != nullptr) return; // already initialized

    m_device = device;

    m_context = context;

    HRESULT hr;

    // Compile vertex shader

    const char* vsCode = R"(

struct VS_INPUT {

    float3 Pos : POSITION;

    float2 Tex : TEXCOORD;

};

struct PS_INPUT {

    float4 Pos : SV_POSITION;

    float2 Tex : TEXCOORD;

};

PS_INPUT main(VS_INPUT input) {

    PS_INPUT output;

    output.Pos = float4(input.Pos, 1.0);

    output.Tex = input.Tex;

    return output;

}

)";

    ID3DBlob* vsBlob = nullptr;

    ID3DBlob* errorBlob = nullptr;

    hr = D3DCompile(vsCode, strlen(vsCode), nullptr, nullptr, nullptr, "main", "vs_4_0", 0, 0, &vsBlob, &errorBlob);

    if (FAILED(hr)) {

        if (errorBlob) {

            OutputDebugStringA((char*)errorBlob->GetBufferPointer());

            errorBlob->Release();

        }

        throw std::runtime_error("Failed to compile vertex shader");

    }

    hr = m_device->CreateVertexShader(vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), nullptr, &m_vs);

    if (FAILED(hr)) throw std::runtime_error("Failed to create vertex shader");

    // Input layout

    D3D11_INPUT_ELEMENT_DESC layout[] = {

        {"POSITION", 0, DXGI_FORMAT_R32G32B32_FLOAT, 0, 0, D3D11_INPUT_PER_VERTEX_DATA, 0},

        {"TEXCOORD", 0, DXGI_FORMAT_R32G32_FLOAT, 0, 12, D3D11_INPUT_PER_VERTEX_DATA, 0}

    };

    hr = m_device->CreateInputLayout(layout, 2, vsBlob->GetBufferPointer(), vsBlob->GetBufferSize(), &m_inputLayout);

    if (FAILED(hr)) throw std::runtime_error("Failed to create input layout");

    vsBlob->Release();

    // Compile pixel shader

    const char* psCode = R"(

Texture2D tex : register(t0);

SamplerState sam : register(s0);

cbuffer BlurCB : register(b0) {

    float2 texSize;

    float sigma;

    int direction;

};

float Gaussian(float x, float sigma) {

    return exp(-0.5 * x * x / (sigma * sigma)) / (sigma * sqrt(2 * 3.14159));

}

float4 main(float4 pos : SV_POSITION, float2 tex : TEXCOORD) : SV_Target {

    float4 color = 0;

    float weightSum = 0;

    int radius = 5;

    for(int i = -radius; i <= radius; i++) {

        float2 offset = direction == 0 ? float2(i, 0) : float2(0, i);

        offset /= texSize;

        float weight = Gaussian(abs(i), sigma);

        color += tex.Sample(sam, tex + offset) * weight;

        weightSum += weight;

    }

    return color / weightSum;

}

)";

    ID3DBlob* psBlob = nullptr;

    hr = D3DCompile(psCode, strlen(psCode), nullptr, nullptr, nullptr, "main", "ps_4_0", 0, 0, &psBlob, &errorBlob);

    if (FAILED(hr)) {

        if (errorBlob) {

            OutputDebugStringA((char*)errorBlob->GetBufferPointer());

            errorBlob->Release();

        }

        throw std::runtime_error("Failed to compile pixel shader");

    }

    hr = m_device->CreatePixelShader(psBlob->GetBufferPointer(), psBlob->GetBufferSize(), nullptr, &m_blurPS);

    if (FAILED(hr)) throw std::runtime_error("Failed to create pixel shader");

    psBlob->Release();

    // Quad vertex buffer

    float quad[] = {

        -1, 1, 0, 0, 0,

        1, 1, 0, 1, 0,

        -1, -1, 0, 0, 1,

        1, -1, 0, 1, 1

    };

    D3D11_BUFFER_DESC bd = {};

    bd.Usage = D3D11_USAGE_DEFAULT;

    bd.ByteWidth = sizeof(quad);

    bd.BindFlags = D3D11_BIND_VERTEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initData = {};

    initData.pSysMem = quad;

    hr = m_device->CreateBuffer(&bd, &initData, &m_quadVB);

    if (FAILED(hr)) throw std::runtime_error("Failed to create vertex buffer");

    // Constant buffer

    bd = {};

    bd.Usage = D3D11_USAGE_DEFAULT;

    bd.ByteWidth = sizeof(BlurCB);

    bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;

    hr = m_device->CreateBuffer(&bd, nullptr, &m_cb);

    if (FAILED(hr)) throw std::runtime_error("Failed to create constant buffer");

    // Sampler

    D3D11_SAMPLER_DESC sampDesc = {};

    sampDesc.Filter = D3D11_FILTER_MIN_MAG_MIP_LINEAR;

    sampDesc.AddressU = D3D11_TEXTURE_ADDRESS_CLAMP;

    sampDesc.AddressV = D3D11_TEXTURE_ADDRESS_CLAMP;

    sampDesc.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;

    hr = m_device->CreateSamplerState(&sampDesc, &m_sampler);

    if (FAILED(hr)) throw std::runtime_error("Failed to create sampler");

}

ID3D11ShaderResourceView* EffectsLibrary::Blur(ID3D11ShaderResourceView* src, float sigma) {

    HRESULT local_hr;

    // Get texture from SRV

    ID3D11Resource* res = nullptr;

    src->GetResource(&res);

    ID3D11Texture2D* tex = static_cast<ID3D11Texture2D*>(res);

    D3D11_TEXTURE2D_DESC desc;

    tex->GetDesc(&desc);

    res->Release();

    // Create temp textures

    D3D11_TEXTURE2D_DESC tempDesc = desc;

    tempDesc.BindFlags = D3D11_BIND_RENDER_TARGET | D3D11_BIND_SHADER_RESOURCE;

    ID3D11Texture2D* temp1 = nullptr;

    local_hr = m_device->CreateTexture2D(&tempDesc, nullptr, &temp1);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create temp texture");

    ID3D11Texture2D* temp2 = nullptr;

    local_hr = m_device->CreateTexture2D(&tempDesc, nullptr, &temp2);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create temp texture 2");

    ID3D11RenderTargetView* rtv1 = nullptr;

    local_hr = m_device->CreateRenderTargetView(temp1, nullptr, &rtv1);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create RTV 1");

    ID3D11RenderTargetView* rtv2 = nullptr;

    local_hr = m_device->CreateRenderTargetView(temp2, nullptr, &rtv2);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create RTV 2");

    ID3D11ShaderResourceView* srv1 = nullptr;

    local_hr = m_device->CreateShaderResourceView(temp1, nullptr, &srv1);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create SRV 1");

    ID3D11ShaderResourceView* srv2 = nullptr;

    local_hr = m_device->CreateShaderResourceView(temp2, nullptr, &srv2);

    if (FAILED(local_hr)) throw std::runtime_error("Failed to create SRV 2");

    // Set viewport

    D3D11_VIEWPORT vp = {};

    vp.Width = (float)desc.Width;

    vp.Height = (float)desc.Height;

    vp.MaxDepth = 1.0f;

    m_context->RSSetViewports(1, &vp);

    // Horizontal blur

    BlurCB cb = { {(float)desc.Width, (float)desc.Height}, sigma, 0 };

    m_context->UpdateSubresource(m_cb, 0, nullptr, &cb, 0, 0);

    m_context->OMSetRenderTargets(1, &rtv1, nullptr);

    m_context->ClearRenderTargetView(rtv1, DirectX::Colors::Transparent);

    m_context->IASetInputLayout(m_inputLayout);

    m_context->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLESTRIP);

    UINT stride = 20;

    UINT offset = 0;

    m_context->IASetVertexBuffers(0, 1, &m_quadVB, &stride, &offset);

    m_context->VSSetShader(m_vs, nullptr, 0);

    m_context->PSSetShader(m_blurPS, nullptr, 0);

    m_context->PSSetConstantBuffers(0, 1, &m_cb);

    m_context->PSSetShaderResources(0, 1, &src);

    m_context->PSSetSamplers(0, 1, &m_sampler);

    m_context->Draw(4, 0);

    // Vertical blur

    cb.direction = 1;

    m_context->UpdateSubresource(m_cb, 0, nullptr, &cb, 0, 0);

    m_context->OMSetRenderTargets(1, &rtv2, nullptr);

    m_context->ClearRenderTargetView(rtv2, DirectX::Colors::Transparent);

    m_context->PSSetShaderResources(0, 1, &srv1);

    m_context->Draw(4, 0);

    // Cleanup

    rtv1->Release();

    rtv2->Release();

    srv1->Release();

    temp1->Release();

    temp2->Release();

    return srv2;

}