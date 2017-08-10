cbuffer Object : register(cb0) {
    float4x4    ObjToViewMatrix;
    float2      CursorPos;
    float4      Color;
    float       BrushSize;
};

struct Vertex
{
    float4 pos : POSITION;
    float2 uv : TEXCOORD;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 screenPos : TEXCOORD;
};


PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.screenPos = mul(ObjToViewMatrix, float4(vertex.pos.xyz, 1.0f));
    output.screenPos.xyz = output.screenPos.xyz / output.screenPos.w;
    float2 uv = float2(vertex.uv.x, 1.0f - vertex.uv.y) * 2.0f - 1.0f;
    output.pos = float4(uv, 0.0f, 1.0f);
    return output;
}