// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS SL
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

#ifndef THINGER_IOTMP_MESSAGE_HPP
#define THINGER_IOTMP_MESSAGE_HPP

#include "iotmp_types.hpp"
#include <cstdlib>

namespace thinger::iotmp {

    namespace message {

        enum wire_type {
            varint   = 0x00,
            pson_v1  = 0x01,
            pson_v2  = 0x02,
        };

        enum type {
            RESERVED      = 0x00,
            OK            = 0x01,
            ERROR         = 0x02,
            CONNECT       = 0x03,
            DISCONNECT    = 0x04,
            KEEP_ALIVE    = 0x05,
            RUN           = 0x06,
            DESCRIBE      = 0x07,
            START_STREAM  = 0x08,
            STOP_STREAM   = 0x09,
            STREAM_DATA   = 0x0a
        };

        enum field {
            STREAM_ID   = 0x01,
            PARAMETERS  = 0x02,
            PAYLOAD     = 0x03,
            RESOURCE    = 0x04,
        };

        namespace connect {
            constexpr const char* PROTOCOL_VERSION = "pv";
            constexpr const char* KEEP_ALIVE = "ka";
            constexpr const char* AUTH_TYPE = "at";
        }
    }

    namespace server {
        enum run {
            READ_DEVICE_PROPERTY  = 0x01,
            SET_DEVICE_PROPERTY   = 0x02,
            CALL_DEVICE           = 0x03,
            CALL_ENDPOINT         = 0x04,
            WRITE_BUCKET          = 0x05,
            LOCK_SYNC             = 0x06,
            UNLOCK_SYNC           = 0x07,
            SUBSCRIBE_EVENT       = 0x08
        };
    }

    class iotmp_message {
    public:
        static constexpr size_t MAX_FIELDS = 8;

        explicit iotmp_message(message::type type) : message_type_(type) {}

        explicit iotmp_message(uint16_t stream_id, message::type type) : message_type_(type) {
            set_stream_id(stream_id);
        }

        ~iotmp_message() = default;

        // Move
        iotmp_message(iotmp_message&&) = default;
        iotmp_message& operator=(iotmp_message&&) = default;

        // Copy
        iotmp_message(const iotmp_message&) = default;
        iotmp_message& operator=(const iotmp_message&) = default;

        // Type accessors
        message::type get_message_type() const { return message_type_; }
        void set_message_type(message::type type) { message_type_ = type; }

        const char* message_type_str() const {
            switch(message_type_) {
                case message::type::OK:           return "OK";
                case message::type::ERROR:        return "ERROR";
                case message::type::KEEP_ALIVE:   return "KEEP_ALIVE";
                case message::type::RUN:          return "RUN";
                case message::type::DESCRIBE:     return "DESCRIBE";
                case message::type::START_STREAM: return "START_STREAM";
                case message::type::STOP_STREAM:  return "STOP_STREAM";
                case message::type::CONNECT:      return "CONNECT";
                case message::type::STREAM_DATA:  return "STREAM_DATA";
                case message::type::DISCONNECT:   return "DISCONNECT";
                default:                          return "UNKNOWN";
            }
        }

        // Field access
        json_t& operator[](uint8_t field) {
            if(field < MAX_FIELDS) field_mask_ |= (1 << field);
            return fields_[field < MAX_FIELDS ? field : 0];
        }

        const json_t& operator[](uint8_t field) const {
            static const json_t null_val;
            return (field < MAX_FIELDS) ? fields_[field] : null_val;
        }

        bool has_field(uint8_t field) const {
            return field < MAX_FIELDS && (field_mask_ & (1 << field));
        }

        bool remove_field(uint8_t field) {
            if(field >= MAX_FIELDS || !(field_mask_ & (1 << field))) return false;
            field_mask_ &= ~(1 << field);
            fields_[field] = json_t();
            return true;
        }

        // Iterate over set fields. Calls func(field_id, value) for each.
        template<typename Func>
        void for_each_field(Func&& func) const {
            for(uint8_t i = 0; i < MAX_FIELDS; ++i) {
                if(field_mask_ & (1 << i)) {
                    func(i, fields_[i]);
                }
            }
        }

        // Stream ID helpers
        uint16_t get_stream_id() const {
            if(has_field(message::field::STREAM_ID) && fields_[message::field::STREAM_ID].is_number()) {
                return fields_[message::field::STREAM_ID].get<uint16_t>();
            }
            return 0;
        }

        void set_stream_id(uint16_t stream_id) {
            field_mask_ |= (1 << message::field::STREAM_ID);
            fields_[message::field::STREAM_ID] = stream_id;
        }

        void set_random_stream_id() {
            set_stream_id(static_cast<uint16_t>(rand()));
        }

        // Convenience accessors
        json_t& params()        { return operator[](message::field::PARAMETERS); }
        json_t& payload()       { return operator[](message::field::PAYLOAD); }

        const json_t& params()  const { return operator[](message::field::PARAMETERS); }
        const json_t& payload() const { return operator[](message::field::PAYLOAD); }

        bool has_params()  const { return has_field(message::field::PARAMETERS); }
        bool has_payload() const { return has_field(message::field::PAYLOAD); }

    private:
        message::type message_type_;
        uint8_t field_mask_ = 0;
        json_t fields_[MAX_FIELDS];
    };

}

#endif
