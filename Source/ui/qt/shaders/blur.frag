#version 440
// One axis of the dual Gaussian.  The kernel itself is ovDualGaussAxis() in
// blurfunc.glsl, spliced in above by CMake — the glass and the EQ fog call the
// same function, so there is one blur in this project, not two.
//
// This pass also carries what the fog needs and the glass does not use:
//
//   sigmaSlope   added to sigma over the height of the surface, so one pass can
//                have a blur that grows with distance.  The fog needs exactly
//                that; a CSS canvas can only fake it with twelve separate
//                strokes, which is why the design has twelve bands and why
//                those bands showed their hand-over as slabs.
//   alphaMul     multiplies in the fog's fade, (1 - t) ^ 1.15.  Zero for glass.
//
// Both default to 0, and with sigmaSlope = 0 the result is bit-for-bit the
// plain dual Gaussian the glass has always used.
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
    float axis;        // 0 = horizontal pass, 1 = vertical
    float sigmaSlope;  // extra sigma over the surface's height, for the fog
    float alphaMul;    // 1 on a pass that applies the fog's fade, 0 otherwise
    // The fog's gradient runs outward from the seam, where the two plots meet:
    // the upper plot's curve escapes down from its bottom edge and the lower
    // one's escapes up from its top edge, and the two edges are a few tens of
    // pixels apart.  So the distance that matters is measured from the SEAM
    // centre, minus the half-gap between the two edges.  One value for the
    // whole surface — which is what lets both halves share one texture.
    float fogMid;      // seam centre, as a fraction of the surface's height
    float fogGap;      // half the gap between the two plot edges, same units
    float fogRange;    // the fog's reach, same units
};
layout(binding = 1) uniform sampler2D src;

void main()
{
    ivec2 ts    = textureSize (src, 0);
    vec2  texel = 1.0 / vec2 (ts);
    vec2  dir   = axis > 0.5 ? vec2 (0.0, texel.y) : vec2 (texel.x, 0.0);

    // t is 0 at the plot edges and 1 at the fog's far end, in either direction:
    // the distance OUTWARD from the nearest edge, normalised by the reach.
    //
    //   |y - fogMid|  distance from the seam centre
    //   fogGap        how far that centre is from the edges (the padding)
    //   fogRange      how far the fog reaches past them
    //
    // So the distance past the edge is (|y - fogMid| - fogGap), and it is this
    // way round — subtracting fogGap from the distance, not the reverse.  The
    // reverse clamps to 0 for every pixel outside the gap, which is every
    // pixel the fog is drawn on: sigma then stayed at its minimum and the alpha
    // ramp at 1, so the fog came out sharp and unfaded.  That is exactly what
    // "it never blurs and never fades" looked like.
    //
    // Irrelevant when sigmaSlope and alphaMul are both 0, which is the glass's
    // case (it passes fogMid 0, fogGap 0, fogRange 1 and ignores the result).
    float t = clamp ((abs (vTex.y - fogMid) - fogGap) / max (fogRange, 1e-5),
                     0.0, 1.0);

    vec4 c = ovDualGaussAxis (src, vTex, dir, sigma + sigmaSlope * t);

    if (alphaMul > 0.5)
        c *= pow (max (0.0, 1.0 - t), 1.15);

    // The source is premultiplied and a Gaussian keeps it that way, which is
    // what ShaderEffect expects.
    fragColor = c * qt_Opacity;
}
