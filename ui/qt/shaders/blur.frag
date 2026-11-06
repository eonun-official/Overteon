#version 440
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float stepX;
    float stepY;
    float sigma;
    float pad;
};
layout(binding = 0) uniform sampler2D src;

void main()
{
    vec2  step = vec2 (stepX, stepY);
    float s    = max (sigma, 0.001);
    int   r    = int (ceil (3.0 * s));
    vec4  sum  = texture (src, vTex);
    float wsum = 1.0;
    for (int i = 1; i <= r; ++i)
    {
        float w = exp (-float (i * i) / (2.0 * s * s));
        sum  += (texture (src, vTex + step * float (i)) +
                 texture (src, vTex - step * float (i))) * w;
        wsum += 2.0 * w;
    }
    fragColor = (sum / wsum) * qt_Opacity;
}
