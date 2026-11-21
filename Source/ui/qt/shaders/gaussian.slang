// One axis of the full separable Gaussian. Preserve the original texel-spaced
// integration and three-sigma radius; both glass and fog use this kernel.
float4 dualGaussian(Sampler2D source, float2 uv, float2 direction, float sigma)
{
    float s = max(sigma, 0.001);
    int radius = int(ceil(3.0 * s));
    float4 sum = source.Sample(uv);
    float weightSum = 1.0;
    for (int i = 1; i <= 96; ++i)
    {
        if (i > radius) break;
        float weight = exp(-float(i * i) / (2.0 * s * s));
        sum += (source.Sample(uv + direction * float(i))
              + source.Sample(uv - direction * float(i))) * weight;
        weightSum += 2.0 * weight;
    }
    return sum / weightSum;
}
