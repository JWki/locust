cbuffer Object : register(cb0) {
    float4x4 WorldMatrix;
};

struct Vertex
{
    float4 pos : POSITION;
    float4 normal : NORMAL;
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
    output.pos = mul(WorldMatrix, vertex.pos);
    output.color = float4(1.0f, 1.0f, 1.0f, 1.0f);
    output.normal = vertex.normal;
    output.uv = vertex.pos.xy;
    return output;
}