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
        : iotmp_client_base("test_user", "test_device", "test_credential") {
        set_state_callback([this](client_state s) {
            states_received.push_back(s);
        });
    }

    // Buffers for simulating I/O
    std::string tx_buffer;   // what the client sends
    std::string rx_buffer;   // what the client reads (simulated server responses)
    size_t rx_pos = 0;
    bool connected = true;

    // Control connect behavior
    bool connect_should_succeed = true;
    int connect_call_count = 0;

    // Track disconnect calls
    bool disconnect_called = false;

    // Control data availability separately from buffer state
    bool has_data = false;
    bool use_has_data_flag = false;  // when true, data_available uses has_data instead of buffer

    // State tracking
    std::vector<client_state> states_received;

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

    // ----- I/O serialization instrumentation ---------------------------
    // Overrides the base no-op hooks so tests can assert that socket
    // access is serialized and, crucially, that a request/response
    // exchange holds the lock across its inner read/write (io_max_depth
    // >= 2). Recursive by design: nested guards just bump the depth.
    int io_depth = 0;
    int io_max_depth = 0;
    int io_lock_calls = 0;
    int io_unlock_calls = 0;

    void io_lock() {
        io_depth++;
        if(io_depth > io_max_depth) io_max_depth = io_depth;
        io_lock_calls++;
    }

    void io_unlock() {
        io_depth--;
        io_unlock_calls++;
    }

    bool data_available_impl() {
        if(use_has_data_flag) return has_data && rx_pos < rx_buffer.size();
        return rx_pos < rx_buffer.size();
    }

    unsigned long get_millis() const { return millis_value; }

    bool connect_impl() {
        connect_call_count++;
        if(connect_should_succeed) {
            connected = true;
        }
        return connect_should_succeed;
    }

    void disconnect_impl() {
        disconnect_called = true;
        connected = false;
    }

    // Test helpers
    unsigned long millis_value = 0;

    void set_connected(bool v) { connected_ = v; }

    void reset() {
        tx_buffer.clear();
        rx_buffer.clear();
        rx_pos = 0;
        connected = true;
        connect_should_succeed = true;
        connect_call_count = 0;
        disconnect_called = false;
        has_data = false;
        use_has_data_flag = false;
        states_received.clear();
        io_depth = 0;
        io_max_depth = 0;
        io_lock_calls = 0;
        io_unlock_calls = 0;
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

    // Queue an auth OK that will match any stream_id (just an OK with stream_id 0)
    void queue_auth_ok() {
        // authenticate() reads the next message regardless of stream_id
        queue_ok();
    }

    // Queue an ERROR response for auth failure
    void queue_auth_error() {
        iotmp_message msg(0, message::ERROR);
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

    // Helper: set both transport and base connected flags to false
    void set_fully_disconnected() {
        connected = false;
        connected_ = false;
    }

    // Helper: prepare mock for a successful handle() connect+auth cycle.
    // Sets millis_value to 5000 so should_reconnect() passes on first call
    // (last_connection_attempt_ starts at 0, reconnect_ms_ starts at 5000).
    void prepare_successful_connection() {
        set_fully_disconnected();
        connect_should_succeed = true;
        millis_value = 5000;  // ensure should_reconnect() passes
        queue_auth_ok();      // queue OK for authenticate()
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
    // The server sends the sampling interval as an object {"interval": ms},
    // not a bare number. This is what the real START_STREAM looks like.
    request[message::field::PARAMETERS]["interval"] = 1000u;

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

    // Start a stream with 1000ms interval (server sends it as {"interval": ms})
    iotmp_message start_req(message::START_STREAM);
    start_req.set_stream_id(900);
    start_req[message::field::RESOURCE] = std::string("temperature");
    start_req[message::field::PARAMETERS]["interval"] = 1000u;

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
// Server API RPC tests (send_and_wait_response) + I/O serialization
//
// This path (write_bucket / set_property / get_property / call_endpoint) had
// no coverage, yet it is exactly what failed on multi-task platforms: the
// client task consumed the RPC response, so the OK was never seen here and
// buckets never recorded. These tests lock in both the happy path and the
// serialization guarantee that fixes it.
// ============================================================================

// send_and_wait_response assigns a random stream_id via rand(). Reseed rand()
// so we can pre-queue an OK with the id the client is about to generate.
static uint16_t predict_next_stream_id(unsigned seed) {
    srand(seed);
    uint16_t id = static_cast<uint16_t>(rand());
    srand(seed); // rewind so the client's next rand() yields the same id
    return id;
}

TEST_CASE("write_bucket completes a request/response exchange") {
    mock_client client;
    client.set_connected(true);

    uint16_t expected_id = predict_next_stream_id(12345);
    client.queue_ok(expected_id);

    json_t data;
    data["value"] = 42;
    bool ok = client.write_bucket("my_bucket", std::move(data));

    CHECK(ok == true);

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::RUN);
    CHECK(sent[0].get_stream_id() == expected_id);
    CHECK(sent[0][message::field::PARAMETERS].get<uint64_t>()
          == static_cast<uint64_t>(server::WRITE_BUCKET));
}

TEST_CASE("write_bucket returns false when not connected") {
    mock_client client;
    client.set_connected(false);
    json_t data;
    data["value"] = 1;
    CHECK(client.write_bucket("b", std::move(data)) == false);
}

TEST_CASE("write_bucket returns false on ERROR response") {
    mock_client client;
    client.set_connected(true);
    uint16_t expected_id = predict_next_stream_id(777);
    iotmp_message err(expected_id, message::ERROR);
    client.queue_response(err);
    json_t data;
    data["value"] = 1;
    CHECK(client.write_bucket("b", std::move(data)) == false);
}

TEST_CASE("send_and_wait_response holds the I/O lock across the whole exchange") {
    mock_client client;
    client.set_connected(true);
    uint16_t expected_id = predict_next_stream_id(2024);
    client.queue_ok(expected_id);

    client.io_max_depth = 0;
    json_t data;
    data["value"] = 7;
    bool ok = client.write_bucket("b", std::move(data));

    CHECK(ok == true);
    // The outer guard (in send_and_wait_response) must still be held while the
    // inner send_message/read_message take the lock -> depth reaches >= 2.
    // That nesting is precisely what stops another task from reading the
    // socket mid-exchange and stealing this response.
    CHECK(client.io_max_depth >= 2);
    // Everything released and balanced.
    CHECK(client.io_depth == 0);
    CHECK(client.io_lock_calls == client.io_unlock_calls);
}

TEST_CASE("write_message serializes socket access and always balances the lock") {
    mock_client client;
    iotmp_message msg(message::KEEP_ALIVE);

    int before = client.io_lock_calls;
    client.write_message(msg);
    CHECK(client.io_lock_calls == before + 1);
    CHECK(client.io_max_depth >= 1);
    CHECK(client.io_depth == 0);
    CHECK(client.io_lock_calls == client.io_unlock_calls);
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

// ============================================================================
// handle() — Connection lifecycle tests
// ============================================================================

TEST_CASE("handle: connects and authenticates on first call") {
    mock_client client;
    client.prepare_successful_connection();

    client.handle();

    // connect_impl was called
    CHECK(client.connect_call_count == 1);
    // client should now be connected at the protocol level
    CHECK(client.is_connected() == true);
    // CONNECT message was sent (authenticate sent it)
    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::CONNECT);
}

TEST_CASE("handle: does not reconnect before backoff expires") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;
    client.millis_value = 5000;

    // First attempt at t=5000 — should call connect (fails, backoff -> 10000)
    client.handle();
    CHECK(client.connect_call_count == 1);

    // Advance time less than the new backoff (10000ms from last attempt at 5000)
    client.millis_value = 12000;
    client.handle();
    // connect_impl should NOT have been called again
    CHECK(client.connect_call_count == 1);
}

TEST_CASE("handle: reconnects after backoff expires") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;
    client.millis_value = 5000;

    // First attempt at t=5000 (fails, backoff -> 10000)
    client.handle();
    CHECK(client.connect_call_count == 1);

    // Advance time past backoff: last_attempt=5000, backoff=10000, need t>=15000
    client.millis_value = 15000;
    client.handle();
    CHECK(client.connect_call_count == 2);
}

TEST_CASE("handle: exponential backoff on repeated failures") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;

    // Attempt 1 at t=5000 (last_attempt=0, backoff=5000 -> passes)
    // After fail: backoff -> 10000
    client.millis_value = 5000;
    client.handle();
    CHECK(client.connect_call_count == 1);

    // Too early: last_attempt=5000, backoff=10000, need t>=15000
    client.millis_value = 14999;
    client.handle();
    CHECK(client.connect_call_count == 1);

    // Attempt 2 at t=15000 — backoff was 10000, after fail -> 20000
    client.millis_value = 15000;
    client.handle();
    CHECK(client.connect_call_count == 2);

    // Too early: last_attempt=15000, backoff=20000, need t>=35000
    client.millis_value = 34999;
    client.handle();
    CHECK(client.connect_call_count == 2);

    // Attempt 3 at t=35000 — backoff was 20000, after fail -> 40000
    client.millis_value = 35000;
    client.handle();
    CHECK(client.connect_call_count == 3);

    // Too early: last_attempt=35000, backoff=40000, need t>=75000
    client.millis_value = 74999;
    client.handle();
    CHECK(client.connect_call_count == 3);

    // Attempt 4 at t=75000 — backoff was 40000, after fail -> 60000 (capped at max)
    client.millis_value = 75000;
    client.handle();
    CHECK(client.connect_call_count == 4);

    // Too early: last_attempt=75000, backoff=60000, need t>=135000
    client.millis_value = 134999;
    client.handle();
    CHECK(client.connect_call_count == 4);

    // Attempt 5 at t=135000 — backoff stays at 60000 (max)
    client.millis_value = 135000;
    client.handle();
    CHECK(client.connect_call_count == 5);
}

TEST_CASE("handle: backoff resets after successful connection") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;

    // Fail once at t=5000 — backoff goes from 5000 to 10000
    client.millis_value = 5000;
    client.handle();
    CHECK(client.connect_call_count == 1);

    // Succeed at t=15000 (last_attempt=5000, backoff=10000)
    client.millis_value = 15000;
    client.connect_should_succeed = true;
    client.queue_auth_ok();
    client.handle();
    CHECK(client.connect_call_count == 2);
    CHECK(client.is_connected() == true);

    // Disconnect — reset_backoff() sets reconnect_ms_ back to 5000
    client.disconnect();
    client.connect_should_succeed = false;

    // After reset_backoff(), reconnect_ms_ should be back to 5000.
    // last_connection_attempt_ was 15000.
    // Advance by 5000 ms — should attempt again at t=20000.
    client.millis_value = 20000;
    client.handle();
    CHECK(client.connect_call_count == 3);
}

TEST_CASE("handle: processes incoming messages when connected") {
    mock_client client;
    client.prepare_successful_connection();
    client["temperature"] = [](output& out) {
        out["celsius"] = 42.0;
    };

    // First handle() connects and authenticates
    client.handle();
    CHECK(client.is_connected() == true);
    client.tx_buffer.clear();
    client.rx_buffer.clear();
    client.rx_pos = 0;

    // Queue a RUN message
    iotmp_message run_msg(message::RUN);
    run_msg.set_stream_id(77);
    run_msg[message::field::RESOURCE] = std::string("temperature");
    client.queue_response(run_msg);

    // Second handle() should process the message
    client.handle();

    auto sent = client.get_sent_messages();
    REQUIRE(sent.size() >= 1);
    CHECK(sent[0].get_message_type() == message::OK);
    CHECK(sent[0].get_stream_id() == 77);
    REQUIRE(sent[0].has_payload());
    CHECK(sent[0].payload()["celsius"].get<double>() == doctest::Approx(42.0));
}

TEST_CASE("handle: disconnects when transport drops") {
    mock_client client;
    client.prepare_successful_connection();

    client.handle();
    CHECK(client.is_connected() == true);
    client.states_received.clear();

    // Simulate transport drop
    client.connected = false;
    client.rx_buffer.clear();
    client.rx_pos = 0;

    client.handle();
    CHECK(client.is_connected() == false);

    // Should have notified SOCKET_DISCONNECTED
    bool found_disconnected = false;
    for(auto s : client.states_received) {
        if(s == client_state::SOCKET_DISCONNECTED) found_disconnected = true;
    }
    CHECK(found_disconnected == true);
}

TEST_CASE("handle: sends keepalive after interval") {
    mock_client client;
    client.prepare_successful_connection();
    // millis_value is 5000 from prepare_successful_connection()

    client.handle();
    CHECK(client.is_connected() == true);
    client.tx_buffer.clear();
    client.rx_buffer.clear();
    client.rx_pos = 0;

    // Keepalive timer was set to 5000, interval is 60000ms, so fires at 65000
    client.millis_value = 65000;
    client.handle();

    auto sent = client.get_sent_messages();
    bool has_keepalive = false;
    for(auto& m : sent) {
        if(m.get_message_type() == message::KEEP_ALIVE) has_keepalive = true;
    }
    CHECK(has_keepalive == true);
}

TEST_CASE("handle: does not send keepalive before interval") {
    mock_client client;
    client.prepare_successful_connection();
    // millis_value is 5000 from prepare_successful_connection()

    client.handle();
    CHECK(client.is_connected() == true);
    client.tx_buffer.clear();
    client.rx_buffer.clear();
    client.rx_pos = 0;

    // Keepalive timer was set to 5000, interval is 60000ms
    // Advance to 35000 (only 30000ms since last keepalive, less than 60000)
    client.millis_value = 35000;
    client.handle();

    auto sent = client.get_sent_messages();
    bool has_keepalive = false;
    for(auto& m : sent) {
        if(m.get_message_type() == message::KEEP_ALIVE) has_keepalive = true;
    }
    CHECK(has_keepalive == false);
}

TEST_CASE("handle: connection failed notifies SOCKET_CONNECTING then SOCKET_CONNECTION_ERROR") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;
    client.millis_value = 5000;

    client.handle();

    REQUIRE(client.states_received.size() >= 2);
    CHECK(client.states_received[0] == client_state::SOCKET_CONNECTING);
    CHECK(client.states_received[1] == client_state::SOCKET_CONNECTION_ERROR);
}

TEST_CASE("handle: auth failed notifies AUTHENTICATING then AUTH_FAILED") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = true;
    client.queue_auth_error();
    client.millis_value = 5000;

    client.handle();

    // Find AUTHENTICATING and AUTH_FAILED in the states
    bool found_authenticating = false;
    bool found_auth_failed = false;
    for(auto s : client.states_received) {
        if(s == client_state::AUTHENTICATING) found_authenticating = true;
        if(s == client_state::AUTH_FAILED) found_auth_failed = true;
    }
    CHECK(found_authenticating == true);
    CHECK(found_auth_failed == true);
}

TEST_CASE("handle: successful connection notifies all states in order") {
    mock_client client;
    client.prepare_successful_connection();

    client.handle();

    // Expected order: SOCKET_CONNECTING -> SOCKET_CONNECTED -> AUTHENTICATING -> AUTHENTICATED -> READY
    REQUIRE(client.states_received.size() >= 5);
    CHECK(client.states_received[0] == client_state::SOCKET_CONNECTING);
    CHECK(client.states_received[1] == client_state::SOCKET_CONNECTED);
    CHECK(client.states_received[2] == client_state::AUTHENTICATING);
    CHECK(client.states_received[3] == client_state::AUTHENTICATED);
    CHECK(client.states_received[4] == client_state::READY);
}

TEST_CASE("handle: auth failure causes disconnect and backoff update") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = true;
    client.queue_auth_error();
    client.millis_value = 5000;

    client.handle();

    // Should not be connected
    CHECK(client.is_connected() == false);
    // disconnect_impl should have been called
    CHECK(client.disconnect_called == true);
    // connect_call_count should be 1
    CHECK(client.connect_call_count == 1);

    // Backoff should have been updated — next attempt needs more time
    // Initial backoff was 5000, after update_backoff -> 10000
    // last_attempt was 5000, so need t >= 15000
    client.connect_should_succeed = false;
    client.millis_value = 10000;
    client.handle();
    CHECK(client.connect_call_count == 1);  // no new attempt (10000-5000=5000 < 10000)

    client.millis_value = 15000;
    client.handle();
    CHECK(client.connect_call_count == 2);  // retried (15000-5000=10000 >= 10000)
}

// ============================================================================
// disconnect() tests
// ============================================================================

TEST_CASE("disconnect: clears connected flag") {
    mock_client client;
    client.prepare_successful_connection();
    client.handle();
    CHECK(client.is_connected() == true);

    client.disconnect();
    CHECK(client.is_connected() == false);
}

TEST_CASE("disconnect: notifies SOCKET_DISCONNECTED") {
    mock_client client;
    client.prepare_successful_connection();
    client.handle();
    client.states_received.clear();

    client.disconnect();

    REQUIRE(client.states_received.size() >= 1);
    CHECK(client.states_received[0] == client_state::SOCKET_DISCONNECTED);
}

TEST_CASE("disconnect: clears streams") {
    mock_client client;
    client.prepare_successful_connection();
    client["temperature"] = [](output& out) {
        out["celsius"] = 22.0;
    };
    client.handle();
    client.tx_buffer.clear();
    client.rx_buffer.clear();
    client.rx_pos = 0;

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

    client.disconnect();

    CHECK(res->stream_enabled() == false);
    CHECK(res->get_stream_id() == 0);
}

TEST_CASE("disconnect: calls disconnect_impl") {
    mock_client client;
    client.prepare_successful_connection();
    client.handle();
    client.disconnect_called = false;  // reset after handle's connect

    client.disconnect();
    CHECK(client.disconnect_called == true);
    CHECK(client.connected == false);
}

TEST_CASE("disconnect: second disconnect does not notify again") {
    mock_client client;
    client.prepare_successful_connection();
    client.handle();
    client.states_received.clear();

    client.disconnect();
    REQUIRE(client.states_received.size() >= 1);
    CHECK(client.states_received.back() == client_state::SOCKET_DISCONNECTED);

    size_t count_after_first = client.states_received.size();
    client.disconnect();
    // No additional SOCKET_DISCONNECTED should be emitted
    CHECK(client.states_received.size() == count_after_first);
}

// ============================================================================
// Keepalive detailed tests
// ============================================================================

TEST_CASE("keepalive: resets timer after sending") {
    mock_client client;
    client.prepare_successful_connection();
    // millis_value is 5000 from prepare_successful_connection()

    client.handle();
    CHECK(client.is_connected() == true);

    // Clear buffers to track new messages
    client.tx_buffer.clear();
    client.rx_buffer.clear();
    client.rx_pos = 0;

    // last_keepalive_ was set to 5000 during connection.
    // Advance to 65000ms (60000ms since last keepalive) — should send keepalive
    client.millis_value = 65000;
    client.handle();
    {
        auto sent = client.get_sent_messages();
        bool has_ka = false;
        for(auto& m : sent) {
            if(m.get_message_type() == message::KEEP_ALIVE) has_ka = true;
        }
        CHECK(has_ka == true);
    }

    // Clear and advance to 105000ms (only 40000ms since last keepalive at 65000)
    client.tx_buffer.clear();
    client.millis_value = 105000;
    client.handle();
    {
        auto sent = client.get_sent_messages();
        bool has_ka = false;
        for(auto& m : sent) {
            if(m.get_message_type() == message::KEEP_ALIVE) has_ka = true;
        }
        CHECK(has_ka == false);  // not yet, only 40000ms since last
    }

    // Advance to 125000ms (60000ms since last keepalive at 65000)
    client.tx_buffer.clear();
    client.millis_value = 125000;
    client.handle();
    {
        auto sent = client.get_sent_messages();
        bool has_ka = false;
        for(auto& m : sent) {
            if(m.get_message_type() == message::KEEP_ALIVE) has_ka = true;
        }
        CHECK(has_ka == true);
    }
}

TEST_CASE("keepalive: no keepalive sent immediately after connection") {
    mock_client client;
    client.prepare_successful_connection();
    // millis_value is 5000 from prepare_successful_connection()

    client.handle();
    CHECK(client.is_connected() == true);

    // The CONNECT message should be present but no KEEP_ALIVE
    auto sent = client.get_sent_messages();
    for(auto& m : sent) {
        CHECK(m.get_message_type() != message::KEEP_ALIVE);
    }
}

// ============================================================================
// handle() — multiple connect/disconnect cycles
// ============================================================================

TEST_CASE("handle: full reconnect cycle after disconnect") {
    mock_client client;
    client.prepare_successful_connection();
    // millis_value is 5000 from prepare_successful_connection()

    // First connection at t=5000
    client.handle();
    CHECK(client.is_connected() == true);
    CHECK(client.connect_call_count == 1);

    // Disconnect
    client.disconnect();
    CHECK(client.is_connected() == false);

    // Queue new auth OK for reconnection
    client.rx_buffer.clear();
    client.rx_pos = 0;
    client.tx_buffer.clear();
    client.queue_auth_ok();

    // Need to wait for backoff (5000ms since reset_backoff).
    // last_connection_attempt_ was 5000, reconnect_ms_ is 5000 after reset.
    client.millis_value = 10000;
    client.handle();
    CHECK(client.connect_call_count == 2);
    CHECK(client.is_connected() == true);
}

TEST_CASE("handle: does not process messages or keepalive when disconnected") {
    mock_client client;
    client.set_fully_disconnected();
    client.connect_should_succeed = false;
    client.millis_value = 5000;

    // Queue a message in rx_buffer — it should NOT be processed
    iotmp_message run_msg(message::RUN);
    run_msg.set_stream_id(1);
    run_msg[message::field::RESOURCE] = std::string("test");
    client.queue_response(run_msg);

    client.handle();

    // Connect was attempted but failed, so no messages should have been processed.
    // tx_buffer should be empty since connect failed before sending anything.
    auto sent = client.get_sent_messages();
    CHECK(sent.size() == 0);
}
