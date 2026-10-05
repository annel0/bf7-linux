// BF7: нагрузочный шейдер для сравнения GL и Vulkan в RetroArch — 64 выборки
// текстуры на пиксель (размытие 8x8). Тот же расчёт, что в blur64.slang.
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
    for (int y = 0; y < 8; y++)
        for (int x = 0; x < 8; x++)
            c += texture2D(Texture, vTex + (vec2(float(x), float(y)) - 3.5) * px).rgb;
    gl_FragColor = vec4(c / 64.0, 1.0);
}
#endif
