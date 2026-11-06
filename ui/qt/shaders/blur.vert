#version 440
layout(location = 0) in vec4 qt_Vertex;
layout(location = 1) in vec2 qt_MultiTexCoord0;
layout(location = 0) out vec2 vTex;
layout(std140, binding = 0) uniform buf {
    mat4  qt_Matrix;
    float qt_Opacity;
    float sigma;          // 4 bytes, then 8 bytes of padding
    vec2  pad0;
    vec2  texelStep;      // 16-byte aligned
    vec2  pad1;
};
void main()
{
    vTex = qt_MultiTexCoord0;
    gl_Position = qt_Matrix * qt_Vertex;
}
