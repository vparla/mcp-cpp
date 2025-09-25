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

## Range reads and chunk helpers

Typed wrappers provide convenient helpers for experimental range reads (offset/length) and for reading an entire resource in fixed-size chunks and reassembling.

```cpp
#include "mcp/typed/ClientTyped.h"
#include "mcp/typed/Content.h"

using namespace mcp;

// Read a 4-byte slice starting at offset 3
ReadResourceResult slice = typed::readResourceRange(*client,
                                                    "mem://doc",
                                                    std::optional<int64_t>(3),
                                                    std::optional<int64_t>(4)).get();
for (const auto& s : typed::collectText(slice)) {
  std::cout << s; // prints the slice
}

// Read entire resource in 8 KiB chunks and reassemble
ReadResourceResult agg = typed::readAllResourceInChunks(*client, "mem://doc", /*chunkSize*/ 8192).get();
std::string all;
for (const auto& s : typed::collectText(agg)) all += s;
```

Clamp-aware overload example:

```cpp
// If the server advertises a clamp in capabilities.experimental.resourceReadChunking.maxChunkBytes,
// use the clamp-aware overload to minimize round-trips by picking min(preferred, clamp).

ServerCapabilities scaps = client->Initialize(ci, caps).get();
std::optional<size_t> clampHint;
auto it = scaps.experimental.find("resourceReadChunking");
if (it != scaps.experimental.end() && std::holds_alternative<mcp::JSONValue::Object>(it->second.value)) {
  const auto& rrc = std::get<mcp::JSONValue::Object>(it->second.value);
  auto itMax = rrc.find("maxChunkBytes");
  if (itMax != rrc.end() && itMax->second && std::holds_alternative<int64_t>(itMax->second->value)) {
    auto v = static_cast<size_t>(std::get<int64_t>(itMax->second->value));
    if (v > 0) clampHint = v;
  }
}

ReadResourceResult agg2 = typed::readAllResourceInChunks(*client, "mem://doc", /*preferredChunkSize*/ 8192, clampHint).get();
```

Notes:

- Range slicing is applied server-side to text content; the result shape remains identical to a normal read (contents array).
- See server-side details in [docs/api/server.md](./server.md#resource-read-chunking-experimental).
- If the server advertises `capabilities.experimental.resourceReadChunking.maxChunkBytes`, it may clamp each returned slice to at most that many bytes. The `readAllResourceInChunks` helper automatically advances the offset by the actual returned bytes so it remains correct even when the server clamp is smaller than the requested `chunkSize`.
- When using the clamp-aware overload, the helper selects `min(preferredChunkSize, clampHint)` as the effective chunk size to reduce the number of ranged reads.

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
