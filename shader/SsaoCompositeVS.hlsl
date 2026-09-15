#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
    Out = (PS_IN)0;
    Out.Position = float4(In.Position.xy, 0.0f, 1.0f);
    Out.TexCoord = In.TexCoord;
}
