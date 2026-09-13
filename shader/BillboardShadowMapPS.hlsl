Texture2D g_Texture : register(t0);
SamplerState g_SamplerState : register(s0);

struct BILLBOARD_SHADOW_PS_IN
{
    float4 Position : SV_POSITION;
    float2 TexCoord : TEXCOORD0;
};

void main(in BILLBOARD_SHADOW_PS_IN In)
{
    // Transparent sprite pixels must not write depth into the shadow map.
    clip(g_Texture.Sample(g_SamplerState, In.TexCoord).a - 0.1f);
}
