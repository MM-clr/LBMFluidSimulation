#pragma once
#include <list>
#include "gameObject.h"
#include <vector>
#include <type_traits>

class Scene
{
public:
	virtual void Init();
	virtual void Uninit();
	virtual void Update();
	virtual void Draw();
	template <class T>
	 T* AddGameObject(int Layer)
	{
		T* obj = new T();
		mGameObject[Layer].push_back(obj);
		obj->Init();
		return obj;
	}

	template<typename T>
	 T* GetGameObject()
	{
		for (int i = 0; i < 3; i++)
		{
			for (auto& obj : mGameObject[i])
			{
				T* t = dynamic_cast<T*>(obj);
				if (t != nullptr)
				{
					return t;
				}
			}
		}
		return nullptr;
	}
	template<typename T>
	 std::list<T*> GetGameObjects()
	{
		std::list<T*> result;
		for (int i = 0; i < 3; i++)
		{
			for (auto& obj : mGameObject[i])
			{
				T* t = dynamic_cast<T*>(obj);
				if (t != nullptr)
				{
					result.push_back(t);
				}

			}
		}
		return result;
	}

private:
	std::list<GameObject*> mGameObject[3]; // List of object IDs in the scene
};

