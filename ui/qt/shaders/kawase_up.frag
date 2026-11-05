#version 440

// ---------------------------------------------------------------------------
//  Dual-Kawase, pass 2 of 2: upsample.
//
//  A 3x3 tent on the coarser level, blended 50/50 with the finer level from
//  the downward pass — the "dual" in dual-Kawase.  Walking back up rebuilds a
//  wide, smooth blur that still keeps the stroke structure, which is what the
//  CSS backdrop-filter in web_fx/index.html looks like.
// ---------------------------------------------------------------------------

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 texel;          // 1.0 / size of the COARSE source
} ubuf;

layout(binding = 1) uniform sampler2D coarse;
layout(binding = 2) uniform sampler2D fine;

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 d  = ubuf.texel;          // one coarse texel

    vec4 c = vec4(0.0);
    float wsum = 0.0;

    for (int j = -1; j <= 1; ++j)
    {
        for (int i = -1; i <= 1; ++i)
        {
            float k = (i == 0 ? 2.0 : 1.0) * (j == 0 ? 2.0 : 1.0);
            c += texture(coarse, uv + vec2(float(i) * d.x, float(j) * d.y)) * k;
            wsum += k;
        }
    }

    c /= wsum;
    fragColor = mix(c, texture(fine, uv), 0.5) * ubuf.qt_Opacity;
}
