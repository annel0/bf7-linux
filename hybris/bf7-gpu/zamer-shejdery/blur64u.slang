#version 450
// BF7: нагрузочный шейдер для сравнения GL и Vulkan в RetroArch — 64 выборки
// текстуры на пиксель (размытие 8x8). Цикл раскрыт вручную (как в blur64u.glsl).
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
    vec2 vT = vTexCoord;
    vec2 px = params.SourceSize.zw;
    c += texture(Source, vT + vec2(-3.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, -3.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, -2.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, -1.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, -0.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, 0.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, 1.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, 2.5) * px).rgb;
    c += texture(Source, vT + vec2(-3.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(-2.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(-1.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(-0.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(0.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(1.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(2.5, 3.5) * px).rgb;
    c += texture(Source, vT + vec2(3.5, 3.5) * px).rgb;
    FragColor = vec4(c / 64.0, 1.0);
}
