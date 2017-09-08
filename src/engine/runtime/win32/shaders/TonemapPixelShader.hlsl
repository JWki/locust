struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

// John Hable's Uncharted 2 tonemapping operator
// taken from http://filmicworlds.com/blog/filmic-tonemapping-operators/

static const float A = 0.15f;
static const float B = 0.50f;
static const float C = 0.10f;
static const float D = 0.20f;
static const float E = 0.02f;
static const float F = 0.30f;
static const float W = 11.2f;

float3 Uncharted2Tonemap(float3 x)
{
    return ((x*(A*x + C*B) + D*E) / (x*(A*x + B) + D*F)) - E / F;
}

float4 main(PixelInput input) : SV_Target
{
    float3 texColor = texture0.Sample(sampler0, input.uv).rgb;

    texColor *= 4.0f;

    float ExposureBias = 2.0f;
    float3 curr = Uncharted2Tonemap(ExposureBias*texColor);

    float3 whiteScale = 1.0f / Uncharted2Tonemap(W);
    float3 color = curr * whiteScale;

    float3 retColor = pow(color, 1.0f / 2.2f);
    return float4(retColor, 1.0f);
}