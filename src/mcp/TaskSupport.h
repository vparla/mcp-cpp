//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: src/mcp/TaskSupport.h
// Purpose: Shared internal helpers for MCP task lifecycle handling
//==========================================================================================================

#pragma once

#include "mcp/JSONRPCTypes.h"
#include "mcp/Protocol.h"

#include <chrono>
#include <condition_variable>
#include <cstdint>
#include <ctime>
#include <functional>
#include <future>
#include <iomanip>
#include <mutex>
#include <optional>
#include <sstream>
#include <stop_token>
#include <string>
#include <unordered_map>
#include <vector>

namespace mcp::tasks {

constexpr const char* RELATED_TASK_META_KEY = "modelcontextprotocol.io/related-task";

namespace Status {
constexpr const char* Working = "working";
constexpr const char* InputRequired = "input_required";
constexpr const char* Completed = "completed";
constexpr const char* Failed = "failed";
constexpr const char* Cancelled = "cancelled";
}  // namespace Status

namespace Support {
constexpr const char* Forbidden = "forbidden";
constexpr const char* Optional = "optional";
constexpr const char* Required = "required";
}  // namespace Support

enum class TaskKind {
    Unknown,
    ToolCall,
    CreateMessage,
    Elicitation,
};

inline bool IsTerminalStatus(const std::string& status) {
    return status == std::string(Status::Completed) ||
           status == std::string(Status::Failed) ||
           status == std::string(Status::Cancelled);
}

inline std::string UtcNowIso8601() {
    const auto now = std::chrono::system_clock::now();
    const auto nowMs = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    const std::time_t current = std::chrono::system_clock::to_time_t(now);
    std::tm tm{};
#if defined(_WIN32)
    gmtime_s(&tm, &current);
#else
    gmtime_r(&current, &tm);
#endif
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%Y-%m-%dT%H:%M:%S", &tm);
    std::ostringstream out;
    out << buffer << '.' << std::setw(3) << std::setfill('0') << nowMs.count() << 'Z';
    return out.str();
}

inline JSONValue MakeRelatedTaskMetaValue(const std::string& taskId) {
    JSONValue::Object obj;
    obj["taskId"] = std::make_shared<JSONValue>(taskId);
    return JSONValue{obj};
}

inline JSONValue AttachRelatedTaskMetaToResult(JSONValue payload, const std::string& taskId) {
    if (!std::holds_alternative<JSONValue::Object>(payload.value)) {
        return payload;
    }
    auto obj = std::get<JSONValue::Object>(payload.value);
    JSONValue::Object meta;
    auto metaIt = obj.find("_meta");
    if (metaIt != obj.end() && metaIt->second && std::holds_alternative<JSONValue::Object>(metaIt->second->value)) {
        meta = std::get<JSONValue::Object>(metaIt->second->value);
    }
    meta[RELATED_TASK_META_KEY] = std::make_shared<JSONValue>(MakeRelatedTaskMetaValue(taskId));
    obj["_meta"] = std::make_shared<JSONValue>(meta);
    return JSONValue{obj};
}

inline JSONValue AttachRelatedTaskMetaToError(JSONValue payload, const std::string& taskId) {
    if (!std::holds_alternative<JSONValue::Object>(payload.value)) {
        return payload;
    }
    auto obj = std::get<JSONValue::Object>(payload.value);
    JSONValue::Object dataObj;
    auto dataIt = obj.find("data");
    if (dataIt != obj.end() && dataIt->second && std::holds_alternative<JSONValue::Object>(dataIt->second->value)) {
        dataObj = std::get<JSONValue::Object>(dataIt->second->value);
    }
    JSONValue::Object metaObj;
    auto metaIt = dataObj.find("_meta");
    if (metaIt != dataObj.end() && metaIt->second &&
        std::holds_alternative<JSONValue::Object>(metaIt->second->value)) {
        metaObj = std::get<JSONValue::Object>(metaIt->second->value);
    }
    metaObj[RELATED_TASK_META_KEY] = std::make_shared<JSONValue>(MakeRelatedTaskMetaValue(taskId));
    dataObj["_meta"] = std::make_shared<JSONValue>(metaObj);
    obj["data"] = std::make_shared<JSONValue>(dataObj);
    return JSONValue{obj};
}

inline JSONValue SerializeTask(const Task& task) {
    JSONValue::Object obj;
    obj["taskId"] = std::make_shared<JSONValue>(task.taskId);
    obj["status"] = std::make_shared<JSONValue>(task.status);
    obj["createdAt"] = std::make_shared<JSONValue>(task.createdAt);
    obj["lastUpdatedAt"] = std::make_shared<JSONValue>(task.lastUpdatedAt);
    if (task.statusMessage.has_value()) {
        obj["statusMessage"] = std::make_shared<JSONValue>(task.statusMessage.value());
    }
    if (task.ttl.has_value()) {
        obj["ttl"] = std::make_shared<JSONValue>(task.ttl.value());
    } else {
        obj["ttl"] = std::make_shared<JSONValue>(nullptr);
    }
    if (task.pollInterval.has_value()) {
        obj["pollInterval"] = std::make_shared<JSONValue>(task.pollInterval.value());
    }
    return JSONValue{obj};
}

inline Task ParseTask(const JSONValue& value) {
    Task task;
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return task;
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto it = obj.find("taskId");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        task.taskId = std::get<std::string>(it->second->value);
    }
    it = obj.find("status");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        task.status = std::get<std::string>(it->second->value);
    }
    it = obj.find("statusMessage");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        task.statusMessage = std::get<std::string>(it->second->value);
    }
    it = obj.find("createdAt");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        task.createdAt = std::get<std::string>(it->second->value);
    }
    it = obj.find("lastUpdatedAt");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        task.lastUpdatedAt = std::get<std::string>(it->second->value);
    }
    it = obj.find("ttl");
    if (it != obj.end() && it->second && std::holds_alternative<int64_t>(it->second->value)) {
        task.ttl = std::get<int64_t>(it->second->value);
    }
    it = obj.find("pollInterval");
    if (it != obj.end() && it->second && std::holds_alternative<int64_t>(it->second->value)) {
        task.pollInterval = std::get<int64_t>(it->second->value);
    }
    return task;
}

inline JSONValue SerializeCreateTaskResult(const CreateTaskResult& result) {
    JSONValue::Object obj;
    obj["task"] = std::make_shared<JSONValue>(SerializeTask(result.task));
    if (result.meta.has_value()) {
        obj["_meta"] = std::make_shared<JSONValue>(result.meta.value());
    }
    return JSONValue{obj};
}

inline CreateTaskResult ParseCreateTaskResult(const JSONValue& value) {
    CreateTaskResult result;
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return result;
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto taskIt = obj.find("task");
    if (taskIt != obj.end() && taskIt->second) {
        result.task = ParseTask(*taskIt->second);
    }
    auto metaIt = obj.find("_meta");
    if (metaIt != obj.end() && metaIt->second) {
        result.meta = *metaIt->second;
    }
    return result;
}

inline JSONValue SerializeTasksListResult(const TasksListResult& result) {
    JSONValue::Object obj;
    JSONValue::Array tasksArray;
    tasksArray.reserve(result.tasks.size());
    for (const auto& task : result.tasks) {
        tasksArray.push_back(std::make_shared<JSONValue>(SerializeTask(task)));
    }
    obj["tasks"] = std::make_shared<JSONValue>(tasksArray);
    if (result.nextCursor.has_value()) {
        obj["nextCursor"] = std::make_shared<JSONValue>(result.nextCursor.value());
    }
    return JSONValue{obj};
}

inline TasksListResult ParseTasksListResult(const JSONValue& value) {
    TasksListResult out;
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return out;
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto it = obj.find("tasks");
    if (it != obj.end() && it->second && std::holds_alternative<JSONValue::Array>(it->second->value)) {
        const auto& arr = std::get<JSONValue::Array>(it->second->value);
        out.tasks.reserve(arr.size());
        for (const auto& item : arr) {
            if (item) {
                out.tasks.push_back(ParseTask(*item));
            }
        }
    }
    it = obj.find("nextCursor");
    if (it != obj.end() && it->second && std::holds_alternative<std::string>(it->second->value)) {
        out.nextCursor = std::get<std::string>(it->second->value);
    }
    return out;
}

inline JSONValue SerializeTaskMetadata(const TaskMetadata& task) {
    JSONValue::Object obj;
    if (task.ttl.has_value()) {
        obj["ttl"] = std::make_shared<JSONValue>(task.ttl.value());
    }
    return JSONValue{obj};
}

inline bool TryParseTaskMetadata(const std::optional<JSONValue>& params, TaskMetadata& task) {
    if (!params.has_value() || !std::holds_alternative<JSONValue::Object>(params->value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(params->value);
    auto it = obj.find("task");
    if (it == obj.end() || !it->second || !std::holds_alternative<JSONValue::Object>(it->second->value)) {
        return false;
    }
    const auto& taskObj = std::get<JSONValue::Object>(it->second->value);
    auto ttlIt = taskObj.find("ttl");
    if (ttlIt != taskObj.end() && ttlIt->second && std::holds_alternative<int64_t>(ttlIt->second->value)) {
        task.ttl = std::get<int64_t>(ttlIt->second->value);
    }
    return true;
}

inline bool TryParseTaskId(const std::optional<JSONValue>& params, std::string& taskId) {
    taskId.clear();
    if (!params.has_value() || !std::holds_alternative<JSONValue::Object>(params->value)) {
        return false;
    }
    const auto& obj = std::get<JSONValue::Object>(params->value);
    auto it = obj.find("taskId");
    if (it == obj.end() || !it->second || !std::holds_alternative<std::string>(it->second->value)) {
        return false;
    }
    taskId = std::get<std::string>(it->second->value);
    return true;
}

inline std::string ToolTaskSupportMode(const Tool& tool) {
    if (!tool.execution.has_value() || !std::holds_alternative<JSONValue::Object>(tool.execution->value)) {
        return Support::Forbidden;
    }
    const auto& obj = std::get<JSONValue::Object>(tool.execution->value);
    auto it = obj.find("taskSupport");
    if (it == obj.end() || !it->second || !std::holds_alternative<std::string>(it->second->value)) {
        return Support::Forbidden;
    }
    const auto& value = std::get<std::string>(it->second->value);
    if (value == std::string(Support::Required) || value == std::string(Support::Optional)) {
        return value;
    }
    return Support::Forbidden;
}

class BackgroundTaskGroup {
public:
    void Add(std::future<void> future) {
        std::lock_guard<std::mutex> lock(mutex_);
        DrainReadyLocked();
        futures_.push_back(std::move(future));
    }

    void WaitAll() {
        std::vector<std::future<void>> futures;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            futures.swap(futures_);
        }
        for (auto& future : futures) {
            if (!future.valid()) {
                continue;
            }
            try {
                future.get();
            } catch (...) {
            }
        }
    }

private:
    void DrainReadyLocked() {
        auto it = futures_.begin();
        while (it != futures_.end()) {
            if (!it->valid()) {
                it = futures_.erase(it);
                continue;
            }
            if (it->wait_for(std::chrono::milliseconds(0)) == std::future_status::ready) {
                try {
                    it->get();
                } catch (...) {
                }
                it = futures_.erase(it);
                continue;
            }
            ++it;
        }
    }

    std::mutex mutex_;
    std::vector<std::future<void>> futures_;
};

class TaskStore {
public:
    using StatusCallback = std::function<void(const Task&)>;

    explicit TaskStore(StatusCallback callback = {})
        : statusCallback_(std::move(callback)) {}

    void SetStatusCallback(StatusCallback callback) {
        std::lock_guard<std::mutex> lock(mutex_);
        statusCallback_ = std::move(callback);
    }

    Task CreateTask(TaskKind kind, const TaskMetadata& metadata) {
        Task task;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            task.taskId = "task-" + std::to_string(nextId_++);
            task.status = Status::Working;
            task.createdAt = UtcNowIso8601();
            task.lastUpdatedAt = task.createdAt;
            task.ttl = metadata.ttl;
            task.pollInterval = static_cast<int64_t>(100);
            auto record = std::make_shared<Record>();
            record->task = task;
            record->kind = kind;
            record->stopSource = std::make_shared<std::stop_source>();
            records_[task.taskId] = record;
            order_.push_back(task.taskId);
        }
        EmitStatus(task);
        return task;
    }

    std::shared_ptr<std::stop_source> GetStopSource(const std::string& taskId) {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(taskId);
        if (it == records_.end()) {
            return {};
        }
        return it->second->stopSource;
    }

    std::optional<Task> GetTask(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(taskId);
        if (it == records_.end()) {
            return std::nullopt;
        }
        return it->second->task;
    }

    std::optional<TaskKind> GetKind(const std::string& taskId) const {
        std::lock_guard<std::mutex> lock(mutex_);
        auto it = records_.find(taskId);
        if (it == records_.end()) {
            return std::nullopt;
        }
        return it->second->kind;
    }

    TasksListResult ListTasks(size_t start, const std::optional<size_t>& limit) const {
        std::lock_guard<std::mutex> lock(mutex_);
        TasksListResult out;
        if (start >= order_.size()) {
            return out;
        }
        const size_t end = limit.has_value() ? std::min(order_.size(), start + limit.value()) : order_.size();
        out.tasks.reserve(end - start);
        for (size_t index = start; index < end; ++index) {
            const auto& taskId = order_[index];
            auto it = records_.find(taskId);
            if (it != records_.end()) {
                out.tasks.push_back(it->second->task);
            }
        }
        if (limit.has_value() && end < order_.size()) {
            out.nextCursor = std::to_string(end);
        }
        return out;
    }

    bool CompleteTask(const std::string& taskId,
                      const std::string& status,
                      const std::optional<std::string>& statusMessage,
                      bool isError,
                      const JSONValue& payload,
                      Task* taskOut = nullptr) {
        return SetTerminal(taskId, status, statusMessage, isError, payload, taskOut);
    }

    bool CancelTask(const std::string& taskId, const JSONValue& errorPayload, Task* taskOut = nullptr) {
        return SetTerminal(taskId, Status::Cancelled, std::string("Cancelled"), true, errorPayload, taskOut, true);
    }

    std::optional<std::pair<bool, JSONValue>> WaitForTerminalPayload(const std::string& taskId) const {
        std::shared_ptr<Record> record;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = records_.find(taskId);
            if (it == records_.end()) {
                return std::nullopt;
            }
            record = it->second;
        }

        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(lock, [&record]() { return record->payload.has_value(); });
        return std::make_pair(record->payloadIsError, record->payload.value());
    }

private:
    struct Record {
        Task task;
        TaskKind kind{TaskKind::Unknown};
        std::shared_ptr<std::stop_source> stopSource;
        bool payloadIsError = false;
        std::optional<JSONValue> payload;
    };

    bool SetTerminal(const std::string& taskId,
                     const std::string& status,
                     const std::optional<std::string>& statusMessage,
                     bool isError,
                     const JSONValue& payload,
                     Task* taskOut,
                     bool requestStop = false) {
        StatusCallback callback;
        Task task;
        std::shared_ptr<std::stop_source> stopSource;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            auto it = records_.find(taskId);
            if (it == records_.end()) {
                return false;
            }
            auto& record = *(it->second);
            if (record.payload.has_value() || IsTerminalStatus(record.task.status)) {
                if (taskOut) {
                    *taskOut = record.task;
                }
                return false;
            }
            record.task.status = status;
            record.task.statusMessage = statusMessage;
            record.task.lastUpdatedAt = UtcNowIso8601();
            record.payloadIsError = isError;
            record.payload = payload;
            stopSource = record.stopSource;
            task = record.task;
            callback = statusCallback_;
            if (taskOut) {
                *taskOut = task;
            }
        }
        if (requestStop && stopSource) {
            try {
                stopSource->request_stop();
            } catch (...) {
            }
        }
        cv_.notify_all();
        if (callback) {
            callback(task);
        }
        return true;
    }

    void EmitStatus(const Task& task) const {
        StatusCallback callback;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            callback = statusCallback_;
        }
        if (callback) {
            callback(task);
        }
    }

    mutable std::mutex mutex_;
    mutable std::condition_variable cv_;
    std::unordered_map<std::string, std::shared_ptr<Record>> records_;
    std::vector<std::string> order_;
    uint64_t nextId_{1};
    StatusCallback statusCallback_;
};

}  // namespace mcp::tasks
