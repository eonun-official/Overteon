#version 440
// One axis of a separable Gaussian — the same dual-Gaussian construction the
// CPU path runs, one direction per pass.
//
// The texel size is taken from textureSize(src, 0) rather than from a uniform.
// A uniform worked in isolation but not in the editor, where Qt's name-based
// fill and the shader's std140 read disagreed and stepX came back as 0.043
// instead of 0.0018; asking the texture itself removes the whole class of
// problem, and it is correct by construction at any device pixel ratio.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float sigma;
    float axis;       // 0 = horizontal pass, 1 = vertical
};
layout(binding = 1) uniform sampler2D src;

void main()
{
    vec2 texel = 1.0 / vec2 (textureSize (src, 0));
    vec2 dir   = axis > 0.5 ? vec2 (0.0, texel.y) : vec2 (texel.x, 0.0);

    float s    = max (sigma, 0.001);
    int   r    = int (ceil (3.0 * s));
    vec4  sum  = texture (src, vTex);
    float wsum = 1.0;
    for (int i = 1; i <= r; ++i)
    {
        float w = exp (-float (i * i) / (2.0 * s * s));
        sum  += (texture (src, vTex + dir * float (i)) +
                 texture (src, vTex - dir * float (i))) * w;
        wsum += 2.0 * w;
    }
    fragColor = (sum / wsum) * qt_Opacity;
}
