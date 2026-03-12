//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: tests/test_tasks.cpp
// Purpose: End-to-end and strict-validation tests for MCP task support
//==========================================================================================================

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <mutex>
#include <optional>
#include <string>
#include <thread>
#include <vector>

#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/Protocol.h"
#include "mcp/Server.h"
#include "mcp/validation/Validation.h"

using namespace mcp;

namespace {

JSONValue makeObjectSchema() {
    JSONValue::Object schema;
    schema["type"] = std::make_shared<JSONValue>(std::string("object"));
    schema["properties"] = std::make_shared<JSONValue>(JSONValue::Object{});
    return JSONValue{schema};
}

JSONValue makeTextContent(const std::string& text) {
    JSONValue::Object obj;
    obj["type"] = std::make_shared<JSONValue>(std::string("text"));
    obj["text"] = std::make_shared<JSONValue>(text);
    return JSONValue{obj};
}

JSONValue makeCreateMessageResult(const std::string& model, const std::string& text) {
    JSONValue::Object contentObj;
    contentObj["type"] = std::make_shared<JSONValue>(std::string("text"));
    contentObj["text"] = std::make_shared<JSONValue>(text);

    JSONValue::Array content;
    content.push_back(std::make_shared<JSONValue>(contentObj));

    JSONValue::Object result;
    result["model"] = std::make_shared<JSONValue>(model);
    result["role"] = std::make_shared<JSONValue>(std::string("assistant"));
    result["content"] = std::make_shared<JSONValue>(content);
    return JSONValue{result};
}

JSONValue makeInitializeResultWithTaskSupport() {
    JSONValue::Object capabilities;
    JSONValue::Object tasksObj;
    tasksObj["list"] = std::make_shared<JSONValue>(JSONValue::Object{});
    tasksObj["cancel"] = std::make_shared<JSONValue>(JSONValue::Object{});
    JSONValue::Object requestsObj;
    JSONValue::Object toolsObj;
    toolsObj["call"] = std::make_shared<JSONValue>(JSONValue::Object{});
    requestsObj["tools"] = std::make_shared<JSONValue>(toolsObj);
    tasksObj["requests"] = std::make_shared<JSONValue>(requestsObj);
    capabilities["tasks"] = std::make_shared<JSONValue>(tasksObj);

    JSONValue::Object serverInfo;
    serverInfo["name"] = std::make_shared<JSONValue>(std::string("StrictTaskServer"));
    serverInfo["version"] = std::make_shared<JSONValue>(std::string("1.0.0"));

    JSONValue::Object result;
    result["protocolVersion"] = std::make_shared<JSONValue>(std::string(PROTOCOL_VERSION));
    result["capabilities"] = std::make_shared<JSONValue>(capabilities);
    result["serverInfo"] = std::make_shared<JSONValue>(serverInfo);
    return JSONValue{result};
}

std::string extractTextFromCallToolResult(const JSONValue& value) {
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return {};
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto contentIt = obj.find("content");
    if (contentIt == obj.end() || !contentIt->second ||
        !std::holds_alternative<JSONValue::Array>(contentIt->second->value)) {
        return {};
    }
    const auto& content = std::get<JSONValue::Array>(contentIt->second->value);
    if (content.empty() || !content.front() ||
        !std::holds_alternative<JSONValue::Object>(content.front()->value)) {
        return {};
    }
    const auto& item = std::get<JSONValue::Object>(content.front()->value);
    auto textIt = item.find("text");
    if (textIt == item.end() || !textIt->second ||
        !std::holds_alternative<std::string>(textIt->second->value)) {
        return {};
    }
    return std::get<std::string>(textIt->second->value);
}

std::string extractTextFromCreateMessageResult(const JSONValue& value) {
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return {};
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto contentIt = obj.find("content");
    if (contentIt == obj.end() || !contentIt->second ||
        !std::holds_alternative<JSONValue::Array>(contentIt->second->value)) {
        return {};
    }
    const auto& content = std::get<JSONValue::Array>(contentIt->second->value);
    if (content.empty() || !content.front() ||
        !std::holds_alternative<JSONValue::Object>(content.front()->value)) {
        return {};
    }
    const auto& item = std::get<JSONValue::Object>(content.front()->value);
    auto textIt = item.find("text");
    if (textIt == item.end() || !textIt->second ||
        !std::holds_alternative<std::string>(textIt->second->value)) {
        return {};
    }
    return std::get<std::string>(textIt->second->value);
}

std::string extractRelatedTaskId(const JSONValue& value, bool errorPayload = false) {
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return {};
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    const JSONValue::Object* metaParent = nullptr;
    if (errorPayload) {
        auto dataIt = obj.find("data");
        if (dataIt == obj.end() || !dataIt->second ||
            !std::holds_alternative<JSONValue::Object>(dataIt->second->value)) {
            return {};
        }
        const auto& dataObj = std::get<JSONValue::Object>(dataIt->second->value);
        auto metaIt = dataObj.find("_meta");
        if (metaIt == dataObj.end() || !metaIt->second ||
            !std::holds_alternative<JSONValue::Object>(metaIt->second->value)) {
            return {};
        }
        metaParent = &std::get<JSONValue::Object>(metaIt->second->value);
    } else {
        auto metaIt = obj.find("_meta");
        if (metaIt == obj.end() || !metaIt->second ||
            !std::holds_alternative<JSONValue::Object>(metaIt->second->value)) {
            return {};
        }
        metaParent = &std::get<JSONValue::Object>(metaIt->second->value);
    }

    auto relatedIt = metaParent->find("modelcontextprotocol.io/related-task");
    if (relatedIt == metaParent->end() || !relatedIt->second ||
        !std::holds_alternative<JSONValue::Object>(relatedIt->second->value)) {
        return {};
    }
    const auto& relatedObj = std::get<JSONValue::Object>(relatedIt->second->value);
    auto taskIdIt = relatedObj.find("taskId");
    if (taskIdIt == relatedObj.end() || !taskIdIt->second ||
        !std::holds_alternative<std::string>(taskIdIt->second->value)) {
        return {};
    }
    return std::get<std::string>(taskIdIt->second->value);
}

std::string extractAction(const JSONValue& value) {
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return {};
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto it = obj.find("action");
    if (it == obj.end() || !it->second || !std::holds_alternative<std::string>(it->second->value)) {
        return {};
    }
    return std::get<std::string>(it->second->value);
}

std::string extractMessage(const JSONValue& value) {
    if (!std::holds_alternative<JSONValue::Object>(value.value)) {
        return {};
    }
    const auto& obj = std::get<JSONValue::Object>(value.value);
    auto it = obj.find("message");
    if (it == obj.end() || !it->second || !std::holds_alternative<std::string>(it->second->value)) {
        return {};
    }
    return std::get<std::string>(it->second->value);
}

Tool makeTaskCapableTool(const std::string& name, const std::string& mode) {
    JSONValue::Object execution;
    execution["taskSupport"] = std::make_shared<JSONValue>(mode);
    return Tool{
        name,
        std::string("Task tool: ") + name,
        makeObjectSchema(),
        std::nullopt,
        std::nullopt,
        std::nullopt,
        JSONValue{execution}
    };
}

}  // namespace

TEST(Tasks, ToolCallLifecycleAndNotifications) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Task Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    server.RegisterTool(makeTaskCapableTool("slow", "optional"),
                        [](const JSONValue&, std::stop_token) -> std::future<ToolResult> {
                            return std::async(std::launch::async, []() {
                                std::this_thread::sleep_for(std::chrono::milliseconds(40));
                                ToolResult result;
                                result.content.push_back(makeTextContent("done"));
                                return result;
                            });
                        });

    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Task Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);

    std::mutex statusMutex;
    std::vector<Task> statuses;
    client->SetTaskStatusHandler([&](const Task& task) {
        std::lock_guard<std::mutex> lock(statusMutex);
        statuses.push_back(task);
    });

    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities clientCaps;
    auto serverCaps = client->Initialize(clientInfo, clientCaps).get();
    ASSERT_TRUE(serverCaps.tasks.has_value());
    EXPECT_TRUE(serverCaps.tasks->list);
    EXPECT_TRUE(serverCaps.tasks->cancel);
    EXPECT_TRUE(serverCaps.tasks->requests.toolCall);

    TaskMetadata taskMetadata;
    taskMetadata.ttl = 5000;
    auto created = client->CallToolTask("slow", makeObjectSchema(), taskMetadata).get();
    EXPECT_FALSE(created.task.taskId.empty());
    EXPECT_EQ(created.task.status, "working");
    ASSERT_TRUE(created.task.ttl.has_value());
    EXPECT_EQ(created.task.ttl.value(), 5000);

    auto listed = client->ListTasks().get();
    ASSERT_EQ(listed.size(), 1u);
    EXPECT_EQ(listed.front().taskId, created.task.taskId);

    auto current = client->GetTask(created.task.taskId).get();
    EXPECT_EQ(current.taskId, created.task.taskId);

    auto result = client->GetTaskResult(created.task.taskId).get();
    EXPECT_EQ(extractTextFromCallToolResult(result), "done");
    EXPECT_EQ(extractRelatedTaskId(result), created.task.taskId);

    auto finalTask = client->GetTask(created.task.taskId).get();
    EXPECT_EQ(finalTask.status, "completed");

    auto page = client->ListTasksPaged(std::nullopt, 1).get();
    ASSERT_EQ(page.tasks.size(), 1u);
    EXPECT_FALSE(page.nextCursor.has_value());

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        ASSERT_FALSE(statuses.empty());
        EXPECT_EQ(statuses.back().taskId, created.task.taskId);
        EXPECT_EQ(statuses.back().status, "completed");
    }

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(Tasks, CancelToolTaskTransitionsToCancelled) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    std::promise<void> handlerStarted;
    auto handlerStartedFuture = handlerStarted.get_future();
    std::promise<void> sawStop;
    auto sawStopFuture = sawStop.get_future();

    Server server("Cancelable Task Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    server.RegisterTool(makeTaskCapableTool("cancelable", "optional"),
                        [&handlerStarted, &sawStop](const JSONValue&, std::stop_token stopToken) -> std::future<ToolResult> {
                            return std::async(std::launch::async, [&handlerStarted, &sawStop, stopToken]() mutable {
                                handlerStarted.set_value();
                                while (!stopToken.stop_requested()) {
                                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                                }
                                sawStop.set_value();
                                ToolResult result;
                                result.content.push_back(makeTextContent("late"));
                                return result;
                            });
                        });
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Cancel Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities clientCaps;
    auto serverCaps = client->Initialize(clientInfo, clientCaps).get();
    ASSERT_TRUE(serverCaps.tasks.has_value());

    auto created = client->CallToolTask("cancelable", makeObjectSchema(), TaskMetadata{}).get();
    ASSERT_EQ(handlerStartedFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto cancelled = client->CancelTask(created.task.taskId).get();
    EXPECT_EQ(cancelled.status, "cancelled");
    ASSERT_EQ(sawStopFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);

    auto errorPayload = client->GetTaskResult(created.task.taskId).get();
    EXPECT_EQ(extractMessage(errorPayload), "Cancelled");
    EXPECT_EQ(extractRelatedTaskId(errorPayload, true), created.task.taskId);

    auto finalTask = client->GetTask(created.task.taskId).get();
    EXPECT_EQ(finalTask.status, "cancelled");

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(Tasks, ServerInitiatedClientTasksRoundTrip) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    Server server("Requesting Server");
    server.SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(server.Start(std::move(serverTransport)).get());

    ClientFactory factory;
    Implementation clientInfo{"Task-Capable Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    client->SetSamplingHandler([](const JSONValue&, const JSONValue&, const JSONValue&, const JSONValue&) -> std::future<JSONValue> {
        return std::async(std::launch::async, []() {
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
            return makeCreateMessageResult("gpt-test", "hello");
        });
    });
    client->SetElicitationHandler([](const ElicitationRequest& request) -> std::future<ElicitationResult> {
        return std::async(std::launch::deferred, [request]() {
            ElicitationResult result;
            result.action = "accept";
            JSONValue::Object content;
            content["answer"] = std::make_shared<JSONValue>(request.message);
            result.content = JSONValue{content};
            return result;
        });
    });
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities clientCaps;
    clientCaps.tasks = ClientTasksCapability{true, true, ClientTaskRequestCapabilities{true, true}};
    (void)client->Initialize(clientInfo, clientCaps).get();

    std::mutex statusMutex;
    std::vector<Task> statuses;
    server.SetTaskStatusHandler([&](const Task& task) {
        std::lock_guard<std::mutex> lock(statusMutex);
        statuses.push_back(task);
    });

    CreateMessageParams createParams;
    createParams.messages.push_back(makeTextContent("hello"));
    TaskMetadata samplingTask;
    samplingTask.ttl = 1234;
    auto createdSampling = server.RequestCreateMessageTask(createParams, samplingTask).get();
    ASSERT_FALSE(createdSampling.task.taskId.empty());

    auto samplingResult = server.GetTaskResult(createdSampling.task.taskId).get();
    EXPECT_EQ(extractRelatedTaskId(samplingResult), createdSampling.task.taskId);
    EXPECT_EQ(extractTextFromCreateMessageResult(samplingResult), "hello");
    auto samplingTaskState = server.GetTask(createdSampling.task.taskId).get();
    EXPECT_EQ(samplingTaskState.status, "completed");

    ElicitationRequest elicitationRequest;
    elicitationRequest.message = "confirm";
    elicitationRequest.requestedSchema = makeObjectSchema();
    auto createdElicitation = server.RequestElicitationTask(elicitationRequest, TaskMetadata{}).get();
    auto elicitationResult = server.GetTaskResult(createdElicitation.task.taskId).get();
    EXPECT_EQ(extractAction(elicitationResult), "accept");
    EXPECT_EQ(extractRelatedTaskId(elicitationResult), createdElicitation.task.taskId);

    auto listed = server.ListTasks().get();
    ASSERT_EQ(listed.size(), 2u);

    {
        std::lock_guard<std::mutex> lock(statusMutex);
        ASSERT_GE(statuses.size(), 2u);
        EXPECT_EQ(statuses.back().status, "completed");
    }

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(server.Stop().get());
}

TEST(Tasks, StrictModeRejectsMalformedTaskShapes) {
    auto pair = InMemoryTransport::CreatePair();
    auto clientTransport = std::move(pair.first);
    auto serverTransport = std::move(pair.second);

    ASSERT_NO_THROW(serverTransport->Start().get());
    serverTransport->SetNotificationHandler([](std::unique_ptr<JSONRPCNotification>) {});
    serverTransport->SetRequestHandler([](const JSONRPCRequest& req) -> std::unique_ptr<JSONRPCResponse> {
        auto response = std::make_unique<JSONRPCResponse>();
        response->id = req.id;
        if (req.method == Methods::Initialize) {
            response->result = makeInitializeResultWithTaskSupport();
            return response;
        }
        if (req.method == Methods::CallTool) {
            JSONValue::Object malformedTask;
            malformedTask["status"] = std::make_shared<JSONValue>(std::string("working"));
            JSONValue::Object malformedResult;
            malformedResult["task"] = std::make_shared<JSONValue>(malformedTask);
            response->result = JSONValue{malformedResult};
            return response;
        }
        if (req.method == Methods::GetTask) {
            JSONValue::Object malformedTask;
            malformedTask["taskId"] = std::make_shared<JSONValue>(std::string("task-1"));
            response->result = JSONValue{malformedTask};
            return response;
        }
        return CreateErrorResponse(req.id, JSONRPCErrorCodes::MethodNotFound, "Method not found", std::nullopt);
    });

    ClientFactory factory;
    Implementation clientInfo{"Strict Task Client", "1.0.0"};
    auto client = factory.CreateClient(clientInfo);
    client->SetValidationMode(validation::ValidationMode::Strict);
    ASSERT_NO_THROW(client->Connect(std::move(clientTransport)).get());

    ClientCapabilities clientCaps;
    auto initFuture = client->Initialize(clientInfo, clientCaps);
    ASSERT_EQ(initFuture.wait_for(std::chrono::seconds(2)), std::future_status::ready);
    (void)initFuture.get();

    EXPECT_THROW((void)client->CallToolTask("broken", makeObjectSchema(), TaskMetadata{}).get(), std::runtime_error);
    EXPECT_THROW((void)client->GetTask("task-1").get(), std::runtime_error);

    ASSERT_NO_THROW(client->Disconnect().get());
    ASSERT_NO_THROW(serverTransport->Close().get());
}
