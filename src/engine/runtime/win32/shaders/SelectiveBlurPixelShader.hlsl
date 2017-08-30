struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0 : register(t0);
sampler   sampler0 : register(s0);

Texture2D texture1 : register(t1);
sampler sampler1 : register(s1);

float4 main(PixelInput input) : SV_Target
{
    //discard;
    float4 col = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float alpha = 1.0f - (texture1.Sample(sampler1, input.uv).a + 0.1f);
    if (alpha < 0.0f) {
        discard;
    }
    float2 aspect = float2(1.0f / 1920.0f, 1.0f / 1080.0f);
    
    int numSamples = 0;
    float2 blurRadius = float2(4.0f, 4.0f);
    const int kernelCount = 2;
    for (int k = 0; k < kernelCount; ++k) {
        int kernelWidth = 3 + k * 2;
        float fac = (kernelWidth - 1) * 0.5f;
        for (int i = 0; i < kernelWidth; ++i) {
            for (int j = 0; j < kernelWidth; ++j) {
                float2 sampleOffset = blurRadius * (i - fac, j - fac) * aspect;
                float4 s = texture0.Sample(sampler1, input.uv + sampleOffset);
                s = s / (1.0f + s);
                s = pow(s, 1.0f / 2.2f);
                col += s;
                numSamples++;
            }
        }
    }
    col /= numSamples;
    return float4(col.rgb, 1.0f);
}