struct PixelInput
{
    float4 clipPos : SV_POSITION;
    float4 localPos : TEXCOORD0;
};

Texture2D hdrCubemap : register(t0);
sampler   sampler1 : register(s0);

static const float PI = 3.14159265359f;
static const float2 invAtan = float2(0.1591f, 0.3183f);
float2 SampleSphericalMap(float3 dir)
{
    float2 uv = float2(atan2(-dir.z, dir.x), asin(dir.y));
    uv *= invAtan;
    uv += 0.5f;
    return uv;
}


float4 main(PixelInput input) : SV_Target
{
    float2 uv = SampleSphericalMap(normalize(input.localPos.xyz));
    return hdrCubemap.Sample(sampler1, uv);
}