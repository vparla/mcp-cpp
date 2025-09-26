//==========================================================================================================
// SPDX-License-Identifier: MIT
// Copyright (c) 2025 Vinny Parla
// File: main.cpp
// Purpose: Example demonstrating server-initiated sampling using a typed helper
//==========================================================================================================

#include <future>
#include <iostream>
#include <string>

#include "mcp/Server.h"
#include "mcp/Client.h"
#include "mcp/InMemoryTransport.hpp"
#include "mcp/typed/Sampling.h"

using namespace mcp;

int main() {
    // Create transport pair
    auto pair = InMemoryTransport::CreatePair();
    auto clientTrans = std::move(pair.first);
    auto serverTrans = std::move(pair.second);

    // Start server
    Server server("SamplingSrv");
    server.Start(std::move(serverTrans)).get();

    // Create client and connect
    ClientFactory factory; Implementation info{"SamplingCli","1.0.0"};
    auto client = factory.CreateClient(info);
    client->Connect(std::move(clientTrans)).get();

    // Register client sampling handler using typed helper to build a result
    client->SetSamplingHandler([](const JSONValue& messages,
                                  const JSONValue& modelPreferences,
                                  const JSONValue& systemPrompt,
                                  const JSONValue& includeContext){
        (void)messages; (void)modelPreferences; (void)systemPrompt; (void)includeContext;
        return std::async(std::launch::deferred, [](){
            return mcp::typed::makeTextSamplingResult("example-model", "assistant", "hello from client");
        });
    });

    // Server requests client to create a message
    CreateMessageParams params;
    JSONValue::Object msgObj; // minimal empty message object for demo
    params.messages = { JSONValue{msgObj} };
    auto fut = server.RequestCreateMessage(params);

    JSONValue result = fut.get();
    // Print model and first text content
    if (std::holds_alternative<JSONValue::Object>(result.value)) {
        const auto& obj = std::get<JSONValue::Object>(result.value);
        auto itModel = obj.find("model");
        auto itContent = obj.find("content");
        if (itModel != obj.end() && itModel->second && std::holds_alternative<std::string>(itModel->second->value)) {
            std::cout << "model: " << std::get<std::string>(itModel->second->value) << "\n";
        }
        if (itContent != obj.end() && itContent->second && std::holds_alternative<JSONValue::Array>(itContent->second->value)) {
            const auto& arr = std::get<JSONValue::Array>(itContent->second->value);
            if (!arr.empty() && arr[0] && std::holds_alternative<JSONValue::Object>(arr[0]->value)) {
                const auto& c0 = std::get<JSONValue::Object>(arr[0]->value);
                auto itText = c0.find("text");
                if (itText != c0.end() && itText->second && std::holds_alternative<std::string>(itText->second->value)) {
                    std::cout << "text: " << std::get<std::string>(itText->second->value) << "\n";
                }
            }
        }
    }

    client->Disconnect().get();
    server.Stop().get();
    return 0;
}
