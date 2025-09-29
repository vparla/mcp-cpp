//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: StdioTransport.cpp
// Purpose: stdio-based transport implementation
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

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <chrono>
#include <condition_variable>
#include <cstdlib>
#include <deque>
#include <future>
#include <iostream>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>
#include <random>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include "logging/Logger.h"
#include "mcp/JSONRPCTypes.h"
#include "mcp/StdioTransport.hpp"
#include "env/EnvVars.h"

namespace mcp {

class StdioTransport::Impl {
public:
    std::atomic<bool> connected{false};
    std::atomic<bool> readerExited{false};
    std::atomic<bool> writerExited{false};
    std::string sessionId;
    ITransport::NotificationHandler notificationHandler;
    ITransport::RequestHandler requestHandler;
    ITransport::ErrorHandler errorHandler;
    std::thread readerThread;
    std::thread timeoutThread;
    std::thread writerThread;
    std::mutex requestMutex;
    std::mutex writeMutex; // protects writeQueue and queuedBytes
    std::condition_variable cvWrite;
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
    std::chrono::milliseconds idleReadTimeout{0}; // 0 = disabled
    std::chrono::steady_clock::time_point lastReadTs{std::chrono::steady_clock::now()};

    // Write queue/backpressure
    std::deque<std::string> writeQueue;
    std::size_t queuedBytes{0};
    std::size_t writeQueueMaxBytes{2 * 1024 * 1024}; // 2 MiB default cap
    std::chrono::milliseconds writeTimeout{0}; // 0 = disabled

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
            } catch (...) { /* ignore malformed value */ }
        }

#ifdef _WIN32
        stopEvent = ::CreateEventW(NULL, TRUE, FALSE, NULL);
        if (!stopEvent) {
            LOG_ERROR("StdioTransport: failed to create stop event (err={})", static_cast<unsigned long>(::GetLastError()));
        }
#else
    #ifdef __linux__
        wakeEventFd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
        if (wakeEventFd < 0) {
            LOG_ERROR("StdioTransport: failed to create eventfd (errno={} msg={})", errno, ::strerror(errno));
        }
    #else
        if (::pipe(wakePipe) != 0) {
            LOG_ERROR("StdioTransport: failed to create self-pipe (errno={} msg={})", errno, ::strerror(errno));
        } else {
            int fl0 = ::fcntl(wakePipe[0], F_GETFL, 0);
            int fl1 = ::fcntl(wakePipe[1], F_GETFL, 0);
            if (fl0 >= 0) {
                (void)::fcntl(wakePipe[0], F_SETFL, fl0 | O_NONBLOCK);
            }
            if (fl1 >= 0) {
                (void)::fcntl(wakePipe[1], F_SETFL, fl1 | O_NONBLOCK);
            }
        }
    #endif
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
    #ifdef __linux__
        if (wakeEventFd >= 0) { ::close(wakeEventFd); wakeEventFd = -1; }
    #else
        if (wakePipe[0] >= 0) { ::close(wakePipe[0]); wakePipe[0] = -1; }
        if (wakePipe[1] >= 0) { ::close(wakePipe[1]); wakePipe[1] = -1; }
    #endif
#endif
    }

    static std::string makeFrame(const std::string& payload) {
        std::string header = "Content-Length: " + std::to_string(payload.size()) + "\r\n\r\n";
        std::string frame; frame.reserve(header.size() + payload.size());
        frame.append(header);
        frame.append(payload);
        return frame;
    }

    bool enqueueFrame(const std::string& payload) {
        std::string frame = makeFrame(payload);
        {
            std::unique_lock<std::mutex> lk(writeMutex);
            if (queuedBytes + frame.size() > writeQueueMaxBytes) {
                LOG_ERROR("StdioTransport: write queue overflow (queued={} add={} max={})", queuedBytes, frame.size(), writeQueueMaxBytes);
                if (errorHandler) {
                    errorHandler("StdioTransport: write queue overflow");
                }
                connected = false;
#ifdef _WIN32
                if (stopEvent) { ::SetEvent(stopEvent); }
#else
    #ifdef __linux__
                if (wakeEventFd >= 0) {
                    uint64_t one = 1;
                    ssize_t wr;
                    do {
                        wr = ::write(wakeEventFd, &one, sizeof(one));
                    } while (wr < 0 && errno == EINTR);
                    if (wr < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        LOG_WARN("StdioTransport: eventfd write failed (errno={} msg={})", errno, ::strerror(errno));
                    }
                }
    #else
                if (wakePipe[1] >= 0) {
                    char b='x';
                    ssize_t wr;
                    do { 
                        wr = ::write(wakePipe[1], &b, 1);
                    } while (wr < 0 && errno == EINTR);
                    if (wr < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
                        LOG_WARN("StdioTransport: wake pipe write failed (errno={} msg={})", errno, ::strerror(errno));
                    }
                }
    #endif
#endif
                cvWrite.notify_all();
                return false;
            }
            queuedBytes += frame.size();
            writeQueue.emplace_back(std::move(frame));
        }
        cvWrite.notify_one();
        return true;
    }

    std::optional<std::string> readFrame(std::istream& in) {
        std::string line;
        std::size_t contentLength = 0;
        bool haveLength = false;

        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty()) {
                break;
            }
            auto colon = line.find(':');
            if (colon != std::string::npos) {
                std::string name = line.substr(0, colon);
                std::string value = line.substr(colon + 1);
                value.erase(value.begin(), std::find_if(value.begin(), value.end(), [](unsigned char ch){ return !std::isspace(ch); }));
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
            return std::nullopt;
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
            constexpr int waitTimeoutMs = 100;
            lastReadTs = std::chrono::steady_clock::now();

            auto tryExtractFrame = [&](std::string& buf) -> std::optional<std::string> {
                const std::string sep = "\r\n\r\n";
                std::size_t headerEnd = buf.find(sep);
                if (headerEnd == std::string::npos) {
                    return std::nullopt;
                }

                std::size_t pos = 0;
                std::size_t contentLength = 0;
                bool haveLength = false;
                while (pos < headerEnd) {
                    std::size_t eol = buf.find("\r\n", pos);
                    if (eol == std::string::npos || eol > headerEnd) {
                        break;
                    }
                    std::string line = buf.substr(pos, eol - pos);
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
                                    buf.erase(0, headerEnd + sep.size());
                                    return std::nullopt;
                                }
                                contentLength = static_cast<std::size_t>(v64);
                                haveLength = true;
                            } catch (...) {
                                LOG_WARN("Invalid Content-Length header: {}", value);
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
                    buf.erase(0, headerEnd + sep.size());
                    return std::nullopt;
                }
                std::size_t frameTotal = headerAndSep + contentLength;
                if (buf.size() < frameTotal) {
                    return std::nullopt;
                }
                std::string payload = buf.substr(headerEnd + sep.size(), contentLength);
                buf.erase(0, frameTotal);
                return payload;
            };

#ifndef _WIN32
            int fd = STDIN_FILENO;
            int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags >= 0) { (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
#endif

            while (connected) {
#ifdef _WIN32
                HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
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
                        HANDLE handles[2] = { stopEvent, hIn };
                        DWORD wr = ::WaitForMultipleObjects(2, handles, FALSE, waitTimeoutMs);
                        if (wr == WAIT_TIMEOUT) {
                        } else if (wr == (WAIT_OBJECT_0 + 1)) {
                            (void)::PeekNamedPipe(hIn, NULL, 0, NULL, &available, NULL);
                        } else if (wr == WAIT_OBJECT_0) {
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
                    HANDLE handles[2] = { stopEvent, hIn };
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
                        break;
                    } else if (wr != WAIT_TIMEOUT) {
                        DWORD err = ::GetLastError();
                        LOG_ERROR("StdioTransport: WaitForMultipleObjects failed (err={})", static_cast<unsigned long>(err));
                        if (errorHandler) {
                            errorHandler("StdioTransport: WaitForMultipleObjects failed");
                        }
                        break;
                    }
                }
#else
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
                    int rc = ::epoll_wait(ep, events, static_cast<int>(eventsCapacity), waitTimeoutMs);
                    ::close(ep);
                    if (rc < 0) {
                        if (errno == EINTR) {
                            continue;
                        }
                        LOG_ERROR("StdioTransport: epoll_wait failed (errno={} msg={})", errno, ::strerror(errno));
                        if (errorHandler) {
                            errorHandler("StdioTransport: epoll_wait failed");
                        }
                        break;
                    }
                    if (rc == 0) {
                    } else {
                        bool woke = false;
                        const size_t limit = (static_cast<size_t>(rc) < eventsCapacity) ? static_cast<size_t>(rc) : eventsCapacity;
                        for (size_t i = 0; i < limit; ++i) {
                            auto &ev = events[i];
                            if (ev.data.fd == fd) {
                                if (ev.events & (EPOLLERR | EPOLLHUP | EPOLLRDHUP)) {
                                    LOG_INFO("StdioTransport: stdin closed (epoll flags={})", static_cast<unsigned int>(ev.events));
                                    if (errorHandler) {
                                        errorHandler("StdioTransport: stdin closed");
                                    }
                                    woke = true;
                                } else if (ev.events & EPOLLIN) {
                                    n = ::read(fd, tmp.data(), tmp.size());
                                }
                            } else {
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
                        std::array<char, 64> b{};
                        while (true) {
                            ssize_t r;
                            do { r = ::read(pfds[1].fd, b.data(), b.size()); } while (r < 0 && errno == EINTR);
                            if (r > 0) { continue; }
                            if (r == 0) { break; }
                            if (r < 0) {
                                if (errno == EAGAIN || errno == EWOULDBLOCK) { break; }
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

                if (hadData) {
                    lastReadTs = std::chrono::steady_clock::now();
                    while (connected) {
                        auto framed = tryExtractFrame(buffer);
                        if (!framed.has_value()) break;
                        processMessage(framed.value());
                    }
                }

                // Idle read timeout check
                if (idleReadTimeout.count() > 0) {
                    auto now = std::chrono::steady_clock::now();
                    if (now - lastReadTs >= idleReadTimeout) {
                        LOG_ERROR("StdioTransport: idle read timeout ({} ms)", static_cast<long long>(idleReadTimeout.count()));
                        if (errorHandler) {
                            errorHandler("StdioTransport: idle read timeout");
                        }
                        break;
                    }
                }
            }
            connected = false;
            readerExited.store(true);
        });
    }

    void startWriter() {
        writerThread = std::thread([this]() {
            writerExited.store(false);
#ifndef _WIN32
            int fd = STDOUT_FILENO;
            int flags = ::fcntl(fd, F_GETFL, 0);
            if (flags >= 0) { (void)::fcntl(fd, F_SETFL, flags | O_NONBLOCK); }
#endif
            while (connected) {
                std::string frame;
                {
                    std::unique_lock<std::mutex> lk(writeMutex);
                    cvWrite.wait_for(lk, std::chrono::milliseconds(50), [&]{ return !connected || !writeQueue.empty(); });
                    if (!connected && writeQueue.empty()) {
                        break;
                    }
                    if (!writeQueue.empty()) {
                        frame = std::move(writeQueue.front());
                        writeQueue.pop_front();
                    }
                }
                if (frame.empty()) {
                    continue;
                }

                std::size_t total = 0;
                auto start = std::chrono::steady_clock::now();
                while (connected && total < frame.size()) {
#ifdef _WIN32
                    DWORD written = 0;
                    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
                    if (!hOut || hOut == INVALID_HANDLE_VALUE) {
                        if (errorHandler) {
                            errorHandler("StdioTransport: invalid STDOUT handle");
                        }
                        connected = false; break;
                    }
                    BOOL ok = ::WriteFile(hOut, frame.data() + total, static_cast<DWORD>(frame.size() - total), &written, NULL);
                    if (!ok) {
                        DWORD err = ::GetLastError();
                        LOG_ERROR("StdioTransport: WriteFile failed (err={})", static_cast<unsigned long>(err));
                        if (errorHandler) {
                            errorHandler("StdioTransport: write failed");
                        }
                        connected = false; break;
                    }
                    if (written == 0) {
                        // Avoid tight loop
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    }
                    total += static_cast<std::size_t>(written);
#else
                    ssize_t w = ::write(STDOUT_FILENO, frame.data() + total, frame.size() - total);
                    if (w > 0) {
                        total += static_cast<std::size_t>(w);
                    } else if (w < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
                        // Check write timeout
                        if (writeTimeout.count() > 0 && (std::chrono::steady_clock::now() - start) >= writeTimeout) {
                            LOG_ERROR("StdioTransport: write timeout ({} ms)", static_cast<long long>(writeTimeout.count()));
                            if (errorHandler) {
                                errorHandler("StdioTransport: write timeout");
                            }
                            connected = false; break;
                        }
                        // Back off a bit
                        std::this_thread::sleep_for(std::chrono::milliseconds(5));
                    } else if (w < 0 && errno == EINTR) {
                        continue;
                    } else if (w == 0) {
                        std::this_thread::sleep_for(std::chrono::milliseconds(1));
                    } else {
                        LOG_ERROR("StdioTransport: write error (errno={} msg={})", errno, ::strerror(errno));
                        if (errorHandler) {
                            errorHandler("StdioTransport: write error");
                        }
                        connected = false; break;
                    }
#endif
                }
                {
                    std::lock_guard<std::mutex> lk(writeMutex);
                    if (queuedBytes >= frame.size()) {
                        queuedBytes -= frame.size();
                    } else {
                        queuedBytes = 0;
                    }
                }
            }
            writerExited.store(true);
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
                        if (kv.second <= now) expired.push_back(kv.first);
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
        JSONRPCRequest request;
        if (message.find("\"method\"") != std::string::npos && message.find("\"id\"") != std::string::npos && request.Deserialize(message)) {
            if (requestHandler) {
                // Run request handling off-thread so reader can continue processing notifications (e.g., cancellations)
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
                        const std::string payload = resp->Serialize();
                        (void)enqueueFrame(payload);
                    } catch (const std::exception& e) {
                        LOG_ERROR("Request handler exception: {}", e.what());
                        auto resp = std::make_unique<JSONRPCResponse>();
                        resp->id = req.id;
                        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, e.what(), std::nullopt);
                        std::string payload = resp->Serialize();
                        (void)enqueueFrame(payload);
                    }
                }).detach();
                return;
            }
        }
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
            if constexpr (std::is_same_v<T, std::string>) { idStr = id; }
            else if constexpr (std::is_same_v<T, int64_t>) { idStr = std::to_string(id); }
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

    std::string generateRequestId() { return "req-" + std::to_string(++requestCounter); }
};

StdioTransport::StdioTransport() : pImpl(std::make_unique<Impl>()) { FUNC_SCOPE(); }
StdioTransport::~StdioTransport() { FUNC_SCOPE(); }

std::future<void> StdioTransport::Start() {
    FUNC_SCOPE();
    LOG_INFO("Starting StdioTransport");
    pImpl->connected = true;
    pImpl->startReader();
    pImpl->startWriter();
    pImpl->startTimeouts();
    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

std::future<void> StdioTransport::Close() {
    FUNC_SCOPE();
    LOG_INFO("Closing StdioTransport");
    pImpl->connected = false;
#ifdef _WIN32
    if (pImpl->stopEvent) { ::SetEvent(pImpl->stopEvent); }
#else
    #ifdef __linux__
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
                break;
            }
            LOG_WARN("StdioTransport: eventfd write failed (errno={} msg={})", errno, ::strerror(errno));
            break;
        }
    }
    #else
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
                break;
            }
            LOG_WARN("StdioTransport: wake pipe write failed (errno={} msg={})", errno, ::strerror(errno));
            break;
        }
    }
    #endif
#endif
#ifdef _WIN32
    HANDLE hIn = ::GetStdHandle(STD_INPUT_HANDLE);
    if (hIn && hIn != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hIn);
    }
    HANDLE hOut = ::GetStdHandle(STD_OUTPUT_HANDLE);
    if (hOut && hOut != INVALID_HANDLE_VALUE) {
        ::CloseHandle(hOut);
    }
#else
    ::close(STDIN_FILENO);
    ::close(STDOUT_FILENO);
#endif

    if (pImpl->readerThread.joinable()) {
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!pImpl->readerExited.load()) {
            if (std::chrono::steady_clock::now() >= deadline) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pImpl->readerExited.load()) { pImpl->readerThread.join(); }
        else { LOG_WARN("StdioTransport: reader thread appears blocked; detaching to avoid hang"); pImpl->readerThread.detach(); }
    }
    if (pImpl->writerThread.joinable()) {
        // Wake writer
        pImpl->cvWrite.notify_all();
        auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(500);
        while (!pImpl->writerExited.load()) {
            if (std::chrono::steady_clock::now() >= deadline) { break; }
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (pImpl->writerExited.load()) {
            pImpl->writerThread.join();
        }
        else { LOG_WARN("StdioTransport: writer thread appears blocked; detaching to avoid hang"); pImpl->writerThread.detach(); }
    }
    if (pImpl->timeoutThread.joinable()) {
        pImpl->timeoutThread.join();
    }

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

    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

bool StdioTransport::IsConnected() const { FUNC_SCOPE(); return pImpl->connected; }
std::string StdioTransport::GetSessionId() const { FUNC_SCOPE(); return pImpl->sessionId; }

std::future<std::unique_ptr<JSONRPCResponse>> StdioTransport::SendRequest(
    std::unique_ptr<JSONRPCRequest> request) {
    FUNC_SCOPE();
    if (!pImpl->connected.load()) {
        LOG_DEBUG("StdioTransport: SendRequest called while disconnected; returning error");
        std::promise<std::unique_ptr<JSONRPCResponse>> promise;
        auto fut = promise.get_future();
        auto resp = std::make_unique<JSONRPCResponse>();
        resp->id = pImpl->generateRequestId();
        resp->error = CreateErrorObject(JSONRPCErrorCodes::InternalError, "Transport not connected", std::nullopt);
        promise.set_value(std::move(resp));
        return fut;
    }
    // Preserve caller-provided id if set (string non-empty or int64); otherwise generate a new id
    std::string requestId;
    bool callerSetId = false;
    std::visit([&](auto&& idVal) {
        using T = std::decay_t<decltype(idVal)>;
        if constexpr (std::is_same_v<T, std::string>) { if (!idVal.empty()) { requestId = idVal; callerSetId = true; } }
        else if constexpr (std::is_same_v<T, int64_t>) { requestId = std::to_string(idVal); callerSetId = true; }
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
        pImpl->requestDeadlines[requestId] = std::chrono::steady_clock::now() + pImpl->requestTimeout;
    }

    std::string serialized = request->Serialize();
    LOG_DEBUG("Sending framed request ({} bytes)", serialized.size());
    (void)pImpl->enqueueFrame(serialized);
    return future;
}

std::future<void> StdioTransport::SendNotification(
    std::unique_ptr<JSONRPCNotification> notification) {
    FUNC_SCOPE();
    if (!pImpl->connected.load()) {
        LOG_DEBUG("StdioTransport: SendNotification called while disconnected; ignoring");
        std::promise<void> ready; ready.set_value(); return ready.get_future();
    }
    std::string serialized = notification->Serialize();
    LOG_DEBUG("Sending framed notification ({} bytes)", serialized.size());
    (void)pImpl->enqueueFrame(serialized);
    std::promise<void> promise; promise.set_value(); return promise.get_future();
}

void StdioTransport::SetNotificationHandler(NotificationHandler handler) { FUNC_SCOPE(); pImpl->notificationHandler = std::move(handler); }
void StdioTransport::SetRequestHandler(RequestHandler handler) { FUNC_SCOPE(); pImpl->requestHandler = std::move(handler); }
void StdioTransport::SetErrorHandler(ErrorHandler handler) { FUNC_SCOPE(); pImpl->errorHandler = std::move(handler); }

void StdioTransport::SetRequestTimeoutMs(uint64_t timeoutMs) {
    FUNC_SCOPE();
    if (timeoutMs == 0) { 
        pImpl->requestTimeout = std::chrono::milliseconds::max(); 
    }
    else { 
        pImpl->requestTimeout = std::chrono::milliseconds(timeoutMs); 
    }
}

void StdioTransport::SetIdleReadTimeoutMs(uint64_t timeoutMs) {
    FUNC_SCOPE();
    pImpl->idleReadTimeout = (timeoutMs == 0) ? std::chrono::milliseconds(0) : std::chrono::milliseconds(timeoutMs);
}

void StdioTransport::SetWriteQueueMaxBytes(std::size_t maxBytes) {
    FUNC_SCOPE();
    if (maxBytes == 0) { maxBytes = 1; }
    pImpl->writeQueueMaxBytes = maxBytes;
}

void StdioTransport::SetWriteTimeoutMs(uint64_t timeoutMs) {
    FUNC_SCOPE();
    pImpl->writeTimeout = (timeoutMs == 0) ? std::chrono::milliseconds(0) : std::chrono::milliseconds(timeoutMs);
}

std::unique_ptr<ITransport> StdioTransportFactory::CreateTransport(const std::string& config) {
    auto t = std::make_unique<StdioTransport>();
    // Parse key=value pairs separated by ';' or whitespace
    auto parseUint = [](const std::string& s, uint64_t& out) -> bool {
        try { out = static_cast<uint64_t>(std::stoull(s)); return true; } catch (...) { return false; }
    };
    auto parseSize = [](const std::string& s, std::size_t& out) -> bool {
        try { unsigned long long v = std::stoull(s); out = static_cast<std::size_t>(v); return true; } catch (...) { return false; }
    };
    std::string token;
    for (std::size_t i = 0; i < config.size();) {
        // Skip separators and spaces
        while (i < config.size() && (config[i] == ';' || config[i] == ' ' || config[i] == '\t')) ++i;
        if (i >= config.size()) break;
        std::size_t start = i;
        while (i < config.size() && config[i] != ';' && config[i] != ' ' && config[i] != '\t') ++i;
        token = config.substr(start, i - start);
        auto eq = token.find('=');
        if (eq != std::string::npos) {
            auto key = token.substr(0, eq);
            auto val = token.substr(eq + 1);
            if (key == "timeout_ms") {
                uint64_t v; if (parseUint(val, v)) t->SetRequestTimeoutMs(v);
            } else if (key == "idle_read_timeout_ms") {
                uint64_t v; if (parseUint(val, v)) t->SetIdleReadTimeoutMs(v);
            } else if (key == "write_timeout_ms") {
                uint64_t v; if (parseUint(val, v)) t->SetWriteTimeoutMs(v);
            } else if (key == "write_queue_max_bytes") {
                std::size_t v; if (parseSize(val, v)) t->SetWriteQueueMaxBytes(v);
            }
        }
    }
    return t;
}

} // namespace mcp
