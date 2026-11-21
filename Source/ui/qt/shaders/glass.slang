#define OV_GLASS 1
#include "effect.slang"

[[vk::binding(1, 0)]] Sampler2D src;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    float2 uv = input.texCoord;
    float4 color = src.Sample(uv);
    if (uniforms.glow > 0.0005)
    {
        float2 center = float2(0.5, uniforms.trapTop > 0.5 ? 0.0 : 1.0);
        float radius = length((uv - center) / float2(0.70, 1.60));
        float alpha;
        if (radius <= 0.38) alpha = lerp(0.36, 0.13, radius / 0.38);
        else if (radius <= 0.78) alpha = lerp(0.13, 0.00, (radius - 0.38) / 0.40);
        else alpha = 0.0;
        alpha *= uniforms.glow * 0.229;
        color.rgb = lerp(color.rgb, float3(uniforms.glowR, uniforms.glowG, uniforms.glowB),
                         clamp(alpha, 0.0, 1.0));
    }
    float luminance = dot(color.rgb, float3(0.2126, 0.7152, 0.0722));
    color.rgb = lerp(float3(luminance), color.rgb, uniforms.saturation);
    color.rgb = color.rgb * (1.0 - uniforms.veil) + uniforms.veil;

    float inside = 1.0;
    if (uniforms.isEllipse > 0.5)
    {
        float2 point = (uv * 2.0 - 1.0) * 0.70710678;
        inside = 1.0 - smoothstep(0.45, 0.78, length(point));
    }
    else
    {
        float t = uniforms.trapTop > 0.5 ? uv.y : 1.0 - uv.y;
        float low = lerp(0.20, 0.0, t);
        float high = lerp(0.80, 1.0, t);
        inside = step(low, uv.x) * step(uv.x, high);
    }
    return float4(color.rgb * inside, inside) * uniforms.qt_Opacity;
}
