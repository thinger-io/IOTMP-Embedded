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

#ifndef PSON_DECODER_HPP
#define PSON_DECODER_HPP

#include "iotmp_value.hpp"
#include "pson_types.hpp"

namespace thinger::iotmp {

    template<class Reader>
    class pson_decoder {
    public:
        explicit pson_decoder(Reader& reader) : reader_(reader) {}

        size_t bytes_read() const { return reader_.bytes_read(); }

        bool pb_decode_tag(pson_wire_type& wire_type, uint64_t& value) {
            uint8_t byte;
            if(!read_byte(&byte)) return false;

            wire_type = static_cast<pson_wire_type>(byte >> 5);
            value = byte & 0x1f;

            if(value == 0x1f) {
                return pb_decode_varint64(value);
            }
            return true;
        }

        bool pb_decode_varint32(uint32_t& varint) {
            varint = 0;
            uint8_t byte;
            uint8_t bit_pos = 0;

            do {
                if(!read_byte(&byte) || bit_pos >= 32) return false;
                varint |= static_cast<uint32_t>(byte & 0x7F) << bit_pos;
                bit_pos += 7;
            } while(byte >= 0x80);

            return true;
        }

        bool pb_decode_varint64(uint64_t& varint) {
            varint = 0;
            uint8_t byte;
            uint8_t bit_pos = 0;

            do {
                if(!read_byte(&byte) || bit_pos >= 64) return false;
                varint |= static_cast<uint64_t>(byte & 0x7F) << bit_pos;
                bit_pos += 7;
            } while(byte >= 0x80);

            return true;
        }

        bool decode(iotmp_value& value) {
            pson_wire_type type;
            uint64_t type_payload;

            if(!pb_decode_tag(type, type_payload)) return false;

            switch(type) {
                case pson_wire_type::unsigned_t:
                    value = type_payload;
                    return true;

                case pson_wire_type::signed_t:
                    value = -static_cast<int64_t>(type_payload);
                    return true;

                case pson_wire_type::floating_t:
                    switch(type_payload) {
                        case 0: {
                            float float_value = 0;
                            if(!read(&float_value, sizeof(float))) return false;
                            value = float_value;
                            return true;
                        }
                        case 1: {
                            double double_value = 0;
                            if(!read(&double_value, sizeof(double))) return false;
                            value = double_value;
                            return true;
                        }
                        default:
                            return false;
                    }

                case pson_wire_type::discrete_t:
                    switch(type_payload) {
                        case 0: value = false; return true;
                        case 1: value = true; return true;
                        case 2: value = nullptr; return true;
                        default: return false;
                    }

                case pson_wire_type::string_t: {
                    if(type_payload > UINT16_MAX) return false;
                    std::string str;
                    str.resize(type_payload);
                    if(!read(&str[0], type_payload)) return false;
                    value = std::move(str);
                    return true;
                }

                case pson_wire_type::bytes_t: {
                    if(type_payload > UINT16_MAX) return false;
                    std::vector<uint8_t> vec(type_payload);
                    if(!read(vec.data(), type_payload)) return false;
                    value = iotmp_value::binary(std::move(vec));
                    return true;
                }

                case pson_wire_type::map_t: {
                    value = iotmp_value::object();
                    for(size_t i = 0; i < type_payload; ++i) {
                        if(!decode_pair(value)) return false;
                    }
                    return true;
                }

                case pson_wire_type::array_t: {
                    value = iotmp_value::array();
                    for(size_t i = 0; i < type_payload; ++i) {
                        iotmp_value element;
                        if(!decode(element)) return false;
                        value.emplace_back(std::move(element));
                    }
                    return true;
                }

                default:
                    return false;
            }
        }

    private:
        Reader& reader_;

        bool read_byte(uint8_t* byte) {
            return reader_.read(byte);
        }

        bool read(void* buffer, size_t size) {
            return reader_.read(buffer, size);
        }

        bool decode_pair(iotmp_value& object) {
            pson_wire_type type;
            uint64_t key_size;

            if(!pb_decode_tag(type, key_size) || type != pson_wire_type::string_t) {
                return false;
            }

            if(key_size > UINT16_MAX) return false;

            std::string key;
            key.resize(key_size);
            if(!read(&key[0], key_size)) return false;

            return decode(object[key]);
        }
    };

}

#endif
