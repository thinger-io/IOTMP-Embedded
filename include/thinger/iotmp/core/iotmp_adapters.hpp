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

#ifndef THINGER_IOTMP_IO_ADAPTERS_HPP
#define THINGER_IOTMP_IO_ADAPTERS_HPP

#include <cstring>
#include <string>

namespace thinger::iotmp {

    class memory_reader {
    private:
        const uint8_t* buffer_;
        size_t size_;
        size_t read_ = 0;

    public:
        memory_reader(const void* buffer, size_t size)
            : buffer_(static_cast<const uint8_t*>(buffer)), size_(size) {}

        bool read(void* target, size_t size) {
            if(read_ + size <= size_) {
                memcpy(target, buffer_ + read_, size);
                read_ += size;
                return true;
            }
            return false;
        }

        bool read(void* target) {
            return read(target, 1);
        }

        size_t bytes_read() const { return read_; }
    };

    class string_writer {
    private:
        std::string& string_;
        size_t written_ = 0;

    public:
        explicit string_writer(std::string& string) : string_(string) {}

        bool write(const void* buffer, size_t size) {
            string_.append(static_cast<const char*>(buffer), size);
            written_ += size;
            return true;
        }

        size_t bytes_written() const { return written_; }
    };

    class null_writer {
    private:
        size_t written_ = 0;

    public:
        bool write(const void*, size_t size) {
            written_ += size;
            return true;
        }

        size_t bytes_written() const { return written_; }
    };

    class memory_writer {
    private:
        uint8_t* buffer_;
        uint8_t* current_;
        uint8_t* end_;

    public:
        memory_writer(void* buffer, size_t size)
            : buffer_(static_cast<uint8_t*>(buffer)),
              current_(buffer_),
              end_(buffer_ + size) {}

        void reset() { current_ = buffer_; }

        size_t bytes_written() const { return current_ - buffer_; }

        bool write(const void* data, size_t size) {
            if(current_ + size > end_) return false;
            memcpy(current_, data, size);
            current_ += size;
            return true;
        }
    };

}

#endif
