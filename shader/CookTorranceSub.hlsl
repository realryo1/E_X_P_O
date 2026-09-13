// Cook-Torrance 物理ベースライティング用サブ関数定義

// マイクロファセット分布（Beckmann分布関数）
float CalculateBeckmann(float smooth, float nh)
{
    float m = 1.0f - smooth;
    m = max(m, 0.01f); // ゼロ除算対策
    float m2 = m * m;
    float nh2 = nh * nh;
    float nh4 = nh2 * nh2;
    nh4 = max(nh4, 0.0001f); // ゼロ除算対策
    
    // tan^2(theta_H) = (1 - cos^2(theta_H)) / cos^2(theta_H)
    float exponent = (nh2 - 1.0f) / (m2 * nh2);
    float D = exp(exponent) / (3.14159265f * m2 * nh4);
    return D;
}

// Schlick近似によるフレネル反射
float CalculateFresnel(float metallic, float lDotH)
{
    float f0 = lerp(0.04f, 1.0f, metallic);
    return f0 + (1.0f - f0) * pow(1.0f - saturate(lDotH), 5.0f);
}

// 幾何減衰（Cook-Torrance幾何減衰）
float CalculateGeometricDamping(float nh, float nv, float nl, float vh)
{
    vh = max(vh, 0.001f); // ゼロ除算対策
    float g1 = (2.0f * nh * nv) / vh;
    float g2 = (2.0f * nh * nl) / vh;
    return min(1.0f, min(g1, g2));
}
