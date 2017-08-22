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

struct Vertex
{
    float4 pos : POSITION;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
    float4 worldPos : TEXCOORD1;
};



PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.pos = mul(ModelViewProjection, vertex.pos);
    output.worldPos = mul(Model, vertex.pos);
    output.color = Color;
    output.normal = mul(Model, float4(vertex.normal.xyz, 0.0f));
    output.uv = vertex.uv;
    //output.uv = (vertex.pos.xy + 0.5f);
    //output.uv.y = -output.uv.y;
    //output.pos = float4(output.uv * 2.0f - 1.0f, 0.5f, 1.0f);
    return output;
}