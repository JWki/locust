cbuffer Object : register(cb0) {
    float4x4    WorldMatrix;
    float4      Color;
    float4      LightDir;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

Texture2D texture1;
sampler   sampler1;

float4 main(PixelInput input) : SV_TARGET
{
    float3 lightDir = normalize(LightDir.xyz);
    float3 n = normalize(input.normal).xyz;
    float lambert = dot(n, -lightDir);
    float light = clamp(lambert, 0.0f, 1.0f) + 0.2f;
    float4 paintColor = texture1.Sample(sampler1, input.uv.xy);
    float4 albedo = (texture0.Sample(sampler0, input.uv.xy) * paintColor.a + float4(pow(paintColor.rgb, 1.0f / 2.2f), 1.0f));
    //return float4(light, 0.0f, 0.0f, 1.0f);
    return float4(light * albedo.rgb, 1.0f);
}