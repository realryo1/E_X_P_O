#include "Common.hlsl"

void main(in VS_IN In, out PS_IN Out)
{
	Out = (PS_IN)0;

	float4 worldPosition = mul(In.Position, World);
	float4 worldNormal = normalize(mul(float4(In.Normal.xyz, 0.0f), World));
	if (Parameter.z >= 2.5f)
	{
		float wave = Null2MembraneOffset(worldPosition.xyz, CameraPosition.w);
		worldPosition.xyz += worldNormal.xyz * wave * Null2Param.z;
		worldNormal.xyz = Null2MembraneNormal(
			worldNormal.xyz,
			worldPosition.xyz,
			CameraPosition.w);
	}

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

	Out.Position = mul(worldPosition, View);
	Out.Position = mul(Out.Position, Projection);
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
