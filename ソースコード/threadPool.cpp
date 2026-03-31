#include "threadPool.h"
#include <stdexcept> // std::runtime_error のために追加

ThreadPool::ThreadPool(size_t threads) : mStop(false), mBusy(0) {
    for (size_t i = 0; i < threads; ++i) {
        mWorkers.emplace_back([this] {
            while (true) {
                std::function<void()> task;
                {
                    std::unique_lock<std::mutex> lock(mQueueMutex);
                    mCondition.wait(lock, [this] { return mStop || !mTasks.empty(); });
                    if (mStop && mTasks.empty()) {
                        return;
                    }
                    task = std::move(mTasks.front());
                    mTasks.pop();
                    mBusy++;
                }

                task(); // タスクを実行

                {
                    std::unique_lock<std::mutex> lock(mQueueMutex);
                    mBusy--;
                }
                mWaitCondition.notify_all(); // タスク完了を通知
            }
        });
    }
}

ThreadPool::~ThreadPool() {
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        mStop = true;
    }
    mCondition.notify_all();
    for (std::thread& worker : mWorkers) {
        worker.join();
    }
}

void ThreadPool::EnqueueTask(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(mQueueMutex);
        if (mStop) {
            throw std::runtime_error("enqueue on stopped ThreadPool");
        }
        mTasks.emplace(std::move(task));
    }
    mCondition.notify_one();
}

// WaitForAllTasksの実装
void ThreadPool::WaitForAllTasks() {
    std::unique_lock<std::mutex> lock(mQueueMutex);
    // タスクキューが空で、かつ実行中のタスクもない状態になるまで待機
    mWaitCondition.wait(lock, [this] { return mTasks.empty() && mBusy == 0; });
}