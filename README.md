# IOTMP-Embedded

Portable, header-only C++17 implementation of the [IOTMP](https://docs.thinger.io) (IoT Message Protocol) core for embedded platforms.

This library provides the protocol foundation (encoding, decoding, message framing, and resource model) used by platform-specific IOTMP client libraries such as [IOTMP-Zephyr](https://github.com/thinger-io/IOTMP-Zephyr) and [IOTMP-Arduino](https://github.com/thinger-io/iotmp-arduino).

## Features

- **[PSON](https://www.mdpi.com/1424-8220/21/13/4559)** binary encoding/decoding — compact, self-describing serialization format for IoT
- **IOTMP message framing** — protobuf-style varint fields with support for all message types (CONNECT, RUN, DESCRIBE, START_STREAM, STOP_STREAM, STREAM_DATA, KEEP_ALIVE, etc.)
- **Lightweight value type** (`iotmp_value`) — dynamic JSON-like data model at ~12 bytes per value on 32-bit targets
- **Resource model** — register input, output, and input/output callbacks with `operator=`
- **Header-only** — no compilation needed, just add the include path
- **Zero platform dependencies** — pure C++17, works on any platform with a conforming compiler

## Usage

### As a dependency (recommended)

This library is designed to be consumed by platform-specific IOTMP client libraries:

| Platform | Repository |
|----------|------------|
| Zephyr RTOS | [IOTMP-Zephyr](https://github.com/thinger-io/IOTMP-Zephyr) |
| Arduino / ESP32 | [IOTMP-Arduino](https://github.com/thinger-io/iotmp-arduino) |

### Direct usage

Add the `include/` directory to your compiler's include path:

```cpp
#include <thinger/iotmp/iotmp.hpp>
```

### Zephyr module

This library includes a `zephyr/module.yml` so it can be used as a Zephyr module. When added to a Zephyr workspace (via `west.yml`), it automatically exposes its include directories to dependent modules.

## API overview

### iotmp_value

A lightweight, dynamic value type that can hold null, bool, integers, floats, strings, binary data, arrays, and objects:

```cpp
using namespace thinger::iotmp;

// Primitives
iotmp_value null_val;
iotmp_value flag = true;
iotmp_value count = 42u;
iotmp_value temp = 23.5;
iotmp_value name = "sensor1";

// Objects (auto-promoted from null)
iotmp_value obj;
obj["temperature"] = 23.5;
obj["humidity"] = 65u;
obj["name"] = "indoor";

// Arrays
auto arr = iotmp_value::array({1, 2, 3});

// Binary
auto bin = iotmp_value::binary({0x01, 0x02, 0x03});
```

### Resources

Define device resources with input/output callbacks. The typical usage is via a client instance:

```cpp
// Output resource (sensor — server reads from device)
thing["sensor"] = [](output& out) {
    out["celsius"] = read_temperature();
};

// Input resource (actuator — server writes to device)
thing["led"] = [](input& in) {
    set_led(in["state"].get<bool>());
};

// Input/Output resource (bidirectional)
thing["config"] = [](input& in, output& out) {
    if(!in.is_empty()) threshold = in["value"].get<float>();
    out["value"] = threshold;
};

// Run resource (action, no data)
thing["reboot"] = []() { reboot(); };
```

### PSON encoding/decoding

```cpp
// Encode
iotmp_value data;
data["temp"] = 23.5;
data["name"] = "test";

std::string buffer;
string_writer writer(buffer);
pson_encoder<string_writer> encoder(writer);
encoder.encode(data);

// Decode
memory_reader reader(buffer.data(), buffer.size());
pson_decoder<memory_reader> decoder(reader);
iotmp_value result;
decoder.decode(result);
```

## Building tests

```bash
# Using CMake
mkdir build && cd build
cmake ..
make
./test_protocol

# Or directly
cd tests/native
c++ -std=c++17 -I../../include -o test_protocol test_protocol.cpp
./test_protocol
```

## Architecture

```
include/thinger/iotmp/core/
├── iotmp_value.hpp       # Lightweight dynamic value type
├── pson_types.hpp        # PSON wire type enum
├── pson_encoder.hpp      # PSON binary encoder (template)
├── pson_decoder.hpp      # PSON binary decoder (template)
├── iotmp_encoder.hpp     # IOTMP message encoder
├── iotmp_decoder.hpp     # IOTMP message decoder
├── iotmp_message.hpp     # Message model (types, fields, framing)
├── iotmp_resource.hpp    # Resource model (input/output callbacks)
├── iotmp_adapters.hpp    # I/O adapters (memory, string, null writers)
└── iotmp_types.hpp       # Common type aliases
```

The encoders and decoders are **template-based** on Writer/Reader types, allowing zero-copy operation with any I/O backend (memory buffers, sockets, streams).

## References

- [PSON: A Serialization Format for IoT Sensor Networks](https://www.mdpi.com/1424-8220/21/13/4559) — Sensors 2021
- [Thinger.io Documentation](https://docs.thinger.io)

## License

MIT License — see [LICENSE](LICENSE) for details.
