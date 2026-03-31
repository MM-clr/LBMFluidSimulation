#include "ImGuiManager.h"
#include "renderer.h"

// ImGui ヘッダ（レンダリング/プラットフォーム呼び出しに必要）
#include "ImGUI\imgui.h"
#include "ImGUI\imgui_impl_win32.h"
#include "ImGUI\imgui_impl_dx11.h"

#include <d3d11.h> // ビューポート / シザーの保存に必要
#include <cassert>
#include <atomic>
#include <cstdio>
#include <vector>
#include <mutex>

#ifndef IMGUI_MANAGER_ENABLE_FRAME_LOG
#define IMGUI_MANAGER_ENABLE_FRAME_LOG 0
#endif

namespace
{
    // GUI コールバックキュー（シーンが描画中に登録する）
    static std::vector<ImGuiManager::GuiCallback> g_GuiQueue;
    static std::mutex g_GuiQueueMutex;

    // デバッグ用カウンタ（NewFrame / Render の呼び出し回数確認）
    static std::atomic_uint s_newFrameCalls(0);
    static std::atomic_uint s_renderCalls(0);
    static std::atomic_bool s_initialized(false);
}

// imgui_impl_win32.h では WndProcHandler の宣言が #if 0 でコメントアウトされているため
// ここでグローバル名前空間に正しい注記で前方宣言を行う。
extern IMGUI_IMPL_API LRESULT ImGui_ImplWin32_WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam);

namespace ImGuiManager
{
    void Init(HWND hwnd)
    {
        if (s_initialized.load())
        {
            OutputDebugStringA("ImGuiManager::Init skipped (already initialized).\n");
            return;
        }

        // ImGui コンテキスト生成と Win32 / DX11 バックエンド初期化
        if (!ImGui::GetCurrentContext())
        {
            ImGui::CreateContext();
        }
        ImGui_ImplWin32_Init(hwnd);
        ImGui_ImplDX11_Init(Renderer::GetDevice(), Renderer::GetDeviceContext());

        // フォント登録とテクスチャ作成（Renderer 側に実装済みの関数を呼ぶ）
        Renderer::InitImGuiFonts();
        s_initialized.store(true);
    }

    void NewFrame()
    {
        if (!ImGui::GetCurrentContext())
            return;

        // デバッグ: NewFrame 呼び出しログ（必要時のみ）
#if IMGUI_MANAGER_ENABLE_FRAME_LOG
        {
            char dbg[256];
            unsigned cnt = ++s_newFrameCalls;
            void* ctx = (void*)ImGui::GetCurrentContext();
            sprintf_s(dbg, sizeof(dbg), "ImGuiManager::NewFrame called. count=%u ctx=%p thread=%u\n", cnt, ctx, (unsigned)GetCurrentThreadId());
            OutputDebugStringA(dbg);
        }
#endif

        // 公式サンプル推奨の順序に合わせる: Win32 -> DX11 -> ImGui::NewFrame
        ImGui_ImplWin32_NewFrame();
        ImGui_ImplDX11_NewFrame();
        ImGui::NewFrame();
    }

    void Render()
    {
        if (!ImGui::GetCurrentContext())
            return;

        // デバッグ: Render 呼び出しログ（必要時のみ）
#if IMGUI_MANAGER_ENABLE_FRAME_LOG
        {
            char dbg[512];
            unsigned cnt = ++s_renderCalls;
            void* ctx = (void*)ImGui::GetCurrentContext();
            sprintf_s(dbg, sizeof(dbg), "ImGuiManager::Render called. count=%u ctx=%p thread=%u\n", cnt, ctx, (unsigned)GetCurrentThreadId());
            OutputDebugStringA(dbg);
        }
#endif

        ID3D11DeviceContext* ctx = Renderer::GetDeviceContext();
        if (!ctx)
            return;

        // --- フレーム中にシーンが登録した GUI コールバックを実行 ---
        // コールバックは ImGui::NewFrame() の後で実行されることが前提です。
        {
            std::vector<GuiCallback> localQueue;
            {
                std::lock_guard<std::mutex> lg(g_GuiQueueMutex);
                if (!g_GuiQueue.empty())
                {
                    localQueue.swap(g_GuiQueue);
                }
            }
            for (auto &cb : localQueue)
            {
                if (cb) cb();
            }
        }

        // ImGui の Render を呼ぶ（NewFrame は呼ばれている前提）
        ImGui::Render();

        // デバッグ: DrawData/フォント状態ログ（必要時のみ）
#if IMGUI_MANAGER_ENABLE_FRAME_LOG
        {
            ImDrawData* dd = ImGui::GetDrawData();
            if (dd)
            {
                char info[256];
                sprintf_s(info, sizeof(info), "ImGuiManager::Render: DrawData=%p CmdLists=%d TotalVtx=%d TotalIdx=%d\n",
                          (void*)dd, dd->CmdListsCount, dd->TotalVtxCount, dd->TotalIdxCount);
                OutputDebugStringA(info);
            }
            else
            {
                OutputDebugStringA("ImGuiManager::Render: DrawData == nullptr\n");
            }
        }

        {
            if (ImGui::GetIO().Fonts && ImGui::GetIO().Fonts->IsBuilt())
                OutputDebugStringA("ImGui: Fonts built\n");
            else
                OutputDebugStringA("ImGui: Fonts not built or Fonts==NULL\n");
        }
#endif

        // -------------------------
        // 重要: 描画前に現在の D3D11 コンテキスト状態を保存する
        // -------------------------
        const UINT MAX_PS_SRV = 16;
        ID3D11ShaderResourceView* prevPSSrvs[MAX_PS_SRV] = {};
        ID3D11SamplerState* prevPSSamplers[MAX_PS_SRV] = {};
        ctx->PSGetShaderResources(0, MAX_PS_SRV, prevPSSrvs);
        ctx->PSGetSamplers(0, MAX_PS_SRV, prevPSSamplers);

        // シェーダ保存
        ID3D11VertexShader* prevVS = nullptr;
        ID3D11PixelShader* prevPS = nullptr;
        ID3D11GeometryShader* prevGS = nullptr;
        ctx->VSGetShader(&prevVS, nullptr, nullptr);
        ctx->PSGetShader(&prevPS, nullptr, nullptr);
        ctx->GSGetShader(&prevGS, nullptr, nullptr);

        // IA 保存 (VB, IB, InputLayout, Topology)
        ID3D11InputLayout* prevLayout = nullptr;
        ctx->IAGetInputLayout(&prevLayout);

        const UINT MAX_VBS = D3D11_IA_VERTEX_INPUT_RESOURCE_SLOT_COUNT;
        ID3D11Buffer* prevVBs[MAX_VBS] = {};
        UINT prevStrides[MAX_VBS] = {};
        UINT prevOffsets[MAX_VBS] = {};
        ctx->IAGetVertexBuffers(0, MAX_VBS, prevVBs, prevStrides, prevOffsets);

        ID3D11Buffer* prevIB = nullptr;
        DXGI_FORMAT prevIBFmt = DXGI_FORMAT_UNKNOWN;
        UINT prevIBOffset = 0;
        ctx->IAGetIndexBuffer(&prevIB, &prevIBFmt, &prevIBOffset);

        D3D11_PRIMITIVE_TOPOLOGY prevTopo = D3D11_PRIMITIVE_TOPOLOGY_UNDEFINED;
        ctx->IAGetPrimitiveTopology(&prevTopo);

        // Rasterizer / Viewport / Scissor 保存
        ID3D11RasterizerState* prevRS = nullptr;
        ctx->RSGetState(&prevRS);

        const UINT MAX_VP = D3D11_VIEWPORT_AND_SCISSORRECT_MAX_INDEX;
        D3D11_VIEWPORT prevViewports[MAX_VP];
        UINT numPrevViewports = MAX_VP;
        ctx->RSGetViewports(&numPrevViewports, prevViewports);

        D3D11_RECT prevScissors[MAX_VP];
        UINT numPrevScissors = MAX_VP;
        ctx->RSGetScissorRects(&numPrevScissors, prevScissors);

        // Blend / OM / DSV 保存
        ID3D11BlendState* prevBlend = nullptr;
        FLOAT prevBlendFactor[4] = { 0,0,0,0 };
        UINT  prevSampleMask = 0xffffffff;
        ctx->OMGetBlendState(&prevBlend, prevBlendFactor, &prevSampleMask);

        ID3D11DepthStencilView* prevDSV = nullptr;
        ID3D11RenderTargetView* prevRTV = nullptr;
        ctx->OMGetRenderTargets(1, &prevRTV, &prevDSV);

        ID3D11DepthStencilState* prevDS = nullptr;
        UINT prevRef = 0;
        ctx->OMGetDepthStencilState(&prevDS, &prevRef);

        // 重要: 他コードが残した SRV/UAV が ImGui の描画を壊す場合があるため解除
        Renderer::UnbindAllShaderResources();

        // Flush を挟む
        ctx->Flush();

        // ImGui を描画（内部で SRV をバインドする）
        ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());

        // -------------------------
        // 描画後：保存しておいた状態を復元する
        // -------------------------
        // 復元：PS SRV / Sampler
        ctx->PSSetShaderResources(0, MAX_PS_SRV, prevPSSrvs);
        ctx->PSSetSamplers(0, MAX_PS_SRV, prevPSSamplers);

        // 復元：シェーダ
        ctx->VSSetShader(prevVS, nullptr, 0);
        ctx->PSSetShader(prevPS, nullptr, 0);
        ctx->GSSetShader(prevGS, nullptr, 0);

        // 復元：IA (VB, IB, InputLayout, Topology)
        ctx->IASetVertexBuffers(0, MAX_VBS, prevVBs, prevStrides, prevOffsets);
        ctx->IASetIndexBuffer(prevIB, prevIBFmt, prevIBOffset);
        ctx->IASetInputLayout(prevLayout);
        ctx->IASetPrimitiveTopology(prevTopo);

        // 復元：Rasterizer, Blend, DepthStencil, OM render targets
        if (numPrevViewports > 0)
            ctx->RSSetViewports(numPrevViewports, prevViewports);
        if (numPrevScissors > 0)
            ctx->RSSetScissorRects(numPrevScissors, prevScissors);

        ctx->RSSetState(prevRS);
        ctx->OMSetBlendState(prevBlend, prevBlendFactor, prevSampleMask);
        ctx->OMSetDepthStencilState(prevDS, prevRef);
        ctx->OMSetRenderTargets(1, &prevRTV, prevDSV);

        // Release: Get 系は参照カウントを増やしているので Release する
        if (prevRTV) prevRTV->Release();
        if (prevDSV) prevDSV->Release();
        if (prevBlend) prevBlend->Release();
        if (prevDS) prevDS->Release();
        if (prevRS) prevRS->Release();
        if (prevLayout) prevLayout->Release();
        if (prevIB) prevIB->Release();
        if (prevVS) prevVS->Release();
        if (prevPS) prevPS->Release();
        if (prevGS) prevGS->Release();

        for (UINT i = 0; i < MAX_VBS; ++i) {
            if (prevVBs[i]) prevVBs[i]->Release();
        }
        for (UINT i = 0; i < MAX_PS_SRV; ++i) {
            if (prevPSSrvs[i]) prevPSSrvs[i]->Release();
            if (prevPSSamplers[i]) prevPSSamplers[i]->Release();
        }
    }

    void RenderFrame()
    {
        NewFrame();
        Render();
    }

    void Shutdown()
    {
        if (!s_initialized.load() || !ImGui::GetCurrentContext())
            return;

        ImGui_ImplDX11_Shutdown();
        ImGui_ImplWin32_Shutdown();
        ImGui::DestroyContext();
        s_initialized.store(false);
    }

    LRESULT WndProcHandler(HWND hWnd, UINT msg, WPARAM wParam, LPARAM lParam)
    {
        if (!s_initialized.load() || !ImGui::GetCurrentContext())
            return 0;

        // グローバル名前空間の ImGui_ImplWin32_WndProcHandler を明示的に呼ぶ
        return ::ImGui_ImplWin32_WndProcHandler(hWnd, msg, wParam, lParam);
    }

    void OnResize(WPARAM sizeType, UINT width, UINT height)
    {
        if (sizeType == SIZE_MINIMIZED) return;
        if (width == 0 || height == 0) return;
        Renderer::Resize(width, height);
    }

    void EnqueueGui(GuiCallback cb)
    {
        std::lock_guard<std::mutex> lg(g_GuiQueueMutex);
        g_GuiQueue.emplace_back(std::move(cb));
    }
}