#include "Common.hlsl" //必須インクルード

struct VS_IN_NM
{
    float4 Position : POSITION0;
    float4 Normal   : NORMAL0;
    float4 Diffuse  : COLOR0;
    float2 TexCoord : TEXCOORD0;
};

struct PS_IN_NM
{
    float4 Position      : SV_POSITION;
    float4 WorldPosition : POSITION0;
    float4 Normal        : NORMAL0;
    float4 Tangent       : TANGENT0;   // 接線
    float2 TexCoord      : TEXCOORD0;
    float4 Diffuse       : COLOR0;
};

void main(in VS_IN_NM In, out PS_IN_NM Out)
{
    // 頂点変換
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);
    Out.Position = mul(In.Position, wvp); // 投影空間への変換

    // 法線の回転のみ行い、ピクセルシェーダーへ渡す
    In.Normal.w = 0.0f;
    float4 worldNormal = mul(In.Normal, World);
    Out.Normal = worldNormal;

    // 接線（Tangent）を法線から簡易的に生成する
    float3 N = normalize(worldNormal.xyz);
    float3 Up = abs(N.y) < 0.999f ? float3(0.0f, 1.0f, 0.0f) : float3(1.0f, 0.0f, 0.0f);
    float3 T = normalize(cross(Up, N));
    Out.Tangent = float4(T, 0.0f);

    Out.TexCoord = In.TexCoord;
    Out.WorldPosition = mul(In.Position, World);
    Out.Diffuse = In.Diffuse * MaterialDiffuse;
}
