struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;



float4 main(PixelInput input) : SV_Target
{
    float3 texColor = texture0.Sample(sampler0, input.uv).rgb;
    //texColor *= 16.0f;
    texColor = texColor / (1.0f + texColor);
    return float4(pow(texColor, 1.0f / 2.2f), 1.0f);
}