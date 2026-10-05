/* Рисуем на GPU в pbuffer и выкладываем в /dev/fb0 с переключением страниц. */
#define _GNU_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <linux/fb.h>
#include <EGL/egl.h>
#include <GLES3/gl3.h>

static const char *vs_src =
"attribute vec2 p;\n"
"varying vec2 uv;\n"
"uniform float ang;\n"
"void main(){\n"
"  float c = cos(ang), s = sin(ang);\n"
"  vec2 q = mat2(c, -s, s, c) * p;\n"
"  uv = p * 0.5 + 0.5;\n"
"  gl_Position = vec4(q, 0.0, 1.0);\n"
"}\n";

static const char *fs_src =
"precision mediump float;\n"
"varying vec2 uv;\n"
"uniform float t;\n"
"void main(){\n"
"  gl_FragColor = vec4(uv.x, uv.y, 0.5 + 0.5 * sin(t), 1.0);\n"
"}\n";

static double now(void)
{
	struct timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return ts.tv_sec + ts.tv_nsec * 1e-9;
}

static GLuint mkshader(GLenum type, const char *src)
{
	GLuint s = glCreateShader(type);
	GLint ok = 0;

	glShaderSource(s, 1, &src, NULL);
	glCompileShader(s);
	glGetShaderiv(s, GL_COMPILE_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei len = 0;
		memset(log, 0, sizeof log);
		glGetShaderInfoLog(s, (GLsizei)sizeof log - 1, &len, log);
		fprintf(stderr, "шейдер не собрался (len=%d): %.1000s\n", (int)len, log);
		exit(1);
	}
	return s;
}

int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 300;
	struct fb_var_screeninfo var;
	struct fb_fix_screeninfo fix;
	int fb = open("/dev/fb0", O_RDWR);
	unsigned char *mem;
	size_t maplen;
	int W, H, stride, pitch, page = 0;
	double t0, tread = 0, el;
	EGLDisplay dpy;
	EGLContext ctx;
	EGLSurface surf;
	EGLConfig cfg;
	EGLint maj, min, n;
	GLuint prog, vao, vbo;
	GLint ok = 0, uAng, uT;
	static const GLfloat quad[] = { -0.7f, -0.7f, 0.7f, -0.7f, -0.7f, 0.7f, 0.7f, 0.7f };
	static const EGLint ca[] = {
		EGL_SURFACE_TYPE, EGL_PBUFFER_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES3_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	static const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 3, EGL_NONE };
	EGLint sa[] = { EGL_WIDTH, 0, EGL_HEIGHT, 0, EGL_NONE };

	setvbuf(stdout, NULL, _IONBF, 0);
	if (fb < 0) { perror("/dev/fb0"); return 1; }
	ioctl(fb, FBIOGET_VSCREENINFO, &var);
	ioctl(fb, FBIOGET_FSCREENINFO, &fix);
	W = var.xres; H = var.yres; stride = fix.line_length; pitch = stride / 4;
	maplen = (size_t)stride * var.yres_virtual;
	mem = mmap(NULL, maplen, PROT_READ | PROT_WRITE, MAP_SHARED, fb, 0);
	if (mem == MAP_FAILED) { perror("mmap"); return 1; }
	printf("fb %dx%d stride=%d virtual_y=%d map=%zu КиБ\n",
	       W, H, stride, var.yres_virtual, maplen / 1024);

	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	eglInitialize(dpy, &maj, &min);
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1) { printf("нет конфига\n"); return 2; }
	sa[1] = W; sa[3] = H;
	surf = eglCreatePbufferSurface(dpy, cfg, sa);
	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("makecurrent: 0x%x\n", eglGetError()); return 3; }
	{
		const GLubyte *r = glGetString(GL_RENDERER);
		const GLubyte *v = glGetString(GL_VERSION);
		const GLubyte *g = glGetString(GL_SHADING_LANGUAGE_VERSION);
		printf("renderer=%p version=%p glsl=%p\n", (void *)r, (void *)v, (void *)g);
		printf("%s | %s\n", r ? (const char *)r : "?", v ? (const char *)v : "?");
	}

	printf("создаём шейдеры...\n");
	prog = glCreateProgram();
	printf("prog=%u\n", prog);
	{ GLuint sh = mkshader(GL_VERTEX_SHADER, vs_src); printf("vs=%u\n", sh); glAttachShader(prog, sh); }
	{ GLuint sh = mkshader(GL_FRAGMENT_SHADER, fs_src); printf("fs=%u\n", sh); glAttachShader(prog, sh); }
	glBindAttribLocation(prog, 0, "p");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) {
		char log[1024];
		GLsizei len = 0;
		memset(log, 0, sizeof log);
		glGetProgramInfoLog(prog, (GLsizei)sizeof log - 1, &len, log);
		printf("линковка программы не удалась (len=%d): %.1000s\n", (int)len, log);
		return 4;
	}
	glUseProgram(prog);
	printf("программа слинкована\n");
	uAng = glGetUniformLocation(prog, "ang");
	uT = glGetUniformLocation(prog, "t");

	glGenVertexArrays(1, &vao); glBindVertexArray(vao);
	printf("vao=%u err=0x%x\n", vao, glGetError());
	glGenBuffers(1, &vbo); glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof quad, quad, GL_STATIC_DRAW);
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, 0);
	glEnableVertexAttribArray(0);

	glViewport(0, 0, W, H);
	glPixelStorei(GL_PACK_ALIGNMENT, 4);
	glPixelStorei(GL_PACK_ROW_LENGTH, pitch);

	printf("входим в цикл, err=0x%x\n", glGetError());
	t0 = now();
	for (int i = 0; i < frames; i++) {
		double t = now() - t0, r0;
		unsigned char *dst = mem + (size_t)page * H * stride;

		glClearColor(0.05f, 0.05f, 0.10f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(uAng, (float)t);
		glUniform1f(uT, (float)t * 2.0f);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		r0 = now();
		glReadPixels(0, 0, W, H, GL_RGBA, GL_UNSIGNED_BYTE, dst);
		tread += now() - r0;
		var.yoffset = page * H;
		var.activate = FB_ACTIVATE_VBL;
		if (ioctl(fb, FBIOPAN_DISPLAY, &var) < 0 && i == 0)
			perror("FBIOPAN_DISPLAY");
		page ^= 1;
	}
	el = now() - t0;
	printf("%d кадров за %.2f с = %.1f fps (glReadPixels %.1f мс/кадр)\n",
	       frames, el, frames / el, tread * 1000.0 / frames);
	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglTerminate(dpy);
	return 0;
}
