#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
	Out = (PS_IN)0;

	matrix wvp = mul(World, View);
	wvp = mul(wvp, Projection);

	float4 worldPosition = mul(In.Position, World);
	float4 worldNormal = normalize(mul(float4(In.Normal.xyz, 0.0f), World));

	float3 materialTint = MaterialDiffuse.rgb;
	float tintMax = max(max(materialTint.r, materialTint.g), materialTint.b);
	if (tintMax > 0.001f)
	{
		materialTint /= tintMax;
	}
	else
	{
		materialTint = float3(1.0f, 1.0f, 1.0f);
	}

	Out.Position = mul(In.Position, wvp);
	Out.WorldPosition = worldPosition;
	Out.Normal = worldNormal;
	Out.Diffuse.rgb = In.Diffuse.rgb * materialTint;
	Out.Diffuse.a = In.Diffuse.a * MaterialDiffuse.a;
	Out.TexCoord = In.TexCoord;
	if (Parameter.w > 0.5f)
	{
		Out.ShadowPosition = mul(worldPosition, LightViewProjection[0]);
	}
	else
	{
		Out.ShadowPosition = float4(0.0f, 0.0f, 0.0f, 0.0f);
	}
}
