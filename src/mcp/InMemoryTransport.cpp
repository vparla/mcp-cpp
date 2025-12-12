//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: InMemoryTransport.cpp
// Purpose: In-memory transport implementation
//==========================================================================================================

#include <atomic>
#include <condition_variable>
#include <future>
#include <mutex>
#include <queue>
#include <random>
#include <stop_token>
#include <string>
#include <thread>
#include <unordered_map>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/JsonRpcMessageRouter.h"

namespace mcp {

class InMemoryTransport::Impl {
public:
    std::atomic<bool> connected{false};
    std::string sessionId;
    ITransport::NotificationHandler notificationHandler;
    ITransport::RequestHandler requestHandler;
    ITransport::ErrorHandler errorHandler;
    InMemoryTransport::Impl* peer = nullptr;
    std::mutex peerMutex;
    std::queue<std::string> messageQueue;
    std::mutex queueMutex;
    std::condition_variable queueCondition;
    std::jthread processingThread;
    std::atomic<unsigned int> requestCounter{0u};
    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;
    std::unique_ptr<IJsonRpcMessageRouter> router;

    Impl() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        sessionId = "memory-" + std::to_string(dis(gen));
        router = MakeDefaultJsonRpcMessageRouter();
    }

    ~Impl() {
        connected = false;
        queueCondition.notify_all();
        if (processingThread.joinable()) {
            processingThread.request_stop();
            processingThread.join();
        }
    }

    void startProcessing() {
        processingThread = std::jthread([this](std::stop_token st) {
            while (connected && !st.stop_requested()) {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCondition.wait(lock, [this, &st]() { return !messageQueue.empty() || !connected || st.stop_requested(); });
                if (st.stop_requested()) {
                    break;
                }
                while (!messageQueue.empty() && connected) {
                    std::string message = messageQueue.front();
                    messageQueue.pop();
                    lock.unlock();
                    processMessage(message);
                    lock.lock();
                }
            }
        });
    }

    void processMessage(const std::string& message) {
        LOG_DEBUG("Processing in-memory message: {}", message);

        // Classify the message using the router to avoid false positives on nested keys/values.
        auto kind = router ? router->classify(message) : IJsonRpcMessageRouter::MessageKind::Unknown;

        // Handle responses (top-level result or error)
        if (kind == IJsonRpcMessageRouter::MessageKind::Response) {
            JSONRPCResponse response;
            if (response.Deserialize(message)) {
                handleResponse(std::move(response));
                return;
            }
        }

        // Handle requests (must have method and top-level id)
        if (kind == IJsonRpcMessageRouter::MessageKind::Request) {
            JSONRPCRequest request;
            if (request.Deserialize(message)) {
                if (requestHandler) {
                    // Handle each request on a separate thread so notifications (e.g., cancellation) can be
                    // processed concurrently while a long-running request is executing.
                    std::thread([this, req = request]() mutable {
                        try {
                            auto resp = requestHandler(req);
                            if (!resp) {
                                resp = std::make_unique<JSONRPCResponse>();
                                resp->id = req.id;
                                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Null response from handler", std::nullopt);
                            } else {
                                resp->id = req.id;
                            }
                            sendToPeer(resp->Serialize());
                        } catch (const std::exception& e) {
                            LOG_ERROR("InMemory request handler exception: {}", e.what());
                            auto resp = std::make_unique<JSONRPCResponse>();
                            resp->id = req.id;
                            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                            sendToPeer(resp->Serialize());
                        }
                    }).detach();
                    return;
                }
            }
        }

        // Finally, notification
        {
            JSONRPCNotification notification;
            if (notification.Deserialize(message)) {
                handleNotification(std::move(notification));
                return;
            }
        }
    }

    void handleResponse(JSONRPCResponse response) {
        std::string idStr;
        std::visit([&idStr](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) { idStr = id; }
            else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(id); }
        }, response.id);

        std::lock_guard<std::mutex> lock(requestMutex);
        auto it = pendingRequests.find(idStr);
        if (it != pendingRequests.end()) {
            it->second.set_value(std::make_unique<JSONRPCResponse>(std::move(response)));
            pendingRequests.erase(it);
        }
    }

    void handleNotification(JSONRPCNotification notification) {
        if (notificationHandler) {
            notificationHandler(std::make_unique<JSONRPCNotification>(std::move(notification)));
        }
    }

    void enqueueMessage(const std::string& message) {
        std::lock_guard<std::mutex> lock(queueMutex);
        messageQueue.push(message);
        queueCondition.notify_one();
    }

    bool sendToPeer(const std::string& message) {
        std::lock_guard<std::mutex> lock(peerMutex);
        if (!peer || !peer->connected.load()) {
            LOG_WARN("InMemoryTransport: peer not connected; dropping message");
            return false;
        }
        peer->enqueueMessage(message);
        return true;
    }

    std::string generateRequestId() { return "mem-req-" + std::to_string(++requestCounter); }
};

InMemoryTransport::InMemoryTransport() : pImpl(std::make_unique<Impl>()) { FUNC_SCOPE(); }
InMemoryTransport::~InMemoryTransport() { FUNC_SCOPE(); }

std::pair<std::unique_ptr<InMemoryTransport>, std::unique_ptr<InMemoryTransport>> InMemoryTransport::CreatePair() {
    FUNC_SCOPE();
    auto transport1 = std::make_unique<InMemoryTransport>();
    auto transport2 = std::make_unique<InMemoryTransport>();
    transport1->pImpl->peer = transport2->pImpl.get();
    transport2->pImpl->peer = transport1->pImpl.get();
    auto ret = std::make_pair(std::move(transport1), std::move(transport2));
    return ret;
}

std::future<void> InMemoryTransport::Start() {
    FUNC_SCOPE();
    LOG_INFO("Starting InMemoryTransport");
    pImpl->connected = true;
    pImpl->startProcessing();
    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

std::future<void> InMemoryTransport::Close() {
    FUNC_SCOPE();
    LOG_INFO("Closing InMemoryTransport");
    pImpl->connected = false;
    pImpl->queueCondition.notify_all();
    // Fail any pending requests
    {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        for (auto& [idStr, prom] : pImpl->pendingRequests) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = idStr;
            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
            prom.set_value(std::move(resp));
        }
        pImpl->pendingRequests.clear();
    }
    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

bool InMemoryTransport::IsConnected() const { FUNC_SCOPE(); return pImpl->connected; }
std::string InMemoryTransport::GetSessionId() const { FUNC_SCOPE(); return pImpl->sessionId; }

std::future<std::unique_ptr<JSONRPCResponse>> InMemoryTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    // Preserve caller-provided id if set (string non-empty or int64); otherwise generate one.
    std::string requestId;
    bool callerSetId = false;
    std::visit([&](auto&& idVal) {
        using T = std::decay_t<decltype(idVal)>;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!idVal.empty()) { requestId = idVal; callerSetId = true; }
        } else if constexpr (std::is_same_v<T, int64_t>) {
            requestId = std::to_string(idVal);
            callerSetId = true;
        }
    }, request->id);
    if (!callerSetId) {
        requestId = pImpl->generateRequestId();
        request->id = requestId;
    }
    std::promise<std::unique_ptr<JSONRPCResponse>> promise;
    auto future = promise.get_future();
    {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        pImpl->pendingRequests[requestId] = std::move(promise);
    }
    std::string serialized = request->Serialize();
    LOG_DEBUG("Sending in-memory request: {}", serialized);
    if (!pImpl->sendToPeer(serialized)) {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        auto it = pImpl->pendingRequests.find(requestId);
        if (it != pImpl->pendingRequests.end()) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = request->id;
            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Peer not connected", std::nullopt);
            it->second.set_value(std::move(resp));
            pImpl->pendingRequests.erase(it);
        }
    }
    return future;
}

std::future<void> InMemoryTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    std::string serialized = notification->Serialize();
    LOG_DEBUG("Sending in-memory notification: {}", serialized);
    if (!pImpl->sendToPeer(serialized)) {
        if (pImpl->errorHandler) {
            pImpl->errorHandler("Peer not connected");
        }
    }
    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

void InMemoryTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandler = std::move(handler);
}

void InMemoryTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

void InMemoryTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    pImpl->requestHandler = std::move(handler);
}

std::unique_ptr<ITransport> InMemoryTransportFactory::CreateTransport(const std::string& /*config*/) {
    return std::make_unique<InMemoryTransport>();
}

} // namespace mcp
