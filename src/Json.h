// LightSwitch Standalone - minimal JSON reader/writer.
//
// Why hand-rolled: the goal of this project is to depend ONLY on the Windows
// SDK and the C++ standard library. PowerToys uses nlohmann/json via its
// common utils; vendoring that would pull a large header-only library and the
// PowerToys build glue. This JSON subset is intentionally small and satisfies
// exactly what a PowerToys-compatible settings.json needs:
//
//   * objects / arrays / strings / numbers / booleans / null
//   * UTF-8 on disk, escapes on write, \uXXXX on read
//
// It is NOT a general purpose JSON library: no duplicate-key detection, no
// big-number handling beyond double, no streaming. That is by design.
#pragma once

#include <map>
#include <string>
#include <vector>

namespace json
{

enum class Type
{
    Null,
    Bool,
    Number,
    String,
    Array,
    Object
};

class Value
{
public:
    Value() = default;
    Value(bool b) : m_type(Type::Bool), m_bool(b) {}
    Value(double d) : m_type(Type::Number), m_number(d) {}
    Value(int i) : m_type(Type::Number), m_number(static_cast<double>(i)) {}
    Value(const char* s) : m_type(Type::String), m_string(s ? s : "") {}
    Value(std::string s) : m_type(Type::String), m_string(std::move(s)) {}

    static Value MakeArray()
    {
        Value v;
        v.m_type = Type::Array;
        return v;
    }

    static Value MakeObject()
    {
        Value v;
        v.m_type = Type::Object;
        return v;
    }

    Type type() const { return m_type; }
    bool isNull() const { return m_type == Type::Null; }
    bool isBool() const { return m_type == Type::Bool; }
    bool isNumber() const { return m_type == Type::Number; }
    bool isString() const { return m_type == Type::String; }
    bool isArray() const { return m_type == Type::Array; }
    bool isObject() const { return m_type == Type::Object; }

    bool asBool(bool fallback = false) const { return m_type == Type::Bool ? m_bool : fallback; }
    double asNumber(double fallback = 0.0) const { return m_type == Type::Number ? m_number : fallback; }
    int asInt(int fallback = 0) const { return m_type == Type::Number ? static_cast<int>(m_number < 0 ? m_number - 0.5 : m_number + 0.5) : fallback; }
    const std::string& asString() const { return m_string; }
    std::string asString(const std::string& fallback) const { return m_type == Type::String ? m_string : fallback; }

    bool has(const std::string& key) const { return m_object.find(key) != m_object.end(); }
    const Value* find(const std::string& key) const
    {
        const auto it = m_object.find(key);
        return it == m_object.end() ? nullptr : &it->second;
    }

    void set(const std::string& key, Value v) { m_object[key] = std::move(v); }
    const std::map<std::string, Value>& members() const { return m_object; }

    void push_back(Value v) { m_array.push_back(std::move(v)); }
    const std::vector<Value>& items() const { return m_array; }
    size_t size() const { return m_type == Type::Array ? m_array.size() : (m_type == Type::Object ? m_object.size() : 0); }

    std::string dump(int indent = 2) const;

private:
    Type m_type = Type::Null;
    bool m_bool = false;
    double m_number = 0.0;
    std::string m_string;
    std::vector<Value> m_array;
    std::map<std::string, Value> m_object;
};

// Parses UTF-8 text. On failure returns a null Value and (optionally) fills
// `error` with a human readable reason. Never throws.
Value Parse(const std::string& text, bool* ok = nullptr, std::string* error = nullptr);

} // namespace json
