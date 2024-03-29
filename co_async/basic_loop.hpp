#pragma once

#include <deque>
#include <functional>
#include <type_traits>
#include <co_async/concepts.hpp>
#include <co_async/error_handling.hpp>
#include <co_async/auto_destroy_promise.hpp>
#include <co_async/task.hpp>

namespace co_async {

struct BasicLoop {
    void run() {
        while (!mQueue.empty()) {
            auto coroutine = mQueue.front();
            mQueue.pop_front();
            coroutine.resume();
        }
    }

    void push(std::coroutine_handle<> coroutine) {
        mQueue.push_back(coroutine);
    }

    BasicLoop() = default;
    BasicLoop(BasicLoop &&) = delete;

private:
    std::deque<std::coroutine_handle<>> mQueue;
};

template <Awaitable A>
inline Task<void, AutoDestroyPromise> taskEnqueueHelper(A a) {
    co_return co_await std::move(a);
}

template <class A>
    requires(!Awaitable<A> && std::invocable<A> &&
             Awaitable<std::invoke_result_t<A>>)
inline Task<void, AutoDestroyPromise> taskEnqueueHelper(A a) {
    return taskEnqueueHelper(std::invoke(std::move(a)));
}

inline void loop_enqueue(BasicLoop &loop, auto task) {
    auto t = taskEnqueueHelper(std::move(task));
    auto coroutine = t.operator co_await().await_suspend(std::noop_coroutine());
    loop.push(coroutine);
    t.release();
}

} // namespace co_async
