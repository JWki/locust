
struct PixelInput
{
    float4 pos : SV_POSITION;
    float2 uv : TEXCOORD;
};


// need SM 4.0 for this!!
PixelInput main(uint vI : SV_VERTEXID)
{
    float2 texcoord = float2(vI & 1, vI >> 1); 

    PixelInput output;
    // * 1 should be * 2!
    output.pos = float4((texcoord.x - 0.5f) * 2, -(texcoord.y - 0.5f) * 2, 0, 1);
    output.uv = texcoord;
    return output;
}