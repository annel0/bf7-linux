#version 450
// BF7: нагрузочный шейдер для сравнения GL и Vulkan в RetroArch — 64 выборки
// текстуры на пиксель (размытие 8x8). Тот же расчёт, что в blur64.glsl.
layout(push_constant) uniform Push {
    vec4 SourceSize;
    vec4 OriginalSize;
    vec4 OutputSize;
    uint FrameCount;
} params;
layout(std140, set = 0, binding = 0) uniform UBO { mat4 MVP; } global;

#pragma stage vertex
layout(location = 0) in vec4 Position;
layout(location = 1) in vec2 TexCoord;
layout(location = 0) out vec2 vTexCoord;
void main() { gl_Position = global.MVP * Position; vTexCoord = TexCoord; }

#pragma stage fragment
layout(location = 0) in vec2 vTexCoord;
layout(location = 0) out vec4 FragColor;
layout(set = 0, binding = 2) uniform sampler2D Source;
void main() {
    vec3 c = vec3(0.0);
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            c += texture(Source, vTexCoord + (vec2(float(x), float(y)) - 3.5) * params.SourceSize.zw).rgb;
    FragColor = vec4(c / 64.0, 1.0);
}
