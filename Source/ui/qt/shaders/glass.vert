#version 440
// Paired vertex shader for glass.frag.  A ShaderEffect needs BOTH: with only a
// fragment shader the texcoord is undefined and every texture() returns zero,
// which looks like a transparent surface, not an error.
//
// The uniform block must match glass.frag exactly — Qt fills it by name, and a
// field that exists in one stage but not the other shifts everything after it.
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 vTex;
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
void main()
{
    vTex = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_Vertex;
}
