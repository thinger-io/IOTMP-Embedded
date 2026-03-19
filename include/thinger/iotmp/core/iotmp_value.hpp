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

#ifndef THINGER_IOTMP_VALUE_HPP
#define THINGER_IOTMP_VALUE_HPP

#include <cstdint>
#include <cstring>
#include <string>
#include <vector>
#include <utility>
#include <type_traits>
#include <initializer_list>

namespace thinger::iotmp {

class iotmp_value {
public:
    using object_t = std::vector<std::pair<std::string, iotmp_value>>;
    using array_t  = std::vector<iotmp_value>;
    using binary_t = std::vector<uint8_t>;

    enum class value_t : uint8_t {
        null,
        boolean,
        number_unsigned,
        number_integer,
        number_float,
        string,
        binary,
        array,
        object
    };

private:
    value_t type_ = value_t::null;

    union data_union {
        bool        b;
        uint64_t    u;
        int64_t     i;
        double      d;
        void*       ptr;

        data_union() : ptr(nullptr) {}
    } data_;

    void destroy() {
        switch(type_) {
            case value_t::string: delete str_ptr(); break;
            case value_t::binary: delete bin_ptr(); break;
            case value_t::array:  delete arr_ptr(); break;
            case value_t::object: delete obj_ptr(); break;
            default: break;
        }
        type_ = value_t::null;
        data_.ptr = nullptr;
    }

    void copy_from(const iotmp_value& o) {
        type_ = o.type_;
        switch(type_) {
            case value_t::boolean:         data_.b = o.data_.b; break;
            case value_t::number_unsigned: data_.u = o.data_.u; break;
            case value_t::number_integer:  data_.i = o.data_.i; break;
            case value_t::number_float:    data_.d = o.data_.d; break;
            case value_t::string: data_.ptr = new std::string(*o.str_ptr()); break;
            case value_t::binary: data_.ptr = new binary_t(*o.bin_ptr()); break;
            case value_t::array:  data_.ptr = new array_t(*o.arr_ptr()); break;
            case value_t::object: data_.ptr = new object_t(*o.obj_ptr()); break;
            default: data_.ptr = nullptr; break;
        }
    }

    // Typed pointer helpers
    std::string* str_ptr() const { return static_cast<std::string*>(data_.ptr); }
    binary_t*    bin_ptr() const { return static_cast<binary_t*>(data_.ptr); }
    array_t*     arr_ptr() const { return static_cast<array_t*>(data_.ptr); }
    object_t*    obj_ptr() const { return static_cast<object_t*>(data_.ptr); }

    // Ensure this value is an object (auto-promote from null)
    object_t& ensure_object() {
        if(type_ != value_t::object) {
            destroy();
            type_ = value_t::object;
            data_.ptr = new object_t();
        }
        return *obj_ptr();
    }

    // Ensure this value is an array (auto-promote from null)
    array_t& ensure_array() {
        if(type_ != value_t::array) {
            destroy();
            type_ = value_t::array;
            data_.ptr = new array_t();
        }
        return *arr_ptr();
    }

public:
    // ======================== Constructors ========================

    iotmp_value() = default;
    iotmp_value(std::nullptr_t) : type_(value_t::null) {}
    iotmp_value(bool v) : type_(value_t::boolean) { data_.b = v; }

    // Accept any unsigned integer type (except bool)
    template<typename T, std::enable_if_t<
        std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>, int> = 0>
    iotmp_value(T v) : type_(value_t::number_unsigned) { data_.u = v; }

    // Accept any signed integer type
    template<typename T, std::enable_if_t<
        std::is_integral_v<T> && std::is_signed_v<T>, int> = 0>
    iotmp_value(T v) : type_(value_t::number_integer) { data_.i = v; }

    iotmp_value(float v)  : type_(value_t::number_float) { data_.d = v; }
    iotmp_value(double v) : type_(value_t::number_float) { data_.d = v; }

    iotmp_value(const char* v) : type_(value_t::string) {
        data_.ptr = new std::string(v);
    }

    iotmp_value(const std::string& v) : type_(value_t::string) {
        data_.ptr = new std::string(v);
    }

    iotmp_value(std::string&& v) : type_(value_t::string) {
        data_.ptr = new std::string(std::move(v));
    }

    // Copy
    iotmp_value(const iotmp_value& o) { copy_from(o); }

    // Move
    iotmp_value(iotmp_value&& o) noexcept : type_(o.type_), data_(o.data_) {
        o.type_ = value_t::null;
        o.data_.ptr = nullptr;
    }

    ~iotmp_value() { destroy(); }

    // ======================== Assignment ========================

    iotmp_value& operator=(const iotmp_value& o) {
        if(this != &o) { destroy(); copy_from(o); }
        return *this;
    }

    iotmp_value& operator=(iotmp_value&& o) noexcept {
        if(this != &o) {
            destroy();
            type_ = o.type_; data_ = o.data_;
            o.type_ = value_t::null; o.data_.ptr = nullptr;
        }
        return *this;
    }

    iotmp_value& operator=(std::nullptr_t) { destroy(); return *this; }
    iotmp_value& operator=(bool v) { destroy(); type_ = value_t::boolean; data_.b = v; return *this; }

    template<typename T, std::enable_if_t<
        std::is_integral_v<T> && std::is_unsigned_v<T> && !std::is_same_v<T, bool>, int> = 0>
    iotmp_value& operator=(T v) { destroy(); type_ = value_t::number_unsigned; data_.u = v; return *this; }

    template<typename T, std::enable_if_t<
        std::is_integral_v<T> && std::is_signed_v<T>, int> = 0>
    iotmp_value& operator=(T v) { destroy(); type_ = value_t::number_integer; data_.i = v; return *this; }

    iotmp_value& operator=(float v)  { destroy(); type_ = value_t::number_float; data_.d = v; return *this; }
    iotmp_value& operator=(double v) { destroy(); type_ = value_t::number_float; data_.d = v; return *this; }

    iotmp_value& operator=(const char* v) {
        destroy(); type_ = value_t::string; data_.ptr = new std::string(v); return *this;
    }

    iotmp_value& operator=(const std::string& v) {
        destroy(); type_ = value_t::string; data_.ptr = new std::string(v); return *this;
    }

    iotmp_value& operator=(std::string&& v) {
        destroy(); type_ = value_t::string; data_.ptr = new std::string(std::move(v)); return *this;
    }

    // ======================== Type queries ========================

    value_t type() const { return type_; }

    bool is_null()            const { return type_ == value_t::null; }
    bool is_boolean()         const { return type_ == value_t::boolean; }
    bool is_number()          const { return type_ == value_t::number_unsigned ||
                                             type_ == value_t::number_integer ||
                                             type_ == value_t::number_float; }
    bool is_number_unsigned() const { return type_ == value_t::number_unsigned; }
    bool is_number_integer()  const { return type_ == value_t::number_integer; }
    bool is_number_float()    const { return type_ == value_t::number_float; }
    bool is_string()          const { return type_ == value_t::string; }
    bool is_binary()          const { return type_ == value_t::binary; }
    bool is_array()           const { return type_ == value_t::array; }
    bool is_object()          const { return type_ == value_t::object; }

    // ======================== Value access ========================

    template<typename T>
    T get() const {
        if constexpr (std::is_same_v<T, bool>) {
            return data_.b;
        } else if constexpr (std::is_integral_v<T> && std::is_unsigned_v<T>) {
            switch(type_) {
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_integer:  return static_cast<T>(data_.i);
                case value_t::number_float:    return static_cast<T>(data_.d);
                default: return T{};
            }
        } else if constexpr (std::is_integral_v<T> && std::is_signed_v<T>) {
            switch(type_) {
                case value_t::number_integer:  return static_cast<T>(data_.i);
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_float:    return static_cast<T>(data_.d);
                default: return T{};
            }
        } else if constexpr (std::is_floating_point_v<T>) {
            switch(type_) {
                case value_t::number_float:    return static_cast<T>(data_.d);
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_integer:  return static_cast<T>(data_.i);
                default: return T{};
            }
        } else if constexpr (std::is_same_v<T, std::string>) {
            return type_ == value_t::string ? *str_ptr() : std::string{};
        }
        return T{};
    }

    const std::string& get_string() const {
        static const std::string empty;
        return type_ == value_t::string ? *str_ptr() : empty;
    }

    const binary_t& get_binary() const {
        static const binary_t empty;
        return type_ == value_t::binary ? *bin_ptr() : empty;
    }

    // ======================== Object access ========================

    iotmp_value& operator[](const char* key) {
        auto& obj = ensure_object();
        for(auto& [k, v] : obj) {
            if(k == key) return v;
        }
        obj.emplace_back(std::string(key), iotmp_value());
        return obj.back().second;
    }

    const iotmp_value& operator[](const char* key) const {
        static const iotmp_value null_val;
        if(type_ != value_t::object) return null_val;
        for(const auto& [k, v] : *obj_ptr()) {
            if(k == key) return v;
        }
        return null_val;
    }

    // For std::string keys
    iotmp_value& operator[](const std::string& key) { return operator[](key.c_str()); }
    const iotmp_value& operator[](const std::string& key) const { return operator[](key.c_str()); }

    bool contains(const char* key) const {
        if(type_ != value_t::object) return false;
        for(const auto& [k, v] : *obj_ptr()) {
            if(k == key) return true;
        }
        return false;
    }

    bool contains(const std::string& key) const { return contains(key.c_str()); }

    // Object iteration (returns ref to underlying vector of pairs)
    const object_t& items() const {
        static const object_t empty;
        return type_ == value_t::object ? *obj_ptr() : empty;
    }

    object_t& items() {
        return ensure_object();
    }

    // ======================== Array access ========================

    iotmp_value& operator[](size_t index) {
        auto& arr = ensure_array();
        if(index >= arr.size()) arr.resize(index + 1);
        return arr[index];
    }

    const iotmp_value& operator[](size_t index) const {
        static const iotmp_value null_val;
        if(type_ != value_t::array) return null_val;
        const auto& arr = *arr_ptr();
        return index < arr.size() ? arr[index] : null_val;
    }

    void emplace_back(iotmp_value&& v) {
        ensure_array().emplace_back(std::move(v));
    }

    void push_back(const iotmp_value& v) {
        ensure_array().push_back(v);
    }

    // ======================== Size / empty ========================

    size_t size() const {
        switch(type_) {
            case value_t::object: return obj_ptr()->size();
            case value_t::array:  return arr_ptr()->size();
            case value_t::string: return str_ptr()->size();
            case value_t::binary: return bin_ptr()->size();
            default: return 0;
        }
    }

    bool empty() const {
        switch(type_) {
            case value_t::null: return true;
            case value_t::object: return obj_ptr()->empty();
            case value_t::array:  return arr_ptr()->empty();
            case value_t::string: return str_ptr()->empty();
            case value_t::binary: return bin_ptr()->empty();
            default: return false;
        }
    }

    // ======================== Swap ========================

    void swap(iotmp_value& o) noexcept {
        std::swap(type_, o.type_);
        std::swap(data_, o.data_);
    }

    // ======================== Static factories ========================

    static iotmp_value array() {
        iotmp_value v;
        v.type_ = value_t::array;
        v.data_.ptr = new array_t();
        return v;
    }

    static iotmp_value array(std::initializer_list<iotmp_value> init) {
        iotmp_value v;
        v.type_ = value_t::array;
        v.data_.ptr = new array_t(init);
        return v;
    }

    static iotmp_value object() {
        iotmp_value v;
        v.type_ = value_t::object;
        v.data_.ptr = new object_t();
        return v;
    }

    static iotmp_value binary(std::vector<uint8_t> data) {
        iotmp_value v;
        v.type_ = value_t::binary;
        v.data_.ptr = new binary_t(std::move(data));
        return v;
    }

    static iotmp_value binary(const uint8_t* data, size_t size) {
        iotmp_value v;
        v.type_ = value_t::binary;
        v.data_.ptr = new binary_t(data, data + size);
        return v;
    }
};

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_VALUE_HPP
