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

#ifndef THINGER_IOTMP_RESOURCE_HPP
#define THINGER_IOTMP_RESOURCE_HPP

#include "iotmp_message.hpp"
#include <functional>

namespace thinger::iotmp {

    class input {
    public:
        input(uint16_t stream_id, json_t& data, bool describe = false)
            : stream_id_(stream_id), data_(data), describe_(describe) {}

        template<class T>
        void operator=(T value) { data_ = value; }

        template<class T>
        operator T() {
            if(data_.is_null()) data_ = T{};
            return data_.get<T>();
        }

        operator json_t&() { return data_; }

        json_t& operator[](const char* name) { return data_[name]; }

        uint16_t get_stream_id() const { return stream_id_; }
        bool describe() const { return describe_; }
        bool is_empty() const { return data_.is_null() || data_.empty(); }
        json_t& payload() { return data_; }
        const json_t& payload() const { return data_; }

    private:
        uint16_t stream_id_ = 0;
        json_t&  data_;
        bool     describe_ = false;
    };

    class output {
    public:
        output(json_t& data, bool describe = false)
            : data_(data), describe_(describe) {}

        template<class T>
        void operator=(T value) { data_ = value; }

        template<class T>
        operator T() { return data_.get<T>(); }

        operator json_t&() { return data_; }

        json_t& operator[](const char* name) { return data_[name]; }

        bool describe() const { return describe_; }
        bool is_success() const { return success_; }
        bool is_empty() const { return data_.is_null() || data_.empty(); }

        void set_error(const char* msg) {
            success_ = false;
            data_["error"] = msg;
        }

        void set_error(int code, const char* msg) {
            success_ = false;
            code_ = code;
            data_["error"] = msg;
        }

        void set_return_code(int code) { code_ = code; }
        int get_return_code() const { return code_; }

        json_t& payload() { return data_; }
        const json_t& payload() const { return data_; }

    private:
        json_t& data_;
        int     code_ = 0;
        bool    describe_ = false;
        bool    success_ = true;
    };

    class iotmp_resource {
    public:
        enum io_type {
            none                 = 0,
            run                  = 1,
            input_wrapper        = 2,
            output_wrapper       = 3,
            input_output_wrapper = 4
        };

        iotmp_resource() = default;

        io_type get_io_type() const { return io_type_; }

        bool stream_enabled() const { return stream_id_ != 0; }
        void set_stream_id(uint16_t id) { stream_id_ = id; }
        uint16_t get_stream_id() const { return stream_id_; }

        bool stream_echo() const { return stream_echo_; }
        void set_stream_echo(bool enabled) { stream_echo_ = enabled; }

        // Register a run callback (no input, no output)
        iotmp_resource& operator=(std::function<void()> fn) {
            io_type_ = run;
            run_fn_ = std::move(fn);
            return *this;
        }

        // Register an input callback
        iotmp_resource& operator=(std::function<void(input&)> fn) {
            io_type_ = input_wrapper;
            input_fn_ = std::move(fn);
            return *this;
        }

        // Register an output callback
        iotmp_resource& operator=(std::function<void(output&)> fn) {
            io_type_ = output_wrapper;
            output_fn_ = std::move(fn);
            return *this;
        }

        // Register an input/output callback
        iotmp_resource& operator=(std::function<void(input&, output&)> fn) {
            io_type_ = input_output_wrapper;
            input_output_fn_ = std::move(fn);
            return *this;
        }

        // Execute the resource
        bool run_resource(iotmp_message& request, iotmp_message& response) {
            bool success = true;
            switch(io_type_) {
                case run:
                    run_fn_();
                    break;
                case input_wrapper: {
                    input in(request.get_stream_id(), request[message::field::PAYLOAD]);
                    input_fn_(in);
                    break;
                }
                case output_wrapper: {
                    output out(response[message::field::PAYLOAD]);
                    output_fn_(out);
                    if(out.get_return_code() != 0) {
                        response[message::field::PARAMETERS] = out.get_return_code();
                    }
                    success = out.is_success();
                    break;
                }
                case input_output_wrapper: {
                    input in(request.get_stream_id(), request[message::field::PAYLOAD]);
                    output out(response[message::field::PAYLOAD], request[message::field::PAYLOAD].empty());
                    input_output_fn_(in, out);
                    if(out.get_return_code() != 0) {
                        response[message::field::PARAMETERS] = out.get_return_code();
                    }
                    success = out.is_success();
                    break;
                }
                case none:
                    break;
            }
            return success;
        }

        // Describe the resource (auto-discovery)
        void describe(iotmp_message& message) {
            switch(io_type_) {
                case output_wrapper: {
                    json_t out_data;
                    output wrapper(out_data, true);
                    output_fn_(wrapper);
                    if(!out_data.is_null()) {
                        message[message::field::PAYLOAD]["out"].swap(out_data);
                    }
                    break;
                }
                case input_wrapper: {
                    json_t in_data;
                    input wrapper(message.get_stream_id(), in_data, true);
                    input_fn_(wrapper);
                    if(!in_data.is_null()) {
                        message[message::field::PAYLOAD]["in"].swap(in_data);
                    }
                    break;
                }
                case input_output_wrapper: {
                    json_t in_data, out_data;
                    input in_w(message.get_stream_id(), in_data, true);
                    output out_w(out_data, true);
                    input_output_fn_(in_w, out_w);
                    if(!in_data.is_null()) {
                        message[message::field::PAYLOAD]["in"].swap(in_data);
                    }
                    if(!out_data.is_null()) {
                        message[message::field::PAYLOAD]["out"].swap(out_data);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        // Fill API descriptor
        void fill_api(json_t& content) {
            if(io_type_ != none) {
                content["fn"] = static_cast<int>(io_type_);
            }
        }

    private:
        io_type io_type_ = none;
        uint16_t stream_id_ = 0;
        bool stream_echo_ = true;

        std::function<void()>                   run_fn_;
        std::function<void(input&)>             input_fn_;
        std::function<void(output&)>            output_fn_;
        std::function<void(input&, output&)>    input_output_fn_;
    };

}

#endif
