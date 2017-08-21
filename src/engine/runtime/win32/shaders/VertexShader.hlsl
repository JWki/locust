cbuffer Object : register(cb0) {
    float4x4    ModelViewProjection;
    float4x4    ModelView;
    float4x4    ViewProjection;
    float4x4    View;
    float4x4    Projection;
    float4x4    Model;
    float4      Color;
    float4      LightDir;
};

struct Vertex
{
    float4 pos : POSITION;
    float4 color : COLOR;
    float weight : TEXCOORD;    
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};

PixelInput main(Vertex vertex)
{
    PixelInput output;
    float4 targetPos = mul(ModelViewProjection, vertex.pos);
    output.pos = lerp(vertex.pos, targetPos, vertex.weight);
    output.color = vertex.color * Color;
    output.normal = float4(0.0f, 0.0f, 1.0f, 0.0f);
    output.uv = vertex.pos.xy;
    return output;
}