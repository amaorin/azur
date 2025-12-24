#define STRICT 1
#define UNICODE 1
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <GL/gl.h>
#include "vendor/opengl/wglext.h"
#include "vendor/opengl/glcorearb.h"
#undef STRICT
#undef UNICODE
#undef NOMINMAX
#undef WIN32_LEAN_AND_MEAN

#include "common.h"

typedef struct Game_Code
{
	HMODULE module;
	Game_Tick_Func* tick_func;
	FILETIME timestamp;
} Game_Code;

struct
{
	HINSTANCE instance;
	HWND window;
	HDC dc;
	HGLRC gl_context;
	GLuint backbuffer;
	GLuint vao;
	GLuint pipeline;
	GLuint vert_shader;
	GLuint frag_shader;
	Bump platform_bump;
	Bump frame_bump;
	Game_Code game_code;
	bool running;
} Globals = {0};

static bool
Bump_Create(u32 capacity, Bump* bump)
{
	u8* memory = VirtualAlloc(0, capacity, MEM_RESERVE | MEM_COMMIT, PAGE_READWRITE);

	*bump = (Bump){
		.memory         = memory,
		.cursor         = 0,
		.capacity       = capacity,
		.high_watermark = 0,
	};

	return (memory != 0);
}

static void
Bump_Destroy(Bump* bump)
{
	VirtualFree(bump->memory, 0, MEM_RELEASE);
	*bump = (Bump){0};
}

static void
Bump_Clear(Bump* bump)
{
	bump->cursor = 0;
}

// NOTE: Based on the OpenGL context tutorial by Mārtiņš Možeiko
//https://gist.github.com/mmozeiko/ed2ad27f75edf9c26053ce332a1f6647
static bool
LoadWGLFunctions(PFNWGLCHOOSEPIXELFORMATARBPROC* wglChoosePixelFormatARB, PFNWGLCREATECONTEXTATTRIBSARBPROC* wglCreateContextAttribsARB, PFNWGLSWAPINTERVALEXTPROC* wglSwapIntervalEXT)
{
	bool succeeded = false;

	HWND dummy_window = 0;
	HDC dc            = 0;
	HGLRC context     = 0;

	dummy_window = CreateWindowExW(0, L"STATIC", L"AZUR_OPENGL_DUMMY_WINDOW", WS_OVERLAPPED & ~WS_VISIBLE, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, 0, 0);

	if (dummy_window == 0)
	{
		//// ERROR
		goto teardown;
	}

	dc = GetDC(dummy_window);

	if (dc == 0)
	{
		//// ERROR
		goto teardown;
	}

	PIXELFORMATDESCRIPTOR pixel_format_descriptor = {
		.nSize           = sizeof(pixel_format_descriptor),
		.nVersion        = 1,
		.dwFlags         = PFD_DRAW_TO_WINDOW | PFD_SUPPORT_OPENGL | PFD_DOUBLEBUFFER,
		.iPixelType      = PFD_TYPE_RGBA,
		.cColorBits      = 24,
	};

	int format = ChoosePixelFormat(dc, &pixel_format_descriptor);
	if (format == 0 || !DescribePixelFormat(dc, format, sizeof(pixel_format_descriptor), &pixel_format_descriptor))
	{
		//// ERROR
		goto teardown;
	}

	if (!SetPixelFormat(dc, format, &pixel_format_descriptor))
	{
		//// ERROR
		goto teardown;
	}

	context = wglCreateContext(dc);
	if (context == 0 || !wglMakeCurrent(dc, context))
	{
		//// ERROR
		goto teardown;
	}

	PFNWGLGETEXTENSIONSSTRINGARBPROC wglGetExtensionsStringARB = (PFNWGLGETEXTENSIONSSTRINGARBPROC)wglGetProcAddress("wglGetExtensionsStringARB");
	if (!wglGetExtensionsStringARB)
	{
		//// ERROR
		goto teardown;
	}

	char* extension_string = (char*)wglGetExtensionsStringARB(dc);
	if (extension_string == 0)
	{
		//// ERROR
		goto teardown;
	}

	*wglChoosePixelFormatARB    = 0;
	*wglCreateContextAttribsARB = 0;
	*wglSwapIntervalEXT         = 0;
	for (char* scan = extension_string; *scan != 0; ++scan)
	{
		while (*scan == ' ') ++scan;
		char* start = scan;

		while (*scan != 0 && *scan != ' ') ++scan;

		String extension_name = (String){ .data = (u8*)start, .len = (u32)(scan - start) };

		if (String_Equal(extension_name, STRING("WGL_ARB_pixel_format")))
		{
			*wglChoosePixelFormatARB = (PFNWGLCHOOSEPIXELFORMATARBPROC)wglGetProcAddress("wglChoosePixelFormatARB");
		}
		else if (String_Equal(extension_name, STRING("WGL_ARB_create_context")))
		{
			*wglCreateContextAttribsARB = (PFNWGLCREATECONTEXTATTRIBSARBPROC)wglGetProcAddress("wglCreateContextAttribsARB");
		}
		else if (String_Equal(extension_name, STRING("WGL_EXT_swap_control")))
		{
			*wglSwapIntervalEXT = (PFNWGLSWAPINTERVALEXTPROC)wglGetProcAddress("wglSwapIntervalEXT");
		}
	}

	if (!*wglChoosePixelFormatARB || !*wglCreateContextAttribsARB || !*wglSwapIntervalEXT)
	{
		//// ERROR
		goto teardown;
	}

	succeeded = true;

teardown:;
	if (context != 0)
	{
		wglMakeCurrent(0, 0);
		wglDeleteContext(context);
	}

	if (dummy_window != 0)
	{
		if (dc != 0) ReleaseDC(dummy_window, dc);
		DestroyWindow(dummy_window);
	}

	return succeeded;
}

#ifdef AZUR_DEBUG
static void APIENTRY GLDebugMessageCallback(GLenum source, GLenum type, GLuint id, GLenum severity, GLsizei length, const GLchar* message, const void* user)
{
	OutputDebugStringA(message);
	OutputDebugStringA("\n");
}
#endif

#define GL_FUNCTIONS(X)                                     \
	X(PFNGLDEBUGMESSAGECALLBACKPROC, glDebugMessageCallback ) \
	X(PFNGLCREATETEXTURESPROC,       glCreateTextures       ) \
	X(PFNGLTEXTUREPARAMETERIPROC,    glTextureParameteri    ) \
	X(PFNGLTEXTURESTORAGE2DPROC,     glTextureStorage2D     ) \
	X(PFNGLTEXTURESUBIMAGE2DPROC,    glTextureSubImage2D    ) \
	X(PFNGLCREATESHADERPROGRAMVPROC, glCreateShaderProgramv ) \
	X(PFNGLGETPROGRAMIVPROC,         glGetProgramiv         ) \
	X(PFNGLGETPROGRAMINFOLOGPROC,    glGetProgramInfoLog    ) \
	X(PFNGLGENPROGRAMPIPELINESPROC,  glGenProgramPipelines  ) \
	X(PFNGLUSEPROGRAMSTAGESPROC,     glUseProgramStages     ) \
	X(PFNGLBINDPROGRAMPIPELINEPROC,  glBindProgramPipeline  ) \
	X(PFNGLBINDTEXTUREUNITPROC,      glBindTextureUnit      ) \
	X(PFNGLUNIFORM4FVPROC,           glUniform4fv           ) \
	X(PFNGLCREATEVERTEXARRAYSPROC,   glCreateVertexArrays   ) \
	X(PFNGLBINDVERTEXARRAYPROC,      glBindVertexArray      ) \
	X(PFNGLUSEPROGRAMPROC,           glUseProgram           ) \
	X(PFNGLACTIVESHADERPROGRAMPROC,  glActiveShaderProgram  ) \


#define X(TYPE, NAME) static TYPE NAME;
GL_FUNCTIONS(X)
#undef X

PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT = 0;

static bool
InitializeOpenGL()
{
	PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB       = 0;
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = 0;
	if (!LoadWGLFunctions(&wglChoosePixelFormatARB, &wglCreateContextAttribsARB, &wglSwapIntervalEXT))
	{
		//// ERROR
		return false;
	}

	int pixel_format_attribs[] = {
		WGL_DRAW_TO_WINDOW_ARB, GL_TRUE,
		WGL_SUPPORT_OPENGL_ARB, GL_TRUE,
		WGL_DOUBLE_BUFFER_ARB,  GL_TRUE,
		WGL_PIXEL_TYPE_ARB,     WGL_TYPE_RGBA_ARB,
		WGL_COLOR_BITS_ARB,     24,
		WGL_DEPTH_BITS_ARB,     24,
		WGL_STENCIL_BITS_ARB,    8,
		0,
	};

	int pixel_format;
	UINT pixel_formats;
	if (!wglChoosePixelFormatARB(Globals.dc, pixel_format_attribs, 0, 1, &pixel_format, &pixel_formats) || pixel_formats == 0)
	{
		//// ERROR
		return false;
	}

	PIXELFORMATDESCRIPTOR pixel_format_desc = {
		.nSize = sizeof(pixel_format_desc)
	};

	if (!DescribePixelFormat(Globals.dc, pixel_format, sizeof(pixel_format_desc), &pixel_format_desc))
	{
		//// ERROR
		return false;
	}

	if (!SetPixelFormat(Globals.dc, pixel_format, &pixel_format_desc))
	{
		//// ERROR
		return false;
	}

	int context_attribs[] = {
		WGL_CONTEXT_MAJOR_VERSION_ARB, 4,
		WGL_CONTEXT_MINOR_VERSION_ARB, 5,
		WGL_CONTEXT_PROFILE_MASK_ARB, WGL_CONTEXT_CORE_PROFILE_BIT_ARB,

#ifdef AZUR_DEBUG
		WGL_CONTEXT_FLAGS_ARB, WGL_CONTEXT_DEBUG_BIT_ARB,
#endif

		0,
	};

	Globals.gl_context = wglCreateContextAttribsARB(Globals.dc, 0, context_attribs);

	if (Globals.gl_context == 0 || !wglMakeCurrent(Globals.dc, Globals.gl_context))
	{
		//// ERROR
		return false;
	}

#define X(TYPE, NAME) NAME = (TYPE)wglGetProcAddress(#NAME); if (NAME == 0) return false;
	GL_FUNCTIONS(X)
#undef X

#ifdef AZUR_DEBUG
	glDebugMessageCallback(GLDebugMessageCallback, 0);
	glEnable(GL_DEBUG_OUTPUT_SYNCHRONOUS);
#endif

	return true;
}

#define AZUR_GAME_DLL L"azur_game.dll"
#define AZUR_GAME_LOADED_DLL L"azur_game_loaded.dll"

static bool
LoadGameCode(Game_Code* game_code)
{
	bool succeeded = false;

	if (game_code->module != 0) FreeLibrary(game_code->module);
	*game_code = (Game_Code){0};

	if (CopyFileW(AZUR_GAME_DLL, AZUR_GAME_LOADED_DLL, FALSE))
	{
		HMODULE module = LoadLibraryW(AZUR_GAME_LOADED_DLL);

		if (module != 0)
		{
			Game_Tick_Func* tick_func = (Game_Tick_Func*)GetProcAddress(module, "Tick");

			if (tick_func != 0)
			{
				*game_code = (Game_Code){
					.module    = module,
					.tick_func = tick_func,
					.timestamp = {0},
				};

				WIN32_FILE_ATTRIBUTE_DATA attrs;
				if (GetFileAttributesExW(AZUR_GAME_DLL, GetFileExInfoStandard, &attrs)) game_code->timestamp = attrs.ftLastWriteTime;

				succeeded = true;
			}
		}

		if (!succeeded && module != 0) FreeLibrary(module);
	}

	return succeeded;
}

static LRESULT
WndProc(HWND window, UINT msg_code, WPARAM wparam, LPARAM lparam)
{
	if (msg_code == WM_CLOSE)
	{
		Globals.running = false;
		return 0;
	}

	return DefWindowProcW(window, msg_code, wparam, lparam);
}

static void
FatalError(const char* message)
{
	MessageBoxA(Globals.window, message, "Azur Fatal Error", MB_OK | MB_ICONERROR);
}

static void
Setup_Error(const char* message)
{
	MessageBoxA(Globals.window, message, "Azur Setup Failed", MB_OK | MB_ICONERROR);
}

static bool
Setup(HINSTANCE instance)
{
	Globals.instance = instance;

	if (!Bump_Create(1 << 20, &Globals.platform_bump) || !Bump_Create(1 << 20, &Globals.frame_bump))
	{
		//// ERROR
		return false;
	}

	{ /// Set working directory
		Bump_Mark mark = Bump_GetMark(&Globals.platform_bump);

		u32 path_cap = 1 << 15;
		wchar_t* path = Bump_Push(&Globals.platform_bump, (path_cap+1) * sizeof(wchar_t), 1);
		
		u32 path_len = GetModuleFileNameW(0, path, path_cap);

		if (path_len == path_cap)
		{
			//// ERROR
			Setup_Error("Failed to get path of executable");
			return false;
		}

		for (wchar_t* scan = path + path_len-1; scan > path; --scan)
		{
			if (*scan == L'/' || *scan == L'\\')
			{
				*(scan + 1) = 0;
				break;
			}
		}

		if (!SetCurrentDirectoryW(path))
		{
			//// ERROR
			Setup_Error("Failed to set working directory");
			return false;
		}

		Bump_PopToMark(&Globals.platform_bump, mark);
	}

	{ /// Create window
		WNDCLASSEXW window_class = {
			.cbSize        = sizeof(window_class),
			.style         = 0,
			.lpfnWndProc   = WndProc,
			.hInstance     = Globals.instance,
			.hIcon         = LoadIcon(0, IDI_QUESTION),
			.hCursor       = LoadCursor(0, IDC_ARROW),
			.lpszClassName = L"AZUR_GAME",
		};

		Globals.window = 0;
		if (RegisterClassExW(&window_class))
		{
			Globals.window = CreateWindowExW(WS_EX_APPWINDOW, window_class.lpszClassName, L"azur", WS_OVERLAPPEDWINDOW,
																			 CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, CW_USEDEFAULT, 0, 0, Globals.instance, 0);
		}

		if (Globals.window == 0)
		{
			//// ERROR
			Setup_Error("Failed to create window");
			return false;
		}

		Globals.dc = GetDC(Globals.window);

		if (Globals.dc == 0)
		{
			//// ERROR
			Setup_Error("Failed to get window device context");
			return false;
		}
	}

	{
		if (!InitializeOpenGL())
		{
			//// ERROR
			Setup_Error("Failed to setup opengl context");
			return false;
		}

		glEnable(GL_BLEND);
		glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

		glDisable(GL_DEPTH_TEST);
		glDisable(GL_CULL_FACE);

		wglSwapIntervalEXT(1);

		glCreateTextures(GL_TEXTURE_2D, 1, &Globals.backbuffer);
		glTextureParameteri(Globals.backbuffer, GL_TEXTURE_MIN_FILTER, GL_NEAREST);
		glTextureParameteri(Globals.backbuffer, GL_TEXTURE_MAG_FILTER, GL_NEAREST);
		glTextureParameteri(Globals.backbuffer, GL_TEXTURE_WRAP_S, GL_REPEAT);
		glTextureParameteri(Globals.backbuffer, GL_TEXTURE_WRAP_T, GL_REPEAT);

		glTextureStorage2D(Globals.backbuffer, 1, GL_R8UI, AZUR_WIDTH, AZUR_HEIGHT);

		glCreateVertexArrays(1, &Globals.vao);

		const char* vert_shader_code =
			"#version 450 core\n"
			"out gl_PerVertex { vec4 gl_Position; };\n\n"
			"out vec2 uv;\n"
			"void main() {\n"
			"	vec2 pos = vec2(gl_VertexID >> 1, (gl_VertexID == 0 ? 1 : 0));\n"
			"	uv = pos;\n"
			"	gl_Position = vec4(vec2(4, -4)*pos + vec2(-1, 1), 0, 1);\n"
			"}\n"
		;

		const char* frag_shader_code =
			"#version 450 core\n"
			"in vec2 uv;\n"
			"layout (binding=0) uniform usampler2D backbuffer;\n"
			"layout (location=0) uniform vec4 color_lut[8];\n"
			"layout (location=0) out vec4 color;\n"
			"void main() {\n"
			" uint index = texture(backbuffer, uv).r;\n"
			"	color = color_lut[index & 0x7];\n"
			"}\n"
		;

		Globals.vert_shader = glCreateShaderProgramv(GL_VERTEX_SHADER,   1, &vert_shader_code);
		Globals.frag_shader = glCreateShaderProgramv(GL_FRAGMENT_SHADER, 1, &frag_shader_code);

		GLint linked;
		glGetProgramiv(Globals.vert_shader, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			char buffer[1024];
			glGetProgramInfoLog(Globals.vert_shader, sizeof(buffer), 0, buffer);
			OutputDebugStringA(buffer);

			//// ERROR
			Setup_Error("Failed to compile main vertex shader");
			return false;
		}

		glGetProgramiv(Globals.frag_shader, GL_LINK_STATUS, &linked);
		if (!linked)
		{
			char buffer[1024];
			glGetProgramInfoLog(Globals.frag_shader, sizeof(buffer), 0, buffer);
			OutputDebugStringA(buffer);

			//// ERROR
			Setup_Error("Failed to compile main fragment shader");
			return false;
		}

		glGenProgramPipelines(1, &Globals.pipeline);
		glUseProgramStages(Globals.pipeline, GL_VERTEX_SHADER_BIT, Globals.vert_shader);
		glUseProgramStages(Globals.pipeline, GL_FRAGMENT_SHADER_BIT, Globals.frag_shader);
	}

	if (!LoadGameCode(&Globals.game_code))
	{
		//// ERROR
		Setup_Error("Failed to load game code");
		return false;
	}

	return true;
}

int WINAPI
wWinMain(HINSTANCE instance, HINSTANCE prev_instance, PWSTR cmdline, int cmdshow)
{
	bool setup_successful = Setup(instance);
	if (!setup_successful)
	{
		//// ERROR
		return 1;
	}

	ShowWindow(Globals.window, SW_SHOW);

	Globals.running = true;
	while (Globals.running)
	{
		for (MSG msg; PeekMessageW(&msg, 0, 0, 0, PM_REMOVE); )
		{
			TranslateMessage(&msg);
			DispatchMessageW(&msg);
		}

		RECT client_rect;
		GetClientRect(Globals.window, &client_rect);
		u32 client_width  = client_rect.right  - client_rect.left;
		u32 client_height = client_rect.bottom - client_rect.top;

		if (client_width == 0 || client_height == 0)
		{
			// NOTE: window is minimized
			// TODO: pause game?
			Sleep(16);
		}
		else
		{
			umm mw = client_width/16;
			umm mh = client_height/9;
			umm m = (mh < mw ? mh : mw);

			glViewport((GLint)(client_width - m*16)/2, (GLint)(client_height - m*9)/2, (GLsizei)m*16, (GLsizei)m*9);

			glClearColor(1, 0, 1, 1);
			glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT | GL_STENCIL_BUFFER_BIT);

			u8 b[AZUR_WIDTH*AZUR_HEIGHT] = {0};
			for (umm i = 0; i < sizeof(b); ++i) b[i] = (u8)i;
			glTextureSubImage2D(Globals.backbuffer, 0, 0, 0, AZUR_WIDTH, AZUR_HEIGHT, GL_RED_INTEGER, GL_UNSIGNED_BYTE, b);

			glUseProgram(0);
			glBindProgramPipeline(Globals.pipeline);
			glActiveShaderProgram(Globals.pipeline, Globals.frag_shader);
			glBindTextureUnit(0, Globals.backbuffer);
			glBindVertexArray(Globals.vao);


			#define HEX_TO_RGBA_TRIPLET(HEX) { (((HEX) >> 16) & 0xFF)/255.0f, (((HEX) >> 8) & 0xFF)/255.0f, (((HEX) >> 0) & 0xFF)/255.0f, 1 }
			f32 lut[8][4] = {
				HEX_TO_RGBA_TRIPLET(0x000000),
				HEX_TO_RGBA_TRIPLET(0x555555),
				HEX_TO_RGBA_TRIPLET(0x7C34F4),
				HEX_TO_RGBA_TRIPLET(0x54DFBB),
				HEX_TO_RGBA_TRIPLET(0xFFFFFF),
				HEX_TO_RGBA_TRIPLET(0xFFCDE2),
				HEX_TO_RGBA_TRIPLET(0xFE7FB8),
				HEX_TO_RGBA_TRIPLET(0xFFF48D),
			};

			glUniform4fv(0, 8, &lut[0][0]);
			glDrawArrays(GL_TRIANGLES, 0, 3);

			if (!SwapBuffers(Globals.dc))
			{
				// TODO: What to do when swapping fails?
				FatalError("Failed to swap OpenGL buffers");
			}
		}
	}

	return 0;
}
