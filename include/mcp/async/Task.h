//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Task.h
// Purpose: Minimal coroutine Task type bridging to std::future for C++20
//==========================================================================================================

#pragma once

#include <coroutine>
#include <future>
#include <exception>
#include <utility>
#include <type_traits>

namespace mcp {
namespace async {

// Task<T> - coroutine-returning type that exposes a std::future<T>
// Usage: Task<T> foo() { co_return value; } -> foo().toFuture()

template <typename T>
class Task {
public:
    using value_type = T;

    struct promise_type {
        std::promise<T> promise;
        Task get_return_object() noexcept { return Task{ promise.get_future() }; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void unhandled_exception() { promise.set_exception(std::current_exception()); }
        template <typename U>
        requires std::convertible_to<U, T>
        void return_value(U&& v) { promise.set_value(std::forward<U>(v)); }
    };

    Task(Task&& other) noexcept : fut(std::move(other.fut)) {}
    Task& operator=(Task&& other) noexcept { fut = std::move(other.fut); return *this; }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    std::future<T> toFuture() { return std::move(fut); }

private:
    explicit Task(std::future<T>&& f) : fut(std::move(f)) {}
    std::future<T> fut;
};

// void specialization

template <>
class Task<void> {
public:
    struct promise_type {
        std::promise<void> promise;
        Task get_return_object() noexcept { return Task{ promise.get_future() }; }
        std::suspend_never initial_suspend() noexcept { return {}; }
        std::suspend_never final_suspend() noexcept { return {}; }
        void unhandled_exception() { promise.set_exception(std::current_exception()); }
        void return_void() { promise.set_value(); }
    };

    Task(Task&& other) noexcept : fut(std::move(other.fut)) {}
    Task& operator=(Task&& other) noexcept { fut = std::move(other.fut); return *this; }
    Task(const Task&) = delete;
    Task& operator=(const Task&) = delete;

    std::future<void> toFuture() { return std::move(fut); }

private:
    explicit Task(std::future<void>&& f) : fut(std::move(f)) {}
    std::future<void> fut;
};

} // namespace async
} // namespace mcp
