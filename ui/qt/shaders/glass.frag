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
layout(binding = 1) uniform sampler2D src;

void main()
{
    // The paint order below is the page's, and it matters:
    //
    //   .controlpit::before   the accent glow   <- BEHIND
    //   .controlpit::after    backdrop-filter + rgba(255,255,255,.018)
    //
    // ::before is a sibling painted first, so the glow is part of the BACKDROP
    // that ::after filters: it gets blurred, then saturated towards grey, then
    // veiled by the 1.8 % white.  Mixing the accent in at the very end instead
    // (which is what this did) left the glow un-veiled and fully saturated, and
    // the pod read as a coloured wash sitting over the plot rather than a
    // frosted version of it.  That is the "glass has colour" complaint.
    vec4 c = texture (src, vTex);

    // ---- accent glow, added to the backdrop before filtering -------------
    if (glow > 0.0005)
    {
        // radial-gradient(ellipse 70% 160% at 50% 0%/100%, accent .36 0%,
        //                 accent .13 38%, accent 0 78%)
        // The stops are LINEAR between colour stops: 0 -> .36, 38 % -> .13,
        // 78 % -> 0.  A smoothstep ramp is not that curve.
        vec2  centre = vec2 (0.5, trapTop > 0.5 ? 0.0 : 1.0);
        vec2  d2 = (vTex - centre) / vec2 (0.70, 1.60);
        float r  = length (d2);
        float a;
        if (r <= 0.38)      a = mix (0.36, 0.13, r / 0.38);
        else if (r <= 0.78) a = mix (0.13, 0.00, (r - 0.38) / 0.40);
        else                a = 0.0;
        // ---- strength, measured against the page ------------------------
        // The gradient above is the page's curve exactly, and comparing the
        // falloff row by row shows the SHAPE already matches (centre 35 vs the
        // page's 8, a quarter out 21 vs 4, edge -3 vs -3 — the same profile).
        // What did not match was the overall level: 4.4x too strong, which is
        // what "it is too full, not like a glow" describes — the right
        // silhouette at the wrong brightness reads as a lit panel.
        //
        //      row   ours  page
        //        2     35     8
        //        6     31     6
        //       10     24     5
        //       14     16     2
        //
        // 8/35 = 0.229.  Earlier values for this factor: 0.20 (taken while the
        // glass sampled the wrong rectangle), 1.0 (mine — the plain CSS number,
        // which is what made it "too full").  This one is a like-for-like
        // screenshot comparison of the same audio.
        const float kGlowToScreen = 0.229;
        a *= glow * kGlowToScreen;       // the layer opacity: var(--light)
        c.rgb = mix (c.rgb, vec3 (glowR, glowG, glowB), clamp (a, 0.0, 1.0));
    }

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

    // Qt's ShaderEffect takes PREMULTIPLIED alpha: with a straight alpha the
    // semi-transparent rim of the mask came out far too bright and the glass
    // read as an opaque white band with a hole in the middle.
    fragColor = vec4 (c.rgb * inside, inside) * qt_Opacity;
}
