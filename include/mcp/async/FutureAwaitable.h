//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: FutureAwaitable.h
// Purpose: Awaiters enabling co_await on std::future for C++20 coroutines
//==========================================================================================================

#pragma once

#include <future>
#include <coroutine>
#include <chrono>
#include <thread>
#include <utility>

namespace mcp {
namespace async {

// Awaiter for std::future<T>

template <typename T>
class FutureAwaitable {
public:
    explicit FutureAwaitable(std::future<T>&& f) : fut(std::move(f)) {}

    bool await_ready() const noexcept {
        using namespace std::chrono_literals;
        return fut.wait_for(0s) == std::future_status::ready;
    }

    void await_suspend(std::coroutine_handle<> h) {
        // Offload waiting to a background thread and resume when ready.
        // We intentionally detach here; callers should prefer a central executor if needed later.
        std::thread waiter([this, h]() mutable {
            try {
                fut.wait();
            } catch (...) {
                // swallow exceptions here; they'll be rethrown in await_resume via fut.get()
            }
            h.resume();
        });
        waiter.detach();
    }

    T await_resume() { return fut.get(); }

private:
    std::future<T> fut;
};

// void specialization

template <>
class FutureAwaitable<void> {
public:
    explicit FutureAwaitable(std::future<void>&& f) : fut(std::move(f)) {}

    bool await_ready() const noexcept {
        using namespace std::chrono_literals;
        return fut.wait_for(0s) == std::future_status::ready;
    }

    void await_suspend(std::coroutine_handle<> h) {
        std::thread waiter([this, h]() mutable {
            try { fut.wait(); } catch (...) {}
            h.resume();
        });
        waiter.detach();
    }

    void await_resume() { fut.get(); }

private:
    std::future<void> fut;
};

// Helper factory

template <typename T>
inline FutureAwaitable<T> makeFutureAwaitable(std::future<T>&& fut) {
    return FutureAwaitable<T>(std::move(fut));
}

} // namespace async
} // namespace mcp
