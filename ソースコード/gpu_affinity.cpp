// gpu_affinity.cpp
// ノートPC の外部 GPU (Optimus / AMD 切替) を優先するためのエクスポート。
// 動作はドライバ依存のヒントであり、すべての環境で強制的に外部 GPU を選ばせるものではありません。

extern "C" {
#if defined(_MSC_VER)
__declspec(dllexport) unsigned long NvOptimusEnablement = 0x00000001;
__declspec(dllexport) int AmdPowerXpressRequestHighPerformance = 1;
#else
// GCC/Clang 用（必要に応じて調整）
unsigned long NvOptimusEnablement __attribute__((visibility("default"))) = 0x00000001;
int AmdPowerXpressRequestHighPerformance __attribute__((visibility("default"))) = 1;
#endif
}