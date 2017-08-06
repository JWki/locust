struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

float4 main(PixelInput input) : SV_TARGET
{
    float4 n = normalize(input.normal) * 0.5f + 0.5f;
	return n * input.color * texture0.Sample(sampler0, input.uv.xy);
}