// BF7: нагрузочный шейдер для сравнения GL и Vulkan в RetroArch — 64 выборки
// текстуры на пиксель (размытие 8x8). Цикл раскрыт вручную (как в blur64u.slang).
#if defined(VERTEX)
attribute vec4 VertexCoord;
attribute vec4 TexCoord;
varying vec2 vTex;
uniform mat4 MVPMatrix;
void main() { gl_Position = MVPMatrix * VertexCoord; vTex = TexCoord.xy; }
#elif defined(FRAGMENT)
precision highp float;
uniform sampler2D Texture;
uniform vec2 TextureSize;
varying vec2 vTex;
void main() {
    vec3 c = vec3(0.0);
    vec2 px = 1.0 / TextureSize;
    vec2 vT = vTex;
    c += texture2D(Texture, vT + vec2(-3.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, -3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, -2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, -1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, -0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, 0.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, 1.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, 2.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-3.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-2.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-1.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(-0.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(0.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(1.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(2.5, 3.5) * px).rgb;
    c += texture2D(Texture, vT + vec2(3.5, 3.5) * px).rgb;
    gl_FragColor = vec4(c / 64.0, 1.0);
}
#endif
