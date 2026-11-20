#include "worker_pool.h"

#include <algorithm>

namespace vortex::detail {
namespace {

std::size_t boundary(std::size_t index, std::size_t count, std::size_t ranges)
{
    return index * (count / ranges) + std::min(index, count % ranges);
}

} // namespace

WorkerPool::WorkerPool(unsigned threadCount)
{
    try {
        for (unsigned i = 1; i < threadCount; ++i)
            threads_.emplace_back(&WorkerPool::work, this, i);
    } catch (...) {
        stop();
        throw;
    }
}

WorkerPool::~WorkerPool()
{
    stop();
}

void WorkerPool::stop()
{
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        stopping_ = true;
    }
    ready_.notify_all();
    for (auto& thread : threads_) thread.join();
}

void WorkerPool::run(std::size_t count, std::size_t grain, Task task, const void* context)
{
    const auto ranges = std::min(threads_.size() + 1, 1 + (count - 1) / grain);
    if (ranges == 1) {
        task(context, 0, count);
        return;
    }
    {
        const std::lock_guard<std::mutex> lock(mutex_);
        count_ = count;
        ranges_ = ranges;
        pending_ = ranges - 1;
        task_ = task;
        context_ = context;
        error_ = nullptr;
        ++generation_;
    }
    ready_.notify_all();

    std::exception_ptr error;
    try {
        task(context, 0, boundary(1, count, ranges));
    } catch (...) {
        error = std::current_exception();
    }

    {
        std::unique_lock<std::mutex> lock(mutex_);
        done_.wait(lock, [this] { return pending_ == 0; });
        if (!error) error = error_;
    }
    if (error) std::rethrow_exception(error);
}

void WorkerPool::work(std::size_t index)
{
    std::unique_lock<std::mutex> lock(mutex_);
    std::size_t generation = 0;
    while (true) {
        ready_.wait(lock, [&] { return stopping_ || generation != generation_; });
        if (stopping_) return;
        generation = generation_;
        if (index >= ranges_) continue;
        const auto begin = boundary(index, count_, ranges_);
        const auto end = boundary(index + 1, count_, ranges_);
        const auto task = task_;
        const auto context = context_;
        lock.unlock();

        std::exception_ptr error;
        try {
            task(context, begin, end);
        } catch (...) {
            error = std::current_exception();
        }

        lock.lock();
        if (error && !error_) error_ = error;
        if (--pending_ == 0) done_.notify_one();
    }
}

} // namespace vortex::detail
