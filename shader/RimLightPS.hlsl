#include "Common.hlsl"

Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    // 元のLambert照明を残し、Gargoyleの質感とトンネル内の明暗を維持する。
    float3 normal = normalize(In.Normal.xyz);
    float3 toLight = Light.Position.xyz - In.WorldPosition.xyz;
    float distanceToLight = length(toLight);
    float3 lightDir = (distanceToLight > 0.001f) ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);

    float lightRange = max(Light.PointLightParam.x, 0.001f);
    float lightIntensity = max(Light.PointLightParam.y, 0.0f);
    float attenuation = saturate(1.0f - distanceToLight / lightRange);
    attenuation = attenuation * attenuation * lightIntensity;

    float light = 0.0f;
    if (Light.Enable)
    {
        light = saturate(dot(normal, lightDir)) * attenuation;
    }

    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    float3 baseColor = texColor.rgb * In.Diffuse.rgb;
    float3 ambient = saturate(Light.Ambient.rgb);
    float3 diffuse = baseColor * light * Light.Diffuse.rgb;

    // リムライトは、面の法線とカメラ方向の角度で強さが変わる。
    float3 viewDir = normalize(CameraPosition.xyz - In.WorldPosition.xyz);
    float rim = 1.0f - saturate(dot(normal, viewDir));
    rim = smoothstep(0.35f, 0.95f, rim);

    // Parameter.rgbはGargoyleごとの色、Parameter.aはリムの強さ。
    float3 rimColor = max(Parameter.rgb, 0.0f) * rim * max(Parameter.a, 0.0f);

    outDiffuse.rgb = saturate(baseColor * ambient + diffuse + rimColor);
    outDiffuse.a = texColor.a * In.Diffuse.a;
}
