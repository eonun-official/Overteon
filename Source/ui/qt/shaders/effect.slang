// Qt Quick ShaderEffect interface shared by both stages. Explicit SPIR-V
// bindings and locations are the boundary between Slang and Qt's RHI.
struct EffectUniforms
{
    column_major float4x4 qt_Matrix;
    float qt_Opacity;
#ifdef OV_GLASS
    float veil;
    float glow;
    float isEllipse;
    float trapTop;
    float saturation;
    float glowR;
    float glowG;
    float glowB;
#else
    float sigma;
    float axis;
    float sigmaSlope;
    float alphaMul;
    float fogMid;
    float fogGap;
    float fogRange;
#endif
};

[[vk::binding(0, 0)]] ConstantBuffer<EffectUniforms> uniforms;

struct VertexInput
{
    [[vk::location(0)]] float4 position : POSITION;
    [[vk::location(1)]] float2 texCoord : TEXCOORD0;
};

struct VertexOutput
{
    float4 position : SV_Position;
    [[vk::location(0)]] float2 texCoord : TEXCOORD0;
};

[shader("vertex")]
VertexOutput vertexMain(VertexInput input)
{
    VertexOutput output;
    output.position = mul(uniforms.qt_Matrix, input.position);
    output.texCoord = input.texCoord;
    return output;
}
