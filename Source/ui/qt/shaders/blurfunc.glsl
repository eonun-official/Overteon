// One axis of a separable Gaussian — the "dual Gaussian", the same kernel the
// central glass uses.
//
// Spliced into the fragment shaders by CMake (qsb has no #include), so there is
// exactly ONE definition of the blur in the project.  The fog and the glass
// cannot drift apart, and fixing a kernel artefact once fixes it everywhere.
//
// The radius follows sigma (out to 3 sigma, where the tail is below 0.3 %)
// instead of using a fixed tap set: a fixed set rounds its taps to whole
// pixels, so for a given sigma they land on positions with gaps between them.
// Over a big glass surface that is invisible; on a thin stroke it is not — the
// profile broke off a few pixels from the stroke instead of fading smoothly.
//
// A fixed-count kernel (8 samples at +-i*sigma/2) was tried as a speed-up and
// reverted: it is cheaper, but it is a coarser integration of the same
// function, and because this file is shared it changed the CENTRAL glass too —
// measured against the web page, the whole window drifted by 2.1 units of mean
// per-pixel error.  Speed came from not re-blurring every frame instead (see
// OvEqFogGpu), which costs the glass nothing.
//
// sigma is in TEXELS of `tex`, and `dir` is one texel of the axis being blurred.
vec4 ovDualGaussAxis (sampler2D tex, vec2 uv, vec2 dir, float sigma)
{
    float s = max (sigma, 0.001);
    int   r = int (ceil (3.0 * s));

    vec4  sum  = texture (tex, uv);
    float wsum = 1.0;
    for (int i = 1; i <= 96; ++i)
    {
        if (i > r) break;
        float w = exp (-float (i * i) / (2.0 * s * s));
        sum  += (texture (tex, uv + dir * float (i)) +
                 texture (tex, uv - dir * float (i))) * w;
        wsum += 2.0 * w;
    }
    return sum / wsum;
}
