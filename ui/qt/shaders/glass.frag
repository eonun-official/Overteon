#version 440
// The design's glass finish, in one pass:
//   .controlpit::after / #hdr::before  -> shape mask + rgba(255,255,255,.018)
//   .controlpit::before               -> accent glow, opacity = var(--light)
// plus saturate(.85), which the CSS applies together with the blur.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    vec2  sizePx;
    float veil;
    float glow;
    vec4  glowColor;
    float isEllipse;
    float trapTop;
    float saturation;
};
layout(binding = 0) uniform sampler2D src;

void main()
{
    vec4 c = texture (src, vTex);

    // ---- saturate(.85) around luminance --------------------------------
    float l = dot (c.rgb, vec3 (0.2126, 0.7152, 0.0722));
    c.rgb = mix (vec3 (l), c.rgb, saturation);

    // ---- veil: rgba(255,255,255,.018) ----------------------------------
    c.rgb = c.rgb * (1.0 - veil) + vec3 (1.0) * veil;

    // ---- shape ---------------------------------------------------------
    float inside = 1.0;
    if (isEllipse > 0.5)
    {
        // mask-image: radial-gradient(ellipse at center,
        //             #000 0%, #000 45%, transparent 78%)
        // No explicit size => farthest-corner: half-axes are half * sqrt(2).
        vec2 p = (vTex * 2.0 - 1.0) * 0.70710678;
        inside = 1.0 - smoothstep (0.415, 0.72, length (p));
    }
    else
    {
        // clip-path: polygon(20% 0,80% 0,100% 100%,0 100%)
        float t  = trapTop > 0.5 ? vTex.y : (1.0 - vTex.y);
        float lo = mix (0.20, 0.0, t);
        float hi = mix (0.80, 1.0, t);
        inside = step (lo, vTex.x) * step (vTex.x, hi);
    }

    // ---- .controlpit::before accent glow -------------------------------
    //   radial-gradient(ellipse 70% 160% at 50% 0%,
    //                   accent .36 0%, accent .13 38%, accent 0 78%)
    if (glow > 0.0005)
    {
        vec2  centre = vec2 (0.5, trapTop > 0.5 ? 0.0 : 1.0);
        vec2  d2 = (vTex - centre) / vec2 (0.70, 1.60);
        float r  = length (d2);
        float a  = glow * 0.36 * (1.0 - smoothstep (0.0, 0.78, r));
        float a2 = glow * 0.13 * (1.0 - smoothstep (0.0, 0.38, r));
        a = max (a, a2);
        c.rgb = mix (c.rgb, glowColor.rgb, clamp (a, 0.0, 1.0));
    }

    // backdrop-filter REPLACES the backdrop with the blurred copy at full
    // alpha; the .018 white veil is then painted over it.  So the glass is
    // fully opaque inside the shape — the reason a first attempt looked wrong
    // is that the *capture* was showing through the plot's own already-drawn
    // content.  Here the blurred copy is the whole of it.
    // Qt's ShaderEffect takes PREMULTIPLIED alpha.  With a straight alpha the
    // semi-transparent rim of the mask came out far too bright and the glass
    // read as an opaque white band with a hole in the middle.
    fragColor = vec4 (c.rgb * inside, inside) * qt_Opacity;
}
