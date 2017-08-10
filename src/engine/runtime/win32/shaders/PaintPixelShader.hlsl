cbuffer Object : register(cb0) {
    float4x4    ObjToViewMatrix;
    float2      CursorPos;
    float2      _padding0;
    float4      Color;
    float       BrushSize;
    //float3      _padding1;
};

struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 screenPos : TEXCOORD0;
    float2 uv : TEXCOORD1;
};

Texture2D texture0;
sampler   sampler0;

float4 main(PixelInput vertex) : SV_TARGET
{
   
    float2 cPos = float2(CursorPos.x / 1920.0f, 1.0f - (CursorPos.y / 1080.0f)) * float2(2.0f, 2.0f) - float2(1.0f, 1.0f);
    float dist = length((vertex.screenPos.xy - cPos) * float2((1920.0f / 1080.0f), 1.0f));
    float alpha = clamp((1.0f - dist* (1080.0f / BrushSize)), 0.0f, 1.0f);
    //return float4(vertex.screenPos.xy, 0.0f, 1.0f);
    //return float4(vertex.screenPos.xy, 0.0f, dist);
    //return float4(float3(1.0f, 1.0f, 1.0f) * (1.0f - dist), 1.0f);
    float3 color = Color.rgb * texture0.Sample(sampler0, vertex.uv * 4.0f).rgb;
    //return float4(color, alpha * Color.a * Color.a);
	return float4(pow(color, 2.2f), alpha * Color.a * Color.a);
}