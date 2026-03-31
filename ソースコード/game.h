#pragma once
#include "scene.h"

class Game:public Scene
{
public:
	void Init() override;
	void Update() override;
	void Uninit() override;

private:
	
};

