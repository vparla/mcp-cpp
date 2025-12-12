//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: SharedMemoryTransport.cpp
// Purpose: Cross-process shared-memory JSON-RPC transport built on Boost.Interprocess message_queue
//==========================================================================================================

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <future>
#include <limits>
#include <memory>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <boost/interprocess/ipc/message_queue.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/SharedMemoryTransport.hpp"
#include "mcp/JsonRpcMessageRouter.h"

namespace mcp {

using namespace boost::interprocess;

class SharedMemoryTransport::Impl {
public:
    SharedMemoryTransport::Options opts;

    std::atomic<bool> connected{false};
    std::unique_ptr<message_queue> sendMq;
    std::unique_ptr<message_queue> recvMq;
    std::jthread processingThread;

    NotificationHandler notificationHandler;
    RequestHandler requestHandler;
    ErrorHandler errorHandler;

    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;

    std::string sessionId;
    std::unique_ptr<IJsonRpcMessageRouter> router;

    Impl(const SharedMemoryTransport::Options& o)
        : opts(o) {
        sessionId = std::string("shm-") + opts.channelName;
        router = MakeDefaultJsonRpcMessageRouter();
    }

    ~Impl() {
        connected = false;
        if (processingThread.joinable()) {
            processingThread.request_stop();
            processingThread.join();
        }
        // Remove queues only if we created them
        if (opts.create) {
            try {
                (void)message_queue::remove((opts.channelName + "_c2s").c_str());
            } catch (...) {
                // ignore
            }
            try {
                (void)message_queue::remove((opts.channelName + "_s2c").c_str());
            } catch (...) {
                // ignore
            }
        }
    }

    void processMessage(const std::string& message) {
        LOG_DEBUG("SharedMemoryTransport processing message: {}", message);
        // First, handle responses (have top-level result or error)
        if (message.find("\"result\"") != std::string::npos || message.find("\"error\"") != std::string::npos) {
            JSONRPCResponse response;
            if (response.Deserialize(message)) {
                handleResponse(std::move(response));
                return;
            }
        }
        // Next, handle requests using router classification (must have method and top-level id)
        if (message.find("\"method\"") != std::string::npos && router &&
            router->classify(message) == IJsonRpcMessageRouter::MessageKind::Request) {
            JSONRPCRequest req;
            if (req.Deserialize(message)) {
                if (requestHandler) {
                    std::thread([this, req = std::move(req)]() mutable {
                        try {
                            auto resp = requestHandler(req);
                            if (!resp) {
                                resp = std::make_unique<JSONRPCResponse>();
                                resp->id = req.id;
                                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Null response from handler", std::nullopt);
                            } else {
                                resp->id = req.id;
                            }
                            sendResponse(*resp);
                        } catch (const std::exception& e) {
                            LOG_ERROR("SharedMemory request handler exception: {}", e.what());
                            auto resp = std::make_unique<JSONRPCResponse>();
                            resp->id = req.id;
                            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                            sendResponse(*resp);
                        }
                    }).detach();
                    return;
                }
            }
        }
        // Otherwise, treat as notification
        {
            JSONRPCNotification note;
            if (note.Deserialize(message)) {
                // Special handling: transport/disconnected from creator side
                if (note.method == std::string("transport/disconnected")) {
                    connected.store(false);
                    // Fail any pending requests immediately
                    {
                        std::lock_guard<std::mutex> lk(requestMutex);
                        for (auto& kv : pendingRequests) {
                            auto resp = std::make_unique<JSONRPCResponse>();
                            resp->id = kv.first;
                            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Peer not connected", std::nullopt);
                            kv.second.set_value(std::move(resp));
                        }
                        pendingRequests.clear();
                    }
                    if (errorHandler) { errorHandler("Peer disconnected"); }
                } else if (notificationHandler) {
                    notificationHandler(std::make_unique<JSONRPCNotification>(std::move(note)));
                }
                return;
            }
        }
    }

    void sendResponse(const JSONRPCResponse& response) {
        const std::string payload = response.Serialize();
        (void)sendPayload(payload);
    }

    bool sendPayload(const std::string& payload) {
        if (!sendMq) {
            if (errorHandler) {
                errorHandler("SharedMemoryTransport: send queue not available");
            }
            return false;
        }
        if (payload.size() > opts.maxMessageSize) {
            if (errorHandler) {
                errorHandler("SharedMemoryTransport: payload too large");
            }
            return false;
        }
        bool ok = false;
        try {
            ok = sendMq->try_send(payload.data(), payload.size(), 0);
            if (!ok) {
                ok = sendMq->timed_send(
                    payload.data(),
                    payload.size(),
                    0,
                    boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(200));
            }
        } catch (const std::exception& e) {
            if (errorHandler) {
                errorHandler(std::string("SharedMemoryTransport send failed: ") + e.what());
            }
            return false;
        }
        if (!ok && errorHandler) {
            errorHandler("SharedMemoryTransport: send failed (queue full or closed)");
        }
        return ok;
    }

    void handleResponse(JSONRPCResponse response) {
        std::string idStr;
        std::visit([&idStr](const auto& id){
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                idStr = id;
            }
            else if constexpr (std::is_same_v<T, int64_t>) {
                idStr = std::to_string(id);
            }
        }, response.id);

        std::lock_guard<std::mutex> lk(requestMutex);
        auto it = pendingRequests.find(idStr);
        if (it != pendingRequests.end()) {
            it->second.set_value(std::make_unique<JSONRPCResponse>(std::move(response)));
            pendingRequests.erase(it);
        }
    }
};

SharedMemoryTransport::SharedMemoryTransport(const Options& opts)
    : pImpl(std::make_unique<Impl>(opts)) {
    FUNC_SCOPE();
}

SharedMemoryTransport::~SharedMemoryTransport() {
    FUNC_SCOPE();
}

std::future<void> SharedMemoryTransport::Start() {
    FUNC_SCOPE();
    try {
        const std::string c2s = pImpl->opts.channelName + "_c2s";
        const std::string s2c = pImpl->opts.channelName + "_s2c";
        if (pImpl->opts.create) {
            // Ensure clean state
            try {
                (void)message_queue::remove(c2s.c_str());
            } catch (...) {
                // ignore
            }
            try {
                (void)message_queue::remove(s2c.c_str());
            } catch (...) {
                // ignore
            }
            pImpl->recvMq = std::make_unique<message_queue>(create_only, c2s.c_str(), pImpl->opts.maxMessageCount, pImpl->opts.maxMessageSize);
            pImpl->sendMq = std::make_unique<message_queue>(create_only, s2c.c_str(), pImpl->opts.maxMessageCount, pImpl->opts.maxMessageSize);
        } else {
            pImpl->recvMq = std::make_unique<message_queue>(open_only, s2c.c_str());
            pImpl->sendMq = std::make_unique<message_queue>(open_only, c2s.c_str());
        }
    } catch (const std::exception& e) {
        std::promise<void> p;
        p.set_exception(std::make_exception_ptr(std::runtime_error(std::string("SharedMemoryTransport start failed: ") + e.what())));
        return p.get_future();
    }

    pImpl->connected.store(true);
    pImpl->processingThread = std::jthread([this](std::stop_token st){
        std::vector<char> buffer;
        buffer.resize(pImpl->opts.maxMessageSize);
        while (pImpl->connected.load() && !st.stop_requested()) {
            std::size_t recvd = 0;
            unsigned int prio = 0;
            bool got = false;
            try {
                got = pImpl->recvMq->timed_receive(
                    buffer.data(),
                    buffer.size(),
                    recvd,
                    prio,
                    boost::posix_time::microsec_clock::universal_time() + boost::posix_time::milliseconds(100));
            } catch (const std::exception& e) {
                if (pImpl->errorHandler) { pImpl->errorHandler(std::string("SharedMemoryTransport receive failed: ") + e.what()); }
                // Treat as disconnect
                pImpl->connected.store(false);
                break;
            }
            if (!got) {
                continue;
            }
            std::string message(buffer.data(), buffer.data() + recvd);
            pImpl->processMessage(message);
        }
        // Epilogue: mark disconnected and fail any pending requests so callers don't hang
        pImpl->connected.store(false);
        {
            std::lock_guard<std::mutex> lk(pImpl->requestMutex);
            for (auto& kv : pImpl->pendingRequests) {
                auto resp = std::make_unique<JSONRPCResponse>();
                resp->id = kv.first;
                resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
                kv.second.set_value(std::move(resp));
            }
            pImpl->pendingRequests.clear();
        }
    });

    std::promise<void> ready;
    ready.set_value();
    return ready.get_future();
}

std::future<void> SharedMemoryTransport::Close() {
    FUNC_SCOPE();
    // If we are the creator side, proactively notify peers before removing queues
    if (pImpl->opts.create && pImpl->sendMq) {
        try {
            JSONRPCNotification note;
            note.method = "transport/disconnected";
            note.params = JSONValue{JSONValue::Object{}};
            const std::string payload = note.Serialize();
            (void)pImpl->sendPayload(payload);
        } catch (...) {
            // ignore
        }
    }
    pImpl->connected.store(false);
    if (pImpl->processingThread.joinable()) {
        try {
            pImpl->processingThread.request_stop();
            pImpl->processingThread.join();
        } catch (...) {
            // ignore
        }
    }
    // If we are the creator side, remove queues so peers observe disconnection
    if (pImpl->opts.create) {
        const std::string c2s = pImpl->opts.channelName + "_c2s";
        const std::string s2c = pImpl->opts.channelName + "_s2c";
        try {
            (void)message_queue::remove(c2s.c_str());
        } catch (...) {
            // ignore
        }
        try {
            (void)message_queue::remove(s2c.c_str());
        } catch (...) {
            // ignore
        }
    }
    // Fail any pending requests
    {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        for (auto& kv : pImpl->pendingRequests) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = kv.first;
            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport closed", std::nullopt);
            kv.second.set_value(std::move(resp));
        }
        pImpl->pendingRequests.clear();
    }
    std::promise<void> done;
    done.set_value();
    return done.get_future();
}

bool SharedMemoryTransport::IsConnected() const { FUNC_SCOPE(); return pImpl->connected.load(); }
std::string SharedMemoryTransport::GetSessionId() const { FUNC_SCOPE(); return pImpl->sessionId; }

std::future<std::unique_ptr<JSONRPCResponse>> SharedMemoryTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    // Early reject if not connected
    if (!pImpl->connected.load()) {
        std::promise<std::unique_ptr<JSONRPCResponse>> prom;
        auto fut = prom.get_future();
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = request->id;
        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Peer not connected", std::nullopt);
        prom.set_value(std::move(resp));
        return fut;
    }
    // Preserve or generate id
    std::string requestId; bool callerSetId = false;
    std::visit([&](auto&& idVal){
        using T = std::decay_t<decltype(idVal)>;
        if constexpr (std::is_same_v<T, std::string>) {
            if (!idVal.empty()) {
                requestId = idVal; callerSetId = true;
            }
        }
        else if constexpr (std::is_same_v<T, int64_t>) {
            requestId = std::to_string(idVal); 
            callerSetId = true;
        }
    }, request->id);
    if (!callerSetId) {
        // Simple request id generator with wrap handling
        static std::atomic<unsigned long long> counter{0ull};
        unsigned long long n = 0ull;
        unsigned long long old = counter.load(std::memory_order_relaxed);
        for (;;) {
            unsigned long long next;
            if (old == std::numeric_limits<unsigned long long>::max()) {
                next = 1ull;
            } else {
                // Safe increment: old < max so this cannot overflow
                next = old + 1ull;
            }
            if (counter.compare_exchange_weak(old, next, std::memory_order_relaxed, std::memory_order_relaxed)) {
                n = next;
                break;
            }
        }
        requestId = std::string("shm-req-") + std::to_string(n);
        request->id = requestId;
    }

    std::promise<std::unique_ptr<JSONRPCResponse>> prom;
    auto fut = prom.get_future();
    {
        std::lock_guard<std::mutex> lk(pImpl->requestMutex);
        pImpl->pendingRequests[requestId] = std::move(prom);
    }

    std::string payload = request->Serialize();
    if (payload.size() > pImpl->opts.maxMessageSize) {
        std::lock_guard<std::mutex> lk(pImpl->requestMutex);
        auto it = pImpl->pendingRequests.find(requestId);
        if (it != pImpl->pendingRequests.end()) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = request->id;
            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Payload too large", std::nullopt);
            it->second.set_value(std::move(resp));
            pImpl->pendingRequests.erase(it);
        }
        return fut;
    }

    bool sent = pImpl->sendPayload(payload);
    if (!sent) {
        std::lock_guard<std::mutex> lk(pImpl->requestMutex);
        auto it = pImpl->pendingRequests.find(requestId);
        if (it != pImpl->pendingRequests.end()) {
            auto resp = std::make_unique<JSONRPCResponse>();
            resp->id = request->id;
            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Send failed", std::nullopt);
            it->second.set_value(std::move(resp));
            pImpl->pendingRequests.erase(it);
        }
    }
    return fut;
}

std::future<void> SharedMemoryTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    std::string payload = notification->Serialize();
    (void)pImpl->sendPayload(payload);
    std::promise<void> p;
    p.set_value();
    return p.get_future();
}

void SharedMemoryTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandler = std::move(handler);
}

void SharedMemoryTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    pImpl->requestHandler = std::move(handler);
}

void SharedMemoryTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

} // namespace mcp

//==========================================================================================================
// SharedMemoryTransportFactory
// Purpose: Create SharedMemoryTransport from config
//   Supported:
//     - "shm://<channelName>?create=true&maxSize=<bytes>&maxCount=<n>"
//     - "<channelName>" (defaults to create=false)
//   Unknown parameters are ignored.
//==========================================================================================================
std::unique_ptr<mcp::ITransport> mcp::SharedMemoryTransportFactory::CreateTransport(const std::string& config) {
    FUNC_SCOPE();
    SharedMemoryTransport::Options opts;

    auto parseBool = [](std::string v) -> bool {
        std::transform(v.begin(), v.end(), v.begin(), [](unsigned char ch){ return static_cast<char>(std::tolower(ch)); });
        return (v == "1" || v == "true" || v == "yes");
    };

    auto parseQuery = [&](const std::string& q){
        std::size_t start = 0;
        while (start < q.size()) {
            std::size_t amp = q.find('&', start);
            if (amp == std::string::npos) {
                amp = q.size();
            }
            std::string kv = q.substr(start, amp - start);
            std::size_t eq = kv.find('=');
            if (eq != std::string::npos) {
                std::string k = kv.substr(0, eq);
                std::string v = kv.substr(eq + 1);
                if (k == "create") {
                    opts.create = parseBool(v);
                } else if (k == "maxSize") {
                    try {
                        unsigned long long sz = std::stoull(v);
                        opts.maxMessageSize = static_cast<std::size_t>(sz);
                    } catch (...) {
                        // ignore
                    }
                } else if (k == "maxCount") {
                    try {
                        unsigned long c = std::stoul(v);
                        opts.maxMessageCount = static_cast<unsigned int>(c);
                    } catch (...) {
                        // ignore
                    }
                }
            }
            start = amp + 1;
        }
    };

    if (!config.empty()) {
        const std::string prefix = "shm://";
        if (config.rfind(prefix, 0) == 0) {
            std::string rest = config.substr(prefix.size());
            std::size_t qpos = rest.find('?');
            if (qpos == std::string::npos) {
                opts.channelName = rest;
            } else {
                opts.channelName = rest.substr(0, qpos);
                std::string qs = rest.substr(qpos + 1);
                parseQuery(qs);
            }
        } else {
            // Allow optional "?<query>" after plain channel name
            std::size_t qpos = config.find('?');
            if (qpos == std::string::npos) {
                opts.channelName = config;
            } else {
                opts.channelName = config.substr(0, qpos);
                std::string qs = config.substr(qpos + 1);
                parseQuery(qs);
            }
        }
    }

    if (opts.channelName.empty()) {
        // Fallback to a default, but note both peers must match to communicate.
        opts.channelName = "mcp-shm";
    }
    return std::make_unique<SharedMemoryTransport>(opts);
}
