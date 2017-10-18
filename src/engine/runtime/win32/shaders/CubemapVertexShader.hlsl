cbuffer Object : register(b0) {
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

struct Vertex
{
    float4 pos : POSITION;
};

struct PixelInput
{
    float4 clipPos : SV_POSITION;
    float4 localPos : TEXCOORD0;
};



PixelInput main(Vertex vertex)
{
    PixelInput output;

    // isolate rotation from View matrix
    float3 rotPosition = mul((float3x3)View, vertex.pos.xyz);
    output.clipPos = mul(Projection, float4(rotPosition, 1.0f));
    //output.clipPos = mul(ViewProjection, vertex.pos);
    output.localPos = vertex.pos;

    return output;
}