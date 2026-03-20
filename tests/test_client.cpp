// Comprehensive integration tests for the IOTMP client base logic.
//
// Copyright (c) INTERNET OF THINGER SL
//
// Uses the doctest single-header testing framework.
// Compile: c++ -std=c++17 -I../include -I. -o test_client test_client.cpp
// Run:     ./test_client

#define DOCTEST_CONFIG_IMPLEMENT_WITH_MAIN
#include "doctest.h"

#include <thinger/iotmp/core/iotmp_client.hpp>

#include <string>
#include <vector>
#include <cstring>

using namespace thinger::iotmp;

// ============================================================================
// Mock transport client using CRTP
// ============================================================================

class mock_client : public iotmp_client_base<mock_client> {
public:
    mock_client()
        : iotmp_client_base("test_user", "test_device", "test_credential") {}

    // Buffers for simulating I/O
    std::string tx_buffer;   // what the client sends
    std::string rx_buffer;   // what the client reads (simulated server responses)
    size_t rx_pos = 0;
    bool connected = true;

    // CRTP implementations
    bool send_bytes_impl(const void* data, size_t len) {
        tx_buffer.append(static_cast<const char*>(data), len);
        return true;
    }

    bool recv_bytes_impl(void* buf, size_t len) {
        if(rx_pos + len > rx_buffer.size()) return false;
        memcpy(buf, rx_buffer.data() + rx_pos, len);
        rx_pos += len;
        return true;
    }

    bool is_connected_impl() const { return connected; }
    bool data_available_impl() { return rx_pos < rx_buffer.size(); }
    unsigned long get_millis() const { return millis_value; }
    bool connect_impl() { return connected; }
    void disconnect_impl() { connected = false; }

    // Test helpers
    unsigned long millis_value = 0;

    void set_connected(bool v) { connected_ = v; }

    void reset() {
        tx_buffer.clear();
        rx_buffer.clear();
        rx_pos = 0;
        connected = true;
    }

    // Queue a server response message into rx_buffer
    void queue_response(iotmp_message& msg) {
        std::string encoded = encode_message(msg);
        rx_buffer.append(encoded);
    }

    void queue_response(message::type type) {
        std::string encoded = encode_message(type);
        rx_buffer.append(encoded);
    }

    // Queue an OK response with a specific stream_id
    void queue_ok(uint16_t stream_id = 0) {
        iotmp_message msg(stream_id, message::OK);
        queue_response(msg);
    }

    // Decode all messages sent by the client from tx_buffer
    std::vector<iotmp_message> get_sent_messages() {
        std::vector<iotmp_message> messages;
        const uint8_t* data = reinterpret_cast<const uint8_t*>(tx_buffer.data());
        size_t remaining = tx_buffer.size();
        size_t pos = 0;

        while(pos < remaining) {
            memory_reader reader(data + pos, remaining - pos);

            // Read type varint
            pson_decoder<memory_reader> varint_dec(reader);
            uint32_t type_val = 0;
            if(!varint_dec.pb_decode_varint32(type_val)) break;

            // Read body size varint
            uint32_t body_size = 0;
            if(!varint_dec.pb_decode_varint32(body_size)) break;

            iotmp_message msg(static_cast<message::type>(type_val));

            if(body_size > 0) {
                size_t header_bytes = reader.bytes_read();
                memory_reader body_reader(data + pos + header_bytes, body_size);
                iotmp_decoder<memory_reader> decoder(body_reader);
                decoder.decode(msg, body_size);
                pos += header_bytes + body_size;
            } else {
                pos += reader.bytes_read();
            }

            messages.push_back(std::move(msg));
        }
        return messages;
    }
};

// ============================================================================
// Authentication tests
// ============================================================================

TEST_CASE("authenticate sends a CONNECT message") {
    mock_client client;
    // Queue an OK response so authenticate() can read it
    client.queue_ok();

    client.authenticate();

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].get_message_type() == message::CONNECT);
}

TEST_CASE("CONNECT message has credentials as array in PAYLOAD") {
    mock_client client;
    client.queue_ok();

    client.authenticate();

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    REQUIRE(sent[0].has_payload());

    const json_t& payload = sent[0].payload();
    REQUIRE(payload.is_array());
    REQUIRE(payload.size() == 3);
    CHECK(payload[size_t(0)].get<std::string>() == "test_user");
    CHECK(payload[size_t(1)].get<std::string>() == "test_device");
    CHECK(payload[size_t(2)].get<std::string>() == "test_credential");
}

TEST_CASE("authenticate returns true on OK response") {
    mock_client client;
    client.queue_ok();
    CHECK(client.authenticate() == true);
}

TEST_CASE("authenticate returns false on ERROR response") {
    mock_client client;
    iotmp_message error_resp(0, message::ERROR);
    client.queue_response(error_resp);
    CHECK(client.authenticate() == false);
}

TEST_CASE("authenticate returns false when no response available") {
    mock_client client;
    // rx_buffer is empty - no server response
    CHECK(client.authenticate() == false);
}

// ============================================================================
// Resource registration tests
// ============================================================================

TEST_CASE("operator[] creates resources") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 23.5;
    };
    CHECK(client.find_resource("temperature") != nullptr);
}

TEST_CASE("Resources are findable by name") {
    mock_client client;
    client["led"] = [](input& /*in*/) {
        // no-op
    };
    client["sensor"] = [](output& out) {
        out["value"] = 42;
    };

    CHECK(client.find_resource("led") != nullptr);
    CHECK(client.find_resource("sensor") != nullptr);
    CHECK(client.find_resource("nonexistent") == nullptr);
}

TEST_CASE("Multiple resource types can be registered") {
    mock_client client;

    // run resource (no I/O)
    bool run_called = false;
    client["action"] = [&run_called]() {
        run_called = true;
    };

    // input resource
    client["led"] = [](input& /*in*/) {};

    // output resource
    client["temp"] = [](output& /*out*/) {};

    // input/output resource
    client["relay"] = [](input& /*in*/, output& /*out*/) {};

    CHECK(client.find_resource("action")->get_io_type() == iotmp_resource::run);
    CHECK(client.find_resource("led")->get_io_type() == iotmp_resource::input_wrapper);
    CHECK(client.find_resource("temp")->get_io_type() == iotmp_resource::output_wrapper);
    CHECK(client.find_resource("relay")->get_io_type() == iotmp_resource::input_output_wrapper);
}

// ============================================================================
// Message handling - RUN tests
// ============================================================================

TEST_CASE("RUN with output resource returns payload in response") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 23.5;
    };

    // Create a RUN message as if server sent it
    iotmp_message request(message::RUN);
    request.set_stream_id(42);
    request[message::field::RESOURCE] = std::string("temperature");

    // Queue it and process
    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    // Check response
    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 42);
    REQUIRE(sent[0].has_payload());
    CHECK(sent[0].payload()["celsius"].get<double>() == doctest::Approx(23.5));
}

TEST_CASE("RUN with input resource executes callback") {
    mock_client client;
    bool callback_called = false;
    int received_value = 0;
    client["led"] = [&](input& in) {
        callback_called = true;
        received_value = in["state"].get<int>();
    };

    // Create a RUN message with input payload
    iotmp_message request(message::RUN);
    request.set_stream_id(10);
    request[message::field::RESOURCE] = std::string("led");
    request[message::field::PAYLOAD]["state"] = 1;

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    CHECK(callback_called == true);
    CHECK(received_value == 1);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 10);
}

TEST_CASE("RUN with input on streamed resource triggers stream echo") {
    mock_client client;
    int led_state = 0;
    client["led"] = [&](input& in) {
        if(!in.is_empty()) {
            led_state = in["state"].get<int>();
        }
    };

    // First, set up a stream on the resource
    auto* res = client.find_resource("led");
    REQUIRE(res != nullptr);
    res->set_stream_id(100);
    res->set_stream_echo(true);

    // Register the stream in the streams_ map by simulating START_STREAM
    iotmp_message start_msg(message::START_STREAM);
    start_msg.set_stream_id(100);
    start_msg[message::field::RESOURCE] = std::string("led");
    client.queue_response(start_msg);
    iotmp_message start_incoming(message::RESERVED);
    REQUIRE(client.read_message(start_incoming));
    client.handle_message(start_incoming);
    client.tx_buffer.clear();  // Clear the OK + initial stream data

    // Now send a RUN with payload
    iotmp_message request(message::RUN);
    request.set_stream_id(55);
    request[message::field::RESOURCE] = std::string("led");
    request[message::field::PAYLOAD]["state"] = 1;

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    CHECK(led_state == 1);

    // Should have sent OK + stream echo
    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 55);
    // Second message is the stream echo
    CHECK(sent[1].get_message_type() == message::STREAM_DATA);
    CHECK(sent[1].get_stream_id() == 100);
}

TEST_CASE("RUN with unknown resource returns ERROR") {
    mock_client client;

    iotmp_message request(message::RUN);
    request.set_stream_id(99);
    request[message::field::RESOURCE] = std::string("nonexistent");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::ERROR);
    CHECK(sent[0].get_stream_id() == 99);
}

TEST_CASE("RUN with run-type resource (no I/O) executes callback") {
    mock_client client;
    bool action_triggered = false;
    client["reboot"] = [&action_triggered]() {
        action_triggered = true;
    };

    iotmp_message request(message::RUN);
    request.set_stream_id(7);
    request[message::field::RESOURCE] = std::string("reboot");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    CHECK(action_triggered == true);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
}

TEST_CASE("RUN with input_output resource passes both in and out") {
    mock_client client;
    client["compute"] = [](input& in, output& out) {
        int val = in["x"].get<int>();
        out["result"] = val * 2;
    };

    iotmp_message request(message::RUN);
    request.set_stream_id(33);
    request[message::field::RESOURCE] = std::string("compute");
    request[message::field::PAYLOAD]["x"] = 21;

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 33);
    REQUIRE(sent[0].has_payload());
    CHECK(sent[0].payload()["result"].get<int>() == 42);
}

// ============================================================================
// Message handling - DESCRIBE tests
// ============================================================================

TEST_CASE("DESCRIBE without resource returns full API") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 0.0;
    };
    client["led"] = [](input& /*in*/) {
        // no-op
    };

    iotmp_message request(message::DESCRIBE);
    request.set_stream_id(20);

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 20);
    REQUIRE(sent[0].has_payload());

    // Payload should contain entries for "temperature" and "led"
    const json_t& payload = sent[0].payload();
    CHECK(payload.contains("temperature"));
    CHECK(payload.contains("led"));
}

TEST_CASE("DESCRIBE with resource returns resource description") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 0.0;
    };

    iotmp_message request(message::DESCRIBE);
    request.set_stream_id(21);
    request[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    REQUIRE(sent[0].has_payload());

    // Should have an "out" field describing the output schema
    const json_t& payload = sent[0].payload();
    CHECK(payload.contains("out"));
}

TEST_CASE("DESCRIBE with unknown resource returns ERROR") {
    mock_client client;

    iotmp_message request(message::DESCRIBE);
    request.set_stream_id(22);
    request[message::field::RESOURCE] = std::string("nonexistent");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::ERROR);
}

// ============================================================================
// Message handling - START_STREAM / STOP_STREAM tests
// ============================================================================

TEST_CASE("START_STREAM registers a stream and sends OK") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };

    iotmp_message request(message::START_STREAM);
    request.set_stream_id(200);
    request[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    // Should have OK + initial stream data
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 200);

    // Resource should now have stream enabled
    auto* res = client.find_resource("temperature");
    REQUIRE(res != nullptr);
    CHECK(res->stream_enabled() == true);
    CHECK(res->get_stream_id() == 200);
}

TEST_CASE("START_STREAM sends initial value") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 25.0;
    };

    iotmp_message request(message::START_STREAM);
    request.set_stream_id(201);
    request[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 2);

    // Second message should be STREAM_DATA with the initial value
    CHECK(sent[1].get_message_type() == message::STREAM_DATA);
    CHECK(sent[1].get_stream_id() == 201);
    REQUIRE(sent[1].has_payload());
    CHECK(sent[1].payload()["celsius"].get<double>() == doctest::Approx(25.0));
}

TEST_CASE("STOP_STREAM removes a stream") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };

    // First start the stream
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(300);
    start_req[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(start_req);
    iotmp_message start_incoming(message::RESERVED);
    REQUIRE(client.read_message(start_incoming));
    client.handle_message(start_incoming);

    auto* res = client.find_resource("temperature");
    REQUIRE(res != nullptr);
    CHECK(res->stream_enabled() == true);

    client.tx_buffer.clear();

    // Now stop the stream
    iotmp_message stop_req(message::STOP_STREAM);
    stop_req.set_stream_id(300);

    client.queue_response(stop_req);
    iotmp_message stop_incoming(message::RESERVED);
    REQUIRE(client.read_message(stop_incoming));
    client.handle_message(stop_incoming);

    // Resource should no longer have stream enabled
    CHECK(res->stream_enabled() == false);
    CHECK(res->get_stream_id() == 0);

    // Should have sent an OK for the STOP_STREAM
    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 300);
}

TEST_CASE("START_STREAM with unknown resource returns ERROR") {
    mock_client client;

    iotmp_message request(message::START_STREAM);
    request.set_stream_id(400);
    request[message::field::RESOURCE] = std::string("nonexistent");

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::ERROR);
    CHECK(sent[0].get_stream_id() == 400);
}

TEST_CASE("START_STREAM with interval stores sampling interval") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };

    iotmp_message request(message::START_STREAM);
    request.set_stream_id(500);
    request[message::field::RESOURCE] = std::string("temperature");
    request[message::field::PARAMETERS] = 1000u;  // 1000ms interval

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 2);
    CHECK(sent[0].get_message_type() == message::OK);
}

// ============================================================================
// Message handling - STREAM_DATA tests
// ============================================================================

TEST_CASE("STREAM_DATA executes resource with input") {
    mock_client client;
    int led_value = 0;
    client["led"] = [&led_value](input& in) {
        if(!in.is_empty()) {
            led_value = in["state"].get<int>();
        }
    };

    // Start a stream first
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(600);
    start_req[message::field::RESOURCE] = std::string("led");

    client.queue_response(start_req);
    iotmp_message start_incoming(message::RESERVED);
    REQUIRE(client.read_message(start_incoming));
    client.handle_message(start_incoming);
    client.tx_buffer.clear();

    // Now send STREAM_DATA
    iotmp_message data_msg(message::STREAM_DATA);
    data_msg.set_stream_id(600);
    data_msg[message::field::PAYLOAD]["state"] = 1;

    client.queue_response(data_msg);
    iotmp_message data_incoming(message::RESERVED);
    REQUIRE(client.read_message(data_incoming));
    client.handle_message(data_incoming);

    CHECK(led_value == 1);
}

TEST_CASE("STREAM_DATA triggers echo on streamed resource") {
    mock_client client;
    int led_value = 0;
    client["led"] = [&led_value](input& in) {
        if(!in.is_empty()) {
            led_value = in["state"].get<int>();
        }
    };

    auto* res = client.find_resource("led");
    REQUIRE(res != nullptr);
    res->set_stream_echo(true);

    // Start a stream
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(700);
    start_req[message::field::RESOURCE] = std::string("led");

    client.queue_response(start_req);
    iotmp_message start_incoming(message::RESERVED);
    REQUIRE(client.read_message(start_incoming));
    client.handle_message(start_incoming);
    client.tx_buffer.clear();

    // Send STREAM_DATA with a new value
    iotmp_message data_msg(message::STREAM_DATA);
    data_msg.set_stream_id(700);
    data_msg[message::field::PAYLOAD]["state"] = 1;

    client.queue_response(data_msg);
    iotmp_message data_incoming(message::RESERVED);
    REQUIRE(client.read_message(data_incoming));
    client.handle_message(data_incoming);

    CHECK(led_value == 1);

    // Should have sent an echo STREAM_DATA back
    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::STREAM_DATA);
    CHECK(sent[0].get_stream_id() == 700);
}

TEST_CASE("STREAM_DATA with unknown stream_id is silently ignored") {
    mock_client client;
    client["led"] = [](input& /*in*/) {};

    iotmp_message data_msg(message::STREAM_DATA);
    data_msg.set_stream_id(9999);
    data_msg[message::field::PAYLOAD]["state"] = 1;

    client.queue_response(data_msg);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    // No response should be sent
    auto sent = client.get_sent_messages();
    CHECK(sent.size() == 0);
}

// ============================================================================
// Stream resource tests
// ============================================================================

TEST_CASE("stream_resource on output resource sends current value") {
    mock_client client;
    double temp = 23.5;
    client["temperature"] = [&temp](output& out) {
        out["celsius"] = temp;
    };

    auto* res = client.find_resource("temperature");
    REQUIRE(res != nullptr);

    client.stream_resource(*res, 42);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].get_message_type() == message::STREAM_DATA);
    CHECK(sent[0].get_stream_id() == 42);
    REQUIRE(sent[0].has_payload());
    CHECK(sent[0].payload()["celsius"].get<double>() == doctest::Approx(23.5));
}

TEST_CASE("stream_resource on input resource sends current state") {
    mock_client client;
    int led_state = 0;
    client["led"] = [&led_state](input& in) {
        // When called for streaming, in will be empty; we write current state into it
        if(in.is_empty()) {
            in["state"] = led_state;
        } else {
            led_state = in["state"].get<int>();
        }
    };

    auto* res = client.find_resource("led");
    REQUIRE(res != nullptr);

    led_state = 1;
    client.stream_resource(*res, 55);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].get_message_type() == message::STREAM_DATA);
    CHECK(sent[0].get_stream_id() == 55);
}

TEST_CASE("stream_resource returns false for run-type resources") {
    mock_client client;
    client["action"] = []() {};

    auto* res = client.find_resource("action");
    REQUIRE(res != nullptr);

    CHECK(client.stream_resource(*res, 42) == false);
}

TEST_CASE("stream_resource returns false for none-type resources") {
    mock_client client;
    // Create resource but do not assign any callback
    client["empty"];

    auto* res = client.find_resource("empty");
    REQUIRE(res != nullptr);
    CHECK(res->get_io_type() == iotmp_resource::none);
    CHECK(client.stream_resource(*res, 42) == false);
}

// ============================================================================
// Write/Read message roundtrip tests
// ============================================================================

TEST_CASE("write_message encodes and read_message decodes correctly") {
    mock_client client;

    // Create a message, write it, then read it back
    iotmp_message original(message::RUN);
    original.set_stream_id(123);
    original[message::field::RESOURCE] = std::string("test_resource");
    original[message::field::PAYLOAD]["value"] = 42;

    // Write the message (goes to tx_buffer)
    client.write_message(original);

    // Copy tx_buffer to rx_buffer and read it back
    client.rx_buffer = client.tx_buffer;
    client.rx_pos = 0;

    iotmp_message decoded(message::RESERVED);
    REQUIRE(client.read_message(decoded));

    CHECK(decoded.get_message_type() == message::RUN);
    CHECK(decoded.get_stream_id() == 123);
    REQUIRE(decoded.has_field(message::field::RESOURCE));
    CHECK(decoded[message::field::RESOURCE].get<std::string>() == "test_resource");
    REQUIRE(decoded.has_payload());
    CHECK(decoded.payload()["value"].get<int>() == 42);
}

TEST_CASE("write_message with empty body encodes correctly") {
    mock_client client;

    iotmp_message msg(message::OK);
    client.write_message(msg);

    // Read it back
    client.rx_buffer = client.tx_buffer;
    client.rx_pos = 0;

    iotmp_message decoded(message::RESERVED);
    REQUIRE(client.read_message(decoded));
    CHECK(decoded.get_message_type() == message::OK);
}

TEST_CASE("read_message fails on empty buffer") {
    mock_client client;
    // rx_buffer is empty

    iotmp_message msg(message::RESERVED);
    CHECK(client.read_message(msg) == false);
}

TEST_CASE("Multiple messages roundtrip") {
    mock_client client;

    // Write two messages
    iotmp_message msg1(message::RUN);
    msg1.set_stream_id(1);
    msg1[message::field::RESOURCE] = std::string("res1");
    client.write_message(msg1);

    iotmp_message msg2(message::DESCRIBE);
    msg2.set_stream_id(2);
    client.write_message(msg2);

    // Read both back
    client.rx_buffer = client.tx_buffer;
    client.rx_pos = 0;

    iotmp_message dec1(message::RESERVED);
    REQUIRE(client.read_message(dec1));
    CHECK(dec1.get_message_type() == message::RUN);
    CHECK(dec1.get_stream_id() == 1);

    iotmp_message dec2(message::RESERVED);
    REQUIRE(client.read_message(dec2));
    CHECK(dec2.get_message_type() == message::DESCRIBE);
    CHECK(dec2.get_stream_id() == 2);
}

// ============================================================================
// Keepalive tests
// ============================================================================

TEST_CASE("send_keepalive sends KEEP_ALIVE message") {
    mock_client client;
    client.send_keepalive();

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].get_message_type() == message::KEEP_ALIVE);
}

TEST_CASE("handle_message with KEEP_ALIVE does not send response") {
    mock_client client;

    iotmp_message ka(message::KEEP_ALIVE);
    client.queue_response(ka);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    CHECK(sent.size() == 0);
}

// ============================================================================
// Disconnect handling tests
// ============================================================================

TEST_CASE("handle_message with DISCONNECT triggers disconnect") {
    mock_client client;
    CHECK(client.connected == true);
    CHECK(client.is_connected() == false); // connected_ (base) starts false

    // Simulate being connected at the protocol level too
    client.set_connected(true);
    CHECK(client.is_connected() == true);

    client.queue_response(message::DISCONNECT);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    CHECK(client.connected == false);      // disconnect_impl() called
    CHECK(client.is_connected() == false);  // base connected_ cleared
}

// ============================================================================
// Configuration tests
// ============================================================================

TEST_CASE("Credentials are stored correctly") {
    mock_client client;
    CHECK(std::string(client.get_username()) == "test_user");
    CHECK(std::string(client.get_device_id()) == "test_device");
    CHECK(std::string(client.get_credential()) == "test_credential");
}

TEST_CASE("set_credentials updates all fields") {
    mock_client client;
    client.set_credentials("new_user", "new_device", "new_cred");
    CHECK(std::string(client.get_username()) == "new_user");
    CHECK(std::string(client.get_device_id()) == "new_device");
    CHECK(std::string(client.get_credential()) == "new_cred");
}

TEST_CASE("set_host updates host and port") {
    mock_client client;
    client.set_host("example.com", 8080);
    CHECK(std::string(client.get_host()) == "example.com");
    CHECK(client.get_port() == 8080);
}

TEST_CASE("Default host and port") {
    mock_client client;
    CHECK(std::string(client.get_host()) == "iot.thinger.io");
    CHECK(client.get_port() == 25204);
}

// ============================================================================
// clear_streams tests
// ============================================================================

TEST_CASE("clear_streams resets all stream state") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };

    // Start a stream
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(800);
    start_req[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(start_req);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto* res = client.find_resource("temperature");
    REQUIRE(res != nullptr);
    CHECK(res->stream_enabled() == true);

    client.clear_streams();

    CHECK(res->stream_enabled() == false);
    CHECK(res->get_stream_id() == 0);
}

// ============================================================================
// check_streams (interval-based streaming) tests
// ============================================================================

TEST_CASE("check_streams sends data when interval elapsed") {
    mock_client client;
    int call_count = 0;
    client["temperature"] = [&call_count](output& out) {
        call_count++;
        out["celsius"] = 22.0;
    };

    // Start a stream with 1000ms interval
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(900);
    start_req[message::field::RESOURCE] = std::string("temperature");
    start_req[message::field::PARAMETERS] = 1000u;

    client.millis_value = 0;
    client.queue_response(start_req);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    // Reset state after start
    int initial_calls = call_count;
    client.tx_buffer.clear();

    // Not enough time elapsed
    client.millis_value = 500;
    client.check_streams();
    auto sent = client.get_sent_messages();
    CHECK(sent.size() == 0);
    CHECK(call_count == initial_calls);

    // Enough time elapsed
    client.millis_value = 1000;
    client.check_streams();
    sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::STREAM_DATA);
    CHECK(call_count == initial_calls + 1);
}

TEST_CASE("check_streams does not send for zero-interval streams") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };

    // Start a stream without interval (0)
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(901);
    start_req[message::field::RESOURCE] = std::string("temperature");

    client.millis_value = 0;
    client.queue_response(start_req);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);
    client.tx_buffer.clear();

    client.millis_value = 10000;
    client.check_streams();
    auto sent = client.get_sent_messages();
    CHECK(sent.size() == 0);
}

// ============================================================================
// stream() convenience method tests
// ============================================================================

TEST_CASE("stream() sends data for active streamed resource") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 30.0;
    };

    // Start a stream
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(950);
    start_req[message::field::RESOURCE] = std::string("temperature");

    client.queue_response(start_req);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);
    client.tx_buffer.clear();

    // Call stream()
    bool result = client.stream("temperature");
    CHECK(result == true);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() == 1);
    CHECK(sent[0].get_message_type() == message::STREAM_DATA);
    CHECK(sent[0].get_stream_id() == 950);
}

TEST_CASE("stream() returns false for non-streamed resource") {
    mock_client client;
    client["temperature"] = [](output& out) {
        out["celsius"] = 30.0;
    };

    // No stream started
    bool result = client.stream("temperature");
    CHECK(result == false);
}

TEST_CASE("stream() returns false for nonexistent resource") {
    mock_client client;
    bool result = client.stream("nonexistent");
    CHECK(result == false);
}

// ============================================================================
// is_transport_connected tests
// ============================================================================

TEST_CASE("is_transport_connected reflects mock state") {
    mock_client client;
    CHECK(client.is_transport_connected() == true);
    client.connected = false;
    CHECK(client.is_transport_connected() == false);
}

// ============================================================================
// Resource path with array format (RESOURCE field as array)
// ============================================================================

TEST_CASE("RUN with resource path as array") {
    mock_client client;
    // Register resource with a path containing '/'
    client["sensors/temperature"] = [](output& out) {
        out["celsius"] = 18.0;
    };

    iotmp_message request(message::RUN);
    request.set_stream_id(77);
    // Set resource as array: ["sensors", "temperature"]
    request[message::field::RESOURCE] = json_t::array({
        json_t("sensors"),
        json_t("temperature")
    });

    client.queue_response(request);
    iotmp_message incoming(message::RESERVED);
    REQUIRE(client.read_message(incoming));
    client.handle_message(incoming);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 77);
    REQUIRE(sent[0].has_payload());
    CHECK(sent[0].payload()["celsius"].get<double>() == doctest::Approx(18.0));
}

// ============================================================================
// Varint I/O tests
// ============================================================================

TEST_CASE("read_varint decodes single-byte varint") {
    mock_client client;
    // Encode 42 as a varint (single byte since < 128)
    client.rx_buffer.push_back(static_cast<char>(42));

    uint32_t value = 0;
    REQUIRE(client.read_varint(value));
    CHECK(value == 42);
}

TEST_CASE("read_varint decodes multi-byte varint") {
    mock_client client;
    // Encode 300 as a varint: 300 = 0b100101100
    // byte1 = 0b10101100 = 0xAC, byte2 = 0b00000010 = 0x02
    client.rx_buffer.push_back(static_cast<char>(0xAC));
    client.rx_buffer.push_back(static_cast<char>(0x02));

    uint32_t value = 0;
    REQUIRE(client.read_varint(value));
    CHECK(value == 300);
}

TEST_CASE("read_varint fails on empty buffer") {
    mock_client client;
    uint32_t value = 0;
    CHECK(client.read_varint(value) == false);
}
