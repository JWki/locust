cbuffer Object : register(cb0) {
    float4x4    ModelViewProjection;
    float4x4    ModelView;
    float4x4    ViewProjection;
    float4x4    View;
    float4x4    InverseView;
    float4x4    Projection;
    float4x4    Model;
    float4      Color;
    float4      LightDir;

    float       Metallic;
    float       Roughness;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 worldPos : TEXCOORD1;
};

Texture2D texture0;
sampler   sampler0;

Texture2D texture1;
sampler   sampler1;

static const float PI = 3.14159264359f;

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * pow(1.0f - cosTheta, 5.0f);
}

float DistributionGGX(float NdotH, float roughness)
{
    float a = roughness * roughness;
    float a2 = a * a;

    float NdotH2 = NdotH * NdotH;

    float nom = a2;
    float denom = (NdotH2 * (a2 - 1.0f) + 1.0f);
    denom = PI * denom * denom;

    return nom / denom;
}

float GeometrySchlickGGX(float NdotV, float roughness)
{
    float r = (roughness + 1.0f);
    float k = (r * r) / 8.0f;
    
    float nom = NdotV;
    float denom = NdotV * (1.0f - k) + k;
    
    return nom / denom;
}

float GeometrySmith(float NdotV, float NdotL, float roughness)
{
    float ggx2 = GeometrySchlickGGX(NdotV, roughness);
    float ggx1 = GeometrySchlickGGX(NdotL, roughness);

    return ggx1 * ggx2;
}

float4 main(PixelInput input) : SV_TARGET
{
    float3 viewPos = mul(InverseView, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;

    float3 N = normalize(input.normal).xyz;
    float3 V = normalize(viewPos - input.worldPos.xyz);
    float3 L = normalize(-LightDir).xyz;
    float3 H = normalize(L + V);
 
    float4 paintColor = texture1.Sample(sampler1, input.uv.xy);
    float4 albedo = (texture0.Sample(sampler0, input.uv.xy) * paintColor.a + float4(pow(paintColor.rgb, 1.0f / 2.2f), 1.0f));
    albedo = float4(1.0f, 1.0f, 1.0f, 1.0f);

    float roughness = clamp(Roughness, 0.01f, 1.0f);
    float metallic = clamp(Metallic, 0.04f, 0.99f);

    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo.rgb, metallic);

    float NdotH = saturate(dot(N, H));
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float HdotV = saturate(dot(H, V));

    float NDF = DistributionGGX(NdotL, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);
   
    float3 nominator = NDF * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.001f;
    
    float3 specular = nominator / denominator;

    float3 kS = F;
    float3 kD = 1.0f - kS;
    kD *= 1.0f - metallic;

    float3 directLight = NdotL * (kD * albedo.rgb / PI + specular);

    //return float4(float3(1.0f, 1.0f, 1.0f) * NdotH, 1.0f);
    //return float4(float3(1.0f, 1.0f, 1.0f) * NDF, 1.0f);
    //return float4(float3(1.0f, 1.0f, 1.0f) * G, 1.0f);
    //return float4(F, 1.0f);
    //return float4(input.worldPos.xyz, 1.0f);
    
    //return float4(1.0f, 1.0f, 1.0f, 1.0f) * (1.0f - saturate(dot(N, V)));
    //return float4(F, 1.0f);
        
    //return float4(float3(1.0f, 1.0f, 1.0f) * saturate(dot(N, V)), 1.0f);
    //return float4(float3(1.0f, 1.0f, 1.0f) * saturate(dot(H, V)), 1.0f);

    return float4(directLight * albedo.rgb, 1.0f);
}