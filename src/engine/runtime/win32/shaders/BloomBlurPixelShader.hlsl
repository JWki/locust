struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0 : register(t0);
sampler   sampler0 : register(s0);

Texture2D texture1 : register(t1);
sampler sampler1 : register(s1);

float4 blur9(Texture2D image, sampler smpler, float2 uv, float2 resolution, float2 direction) {
    float4 color = 0.0f;
    float2 off1 = 1.3846153846 * direction;
    float2 off2 = 3.2307692308 * direction;
    color += image.Sample(smpler, uv) * 0.2270270270 * (texture1.Sample(sampler1, uv).a > 1.0f ? 1.0f : 0.0f);
    color += image.Sample(smpler, uv + (off1 / resolution)) * 0.3162162162 * (texture1.Sample(sampler1, uv + (off1 / resolution)).a > 1.0f ? 1.0f : 0.0f);
    color += image.Sample(smpler, uv - (off1 / resolution)) * 0.3162162162 * (texture1.Sample(sampler1, uv - (off1 / resolution)).a > 1.0f ? 1.0f : 0.0f);
    color += image.Sample(smpler, uv + (off2 / resolution)) * 0.0702702703 * (texture1.Sample(sampler1, uv + (off2 / resolution)).a > 1.0f ? 1.0f : 0.0f);
    color += image.Sample(smpler, uv - (off2 / resolution)) * 0.0702702703 * (texture1.Sample(sampler1, uv - (off2 / resolution)).a > 1.0f ? 1.0f : 0.0f);
    return color;
}

float4 main(PixelInput input) : SV_Target
{
    //discard;
    //float4 col = float4(0.0f, 0.0f, 0.0f, 1.0f);
    float2 aspect = float2(1.0f / 1920.0f, 1.0f / 1080.0f);

    float2 res;
    texture0.GetDimensions(res.x, res.y);
    
    float4 colA = blur9(texture0, sampler0, input.uv, res, float2(1.0f, 0.0f));
    float4 colB = blur9(texture0, sampler0, input.uv, res, float2(0.0f, 1.0f));
    float4 colC = blur9(texture0, sampler0, input.uv, res, float2(1.0f, 1.0f));
    //float4 colD = blur9(texture0, sampler0, input.uv, res, float2(0.0f, 0.0f));
    //float4 colE = blur9(texture0, sampler0, input.uv, res, float2(0.0f, 1.0f));
    //float4 colF = blur9(texture0, sampler0, input.uv, res, float2(0.0f, 1.0f));
    float4 col = colA + colB + colC;
    /*
    float4 colB //if (a < 1.0f) { discard; }
    int numSamples = 0;
    float2 blurRadius = float2(4.0f, 4.0f);
    const int kernelCount = 2;
    for (int k = 0; k < kernelCount; ++k) {
        int kernelWidth = 3 + k * 2;
        float fac = (kernelWidth - 1) * 0.5f;
        for (int i = -kernelWidth / 2; i < kernelWidth / 2; ++i) {
            for (int j = -kernelWidth / 2; j < kernelWidth / 2; ++j) {
                float2 sampleOffset = blurRadius * (i - fac, j - fac) * aspect;
                float4 s = texture0.Sample(sampler0, input.uv + sampleOffset);
                float a = texture1.Sample(sampler1, input.uv + sampleOffset).a;
                if (a < 1.0f) { s = 0.0f; }
                s = s / (1.0f + s);
                s = pow(s, 1.0f / 2.2f);
                col += s;
                numSamples++;
            }
        }
    }
    col /= numSamples;
    */
    return float4(col.rgb, 1.0f);
}