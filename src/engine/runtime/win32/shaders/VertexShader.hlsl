struct Vertex
{
    float4 pos : POSITION;
    float4 color : COLOR;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
};

PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.pos = vertex.pos;
    output.color = vertex.color;
    return output;
}