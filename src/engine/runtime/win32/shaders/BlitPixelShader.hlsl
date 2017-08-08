struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

float4 main(PixelInput input) : SV_Target
{
    return texture0.Sample(sampler0, input.uv.xy);
}