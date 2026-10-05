/*
 * Настоящая оконная поверхность EGL на панели телефона.
 * Требует платформу mtkfb: EGL_PLATFORM=mtkfb.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <EGL/egl.h>
#include <GLES2/gl2.h>

static const char *vs_src =
"attribute vec2 p;\n"
"varying vec2 uv;\n"
"uniform float ang;\n"
"uniform float aspect;\n"
"void main(){\n"
"  float c = cos(ang), s = sin(ang);\n"
"  vec2 q = mat2(c, -s, s, c) * p;\n"
"  q.y *= aspect;\n"
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
		fprintf(stderr, "шейдер: %.1000s\n", log);
		exit(1);
	}
	return s;
}

int main(int argc, char **argv)
{
	int frames = argc > 1 ? atoi(argv[1]) : 300;
	EGLDisplay dpy;
	EGLConfig cfg;
	EGLSurface surf;
	EGLContext ctx;
	EGLint maj, min, n, w = 0, h = 0;
	GLuint prog;
	GLint ok = 0, uAng, uT, uAspect;
	double t0, el, tdraw = 0, tswap = 0;
	static const GLfloat quad[] = { -0.6f, -0.6f, 0.6f, -0.6f, -0.6f, 0.6f, 0.6f, 0.6f };
	static const EGLint ca[] = {
		EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
		EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
		EGL_RED_SIZE, 8, EGL_GREEN_SIZE, 8, EGL_BLUE_SIZE, 8, EGL_ALPHA_SIZE, 8,
		EGL_NONE
	};
	static const EGLint xa[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };

	setvbuf(stdout, NULL, _IONBF, 0);
	dpy = eglGetDisplay(EGL_DEFAULT_DISPLAY);
	if (!eglInitialize(dpy, &maj, &min)) { printf("eglInitialize: 0x%x\n", eglGetError()); return 1; }
	eglBindAPI(EGL_OPENGL_ES_API);
	if (!eglChooseConfig(dpy, ca, &cfg, 1, &n) || n < 1) { printf("нет оконного конфига: 0x%x\n", eglGetError()); return 2; }

	surf = eglCreateWindowSurface(dpy, cfg, (EGLNativeWindowType)0, NULL);
	if (surf == EGL_NO_SURFACE) { printf("eglCreateWindowSurface: 0x%x\n", eglGetError()); return 3; }
	eglQuerySurface(dpy, surf, EGL_WIDTH, &w);
	eglQuerySurface(dpy, surf, EGL_HEIGHT, &h);
	printf("оконная поверхность %dx%d\n", w, h);

	ctx = eglCreateContext(dpy, cfg, EGL_NO_CONTEXT, xa);
	if (ctx == EGL_NO_CONTEXT) { printf("eglCreateContext: 0x%x\n", eglGetError()); return 4; }
	{
		EGLint vid = -1, sid = -1, cid = -1;
		eglGetConfigAttrib(dpy, cfg, EGL_NATIVE_VISUAL_ID, &vid);
		eglGetConfigAttrib(dpy, cfg, EGL_CONFIG_ID, &cid);
		eglQuerySurface(dpy, surf, EGL_CONFIG_ID, &sid);
		printf("config id=%d native_visual=%d, у поверхности config id=%d\n", cid, vid, sid);
	}
	if (!eglMakeCurrent(dpy, surf, surf, ctx)) { printf("makecurrent: 0x%x\n", eglGetError()); return 4; }
	printf("%s | %s\n", glGetString(GL_RENDERER), glGetString(GL_VERSION));

	prog = glCreateProgram();
	glAttachShader(prog, mkshader(GL_VERTEX_SHADER, vs_src));
	glAttachShader(prog, mkshader(GL_FRAGMENT_SHADER, fs_src));
	glBindAttribLocation(prog, 0, "p");
	glLinkProgram(prog);
	glGetProgramiv(prog, GL_LINK_STATUS, &ok);
	if (!ok) { printf("линковка не удалась\n"); return 5; }
	glUseProgram(prog);
	uAng = glGetUniformLocation(prog, "ang");
	uT = glGetUniformLocation(prog, "t");
	uAspect = glGetUniformLocation(prog, "aspect");

	if (getenv("WINSPIN_DESTROY_SWAP")) {
		EGLBoolean r = eglSurfaceAttrib(dpy, surf, EGL_SWAP_BEHAVIOR, EGL_BUFFER_DESTROYED);
		EGLint sb = 0;
		eglQuerySurface(dpy, surf, EGL_SWAP_BEHAVIOR, &sb);
		printf("swap behavior -> %d, задано=%d (DESTROYED=0x%x PRESERVED=0x%x)\n",
		       sb, (int)r, EGL_BUFFER_DESTROYED, EGL_BUFFER_PRESERVED);
	} else {
		EGLint sb = 0;
		eglQuerySurface(dpy, surf, EGL_SWAP_BEHAVIOR, &sb);
		printf("swap behavior = %d (DESTROYED=0x%x PRESERVED=0x%x)\n",
		       sb, EGL_BUFFER_DESTROYED, EGL_BUFFER_PRESERVED);
	}
	glVertexAttribPointer(0, 2, GL_FLOAT, GL_FALSE, 0, quad);
	glEnableVertexAttribArray(0);
	glViewport(0, 0, w, h);
	eglSwapInterval(dpy, 1);

	t0 = now();
	for (int i = 0; i < frames; i++) {
		double t = now() - t0;

		glClearColor(0.03f, 0.05f, 0.12f, 1.0f);
		glClear(GL_COLOR_BUFFER_BIT);
		glUniform1f(uAng, (float)t);
		glUniform1f(uT, (float)t * 2.0f);
		glUniform1f(uAspect, h ? (float)w / (float)h : 1.0f);
		glDrawArrays(GL_TRIANGLE_STRIP, 0, 4);
		{
			double a = now();
			glFinish();
			double b = now();
			eglSwapBuffers(dpy, surf);
			double c = now();
			tdraw += b - a;
			tswap += c - b;
		}
	}
	el = now() - t0;
	printf("%d кадров за %.2f с = %.1f fps (glFinish %.1f мс, eglSwapBuffers %.1f мс на кадр)\n",
	       frames, el, frames / el, tdraw * 1e3 / frames, tswap * 1e3 / frames);

	eglMakeCurrent(dpy, EGL_NO_SURFACE, EGL_NO_SURFACE, EGL_NO_CONTEXT);
	eglDestroySurface(dpy, surf);
	eglTerminate(dpy);
	return 0;
}
