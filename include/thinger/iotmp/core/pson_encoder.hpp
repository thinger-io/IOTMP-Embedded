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

#ifndef PSON_ENCODER_HPP
#define PSON_ENCODER_HPP

#include "iotmp_value.hpp"
#include "pson_types.hpp"

namespace thinger::iotmp {

    template<class Writer>
    class pson_encoder {
    public:
        explicit pson_encoder(Writer& writer) : writer_(writer) {}

        size_t bytes_written() const { return writer_.bytes_written(); }

        bool pb_encode_tag_fixed(pson_wire_type wire_type, uint8_t value) {
            const uint8_t tag = (static_cast<uint8_t>(wire_type) << 5) | value;
            return write(&tag);
        }

        bool pb_encode_tag(pson_wire_type wire_type, uint64_t value) {
            if(value < 0x1f) {
                return pb_encode_tag_fixed(wire_type, static_cast<uint8_t>(value));
            }
            return pb_encode_tag_fixed(wire_type, 0x1f) && pb_write_varint(value);
        }

        bool pb_write_varint(uint64_t value) {
            do {
                uint8_t byte = (value & 0x7F);
                value >>= 7;
                if(value > 0) byte |= 0x80;
                if(!write(&byte)) return false;
            } while(value > 0);
            return true;
        }

        bool pb_encode_string(const char* str, size_t len) {
            return pb_encode_tag(pson_wire_type::string_t, len) && write(str, len);
        }

        bool pb_encode_bytes(const void* data, size_t size) {
            return pb_encode_tag(pson_wire_type::bytes_t, size) && write(data, size);
        }

        bool pb_encode_float(float value) {
            return pb_encode_tag_fixed(pson_wire_type::floating_t, 0) && write(&value, sizeof(float));
        }

        bool pb_encode_double(double value) {
            return pb_encode_tag_fixed(pson_wire_type::floating_t, 1) && write(&value, sizeof(double));
        }

        bool encode(const iotmp_value& value) {
            switch(value.type()) {
                case iotmp_value::value_t::boolean:
                    return pb_encode_tag_fixed(pson_wire_type::discrete_t, value.get<bool>() ? 1 : 0);

                case iotmp_value::value_t::null:
                    return pb_encode_tag_fixed(pson_wire_type::discrete_t, 2);

                case iotmp_value::value_t::number_integer: {
                    int64_t signed_value = value.get<int64_t>();
                    if(signed_value < 0) {
                        return pb_encode_tag(pson_wire_type::signed_t, static_cast<uint64_t>(-signed_value));
                    }
                    return pb_encode_tag(pson_wire_type::unsigned_t, static_cast<uint64_t>(signed_value));
                }

                case iotmp_value::value_t::number_unsigned:
                    return pb_encode_tag(pson_wire_type::unsigned_t, value.get<uint64_t>());

                case iotmp_value::value_t::number_float: {
                    double double_value = value.get<double>();

                    // Check if it can be saved as integer
                    auto int_value = static_cast<int64_t>(double_value);
                    if(static_cast<double>(int_value) == double_value) {
                        if(int_value < 0) {
                            return pb_encode_tag(pson_wire_type::signed_t, static_cast<uint64_t>(-int_value));
                        }
                        return pb_encode_tag(pson_wire_type::unsigned_t, static_cast<uint64_t>(int_value));
                    }

                    // Check if float precision is enough
                    auto float_value = static_cast<float>(double_value);
                    if(static_cast<double>(float_value) == double_value) {
                        return pb_encode_float(float_value);
                    }

                    return pb_encode_double(double_value);
                }

                case iotmp_value::value_t::string: {
                    const auto& str = value.get_string();
                    return pb_encode_string(str.c_str(), str.size());
                }

                case iotmp_value::value_t::binary: {
                    const auto& bin = value.get_binary();
                    return pb_encode_bytes(bin.data(), bin.size());
                }

                case iotmp_value::value_t::array: {
                    if(!pb_encode_tag(pson_wire_type::array_t, value.size())) return false;
                    return encode_array_elements(value);
                }

                case iotmp_value::value_t::object: {
                    if(!pb_encode_tag(pson_wire_type::map_t, value.size())) return false;
                    for(const auto& [key, val] : value.items()) {
                        if(!pb_encode_string(key.c_str(), key.size())) return false;
                        if(!encode(val)) return false;
                    }
                    return true;
                }

                default:
                    return false;
            }
        }

    private:
        Writer& writer_;

        bool write(const void* data, size_t size = 1) {
            return writer_.write(data, size);
        }

        bool encode_array_elements(const iotmp_value& arr) {
            for(size_t i = 0; i < arr.size(); ++i) {
                if(!encode(arr[i])) return false;
            }
            return true;
        }
    };

}

#endif
