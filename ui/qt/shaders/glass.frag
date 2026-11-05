#version 440

// ---------------------------------------------------------------------------
//  Frosted glass composite.
//
//  Takes the dual-Kawase result and applies what the CSS declares on top of
//  it (web_fx/index.html):
//
//    backdrop-filter: blur(3px) saturate(.85)     -> saturation lift here
//    background: rgba(255,255,255,.018)           -> veil
//    clip-path: polygon(...)                      -> trapezoid coverage
//    mask-image: radial-gradient(...)             -> softened ellipse
//    .controlpit::before radial accent glow       -> glow
//
//  Doing the shaping in the shader (instead of opaque overlays) keeps the
//  trapezoid edges antialiased at any scale.
// ---------------------------------------------------------------------------

layout(location = 0) in vec2 qt_TexCoord0;
layout(location = 0) out vec4 fragColor;

layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;

    float saturation;     // CSS saturate(): 1 = unchanged
    float veil;           // white veil alpha, 0.018 in the design
    float shape;          // 0 = rectangle, 1 = trapezoid top, 2 = trapezoid bottom,
                          // 3 = feathered ellipse
    float pitWidth;       // trapezoid narrow edge, 0..1 of the item width
    float edgeSoftness;   // antialias width in item pixels
    vec2  itemSize;

    vec4  glowColor;      // rgb + strength in .a
    float glowFromBottom; // 1 = glow anchored at the bottom edge
} ubuf;

layout(binding = 1) uniform sampler2D blurred;

float trapezoidCoverage (vec2 uv)
{
    // polygon(20% 0, 80% 0, 100% 100%, 0 100%) and its mirror image: the
    // narrow edge is up for the top bar, down for the bottom bar.
    float halfNarrow = ubuf.pitWidth * 0.5;
    float halfWide   = 0.5;

    float y = (ubuf.shape < 1.5) ? uv.y : 1.0 - uv.y;   // 0 = narrow edge
    float halfW = mix(halfNarrow, halfWide, y);
    float d = abs(uv.x - 0.5) - halfW;                  // >0 outside
    float aa = ubuf.edgeSoftness / max(1.0, ubuf.itemSize.x);
    return 1.0 - smoothstep(-aa, aa, d);
}

float ellipseCoverage (vec2 uv)
{
    // mask-image: radial-gradient(ellipse at center, #000 0%, #000 45%, transparent 78%)
    vec2 p = (uv - 0.5) * vec2(ubuf.itemSize.x / max(1.0, ubuf.itemSize.y), 1.0);
    float r = length(p) / 0.5;
    return 1.0 - smoothstep(0.45, 0.78, r);
}

void main()
{
    vec2 uv = qt_TexCoord0;

    vec3 c = texture(blurred, uv).rgb;

    // CSS saturate(.85): mix towards luminance
    float l = dot(c, vec3(0.2126, 0.7152, 0.0722));
    c = mix(vec3(l), c, ubuf.saturation);

    // white veil (background: rgba(255,255,255,.018))
    c = mix(c, vec3(1.0), ubuf.veil);

    // radial accent glow (.controlpit::before)
    if (ubuf.glowColor.a > 0.001)
    {
        vec2 anchor = vec2(0.5, ubuf.glowFromBottom > 0.5 ? 1.0 : 0.0);
        float d = distance(uv, anchor);
        float g = clamp(1.0 - d * 1.2, 0.0, 1.0);
        c += ubuf.glowColor.rgb * ubuf.glowColor.a * g * g;
    }

    float a = 1.0;
    if (ubuf.shape > 2.5)      a = ellipseCoverage(uv);
    else if (ubuf.shape > 0.5) a = trapezoidCoverage(uv);

    fragColor = vec4(c, a) * ubuf.qt_Opacity;
}
