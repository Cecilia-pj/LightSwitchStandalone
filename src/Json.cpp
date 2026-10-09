// LightSwitch Standalone - minimal JSON reader/writer (implementation).
#include "Json.h"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace json
{

namespace
{

// ---------------------------------------------------------------------------
// Serialization
// ---------------------------------------------------------------------------

void AppendEscapedString(std::string& out, const std::string& s)
{
    out.push_back('"');
    for (unsigned char c : s)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\b':
            out += "\\b";
            break;
        case '\f':
            out += "\\f";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                char buf[8];
                std::snprintf(buf, sizeof(buf), "\\u%04x", c);
                out += buf;
            }
            else
            {
                // Bytes >= 0x20 (including UTF-8 continuation bytes) pass through.
                out.push_back(static_cast<char>(c));
            }
            break;
        }
    }
    out.push_back('"');
}

void AppendNumber(std::string& out, double d)
{
    if (std::isnan(d) || std::isinf(d))
    {
        out += "0";
        return;
    }

    if (d == std::floor(d) && std::fabs(d) < 1.0e15)
    {
        char buf[32];
        std::snprintf(buf, sizeof(buf), "%lld", static_cast<long long>(d));
        out += buf;
        return;
    }

    char buf[40];
    std::snprintf(buf, sizeof(buf), "%.10g", d);
    out += buf;
}

void AppendIndent(std::string& out, int depth, int indent)
{
    if (indent <= 0)
        return;
    out.push_back('\n');
    out.append(static_cast<size_t>(depth) * static_cast<size_t>(indent), ' ');
}

void DumpValue(const Value& v, std::string& out, int indent, int depth)
{
    switch (v.type())
    {
    case Type::Null:
        out += "null";
        break;

    case Type::Bool:
        out += v.asBool() ? "true" : "false";
        break;

    case Type::Number:
        AppendNumber(out, v.asNumber());
        break;

    case Type::String:
        AppendEscapedString(out, v.asString());
        break;

    case Type::Array:
    {
        const auto& items = v.items();
        if (items.empty())
        {
            out += "[]";
            break;
        }
        out.push_back('[');
        for (size_t i = 0; i < items.size(); ++i)
        {
            if (i)
                out.push_back(',');
            AppendIndent(out, depth + 1, indent);
            DumpValue(items[i], out, indent, depth + 1);
        }
        AppendIndent(out, depth, indent);
        out.push_back(']');
        break;
    }

    case Type::Object:
    {
        const auto& members = v.members();
        if (members.empty())
        {
            out += "{}";
            break;
        }
        out.push_back('{');
        bool first = true;
        for (const auto& kv : members)
        {
            if (!first)
                out.push_back(',');
            first = false;
            AppendIndent(out, depth + 1, indent);
            AppendEscapedString(out, kv.first);
            out.push_back(':');
            if (indent > 0)
                out.push_back(' ');
            DumpValue(kv.second, out, indent, depth + 1);
        }
        AppendIndent(out, depth, indent);
        out.push_back('}');
        break;
    }
    }
}

// ---------------------------------------------------------------------------
// Parsing
// ---------------------------------------------------------------------------

class Parser
{
public:
    explicit Parser(const std::string& text) : m_text(text) {}

    bool Run(Value& out, std::string& error)
    {
        SkipWhitespace();
        if (!ParseValue(out, error))
            return false;
        SkipWhitespace();
        if (m_pos != m_text.size())
        {
            error = "trailing characters after top-level value";
            return false;
        }
        return true;
    }

private:
    const std::string& m_text;
    size_t m_pos = 0;
    int m_depth = 0;

    static constexpr int kMaxDepth = 64;

    bool AtEnd() const { return m_pos >= m_text.size(); }
    char Peek() const { return m_pos < m_text.size() ? m_text[m_pos] : '\0'; }

    void SkipWhitespace()
    {
        while (m_pos < m_text.size())
        {
            const char c = m_text[m_pos];
            if (c == ' ' || c == '\t' || c == '\n' || c == '\r')
                ++m_pos;
            else
                break;
        }
    }

    bool Fail(std::string& error, const char* what)
    {
        error = std::string(what) + " at offset " + std::to_string(m_pos);
        return false;
    }

    bool Literal(const char* text, std::string& error)
    {
        const size_t len = std::strlen(text);
        if (m_text.compare(m_pos, len, text) != 0)
            return Fail(error, "invalid literal");
        m_pos += len;
        return true;
    }

    bool ParseValue(Value& out, std::string& error)
    {
        if (++m_depth > kMaxDepth)
            return Fail(error, "nesting too deep");

        bool result = ParseValueInner(out, error);
        --m_depth;
        return result;
    }

    bool ParseValueInner(Value& out, std::string& error)
    {
        if (AtEnd())
            return Fail(error, "unexpected end of input");

        switch (Peek())
        {
        case '{':
            return ParseObject(out, error);
        case '[':
            return ParseArray(out, error);
        case '"':
        {
            std::string s;
            if (!ParseString(s, error))
                return false;
            out = Value(std::move(s));
            return true;
        }
        case 't':
            if (!Literal("true", error))
                return false;
            out = Value(true);
            return true;
        case 'f':
            if (!Literal("false", error))
                return false;
            out = Value(false);
            return true;
        case 'n':
            if (!Literal("null", error))
                return false;
            out = Value();
            return true;
        default:
            return ParseNumber(out, error);
        }
    }

    bool ParseObject(Value& out, std::string& error)
    {
        ++m_pos; // '{'
        out = Value::MakeObject();
        SkipWhitespace();
        if (Peek() == '}')
        {
            ++m_pos;
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            if (Peek() != '"')
                return Fail(error, "expected object key");

            std::string key;
            if (!ParseString(key, error))
                return false;

            SkipWhitespace();
            if (Peek() != ':')
                return Fail(error, "expected ':'");
            ++m_pos;

            SkipWhitespace();
            Value child;
            if (!ParseValue(child, error))
                return false;
            out.set(key, std::move(child));

            SkipWhitespace();
            const char c = Peek();
            if (c == ',')
            {
                ++m_pos;
                continue;
            }
            if (c == '}')
            {
                ++m_pos;
                return true;
            }
            return Fail(error, "expected ',' or '}'");
        }
    }

    bool ParseArray(Value& out, std::string& error)
    {
        ++m_pos; // '['
        out = Value::MakeArray();
        SkipWhitespace();
        if (Peek() == ']')
        {
            ++m_pos;
            return true;
        }

        for (;;)
        {
            SkipWhitespace();
            Value child;
            if (!ParseValue(child, error))
                return false;
            out.push_back(std::move(child));

            SkipWhitespace();
            const char c = Peek();
            if (c == ',')
            {
                ++m_pos;
                continue;
            }
            if (c == ']')
            {
                ++m_pos;
                return true;
            }
            return Fail(error, "expected ',' or ']'");
        }
    }

    static void AppendCodepointUtf8(std::string& out, unsigned int cp)
    {
        if (cp <= 0x7F)
        {
            out.push_back(static_cast<char>(cp));
        }
        else if (cp <= 0x7FF)
        {
            out.push_back(static_cast<char>(0xC0 | (cp >> 6)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else if (cp <= 0xFFFF)
        {
            out.push_back(static_cast<char>(0xE0 | (cp >> 12)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
        else
        {
            out.push_back(static_cast<char>(0xF0 | (cp >> 18)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 12) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | ((cp >> 6) & 0x3F)));
            out.push_back(static_cast<char>(0x80 | (cp & 0x3F)));
        }
    }

    bool ParseHex4(unsigned int& value, std::string& error)
    {
        if (m_pos + 4 > m_text.size())
            return Fail(error, "truncated \\u escape");

        value = 0;
        for (int i = 0; i < 4; ++i)
        {
            const char c = m_text[m_pos++];
            value <<= 4;
            if (c >= '0' && c <= '9')
                value |= static_cast<unsigned int>(c - '0');
            else if (c >= 'a' && c <= 'f')
                value |= static_cast<unsigned int>(c - 'a' + 10);
            else if (c >= 'A' && c <= 'F')
                value |= static_cast<unsigned int>(c - 'A' + 10);
            else
                return Fail(error, "invalid hex digit in \\u escape");
        }
        return true;
    }

    bool ParseString(std::string& out, std::string& error)
    {
        ++m_pos; // opening quote
        out.clear();

        while (true)
        {
            if (AtEnd())
                return Fail(error, "unterminated string");

            const char c = m_text[m_pos++];
            if (c == '"')
                return true;

            if (c != '\\')
            {
                out.push_back(c);
                continue;
            }

            if (AtEnd())
                return Fail(error, "unterminated escape");

            const char esc = m_text[m_pos++];
            switch (esc)
            {
            case '"':
                out.push_back('"');
                break;
            case '\\':
                out.push_back('\\');
                break;
            case '/':
                out.push_back('/');
                break;
            case 'b':
                out.push_back('\b');
                break;
            case 'f':
                out.push_back('\f');
                break;
            case 'n':
                out.push_back('\n');
                break;
            case 'r':
                out.push_back('\r');
                break;
            case 't':
                out.push_back('\t');
                break;
            case 'u':
            {
                unsigned int cp = 0;
                if (!ParseHex4(cp, error))
                    return false;

                if (cp >= 0xD800 && cp <= 0xDBFF)
                {
                    // High surrogate: try to pair it with a low surrogate.
                    if (m_pos + 1 < m_text.size() && m_text[m_pos] == '\\' && m_text[m_pos + 1] == 'u')
                    {
                        m_pos += 2;
                        unsigned int low = 0;
                        if (!ParseHex4(low, error))
                            return false;
                        if (low >= 0xDC00 && low <= 0xDFFF)
                        {
                            cp = 0x10000 + ((cp - 0xD800) << 10) + (low - 0xDC00);
                        }
                        else
                        {
                            AppendCodepointUtf8(out, 0xFFFD);
                            cp = low;
                        }
                    }
                    else
                    {
                        cp = 0xFFFD;
                    }
                }
                else if (cp >= 0xDC00 && cp <= 0xDFFF)
                {
                    cp = 0xFFFD; // lone low surrogate
                }

                AppendCodepointUtf8(out, cp);
                break;
            }
            default:
                return Fail(error, "invalid escape sequence");
            }
        }
    }

    bool ParseNumber(Value& out, std::string& error)
    {
        const size_t start = m_pos;

        if (Peek() == '-')
            ++m_pos;

        if (!std::isdigit(static_cast<unsigned char>(Peek())))
            return Fail(error, "invalid number");

        while (std::isdigit(static_cast<unsigned char>(Peek())))
            ++m_pos;

        if (Peek() == '.')
        {
            ++m_pos;
            if (!std::isdigit(static_cast<unsigned char>(Peek())))
                return Fail(error, "invalid fraction");
            while (std::isdigit(static_cast<unsigned char>(Peek())))
                ++m_pos;
        }

        if (Peek() == 'e' || Peek() == 'E')
        {
            ++m_pos;
            if (Peek() == '+' || Peek() == '-')
                ++m_pos;
            if (!std::isdigit(static_cast<unsigned char>(Peek())))
                return Fail(error, "invalid exponent");
            while (std::isdigit(static_cast<unsigned char>(Peek())))
                ++m_pos;
        }

        const std::string token = m_text.substr(start, m_pos - start);
        out = Value(std::strtod(token.c_str(), nullptr));
        return true;
    }
};

} // namespace

std::string Value::dump(int indent) const
{
    std::string out;
    out.reserve(256);
    DumpValue(*this, out, indent, 0);
    return out;
}

Value Parse(const std::string& text, bool* ok, std::string* error)
{
    bool localOk = false;
    std::string localError;

    Value result;
    try
    {
        Parser parser(text);
        localOk = parser.Run(result, localError);
    }
    catch (...)
    {
        localOk = false;
        localError = "parser threw an exception";
    }

    if (!localOk)
        result = Value();

    if (ok)
        *ok = localOk;
    if (error)
        *error = localError;

    return result;
}

} // namespace json
