#include "Common.hlsl"

// 水面の多角形。頂点はワールド座標で、水域ごとの水平な高さに置いてある。
void main(in VS_IN In, out PS_IN Out)
{
	Out = (PS_IN)0;

	float4 worldPosition = mul(In.Position, World);
	Out.Position = mul(worldPosition, View);
	Out.Position = mul(Out.Position, Projection);
	Out.WorldPosition = worldPosition;
	Out.Normal = float4(0.0f, 1.0f, 0.0f, 0.0f);
	Out.Diffuse = In.Diffuse;
	Out.TexCoord = In.TexCoord;
}
