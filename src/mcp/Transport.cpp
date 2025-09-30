//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: Transport.cpp
// Purpose: MCP transport layer implementations
//==========================================================================================================

#ifdef _WIN32
#  define NOMINMAX
#  include <windows.h>
#  include <io.h>
#else
#  include <unistd.h>
#  include <fcntl.h>
#  include <errno.h>
#  include <cstring>
#  ifdef __linux__
#    include <sys/epoll.h>
#    include <sys/eventfd.h>
#  else
#    include <poll.h>
#  endif
#endif

#include <iostream>
#include <thread>
#include <atomic>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <random>
#include <optional>
#include <vector>
#include <array>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cstdlib>
#include <limits>


#include "mcp/Transport.h"
#include "mcp/JSONRPCTypes.h"
#include "logging/Logger.h"
#include "env/EnvVars.h"


namespace mcp {

// StdioTransport implementation
class StdioTransport::Impl {
public:
    std::atomic<bool> connected{false};
    std::atomic<bool> readerExited{false};
    std::string sessionId;
    ITransport::NotificationHandler notificationHandler;
    ITransport::RequestHandler requestHandler;
    ITransport::ErrorHandler errorHandler;
    std::thread readerThread;
    std::thread timeoutThread;
    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;
    std::unordered_map<std::string, std::chrono::steady_clock::time_point> requestDeadlines;
    std::atomic<unsigned int> requestCounter{0u};

#ifdef _WIN32
    HANDLE stopEvent{NULL};
#else
#  ifdef __linux__
    int wakeEventFd{-1};
#  else
    int wakePipe[2]{-1, -1};
#  endif
#endif

    static constexpr std::size_t MaxContentLength = 1024 * 1024; // 1 MiB cap
    std::chrono::milliseconds requestTimeout{30000}; // default 30s (configurable)

    Impl() {
        // Generate session ID
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        sessionId = "stdio-" + std::to_string(dis(gen));
        // Env override for timeout
        std::string env = GetEnvOrDefault("MCP_STDIOTRANSPORT_TIMEOUT_MS", "");
        if (!env.empty()) {
            try {
                unsigned long long v = std::stoull(env);
                requestTimeout = std::chrono::milliseconds(static_cast<uint64_t>(v));
            } catch (...) { /* ignore malformed */ }
        }

#ifdef _WIN32
        stopEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!stopEvent) {
            LOG_ERROR("StdioTransport: failed to create stop event (err={})", static_cast<unsigned long>(::GetLastError()));
        }
#else
#  ifdef __linux__
        wakeEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeEventFd < 0) {
            LOG_ERROR("StdioTransport: failed to create eventfd (errno={} msg={})", errno, ::strerror(errno));
        }
#  else
        if (::pipe(wakePipe) != 0) {
            LOG_ERROR("StdioTransport: failed to create self-pipe (errno={} msg={})", errno, ::strerror(errno));
        } else {
            int fl0 = ::fcntl(wakePipe[0], F_GETFL, 0);
            int fl1 = ::fcntl(wakePipe[1], F_GETFL, 0);
            if (fl0 >= 0) {
                if (::fcntl(wakePipe[0], F_SETFL, fl0 | O_NONBLOCK) < 0) {
                    LOG_WARN("StdioTransport: failed to set O_NONBLOCK on wakePipe[0] (errno={} msg={})", errno, ::strerror(errno));
                }
            } else {
                LOG_WARN("StdioTransport: fcntl(F_GETFL) failed for wakePipe[0] (errno={} msg={})", errno, ::strerror(errno));
            }
            if (fl1 >= 0) {
                if (::fcntl(wakePipe[1], F_SETFL, fl1 | O_NONBLOCK) < 0) {
                    LOG_WARN("StdioTransport: failed to set O_NONBLOCK on wakePipe[1] (errno={} msg={})", errno, ::strerror(errno));
                }
            } else {
                LOG_WARN("StdioTransport: fcntl(F_GETFL) failed for wakePipe[1] (errno={} msg={})", errno, ::strerror(errno));
            }
        }
#  endif
#endif
    }

    ~Impl() {
        if (readerThread.joinable()) {
            connected = false;
            if (readerExited.load()) {
                readerThread.join();
            } else {
                // Best-effort: avoid blocking destructor if the thread is stuck in a blocking read
                readerThread.detach();
            }
        }
        if (timeoutThread.joinable()) {
            connected = false;
            timeoutThread.join();
        }

#ifdef _WIN32
        if (stopEvent) { ::CloseHandle(stopEvent); stopEvent = NULL; }
#else
#  ifdef __linux__
        if (wakeEventFd >= 0) { ::close(wakeEventFd); wakeEventFd = -1; }
#  else
        if (wakePipe[0] >= 0) { ::close(wakePipe[0]); wakePipe[0] = -1; }
        if (wakePipe[1] >= 0) { ::close(wakePipe[1]); wakePipe[1] = -1; }
#  endif
#endif
    }

    // Write a framed JSON-RPC message to the given stream using MCP stdio framing.
    void writeFrame(std::ostream& out, const std::string& payload) {
        // Content-Length header followed by CRLF CRLF and the JSON payload
        out << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        out.flush();
    }

    // Read a framed JSON-RPC message from the given stream. Returns empty on EOF/error.
    std::optional<std::string> readFrame(std::istream& in) {
        std::string line;
        std::size_t contentLength = 0;
        bool haveLength = false;

        // Read headers until blank line
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break; // end of headers
            }
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                // trim leading spaces in value
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch){ return !std::isspace(ch); }));
                // lowercase header name for case-insensitive compare
                std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                if (name == "content-length") {
                    try {
                        unsigned long long v64 = std::stoull(value);
                        if (v64 > MaxContentLength || v64 > std::numeric_limits<std::size_t>::max()) {
                            LOG_WARN("Invalid or too large Content-Length header: {}", value);
                            if (errorHandler) {
                                errorHandler("StdioTransport: body too large");
                            }
                            return std::nullopt;
                        }
                        contentLength = static_cast<std::size_t>(v64);
                        haveLength = true;
                    } catch (...) {
                        LOG_WARN("Invalid Content-Length header: {}", value);
                        return std::nullopt;
                    }
                }
            }
        }

        if (!in.good()) {
            return std::nullopt; // EOF or error
        }
        if (!haveLength) {
            LOG_WARN("Missing Content-Length header");
            return std::nullopt;
        }

        if (contentLength > MaxContentLength) {
            LOG_WARN("Content-Length {} exceeds max {}", contentLength, MaxContentLength);
            if (errorHandler) {
                errorHandler("StdioTransport: body too large");
            }
            return std::nullopt;
        }

        std::string body;
        body.resize(contentLength);
        std::size_t total = 0;
        while (total < contentLength) {
            in.read(&body[total], static_cast<std::streamsize>(contentLength - total));
            std::streamsize got = in.gcount();
            if (got <= 0) {
                LOG_WARN("Unexpected EOF while reading body (read {} of {} bytes)", total, contentLength);
                return std::nullopt;
            }
            total += static_cast<std::size_t>(got);
        }
        return body;
    }

    void startReader() {
        readerThread = std::thread([this]() {
            std::string buffer;
            constexpr int waitTimeoutMs = 100; // short, responsive wait

            auto tryExtractFrame = [&](std::string& buf) -> std::optional<std::string> {
                // Find end of headers (CRLF CRLF)
                const std::string sep = "\r\n\r\n";
                std::size_t headerEnd = buf.find(sep);
                if (headerEnd == std::string::npos) {
                    return std::nullopt;
                }

                // Parse headers for Content-Length (case-insensitive)
                std::size_t pos = 0;
                std::size_t contentLength = 0;
                bool haveLength = false;
                while (pos < headerEnd) {
                    std::size_t eol = buf.find("\r\n", pos);
                    if (eol == std::string::npos || eol > headerEnd) {
                        break;
                    }
                    std::string line = buf.substr(pos, eol - pos);
                    // Lowercase header name portion up to ':'
                    auto colon = line.find(':');
                    if (colon != std::string::npos) {
                        std::string name = line.substr(0, colon);
                        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c){ return static_cast<char>(std::tolower(c)); });
                        std::string value = line.substr(colon + 1);
                        value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch){ return !std::isspace(ch); }));
                        if (name == "content-length") {
                            try {
                                unsigned long long v64 = std::stoull(value);
                                if (v64 > MaxContentLength || v64 > std::numeric_limits<std::size_t>::max()) {
                                    LOG_WARN("Content-Length {} exceeds limits (max={})", v64, MaxContentLength);
                                    if (errorHandler) {
                                        errorHandler("StdioTransport: body too large");
                                    }
                                    // Drop these headers and continue reading fresh
                                    buf.erase(0, headerEnd + sep.size());
                                    return std::nullopt;
                                }
                                contentLength = static_cast<std::size_t>(v64);
                                haveLength = true;
                            } catch (...) {
                                LOG_WARN("Invalid Content-Length header: {}", value);
                                // Drop bad headers and continue
                            }
                        }
                    }
                    pos = eol + 2;
                }

                if (!haveLength) {
                    LOG_WARN("Missing Content-Length header (dropping headers)");
                    buf.erase(0, headerEnd + sep.size());
                    return std::nullopt;
                }

                const std::size_t headerAndSep = headerEnd + sep.size();
                if (contentLength > std::numeric_limits<std::size_t>::max() - headerAndSep) {
                    LOG_WARN("Frame size overflow detected (header={}, len={})", headerAndSep, contentLength);
                    // Drop headers to resynchronize
                    buf.erase(0, headerEnd + sep.size());
                    return std::nullopt;
                }
                std::size_t frameTotal = headerAndSep + contentLength;
                if (buf.size() < frameTotal) {
                    return std::nullopt; // need more data
                }
                std::string payload = buf.substr(headerEnd + sep.size(), contentLength);
                buf.erase(0, frameTotal);
                return payload;
            };

#ifndef _WIN32
            // POSIX: set stdin non-blocking to avoid any accidental blocking reads
            int fd = STDIN_FILENO;
            int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags >= 0) {
                if (::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
                    LOG_WARN("StdioTransport: failed to set O_NONBLOCK on stdin (errno={} msg={})", errno, ::strerror(errno));
                }
            } else {
                LOG_WARN("StdioTransport: fcntl(F_GETFL) failed for stdin (errno={} msg={})", errno, ::strerror(errno));
            }
#endif

            while (connected) {
                // Platform-specific readiness wait and read
#ifdef _WIN32
                ::HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
                if (!hIn || hIn == INVALID_HANDLE_VALUE) {
                    if (errorHandler) {
                        errorHandler("StdioTransport: invalid STDIN handle");
                    }
                    break;
                }
                DWORD fileType = ::GetFileType(hIn);
                std::vector<char> tmp(4096);
                DWORD bytesRead = 0;
                bool hadData = false;

                if (fileType == FILE_TYPE_PIPE) {
                    DWORD available = 0;
                    if (!::PeekNamedPipe(hIn, NULL, 0, NULL, &available, NULL)) {
                        DWORD err = ::GetLastError();
                        LOG_ERROR("StdioTransport: PeekNamedPipe failed (err={})", static_cast<unsigned long>(err));
                        if (errorHandler) {
                            errorHandler("StdioTransport: PeekNamedPipe failed");
                        }
                        break;
                    }
                    if (available == 0) {
                        ::HANDLE handles[2] = { stopEvent, hIn };
                        DWORD wr = ::WaitForMultipleObjects(2, handles, FALSE, waitTimeoutMs);
                        if (wr == WAIT_TIMEOUT) {
                            // no data yet
                        } else if (wr == (WAIT_OBJECT_0 + 1)) {
                            // ready, re-peek
                            (void)::PeekNamedPipe(hIn, NULL, 0, NULL, &available, NULL);
                        } else if (wr == WAIT_OBJECT_0) {
                            // stop event
                            break;
                        } else {
                            DWORD err = ::GetLastError();
                            LOG_ERROR("StdioTransport: WaitForMultipleObjects failed (err={})", static_cast<unsigned long>(err));
                            if (errorHandler) {
                                errorHandler("StdioTransport: WaitForMultipleObjects failed");
                            }
                            break;
                        }
                    }
                    if (available > 0) {
                        DWORD toRead = available;
                        if (toRead > static_cast<DWORD>(tmp.size())) {
                            toRead = static_cast<DWORD>(tmp.size());
                        }
                        if (!::ReadFile(hIn, tmp.data(), toRead, &bytesRead, NULL)) {
                            DWORD err = ::GetLastError();
                            if (err == ERROR_BROKEN_PIPE) {
                                LOG_INFO("StdioTransport: EOF on pipe");
                                if (errorHandler) {
                                    errorHandler("StdioTransport: EOF on pipe");
                                }
                            } else {
                                LOG_ERROR("StdioTransport: ReadFile failed (err={})", static_cast<unsigned long>(err));
                                if (errorHandler) {
                                    errorHandler("StdioTransport: ReadFile failed");
                                }
                            }
                            break;
                        }
                        if (bytesRead == 0) {
                            LOG_INFO("StdioTransport: EOF on pipe");
                            if (errorHandler) {
                                errorHandler("StdioTransport: EOF on pipe");
                            }
                            break;
                        }
                        buffer.append(tmp.data(), tmp.data() + bytesRead);
                        hadData = true;
                    }
                } else {
                    ::HANDLE handles[2] = { stopEvent, hIn };
                    DWORD wr = ::WaitForMultipleObjects(2, handles, FALSE, waitTimeoutMs);
                    if (wr == (WAIT_OBJECT_0 + 1)) {
                        if (!::ReadFile(hIn, tmp.data(), static_cast<DWORD>(tmp.size()), &bytesRead, NULL)) {
                            DWORD err = ::GetLastError();
                            if (err == ERROR_BROKEN_PIPE) {
                                LOG_INFO("StdioTransport: EOF on stdin");
                                if (errorHandler) {
                                    errorHandler("StdioTransport: EOF on stdin");
                                }
                            } else {
                                LOG_ERROR("StdioTransport: ReadFile failed (err={})", static_cast<unsigned long>(err));
                                if (errorHandler) {
                                    errorHandler("StdioTransport: ReadFile failed");
                                }
                            }
                            break;
                        }
                        if (bytesRead == 0) {
                            LOG_INFO("StdioTransport: EOF on stdin");
                            if (errorHandler) {
                                errorHandler("StdioTransport: EOF on stdin");
                            }
                            break;
                        }
                        buffer.append(tmp.data(), tmp.data() + bytesRead);
                        hadData = true;
                    } else if (wr == WAIT_OBJECT_0) {
                        // stop event signaled
                        break;
                    } else if (wr != WAIT_TIMEOUT) {
                        DWORD err = ::GetLastError();
                        LOG_ERROR("StdioTransport: WaitForMultipleObjects failed (err={})", static_cast<unsigned long>(err));
                        if (errorHandler) {
                            errorHandler("StdioTransport: WaitForMultipleObjects failed");
                        }
                        break;
                    } // else timeout, just loop
                }
#else // POSIX
                bool hadData = false;
                std::vector<char> tmp(4096);
                ssize_t n = -1;
    #ifdef __linux__
                int ep = ::epoll_create1(EPOLL_CLOEXEC);
                if (ep >= 0) {
                    epoll_event evIn{}; evIn.events = EPOLLIN | EPOLLRDHUP | EPOLLERR; evIn.data.fd = fd;
                    (void)::epoll_ctl(ep, EPOLL_CTL_ADD, fd, &evIn);
                    int wfd = wakeEventFd;
                    if (wfd >= 0) {
                        epoll_event evWake{}; evWake.events = EPOLLIN; evWake.data.fd = wfd;
                        (void)::epoll_ctl(ep, EPOLL_CTL_ADD, wfd, &evWake);
                    }
                    epoll_event events[2];
                    const size_t eventsCapacity = sizeof(events) / sizeof(events[0]);
                    int rc = ::epoll_wait(ep, events, eventsCapacity, waitTimeoutMs);
                    ::close(ep);
                    if (rc < 0) {
                        if (errno == EINTR) {
                            // interrupted by signal; try again
                            continue;
                        }
                        LOG_ERROR("StdioTransport: epoll_wait failed (errno={} msg={})", errno, ::strerror(errno));
                        if (errorHandler) {
                            errorHandler("StdioTransport: epoll_wait failed");
                        }
                        break;
                    }
                    if (rc == 0) {
                        // timeout, no data
                    } else {
                        bool woke = false;
                        const size_t limit = (rc < eventsCapacity) ? rc : eventsCapacity;
                        for (size_t i = 0; i < limit; ++i) {
                            auto &ev = events[i];
                            if (ev.data.fd == fd) {
                                if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                                    LOG_INFO("StdioTransport: stdin closed (epoll flags={})", static_cast<unsigned int>(ev.events));
                                    if (errorHandler) {
                                        errorHandler("StdioTransport: stdin closed");
                                    }
                                    woke = true; // forces break
                                } else if (ev.events & EPOLLIN) {
                                    n = ::read(fd, tmp.data(), tmp.size());
                                }
                            } else {
                                // wake event
                                woke = true;
                                uint64_t v = 0;
                                ssize_t r;
                                do {
                                    r = ::read(ev.data.fd, &v, sizeof(v));
                                } while (r < 0 && errno == EINTR);
                                if (r < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                                    LOG_WARN("StdioTransport: wake event read failed (errno={} msg={})", errno, ::strerror(errno));
                                }
                            }
                        }
                        if (woke && !connected) {
                            break;
                        }
                    }
                } else {
                    // Fallback: small sleep to avoid busy loop if epoll_create1 fails
                    std::this_thread::sleep_for(std::chrono::milliseconds(waitTimeoutMs));
                }
    #else
                int wfd = (wakePipe[0] >= 0) ? wakePipe[0] : -1;
                struct pollfd pfds[2];
                pfds[0].fd = fd; pfds[0].events = POLLIN | POLLERR | POLLHUP; pfds[0].revents = 0;
                int nfds = 1;
                if (wfd >= 0) { pfds[1].fd = wfd; pfds[1].events = POLLIN; pfds[1].revents = 0; nfds = 2; }
                int rc = ::poll(pfds, nfds, waitTimeoutMs);
                if (rc < 0) {
                    if (errno == EINTR) {
                        // interrupted by signal; try again
                        continue;
                    }
                    LOG_ERROR("StdioTransport: poll failed (errno={} msg={})", errno, ::strerror(errno));
                    if (errorHandler) {
                        errorHandler("StdioTransport: poll failed");
                    }
                    break;
                }
                if (rc > 0) {
                    if (nfds == 2 && (pfds[1].revents & POLLIN)) {
                        // wake: drain the self-pipe
                        std::array<char, 64> b{};
                        while (true) {
                            ssize_t r;
                            do {
                                r = ::read(pfds[1].fd, b.data(), b.size());
                            } while (r < 0 && errno == EINTR);
                            if (r > 0) {
                                // continue draining until EAGAIN
                                continue;
                            }
                            if (r == 0) {
                                break;
                            }
                            if (r < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) {
                                    break;
                                }
                                LOG_WARN("StdioTransport: wake pipe read failed (errno={} msg={})", errno, ::strerror(errno));
                                break;
                            }
                        }
                        break;
                    }
                    if (pfds[0].revents & (POLLERR | POLLHUP)) {
                        LOG_INFO("StdioTransport: stdin closed (poll revents={})", static_cast<unsigned int>(pfds[0].revents));
                        if (errorHandler) {
                            errorHandler("StdioTransport: stdin closed");
                        }
                        break;
                    }
                    n = ::read(fd, tmp.data(), tmp.size());
                }
    #endif
                if (n > 0) {
                    buffer.append(tmp.data(), tmp.data() + n);
                    hadData = true;
                } else if (n == 0) {
                    LOG_INFO("StdioTransport: EOF on stdin");
                    if (errorHandler) {
                        errorHandler("StdioTransport: EOF on stdin");
                    }
                    break;
                } else if (n < 0) {
                    if (errno != 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) {
                        LOG_ERROR("StdioTransport: read error (errno={} msg={})", errno, ::strerror(errno));
                        if (errorHandler) {
                            errorHandler("StdioTransport: read error");
                        }
                        break;
                    }
                }
#endif

                // Process any complete frames in buffer
                if (hadData) {
                    while (connected) {
                        auto framed = tryExtractFrame(buffer);
                        if (!framed.has_value()) {
                            break;
                        }
                        processMessage(framed.value());
                    }
                }
            }
            connected = false;
            readerExited.store(true);
        });
    }

    void startTimeouts() {
        timeoutThread = std::thread([this]() {
            using clock = std::chrono::steady_clock;
            while (connected) {
                std::vector<std::string> expired;
                auto now = clock::now();
                {
                    std::lock_guard<std::mutex> lock(requestMutex);
                    for (const auto& kv : requestDeadlines) {
                        if (kv.second <= now) {
                            expired.push_back(kv.first);
                        }
                    }
                    for (const auto& idStr : expired) {
                        auto it = pendingRequests.find(idStr);
                        if (it != pendingRequests.end()) {
                            auto resp = std::make_unique<JSONRPCResponse>();
                            resp->id = idStr;
                            resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Request timeout", std::nullopt);
                            it->second.set_value(std::move(resp));
                            pendingRequests.erase(it);
                        }
                        requestDeadlines.erase(idStr);
                    }
                }
                std::this_thread::sleep_for(std::chrono::milliseconds(50));
            }
        });
    }

    void processMessage(const std::string& message) {
        LOG_DEBUG("Received message: {}", message);
        
        // Try to parse as request (server side) when both method and id are present
        JSONRPCRequest request;
        if (message.find("\"method\"") != std::string::npos && message.find("\"id\"") != std::string::npos && request.Deserialize(message)) {
            if (requestHandler) {
                try {
                    auto resp = requestHandler(request);
                    if (!resp) {
                        resp = std::make_unique<JSONRPCResponse>();
                        resp->id = request.id;
                        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Null response from handler", std::nullopt);
                    } else {
                        // Ensure response has matching id
                        resp->id = request.id;
                    }
                    const std::string payload = resp->Serialize();
                    writeFrame(std::cout, payload);
                } catch (const std::exception& e) {
                    LOG_ERROR("Request handler exception: {}", e.what());
                    auto resp = std::make_unique<JSONRPCResponse>();
                    resp->id = request.id;
                    resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                    writeFrame(std::cout, resp->Serialize());
                }
                return;
            }
        }
        
        // Try to parse as response afterwards
        JSONRPCResponse response;
        if (response.Deserialize(message)) {
            handleResponse(std::move(response));
            return;
        }
        
        LOG_WARN("Failed to parse message: {}", message);
    }

    void handleResponse(JSONRPCResponse response) {
        std::string idStr;
        std::visit([&idStr](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                idStr = id;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                idStr = std::to_string(id);
            }
        }, response.id);

        std::lock_guard<std::mutex> lock(requestMutex);
        auto it = pendingRequests.find(idStr);
        if (it != pendingRequests.end()) {
            it->second.set_value(std::make_unique<JSONRPCResponse>(std::move(response)));
            pendingRequests.erase(it);
        }
        requestDeadlines.erase(idStr);
    }

    void handleNotification(JSONRPCNotification notification) {
        if (notificationHandler) {
            notificationHandler(std::make_unique<JSONRPCNotification>(std::move(notification)));
        }
    }

    std::string generateRequestId() {
        return "req-" + std::to_string(++requestCounter);
    }
};

StdioTransport::StdioTransport() : pImpl(std::make_unique<Impl>()) { FUNC_SCOPE(); }
StdioTransport::~StdioTransport() { FUNC_SCOPE(); }

std::future<void> StdioTransport::Start() {
    FUNC_SCOPE();
    LOG_INFO("Starting StdioTransport");
    pImpl->connected = true;
    pImpl->startReader();
    pImpl->startTimeouts();
    
    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
}

std::future<void> StdioTransport::Close() {
    FUNC_SCOPE();
    LOG_INFO("Closing StdioTransport");
    pImpl->connected = false;

    // Wake reader thread immediately to avoid detach
#ifdef _WIN32
    if (pImpl->stopEvent) { ::SetEvent(pImpl->stopEvent); }
#else
#  ifdef __linux__
    if (pImpl->wakeEventFd >= 0) {
        uint64_t one = 1;
        for (;;) {
            ssize_t w = ::write(pImpl->wakeEventFd, &one, sizeof(one));
            if (w >= 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // already signaled
            }
            LOG_WARN("StdioTransport: eventfd write failed (errno={} msg={})", errno, ::strerror(errno));
            break;
        }
    }
#  else
    if (pImpl->wakePipe[1] >= 0) {
        char b = 'x';
        for (;;) {
            ssize_t w = ::write(pImpl->wakePipe[1], &b, 1);
            if (w >= 0) {
                break;
            }
            if (errno == EINTR) {
                continue;
            }
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                break; // pipe full, reader will see POLLIN
            }
            LOG_WARN("StdioTransport: wake pipe write failed (errno={} msg={})", errno, ::strerror(errno));
            break;
        }
    }
#  endif
#endif

#ifdef _WIN32
    // Close STDIN handle to unblock reader thread
    ::HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    if (hIn && hIn != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hIn);
    }
    // Also close STDOUT to prevent any further writes from blocking
    ::HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hOut);
    }
#else
    // Close stdin file descriptor to unblock reader thread
    ::close(STDIN_FILENO);
    // Also close stdout to prevent any further writes from blocking
    ::close(STDOUT_FILENO);
#endif

    if (pImpl->readerThread.joinable()) {
        // Give the reader a short grace period to exit after wake
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!pImpl->readerExited.load()) {
            if (std::chrono::steady_clock::now() >= deadline) {
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pImpl->readerExited.load()) {
            pImpl->readerThread.join();
        } else {
            LOG_WARN("StdioTransport: reader thread appears blocked; detaching to avoid hang");
            pImpl->readerThread.detach();
        }
    }
    if (pImpl->timeoutThread.joinable()) {
        pImpl->timeoutThread.join();
    }

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
        pImpl->requestDeadlines.clear();
    }

    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
}

bool StdioTransport::IsConnected() const {
    FUNC_SCOPE();
    bool val = pImpl->connected;
    return val;
}

std::string StdioTransport::GetSessionId() const {
    FUNC_SCOPE();
    auto id = pImpl->sessionId;
    return id;
}

std::future<std::unique_ptr<JSONRPCResponse>> StdioTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    if (!pImpl->connected.load()) {
        LOG_DEBUG("StdioTransport: SendRequest called while disconnected; returning error");
        std::promise<std::unique_ptr<JSONRPCResponse>> promise;
        auto fut = promise.get_future();
        auto resp = std::make_unique<JSONRPCResponse>();
        // Generate a request id to echo back in the error response
        resp->id = pImpl->generateRequestId();
        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport not connected", std::nullopt);
        promise.set_value(std::move(resp));
        return fut;
    }
    std::string requestId = pImpl->generateRequestId();
    request->id = requestId;
    
    std::promise<std::unique_ptr<JSONRPCResponse>> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        pImpl->pendingRequests[requestId] = std::move(promise);
        pImpl->requestDeadlines[requestId] = std::chrono::steady_clock::now() + pImpl->requestTimeout;
    }
    
    std::string serialized = request->Serialize();
    LOG_DEBUG("Sending framed request ({} bytes)", serialized.size());
    pImpl->writeFrame(std::cout, serialized);
    return future;
}

std::future<void> StdioTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    if (!pImpl->connected.load()) {
        LOG_DEBUG("StdioTransport: SendNotification called while disconnected; ignoring");
        std::promise<void> ready;
        ready.set_value();
        return ready.get_future();
    }
    std::string serialized = notification->Serialize();
    LOG_DEBUG("Sending framed notification ({} bytes)", serialized.size());
    pImpl->writeFrame(std::cout, serialized);
    
    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
}

void StdioTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandler = std::move(handler);
}

void StdioTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    pImpl->requestHandler = std::move(handler);
}

void StdioTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

void StdioTransport::SetRequestTimeoutMs(uint64_t timeoutMs) {
    FUNC_SCOPE();
    if (timeoutMs == 0) {
        // Zero disables timeouts; use a very large duration
        pImpl->requestTimeout = std::chrono::milliseconds::max();
    } else {
        pImpl->requestTimeout = std::chrono::milliseconds(timeoutMs);
    }
}

// InMemoryTransport implementation
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
    std::thread processingThread;
    std::atomic<int> requestCounter{0};
    std::mutex requestMutex;
    std::unordered_map<std::string, std::promise<std::unique_ptr<JSONRPCResponse>>> pendingRequests;

    Impl() {
        std::random_device rd;
        std::mt19937 gen(rd());
        std::uniform_int_distribution<> dis(1000, 9999);
        sessionId = "memory-" + std::to_string(dis(gen));
    }

    ~Impl() {
        connected = false;
        queueCondition.notify_all();
        if (processingThread.joinable()) {
            processingThread.join();
        }
    }

    void startProcessing() {
        processingThread = std::thread([this]() {
            while (connected) {
                std::unique_lock<std::mutex> lock(queueMutex);
                queueCondition.wait(lock, [this]() { return !messageQueue.empty() || !connected; });
                
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

        // Try to parse as request first (must have method + id)
        JSONRPCRequest request;
        if (message.find("\"method\"") != std::string::npos && message.find("\"id\"") != std::string::npos && request.Deserialize(message)) {
            if (requestHandler) {
                try {
                    auto resp = requestHandler(request);
                    if (!resp) {
                        resp = std::make_unique<JSONRPCResponse>();
                        resp->id = request.id;
                        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Null response from handler", std::nullopt);
                    } else {
                        // Ensure response id matches request id
                        resp->id = request.id;
                    }
                    sendToPeer(resp->Serialize());
                } catch (const std::exception& e) {
                    LOG_ERROR("InMemory request handler exception: {}", e.what());
                    auto resp = std::make_unique<JSONRPCResponse>();
                    resp->id = request.id;
                    resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                    sendToPeer(resp->Serialize());
                }
                return;
            }
        }

        // Try to parse as response next (should contain result or error)
        if (message.find("\"result\"") != std::string::npos || message.find("\"error\"") != std::string::npos) {
            JSONRPCResponse response;
            if (response.Deserialize(message)) {
                handleResponse(std::move(response));
                return;
            }
        }

        // Try to parse as notification (method present, no id expected)
        JSONRPCNotification notification;
        if (notification.Deserialize(message)) {
            handleNotification(std::move(notification));
            return;
        }
    }

    void handleResponse(JSONRPCResponse response) {
        std::string idStr;
        std::visit([&idStr](const auto& id) {
            using T = std::decay_t<decltype(id)>;
            if constexpr (std::is_same_v<T, std::string>) {
                idStr = id;
            } else if constexpr (std::is_same_v<T, int64_t>) {
                idStr = std::to_string(id);
            }
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

    std::string generateRequestId() {
        return "mem-req-" + std::to_string(++requestCounter);
    }
};

InMemoryTransport::InMemoryTransport() : pImpl(std::make_unique<Impl>()) { FUNC_SCOPE(); }
InMemoryTransport::~InMemoryTransport() { FUNC_SCOPE(); }

std::pair<std::unique_ptr<InMemoryTransport>, std::unique_ptr<InMemoryTransport>> 
InMemoryTransport::CreatePair() {
    FUNC_SCOPE();
    auto transport1 = std::make_unique<InMemoryTransport>();
    auto transport2 = std::make_unique<InMemoryTransport>();
    // Wire peers
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
    
    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
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
    
    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
}

bool InMemoryTransport::IsConnected() const {
    FUNC_SCOPE();
    bool val = pImpl->connected;
    return val;
}

std::string InMemoryTransport::GetSessionId() const {
    FUNC_SCOPE();
    auto id = pImpl->sessionId;
    return id;
}

std::future<std::unique_ptr<JSONRPCResponse>> InMemoryTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    std::string requestId = pImpl->generateRequestId();
    request->id = requestId;
    
    std::promise<std::unique_ptr<JSONRPCResponse>> promise;
    auto future = promise.get_future();
    
    {
        std::lock_guard<std::mutex> lock(pImpl->requestMutex);
        pImpl->pendingRequests[requestId] = std::move(promise);
    }
    
    std::string serialized = request->Serialize();
    LOG_DEBUG("Sending in-memory request: {}", serialized);
    if (!pImpl->sendToPeer(serialized)) {
        // Fail pending request immediately
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
    
    std::promise<void> promise;
    promise.set_value();
    auto fut = promise.get_future();
    return fut;
}

void InMemoryTransport::SetNotificationHandler(NotificationHandler handler) {
    FUNC_SCOPE();
    pImpl->notificationHandler = std::move(handler);
}

void InMemoryTransport::SetRequestHandler(RequestHandler handler) {
    FUNC_SCOPE();
    pImpl->requestHandler = std::move(handler);
}

void InMemoryTransport::SetErrorHandler(ErrorHandler handler) {
    FUNC_SCOPE();
    pImpl->errorHandler = std::move(handler);
}

// Transport factory implementations
std::unique_ptr<ITransport> StdioTransportFactory::CreateTransport(const std::string& config) {
    auto t = std::make_unique<StdioTransport>();
    // Optional config format: "timeout_ms=<number>" (additional keys can be added later)
    auto pos = config.find("timeout_ms=");
    if (pos != std::string::npos) {
        pos += std::string("timeout_ms=").size();
        std::size_t end = pos;
        while (end < config.size() && config[end] >= '0' && config[end] <= '9') {
            ++end;
        }
        if (end > pos) {
            const std::string num = config.substr(pos, end - pos);
            try {
                uint64_t ms = static_cast<uint64_t>(std::stoull(num));
                t->SetRequestTimeoutMs(ms);
            } catch (...) {
                // ignore malformed value; keep default or env override
            }
        }
    }
    return t;
}

std::unique_ptr<ITransport> InMemoryTransportFactory::CreateTransport(const std::string& /*config*/) {
    return std::make_unique<InMemoryTransport>();
}

} // namespace mcp
