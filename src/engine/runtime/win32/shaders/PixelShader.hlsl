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

    bool        UseTextures;
};


struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD0;
    float4 worldPos : TEXCOORD1;
    float3 tangent : TEXCOORD2;
    float3 bitangent : TEXCOORD3;
    float3x3 TBN : TEXCOORD4;
};

Texture2D diffuseMap : register(t0);
sampler   sampler0 : register(s0);
Texture2D roughnessMap : register(t1);
sampler   sampler1 : register(s1);
Texture2D metallicMap : register(t2);
sampler   sampler2 : register(s2);
Texture2D normalMap : register(t3);
sampler   sampler6 : register(s3);

Texture2D aoMap : register(t4);
sampler   sampler3 : register(s4);

Texture2D paintRoughness : register(t5);
sampler   sampler4 : register(s5);
Texture2D paintMetallic : register(t6);
sampler   sampler5 : register(s6);

Texture2D paintNormal : register(t7);
sampler sampler8 : register(s7);

TextureCube cubemap : register(t8);
sampler sampler7 : register(s8);
Texture2D hdrCubemap : register(t9);
sampler sampler9 : register(s9);
Texture2D hdrDiffuse : register(t10);
sampler sampler10 : register(s10);

Texture2D brdfLUT : register(t11);
sampler sampler11 : register(s11);

static const float PI = 3.14159264359f;

float3 FresnelSchlick(float cosTheta, float3 F0)
{
    return F0 + (float3(1.0f, 1.0f, 1.0f) - F0) * pow(1.0f - cosTheta, 5.0f);
}

float3 FresnelSchlickRoughness(float cosTheta, float3 F0, float roughness)
{
    //max(vec3(1.0 - roughness), F0)
    return F0 + (max(float3(1.0f, 1.0f, 1.0f) - roughness, F0) - F0) * pow(1.0f - cosTheta, 5.0f);
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


// http://blog.selfshadow.com/publications/blending-in-detail/
float3 blend_rnm(float3 n1, float3 n2)
{
    float3 t = n1*float3(2, 2, 2) + float3(-1, -1, 0);
    float3 u = n2*float3(-2, -2, 2) + float3(1, 1, -1);
    float3 r = t*dot(t, u) - u*t.z;
    return normalize(r);
}


static const float2 invAtan = float2(0.1591f, 0.3183f);
float2 SampleSphericalMap(float3 dir)
{
    float2 uv = float2(atan2(-dir.z, dir.x), asin(dir.y));
    uv *= invAtan;
    uv += 0.5f;
    return uv;
}





float4 main(PixelInput input) : SV_TARGET
{
    float3 viewPos = mul(InverseView, float4(0.0f, 0.0f, 0.0f, 1.0f)).xyz;

    float3 N = normalize(input.normal).xyz;
    //float3 paintN = paintNormal.Sample(sampler8, input.uv).rgb;

    // @HACK uncomment back in
    N = normalMap.Sample(sampler6, input.uv).rgb;
    N = N * 2.0f - 1.0f;
    N = normalize(mul(input.TBN, float4(N, 0.0f)).xyz); 

    //return float4(N * 0.5f + 0.5f, 1.0f);

    float3 V = normalize(viewPos - input.worldPos.xyz);
    float3 L = normalize(-LightDir.xyz);
    float3 H = normalize(L + V);
 

    //float4 paintColor = paintDiffuse.Sample(sampler1, input.uv.xy);
    float4 albedo = pow(diffuseMap.Sample(sampler0, input.uv.xy), 2.2f); // * paintColor.a + float4(paintColor.rgb, 1.0f);
    //albedo = float4(1.0f, 0.0f, 0.0f, 1.0f);
    float3 ao = aoMap.Sample(sampler3, input.uv.xy).rgb;
    //return float4(ao, 1.0f);

    float roughness = roughnessMap.Sample(sampler1, input.uv.xy).r; // *paintColor.a + roughness;
    float metallic = metallicMap.Sample(sampler2, input.uv.xy).r; // *paintColor.a + metallic;

    //roughness = roughness + paintRoughness.Sample(sampler4, input.uv.xy).r;
    //metallic = metallic + paintMetallic.Sample(sampler5, input.uv.xy).r;

    if (!UseTextures) {
        roughness = Roughness;
        metallic = Metallic;
    }

    // @HACK delete this code
    //roughness = 0.127f;
    //metallic = 0.0f;

    //roughness = input.worldPos.x / 15.0f;
    //metallic = input.worldPos.z / 15.0f;

    roughness = clamp(roughness, 0.01f, 1.0f);
    metallic = clamp(metallic, 0.04f, 0.99f);


    float3 F0 = float3(0.04f, 0.04f, 0.04f);
    F0 = lerp(F0, albedo.rgb, metallic);

    float NdotH = saturate(dot(N, H));
    float NdotV = saturate(dot(N, V));
    float NdotL = saturate(dot(N, L));
    float HdotV = saturate(dot(H, V));

    float NDF = DistributionGGX(NdotL, roughness);
    float G = GeometrySmith(NdotV, NdotL, roughness);
    float3 F = FresnelSchlick(HdotV, F0);
   
    float3 id = float3(1.0f, 1.0f, 1.0f);
    //return float4(id * NDF, 1.0f);
    //return float4(id * G, 1.0f);
    //return float4(F, 1.0f);

    float3 nominator = NDF * G * F;
    float denominator = 4.0f * NdotV * NdotL + 0.001f;
    
    float3 specular = nominator / denominator;

    float3 kS = F;
    float3 kD = 1.0f - kS;
    kD *= 1.0f - metallic;

    float3 directLight = LightDir.w * NdotL * (kD * albedo.rgb / PI + specular);

    //return float4(directLight, 1.0f);

    float3 r = normalize(reflect(-V, N));

    float3 indirectLight = float3(0.0f, 0.0f, 0.0f);
    {
        float3 kS2 = FresnelSchlickRoughness(NdotV, F0, roughness);
        float3 kD2 = 1.0f - kS;
        kD2 *= 1.0f - metallic;

        float3 irradiance = hdrDiffuse.Sample(sampler10, SampleSphericalMap(N)).rgb;
        float3 diffuseAmbient = kD2 * irradiance * albedo.rgb;

        float width, height; 
        uint numMipLevels;
        hdrCubemap.GetDimensions(0, width, height, numMipLevels);
        float3 prefilteredSpecular = hdrCubemap.SampleLevel(sampler9, SampleSphericalMap(r), roughness * numMipLevels);//FilterCubemap(hdrCubemap, sampler9, V, H, N, roughness);

        //float2 brdfLUTDimensions;
        //brdfLUT.GetDimensions(brdfLUTDimensions.x, brdfLUTDimensions.y);
        float2 envBRDFSampleLoc = float2(NdotV, roughness);
        //envBRDFSampleLoc.x = clamp(envBRDFSampleLoc.x, 0.5f / brdfLUTDimensions.x, 1.0f - (0.5f / brdfLUTDimensions.x));
        //envBRDFSampleLoc.y = clamp(envBRDFSampleLoc.y, 0.5f / brdfLUTDimensions.y, 1.0f - (0.5f / brdfLUTDimensions.y));

        float2 envBRDF = brdfLUT.Sample(sampler11, envBRDFSampleLoc).rg;
        float3 specularAmbient = prefilteredSpecular * (kS2 * envBRDF.x + envBRDF.y);

        indirectLight = (diffuseAmbient + specularAmbient) * ao;

        //return float4(float3(roughness, roughness, roughness), 1.0f);
       // return float4(prefilteredSpecular, 1.0f);
        //return float4(kS2, 1.0f);
        //return float4(specularAmbient, 1.0f);
        //return float4(envBRDF, 0.0f, 1.0f);
        //return float4(kS2, 1.0f);
    }

   
    float3 totalLighting = directLight + indirectLight;
    //totalLighting = indirectLight;
    return float4(totalLighting, 1.0f);
}