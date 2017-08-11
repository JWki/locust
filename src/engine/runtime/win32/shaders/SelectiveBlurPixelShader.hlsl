struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};

Texture2D texture0;
sampler   sampler0;

Texture2D texture1;
sampler sampler1;

float4 main(PixelInput input) : SV_Target
{
    //discard;
    float4 col =  texture0.Sample(sampler0, input.uv);
    float alpha = 1.0f - (texture1.Sample(sampler1, input.uv).a + 0.1f);
    if (alpha < 0.0f) {
        discard;
    }

    float2 aspect = float2(1.0f / 1920.0f, 1.0f / 1080.0f);
    for (int i = 0; i < 16; ++i) {
        col += texture0.Sample(sampler0, input.uv + float(i + 1) * float2(1.0f, -1.0f) * aspect);
        col += texture0.Sample(sampler0, input.uv + float(i + 1) * float2(1.0f, 1.0f) * aspect);
        col += texture0.Sample(sampler0, input.uv + float(i + 1) * float2(-1.0f, -1.0f) * aspect);
        col += texture0.Sample(sampler0, input.uv + float(i + 1) * float2(-1.0f, 1.0f) * aspect);
    }
    col = col / (4.0f * 16.0f + 1.0f);

    return float4(col.rgb, 1.0f);
}