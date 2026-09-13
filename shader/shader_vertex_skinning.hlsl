//===============================================================================================
//
// AnimSprite3D用スキニングシェーダー
// ボーンに従って頂点を動かすための専用VertexShader。
// ShaderManagerの通常VSは使わず、このVSで動かした結果を各PixelShaderへ渡す。
//
//===============================================================================================

#include "Common.hlsl"

#define MAX_BONES 256

cbuffer BoneBuffer : register(b7)
{
    row_major float4x4 BoneMatrices[MAX_BONES];
};

struct VS_IN_SKINNED
{
    float4 Position   : POSITION0;
    float4 Normal     : NORMAL0;
    float4 Diffuse    : COLOR0;
    float2 TexCoord   : TEXCOORD0;
    uint4  BoneIndex  : BLENDINDICES0;
    float4 BoneWeight : BLENDWEIGHT0;
};

void main(in VS_IN_SKINNED In, out PS_IN Out)
{
    Out = (PS_IN)0;

    float4 skinnedPos = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float4 skinnedNormal = float4(0.0f, 0.0f, 0.0f, 0.0f);
    float totalWeight = In.BoneWeight.x + In.BoneWeight.y + In.BoneWeight.z + In.BoneWeight.w;

    if (totalWeight > 0.0f)
    {
        // 1頂点に最大4本のボーンを混ぜて、アニメ後の位置と法線を作る。
        [unroll]
        for (int i = 0; i < 4; i++)
        {
            float w = In.BoneWeight[i];
            uint idx = In.BoneIndex[i];
            if (w > 0.0f && idx < MAX_BONES)
            {
                skinnedPos += w * mul(In.Position, BoneMatrices[idx]);
                skinnedNormal += w * mul(float4(In.Normal.xyz, 0.0f), BoneMatrices[idx]);
            }
        }
        skinnedPos.w = 1.0f;
    }
    else
    {
        // ボーン情報がない頂点は、元の位置と法線のまま描画する。
        skinnedPos = In.Position;
        skinnedNormal = float4(In.Normal.xyz, 0.0f);
    }

    matrix wvp = mul(World, View);
    wvp = mul(wvp, Projection);

    float4 worldPosition = mul(skinnedPos, World);
    float4 worldNormal = normalize(mul(float4(skinnedNormal.xyz, 0.0f), World));

    // マテリアル色は通常モデルのPixel系VSと同じく、色味だけを頂点色へ混ぜる。
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

    Out.Position = mul(skinnedPos, wvp);
    Out.WorldPosition = worldPosition;
    Out.Normal = worldNormal;
    Out.Diffuse.rgb = In.Diffuse.rgb * materialTint;
    Out.Diffuse.a = In.Diffuse.a * MaterialDiffuse.a;
    Out.TexCoord = In.TexCoord;

    // 影を受けるPixelShaderでも使えるよう、ライト視点の座標も作っておく。
    Out.ShadowPosition = mul(worldPosition, LightViewProjection[0]);
}
