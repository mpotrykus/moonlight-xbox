Texture2D<min16float> luminancePlane : register(t0);
Texture2D<min16float2> chrominancePlane : register(t1);
SamplerState theSampler : register(s0);

struct ShaderInput
{
    float4 pos : SV_POSITION;
    float2 tex : TEXCOORD0;
};

cbuffer CSC_CONST_BUF : register(b0)
{
    min16float3x3 cscMatrix;
    min16float3 offsets;
    min16float contrast;
    min16float2 chromaOffset;
    min16float2 chromaTexMax;
    min16float blackLevel;
    min16float whiteLevel;
    min16float gamma;
    min16float saturation;
};

min16float4 main(ShaderInput input) : SV_TARGET
{
    // Clamp the chrominance texcoords to avoid sampling the row of texels adjacent to the alignment padding
    min16float3 yuv = min16float3(luminancePlane.Sample(theSampler, input.tex),
                                  chrominancePlane.Sample(theSampler, min(input.tex + chromaOffset, chromaTexMax.rg)));

    // Subtract the YUV offset for limited vs full range
    yuv -= offsets;

    // Multiply by the conversion matrix for this colorspace
    yuv = mul(yuv, cscMatrix);

    // Levels: remap minimum black / maximum white to 0..1
    yuv = saturate((yuv - blackLevel) / max(whiteLevel - blackLevel, 0.001h));

    // Gamma
    yuv = pow(yuv, 1.0h / gamma);

    // Contrast, applied around mid-gray
    yuv = saturate((yuv - 0.5h) * contrast + 0.5h);

    // Saturation: blend toward luma
    min16float luma = dot(yuv, min16float3(0.2126h, 0.7152h, 0.0722h));
    yuv = saturate(lerp((min16float3)luma, yuv, saturation));

    return min16float4(yuv, 1.0);
}