#pragma once

#include "gameObject.h"
#include <list>
#include <vector>

class Scene; // 前方宣言
class Renderer; // 前方宣言

class Manager
{
private:
	static Scene* m_Scene;
	static Scene* mNextScene;
	static Renderer* m_Renderer; // Rendererへのポインタを追加
public:
	static void Init();
	static void Uninit();
	static void Update();
	static void Draw();

	static Scene* GetScene() { return m_Scene; }
	static Renderer* GetRenderer() { return m_Renderer; } // GetRendererメソッドを追加
	template<typename T>
	static void SetScene() 
	{
		mNextScene = new T();
	}
};
