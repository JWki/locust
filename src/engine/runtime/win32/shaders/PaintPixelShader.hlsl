cbuffer Object : register(cb0) {
    float4x4    ObjToViewMatrix;
    float4x4    ObjToProjMatrix;
    float2      CursorPos;
    //float2      _padding0;
    float4      Color;
    float       BrushSize;
    //float3      _padding1;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 screenPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float NdV : TEXCOORD2;
};

struct PixelOutput
{
    float4 diffuse : SV_TARGET0;
    float4 roughness : SV_TARGET1;   
    float4 metallic : SV_TARGET2;
};

Texture2D diffuse : register(t0);
sampler   sampler0 : register(s0);

Texture2D roughness : register(t1);
sampler   sampler1 : register(s1);

Texture2D metallic : register(t2);
sampler   sampler2 : register(s2);

PixelOutput main(PixelInput vertex) 
{
   
    float2 cPos = float2(CursorPos.x / 1920.0f, 1.0f - (CursorPos.y / 1080.0f)) * float2(2.0f, 2.0f) - float2(1.0f, 1.0f);
    float dist = length((vertex.screenPos.xy - cPos) * float2((1920.0f / 1080.0f), 1.0f));
    float alpha = clamp((1.0f - dist* (1080.0f / BrushSize)), 0.0f, 1.0f);

    float3 color = Color.rgb * diffuse.Sample(sampler0, vertex.uv * 4.0f).rgb;
    float a = clamp(vertex.NdV, 0.0f, 1.0f) * alpha * Color.a * Color.a;

    PixelOutput output;
    output.diffuse = float4(pow(color, 2.2f), a);
    output.roughness = float4(roughness.Sample(sampler1, vertex.uv * 4.0f).r, 0.0f, 0.0f, a);
    output.metallic = float4(metallic.Sample(sampler2, vertex.uv * 4.0f).r, 0.0f, 0.0f, a);

    return output;
}