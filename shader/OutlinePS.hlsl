#include "Common.hlsl"

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // テクスチャやライトは使わず、単色で輪郭を描く
    outDiffuse = In.Diffuse;
}
