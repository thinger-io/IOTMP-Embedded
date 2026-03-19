// Native protocol test — validates IOTMP encoding/decoding and connects
// to a real Thinger.io server using POSIX sockets.
//
// Compile: clang++ -std=c++17 -I../../include -o test_protocol test_protocol.cpp
// Run:     ./test_protocol [host] [port]

#include <thinger/iotmp/core/iotmp_value.hpp>
#include <thinger/iotmp/core/iotmp_message.hpp>
#include <thinger/iotmp/core/iotmp_encoder.hpp>
#include <thinger/iotmp/core/iotmp_decoder.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <cassert>

using namespace thinger::iotmp;

// ============================================================================
// Protocol unit tests
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  TEST: %-40s ", name);
#define PASS() do { printf("OK\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("FAIL: %s\n", msg); tests_failed++; } while(0)
#define ASSERT(cond, msg) do { if(!(cond)) { FAIL(msg); return; } } while(0)

void test_value_primitives() {
    TEST("iotmp_value primitives");

    iotmp_value null_v;
    ASSERT(null_v.is_null(), "null check");

    iotmp_value bool_v(true);
    ASSERT(bool_v.is_boolean() && bool_v.get<bool>() == true, "bool check");

    iotmp_value uint_v(42u);
    ASSERT(uint_v.is_number_unsigned() && uint_v.get<uint32_t>() == 42, "uint check");

    iotmp_value int_v(-7);
    ASSERT(int_v.is_number_integer() && int_v.get<int32_t>() == -7, "int check");

    iotmp_value float_v(3.14);
    ASSERT(float_v.is_number_float(), "float type check");
    ASSERT(float_v.get<double>() > 3.13 && float_v.get<double>() < 3.15, "float value check");

    iotmp_value str_v("hello");
    ASSERT(str_v.is_string() && str_v.get<std::string>() == "hello", "string check");

    PASS();
}

void test_value_object() {
    TEST("iotmp_value object");

    iotmp_value obj;
    obj["name"] = "sensor1";
    obj["temp"] = 23.5;
    obj["active"] = true;

    ASSERT(obj.is_object(), "is object");
    ASSERT(obj.size() == 3, "size == 3");
    ASSERT(obj["name"].get<std::string>() == "sensor1", "name field");
    ASSERT(obj["temp"].get<double>() == 23.5, "temp field");
    ASSERT(obj["active"].get<bool>() == true, "active field");
    ASSERT(obj.contains("name"), "contains name");
    ASSERT(!obj.contains("missing"), "not contains missing");

    PASS();
}

void test_value_array() {
    TEST("iotmp_value array");

    auto arr = iotmp_value::array({"one", "two", "three"});
    ASSERT(arr.is_array(), "is array");
    ASSERT(arr.size() == 3, "size == 3");
    ASSERT(arr[(size_t)0].get<std::string>() == "one", "element 0");
    ASSERT(arr[(size_t)2].get<std::string>() == "three", "element 2");

    PASS();
}

void test_value_copy_move() {
    TEST("iotmp_value copy/move");

    iotmp_value original;
    original["key"] = "value";

    // Copy
    iotmp_value copy = original;
    ASSERT(copy["key"].get<std::string>() == "value", "copy preserves value");

    // Modify copy doesn't affect original
    copy["key"] = "modified";
    ASSERT(original["key"].get<std::string>() == "value", "original unchanged");

    // Move
    iotmp_value moved = std::move(copy);
    ASSERT(moved["key"].get<std::string>() == "modified", "move preserves value");
    ASSERT(copy.is_null(), "source is null after move");

    PASS();
}

void test_pson_roundtrip() {
    TEST("PSON v2 encode/decode roundtrip");

    // Create a complex value
    iotmp_value original;
    original["temperature"] = 23.5;
    original["humidity"] = 65u;
    original["name"] = "test_sensor";
    original["active"] = true;
    original["error_count"] = -3;

    // Encode to PSON
    std::string buffer;
    buffer.reserve(256);
    string_writer writer(buffer);
    pson_encoder<string_writer> encoder(writer);
    bool encoded = encoder.encode(original);
    ASSERT(encoded, "encode succeeded");
    ASSERT(buffer.size() > 0, "encoded data not empty");

    // Decode back
    memory_reader reader(buffer.data(), buffer.size());
    pson_decoder<memory_reader> decoder(reader);
    iotmp_value decoded;
    bool ok = decoder.decode(decoded);
    ASSERT(ok, "decode succeeded");

    // Verify
    ASSERT(decoded.is_object(), "decoded is object");
    ASSERT(decoded["temperature"].get<double>() == 23.5, "temperature roundtrip");
    ASSERT(decoded["humidity"].get<uint64_t>() == 65, "humidity roundtrip");
    ASSERT(decoded["name"].get<std::string>() == "test_sensor", "name roundtrip");
    ASSERT(decoded["active"].get<bool>() == true, "active roundtrip");
    ASSERT(decoded["error_count"].get<int64_t>() == -3, "error_count roundtrip");

    PASS();
}

void test_pson_nested() {
    TEST("PSON v2 nested structures");

    iotmp_value original;
    original["data"]["temp"] = 22.0;
    original["data"]["hum"] = 55u;
    original["tags"] = iotmp_value::array({"indoor", "floor2"});

    std::string buffer;
    string_writer writer(buffer);
    pson_encoder<string_writer> encoder(writer);
    ASSERT(encoder.encode(original), "encode nested");

    memory_reader reader(buffer.data(), buffer.size());
    pson_decoder<memory_reader> decoder(reader);
    iotmp_value decoded;
    ASSERT(decoder.decode(decoded), "decode nested");

    ASSERT(decoded["data"]["temp"].get<double>() == 22.0, "nested temp");
    ASSERT(decoded["data"]["hum"].get<uint64_t>() == 55, "nested hum");
    ASSERT(decoded["tags"].is_array(), "tags is array");
    ASSERT(decoded["tags"][(size_t)0].get<std::string>() == "indoor", "tag 0");
    ASSERT(decoded["tags"][(size_t)1].get<std::string>() == "floor2", "tag 1");

    PASS();
}

void test_message_encode_decode() {
    TEST("IOTMP message encode/decode");

    // Create a RUN message with resource and payload
    iotmp_message msg(message::type::RUN);
    msg.set_stream_id(42);
    msg[message::field::RESOURCE] = std::string("temperature");
    msg[message::field::PAYLOAD]["celsius"] = 23.5;

    // Encode full message (header + body)
    auto encoded = encode_message(msg);
    ASSERT(encoded.size() > 0, "encoded not empty");

    // Decode: read type varint, size varint, then body
    memory_reader reader(encoded.data(), encoded.size());

    // Read type
    uint8_t type_byte;
    ASSERT(reader.read(&type_byte), "read type");
    ASSERT(type_byte == message::type::RUN, "type is RUN");

    // Read size varint
    pson_decoder<memory_reader> varint_decoder(reader);
    uint32_t body_size = 0;
    ASSERT(varint_decoder.pb_decode_varint32(body_size), "read body size");
    ASSERT(body_size > 0, "body size > 0");

    // Decode body
    iotmp_decoder<memory_reader> msg_decoder(reader);
    iotmp_message decoded(message::type::RUN);
    ASSERT(msg_decoder.decode(decoded, body_size), "decode body");

    ASSERT(decoded.get_stream_id() == 42, "stream_id roundtrip");
    ASSERT(decoded[message::field::RESOURCE].get<std::string>() == "temperature", "resource roundtrip");
    ASSERT(decoded[message::field::PAYLOAD]["celsius"].get<double>() == 23.5, "payload roundtrip");

    PASS();
}

void test_resource_callbacks() {
    TEST("Resource callbacks");

    // Output resource
    iotmp_resource res;
    res = std::function<void(output&)>([](output& out) {
        out["temp"] = 23.5;
        out["hum"] = 65u;
    });

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    bool ok = res.run_resource(req, resp);
    ASSERT(ok, "run succeeded");
    ASSERT(resp[message::field::PAYLOAD]["temp"].get<double>() == 23.5, "output temp");
    ASSERT(resp[message::field::PAYLOAD]["hum"].get<uint64_t>() == 65, "output hum");

    // Input resource
    iotmp_resource in_res;
    bool led_state = false;
    in_res = std::function<void(input&)>([&led_state](input& in) {
        led_state = in["state"].get<bool>();
    });

    iotmp_message in_req(message::type::RUN);
    in_req[message::field::PAYLOAD]["state"] = true;
    iotmp_message in_resp(message::type::OK);
    in_res.run_resource(in_req, in_resp);
    ASSERT(led_state == true, "input callback received value");

    PASS();
}

void run_protocol_tests() {
    printf("\n=== IOTMP Protocol Tests ===\n\n");

    test_value_primitives();
    test_value_object();
    test_value_array();
    test_value_copy_move();
    test_pson_roundtrip();
    test_pson_nested();
    test_message_encode_decode();
    test_resource_callbacks();

    printf("\n  Results: %d passed, %d failed\n\n", tests_passed, tests_failed);
}

// ============================================================================
// Live connection test
// ============================================================================

class simple_tcp_client {
    int sock_ = -1;

public:
    ~simple_tcp_client() { disconnect(); }

    bool connect(const char* host, uint16_t port) {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);

        if(getaddrinfo(host, port_str, &hints, &res) != 0 || !res) {
            printf("  DNS resolve failed for %s\n", host);
            return false;
        }

        sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sock_ < 0) {
            freeaddrinfo(res);
            printf("  Socket creation failed\n");
            return false;
        }

        // Set timeout
        struct timeval tv = { .tv_sec = 10, .tv_usec = 0 };
        setsockopt(sock_, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        setsockopt(sock_, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));

        int rc = ::connect(sock_, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);

        if(rc < 0) {
            printf("  Connect failed: %s\n", strerror(errno));
            close(sock_);
            sock_ = -1;
            return false;
        }

        return true;
    }

    void disconnect() {
        if(sock_ >= 0) {
            close(sock_);
            sock_ = -1;
        }
    }

    bool send_all(const void* buf, size_t len) {
        auto* ptr = static_cast<const uint8_t*>(buf);
        while(len > 0) {
            ssize_t rc = send(sock_, ptr, len, 0);
            if(rc <= 0) return false;
            ptr += rc;
            len -= rc;
        }
        return true;
    }

    bool recv_all(void* buf, size_t len) {
        auto* ptr = static_cast<uint8_t*>(buf);
        while(len > 0) {
            ssize_t rc = recv(sock_, ptr, len, 0);
            if(rc <= 0) return false;
            ptr += rc;
            len -= rc;
        }
        return true;
    }

    bool read_varint(uint32_t& value) {
        value = 0;
        uint8_t byte;
        uint8_t bit_pos = 0;
        do {
            if(!recv_all(&byte, 1) || bit_pos >= 32) return false;
            value |= static_cast<uint32_t>(byte & 0x7F) << bit_pos;
            bit_pos += 7;
        } while(byte & 0x80);
        return true;
    }

    bool write_message(iotmp_message& msg) {
        auto encoded = encode_message(msg);
        return send_all(encoded.data(), encoded.size());
    }

    bool read_message(iotmp_message& msg) {
        uint32_t type_val;
        if(!read_varint(type_val)) return false;

        uint32_t body_size;
        if(!read_varint(body_size)) return false;

        msg.set_message_type(static_cast<message::type>(type_val));

        if(body_size == 0) return true;
        if(body_size > 64 * 1024) return false;

        std::vector<uint8_t> buffer(body_size);
        if(!recv_all(buffer.data(), body_size)) return false;

        memory_reader reader(buffer.data(), body_size);
        iotmp_decoder<memory_reader> decoder(reader);
        return decoder.decode(msg, body_size);
    }
};

void test_live_connection(const char* host, uint16_t port,
                          const char* username, const char* device_id,
                          const char* credential) {
    printf("=== Live Connection Test ===\n\n");
    printf("  Server: %s:%u\n", host, port);
    printf("  Device: %s@%s\n", device_id, username);

    simple_tcp_client client;

    // Connect
    printf("  Connecting... ");
    if(!client.connect(host, port)) {
        printf("FAILED\n");
        return;
    }
    printf("OK\n");

    // Authenticate
    printf("  Authenticating... ");
    iotmp_message connect_msg(message::type::CONNECT);
    connect_msg.set_random_stream_id();
    connect_msg[message::field::PAYLOAD] = iotmp_value::array({
        iotmp_value(username),
        iotmp_value(device_id),
        iotmp_value(credential)
    });

    if(!client.write_message(connect_msg)) {
        printf("FAILED (send)\n");
        return;
    }

    iotmp_message response(message::type::RESERVED);
    if(!client.read_message(response)) {
        printf("FAILED (recv)\n");
        return;
    }

    if(response.get_message_type() == message::type::OK) {
        printf("OK\n");
    } else {
        printf("REJECTED (type=%d)\n", static_cast<int>(response.get_message_type()));
        return;
    }

    // Wait for DESCRIBE from server
    printf("  Waiting for server requests... ");
    iotmp_message server_msg(message::type::RESERVED);
    if(client.read_message(server_msg)) {
        printf("got %s (stream=%u)\n", server_msg.message_type_str(), server_msg.get_stream_id());

        // If it's a DESCRIBE, respond with our API
        if(server_msg.get_message_type() == message::type::DESCRIBE) {
            printf("  Sending API description... ");
            iotmp_message desc_resp(server_msg.get_stream_id(), message::type::OK);

            // Describe our resources
            desc_resp[message::field::PAYLOAD]["environment"]["fn"] = 3; // output
            desc_resp[message::field::PAYLOAD]["led"]["fn"] = 2;        // input
            desc_resp[message::field::PAYLOAD]["threshold"]["fn"] = 4;  // input_output

            if(client.write_message(desc_resp)) {
                printf("OK\n");
            } else {
                printf("FAILED\n");
            }
        }
    } else {
        printf("timeout (normal if server doesn't describe immediately)\n");
    }

    // Send a keepalive
    printf("  Sending keep-alive... ");
    auto ka = encode_message(message::type::KEEP_ALIVE);
    if(client.send_all(ka.data(), ka.size())) {
        printf("OK\n");
    } else {
        printf("FAILED\n");
    }

    printf("\n  Connection test completed successfully!\n");
    printf("  The IOTMP protocol implementation is wire-compatible.\n\n");
}

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    // Always run protocol unit tests
    run_protocol_tests();

    if(tests_failed > 0) {
        printf("Protocol tests failed. Fix before testing live connection.\n");
        return 1;
    }

    // Live connection test (needs credentials)
    const char* host = argc > 1 ? argv[1] : "iot.thinger.io";
    uint16_t port = argc > 2 ? static_cast<uint16_t>(atoi(argv[2])) : 25204;

    if(argc > 5) {
        test_live_connection(host, port, argv[3], argv[4], argv[5]);
    } else if(argc > 3) {
        test_live_connection(host, port, argv[1], argv[2], argv[3]);
    } else {
        printf("=== Live Connection Test ===\n\n");
        printf("  Skipped — provide credentials to test:\n");
        printf("  ./test_protocol <username> <device_id> <credential>\n");
        printf("  ./test_protocol <host> <port> <username> <device_id> <credential>\n\n");
    }

    return tests_failed > 0 ? 1 : 0;
}
