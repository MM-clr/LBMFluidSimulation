#include "main.h"
#include "game.h"
#include "renderer.h"
#include "input.h"
#include "polygon.h"
#include "camera.h"
#include "manager.h"
#include "fluidSimulation.h"
#include "fluidcircle.h"
void Game::Init()
{
   
    AddGameObject<camera>(0);
   
    //AddGameObject<Player>(1)->SetPosition({ 0.0f, 1.0f, 5.0f });
    

    
    // 流体シミュレーション
    FluidSimulation* fluidSimulation = AddGameObject<FluidSimulation>(1);
    
   
    
   
    
   
}

void Game::Update()
{
    Scene::Update();
    
   
}

void Game::Uninit()
{
   
    
    Scene::Uninit();
}