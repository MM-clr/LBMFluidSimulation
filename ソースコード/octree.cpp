#include "octree.h"
#include "fluidSimulation.h" // FluidParticle
#include <cmath> // sqrt関数の使用
#include <functional> // std::function の使用
#include <algorithm>


// コンストラクタ
Octree::Octree(const Vector3& center, float halfSize, int maxDepth, int maxParticles, const std::vector<Vector3>* positions)
    : mRoot(new Node{ center, halfSize }), mMaxDepth(maxDepth), mMaxParticles(maxParticles), mPositionsPtr(positions) {
}

// デストラクタ
// メモリに確保されたノードの再帰的な解放
Octree::~Octree() {
    // 再帰的にノードを削除するラムダ関数
    std::function<void(Node*)> deleteNode = [&](Node* node) {
        if (!node) return;
        for (auto& child : node->children) {
            deleteNode(child); // 子ノードの削除
        }
        delete node; // 現在のノードの削除
    };
    deleteNode(mRoot); // ルートノードから削除を開始
}

// 粒子の挿入
// particleIndex: 挿入する粒子のインデックス
// position: 挿入する粒子の位置
void Octree::Insert(int particleIndex, const Vector3& position) {
    Insert(mRoot, particleIndex, position, 0); // 再帰的な挿入
}

// ノードへの粒子の挿入（内部処理用）
// node: 現在のノード
// particleIndex: 挿入する粒子のインデックス
// position: 挿入する粒子の位置
// depth: 現在の深度
void Octree::Insert(Node* node, int particleIndex, const Vector3& position, int depth) {
    // 粒子がノードの範囲外の場合の処理
    if (!IsWithinBounds(position, node)) return;

    // 葉ノードの場合の処理
    if (node->isLeaf) {
        if (node->particleIndices.size() < static_cast<size_t>(mMaxParticles) || depth >= mMaxDepth) {
            node->particleIndices.push_back(particleIndex);
        } else {
            Subdivide(node, depth);
            // 古い粒子を子ノードに再分配
            for (int oldIndex : node->particleIndices) {
                Insert(node, oldIndex, (*mPositionsPtr)[oldIndex], depth);
            }
            node->particleIndices.clear();
            // 新しい粒子を挿入
            Insert(node, particleIndex, position, depth);
        }
    } else {
        // 子ノードへの再帰的な挿入
        int octant = 0;
        if (position.x > node->center.x) octant |= 1;
        if (position.y > node->center.y) octant |= 2;
        if (position.z > node->center.z) octant |= 4;
        Insert(node->children[octant], particleIndex, position, depth + 1);
    }
}

// 近傍粒子の検索
// position: 検索の中心位置
// radius: 検索範囲の半径
// result: 近傍粒子のインデックスを格納するベクター
void Octree::Query(const Vector3& position, float radius, std::vector<int>& result) const {
    Query(mRoot, position, radius, result); // ルートノードから検索を開始
}

// ノード内での近傍粒子の検索（内部処理用）
// node: 現在のノード
// position: 検索の中心位置
// radius: 検索範囲の半径
// result: 近傍粒子のインデックスを格納するベクター
// このメソッドは、ノードが存在しない場合や検索範囲外の場合に処理をスキップします。
void Octree::Query(Node* node, const Vector3& position, float radius, std::vector<int>& result) const {
    if (!node) return;

    // AABBと球の交差判定 (より正確な判定)
    float dx = std::max(0.0f, std::abs(position.x - node->center.x) - node->halfSize);
    float dy = std::max(0.0f, std::abs(position.y - node->center.y) - node->halfSize);
    float dz = std::max(0.0f, std::abs(position.z - node->center.z) - node->halfSize);
    if (dx * dx + dy * dy + dz * dz > radius * radius) {
        return; // ノードが検索範囲と交差していない
    }

    // 葉ノードの場合の処理
    if (node->isLeaf) {
        for (int particleIndex : node->particleIndices) {
            // 粒子が本当に検索範囲内にあるかチェック
            if (((*mPositionsPtr)[particleIndex] - position).lengthSq() <= radius * radius) {
                result.push_back(particleIndex);
            }
        }
    } else {
        // 子ノードの再帰的な検索
        for (auto& child : node->children) {
            Query(child, position, radius, result);
        }
    }
}

// ノードの分割
// node: 分割対象のノード
// depth: 現在の深度
// このメソッドは、現在のノードを8つの子ノードに分割します。
// 各子ノードの中心座標を計算し、新しいノードを作成します。
void Octree::Subdivide(Node* node, int depth) {
    node->isLeaf = false;
    float quarterSize = node->halfSize / 2.0f; // 子ノードの半径
    for (int i = 0; i < 8; ++i) {
        // 子ノードの中心の計算
        Vector3 offset(
            (i & 1 ? quarterSize : -quarterSize),
            (i & 2 ? quarterSize : -quarterSize),
            (i & 4 ? quarterSize : -quarterSize)
        );
        node->children[i] = new Node{ node->center + offset, quarterSize }; // 子ノードの作成
    }
}

// 点がノードの範囲内にあるかどうかの判定
// point: 判定する点
// node: 判定対象のノード
// 戻り値: 点がノード内にある場合は true、それ以外は false
bool Octree::IsWithinBounds(const Vector3& point, const Node* node) const {
    return std::abs(point.x - node->center.x) <= node->halfSize &&
           std::abs(point.y - node->center.y) <= node->halfSize &&
           std::abs(point.z - node->center.z) <= node->halfSize;
}

// プライベートヘルパー関数: 再帰的にノードをクリアする
void Octree::Clear(Node* node) {
    if (!node) {
        return;
    }

    // 粒子インデックスのリストをクリア
    node->particleIndices.clear();
    // ノードを葉ノードとしてリセット
    node->isLeaf = true;

    // 子ノードが存在する場合は再帰的にクリア
    // メモリは再利用するため、子ノードのポインタは削除しない
    for (int i = 0; i < 8; ++i) {
        if (node->children[i] != nullptr) {
            Clear(node->children[i]);
        }
    }
}

// 公開メソッド: 八分木をクリアする
void Octree::Clear() {
    if (mRoot) {
        Clear(mRoot);
    }
}