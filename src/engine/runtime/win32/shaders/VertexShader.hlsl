cbuffer Object : register(cb0) {
    float4x4 WorldMatrix;
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
};

PixelInput main(Vertex vertex)
{
    PixelInput output;
    float4 targetPos = mul(WorldMatrix, vertex.pos);
    output.pos = lerp(vertex.pos, targetPos, vertex.weight);
    output.color = vertex.color;
    return output;
}