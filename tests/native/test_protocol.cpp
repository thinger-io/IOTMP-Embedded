// Comprehensive offline unit tests for the IOTMP protocol library.
//
// Compile: c++ -std=c++17 -I../../include -o test_protocol test_protocol.cpp
// Run:     ./test_protocol

#include <thinger/iotmp/core/iotmp_value.hpp>
#include <thinger/iotmp/core/iotmp_message.hpp>
#include <thinger/iotmp/core/iotmp_encoder.hpp>
#include <thinger/iotmp/core/iotmp_decoder.hpp>
#include <thinger/iotmp/core/iotmp_resource.hpp>

#include <cstdio>
#include <cstdlib>
#include <cassert>
#include <cstring>
#include <cmath>
#include <vector>
#include <string>
#include <limits>

using namespace thinger::iotmp;

// ============================================================================
// Minimal test framework
// ============================================================================

static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name) printf("  %-50s ", name);
#define PASS() do { printf("\033[32mPASS\033[0m\n"); tests_passed++; } while(0)
#define FAIL(msg) do { printf("\033[31mFAIL: %s\033[0m\n", msg); tests_failed++; } while(0)
#define CHECK(cond, msg) do { if(!(cond)) { FAIL(msg); return; } } while(0)

// ============================================================================
// Helper: PSON roundtrip encode then decode
// ============================================================================

static iotmp_value pson_roundtrip(const iotmp_value& original) {
    std::string buffer;
    buffer.reserve(256);
    string_writer writer(buffer);
    pson_encoder<string_writer> encoder(writer);
    encoder.encode(original);

    memory_reader reader(buffer.data(), buffer.size());
    pson_decoder<memory_reader> decoder(reader);
    iotmp_value decoded;
    decoder.decode(decoded);
    return decoded;
}

// ============================================================================
// 1. iotmp_value: basic types
// ============================================================================

void test_value_null() {
    TEST("value: null");
    iotmp_value v;
    CHECK(v.is_null(), "default is null");
    CHECK(!v.is_boolean(), "not boolean");
    CHECK(!v.is_number(), "not number");
    CHECK(!v.is_string(), "not string");
    CHECK(!v.is_array(), "not array");
    CHECK(!v.is_object(), "not object");
    CHECK(!v.is_binary(), "not binary");
    CHECK(v.size() == 0, "size is 0");
    CHECK(v.empty(), "is empty");
    PASS();
}

void test_value_nullptr() {
    TEST("value: nullptr_t constructor");
    iotmp_value v(nullptr);
    CHECK(v.is_null(), "nullptr is null");
    PASS();
}

void test_value_bool_true() {
    TEST("value: bool true");
    iotmp_value v(true);
    CHECK(v.is_boolean(), "is boolean");
    CHECK(v.get<bool>() == true, "value is true");
    PASS();
}

void test_value_bool_false() {
    TEST("value: bool false");
    iotmp_value v(false);
    CHECK(v.is_boolean(), "is boolean");
    CHECK(v.get<bool>() == false, "value is false");
    PASS();
}

void test_value_uint() {
    TEST("value: unsigned integer");
    iotmp_value v(42u);
    CHECK(v.is_number_unsigned(), "is unsigned");
    CHECK(v.is_number(), "is number");
    CHECK(v.get<uint32_t>() == 42, "value is 42");
    CHECK(v.get<uint64_t>() == 42, "u64 is 42");
    PASS();
}

void test_value_int_positive() {
    TEST("value: positive signed integer");
    iotmp_value v(100);
    CHECK(v.is_number_integer(), "is integer");
    CHECK(v.is_number(), "is number");
    CHECK(v.get<int32_t>() == 100, "value is 100");
    PASS();
}

void test_value_int_negative() {
    TEST("value: negative integer");
    iotmp_value v(-42);
    CHECK(v.is_number_integer(), "is integer");
    CHECK(v.get<int32_t>() == -42, "value is -42");
    CHECK(v.get<int64_t>() == -42, "i64 is -42");
    PASS();
}

void test_value_int_zero() {
    TEST("value: zero integer");
    iotmp_value v(0);
    CHECK(v.is_number_integer(), "is integer");
    CHECK(v.get<int32_t>() == 0, "value is 0");
    PASS();
}

void test_value_float() {
    TEST("value: float");
    iotmp_value v(3.14f);
    CHECK(v.is_number_float(), "is float");
    CHECK(v.is_number(), "is number");
    CHECK(std::fabs(v.get<float>() - 3.14f) < 1e-6f, "value ~3.14");
    PASS();
}

void test_value_double() {
    TEST("value: double");
    iotmp_value v(2.718281828459045);
    CHECK(v.is_number_float(), "is float type");
    CHECK(std::fabs(v.get<double>() - 2.718281828459045) < 1e-12, "value ~e");
    PASS();
}

void test_value_string_cstr() {
    TEST("value: string from const char*");
    iotmp_value v("hello world");
    CHECK(v.is_string(), "is string");
    CHECK(v.get<std::string>() == "hello world", "value matches");
    CHECK(v.get_string() == "hello world", "get_string matches");
    CHECK(v.size() == 11, "size is 11");
    PASS();
}

void test_value_string_std() {
    TEST("value: string from std::string");
    std::string s = "test string";
    iotmp_value v(s);
    CHECK(v.is_string(), "is string");
    CHECK(v.get<std::string>() == "test string", "value matches");
    PASS();
}

void test_value_string_move() {
    TEST("value: string move constructor");
    std::string s = "moveable";
    iotmp_value v(std::move(s));
    CHECK(v.is_string(), "is string");
    CHECK(v.get<std::string>() == "moveable", "value matches");
    PASS();
}

void test_value_string_empty() {
    TEST("value: empty string");
    iotmp_value v("");
    CHECK(v.is_string(), "is string");
    CHECK(v.get<std::string>().empty(), "string is empty");
    CHECK(v.size() == 0, "size is 0");
    PASS();
}

void test_value_binary() {
    TEST("value: binary data");
    std::vector<uint8_t> data = {0x00, 0x01, 0x02, 0xFF, 0xFE};
    auto v = iotmp_value::binary(data);
    CHECK(v.is_binary(), "is binary");
    CHECK(v.size() == 5, "size is 5");
    auto& bin = v.get_binary();
    CHECK(bin[0] == 0x00, "byte 0");
    CHECK(bin[4] == 0xFE, "byte 4");
    PASS();
}

void test_value_binary_from_ptr() {
    TEST("value: binary from pointer");
    uint8_t data[] = {0xDE, 0xAD, 0xBE, 0xEF};
    auto v = iotmp_value::binary(data, 4);
    CHECK(v.is_binary(), "is binary");
    CHECK(v.size() == 4, "size is 4");
    CHECK(v.get_binary()[0] == 0xDE, "first byte");
    CHECK(v.get_binary()[3] == 0xEF, "last byte");
    PASS();
}

// ============================================================================
// 2. iotmp_value: object operations
// ============================================================================

void test_value_object_create() {
    TEST("value: object creation and access");
    iotmp_value obj;
    obj["name"] = "sensor";
    obj["temp"] = 23.5;
    obj["active"] = true;
    obj["count"] = 42u;

    CHECK(obj.is_object(), "is object");
    CHECK(obj.size() == 4, "size is 4");
    CHECK(obj["name"].get<std::string>() == "sensor", "name field");
    CHECK(obj["active"].get<bool>() == true, "active field");
    CHECK(obj["count"].get<uint32_t>() == 42, "count field");
    PASS();
}

void test_value_object_contains() {
    TEST("value: object contains");
    iotmp_value obj;
    obj["key1"] = 1;
    obj["key2"] = 2;

    CHECK(obj.contains("key1"), "contains key1");
    CHECK(obj.contains("key2"), "contains key2");
    CHECK(!obj.contains("key3"), "not contains key3");
    PASS();
}

void test_value_object_iteration() {
    TEST("value: object iteration");
    iotmp_value obj;
    obj["a"] = 1;
    obj["b"] = 2;
    obj["c"] = 3;

    const auto& items = obj.items();
    CHECK(items.size() == 3, "3 items");
    CHECK(items[0].first == "a", "first key is a");
    CHECK(items[1].first == "b", "second key is b");
    CHECK(items[2].first == "c", "third key is c");
    PASS();
}

void test_value_object_const_access() {
    TEST("value: const object access");
    iotmp_value obj;
    obj["key"] = "value";

    const iotmp_value& cref = obj;
    CHECK(cref["key"].get<std::string>() == "value", "const access works");
    CHECK(cref["missing"].is_null(), "missing key returns null");
    PASS();
}

void test_value_object_nested() {
    TEST("value: nested objects");
    iotmp_value obj;
    obj["level1"]["level2"]["level3"] = "deep";
    CHECK(obj["level1"]["level2"]["level3"].get<std::string>() == "deep", "deep access");
    CHECK(obj["level1"].is_object(), "level1 is object");
    CHECK(obj["level1"]["level2"].is_object(), "level2 is object");
    PASS();
}

void test_value_object_overwrite() {
    TEST("value: object overwrite value");
    iotmp_value obj;
    obj["key"] = "first";
    CHECK(obj["key"].get<std::string>() == "first", "initial value");
    obj["key"] = "second";
    CHECK(obj["key"].get<std::string>() == "second", "overwritten value");
    CHECK(obj.size() == 1, "still size 1");
    PASS();
}

void test_value_object_factory() {
    TEST("value: static object() factory");
    auto obj = iotmp_value::object();
    CHECK(obj.is_object(), "is object");
    CHECK(obj.empty(), "is empty");
    CHECK(obj.size() == 0, "size is 0");
    PASS();
}

// ============================================================================
// 3. iotmp_value: array operations
// ============================================================================

void test_value_array_factory_empty() {
    TEST("value: static array() factory empty");
    auto arr = iotmp_value::array();
    CHECK(arr.is_array(), "is array");
    CHECK(arr.empty(), "is empty");
    CHECK(arr.size() == 0, "size is 0");
    PASS();
}

void test_value_array_initializer() {
    TEST("value: array with initializer list");
    auto arr = iotmp_value::array({"one", "two", "three"});
    CHECK(arr.is_array(), "is array");
    CHECK(arr.size() == 3, "size is 3");
    CHECK(arr[(size_t)0].get<std::string>() == "one", "element 0");
    CHECK(arr[(size_t)1].get<std::string>() == "two", "element 1");
    CHECK(arr[(size_t)2].get<std::string>() == "three", "element 2");
    PASS();
}

void test_value_array_push_back() {
    TEST("value: array push_back/emplace_back");
    iotmp_value arr = iotmp_value::array();
    arr.push_back(iotmp_value(10));
    arr.push_back(iotmp_value(20));
    arr.emplace_back(iotmp_value(30));

    CHECK(arr.size() == 3, "size is 3");
    CHECK(arr[(size_t)0].get<int32_t>() == 10, "element 0");
    CHECK(arr[(size_t)1].get<int32_t>() == 20, "element 1");
    CHECK(arr[(size_t)2].get<int32_t>() == 30, "element 2");
    PASS();
}

void test_value_array_index_access() {
    TEST("value: array index auto-resize");
    iotmp_value v;
    v[(size_t)2] = "at index 2";
    CHECK(v.is_array(), "auto-promoted to array");
    CHECK(v.size() == 3, "size is 3 (0,1,2)");
    CHECK(v[(size_t)0].is_null(), "index 0 is null");
    CHECK(v[(size_t)1].is_null(), "index 1 is null");
    CHECK(v[(size_t)2].get<std::string>() == "at index 2", "index 2 ok");
    PASS();
}

void test_value_array_const_access() {
    TEST("value: const array access");
    auto arr = iotmp_value::array({"a", "b"});
    const iotmp_value& cref = arr;
    CHECK(cref[(size_t)0].get<std::string>() == "a", "const index 0");
    CHECK(cref[(size_t)1].get<std::string>() == "b", "const index 1");
    CHECK(cref[(size_t)99].is_null(), "out of bounds returns null");
    PASS();
}

// ============================================================================
// 4. iotmp_value: copy, move, swap
// ============================================================================

void test_value_copy() {
    TEST("value: copy constructor");
    iotmp_value original;
    original["key"] = "value";
    original["num"] = 42;

    iotmp_value copy(original);
    CHECK(copy.is_object(), "copy is object");
    CHECK(copy["key"].get<std::string>() == "value", "copy has key");
    CHECK(copy["num"].get<int32_t>() == 42, "copy has num");

    // Modify copy, original unchanged
    copy["key"] = "changed";
    CHECK(original["key"].get<std::string>() == "value", "original unchanged");
    PASS();
}

void test_value_copy_assign() {
    TEST("value: copy assignment");
    iotmp_value a;
    a["x"] = 1;

    iotmp_value b;
    b = a;
    CHECK(b["x"].get<int32_t>() == 1, "copy assigned");
    b["x"] = 2;
    CHECK(a["x"].get<int32_t>() == 1, "original unchanged");
    PASS();
}

void test_value_move() {
    TEST("value: move constructor");
    iotmp_value original;
    original["key"] = "value";

    iotmp_value moved(std::move(original));
    CHECK(moved["key"].get<std::string>() == "value", "moved has value");
    CHECK(original.is_null(), "source is null after move");
    PASS();
}

void test_value_move_assign() {
    TEST("value: move assignment");
    iotmp_value a;
    a["x"] = 10;

    iotmp_value b;
    b = std::move(a);
    CHECK(b["x"].get<int32_t>() == 10, "move assigned");
    CHECK(a.is_null(), "source is null");
    PASS();
}

void test_value_swap() {
    TEST("value: swap");
    iotmp_value a("hello");
    iotmp_value b(42);

    a.swap(b);
    CHECK(a.is_number_integer(), "a is now integer");
    CHECK(a.get<int32_t>() == 42, "a is 42");
    CHECK(b.is_string(), "b is now string");
    CHECK(b.get<std::string>() == "hello", "b is hello");
    PASS();
}

void test_value_assign_null() {
    TEST("value: assign nullptr resets");
    iotmp_value v("something");
    CHECK(v.is_string(), "initially string");
    v = nullptr;
    CHECK(v.is_null(), "now null");
    PASS();
}

// ============================================================================
// 5. iotmp_value: auto-promotion
// ============================================================================

void test_value_auto_promote_to_object() {
    TEST("value: null auto-promotes to object on []");
    iotmp_value v;
    CHECK(v.is_null(), "starts null");
    v["key"] = 1;
    CHECK(v.is_object(), "now object");
    PASS();
}

void test_value_auto_promote_to_array() {
    TEST("value: null auto-promotes to array on [size_t]");
    iotmp_value v;
    CHECK(v.is_null(), "starts null");
    v[(size_t)0] = "first";
    CHECK(v.is_array(), "now array");
    PASS();
}

// ============================================================================
// 6. iotmp_value: type cross-conversion
// ============================================================================

void test_value_cross_conversion() {
    TEST("value: numeric type cross-conversion");
    iotmp_value u(100u);
    CHECK(u.get<int32_t>() == 100, "uint -> int");
    CHECK(u.get<double>() == 100.0, "uint -> double");

    iotmp_value i(-50);
    CHECK(i.get<uint32_t>() != 0 || i.get<int32_t>() == -50, "int -> uint wraps");
    CHECK(i.get<double>() == -50.0, "int -> double");

    iotmp_value d(99.0);
    CHECK(d.get<int32_t>() == 99, "double -> int");
    CHECK(d.get<uint32_t>() == 99, "double -> uint");
    PASS();
}

// ============================================================================
// 7. iotmp_value: assignment operators
// ============================================================================

void test_value_assignment_operators() {
    TEST("value: assignment operators for all types");
    iotmp_value v;

    v = true;
    CHECK(v.is_boolean() && v.get<bool>() == true, "assign bool");

    v = 42u;
    CHECK(v.is_number_unsigned() && v.get<uint32_t>() == 42, "assign uint");

    v = -7;
    CHECK(v.is_number_integer() && v.get<int32_t>() == -7, "assign int");

    v = 3.14f;
    CHECK(v.is_number_float(), "assign float");

    v = 2.71828;
    CHECK(v.is_number_float(), "assign double");

    v = "hello";
    CHECK(v.is_string() && v.get<std::string>() == "hello", "assign cstr");

    std::string s = "world";
    v = s;
    CHECK(v.is_string() && v.get<std::string>() == "world", "assign string ref");

    v = std::string("moved");
    CHECK(v.is_string() && v.get<std::string>() == "moved", "assign string rval");
    PASS();
}

// ============================================================================
// 8. PSON v2: roundtrip for all types
// ============================================================================

void test_pson_null() {
    TEST("pson: null roundtrip");
    iotmp_value original;
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_null(), "decoded is null");
    PASS();
}

void test_pson_bool_true() {
    TEST("pson: bool true roundtrip");
    iotmp_value original(true);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_boolean(), "decoded is boolean");
    CHECK(decoded.get<bool>() == true, "decoded is true");
    PASS();
}

void test_pson_bool_false() {
    TEST("pson: bool false roundtrip");
    iotmp_value original(false);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_boolean(), "decoded is boolean");
    CHECK(decoded.get<bool>() == false, "decoded is false");
    PASS();
}

void test_pson_uint_small() {
    TEST("pson: small unsigned roundtrip");
    iotmp_value original(7u);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.get<uint64_t>() == 7, "value is 7");
    PASS();
}

void test_pson_uint_large() {
    TEST("pson: large unsigned roundtrip");
    uint64_t big = 1000000ULL;
    iotmp_value original(big);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.get<uint64_t>() == 1000000, "value is 1000000");
    PASS();
}

void test_pson_uint_zero() {
    TEST("pson: zero unsigned roundtrip");
    iotmp_value original(0u);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.get<uint64_t>() == 0, "value is 0");
    PASS();
}

void test_pson_int_negative() {
    TEST("pson: negative integer roundtrip");
    iotmp_value original(-42);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.get<int64_t>() == -42, "value is -42");
    PASS();
}

void test_pson_int_large_negative() {
    TEST("pson: large negative integer roundtrip");
    iotmp_value original(-100000);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.get<int64_t>() == -100000, "value is -100000");
    PASS();
}

void test_pson_int_positive_as_signed() {
    TEST("pson: positive signed int roundtrip");
    iotmp_value original(55);
    auto decoded = pson_roundtrip(original);
    // Positive signed ints encode as unsigned in PSON
    CHECK(decoded.get<int64_t>() == 55, "value is 55");
    PASS();
}

void test_pson_float() {
    TEST("pson: float roundtrip");
    // Use a value that needs float precision (not an exact integer,
    // not exactly representable as float==double mismatch)
    iotmp_value original(23.5f);
    auto decoded = pson_roundtrip(original);
    // 23.5 has exact float rep, but encoder may optimize to int or float
    // Since float(23.5) == double(23.5) but 23.5 is not an integer, it encodes as float
    CHECK(std::fabs(decoded.get<double>() - 23.5) < 1e-6, "value ~23.5");
    PASS();
}

void test_pson_double() {
    TEST("pson: double roundtrip (high precision)");
    // Use a value that requires double precision
    double val = 3.141592653589793;
    iotmp_value original(val);
    auto decoded = pson_roundtrip(original);
    CHECK(std::fabs(decoded.get<double>() - val) < 1e-15, "value matches");
    PASS();
}

void test_pson_float_exact_integer() {
    TEST("pson: float that is exact integer");
    // The encoder optimizes exact integer doubles to varint
    iotmp_value original(100.0);
    auto decoded = pson_roundtrip(original);
    // May decode as unsigned since 100.0 -> unsigned 100 optimization
    CHECK(decoded.get<uint64_t>() == 100, "value is 100");
    PASS();
}

void test_pson_string() {
    TEST("pson: string roundtrip");
    iotmp_value original("hello world");
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_string(), "decoded is string");
    CHECK(decoded.get<std::string>() == "hello world", "value matches");
    PASS();
}

void test_pson_string_empty() {
    TEST("pson: empty string roundtrip");
    iotmp_value original("");
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_string(), "decoded is string");
    CHECK(decoded.get<std::string>().empty(), "decoded is empty");
    PASS();
}

void test_pson_string_long() {
    TEST("pson: long string roundtrip");
    std::string long_str(500, 'x');
    iotmp_value original(long_str);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_string(), "decoded is string");
    CHECK(decoded.get<std::string>() == long_str, "value matches");
    CHECK(decoded.size() == 500, "size is 500");
    PASS();
}

void test_pson_binary() {
    TEST("pson: binary roundtrip");
    std::vector<uint8_t> data = {0x00, 0x01, 0xFF, 0xAB, 0xCD};
    auto original = iotmp_value::binary(data);
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_binary(), "decoded is binary");
    CHECK(decoded.size() == 5, "size is 5");
    auto& bin = decoded.get_binary();
    CHECK(bin[0] == 0x00, "byte 0");
    CHECK(bin[2] == 0xFF, "byte 2");
    CHECK(bin[4] == 0xCD, "byte 4");
    PASS();
}

void test_pson_binary_empty() {
    TEST("pson: empty binary roundtrip");
    auto original = iotmp_value::binary(std::vector<uint8_t>{});
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_binary(), "decoded is binary");
    CHECK(decoded.size() == 0, "size is 0");
    PASS();
}

// ============================================================================
// 9. PSON v2: nested structures
// ============================================================================

void test_pson_simple_object() {
    TEST("pson: simple object roundtrip");
    iotmp_value original;
    original["name"] = "test";
    original["value"] = 42u;
    original["flag"] = true;

    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_object(), "decoded is object");
    CHECK(decoded.size() == 3, "size is 3");
    CHECK(decoded["name"].get<std::string>() == "test", "name");
    CHECK(decoded["value"].get<uint64_t>() == 42, "value");
    CHECK(decoded["flag"].get<bool>() == true, "flag");
    PASS();
}

void test_pson_simple_array() {
    TEST("pson: simple array roundtrip");
    auto original = iotmp_value::array({"alpha", "beta", "gamma"});
    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_array(), "decoded is array");
    CHECK(decoded.size() == 3, "size is 3");
    CHECK(decoded[(size_t)0].get<std::string>() == "alpha", "elem 0");
    CHECK(decoded[(size_t)1].get<std::string>() == "beta", "elem 1");
    CHECK(decoded[(size_t)2].get<std::string>() == "gamma", "elem 2");
    PASS();
}

void test_pson_nested_object() {
    TEST("pson: nested object roundtrip");
    iotmp_value original;
    original["data"]["temp"] = 22.5;
    original["data"]["hum"] = 55u;
    original["meta"]["version"] = 1u;

    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_object(), "decoded is object");
    CHECK(decoded["data"].is_object(), "data is object");
    CHECK(std::fabs(decoded["data"]["temp"].get<double>() - 22.5) < 1e-6, "temp");
    CHECK(decoded["data"]["hum"].get<uint64_t>() == 55, "hum");
    CHECK(decoded["meta"]["version"].get<uint64_t>() == 1, "version");
    PASS();
}

void test_pson_object_with_array() {
    TEST("pson: object containing array roundtrip");
    iotmp_value original;
    original["tags"] = iotmp_value::array({"indoor", "floor2"});
    original["id"] = 99u;

    auto decoded = pson_roundtrip(original);
    CHECK(decoded["tags"].is_array(), "tags is array");
    CHECK(decoded["tags"].size() == 2, "tags size 2");
    CHECK(decoded["tags"][(size_t)0].get<std::string>() == "indoor", "tag 0");
    CHECK(decoded["tags"][(size_t)1].get<std::string>() == "floor2", "tag 1");
    CHECK(decoded["id"].get<uint64_t>() == 99, "id");
    PASS();
}

void test_pson_array_with_objects() {
    TEST("pson: array containing objects roundtrip");
    auto original = iotmp_value::array();
    {
        iotmp_value item;
        item["name"] = "a";
        item["val"] = 1;
        original.emplace_back(std::move(item));
    }
    {
        iotmp_value item;
        item["name"] = "b";
        item["val"] = 2;
        original.emplace_back(std::move(item));
    }

    auto decoded = pson_roundtrip(original);
    CHECK(decoded.is_array(), "decoded is array");
    CHECK(decoded.size() == 2, "size is 2");
    CHECK(decoded[(size_t)0]["name"].get<std::string>() == "a", "item 0 name");
    CHECK(decoded[(size_t)1]["name"].get<std::string>() == "b", "item 1 name");
    PASS();
}

void test_pson_deeply_nested() {
    TEST("pson: deeply nested structure roundtrip");
    iotmp_value original;
    original["a"]["b"]["c"]["d"] = "deep";

    auto decoded = pson_roundtrip(original);
    CHECK(decoded["a"]["b"]["c"]["d"].get<std::string>() == "deep", "deep value");
    PASS();
}

void test_pson_mixed_array() {
    TEST("pson: array with mixed types");
    auto arr = iotmp_value::array();
    arr.push_back(iotmp_value(true));
    arr.push_back(iotmp_value(42u));
    arr.push_back(iotmp_value("text"));
    arr.push_back(iotmp_value(-5));

    auto decoded = pson_roundtrip(arr);
    CHECK(decoded.size() == 4, "size is 4");
    CHECK(decoded[(size_t)0].get<bool>() == true, "bool elem");
    CHECK(decoded[(size_t)1].get<uint64_t>() == 42, "uint elem");
    CHECK(decoded[(size_t)2].get<std::string>() == "text", "str elem");
    CHECK(decoded[(size_t)3].get<int64_t>() == -5, "int elem");
    PASS();
}

// ============================================================================
// 10. PSON v2: edge cases
// ============================================================================

void test_pson_edge_zero_values() {
    TEST("pson: zero values roundtrip");
    iotmp_value original;
    original["zero_uint"] = 0u;
    original["zero_int"] = 0;
    original["zero_float"] = 0.0;

    auto decoded = pson_roundtrip(original);
    CHECK(decoded["zero_uint"].get<uint64_t>() == 0, "zero uint");
    CHECK(decoded["zero_int"].get<int64_t>() == 0, "zero int");
    // 0.0 is exact integer, encoder optimizes to unsigned 0
    CHECK(decoded["zero_float"].get<double>() == 0.0, "zero float");
    PASS();
}

void test_pson_edge_large_numbers() {
    TEST("pson: large numbers roundtrip");
    iotmp_value original;
    original["big_uint"] = static_cast<uint64_t>(1000000);
    original["big_neg"] = static_cast<int64_t>(-999999);

    auto decoded = pson_roundtrip(original);
    CHECK(decoded["big_uint"].get<uint64_t>() == 1000000, "big uint");
    CHECK(decoded["big_neg"].get<int64_t>() == -999999, "big neg");
    PASS();
}

void test_pson_encode_decode_size() {
    TEST("pson: encoded size is non-zero");
    iotmp_value v;
    v["x"] = 1;

    std::string buffer;
    string_writer writer(buffer);
    pson_encoder<string_writer> encoder(writer);
    bool ok = encoder.encode(v);
    CHECK(ok, "encode succeeded");
    CHECK(buffer.size() > 0, "non-empty output");
    PASS();
}

void test_pson_null_writer_sizing() {
    TEST("pson: null_writer computes correct size");
    iotmp_value v;
    v["key"] = "value";
    v["num"] = 12345u;

    // Size with null_writer
    null_writer nw;
    pson_encoder<null_writer> sizer(nw);
    sizer.encode(v);
    size_t computed_size = sizer.bytes_written();

    // Actual size with string_writer
    std::string buffer;
    string_writer sw(buffer);
    pson_encoder<string_writer> encoder(sw);
    encoder.encode(v);

    CHECK(computed_size == buffer.size(), "sizes match");
    PASS();
}

// ============================================================================
// 11. IOTMP message: encode/decode roundtrip
// ============================================================================

void test_message_run_roundtrip() {
    TEST("message: RUN encode/decode roundtrip");
    iotmp_message msg(message::type::RUN);
    msg.set_stream_id(42);
    msg[message::field::RESOURCE] = std::string("temperature");
    msg[message::field::PAYLOAD]["celsius"] = 23.5;

    auto encoded = encode_message(msg);
    CHECK(encoded.size() > 0, "encoded not empty");

    memory_reader reader(encoded.data(), encoded.size());
    pson_decoder<memory_reader> varint_dec(reader);

    uint8_t type_byte;
    CHECK(reader.read(&type_byte), "read type");
    CHECK(type_byte == message::type::RUN, "type is RUN");

    uint32_t body_size = 0;
    CHECK(varint_dec.pb_decode_varint32(body_size), "read body size");
    CHECK(body_size > 0, "body size > 0");

    iotmp_decoder<memory_reader> msg_decoder(reader);
    iotmp_message decoded(message::type::RUN);
    CHECK(msg_decoder.decode(decoded, body_size), "decode body");

    CHECK(decoded.get_stream_id() == 42, "stream_id");
    CHECK(decoded[message::field::RESOURCE].get<std::string>() == "temperature", "resource");
    CHECK(std::fabs(decoded[message::field::PAYLOAD]["celsius"].get<double>() - 23.5) < 1e-6, "payload");
    PASS();
}

void test_message_connect() {
    TEST("message: CONNECT roundtrip");
    iotmp_message msg(message::type::CONNECT);
    msg.set_stream_id(1);
    msg[message::field::PAYLOAD] = iotmp_value::array({
        iotmp_value("user"),
        iotmp_value("device"),
        iotmp_value("cred")
    });

    auto encoded = encode_message(msg);
    CHECK(encoded.size() > 0, "encoded not empty");

    memory_reader reader(encoded.data(), encoded.size());
    uint8_t type_byte;
    reader.read(&type_byte);
    CHECK(type_byte == message::type::CONNECT, "type is CONNECT");
    PASS();
}

void test_message_keep_alive() {
    TEST("message: KEEP_ALIVE (no body)");
    auto encoded = encode_message(message::type::KEEP_ALIVE);
    CHECK(encoded.size() == 2, "size is 2 (type + size=0)");

    memory_reader reader(encoded.data(), encoded.size());
    uint8_t type_byte;
    reader.read(&type_byte);
    CHECK(type_byte == message::type::KEEP_ALIVE, "type is KEEP_ALIVE");

    uint8_t size_byte;
    reader.read(&size_byte);
    CHECK(size_byte == 0, "body size is 0");
    PASS();
}

void test_message_types_enum() {
    TEST("message: all type enum values");
    CHECK(message::type::RESERVED == 0x00, "RESERVED");
    CHECK(message::type::OK == 0x01, "OK");
    CHECK(message::type::ERROR == 0x02, "ERROR");
    CHECK(message::type::CONNECT == 0x03, "CONNECT");
    CHECK(message::type::DISCONNECT == 0x04, "DISCONNECT");
    CHECK(message::type::KEEP_ALIVE == 0x05, "KEEP_ALIVE");
    CHECK(message::type::RUN == 0x06, "RUN");
    CHECK(message::type::DESCRIBE == 0x07, "DESCRIBE");
    CHECK(message::type::START_STREAM == 0x08, "START_STREAM");
    CHECK(message::type::STOP_STREAM == 0x09, "STOP_STREAM");
    CHECK(message::type::STREAM_DATA == 0x0a, "STREAM_DATA");
    PASS();
}

void test_message_field_enum() {
    TEST("message: field enum values");
    CHECK(message::field::STREAM_ID == 0x01, "STREAM_ID");
    CHECK(message::field::PARAMETERS == 0x02, "PARAMETERS");
    CHECK(message::field::PAYLOAD == 0x03, "PAYLOAD");
    CHECK(message::field::RESOURCE == 0x04, "RESOURCE");
    PASS();
}

void test_message_type_str() {
    TEST("message: type string representation");
    iotmp_message msg(message::type::RUN);
    CHECK(strcmp(msg.message_type_str(), "RUN") == 0, "RUN str");

    msg.set_message_type(message::type::OK);
    CHECK(strcmp(msg.message_type_str(), "OK") == 0, "OK str");

    msg.set_message_type(message::type::ERROR);
    CHECK(strcmp(msg.message_type_str(), "ERROR") == 0, "ERROR str");

    msg.set_message_type(message::type::CONNECT);
    CHECK(strcmp(msg.message_type_str(), "CONNECT") == 0, "CONNECT str");

    msg.set_message_type(message::type::DISCONNECT);
    CHECK(strcmp(msg.message_type_str(), "DISCONNECT") == 0, "DISCONNECT str");

    msg.set_message_type(message::type::KEEP_ALIVE);
    CHECK(strcmp(msg.message_type_str(), "KEEP_ALIVE") == 0, "KEEP_ALIVE str");

    msg.set_message_type(message::type::DESCRIBE);
    CHECK(strcmp(msg.message_type_str(), "DESCRIBE") == 0, "DESCRIBE str");

    msg.set_message_type(message::type::START_STREAM);
    CHECK(strcmp(msg.message_type_str(), "START_STREAM") == 0, "START_STREAM str");

    msg.set_message_type(message::type::STOP_STREAM);
    CHECK(strcmp(msg.message_type_str(), "STOP_STREAM") == 0, "STOP_STREAM str");

    msg.set_message_type(message::type::STREAM_DATA);
    CHECK(strcmp(msg.message_type_str(), "STREAM_DATA") == 0, "STREAM_DATA str");
    PASS();
}

void test_message_field_access() {
    TEST("message: field access and has_field");
    iotmp_message msg(message::type::RUN);
    CHECK(!msg.has_field(message::field::STREAM_ID), "no stream id yet");
    CHECK(!msg.has_field(message::field::PAYLOAD), "no payload yet");

    msg.set_stream_id(100);
    CHECK(msg.has_field(message::field::STREAM_ID), "has stream id");
    CHECK(msg.get_stream_id() == 100, "stream id is 100");

    msg[message::field::PAYLOAD]["key"] = "value";
    CHECK(msg.has_field(message::field::PAYLOAD), "has payload");
    CHECK(msg.has_payload(), "has_payload convenience");

    msg[message::field::PARAMETERS] = 5u;
    CHECK(msg.has_field(message::field::PARAMETERS), "has parameters");
    CHECK(msg.has_params(), "has_params convenience");
    PASS();
}

void test_message_remove_field() {
    TEST("message: remove_field");
    iotmp_message msg(message::type::RUN);
    msg.set_stream_id(1);
    msg[message::field::PAYLOAD] = "data";

    CHECK(msg.has_field(message::field::PAYLOAD), "has payload");
    bool removed = msg.remove_field(message::field::PAYLOAD);
    CHECK(removed, "remove returned true");
    CHECK(!msg.has_field(message::field::PAYLOAD), "payload removed");

    // Removing non-existent field
    CHECK(!msg.remove_field(message::field::RESOURCE), "remove non-existent");
    PASS();
}

void test_message_convenience_accessors() {
    TEST("message: params()/payload() convenience");
    iotmp_message msg(message::type::RUN);
    msg.payload()["temp"] = 25.0;
    msg.params() = 42u;

    CHECK(msg.payload()["temp"].get<double>() == 25.0, "payload via convenience");
    CHECK(msg.params().get<uint64_t>() == 42, "params via convenience");
    PASS();
}

void test_message_describe_roundtrip() {
    TEST("message: DESCRIBE roundtrip");
    iotmp_message msg(message::type::DESCRIBE);
    msg.set_stream_id(7);
    msg[message::field::RESOURCE] = std::string("sensor");

    auto encoded = encode_message(msg);
    memory_reader reader(encoded.data(), encoded.size());

    uint8_t type_byte;
    reader.read(&type_byte);
    CHECK(type_byte == message::type::DESCRIBE, "type is DESCRIBE");

    pson_decoder<memory_reader> varint_dec(reader);
    uint32_t body_size = 0;
    varint_dec.pb_decode_varint32(body_size);

    iotmp_decoder<memory_reader> msg_decoder(reader);
    iotmp_message decoded(message::type::DESCRIBE);
    CHECK(msg_decoder.decode(decoded, body_size), "decode body");
    CHECK(decoded.get_stream_id() == 7, "stream_id");
    CHECK(decoded[message::field::RESOURCE].get<std::string>() == "sensor", "resource");
    PASS();
}

void test_message_start_stream_roundtrip() {
    TEST("message: START_STREAM roundtrip");
    iotmp_message msg(message::type::START_STREAM);
    msg.set_stream_id(55);
    msg[message::field::RESOURCE] = std::string("data");
    msg[message::field::PARAMETERS]["interval"] = 1000u;

    auto encoded = encode_message(msg);
    memory_reader reader(encoded.data(), encoded.size());

    uint8_t type_byte;
    reader.read(&type_byte);
    CHECK(type_byte == message::type::START_STREAM, "type is START_STREAM");

    pson_decoder<memory_reader> varint_dec(reader);
    uint32_t body_size = 0;
    varint_dec.pb_decode_varint32(body_size);

    iotmp_decoder<memory_reader> msg_decoder(reader);
    iotmp_message decoded(message::type::START_STREAM);
    CHECK(msg_decoder.decode(decoded, body_size), "decode body");
    CHECK(decoded.get_stream_id() == 55, "stream_id");
    CHECK(decoded[message::field::RESOURCE].get<std::string>() == "data", "resource");
    PASS();
}

void test_message_for_each_field() {
    TEST("message: for_each_field iteration");
    iotmp_message msg(message::type::RUN);
    msg.set_stream_id(1);
    msg[message::field::RESOURCE] = std::string("res");
    msg[message::field::PAYLOAD] = 42u;

    int field_count = 0;
    msg.for_each_field([&field_count](uint8_t, const json_t&) {
        field_count++;
    });
    CHECK(field_count == 3, "3 fields iterated");
    PASS();
}

// ============================================================================
// 12. Resource: callback types
// ============================================================================

void test_resource_run_callback() {
    TEST("resource: run callback (no input/output)");
    iotmp_resource res;
    bool called = false;
    res = [&called]() {
        called = true;
    };

    CHECK(res.get_io_type() == iotmp_resource::run, "type is run");

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    bool ok = res.run_resource(req, resp);
    CHECK(ok, "run succeeded");
    CHECK(called, "callback was invoked");
    PASS();
}

void test_resource_output_callback() {
    TEST("resource: output callback");
    iotmp_resource res;
    res = [](output& out) {
        out["temp"] = 23.5;
        out["hum"] = 65u;
    };

    CHECK(res.get_io_type() == iotmp_resource::output_wrapper, "type is output");

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    bool ok = res.run_resource(req, resp);
    CHECK(ok, "run succeeded");
    CHECK(std::fabs(resp[message::field::PAYLOAD]["temp"].get<double>() - 23.5) < 1e-6, "temp");
    CHECK(resp[message::field::PAYLOAD]["hum"].get<uint64_t>() == 65, "hum");
    PASS();
}

void test_resource_input_callback() {
    TEST("resource: input callback");
    iotmp_resource res;
    bool received = false;
    int received_val = 0;
    res = [&](input& in) {
        received = true;
        received_val = in["value"].get<int32_t>();
    };

    CHECK(res.get_io_type() == iotmp_resource::input_wrapper, "type is input");

    iotmp_message req(message::type::RUN);
    req[message::field::PAYLOAD]["value"] = 42;
    iotmp_message resp(message::type::OK);
    res.run_resource(req, resp);
    CHECK(received, "callback invoked");
    CHECK(received_val == 42, "received correct value");
    PASS();
}

void test_resource_input_output_callback() {
    TEST("resource: input/output callback");
    iotmp_resource res;
    float threshold = 30.0f;
    res = [&threshold](input& in, output& out) {
            if(!in.is_empty()) {
                threshold = in["value"].get<float>();
            }
            out["value"] = threshold;
        };

    CHECK(res.get_io_type() == iotmp_resource::input_output_wrapper, "type is input_output");

    // Run with input
    iotmp_message req(message::type::RUN);
    req[message::field::PAYLOAD]["value"] = 25.0f;
    iotmp_message resp(message::type::OK);
    res.run_resource(req, resp);
    CHECK(threshold == 25.0f, "threshold updated");
    CHECK(std::fabs(resp[message::field::PAYLOAD]["value"].get<float>() - 25.0f) < 1e-6f, "output value");
    PASS();
}

void test_resource_none() {
    TEST("resource: default has no callback");
    iotmp_resource res;
    CHECK(res.get_io_type() == iotmp_resource::none, "type is none");

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    bool ok = res.run_resource(req, resp);
    CHECK(ok, "run on none succeeds (no-op)");
    PASS();
}

void test_resource_output_error() {
    TEST("resource: output callback with error");
    iotmp_resource res;
    res = [](output& out) {
        out.set_error("something failed");
    };

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    bool ok = res.run_resource(req, resp);
    CHECK(!ok, "run returned false (error)");
    CHECK(resp[message::field::PAYLOAD]["error"].get<std::string>() == "something failed", "error msg");
    PASS();
}

void test_resource_output_return_code() {
    TEST("resource: output callback with return code");
    iotmp_resource res;
    res = [](output& out) {
        out.set_return_code(404);
        out["status"] = "not found";
    };

    iotmp_message req(message::type::RUN);
    iotmp_message resp(message::type::OK);
    res.run_resource(req, resp);
    CHECK(resp.has_field(message::field::PARAMETERS), "has params (return code)");
    PASS();
}

// ============================================================================
// 13. Resource: describe functionality
// ============================================================================

void test_resource_describe_output() {
    TEST("resource: describe output resource");
    iotmp_resource res;
    res = [](output& out) {
        out["temp"] = 0.0;
        out["hum"] = 0u;
    };

    iotmp_message desc_msg(message::type::DESCRIBE);
    res.describe(desc_msg);
    CHECK(desc_msg.has_field(message::field::PAYLOAD), "has payload");
    CHECK(desc_msg[message::field::PAYLOAD].contains("out"), "has 'out' section");
    PASS();
}

void test_resource_describe_input() {
    TEST("resource: describe input resource");
    iotmp_resource res;
    res = [](input& in) {
        bool val = in["state"].get<bool>();
        (void)val;
    };

    iotmp_message desc_msg(message::type::DESCRIBE);
    res.describe(desc_msg);
    CHECK(desc_msg.has_field(message::field::PAYLOAD), "has payload");
    CHECK(desc_msg[message::field::PAYLOAD].contains("in"), "has 'in' section");
    PASS();
}

void test_resource_describe_input_output() {
    TEST("resource: describe input_output resource");
    iotmp_resource res;
    res = [](input& in, output& out) {
        float v = in["value"].get<float>();
        (void)v;
        out["value"] = 0.0f;
    };

    iotmp_message desc_msg(message::type::DESCRIBE);
    res.describe(desc_msg);
    CHECK(desc_msg.has_field(message::field::PAYLOAD), "has payload");
    CHECK(desc_msg[message::field::PAYLOAD].contains("in"), "has 'in' section");
    CHECK(desc_msg[message::field::PAYLOAD].contains("out"), "has 'out' section");
    PASS();
}

void test_resource_fill_api() {
    TEST("resource: fill_api");
    iotmp_resource res;
    res = [](output& out) {
        out["x"] = 0;
    };

    json_t content;
    res.fill_api(content);
    CHECK(content.contains("fn"), "has fn field");
    CHECK(content["fn"].get<int32_t>() == iotmp_resource::output_wrapper, "fn value");
    PASS();
}

void test_resource_stream_properties() {
    TEST("resource: stream id and echo properties");
    iotmp_resource res;
    CHECK(!res.stream_enabled(), "stream not enabled initially");
    CHECK(res.stream_echo(), "echo enabled by default");

    res.set_stream_id(42);
    CHECK(res.stream_enabled(), "stream enabled after set");
    CHECK(res.get_stream_id() == 42, "stream id is 42");

    res.set_stream_echo(false);
    CHECK(!res.stream_echo(), "echo disabled");
    PASS();
}

// ============================================================================
// 14. I/O adapters
// ============================================================================

void test_memory_reader() {
    TEST("adapter: memory_reader basic ops");
    uint8_t data[] = {0x01, 0x02, 0x03, 0x04};
    memory_reader reader(data, 4);

    uint8_t byte;
    CHECK(reader.read(&byte), "read 1st byte");
    CHECK(byte == 0x01, "1st byte value");
    CHECK(reader.bytes_read() == 1, "bytes_read is 1");

    uint8_t buf[2];
    CHECK(reader.read(buf, 2), "read 2 bytes");
    CHECK(buf[0] == 0x02 && buf[1] == 0x03, "2-byte values");
    CHECK(reader.bytes_read() == 3, "bytes_read is 3");

    CHECK(reader.read(&byte), "read last byte");
    CHECK(!reader.read(&byte), "read past end fails");
    PASS();
}

void test_string_writer() {
    TEST("adapter: string_writer basic ops");
    std::string buf;
    string_writer writer(buf);

    uint8_t data[] = {0xAA, 0xBB};
    CHECK(writer.write(data, 2), "write 2 bytes");
    CHECK(writer.bytes_written() == 2, "bytes_written is 2");
    CHECK(buf.size() == 2, "string size is 2");
    CHECK(static_cast<uint8_t>(buf[0]) == 0xAA, "byte 0");
    CHECK(static_cast<uint8_t>(buf[1]) == 0xBB, "byte 1");
    PASS();
}

void test_null_writer() {
    TEST("adapter: null_writer counts bytes");
    null_writer writer;
    uint8_t data[10] = {};
    CHECK(writer.write(data, 10), "write succeeds");
    CHECK(writer.bytes_written() == 10, "counted 10 bytes");
    CHECK(writer.write(data, 5), "write again");
    CHECK(writer.bytes_written() == 15, "counted 15 total");
    PASS();
}

void test_memory_writer() {
    TEST("adapter: memory_writer basic ops");
    uint8_t buf[4] = {};
    memory_writer writer(buf, 4);

    uint8_t data[] = {0x01, 0x02};
    CHECK(writer.write(data, 2), "write 2 bytes");
    CHECK(writer.bytes_written() == 2, "bytes_written is 2");
    CHECK(buf[0] == 0x01 && buf[1] == 0x02, "buffer contents");

    uint8_t more[] = {0x03, 0x04};
    CHECK(writer.write(more, 2), "write 2 more");
    CHECK(writer.bytes_written() == 4, "bytes_written is 4");

    uint8_t extra = 0x05;
    CHECK(!writer.write(&extra, 1), "write past capacity fails");

    writer.reset();
    CHECK(writer.bytes_written() == 0, "reset clears count");
    PASS();
}

// ============================================================================
// Main
// ============================================================================

int main() {
    printf("\n=== IOTMP Protocol Tests ===\n\n");

    // -- iotmp_value basic types --
    printf("--- iotmp_value: basic types ---\n");
    test_value_null();
    test_value_nullptr();
    test_value_bool_true();
    test_value_bool_false();
    test_value_uint();
    test_value_int_positive();
    test_value_int_negative();
    test_value_int_zero();
    test_value_float();
    test_value_double();
    test_value_string_cstr();
    test_value_string_std();
    test_value_string_move();
    test_value_string_empty();
    test_value_binary();
    test_value_binary_from_ptr();

    // -- iotmp_value object --
    printf("\n--- iotmp_value: object ---\n");
    test_value_object_create();
    test_value_object_contains();
    test_value_object_iteration();
    test_value_object_const_access();
    test_value_object_nested();
    test_value_object_overwrite();
    test_value_object_factory();

    // -- iotmp_value array --
    printf("\n--- iotmp_value: array ---\n");
    test_value_array_factory_empty();
    test_value_array_initializer();
    test_value_array_push_back();
    test_value_array_index_access();
    test_value_array_const_access();

    // -- iotmp_value copy/move/swap --
    printf("\n--- iotmp_value: copy/move/swap ---\n");
    test_value_copy();
    test_value_copy_assign();
    test_value_move();
    test_value_move_assign();
    test_value_swap();
    test_value_assign_null();

    // -- iotmp_value auto-promotion --
    printf("\n--- iotmp_value: auto-promotion ---\n");
    test_value_auto_promote_to_object();
    test_value_auto_promote_to_array();

    // -- iotmp_value cross-conversion --
    printf("\n--- iotmp_value: cross-conversion ---\n");
    test_value_cross_conversion();

    // -- iotmp_value assignment operators --
    printf("\n--- iotmp_value: assignment operators ---\n");
    test_value_assignment_operators();

    // -- PSON roundtrip --
    printf("\n--- PSON v2: roundtrip ---\n");
    test_pson_null();
    test_pson_bool_true();
    test_pson_bool_false();
    test_pson_uint_small();
    test_pson_uint_large();
    test_pson_uint_zero();
    test_pson_int_negative();
    test_pson_int_large_negative();
    test_pson_int_positive_as_signed();
    test_pson_float();
    test_pson_double();
    test_pson_float_exact_integer();
    test_pson_string();
    test_pson_string_empty();
    test_pson_string_long();
    test_pson_binary();
    test_pson_binary_empty();

    // -- PSON nested --
    printf("\n--- PSON v2: nested structures ---\n");
    test_pson_simple_object();
    test_pson_simple_array();
    test_pson_nested_object();
    test_pson_object_with_array();
    test_pson_array_with_objects();
    test_pson_deeply_nested();
    test_pson_mixed_array();

    // -- PSON edge cases --
    printf("\n--- PSON v2: edge cases ---\n");
    test_pson_edge_zero_values();
    test_pson_edge_large_numbers();
    test_pson_encode_decode_size();
    test_pson_null_writer_sizing();

    // -- IOTMP message --
    printf("\n--- IOTMP message ---\n");
    test_message_run_roundtrip();
    test_message_connect();
    test_message_keep_alive();
    test_message_types_enum();
    test_message_field_enum();
    test_message_type_str();
    test_message_field_access();
    test_message_remove_field();
    test_message_convenience_accessors();
    test_message_describe_roundtrip();
    test_message_start_stream_roundtrip();
    test_message_for_each_field();

    // -- Resource --
    printf("\n--- Resource ---\n");
    test_resource_run_callback();
    test_resource_output_callback();
    test_resource_input_callback();
    test_resource_input_output_callback();
    test_resource_none();
    test_resource_output_error();
    test_resource_output_return_code();
    test_resource_describe_output();
    test_resource_describe_input();
    test_resource_describe_input_output();
    test_resource_fill_api();
    test_resource_stream_properties();

    // -- I/O adapters --
    printf("\n--- I/O Adapters ---\n");
    test_memory_reader();
    test_string_writer();
    test_null_writer();
    test_memory_writer();

    printf("\n  %d passed, %d failed\n\n", tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
