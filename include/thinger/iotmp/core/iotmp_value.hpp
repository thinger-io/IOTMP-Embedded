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
        object,
        discarded
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

    static std::string escape_json(const std::string& s) {
        std::string result;
        result.reserve(s.size());
        for(char c : s) {
            switch(c) {
                case '"':  result += "\\\""; break;
                case '\\': result += "\\\\"; break;
                case '\n': result += "\\n"; break;
                case '\r': result += "\\r"; break;
                case '\t': result += "\\t"; break;
                default:   result += c; break;
            }
        }
        return result;
    }

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
        std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value, int> = 0>
    iotmp_value(T v) : type_(value_t::number_unsigned) { data_.u = v; }

    // Accept any signed integer type
    template<typename T, std::enable_if_t<
        std::is_integral<T>::value && std::is_signed<T>::value, int> = 0>
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
        std::is_integral<T>::value && std::is_unsigned<T>::value && !std::is_same<T, bool>::value, int> = 0>
    iotmp_value& operator=(T v) { destroy(); type_ = value_t::number_unsigned; data_.u = v; return *this; }

    template<typename T, std::enable_if_t<
        std::is_integral<T>::value && std::is_signed<T>::value, int> = 0>
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
        if constexpr (std::is_same<T, bool>::value) {
            return data_.b;
        } else if constexpr (std::is_integral<T>::value && std::is_unsigned<T>::value) {
            switch(type_) {
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_integer:  return static_cast<T>(data_.i);
                case value_t::number_float:    return static_cast<T>(data_.d);
                default: return T{};
            }
        } else if constexpr (std::is_integral<T>::value && std::is_signed<T>::value) {
            switch(type_) {
                case value_t::number_integer:  return static_cast<T>(data_.i);
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_float:    return static_cast<T>(data_.d);
                default: return T{};
            }
        } else if constexpr (std::is_floating_point<T>::value) {
            switch(type_) {
                case value_t::number_float:    return static_cast<T>(data_.d);
                case value_t::number_unsigned: return static_cast<T>(data_.u);
                case value_t::number_integer:  return static_cast<T>(data_.i);
                default: return T{};
            }
        } else if constexpr (std::is_same<T, std::string>::value) {
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

    // ======================== JSON serialization ========================

    std::string dump() const {
        switch(type_) {
            case value_t::null:    return "null";
            case value_t::boolean: return data_.b ? "true" : "false";
            case value_t::number_unsigned: return std::to_string(data_.u);
            case value_t::number_integer:  return std::to_string(data_.i);
            case value_t::number_float:    return std::to_string(data_.d);
            case value_t::string:
                return "\"" + escape_json(*str_ptr()) + "\"";
            case value_t::binary:
                return "\"<binary:" + std::to_string(bin_ptr()->size()) + ">\"";
            case value_t::array: {
                std::string s = "[";
                const auto& arr = *arr_ptr();
                for(size_t i = 0; i < arr.size(); ++i) {
                    if(i > 0) s += ",";
                    s += arr[i].dump();
                }
                return s + "]";
            }
            case value_t::object: {
                std::string s = "{";
                const auto& obj = *obj_ptr();
                for(size_t i = 0; i < obj.size(); ++i) {
                    if(i > 0) s += ",";
                    s += "\"" + escape_json(obj[i].first) + "\":" + obj[i].second.dump();
                }
                return s + "}";
            }
            default: return "null";
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

    // ======================== Discarded (parse failure) ========================

    bool is_discarded() const { return type_ == value_t::discarded; }

    static iotmp_value discarded() {
        iotmp_value v;
        v.type_ = value_t::discarded;
        return v;
    }

    // ======================== JSON parsing ========================

    // Parse a JSON string into an iotmp_value.
    // Compatible with nlohmann::json signature: parse(text, nullptr, allow_exceptions)
    // When allow_exceptions is false, returns a discarded value on error.
    static iotmp_value parse(const char* text, void* = nullptr, bool = true) {
        if(!text) return discarded();
        const char* p = text;
        iotmp_value result;
        if(parse_value(p, result)) {
            skip_ws(p);
            return result;
        }
        return discarded();
    }

    static iotmp_value parse(const std::string& text, void* cb = nullptr, bool allow_exceptions = true) {
        return parse(text.c_str(), cb, allow_exceptions);
    }

private:
    static void skip_ws(const char*& p) {
        while(*p == ' ' || *p == '\t' || *p == '\n' || *p == '\r') ++p;
    }

    static bool parse_value(const char*& p, iotmp_value& out) {
        skip_ws(p);
        if(*p == '\0') return false;

        switch(*p) {
            case '"': return parse_string(p, out);
            case '{': return parse_object(p, out);
            case '[': return parse_array(p, out);
            case 't': return parse_literal(p, "true", 4, out, true);
            case 'f': return parse_literal(p, "false", 5, out, false);
            case 'n': return parse_null(p, out);
            default:
                if(*p == '-' || (*p >= '0' && *p <= '9')) return parse_number(p, out);
                return false;
        }
    }

    static bool parse_string(const char*& p, iotmp_value& out) {
        std::string s;
        if(!parse_string_raw(p, s)) return false;
        out = std::move(s);
        return true;
    }

    static bool parse_string_raw(const char*& p, std::string& out) {
        if(*p != '"') return false;
        ++p;
        out.clear();
        while(*p && *p != '"') {
            if(*p == '\\') {
                ++p;
                switch(*p) {
                    case '"':  out += '"'; break;
                    case '\\': out += '\\'; break;
                    case '/':  out += '/'; break;
                    case 'n':  out += '\n'; break;
                    case 'r':  out += '\r'; break;
                    case 't':  out += '\t'; break;
                    case 'b':  out += '\b'; break;
                    case 'f':  out += '\f'; break;
                    case 'u': {
                        // Basic \uXXXX support (BMP only)
                        ++p;
                        unsigned cp = 0;
                        for(int i = 0; i < 4; ++i) {
                            cp <<= 4;
                            char c = *p;
                            if(c >= '0' && c <= '9') cp |= (c - '0');
                            else if(c >= 'a' && c <= 'f') cp |= (c - 'a' + 10);
                            else if(c >= 'A' && c <= 'F') cp |= (c - 'A' + 10);
                            else return false;
                            ++p;
                        }
                        // UTF-8 encode
                        if(cp < 0x80) {
                            out += static_cast<char>(cp);
                        } else if(cp < 0x800) {
                            out += static_cast<char>(0xC0 | (cp >> 6));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        } else {
                            out += static_cast<char>(0xE0 | (cp >> 12));
                            out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
                            out += static_cast<char>(0x80 | (cp & 0x3F));
                        }
                        continue; // already advanced past \uXXXX
                    }
                    default: return false;
                }
            } else {
                out += *p;
            }
            ++p;
        }
        if(*p != '"') return false;
        ++p;
        return true;
    }

    static bool parse_number(const char*& p, iotmp_value& out) {
        const char* start = p;
        bool negative = false;
        bool is_float = false;

        if(*p == '-') { negative = true; ++p; }
        if(!(*p >= '0' && *p <= '9')) return false;
        while(*p >= '0' && *p <= '9') ++p;
        if(*p == '.') { is_float = true; ++p; while(*p >= '0' && *p <= '9') ++p; }
        if(*p == 'e' || *p == 'E') {
            is_float = true; ++p;
            if(*p == '+' || *p == '-') ++p;
            while(*p >= '0' && *p <= '9') ++p;
        }

        std::string num_str(start, p);
        if(is_float) {
            out = std::stod(num_str);
        } else if(negative) {
            out = static_cast<int64_t>(std::stoll(num_str));
        } else {
            out = static_cast<uint64_t>(std::stoull(num_str));
        }
        return true;
    }

    static bool parse_object(const char*& p, iotmp_value& out) {
        if(*p != '{') return false;
        ++p;
        out = iotmp_value::object();
        skip_ws(p);
        if(*p == '}') { ++p; return true; }

        while(true) {
            skip_ws(p);
            std::string key;
            if(!parse_string_raw(p, key)) return false;
            skip_ws(p);
            if(*p != ':') return false;
            ++p;
            iotmp_value val;
            if(!parse_value(p, val)) return false;
            out[key] = std::move(val);
            skip_ws(p);
            if(*p == '}') { ++p; return true; }
            if(*p != ',') return false;
            ++p;
        }
    }

    static bool parse_array(const char*& p, iotmp_value& out) {
        if(*p != '[') return false;
        ++p;
        out = iotmp_value::array();
        skip_ws(p);
        if(*p == ']') { ++p; return true; }

        while(true) {
            iotmp_value val;
            if(!parse_value(p, val)) return false;
            out.emplace_back(std::move(val));
            skip_ws(p);
            if(*p == ']') { ++p; return true; }
            if(*p != ',') return false;
            ++p;
        }
    }

    static bool parse_literal(const char*& p, const char* lit, size_t len, iotmp_value& out, bool bool_val) {
        if(std::strncmp(p, lit, len) == 0) {
            p += len;
            out = bool_val;
            return true;
        }
        return false;
    }

    static bool parse_null(const char*& p, iotmp_value& out) {
        if(std::strncmp(p, "null", 4) == 0) {
            p += 4;
            out = nullptr;
            return true;
        }
        return false;
    }
};

} // namespace thinger::iotmp

#endif // THINGER_IOTMP_VALUE_HPP
