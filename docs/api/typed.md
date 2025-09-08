<!--
==========================================================================================================
SPDX-License-Identifier: MIT
Copyright (c) 2025 Vinny Parla
File: docs/api/typed.md
Purpose: Typed client wrappers and content helpers quick-start
==========================================================================================================
-->
# Typed Client Wrappers and Content Helpers

Headers:
- `include/mcp/typed/ClientTyped.h`
- `include/mcp/typed/Content.h`
- `include/mcp/typed/Prompts.h`

The typed wrappers provide ergonomic helpers on top of the raw JSON APIs to parse and return strongly-typed C++ structs for common operations. They also include utilities to easily build or extract typed content (e.g., text messages).

## Why typed wrappers?
- Fewer manual shape checks and conversions.
- Consistent error handling: wrappers detect JSON-RPC errors (typed errors) and throw `std::runtime_error`.
- Paging helpers to list “all items” with automatic cursor handling.

## Quick-start

```cpp
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

// Call a tool and extract text content
auto toolFut = typed::callTool(*client, "echo", /*arguments=*/JSONValue{JSONValue::Object{}});
CallToolResult toolRes = toolFut.get();
if (auto text = typed::firstText(toolRes)) {
  std::cout << "tool text: " << *text << "\n";
}

// Read a resource and collect all text chunks
ReadResourceResult rr = typed::readResource(*client, "mem://hello").get();
for (const auto& s : typed::collectText(rr)) {
  std::cout << "chunk: " << s << "\n";
}

// Get a prompt result and read its messages
GetPromptResult pr = typed::getPrompt(*client, "myPrompt", JSONValue{}).get();
std::cout << "desc: " << pr.description << "\n";
```

## Paging helpers

Wrappers include helpers that transparently iterate through pages until exhaustion.

```cpp
// List all tools with a page size of 25
std::vector<Tool> tools = typed::listAllTools(*client, 25).get();
```

## Error handling

When the server returns a JSON-RPC error (e.g., `ToolNotFound`, `ResourceNotFound`, `PromptNotFound`, etc.), wrappers throw `std::runtime_error` with a readable message. If you prefer not to throw, you can use the underlying raw APIs and check result vs error manually.

## Prompt builders

You can build prompt arguments and simple message arrays using `Prompts.h`:

```cpp
#include "mcp/typed/Prompts.h"

using namespace mcp::typed::prompts;

ArgsBuilder args;
args.addString("name", "world").addInt("count", 3);
JSONValue messages = JSONValue{JSONValue::Array{}}; // or use makeTextMessages({"hello"})
```

For a complete runnable example, see `examples/typed_quickstart`.
