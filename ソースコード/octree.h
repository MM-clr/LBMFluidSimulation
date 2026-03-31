#pragma once
#include <vector>
#include "vector3.h"

// Octree クラスは、3D 空間を効率的に分割し、近傍探索を高速化するためのデータ構造
// このクラスは、粒子やオブジェクトの空間的な配置を管理し、
// 効率的な近傍探索を可能にします。
// 主な用途:
// - 流体シミュレーションにおける粒子間の相互作用の計算
// - 衝突検出や空間分割を必要とする物理シミュレーション
// - ゲームやグラフィックスにおける空間的な最適化
class Octree {
public:
    // コンストラクタとデストラクタ
    Octree(const Vector3& center, float halfSize, int maxDepth, int maxParticles, const std::vector<Vector3>* positions);
    ~Octree();

    // 公開メソッド
    void Insert(int particleIndex, const Vector3& position);
    void Query(const Vector3& position, float radius, std::vector<int>& result) const;
    void Clear(); // この行を追加

private:
    // Node構造体の定義をヘッダーに移動
    struct Node {
        Vector3 center;
        float halfSize;
        std::vector<int> particleIndices; // メンバー名を 'particleIndices' に統一
        Node* children[8] = { nullptr };
        bool isLeaf = true;
    };

    // メンバー変数の宣言
    Node* mRoot;
    int mMaxDepth;
    int mMaxParticles;
    const std::vector<Vector3>* mPositionsPtr;

    // プライベートヘルパーメソッド
    void Insert(Node* node, int particleIndex, const Vector3& position, int depth);
    void Query(Node* node, const Vector3& position, float radius, std::vector<int>& result) const;
    void Subdivide(Node* node, int depth);
    bool IsWithinBounds(const Vector3& point, const Node* node) const;
    void Clear(Node* node); // この行を追加
};