cbuffer Object : register(cb0) {
    float4x4    ObjToViewMatrix;
    float4x4    ObjToProjMatrix;
    float2      CursorPos;
    //float2      _padding0;
    float4      Color;
    float       BrushSize;
    //float3      _padding1;
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
    float4 screenPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
    float NdV : TEXCOORD2;
};


PixelInput main(Vertex vertex)
{
    PixelInput output;
    output.screenPos = mul(ObjToProjMatrix, float4(vertex.pos.xyz, 1.0f));
    output.screenPos.xyz = output.screenPos.xyz / output.screenPos.w;
    output.uv = vertex.uv;
    float3 n = mul(ObjToViewMatrix, float4(vertex.normal.xyz, 0.0f)).xyz;
    output.NdV = dot(normalize(n), -float3(0.0f, 0.0f, 1.0f));
    float2 uv = float2(vertex.uv.x, 1.0f - vertex.uv.y) * 2.0f - 1.0f;
    output.pos = float4(uv, 0.0f, 1.0f);
    return output;
}