#pragma once

#include <d3d11.h>
#include <Windows.h>
#include <functional>
#include <vector>
#include <mutex>

// ImGui のレンダリングと Direct3D 状態の保存/復元を行うユーティリティ
namespace ImGuiManager
{
    using GuiCallback = std::function<void()>; // シーンが描画時に登録する GUI コールバック

    // ImGui を初期化する（Win32/DX11 バックエンドの初期化、フォント登録もここで行う）
    void Init(HWND hwnd);

    // フレームループの先頭で呼ぶ（ImGui NewFrame を内包） --- ※ Manager::Draw の呼び出し設計により
    //    この関数は Renderer::Begin() → シーン描画 → この NewFrame() の順で呼ばれる想定です。
    void NewFrame();

    // ImGui の描画（Direct3D の状態保存/復元を行う）
    // - NewFrame() 呼び出し後にシーンが登録した GUI コールバックを実行してから描画します。
    void Render();

    // 1フレーム分の ImGui 処理を実行する（NewFrame -> Render）
    void RenderFrame();

    // ImGui をシャットダウンする（バックエンド破棄、コンテキスト破棄）
    void Shutdown();

    // Win32 メッセージを ImGui に渡す（WndProc から呼ぶ）
    LRESULT WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

    // ウィンドウリサイズ時の処理をまとめる
    void OnResize(WPARAM sizeType, UINT width, UINT height);

    // シーン側がフレーム中に ImGui コマンドを実行したい場合は
    // Renderer::Begin() → Scene::Draw() の中でこの関数を使って
    // コールバックを登録してください。登録されたコールバックは
    // NewFrame() → (GUI コールバック実行) → Render() の流れで実行されます。
    void EnqueueGui(GuiCallback cb);
}