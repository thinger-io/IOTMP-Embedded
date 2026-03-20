// The MIT License (MIT)
//
// Copyright (c) INTERNET OF THINGER SL
// Author: alvarolb@gmail.com (Alvaro Luis Bustamante)
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#ifndef THINGER_IOTMP_CLIENT_BASE_HPP
#define THINGER_IOTMP_CLIENT_BASE_HPP

#include "iotmp_log.hpp"
#include "iotmp_message.hpp"
#include "iotmp_encoder.hpp"
#include "iotmp_decoder.hpp"
#include "iotmp_resource.hpp"
#include "iotmp_adapters.hpp"

#include <map>
#include <string>
#include <vector>
#include <cstdint>
#include <functional>

namespace thinger::iotmp {

    // ----------------------------------------------------------------
    // Unified client state enum — shared across all platforms.
    // ----------------------------------------------------------------
    enum class client_state {
        NETWORK_CONNECTING,
        NETWORK_CONNECTED,
        NETWORK_CONNECT_ERROR,
        SOCKET_CONNECTING,
        SOCKET_CONNECTED,
        SOCKET_CONNECTION_ERROR,
        SOCKET_DISCONNECTED,
        SOCKET_TIMEOUT,
        SOCKET_ERROR,
        AUTHENTICATING,
        AUTHENTICATED,
        AUTH_FAILED,
        DISCONNECTED,
        READY,
        STOP_REQUEST
    };

    // ----------------------------------------------------------------
    // Stream configuration stored per active stream.
    // ----------------------------------------------------------------
    struct stream_config {
        const char* resource_name = nullptr;
        unsigned long interval_ms = 0;
        unsigned long last_sample  = 0;
    };

    // ----------------------------------------------------------------
    // CRTP base class for IOTMP clients.
    //
    // Derived must implement:
    //   bool send_bytes_impl(const void* data, size_t len)
    //   bool recv_bytes_impl(void* data, size_t len)
    //   bool is_connected_impl() const
    // ----------------------------------------------------------------
    template<typename Derived>
    class iotmp_client_base {
    public:

        using state_callback_t = std::function<void(client_state)>;

        iotmp_client_base() = default;

        iotmp_client_base(const char* user, const char* device, const char* credential)
            : username_(user),
              device_id_(device),
              credential_(credential) {}

        void set_state_callback(state_callback_t cb) { state_callback_ = std::move(cb); }

        // ----- CRTP dispatch -----------------------------------------

        bool send_bytes(const void* data, size_t len) {
            return derived().send_bytes_impl(data, len);
        }

        bool recv_bytes(void* data, size_t len) {
            return derived().recv_bytes_impl(data, len);
        }

        bool is_transport_connected() const {
            return derived().is_connected_impl();
        }

        // ----- Resource registration ---------------------------------

        iotmp_resource& operator[](const char* name) {
            return resources_[std::string(name)];
        }

        // ----- Configuration -----------------------------------------

        void set_host(const char* host, uint16_t port = 0) {
            host_ = host;
            if(port != 0) port_ = port;
        }

        void set_credentials(const char* user, const char* device, const char* credential) {
            username_ = user;
            device_id_ = device;
            credential_ = credential;
        }

        const char* get_host() const { return host_; }
        uint16_t get_port() const { return port_; }
        const char* get_username() const { return username_; }
        const char* get_device_id() const { return device_id_; }
        const char* get_credential() const { return credential_; }

        // ----- Varint I/O --------------------------------------------

        bool read_varint(uint32_t& value) {
            value = 0;
            uint8_t byte;
            uint8_t bit_pos = 0;
            do {
                if(!recv_bytes(&byte, 1) || bit_pos >= 32) return false;
                value |= static_cast<uint32_t>(byte & 0x7F) << bit_pos;
                bit_pos += 7;
            } while(byte & 0x80);
            return true;
        }

        // ----- Message read / write ----------------------------------

        bool read_message(iotmp_message& msg) {
            // Read message type varint
            uint32_t msg_type = 0;
            if(!read_varint(msg_type)) return false;
            msg.set_message_type(static_cast<message::type>(msg_type));

            // Read body size varint
            uint32_t body_size = 0;
            if(!read_varint(body_size)) return false;

            if(body_size == 0) return true;

            // Read body into buffer, then decode from memory
            std::vector<uint8_t> body(body_size);
            if(!recv_bytes(body.data(), body_size)) return false;

            iotmp_memory_decoder decoder(body.data(), body_size);
            return decoder.decode(msg, body_size);
        }

        bool write_message(iotmp_message& msg) {
            // encode_message does two-pass: null_writer for size, then string_writer
            // Result is a complete message in a single std::string
            std::string encoded = encode_message(msg);
            return send_bytes(encoded.data(), encoded.size());
        }

        void send_message(iotmp_message& msg) {
            if(msg.get_message_type() != message::STREAM_DATA) {
                THINGER_LOG_DEBUG("TX: %s (stream=%u)", msg.message_type_str(), msg.get_stream_id());
            }
            write_message(msg);
        }

        void send_keepalive() {
            THINGER_LOG_DEBUG("Keep-alive sent");
            std::string encoded = encode_message(message::KEEP_ALIVE);
            send_bytes(encoded.data(), encoded.size());
        }

        // ----- Authentication ----------------------------------------

        bool authenticate() {
            THINGER_LOG_INFO("Authenticating as %s@%s", device_id_, username_);

            iotmp_message msg(message::CONNECT);
            msg.set_random_stream_id();
            msg[message::field::PAYLOAD] = json_t::array({
                json_t(username_),
                json_t(device_id_),
                json_t(credential_)
            });

            if(!write_message(msg)) {
                THINGER_LOG_ERROR("Failed to send CONNECT");
                return false;
            }

            // Wait for response
            iotmp_message response(message::RESERVED);
            if(!read_message(response)) {
                THINGER_LOG_ERROR("No CONNECT response");
                return false;
            }

            bool ok = response.get_message_type() == message::OK;
            if(ok) {
                THINGER_LOG_INFO("Authenticated!");
            } else {
                THINGER_LOG_ERROR("Authentication failed");
            }
            return ok;
        }

        // ----- Message handling --------------------------------------

        void handle_message(iotmp_message& msg) {
            if(msg.get_message_type() != message::STREAM_DATA) {
                THINGER_LOG_DEBUG("RX: %s (stream=%u)", msg.message_type_str(), msg.get_stream_id());
            }

            switch(msg.get_message_type()) {
                case message::RUN:
                    handle_resource_request(msg);
                    break;
                case message::DESCRIBE:
                    handle_describe(msg);
                    break;
                case message::START_STREAM:
                    handle_start_stream(msg);
                    break;
                case message::STOP_STREAM:
                    handle_stop_stream(msg);
                    break;
                case message::STREAM_DATA:
                    handle_stream_data(msg);
                    break;
                case message::KEEP_ALIVE:
                    break;
                case message::DISCONNECT:
                    derived().on_disconnect();
                    break;
                default:
                    break;
            }
        }

        void handle_resource_request(iotmp_message& request) {
            // Resource path is in RESOURCE field
            std::string path = extract_resource_path(request);

            iotmp_resource* resource = find_resource(path);
            iotmp_message response(request.get_stream_id(), message::OK);

            if(resource) {
                bool success = resource->run_resource(request, response);
                if(!success) {
                    response.set_message_type(message::ERROR);
                }
            } else {
                response.set_message_type(message::ERROR);
            }

            send_message(response);

            // If the resource received input, has an active stream, and echo is enabled,
            // stream the current state so the dashboard updates
            if(resource && request.has_payload() && resource->stream_enabled() && resource->stream_echo() &&
               (resource->get_io_type() == iotmp_resource::input_wrapper ||
                resource->get_io_type() == iotmp_resource::input_output_wrapper)) {
                stream_resource(*resource, resource->get_stream_id());
            }
        }

        void handle_describe(iotmp_message& request) {
            iotmp_message response(request.get_stream_id(), message::OK);

            // Check if asking for a specific resource or the API
            if(request.has_field(message::field::RESOURCE)) {
                std::string path = extract_resource_path(request);

                iotmp_resource* resource = find_resource(path);
                if(resource) {
                    resource->describe(response);
                } else {
                    response.set_message_type(message::ERROR);
                }
            } else {
                // Describe full API: list all resources
                json_t& payload = response[message::field::PAYLOAD];
                for(auto& [name, res] : resources_) {
                    res.fill_api(payload[name]);
                }
            }

            send_message(response);
        }

        void handle_start_stream(iotmp_message& request) {
            std::string path = extract_resource_path(request);

            uint16_t stream_id = request.get_stream_id();

            iotmp_resource* resource = find_resource(path);
            if(!resource) {
                iotmp_message response(stream_id, message::ERROR);
                send_message(response);
                return;
            }

            // Set the stream id on the resource
            resource->set_stream_id(stream_id);

            // Check if there is an interval in parameters
            unsigned long interval_ms = 0;
            if(request.has_params()) {
                const json_t& params = request.params();
                if(params.is_number()) {
                    interval_ms = params.get<uint64_t>();
                }
            }

            // Register stream
            stream_config cfg;
            cfg.resource_name = nullptr;
            cfg.interval_ms = interval_ms;
            cfg.last_sample = derived().get_millis();

            // We need to store the path — find the key in resources_ map
            for(auto& [name, res] : resources_) {
                if(&res == resource) {
                    cfg.resource_name = name.c_str();
                    break;
                }
            }

            streams_[stream_id] = cfg;

            THINGER_LOG_DEBUG("Stream started: %s (id=%u)", cfg.resource_name ? cfg.resource_name : "?", stream_id);

            // Send OK
            iotmp_message response(stream_id, message::OK);
            send_message(response);

            // Send initial stream data
            stream_resource(*resource, stream_id);
        }

        void handle_stream_data(iotmp_message& request) {
            uint16_t stream_id = request.get_stream_id();

            // Find resource by stream_id
            auto it = streams_.find(stream_id);
            if(it == streams_.end()) return;

            iotmp_resource* resource = find_resource(it->second.resource_name);
            if(!resource) return;

            // Execute the resource with the incoming input
            iotmp_message response(stream_id, message::STREAM_DATA);
            resource->run_resource(request, response);

            // Echo back current state
            if(resource->stream_echo()) {
                stream_resource(*resource, stream_id);
            }
        }

        void handle_stop_stream(iotmp_message& request) {
            uint16_t stream_id = request.get_stream_id();
            THINGER_LOG_DEBUG("Stream stopped (id=%u)", stream_id);

            // Find and clean up the stream
            auto it = streams_.find(stream_id);
            if(it != streams_.end()) {
                // Clear the stream id on the resource
                iotmp_resource* resource = find_resource(it->second.resource_name);
                if(resource) {
                    resource->set_stream_id(0);
                }
                streams_.erase(it);
            }

            // Send OK
            iotmp_message response(stream_id, message::OK);
            send_message(response);
        }

        // ----- Streaming ---------------------------------------------

        bool stream_resource(iotmp_resource& resource, uint16_t stream_id) {
            if(resource.get_io_type() == iotmp_resource::none ||
               resource.get_io_type() == iotmp_resource::run) {
                return false;
            }

            iotmp_message request(message::STREAM_DATA);
            iotmp_message response(message::STREAM_DATA);
            resource.run_resource(request, response);

            // For input resources, the callback writes to request[PAYLOAD]
            // For output/input_output resources, the callback writes to response[PAYLOAD]
            auto& msg = response.has_payload() ? response : request;
            if(msg.has_payload()) {
                msg.set_stream_id(stream_id);
                send_message(msg);
            }
            return true;
        }

        void check_streams() {
            unsigned long now = derived().get_millis();
            for(auto& [stream_id, cfg] : streams_) {
                if(cfg.interval_ms == 0) continue;
                if(now - cfg.last_sample >= cfg.interval_ms) {
                    cfg.last_sample = now;
                    if(cfg.resource_name) {
                        iotmp_resource* resource = find_resource(cfg.resource_name);
                        if(resource) {
                            stream_resource(*resource, stream_id);
                        }
                    }
                }
            }
        }

        bool stream(const char* resource_name) {
            auto it = resources_.find(resource_name);
            if(it == resources_.end()) return false;
            auto& res = it->second;
            if(!res.stream_enabled()) return false;
            return stream_resource(res, res.get_stream_id());
        }

        // ----- Resource lookup ---------------------------------------

        iotmp_resource* find_resource(const char* path) {
            if(!path) return nullptr;
            return find_resource(std::string(path));
        }

        iotmp_resource* find_resource(const std::string& path) {
            auto it = resources_.find(path);
            if(it != resources_.end()) return &it->second;
            return nullptr;
        }

        // ----- Clear streams on disconnect ---------------------------

        void clear_streams() {
            streams_.clear();
            for(auto& [name, res] : resources_) {
                res.set_stream_id(0);
            }
        }

    protected:

        void notify_state(client_state state) {
            if(state_callback_) state_callback_(state);
        }

        // State callback
        state_callback_t state_callback_;

        // Credentials
        const char* username_   = nullptr;
        const char* device_id_  = nullptr;
        const char* credential_ = nullptr;

        // Host
        const char* host_ = "iot.thinger.io";
        uint16_t port_ = 25204;

        // Resources
        std::map<std::string, iotmp_resource> resources_;

        // Active streams (stream_id -> config)
        std::map<uint16_t, stream_config> streams_;

    private:

        Derived& derived() {
            return *static_cast<Derived*>(this);
        }

        const Derived& derived() const {
            return *static_cast<const Derived*>(this);
        }

        // ----- Helpers -----------------------------------------------

        static std::string extract_resource_path(const iotmp_message& request) {
            std::string path;
            if(request.has_field(message::field::RESOURCE)) {
                const json_t& res_field = request[message::field::RESOURCE];
                if(res_field.is_string()) {
                    path = res_field.get<std::string>();
                } else if(res_field.is_array()) {
                    // Concatenate array elements with '/'
                    for(size_t i = 0; i < res_field.size(); ++i) {
                        if(i > 0) path += '/';
                        path += res_field[i].get<std::string>();
                    }
                }
            }
            return path;
        }
    };

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_CLIENT_BASE_HPP
