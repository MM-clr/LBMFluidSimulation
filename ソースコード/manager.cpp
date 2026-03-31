#include "main.h"
#include "manager.h"
#include "renderer.h"
#include "input.h"
#include "polygon.h"
#include "camera.h"
#include "scene.h"
#include "game.h"

#include "ImGuiManager.h"

// 以前はここに d3d11 / ImGui ヘッダを直接書いて ImGui 描画処理を持っていたが
// システム的な ImGui の状態保存/復元処理を ImGuiManager に移譲した。

Scene* Manager::m_Scene = nullptr;
Scene* Manager::mNextScene = nullptr;
Renderer* Manager::m_Renderer = nullptr;

void Manager::Init()
{
	Input::Init();

	Renderer::Init(); // ??I????o?????X

	m_Scene = new Game;
	m_Scene->Init();
}

void Manager::Uninit()
{
	m_Scene->Uninit();
	delete m_Scene;

	Renderer::Uninit();

	Input::Uninit();
}

void Manager::Update()
{
	Input::Update();
	m_Scene->Update();

	if (mNextScene != nullptr)
	{
		m_Scene->Uninit();
		delete m_Scene;
		m_Scene = mNextScene;
		m_Scene->Init();
		mNextScene = nullptr;
	}
}

void Manager::Draw()
{
	// Renderer Begin
    Renderer::Begin();

	// シーン描画（3D 描画など）
	m_Scene->Draw();

  // ImGui の 1フレーム処理は ImGuiManager に集約
	ImGuiManager::RenderFrame();

	// --- ここまで ImGui 描画の保護 ---
	// 3D 描画に戻す（通常は深度有効）
	Renderer::SetDepthEnable(true);

   // Renderer End (Present)
	Renderer::End();
}
