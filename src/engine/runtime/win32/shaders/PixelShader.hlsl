struct PixelInput
{
    float4 pos : SV_POSITION;
    float4 color : COLOR;
    float4 normal : NORMAL;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

Texture2D texture1;
sampler   sampler1;

float4 main(PixelInput input) : SV_TARGET
{
    float4 n = normalize(input.normal) * 0.5f + 0.5f;
    float4 paintColor = texture1.Sample(sampler1, input.uv.xy);
    return texture0.Sample(sampler0, input.uv.xy) + paintColor;
}