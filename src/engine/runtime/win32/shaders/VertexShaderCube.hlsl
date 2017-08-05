cbuffer Object : register(cb0) {
    float4x4 WorldMatrix;
};

struct Vertex
{
    float4 pos : POSITION;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.pos = mul(WorldMatrix, vertex.pos);
    output.color = vertex.pos * 0.5f + 0.5f;
    return output;
}