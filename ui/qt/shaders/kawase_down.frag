#version 440

// ---------------------------------------------------------------------------
//  Dual-Kawase, pass 1 of 2: downsample.
//
//  Same kernel as the HTML FrostEngine (web_fx/index.html): the centre tap
//  weighted 4 plus the four half-texel corners, normalised by 8.  Rendering
//  into a half-size target means each output pixel already averages a 2x2
//  block through bilinear sampling, so three of these give the classic
//  1/2, 1/4, 1/8 pyramid.
// ---------------------------------------------------------------------------

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4 qt_Matrix;
    float qt_Opacity;
    vec2 texel;          // 1.0 / size of the SOURCE
} ubuf;

layout(binding = 1) uniform sampler2D source;

void main()
{
    vec2 uv = qt_TexCoord0;
    vec2 d  = ubuf.texel * 0.5;

    vec4 c  = texture(source, uv) * 4.0;
    c += texture(source, uv + vec2(-d.x, -d.y));
    c += texture(source, uv + vec2( d.x, -d.y));
    c += texture(source, uv + vec2(-d.x,  d.y));
    c += texture(source, uv + vec2( d.x,  d.y));

    fragColor = (c / 8.0) * ubuf.qt_Opacity;
}
