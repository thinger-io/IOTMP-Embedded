// The MIT License (MIT)
//
// Copyright (c) 2017 THINK BIG LABS S.L.
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

#ifndef THINGER_IOTMP_ENCODER_HPP
#define THINGER_IOTMP_ENCODER_HPP

#include "iotmp_message.hpp"
#include "iotmp_adapters.hpp"
#include "pson_encoder.hpp"

namespace thinger::iotmp {

    template<class Writer>
    class iotmp_encoder {
    public:
        template<class... Args>
        explicit iotmp_encoder(Args&&... args) : writer_(std::forward<Args>(args)...) {}

        size_t bytes_written() const { return writer_.bytes_written(); }

        void pb_write_varint(uint64_t value) {
            do {
                auto byte = static_cast<uint8_t>(value & 0x7F);
                value >>= 7;
                if(value > 0) byte |= 0x80;
                writer_.write(&byte, 1);
            } while(value > 0);
        }

        void encode(const iotmp_message& message) {
            message.for_each_field([this](uint8_t field, const json_t& value) {
                if(value.is_number_unsigned() || value.is_number_integer()) {
                    encode_field(message::wire_type::varint, field);
                    pb_write_varint(value.get<uint64_t>());
                } else {
                    encode_field(message::wire_type::pson_v2, field);
                    pson_encoder<Writer> encoder(writer_);
                    encoder.encode(value);
                }
            });
        }

    private:
        Writer writer_;

        void encode_field(message::wire_type wire_type, uint8_t field_number) {
            uint8_t tag = (field_number << 3) | static_cast<uint8_t>(wire_type);
            writer_.write(&tag, 1);
        }
    };

    // Encode a complete message (header + body) to a string
    inline std::string encode_message(const iotmp_message& message) {
        // First pass: calculate body size
        iotmp_encoder<null_writer> sizer;
        sizer.encode(message);
        size_t body_size = sizer.bytes_written();

        // Second pass: encode to string
        std::string output;
        output.reserve(10 + body_size);

        iotmp_encoder<string_writer> encoder(output);
        encoder.pb_write_varint(static_cast<uint8_t>(message.get_message_type()));
        encoder.pb_write_varint(body_size);
        encoder.encode(message);

        return output;
    }

    // Encode a message with no body (e.g., KEEP_ALIVE)
    inline std::string encode_message(message::type type) {
        std::string output;
        iotmp_encoder<string_writer> encoder(output);
        encoder.pb_write_varint(static_cast<uint8_t>(type));
        encoder.pb_write_varint(0);
        return output;
    }

}

#endif
