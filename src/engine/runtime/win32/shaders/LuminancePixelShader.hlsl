struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

float4 main(PixelInput input) : SV_Target
{
    float3 rgb = texture0.Sample(sampler0, input.uv).rgb;
    const float3 W = float3(0.2125f, 0.7154f, 0.0721f);
    float luminance = dot(rgb, W);
    if (luminance > 1.0f) {
        return float4(rgb, luminance);
    }
    else {
        return float4(float3(0.0f, 0.0f, 0.0f), luminance);
    }
}