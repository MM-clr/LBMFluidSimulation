#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <functional>
#include <atomic> // std::atomic のために追加

// スレッドプールクラス
class ThreadPool {
public:
    // コンストラクタ
    explicit ThreadPool(size_t threadCount);

    // デストラクタ
    ~ThreadPool();

    // タスク追加
    void EnqueueTask(std::function<void()> task);

    // すべてのタスクが完了するのを待つメソッド
    void WaitAll();

    // スレッド数を取得するメソッド
    size_t GetThreadCount() const { return mWorkers.size(); }

    void WaitForAllTasks(); // この行を追加

private:
    // ワーカーズレッドの関数
    void WorkerThread();

    std::vector<std::thread> mWorkers; // スレッドのリスト
    std::queue<std::function<void()>> mTasks; // タスクキュー

    std::mutex mQueueMutex; // タスクキューの排他用ミューテックス
    std::condition_variable mCondition; // タスクの通知用
    std::condition_variable mWaitCondition; // WaitAll用のcondition
    std::atomic<size_t> mBusy = 0; // ビジースレッド数を管理
    std::atomic<bool> mStop = false; // スレッドプールの停止フラグ
};