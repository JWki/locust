cbuffer Object : register(cb0) {
    float4x4    WorldMatrix;
    float4      Color;
    float4      LightDir;
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
};



PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.pos = mul(WorldMatrix, vertex.pos);
    output.color = Color;
    output.normal = mul(WorldMatrix, float4(vertex.normal.xyz, 0.0f));
    output.uv = vertex.uv;
    //output.uv = (vertex.pos.xy + 0.5f);
    //output.uv.y = -output.uv.y;
    //output.pos = float4(output.uv * 2.0f - 1.0f, 0.5f, 1.0f);
    return output;
}