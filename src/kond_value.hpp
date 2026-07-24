#pragma once

#include "kond_frontend.hpp"

namespace kond {



struct Binding;
struct RuntimeCondition;
struct HttpRequestData;
struct HttpResponseData;

enum class ValueKind {
    Null,
    Integer,
    Float,
    Boolean,
    String,
    Array,
    Object,
    Function,
    Builtin,
    Method,
    Lambda,
    Condition,
    Reference,
    HttpRequest,
    HttpResponse
};
enum class BorrowKind { Shared, Unique };

enum FlowLabel : std::uint32_t {
    FlowPublic = 0,
    FlowUntrusted = 1U << 0,
    FlowSecret = 1U << 1,
    FlowPersonal = 1U << 2,
    FlowOpaque = 1U << 31
};

enum SafetyMark : std::uint32_t {
    SafetyNone = 0,
    SafetyHtmlText = 1U << 0,
    SafetyUrlComponent = 1U << 1,
    SafetySqlParameter = 1U << 2,
    SafetySqlQuery = 1U << 3
};

enum class EvidenceKind { StaticProof, RuntimeProof, Assumption };

struct OwnershipEvent {
    std::string binding;
    std::string transition;
    SourcePos pos;
};

struct OwnershipLog {
    std::vector<OwnershipEvent> events;

    void record(const std::string &binding, std::string transition, const SourcePos &pos) {
        events.push_back(OwnershipEvent{binding, std::move(transition), pos});
    }
};

struct BorrowHandle {
    std::weak_ptr<Binding> owner;
    std::weak_ptr<OwnershipLog> log;
    BorrowKind kind = BorrowKind::Shared;
    std::string bindingName;
    SourcePos pos;
    ~BorrowHandle();
};

struct Value {
    ValueKind kind = ValueKind::Null;
    std::uint32_t flow = FlowPublic;
    std::uint32_t safety = SafetyNone;
    std::int64_t integer = 0;
    bool boolean = false;
    double floating = 0.0;
    std::string string;
    std::shared_ptr<std::vector<Value>> array;
    std::shared_ptr<std::map<std::string, Value>> object;
    std::string name;
    ExprPtr lambda;
    std::shared_ptr<RuntimeCondition> condition;
    std::shared_ptr<Binding> reference;
    std::shared_ptr<Value> methodReceiver;
    std::shared_ptr<BorrowHandle> borrow;
    std::shared_ptr<HttpRequestData> request;
    std::shared_ptr<HttpResponseData> response;

    static Value null() { return Value{}; }
    static Value integerValue(std::int64_t value) {
        Value result;
        result.kind = ValueKind::Integer;
        result.integer = value;
        return result;
    }
    static Value floatValue(double value) {
        Value result;
        result.kind = ValueKind::Float;
        result.floating = value;
        return result;
    }
    static Value booleanValue(bool value) {
        Value result;
        result.kind = ValueKind::Boolean;
        result.boolean = value;
        return result;
    }
    static Value stringValue(std::string value) {
        Value result;
        result.kind = ValueKind::String;
        const std::size_t separator = value.find('\x1f');
        if (separator != std::string::npos) {
            const std::string tag = value.substr(0, separator);
            if (tag == "html") result.safety |= SafetyHtmlText;
            if (tag == "sql") result.safety |= SafetySqlQuery;
        }
        result.string = std::move(value);
        return result;
    }
    static Value arrayValue(std::vector<Value> values) {
        Value result;
        result.kind = ValueKind::Array;
        for (const Value &value : values) result.flow |= value.flow;
        result.array = std::make_shared<std::vector<Value>>(std::move(values));
        return result;
    }
    static Value objectValue(std::map<std::string, Value> values) {
        Value result;
        result.kind = ValueKind::Object;
        for (const auto &entry : values) result.flow |= entry.second.flow;
        result.object = std::make_shared<std::map<std::string, Value>>(std::move(values));
        return result;
    }
    static Value functionValue(std::string functionName) {
        Value result;
        result.kind = ValueKind::Function;
        result.name = std::move(functionName);
        return result;
    }
    static Value builtinValue(std::string builtinName) {
        Value result;
        result.kind = ValueKind::Builtin;
        result.name = std::move(builtinName);
        return result;
    }
    static Value methodValue(Value receiver, std::string methodName);
    static Value lambdaValue(ExprPtr expression) {
        Value result;
        result.kind = ValueKind::Lambda;
        result.lambda = std::move(expression);
        return result;
    }
    static Value referenceValue(std::shared_ptr<Binding> binding, BorrowKind kind, const SourcePos &pos);
    static Value conditionValue(std::shared_ptr<RuntimeCondition> value);

    bool isNumber() const { return kind == ValueKind::Integer || kind == ValueKind::Float; }
};

struct HttpRequestData {
    std::string method;
    std::string target;
    std::string path;
    std::string body;
    std::map<std::string, Value> query;
    std::map<std::string, Value> headers;
    std::map<std::string, Value> cookies;
};

struct HttpResponseData {
    int status = 200;
    std::map<std::string, Value> headers;
    Value body;
};

struct HttpResponse {
    int status = 200;
    std::map<std::string, std::string> headers;
    std::string body;
};

class JsonParser {
public:
    JsonParser(std::string_view input, SourcePos pos) : input_(input), pos_(std::move(pos)) {}

    Value parse() {
        skipWhitespace();
        Value result = parseValue();
        skipWhitespace();
        if (cursor_ != input_.size()) error("JSON の後ろに不要な文字があります");
        return result;
    }

private:
    std::string_view input_;
    std::size_t cursor_ = 0;
    SourcePos pos_;

    [[noreturn]] void error(const std::string &message) const {
        fail("E4001", pos_, "不正なJSONです: " + message);
    }

    char peek() const { return cursor_ < input_.size() ? input_[cursor_] : '\0'; }

    char advance() {
        if (cursor_ >= input_.size()) return '\0';
        return input_[cursor_++];
    }

    void skipWhitespace() {
        while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') ++cursor_;
    }

    static Value untrusted(Value value) {
        value.flow |= FlowUntrusted;
        return value;
    }

    static int hexDigit(char ch) {
        if (ch >= '0' && ch <= '9') return ch - '0';
        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
        return -1;
    }

    static void appendUtf8(std::string &output, unsigned int codepoint) {
        if (codepoint <= 0x7f) {
            output.push_back(static_cast<char>(codepoint));
        } else if (codepoint <= 0x7ff) {
            output.push_back(static_cast<char>(0xc0 | (codepoint >> 6)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else if (codepoint <= 0xffff) {
            output.push_back(static_cast<char>(0xe0 | (codepoint >> 12)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        } else {
            output.push_back(static_cast<char>(0xf0 | (codepoint >> 18)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 12) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | ((codepoint >> 6) & 0x3f)));
            output.push_back(static_cast<char>(0x80 | (codepoint & 0x3f)));
        }
    }

    unsigned int unicodeEscape() {
        unsigned int codepoint = 0;
        for (int i = 0; i < 4; ++i) {
            const int digit = hexDigit(advance());
            if (digit < 0) error("\\u エスケープが不正です");
            codepoint = (codepoint << 4) | static_cast<unsigned int>(digit);
        }
        return codepoint;
    }

    std::string parseString() {
        if (advance() != '"') error("文字列の開始が必要です");
        std::string result;
        while (true) {
            const char ch = advance();
            if (ch == '\0') error("文字列が閉じられていません");
            if (ch == '"') return result;
            if (static_cast<unsigned char>(ch) < 0x20) error("制御文字は文字列に含められません");
            if (ch != '\\') {
                result.push_back(ch);
                continue;
            }
            const char escaped = advance();
            switch (escaped) {
            case '"': result.push_back('"'); break;
            case '\\': result.push_back('\\'); break;
            case '/': result.push_back('/'); break;
            case 'b': result.push_back('\b'); break;
            case 'f': result.push_back('\f'); break;
            case 'n': result.push_back('\n'); break;
            case 'r': result.push_back('\r'); break;
            case 't': result.push_back('\t'); break;
            case 'u': {
                unsigned int codepoint = unicodeEscape();
                if (codepoint >= 0xd800 && codepoint <= 0xdbff) {
                    if (peek() != '\\' || cursor_ + 1 >= input_.size() || input_[cursor_ + 1] != 'u') {
                        error("上位サロゲートの対がありません");
                    }
                    cursor_ += 2;
                    const unsigned int low = unicodeEscape();
                    if (low < 0xdc00 || low > 0xdfff) error("下位サロゲートが不正です");
                    codepoint = 0x10000 + ((codepoint - 0xd800) << 10) + (low - 0xdc00);
                } else if (codepoint >= 0xdc00 && codepoint <= 0xdfff) {
                    error("下位サロゲートが単独で現れています");
                }
                appendUtf8(result, codepoint);
                break;
            }
            default: error("未対応のエスケープです");
            }
        }
    }

    Value parseNumber() {
        const std::size_t start = cursor_;
        if (peek() == '-') ++cursor_;
        if (peek() == '0') {
            ++cursor_;
            if (peek() >= '0' && peek() <= '9') error("数値の先頭に0を置けません");
        } else {
            if (peek() < '1' || peek() > '9') error("数値が必要です");
            while (peek() >= '0' && peek() <= '9') ++cursor_;
        }
        bool floating = false;
        if (peek() == '.') {
            floating = true;
            ++cursor_;
            if (peek() < '0' || peek() > '9') error("小数点の後ろに数字が必要です");
            while (peek() >= '0' && peek() <= '9') ++cursor_;
        }
        if (peek() == 'e' || peek() == 'E') {
            floating = true;
            ++cursor_;
            if (peek() == '+' || peek() == '-') ++cursor_;
            if (peek() < '0' || peek() > '9') error("指数部に数字が必要です");
            while (peek() >= '0' && peek() <= '9') ++cursor_;
        }

        const std::string text(input_.substr(start, cursor_ - start));
        try {
            if (!floating) {
                std::size_t consumed = 0;
                const std::int64_t value = std::stoll(text, &consumed);
                if (consumed != text.size()) error("整数が不正です");
                return untrusted(Value::integerValue(value));
            }
            std::size_t consumed = 0;
            const double value = std::stod(text, &consumed);
            if (consumed != text.size() || !std::isfinite(value)) error("浮動小数点数が不正です");
            return untrusted(Value::floatValue(value));
        } catch (const std::exception &) {
            error("数値が範囲外です");
        }
    }

    Value parseArray() {
        advance();
        std::vector<Value> values;
        skipWhitespace();
        if (peek() != ']') {
            while (true) {
                values.push_back(parseValue());
                skipWhitespace();
                if (peek() == ']') break;
                if (advance() != ',') error("配列要素の区切り ',' が必要です");
                skipWhitespace();
            }
        }
        if (advance() != ']') error("配列が閉じられていません");
        return untrusted(Value::arrayValue(std::move(values)));
    }

    Value parseObject() {
        advance();
        std::map<std::string, Value> fields;
        skipWhitespace();
        if (peek() != '}') {
            while (true) {
                if (peek() != '"') error("オブジェクトのキーには文字列が必要です");
                const std::string key = parseString();
                skipWhitespace();
                if (advance() != ':') error("オブジェクトのキーと値の間に ':' が必要です");
                skipWhitespace();
                fields[key] = parseValue();
                skipWhitespace();
                if (peek() == '}') break;
                if (advance() != ',') error("オブジェクト要素の区切り ',' が必要です");
                skipWhitespace();
            }
        }
        if (advance() != '}') error("オブジェクトが閉じられていません");
        return untrusted(Value::objectValue(std::move(fields)));
    }

    Value parseValue() {
        skipWhitespace();
        switch (peek()) {
        case 'n':
            if (input_.substr(cursor_, 4) != "null") error("null が不正です");
            cursor_ += 4;
            return untrusted(Value::null());
        case 't':
            if (input_.substr(cursor_, 4) != "true") error("true が不正です");
            cursor_ += 4;
            return untrusted(Value::booleanValue(true));
        case 'f':
            if (input_.substr(cursor_, 5) != "false") error("false が不正です");
            cursor_ += 5;
            return untrusted(Value::booleanValue(false));
        case '"': return untrusted(Value::stringValue(parseString()));
        case '[': return parseArray();
        case '{': return parseObject();
        default:
            if (peek() == '-' || (peek() >= '0' && peek() <= '9')) return parseNumber();
            error("値が必要です");
        }
    }
};


} // namespace kond
