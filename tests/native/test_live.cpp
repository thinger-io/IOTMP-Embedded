// Live IOTMP client test — stays connected for several minutes,
// registers resources, handles server requests, and tests server APIs.
//
// Compile: clang++ -std=c++17 -I../../include -o test_live test_live.cpp
// Run:     ./test_live <host> <port> <username> <device_id> <credential> [duration_s]

#include <thinger/iotmp/core/iotmp_value.hpp>
#include <thinger/iotmp/core/iotmp_message.hpp>
#include <thinger/iotmp/core/iotmp_encoder.hpp>
#include <thinger/iotmp/core/iotmp_decoder.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>

#include <sys/socket.h>
#include <netdb.h>
#include <unistd.h>
#include <poll.h>
#include <cerrno>
#include <cstring>
#include <cstdio>
#include <ctime>
#include <cmath>
#include <map>
#include <string>
#include <chrono>

using namespace thinger::iotmp;

// ============================================================================
// Simple TCP client
// ============================================================================

class tcp_client {
    int sock_ = -1;

public:
    ~tcp_client() { disconnect(); }

    bool connect_to(const char* host, uint16_t port) {
        struct addrinfo hints = {}, *res = nullptr;
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;

        char port_str[8];
        snprintf(port_str, sizeof(port_str), "%u", port);

        if(getaddrinfo(host, port_str, &hints, &res) != 0 || !res) return false;

        sock_ = socket(res->ai_family, res->ai_socktype, res->ai_protocol);
        if(sock_ < 0) { freeaddrinfo(res); return false; }

        int rc = ::connect(sock_, res->ai_addr, res->ai_addrlen);
        freeaddrinfo(res);

        if(rc < 0) { close(sock_); sock_ = -1; return false; }
        return true;
    }

    void disconnect() {
        if(sock_ >= 0) { close(sock_); sock_ = -1; }
    }

    int fd() const { return sock_; }
    bool is_connected() const { return sock_ >= 0; }

    bool send_all(const void* buf, size_t len) {
        auto* ptr = static_cast<const uint8_t*>(buf);
        while(len > 0) {
            ssize_t rc = send(sock_, ptr, len, 0);
            if(rc <= 0) return false;
            ptr += rc; len -= rc;
        }
        return true;
    }

    bool recv_all(void* buf, size_t len) {
        auto* ptr = static_cast<uint8_t*>(buf);
        while(len > 0) {
            ssize_t rc = recv(sock_, ptr, len, 0);
            if(rc <= 0) return false;
            ptr += rc; len -= rc;
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

    bool write_message(message::type type) {
        auto encoded = encode_message(type);
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

// ============================================================================
// Simulated device state
// ============================================================================

static float temperature = 22.5f;
static float humidity = 65.0f;
static bool led_state = false;
static float threshold = 30.0f;
static int uptime_seconds = 0;

static void update_sensors() {
    // Simulate some realistic drift
    temperature = 20.0f + 5.0f * sinf(uptime_seconds * 0.05f);
    humidity = 60.0f + 10.0f * cosf(uptime_seconds * 0.03f);
}

// ============================================================================
// Timestamp helper
// ============================================================================

static const char* timestamp() {
    static char buf[32];
    auto now = std::chrono::system_clock::now();
    auto t = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()).count() % 1000;
    struct tm* tm = localtime(&t);
    snprintf(buf, sizeof(buf), "%02d:%02d:%02d.%03d",
             tm->tm_hour, tm->tm_min, tm->tm_sec, (int)ms);
    return buf;
}

#define LOG(fmt, ...) printf("[%s] " fmt "\n", timestamp(), ##__VA_ARGS__)

// ============================================================================
// Main
// ============================================================================

int main(int argc, char* argv[]) {
    if(argc < 6) {
        printf("Usage: %s <host> <port> <username> <device_id> <credential> [duration_s]\n", argv[0]);
        return 1;
    }

    const char* host = argv[1];
    uint16_t port = static_cast<uint16_t>(atoi(argv[2]));
    const char* username = argv[3];
    const char* device_id = argv[4];
    const char* credential = argv[5];
    int duration = argc > 6 ? atoi(argv[6]) : 300; // default 5 minutes

    // ---- Register resources ----
    std::map<std::string, iotmp_resource> resources;

    resources["environment"] = std::function<void(output&)>([](output& out) {
        out["temperature"] = temperature;
        out["humidity"] = humidity;
    });

    resources["led"] = std::function<void(input&)>([](input& in) {
        led_state = in["state"].get<bool>();
        LOG("  -> LED set to: %s", led_state ? "ON" : "OFF");
    });

    resources["threshold"] = std::function<void(input&, output&)>(
        [](input& in, output& out) {
            if(!in.is_empty()) {
                threshold = in["value"].get<float>();
                LOG("  -> Threshold set to: %.1f", threshold);
            }
            out["value"] = threshold;
        }
    );

    resources["reboot"] = std::function<void()>([]() {
        LOG("  -> Reboot requested (simulated)");
    });

    resources["uptime"] = std::function<void(output&)>([](output& out) {
        out["seconds"] = uptime_seconds;
    });

    // Stream tracking
    struct stream_info {
        iotmp_resource* resource = nullptr;
        uint32_t interval_ms = 0;
        int64_t last_sent = 0;
    };
    std::map<uint16_t, stream_info> streams;

    // ---- Connect ----
    tcp_client client;
    LOG("Connecting to %s:%u...", host, port);

    if(!client.connect_to(host, port)) {
        LOG("Connection failed!");
        return 1;
    }
    LOG("Connected");

    // ---- Authenticate ----
    LOG("Authenticating as %s@%s...", device_id, username);

    iotmp_message connect_msg(message::type::CONNECT);
    connect_msg.set_random_stream_id();
    connect_msg[message::field::PAYLOAD] = iotmp_value::array({
        iotmp_value(username),
        iotmp_value(device_id),
        iotmp_value(credential)
    });

    if(!client.write_message(connect_msg)) {
        LOG("Failed to send CONNECT");
        return 1;
    }

    iotmp_message auth_resp(message::type::RESERVED);
    if(!client.read_message(auth_resp) || auth_resp.get_message_type() != message::type::OK) {
        LOG("Authentication failed!");
        return 1;
    }
    LOG("Authenticated successfully!");
    LOG("Device online — running for %d seconds. Test from dashboard!", duration);
    LOG("---");

    // ---- Event loop ----
    auto start_time = std::chrono::steady_clock::now();
    auto last_keepalive = start_time;
    auto last_sensor_update = start_time;

    while(client.is_connected()) {
        auto now = std::chrono::steady_clock::now();
        auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - start_time).count();

        if(elapsed >= duration) {
            LOG("Duration reached (%d s), disconnecting.", duration);
            break;
        }

        // Poll for incoming messages (100ms timeout for responsiveness)
        struct pollfd pfd = { .fd = client.fd(), .events = POLLIN, .revents = 0 };
        int rc = poll(&pfd, 1, 100);

        if(rc > 0 && (pfd.revents & POLLIN)) {
            iotmp_message msg(message::type::RESERVED);
            if(!client.read_message(msg)) {
                LOG("Read failed, connection lost.");
                break;
            }

            auto msg_type = msg.get_message_type();
            uint16_t stream_id = msg.get_stream_id();

            switch(msg_type) {
                case message::KEEP_ALIVE:
                    LOG("RX: KEEP_ALIVE");
                    break;

                case message::DESCRIBE: {
                    LOG("RX: DESCRIBE (stream=%u)", stream_id);

                    if(!msg.has_field(message::field::RESOURCE)) {
                        // Full API describe
                        iotmp_message resp(stream_id, message::type::OK);
                        for(auto& [name, res] : resources) {
                            res.fill_api(resp[message::field::PAYLOAD][name.c_str()]);
                        }
                        client.write_message(resp);
                        LOG("TX: OK (API description with %zu resources)", resources.size());
                    } else {
                        // Describe specific resource
                        auto res_name = msg[message::field::RESOURCE].get<std::string>();
                        auto it = resources.find(res_name);
                        if(it != resources.end()) {
                            iotmp_message resp(stream_id, message::type::OK);
                            it->second.describe(resp);
                            client.write_message(resp);
                            LOG("TX: OK (describe '%s')", res_name.c_str());
                        } else {
                            iotmp_message resp(stream_id, message::type::ERROR);
                            client.write_message(resp);
                            LOG("TX: ERROR (resource '%s' not found)", res_name.c_str());
                        }
                    }
                    break;
                }

                case message::RUN: {
                    auto res_name = msg.has_field(message::field::RESOURCE)
                        ? msg[message::field::RESOURCE].get<std::string>() : std::string("?");
                    LOG("RX: RUN '%s' (stream=%u)", res_name.c_str(), stream_id);

                    auto it = resources.find(res_name);
                    if(it != resources.end()) {
                        iotmp_message resp(stream_id, message::type::OK);
                        bool ok = it->second.run_resource(msg, resp);
                        resp.set_message_type(ok ? message::type::OK : message::type::ERROR);
                        client.write_message(resp);
                        LOG("TX: %s", ok ? "OK" : "ERROR");
                    } else {
                        iotmp_message resp(stream_id, message::type::ERROR);
                        client.write_message(resp);
                        LOG("TX: ERROR (not found)");
                    }
                    break;
                }

                case message::START_STREAM: {
                    auto res_name = msg.has_field(message::field::RESOURCE)
                        ? msg[message::field::RESOURCE].get<std::string>() : std::string("?");
                    LOG("RX: START_STREAM '%s' (stream=%u)", res_name.c_str(), stream_id);

                    auto it = resources.find(res_name);
                    if(it != resources.end()) {
                        auto& si = streams[stream_id];
                        si.resource = &it->second;
                        si.interval_ms = 0;
                        si.last_sent = 0;

                        // Check for interval
                        if(msg.has_params() && msg.params().contains("interval")) {
                            si.interval_ms = msg.params()["interval"].get<uint32_t>();
                            LOG("  Stream interval: %u ms", si.interval_ms);
                        }

                        // Send OK
                        iotmp_message resp(stream_id, message::type::OK);
                        client.write_message(resp);

                        // Send initial value
                        update_sensors();
                        iotmp_message data_req(message::type::STREAM_DATA);
                        iotmp_message data_resp(message::type::STREAM_DATA);
                        it->second.run_resource(data_req, data_resp);
                        auto& data_msg = data_resp.has_field(message::field::PAYLOAD) ? data_resp : data_req;
                        if(data_msg.has_field(message::field::PAYLOAD)) {
                            data_msg.set_stream_id(stream_id);
                            client.write_message(data_msg);
                            LOG("TX: STREAM_DATA (initial value for '%s')", res_name.c_str());
                        }
                    } else {
                        iotmp_message resp(stream_id, message::type::ERROR);
                        client.write_message(resp);
                    }
                    break;
                }

                case message::STOP_STREAM: {
                    LOG("RX: STOP_STREAM (stream=%u)", stream_id);
                    streams.erase(stream_id);
                    iotmp_message resp(stream_id, message::type::OK);
                    client.write_message(resp);
                    LOG("TX: OK (stream stopped)");
                    break;
                }

                case message::STREAM_DATA: {
                    // Input on a streamed resource
                    auto sit = streams.find(stream_id);
                    if(sit != streams.end() && sit->second.resource) {
                        iotmp_message resp(stream_id, message::type::STREAM_DATA);
                        sit->second.resource->run_resource(msg, resp);

                        // Echo back current state
                        update_sensors();
                        iotmp_message echo_req(message::type::STREAM_DATA);
                        iotmp_message echo_resp(message::type::STREAM_DATA);
                        sit->second.resource->run_resource(echo_req, echo_resp);
                        auto& echo_msg = echo_resp.has_field(message::field::PAYLOAD) ? echo_resp : echo_req;
                        if(echo_msg.has_field(message::field::PAYLOAD)) {
                            echo_msg.set_stream_id(stream_id);
                            client.write_message(echo_msg);
                        }
                    }
                    break;
                }

                default:
                    LOG("RX: %s (type=%d, stream=%u)",
                        msg.message_type_str(),
                        static_cast<int>(msg_type), stream_id);
                    break;
            }
        }

        if(pfd.revents & (POLLHUP | POLLERR)) {
            LOG("Socket error/hangup");
            break;
        }

        now = std::chrono::steady_clock::now();

        // Send keepalive every 60 seconds
        auto ka_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_keepalive).count();
        if(ka_elapsed >= 60) {
            client.write_message(message::type::KEEP_ALIVE);
            last_keepalive = now;
            LOG("TX: KEEP_ALIVE");
        }

        // Update sensors every second
        auto sensor_elapsed = std::chrono::duration_cast<std::chrono::seconds>(now - last_sensor_update).count();
        if(sensor_elapsed >= 1) {
            uptime_seconds += sensor_elapsed;
            update_sensors();
            last_sensor_update = now;
        }

        // Check stream intervals
        auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(
            now.time_since_epoch()).count();
        for(auto& [sid, si] : streams) {
            if(si.interval_ms > 0 && si.resource) {
                if(now_ms - si.last_sent >= si.interval_ms) {
                    si.last_sent = now_ms;

                    iotmp_message req(message::type::STREAM_DATA);
                    iotmp_message resp(message::type::STREAM_DATA);
                    si.resource->run_resource(req, resp);
                    auto& data = resp.has_field(message::field::PAYLOAD) ? resp : req;
                    if(data.has_field(message::field::PAYLOAD)) {
                        data.set_stream_id(sid);
                        client.write_message(data);
                    }
                }
            }
        }
    }

    LOG("---");
    LOG("Session ended. Uptime: %d seconds.", uptime_seconds);
    LOG("Active streams at disconnect: %zu", streams.size());

    return 0;
}
