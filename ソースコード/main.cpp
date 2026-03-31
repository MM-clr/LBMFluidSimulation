#include "main.h"
#include "manager.h"
#include <thread>
#include <string> 
#include "renderer.h"

#include "ImGuiManager.h"

const char* CLASS_NAME = "AppClass";
const char* WINDOW_NAME = "DX11ゲーム";

// FPS表示用のグローバル変数を定義
float g_Fps = 0.0f;

LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam);


HWND g_Window;

HWND GetWindow()
{
	return g_Window;
}


int APIENTRY WinMain(HINSTANCE hInstance, HINSTANCE hPrevInstance, LPSTR lpCmdLine, int nCmdShow)
{
	WNDCLASSEX wcex;
	{
		wcex.cbSize = sizeof(WNDCLASSEX);
		wcex.style = 0;
		wcex.lpfnWndProc = WndProc;
		wcex.cbClsExtra = 0;
		wcex.cbWndExtra = 0;
		wcex.hInstance = hInstance;
		wcex.hIcon = nullptr;
		wcex.hCursor = LoadCursor(nullptr, IDC_ARROW);
		wcex.hbrBackground = nullptr;
		wcex.lpszMenuName = nullptr;
		wcex.lpszClassName = CLASS_NAME;
		wcex.hIconSm = nullptr;

		RegisterClassEx(&wcex);

		RECT rc = { 0, 0, (LONG)SCREEN_WIDTH, (LONG)SCREEN_HEIGHT };
		AdjustWindowRect(&rc, WS_OVERLAPPEDWINDOW, FALSE);

		g_Window = CreateWindowEx(0, CLASS_NAME, WINDOW_NAME, WS_OVERLAPPEDWINDOW, CW_USEDEFAULT, CW_USEDEFAULT,
			rc.right - rc.left, rc.bottom - rc.top, nullptr, nullptr, hInstance, nullptr);
	}

	CoInitializeEx(nullptr, COINITBASE_MULTITHREADED);

	ShowWindow(g_Window, nCmdShow);
	UpdateWindow(g_Window);

	// Manager と Renderer の初期化（Renderer は Manager::Init 内で呼ばれます）
	Manager::Init();

	// ImGui の初期化は main 側で一元管理
	ImGuiManager::Init(g_Window);

	DWORD dwExecLastTime;
	DWORD dwCurrentTime;
	timeBeginPeriod(1);
	dwExecLastTime = timeGetTime();
	dwCurrentTime = 0;

	// FPS計算用の変数を追加
	DWORD dwFpsLastTime = dwExecLastTime;
	int frameCount = 0;

	MSG msg;
	bool quit = false;
	while (!quit)
	{
		// ウィンドウメッセージを全て処理
		while (PeekMessage(&msg, NULL, 0, 0, PM_REMOVE))
		{
			if (msg.message == WM_QUIT)
			{
				quit = true;
				break;
			}
			TranslateMessage(&msg);
			DispatchMessage(&msg);
		}

		if (quit) break;

		dwCurrentTime = timeGetTime();

		// 固定更新（60Hz）
		if ((dwCurrentTime - dwExecLastTime) >= (1000 / 60))
		{
			dwExecLastTime = dwCurrentTime;
			Manager::Update();
		}

		// 毎フレーム描画（ImGui の描画は Manager::Draw 内で適切な順序で行われる）
		Manager::Draw();

		// FPS 計測（レンダーフレーム基準）
		frameCount++;
		if ((dwCurrentTime - dwFpsLastTime) >= 1000)
		{
			g_Fps = static_cast<float>(frameCount);
			frameCount = 0;
			dwFpsLastTime = dwCurrentTime;

			std::string title = WINDOW_NAME;
			title += " - FPS: " + std::to_string(g_Fps);
			SetWindowTextA(g_Window, title.c_str());
		}
	}

	timeEndPeriod(1);

	Manager::Uninit();

	// ImGui の終了も main 側で一元管理（シーン解放後）
	ImGuiManager::Shutdown();

	UnregisterClass(CLASS_NAME, wcex.hInstance);

	CoUninitialize();

	return (int)msg.wParam;
}



LRESULT CALLBACK WndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam)
{
    // ImGuiManager の WndProcHandler に転送（戻り値が非ゼロなら既に処理済み）
    if (ImGuiManager::WndProcHandler(hWnd, uMsg, wParam, lParam))
        return TRUE;

	switch(uMsg)
	{
	case WM_SIZE:
	{
		UINT width = LOWORD(lParam);
		UINT height = HIWORD(lParam);
        ImGuiManager::OnResize(wParam, width, height);
		break;
	}

	case WM_DESTROY:
		PostQuitMessage(0);
		break;

	case WM_KEYDOWN:
		switch(wParam)
		{
		case VK_ESCAPE:
			DestroyWindow(hWnd);
			break;
		}
		break;

	default:
		break;
	}

	return DefWindowProc(hWnd, uMsg, wParam, lParam);
}
