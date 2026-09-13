#include "Common.hlsl" //必須インクルード

Texture2D g_Texture : register(t0);
Texture2D g_NormalMap : register(t2);
Texture2D g_EmissiveMap : register(t5);
SamplerState g_SamplerState : register(s0);

// --- 4面ShadowMap（トンネルの各面用）---
// 面ごとのライト行列と、4スライスのShadowMap配列。サンプラーはCommon.hlslのs1を流用。
cbuffer FaceShadowBuffer : register(b9)
{
    matrix FaceLightViewProjection[4];
    float4 FaceShadowParam;  // x:バイアス y:Playerの影の濃さ z,w:1テクセルUVサイズ
    float4 FaceShadowParam2; // x:Enemyの影の濃さ
};
Texture2DArray g_FaceShadowMap : register(t6);

// worldPos がトンネルのどの面かを返す (0=FLOOR,1=LEFT_WALL,2=CEILING,3=RIGHT_WALL)
int DetectTunnelFace(float3 worldPos)
{
    if (abs(worldPos.y) >= abs(worldPos.x))
        return (worldPos.y < 0.0f) ? 0 : 2; // 下→床 / 上→天井
    else
        return (worldPos.x < 0.0f) ? 1 : 3; // 左→左壁 / 右→右壁
}

// 指定スライス(slice)で影の明るさを求める。brightnessは影部分の明るさ(0=真っ黒〜1=影なし)。
float SampleFaceShadow(float3 worldPos, int face, int slice, float brightness)
{
    float4 shadowPosition = mul(float4(worldPos, 1.0f), FaceLightViewProjection[face]);

    float3 proj = shadowPosition.xyz / shadowPosition.w;
    float2 uv = float2(proj.x * 0.5f + 0.5f, -proj.y * 0.5f + 0.5f);

    // 範囲外は影なし
    float result = 1.0f;
    if (uv.x >= 0.0f && uv.x <= 1.0f && uv.y >= 0.0f && uv.y <= 1.0f && proj.z >= 0.0f && proj.z <= 1.0f)
    {
        float currentDepth = proj.z - FaceShadowParam.x;
        float2 texelSize = FaceShadowParam.zw;

        float litCount = 0.0f;
        [unroll]
        for (int y = -1; y <= 1; y++)
        {
            [unroll]
            for (int x = -1; x <= 1; x++)
            {
                float2 sampleUV = uv + float2(x, y) * texelSize;
                float shadowDepth = g_FaceShadowMap.Sample(g_ShadowSampler, float3(sampleUV, (float)slice)).r;
                litCount += (currentDepth > shadowDepth) ? 0.0f : 1.0f;
            }
        }
        float visibility = litCount / 9.0f;
        result = lerp(brightness, 1.0f, visibility);
    }
    return result;
}

// この面のPlayer影(スライス face)とEnemy影(スライス face+4)を、それぞれの濃さで求めて合成。
float CalcFaceShadow(float3 worldPos)
{
    int face = DetectTunnelFace(worldPos);
    float shadowPlayer = SampleFaceShadow(worldPos, face, face,     FaceShadowParam.y);  // Player: 0-3
    float shadowEnemy  = SampleFaceShadow(worldPos, face, face + 4, FaceShadowParam2.x); // Enemy : 4-7
    // 濃い方(暗い方)を採用
    return min(shadowPlayer, shadowEnemy);
}

float3 CalcNormalMapWorldNormal(PS_IN In)
{
    float3 baseNormal = normalize(In.Normal.xyz);
    float3 normalSample = g_NormalMap.Sample(g_SamplerState, In.TexCoord).xyz * 2.0f - 1.0f;
    float3 tangent;
    float3 bitangent;

    float3 dp1 = ddx(In.WorldPosition.xyz);
    float3 dp2 = ddy(In.WorldPosition.xyz);
    float2 duv1 = ddx(In.TexCoord);
    float2 duv2 = ddy(In.TexCoord);

    float det = duv1.x * duv2.y - duv1.y * duv2.x;
    if (abs(det) > 0.00000001f)
    {
        float invDet = 1.0f / det;
        tangent = (dp1 * duv2.y - dp2 * duv1.y) * invDet;
        bitangent = (dp2 * duv1.x - dp1 * duv2.x) * invDet;
    }
    else
    {
        float3 axis = (abs(baseNormal.y) < 0.99f) ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
        tangent = cross(axis, baseNormal);
        bitangent = cross(baseNormal, tangent);
    }

    tangent = normalize(tangent);
    bitangent = normalize(bitangent);

    return normalize(
        normalSample.x * tangent +
        normalSample.y * bitangent +
        normalSample.z * baseNormal
    );
}

// 通常のPhong(点光源ランバート)に、ShadowMapの落ち影を掛け合わせる。
void main(in PS_IN In, out float4 outDiffuse : SV_Target)
{
    float3 worldNormal = CalcNormalMapWorldNormal(In);
    float3 toLight = Light.Position.xyz - In.WorldPosition.xyz;
    float distanceToLight = length(toLight);
    float3 lightDirection = (distanceToLight > 0.001f) ? toLight / distanceToLight : float3(0.0f, 1.0f, 0.0f);
    float lightRange = max(Light.PointLightParam.x, 0.001f);
    float lightIntensity = max(Light.PointLightParam.y, 0.0f);
    float attenuation = saturate(1.0f - distanceToLight / lightRange);
    attenuation = attenuation * attenuation * lightIntensity;

    float light = 0.0f;
    if (Light.Enable)
    {
        light = saturate(dot(lightDirection, worldNormal)) * attenuation;
    }

    float4 texColor = g_Texture.Sample(g_SamplerState, In.TexCoord);
    float3 emissive = g_EmissiveMap.Sample(g_SamplerState, In.TexCoord).rgb;
    float3 baseColor = texColor.rgb * In.Diffuse.rgb;
    float3 ambient = saturate(Light.Ambient.rgb);

    // 落ち影：面ごとのShadowMapから、この画素の面に対応する影を求める。
    float shadow = CalcFaceShadow(In.WorldPosition.xyz);

    float3 diffuse = baseColor * light * Light.Diffuse.rgb * shadow;

    outDiffuse.rgb = saturate(baseColor * ambient + diffuse + emissive);
    outDiffuse.a = texColor.a * In.Diffuse.a;
}
