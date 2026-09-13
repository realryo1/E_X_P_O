#include "Common.hlsl"

struct BILLBOARD_SHADOW_PS_IN
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

void main(in VS_IN In, out BILLBOARD_SHADOW_PS_IN Out)
{
    matrix wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    Out.Position = mul(In.Position, wvp);
    Out.TexCoord = In.TexCoord;
}
