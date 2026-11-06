#version 440
// The design's glass finish, in one pass:
//   .controlpit::after / #hdr::before  -> shape mask + rgba(255,255,255,.018)
//   .controlpit::before               -> accent glow, opacity = var(--light)
// plus saturate(.85), which the CSS applies together with the blur.
//
// Every uniform is a `float`: in std140 a vec2 is padded to 16 bytes and a vec4
// forces its own 16-byte alignment, so mixing them with scalars puts the later
// fields at offsets Qt does not write.  Floats pack at 4 bytes with no padding,
// so the layout Qt fills by name matches the layout this shader reads.
layout(location = 0) in vec2 vTex;
layout(location = 0) out vec4 fragColor;
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float veil;
    float glow;
    float isEllipse;
    float trapTop;
    float saturation;
    float glowR;
    float glowG;
    float glowB;
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
        // No explicit size => farthest-corner: the half-axes are half * sqrt(2),
        // so the box is scaled by 1/sqrt(2) before taking the length.
        vec2 p = (vTex * 2.0 - 1.0) * 0.70710678;
        inside = 1.0 - smoothstep (0.45, 0.78, length (p));
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
        // The gradient's stops are LINEAR between colour stops:
        //     0 % -> .36,  38 % -> .13,  78 % -> 0
        // A smoothstep ramp is not that curve — it held the glow much brighter
        // through the middle of the bar, which showed as a pink wash over the
        // controls where the CPU path had almost none.
        vec2  centre = vec2 (0.5, trapTop > 0.5 ? 0.0 : 1.0);
        vec2  d2 = (vTex - centre) / vec2 (0.70, 1.60);
        float r  = length (d2);
        float a;
        if (r <= 0.38)      a = mix (0.36, 0.13, r / 0.38);
        else if (r <= 0.78) a = mix (0.13, 0.00, (r - 0.38) / 0.40);
        else                a = 0.0;
        a *= glow;                       // the layer opacity: var(--light)
        c.rgb = mix (c.rgb, vec3 (glowR, glowG, glowB), clamp (a, 0.0, 1.0));
    }

    // Qt's ShaderEffect takes PREMULTIPLIED alpha: with a straight alpha the
    // semi-transparent rim of the mask came out far too bright and the glass
    // read as an opaque white band with a hole in the middle.
    fragColor = vec4 (c.rgb * inside, inside) * qt_Opacity;
}
