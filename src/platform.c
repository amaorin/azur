#define STRICT 1
#define UNICODE 1
#define NOMINMAX 1
#define WIN32_LEAN_AND_MEAN 1
#include <windows.h>
#include <GL/gl.h>
#include "vendor/opengl/wglext.h"
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

static bool
InitializeOpenGL()
{
	PFNWGLCHOOSEPIXELFORMATARBPROC wglChoosePixelFormatARB       = 0;
	PFNWGLCREATECONTEXTATTRIBSARBPROC wglCreateContextAttribsARB = 0;
	PFNWGLSWAPINTERVALEXTPROC wglSwapIntervalEXT                 = 0;
	if (!LoadWGLFunctions(&wglChoosePixelFormatARB, &wglCreateContextAttribsARB, &wglSwapIntervalEXT))
	{
		//// ERROR
		return false;
	}

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

void
Setup_Error(const char* message)
{
	MessageBoxA(0, message, "Azur Setup Failed", MB_OK | MB_ICONERROR);
}

bool
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

	if (!InitializeOpenGL())
	{
		//// ERROR
		Setup_Error("Failed to setup opengl context");
		return false;
	}

	if (!LoadGameCode(&Globals.game_code))
	{
		//// ERROR
		Setup_Error("Failed to load game code");
		return false;
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

		Sleep(16);
	}

	return 0;
}
