struct PixelInput
{
    float4 clipPos : SV_POSITION;
    float4 localPos : TEXCOORD0;
};

TextureCube cubemap;
sampler   sampler0;

float4 main(PixelInput input) : SV_Target
{
    return pow(cubemap.Sample(sampler0, normalize(input.localPos)), 2.2f);
}