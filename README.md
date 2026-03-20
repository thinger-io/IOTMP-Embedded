# IOTMP-Embedded

[![Tests](https://github.com/thinger-io/IOTMP-Embedded/actions/workflows/tests.yml/badge.svg)](https://github.com/thinger-io/IOTMP-Embedded/actions/workflows/tests.yml)

Portable, header-only C++17 core for the [IOTMP](https://docs.thinger.io) (IoT Message Protocol). Provides everything needed to build an IOTMP client for any platform — protocol encoding, message handling, resource model, connection lifecycle, streaming, and server API.

Used by:

| Platform | Repository |
|----------|------------|
| Arduino / ESP32 / ESP8266 | [IOTMP-Arduino](https://github.com/thinger-io/IOTMP-Arduino) |
| ESP-IDF (native) | [IOTMP-ESPIDF](https://github.com/thinger-io/IOTMP-ESPIDF) |
| Zephyr RTOS | [IOTMP-Zephyr](https://github.com/thinger-io/IOTMP-Zephyr) |

## Creating a client for a new platform

The core provides a CRTP base class `iotmp_client_base<Derived>` that contains all the protocol logic. To support a new platform, you only implement 7 transport methods:

```cpp
#include <thinger/iotmp/iotmp.hpp>

class my_client : public thinger::iotmp::iotmp_client_base<my_client> {
public:
    my_client(const char* user, const char* device, const char* credential)
        : iotmp_client_base(user, device, credential) {}

    // --- Required: implement these 7 methods ---

    // Send raw bytes to the server. Return true on success.
    bool send_bytes_impl(const void* data, size_t len);

    // Receive exactly `len` bytes from the server (blocking with timeout).
    bool recv_bytes_impl(void* buf, size_t len);

    // Check if the transport is connected.
    bool is_connected_impl() const;

    // Open a connection to host_:port_. Return true on success.
    bool connect_impl();

    // Close the connection.
    void disconnect_impl();

    // Check if data is available to read.
    // Non-blocking for cooperative platforms (Arduino).
    // Can block with short timeout for threaded platforms (RTOS).
    bool data_available_impl();

    // Return monotonic time in milliseconds.
    unsigned long get_millis() const;
};
```

That's it. The base class provides everything else:

- **`handle()`** — full connection lifecycle: reconnect with backoff, connect, authenticate, process messages, keepalive, streams
- **`operator[]`** — resource registration with lambda callbacks
- **`authenticate()`** — IOTMP CONNECT handshake
- **`read_message()` / `write_message()` / `send_message()`** — protocol encoding/decoding
- **`handle_message()`** — dispatches RUN, DESCRIBE, START_STREAM, STOP_STREAM, STREAM_DATA
- **`stream_resource()` / `check_streams()`** — periodic and event-driven streaming with echo
- **`send_keepalive()`** — keepalive management
- **`set_property()` / `get_property()` / `write_bucket()` / `call_endpoint()`** — server API
- **`set_state_callback()`** — connection state notifications
- **`disconnect()`** — clean shutdown with state notification

### Example: minimal client

```cpp
my_client thing("username", "device_id", "credential");
thing.set_host("iot.thinger.io");

thing["temperature"] = [](thinger::iotmp::output& out) {
    out["celsius"] = read_sensor();
};

thing["led"] << digitalPin(2);

// Call handle() repeatedly (from main loop or RTOS task)
while(true) {
    thing.handle();
}
```

## What the core provides

### Value type (`iotmp_value`)

Lightweight dynamic type (~12 bytes on 32-bit) supporting null, bool, integers, floats, strings, binary, arrays, and objects:

```cpp
iotmp_value obj;
obj["temperature"] = 23.5;
obj["name"] = "sensor1";

auto arr = iotmp_value::array({1, 2, 3});
auto bin = iotmp_value::binary({0x01, 0x02, 0x03});

std::string json = obj.dump();  // JSON serialization
auto parsed = iotmp_value::parse("{\"key\": 42}");  // JSON parsing
```

### Resources

```cpp
// Output (server reads from device)
thing["sensor"] = [](output& out) {
    out["celsius"] = read_temperature();
};

// Input (server writes to device)
thing["led"] = [](input& in) {
    static bool state = false;
    if(in.is_empty()) { in = state; }
    else { state = (bool)in; set_gpio(state); }
};

// Input/Output (bidirectional)
thing["config"] = [](input& in, output& out) {
    if(!in.is_empty()) threshold = in["value"].get<float>();
    out["value"] = threshold;
};

// Run (action, no data)
thing["reboot"] = []() { reboot(); };
```

### Convenience operators and macros

```cpp
thing["led"] << digitalPin(2);       // input with GPIO state tracking
thing["millis"] >> outputValue(millis());  // output expression
thing["A0"] >> analogPin(A0);        // analog reading
```

Each platform overrides `digitalPin` and `analogPin` with its GPIO functions. The core provides no-op defaults.

### Connection state

```cpp
thing.set_state_callback([](thinger::iotmp::client_state state) {
    switch(state) {
        case client_state::AUTHENTICATED: /* connected */ break;
        case client_state::SOCKET_DISCONNECTED: /* lost */ break;
    }
});
```

### [PSON](https://www.mdpi.com/1424-8220/21/13/4559) encoding

Compact binary serialization with template-based encoder/decoder:

```cpp
iotmp_value data;
data["temp"] = 23.5;

std::string buffer;
string_writer writer(buffer);
pson_encoder<string_writer> encoder(writer);
encoder.encode(data);
```

### Logging

Define `THINGER_LOG_*` macros before including `iotmp.hpp` to route logs to your platform:

```cpp
// Arduino
#define THINGER_LOG_INFO(fmt, ...) Serial.printf("[I] " fmt "\n", ##__VA_ARGS__)

// ESP-IDF
#define THINGER_LOG_INFO(fmt, ...) ESP_LOGI("iotmp", fmt, ##__VA_ARGS__)

// Zephyr
#define THINGER_LOG_INFO(fmt, ...) LOG_INF(fmt, ##__VA_ARGS__)
```

## Architecture

```
include/thinger/iotmp/
├── iotmp.hpp                 # Single include
└── core/
    ├── iotmp_client.hpp      # CRTP client base (full lifecycle)
    ├── iotmp_value.hpp       # Dynamic value type
    ├── iotmp_resource.hpp    # Resources + operators (>> <<)
    ├── iotmp_message.hpp     # Message model
    ├── iotmp_encoder.hpp     # IOTMP message encoder
    ├── iotmp_decoder.hpp     # IOTMP message decoder
    ├── pson_encoder.hpp      # PSON binary encoder
    ├── pson_decoder.hpp      # PSON binary decoder
    ├── pson_types.hpp        # Wire type enum
    ├── iotmp_adapters.hpp    # I/O adapters
    ├── iotmp_macros.hpp      # Platform macros
    ├── iotmp_log.hpp         # Logging macros
    └── iotmp_types.hpp       # Type aliases
```

## Tests

```bash
# Protocol tests (94 tests)
c++ -std=c++17 -I include -o test tests/native/test_protocol.cpp && ./test

# Client integration tests (51 tests, doctest)
c++ -std=c++17 -I include -I tests -o test tests/test_client.cpp && ./test
```

## References

- [PSON: A Serialization Format for IoT Sensor Networks](https://www.mdpi.com/1424-8220/21/13/4559) — Sensors 2021
- [Thinger.io Documentation](https://docs.thinger.io)

## License

MIT License — see [LICENSE](LICENSE) for details.
