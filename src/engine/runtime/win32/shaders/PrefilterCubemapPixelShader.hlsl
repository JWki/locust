struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

cbuffer Params : register(cb0) {
    float Roughness;
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

float3 DirFromUV(float2 uv)
{
    float2 normalized = 2.0f * uv - 1.0f;
    float theta = normalized.x * PI;
    float phi = normalized.y * PI / 2.0f;

    return float3(cos(phi) * cos(theta), sin(phi), -cos(phi) * sin(theta));
}



float RadicalInverse_VdC(uint bits)
{
    bits = (bits << 16u) | (bits >> 16u);
    bits = ((bits & 0x55555555u) << 1u) | ((bits & 0xAAAAAAAAu) >> 1u);
    bits = ((bits & 0x33333333u) << 2u) | ((bits & 0xCCCCCCCCu) >> 2u);
    bits = ((bits & 0x0F0F0F0Fu) << 4u) | ((bits & 0xF0F0F0F0u) >> 4u);
    bits = ((bits & 0x00FF00FFu) << 8u) | ((bits & 0xFF00FF00u) >> 8u);
    return float(bits) * 2.3283064365386963e-10; // / 0x100000000
}
// ----------------------------------------------------------------------------
float2 Hammersley(uint i, uint N)
{
    return float2(float(i) / float(N), RadicalInverse_VdC(i));
}

float3 ImportanceSampleGGX(float2 Xi, float3 N, float roughness)
{
    float a = roughness*roughness;

    float phi = 2.0 * PI * Xi.x;
    float cosTheta = sqrt((1.0 - Xi.y) / (1.0 + (a*a - 1.0) * Xi.y));
    float sinTheta = sqrt(1.0 - cosTheta*cosTheta);

    // from spherical coordinates to cartesian coordinates
    float3 H;
    H.x = cos(phi) * sinTheta;
    H.y = sin(phi) * sinTheta;
    H.z = cosTheta;

    // from tangent-space vector to world-space sample vector
    float3 up = abs(N.z) < 0.999 ? float3(0.0, 0.0, 1.0) : float3(1.0, 0.0, 0.0);
    float3 tangent = normalize(cross(up, N));
    float3 bitangent = cross(N, tangent);

    float3 sampleVec = tangent * H.x + bitangent * H.y + N * H.z;
    return normalize(sampleVec);
}


float3 FilterCubemap(Texture2D map, sampler smpl, float3 V, float3 N, float roughness)
{
    //return map.Sample(smpl, SampleSphericalMap(N)).rgb;

    const uint SAMPLE_COUNT = 1024u;
    float totalWeight = 0.0f;
    float3 prefilteredColor = float3(0.0f, 0.0f, 0.0f);
    for (uint i = 0u; i < SAMPLE_COUNT; ++i)
    {
        float2 Xi = Hammersley(i, SAMPLE_COUNT);
        float3 H = ImportanceSampleGGX(Xi, N, roughness);
        float3 L = normalize(2.0f * max(dot(V, H), 0.0f) * H - V);

        float NdotL = max(dot(N, L), 0.0f);
        if (NdotL > 0.0f)
        {
            float3 sampleColor = map.Sample(smpl, SampleSphericalMap(L)).rgb;

            prefilteredColor += sampleColor * NdotL;
            totalWeight += NdotL;
        }
    }
    prefilteredColor = prefilteredColor / totalWeight;

    return prefilteredColor;
}

float4 main(PixelInput input) : SV_Target
{
    float2 uv = input.uv;

    float3 N = normalize(DirFromUV(input.uv));
    float3 V = N;

    //return float4(1.0f, 1.0f, 1.0f, 1.0f);
    return float4(FilterCubemap(hdrCubemap, sampler1, V, N, Roughness), 1.0f);
}