#include "effect.slang"
#include "gaussian.slang"

[[vk::binding(1, 0)]] Sampler2D src;

[shader("fragment")]
float4 fragmentMain(VertexOutput input) : SV_Target0
{
    uint width, height;
    src.GetDimensions(width, height);
    float2 texel = 1.0 / float2(width, height);
    float2 direction = uniforms.axis > 0.5 ? float2(0.0, texel.y) : float2(texel.x, 0.0);
    float t = clamp((abs(input.texCoord.y - uniforms.fogMid) - uniforms.fogGap)
                    / max(uniforms.fogRange, 1e-5), 0.0, 1.0);
    float4 color = dualGaussian(src, input.texCoord, direction,
                                uniforms.sigma + uniforms.sigmaSlope * t);
    if (uniforms.alphaMul > 0.5)
        color *= pow(max(0.0, 1.0 - t), 1.15);
    return color * uniforms.qt_Opacity;
}
