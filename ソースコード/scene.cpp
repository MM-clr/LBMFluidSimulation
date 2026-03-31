#include "main.h"
#include "scene.h"
#include "renderer.h"
#include "input.h"
#include "polygon.h"
#include "camera.h"

//std::list<GameObject*>Scene::mGameObject[3];
void Scene::Init()
{
	/*
	AddGameObject<Polygon2D>(2);
	AddGameObject<camera>(0);
	AddGameObject<Field>(0);
	AddGameObject<Player>(1);
	AddGameObject<Enemy>(1)->SetPosition({ -5.0f, 0.0f, 3.0f });
	AddGameObject<Enemy>(1)->SetPosition({ 0.0f, 0.0f, 3.0f });
	AddGameObject<Enemy>(1)->SetPosition({ 5.0f, 0.0f, 3.0f });*/


}
void Scene::Uninit()
{
	for (auto GameObject : mGameObject)
	{
		for (auto gameObject : GameObject)
		{
			gameObject->Uninit();
			delete gameObject;
		}
		GameObject.clear();
	}

	
}

void Scene::Update()
{
	
	for (auto GameObject : mGameObject)
	{
		for (auto gameObject : GameObject)
		{

			gameObject->Update();
		}
	}
	for (auto& gameobject : mGameObject)
	{
		gameobject.remove_if([](GameObject* gameObject)
			{
				return gameObject->Destroy();
			});
	}

}

void Scene::Draw()
{
	
	//Zé≤ÇÃãóó£Ç≈É\Å[Ég
	camera* cam = GetGameObject<camera>();
	if (cam!=nullptr)
	{
		Vector3 camPos = cam->GetPosition();
		mGameObject[1].sort([&](GameObject* a, GameObject* b)
			{
				return a->GetDistance(camPos) > b->GetDistance(camPos);
			});
	}
	
	for (auto GameObject : mGameObject)
	{
		for (auto gameObject : GameObject)
		{
			gameObject->Draw();
		}
	}
}