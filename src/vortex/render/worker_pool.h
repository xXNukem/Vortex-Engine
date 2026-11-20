#pragma once

#include <condition_variable>
#include <cstddef>
#include <exception>
#include <mutex>
#include <thread>
#include <vector>

namespace vortex::detail {

class WorkerPool {
public:
    explicit WorkerPool(unsigned threadCount);
    ~WorkerPool();

    WorkerPool(const WorkerPool&) = delete;
    WorkerPool& operator=(const WorkerPool&) = delete;

    using Task = void (*)(const void*, std::size_t, std::size_t);
    void run(std::size_t count, std::size_t grain, Task task, const void* context);

private:
    void work(std::size_t index);
    void stop();

    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable ready_, done_;
    std::size_t generation_ = 0, pending_ = 0, count_ = 0, ranges_ = 0;
    Task task_ = nullptr;
    const void* context_ = nullptr;
    std::exception_ptr error_;
    bool stopping_ = false;
};

template<class Action>
void forRanges(WorkerPool* pool, std::size_t count, std::size_t grain, const Action& action)
{
    if (pool && count > grain) {
        pool->run(count, grain, [](const void* context, std::size_t begin, std::size_t end) {
            (*static_cast<const Action*>(context))(begin, end);
        }, &action);
    } else {
        action(0, count);
    }
}

} // namespace vortex::detail
