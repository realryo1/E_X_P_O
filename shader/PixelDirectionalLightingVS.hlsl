#include "Common.hlsl" //必須インクルード

void main(in VS_IN In, out PS_IN Out)
{
	Out = (PS_IN) 0; // 追加: 出力構造体を完全初期化
	
    // 頂点変換
    matrix wvp;
    wvp = mul(World, View);
    wvp = mul(wvp, Projection);
    Out.Position = mul(In.Position, wvp); // 投影空間への変換

    // 法線の回転のみ行い、ピクセルシェーダーへ渡す(ライティング計算はしない)
    In.Normal.w = 0.0f;
    float4 worldNormal = mul(In.Normal, World);
    Out.Normal = worldNormal;
	
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

	Out.Diffuse.rgb = In.Diffuse.rgb * materialTint; //モデルそのもののMaterialカラーのみを乗算（Mayaで言うハイパーシェード）
	Out.Diffuse.a = In.Diffuse.a;
    Out.TexCoord = In.TexCoord;
    Out.WorldPosition = mul(In.Position, World);
}
