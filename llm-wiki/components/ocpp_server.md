# OcppServer

**Source:** `esphome/components/ocpp/server.h:30` / `server.cpp:1`

Raw WebSocket server built on ESPHome's `socket` abstraction. No external WebSocket library — all framing is hand-rolled.

## Key Constants

| Constant | Value | Location |
| --- | --- | --- |
| `MAX_WS_PAYLOAD` | 16384 bytes | `server.cpp:19` |
| `MAX_RX_BUFFER` | 16392 bytes (payload + 8 header) | `server.cpp:21` |
| `MAX_WS_FRAMES_PER_LOOP` | 1 | `server.cpp:22` |
| `WS_GUID` | `"258EAFA5-E914-47DA-95CA-C5AB0DC85B11"` | `server.cpp:23` |

## Lifecycle

### `setup()` (`server.cpp:201`)

1. Creates a TCP `ListenSocket` via `socket::socket_ip_loop_monitored()`
2. Sets `SO_REUSEADDR`
3. Binds to `server_port_` (default 9000)
4. Listens with backlog = max(max_clients, 1)
5. Sets non-blocking
6. Returns false → component calls `mark_failed()`

### `loop()` (`server.cpp:221`)

1. If listen socket is ready → `accept_client_()`
2. For each client with data ready → `read_client_()`

## Connection Handling

### `accept_client_()` (`server.cpp:244`)

- Accepts TCP connection via `accept_loop_monitored()`
- Rejects if session count ≥ `max_clients + 1` (the +1 is for the handshake-before-protocol-negotiation slot)
- Creates `ClientSession` with empty `connection_id` and `handshake_done = false`

### `read_client_()` (`server.cpp:272`)

- Reads into 512-byte stack buffer, appends to `rx_buffer`
- Closes on: zero read (peer disconnect), `errno` other than `EAGAIN`/`EWOULDBLOCK`, oversized buffer
- If handshake not done → `handle_http_handshake_()`
- If handshake done → `handle_ws_frames_()`

## HTTP Handshake (`server.cpp:319`)

1. Extracts HTTP request line URI and query string (strips query)
2. Reads `Sec-WebSocket-Key` and `Sec-WebSocket-Protocol` headers
3. Validates: method is GET, key present, URI matches configured path
4. Extracts `connection_id` from URI:
   - Exact match to `server_path_` → empty connection_id
   - `server_path_ == "/"` and URI starts with `/` → connection_id = URI after leading `/`
   - `server_path_/rest` pattern → connection_id = URI after prefix
5. Calls `listener->select_websocket_protocol()` for protocol negotiation
6. Rejects with HTTP 400 if no compatible protocol
7. Closes any existing connection with the same `connection_id` (duplicate replacement)
8. Sends HTTP 101 response with `Sec-WebSocket-Accept` (SHA-1 of key + GUID, base64-encoded) and `Sec-WebSocket-Protocol`
9. Marks `handshake_done = true`, notifies listener via `on_websocket_connected()`

## WebSocket Frame Handling (`server.cpp:385`)

### Receiving

- Reads frame header: opcode (4 bits), masked flag, payload length (7/16/64 bits)
- Rejects: unmasked client frames, >16384 byte payloads, 64-bit extended length
- Unmasks payload with 4-byte mask key
- Opcode dispatch:
  - `0x8` (Close) → close connection
  - `0x9` (Ping) → send Pong
  - `0x1` (Text) → forward to `listener->on_websocket_text()`
- Processes max 1 frame per loop call (`MAX_WS_FRAMES_PER_LOOP`)

### Sending: `write_frame_()` (`server.cpp:457`)

- Builds unmasked server frame (server frames are NOT masked per RFC 6455)
- Opcode byte: `0x80 | opcode` (FIN bit always set)
- Payload length: 1-byte (<126), 3-byte (126 + uint16), or 9-byte (127 + uint64)
- Writes header + payload in one `socket::write()` call

## URL Generation

`get_charger_url()` (`server.cpp:231`): Returns `ws://<first-ipv4>:<port><path>` for logging/display. Uses `<server_ip>` placeholder if no IP is available yet.

## SHA-1 Implementation

`sha1()` (`server.cpp:96`): Hand-rolled SHA-1 for WebSocket accept key computation. No external crypto dependency. Processes input with standard padding and 80-round compression.
