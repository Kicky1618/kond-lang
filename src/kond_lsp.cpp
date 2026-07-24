#include "kond_lsp.hpp"

#include "kond_interpreter_api.hpp"
#include "kond_value.hpp"

#include <cctype>
#include <filesystem>
#include <fstream>

namespace kond {
namespace {

using JsonField = std::pair<std::string, std::string>;

const Value *objectField(const Value &value, const std::string &name) {
    if (value.kind != ValueKind::Object || !value.object) return nullptr;
    const auto found = value.object->find(name);
    return found == value.object->end() ? nullptr : &found->second;
}

std::string stringField(const Value &value, const std::string &name, const std::string &fallback = {}) {
    const Value *field = objectField(value, name);
    return field && field->kind == ValueKind::String ? field->string : fallback;
}

int integerField(const Value &value, const std::string &name, int fallback = 0) {
    const Value *field = objectField(value, name);
    if (!field) return fallback;
    if (field->kind == ValueKind::Integer) return static_cast<int>(field->integer);
    if (field->kind == ValueKind::Float) return static_cast<int>(field->floating);
    return fallback;
}

bool booleanField(const Value &value, const std::string &name, bool fallback = false) {
    const Value *field = objectField(value, name);
    return field && field->kind == ValueKind::Boolean ? field->boolean : fallback;
}

std::string jsonEscape(std::string_view text) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::ostringstream output;
    for (unsigned char ch : text) {
        switch (ch) {
        case '"': output << "\\\""; break;
        case '\\': output << "\\\\"; break;
        case '\b': output << "\\b"; break;
        case '\f': output << "\\f"; break;
        case '\n': output << "\\n"; break;
        case '\r': output << "\\r"; break;
        case '\t': output << "\\t"; break;
        default:
            if (ch < 0x20) {
                output << "\\u00" << hex[(ch >> 4) & 0x0f] << hex[ch & 0x0f];
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

std::string jsonQuoted(std::string_view value) {
    return "\"" + jsonEscape(value) + "\"";
}

std::string jsonObject(const std::vector<JsonField> &fields) {
    std::string result = "{";
    for (std::size_t i = 0; i < fields.size(); ++i) {
        if (i != 0) result += ',';
        result += jsonQuoted(fields[i].first) + ":" + fields[i].second;
    }
    return result + "}";
}

std::string jsonObject(std::initializer_list<JsonField> fields) {
    return jsonObject(std::vector<JsonField>(fields));
}

std::string jsonArray(const std::vector<std::string> &values) {
    std::string result = "[";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) result += ',';
        result += values[i];
    }
    return result + "]";
}

std::string jsonValue(const Value &value) {
    switch (value.kind) {
    case ValueKind::Null: return "null";
    case ValueKind::Integer: return std::to_string(value.integer);
    case ValueKind::Float: {
        if (!std::isfinite(value.floating)) return "null";
        std::ostringstream output;
        output << std::setprecision(15) << value.floating;
        return output.str();
    }
    case ValueKind::Boolean: return value.boolean ? "true" : "false";
    case ValueKind::String: return jsonQuoted(value.string);
    case ValueKind::Array: {
        std::vector<std::string> values;
        if (value.array) {
            values.reserve(value.array->size());
            for (const Value &item : *value.array) values.push_back(jsonValue(item));
        }
        return jsonArray(values);
    }
    case ValueKind::Object: {
        std::vector<JsonField> fields;
        if (value.object) {
            fields.reserve(value.object->size());
            for (const auto &field : *value.object) fields.emplace_back(field.first, jsonValue(field.second));
        }
        return jsonObject(fields);
    }
    default: return "null";
    }
}

int hexValue(char ch) {
    if (ch >= '0' && ch <= '9') return ch - '0';
    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
    return -1;
}

std::string percentDecode(std::string value) {
    std::string result;
    result.reserve(value.size());
    for (std::size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%' && i + 2 < value.size()) {
            const int high = hexValue(value[i + 1]);
            const int low = hexValue(value[i + 2]);
            if (high >= 0 && low >= 0) {
                result.push_back(static_cast<char>((high << 4) | low));
                i += 2;
                continue;
            }
        }
        result.push_back(value[i]);
    }
    return result;
}

std::string percentEncode(const std::string &value) {
    static constexpr char hex[] = "0123456789ABCDEF";
    std::string result;
    for (unsigned char ch : value) {
        const bool safe = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                          (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' ||
                          ch == '~' || ch == '/';
        if (safe) {
            result.push_back(static_cast<char>(ch));
        } else {
            result.push_back('%');
            result.push_back(hex[(ch >> 4) & 0x0f]);
            result.push_back(hex[ch & 0x0f]);
        }
    }
    return result;
}

std::string uriToPath(const std::string &uri) {
    if (uri.rfind("file://", 0) != 0) return uri;
    std::string path = uri.substr(7);
    if (path.rfind("localhost/", 0) == 0) path.erase(0, 9);
    if (path.rfind("localhost", 0) == 0 && path.size() > 9 && path[9] == '/') path.erase(0, 10);
    return percentDecode(path);
}

std::string pathToUri(const std::string &path) {
    std::error_code error;
    std::filesystem::path absolute = std::filesystem::absolute(path, error);
    const std::string normalized = (error ? std::filesystem::path(path) : absolute).generic_string();
    if (!normalized.empty() && normalized.front() == '/') return "file://" + percentEncode(normalized);
    return "file:///" + percentEncode(normalized);
}

struct LspPosition {
    int line = 0;
    int character = 0;
};

struct LspRange {
    LspPosition start;
    LspPosition end;
};

std::size_t nextUtf8(std::string_view text, std::size_t offset, std::uint32_t &codepoint) {
    if (offset >= text.size()) {
        codepoint = 0;
        return offset;
    }
    const unsigned char first = static_cast<unsigned char>(text[offset]);
    if (first < 0x80) {
        codepoint = first;
        return offset + 1;
    }
    const int width = first >= 0xf0 ? 4 : first >= 0xe0 ? 3 : first >= 0xc0 ? 2 : 1;
    if (width == 1 || offset + static_cast<std::size_t>(width) > text.size()) {
        codepoint = first;
        return offset + 1;
    }
    std::uint32_t result = first & (width == 2 ? 0x1fU : width == 3 ? 0x0fU : 0x07U);
    for (int i = 1; i < width; ++i) {
        const unsigned char continuation = static_cast<unsigned char>(text[offset + static_cast<std::size_t>(i)]);
        if ((continuation & 0xc0U) != 0x80U) {
            codepoint = first;
            return offset + 1;
        }
        result = (result << 6) | (continuation & 0x3fU);
    }
    codepoint = result;
    return offset + static_cast<std::size_t>(width);
}

int utf16Width(std::uint32_t codepoint) {
    return codepoint > 0xffffU ? 2 : 1;
}

std::size_t offsetForPosition(std::string_view text, LspPosition position) {
    const int requestedLine = std::max(0, position.line);
    const int requestedCharacter = std::max(0, position.character);
    std::size_t lineStart = 0;
    int line = 0;
    while (line < requestedLine) {
        const std::size_t newline = text.find('\n', lineStart);
        if (newline == std::string_view::npos) return text.size();
        lineStart = newline + 1;
        ++line;
    }
    std::size_t cursor = lineStart;
    int character = 0;
    while (cursor < text.size() && text[cursor] != '\n' && character < requestedCharacter) {
        std::uint32_t codepoint = 0;
        const std::size_t next = nextUtf8(text, cursor, codepoint);
        const int width = utf16Width(codepoint);
        if (character + width > requestedCharacter) break;
        cursor = next;
        character += width;
    }
    return cursor;
}

LspPosition positionForOffset(std::string_view text, std::size_t requestedOffset) {
    const std::size_t offset = std::min(requestedOffset, text.size());
    LspPosition result;
    std::size_t lineStart = 0;
    for (std::size_t i = 0; i < offset; ++i) {
        if (text[i] == '\n') {
            ++result.line;
            lineStart = i + 1;
        }
    }
    std::size_t cursor = lineStart;
    while (cursor < offset) {
        std::uint32_t codepoint = 0;
        const std::size_t next = nextUtf8(text, cursor, codepoint);
        if (next > offset) break;
        result.character += utf16Width(codepoint);
        cursor = next;
    }
    return result;
}

std::string positionJson(const LspPosition &position) {
    return jsonObject({{"line", std::to_string(position.line)},
                       {"character", std::to_string(position.character)}});
}

std::string rangeJson(const LspRange &range) {
    return jsonObject({{"start", positionJson(range.start)}, {"end", positionJson(range.end)}});
}

std::string paramsText(const std::vector<Param> &params) {
    std::string result = "(";
    for (std::size_t i = 0; i < params.size(); ++i) {
        if (i != 0) result += ", ";
        result += params[i].name;
    }
    return result + ")";
}

struct Diagnostic {
    SourcePos pos;
    std::string code;
    std::string message;
};

struct Document {
    std::string uri;
    std::string path;
    std::string text;
    int version = 0;
    bool fromClient = false;
    std::optional<Program> program;
    std::vector<Token> tokens;
    std::vector<Diagnostic> diagnostics;
};

struct SourceScope {
    std::size_t start = 0;
    std::size_t end = 0;
};

std::vector<SourceScope> functionScopes(const Document &document) {
    std::vector<SourceScope> scopes;
    if (!document.program) return scopes;
    for (const auto &entry : document.program->functions) {
        const FunctionDef &function = entry.second;
        std::size_t tokenIndex = 0;
        while (tokenIndex < document.tokens.size() && document.tokens[tokenIndex].pos.offset < function.pos.offset) {
            ++tokenIndex;
        }
        while (tokenIndex < document.tokens.size() && document.tokens[tokenIndex].kind != TokenKind::LBrace) {
            ++tokenIndex;
        }
        if (tokenIndex >= document.tokens.size()) continue;
        int depth = 0;
        for (std::size_t i = tokenIndex; i < document.tokens.size(); ++i) {
            if (document.tokens[i].kind == TokenKind::LBrace) {
                ++depth;
            } else if (document.tokens[i].kind == TokenKind::RBrace) {
                --depth;
                if (depth == 0) {
                    scopes.push_back(SourceScope{function.pos.offset,
                                                 document.tokens[i].pos.offset + document.tokens[i].text.size()});
                    break;
                }
            }
        }
    }
    return scopes;
}

std::size_t scopeAt(const Document &document, std::size_t offset) {
    const std::vector<SourceScope> scopes = functionScopes(document);
    std::size_t best = std::numeric_limits<std::size_t>::max();
    std::size_t bestWidth = std::numeric_limits<std::size_t>::max();
    for (const SourceScope &scope : scopes) {
        if (offset < scope.start || offset > scope.end) continue;
        const std::size_t width = scope.end - scope.start;
        if (width < bestWidth) {
            best = scope.start;
            bestWidth = width;
        }
    }
    return best;
}

LspRange sourceRange(const Document &document, const SourcePos &pos, std::size_t length) {
    std::size_t startOffset = pos.offset;
    if (startOffset > document.text.size()) {
        startOffset = offsetForPosition(document.text, LspPosition{std::max(0, pos.line - 1),
                                                                    std::max(0, pos.column - 1)});
    }
    const std::size_t endOffset = std::min(document.text.size(), startOffset + length);
    return LspRange{positionForOffset(document.text, startOffset), positionForOffset(document.text, endOffset)};
}

LspRange diagnosticRange(const Document &document, const SourcePos &pos) {
    std::size_t startOffset = pos.offset;
    if (startOffset > document.text.size()) {
        startOffset = offsetForPosition(document.text, LspPosition{std::max(0, pos.line - 1),
                                                                    std::max(0, pos.column - 1)});
    }
    std::size_t endOffset = startOffset;
    if (startOffset < document.text.size() && document.text[startOffset] != '\n') {
        std::uint32_t codepoint = 0;
        endOffset = nextUtf8(document.text, startOffset, codepoint);
    }
    return LspRange{positionForOffset(document.text, startOffset), positionForOffset(document.text, endOffset)};
}

std::string diagnosticJson(const Document &document, const Diagnostic &diagnostic) {
    return jsonObject({{"range", rangeJson(diagnosticRange(document, diagnostic.pos))},
                       {"severity", "1"},
                       {"source", jsonQuoted("kond")},
                       {"code", jsonQuoted(diagnostic.code)},
                       {"message", jsonQuoted(diagnostic.message)}});
}

bool identifierCharacter(char ch) {
    return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
           (ch >= '0' && ch <= '9') || ch == '_';
}

struct WordAtPosition {
    std::string text;
    std::size_t start = 0;
    std::size_t end = 0;
};

WordAtPosition wordAtPosition(std::string_view text, std::size_t requestedOffset) {
    std::size_t cursor = std::min(requestedOffset, text.size());
    if ((cursor == text.size() || !identifierCharacter(text[cursor])) && cursor > 0 &&
        identifierCharacter(text[cursor - 1])) {
        --cursor;
    }
    if (cursor >= text.size() || !identifierCharacter(text[cursor])) return {};
    std::size_t start = cursor;
    while (start > 0 && identifierCharacter(text[start - 1])) --start;
    std::size_t end = cursor + 1;
    while (end < text.size() && identifierCharacter(text[end])) ++end;
    return WordAtPosition{std::string(text.substr(start, end - start)), start, end};
}

std::string completionPrefix(std::string_view text, std::size_t requestedOffset, std::size_t &start) {
    const std::size_t cursor = std::min(requestedOffset, text.size());
    start = cursor;
    while (start > 0 && (identifierCharacter(text[start - 1]) || text[start - 1] == '.')) --start;
    return std::string(text.substr(start, cursor - start));
}

struct SymbolEntry {
    std::string name;
    std::string selectionText;
    std::string detail;
    std::string uri;
    SourcePos pos;
    int kind = 12;
    bool local = false;
};

std::string functionDetail(const FunctionDef &function) {
    if (function.route) return "route " + function.routeMethod + " \"" + function.routePath + "\"";
    return std::string(function.unsafe ? "unsafe fn " : "fn ") + function.name + paramsText(function.params);
}

std::string conditionDetail(const ConditionDef &condition) {
    return "condition " + condition.name + paramsText(condition.params);
}

std::vector<SymbolEntry> documentSymbols(const Document &document, bool includeLocals) {
    std::vector<SymbolEntry> result;
    if (!document.program) return result;

    for (const auto &entry : document.program->conditions) {
        const ConditionDef &condition = entry.second;
        result.push_back(SymbolEntry{condition.name, condition.name, conditionDetail(condition), document.uri,
                                     condition.pos, 12, false});
        if (includeLocals) {
            for (const Param &param : condition.params) {
                result.push_back(SymbolEntry{param.name, param.name, "parameter", document.uri, param.pos, 13, true});
            }
        }
    }
    for (const auto &entry : document.program->functions) {
        const FunctionDef &function = entry.second;
        const std::string displayName = function.route ? function.routeMethod + " " + function.routePath : function.name;
        const std::string selection = function.route ? function.routeMethod : function.name;
        result.push_back(SymbolEntry{displayName, selection, functionDetail(function), document.uri, function.pos,
                                     function.route ? 6 : 12, false});
        if (includeLocals) {
            for (const Param &param : function.params) {
                result.push_back(SymbolEntry{param.name, param.name, "parameter", document.uri, param.pos, 13, true});
            }
        }
    }

    if (includeLocals) {
        for (std::size_t i = 0; i + 1 < document.tokens.size(); ++i) {
            const Token &keyword = document.tokens[i];
            if (keyword.kind != TokenKind::Identifier || (keyword.text != "let" && keyword.text != "for")) continue;
            const Token &name = document.tokens[i + 1];
            if (name.kind != TokenKind::Identifier) continue;
            result.push_back(SymbolEntry{name.text, name.text,
                                         keyword.text == "let" ? "local variable" : "loop variable",
                                         document.uri, name.pos, 13, true});
        }
    }
    return result;
}

std::string locationJson(const Document &document, const SymbolEntry &symbol) {
    return jsonObject({{"uri", jsonQuoted(symbol.uri)},
                       {"range", rangeJson(sourceRange(document, symbol.pos, symbol.selectionText.size()))}});
}

std::string symbolInformationJson(const Document &document, const SymbolEntry &symbol) {
    return jsonObject({{"name", jsonQuoted(symbol.name)},
                       {"kind", std::to_string(symbol.kind)},
                       {"location", locationJson(document, symbol)},
                       {"containerName", jsonQuoted("Kond")}});
}

std::string documentSymbolJson(const Document &document, const SymbolEntry &symbol) {
    const LspRange range = sourceRange(document, symbol.pos, symbol.selectionText.size());
    return jsonObject({{"name", jsonQuoted(symbol.name)},
                       {"detail", jsonQuoted(symbol.detail)},
                       {"kind", std::to_string(symbol.kind)},
                       {"range", rangeJson(range)},
                       {"selectionRange", rangeJson(range)}});
}

struct BuiltinSpec {
    const char *name;
    const char *detail;
};

const std::vector<BuiltinSpec> &builtinSpecs() {
    static const std::vector<BuiltinSpec> specs{
        {"print", "print(value)"}, {"println", "println(value)"}, {"input", "input()"},
        {"len", "len(value)"}, {"abs", "abs(number)"}, {"sqrt", "sqrt(number)"},
        {"type", "type(value)"}, {"push", "push(list, value)"}, {"pop", "pop(list)"},
        {"untrusted", "untrusted(value)"}, {"secret", "secret(value)"}, {"personal", "personal(value)"},
        {"std.core.clone", "std.core.clone(value)"}, {"std.core.type_of", "std.core.type_of(value)"},
        {"std.core.to_string", "std.core.to_string(value)"}, {"std.core.coalesce", "std.core.coalesce(value, fallback)"},
        {"std.core.range", "std.core.range(start, end[, step])"}, {"std.core.repeat", "std.core.repeat(value, count)"},
        {"std.math.abs", "std.math.abs(number)"}, {"std.math.sqrt", "std.math.sqrt(number)"},
        {"std.math.floor", "std.math.floor(number)"}, {"std.math.ceil", "std.math.ceil(number)"},
        {"std.math.round", "std.math.round(number)"}, {"std.math.pow", "std.math.pow(left, right)"},
        {"std.math.min", "std.math.min(values...)"}, {"std.math.max", "std.math.max(values...)"},
        {"std.math.clamp", "std.math.clamp(value, lower, upper)"}, {"std.math.sign", "std.math.sign(number)"},
        {"std.math.gcd", "std.math.gcd(left, right)"}, {"std.math.lcm", "std.math.lcm(left, right)"},
        {"std.string.length", "std.string.length(value)"}, {"std.string.trim", "std.string.trim(value)"},
        {"std.string.uppercase", "std.string.uppercase(value)"}, {"std.string.lowercase", "std.string.lowercase(value)"},
        {"std.string.contains", "std.string.contains(value, needle)"},
        {"std.string.starts_with", "std.string.starts_with(value, prefix)"},
        {"std.string.ends_with", "std.string.ends_with(value, suffix)"},
        {"std.string.split", "std.string.split(value, separator)"},
        {"std.string.join", "std.string.join(list, separator)"},
        {"std.string.replace", "std.string.replace(value, old, new)"},
        {"std.string.substring", "std.string.substring(value, start[, length])"},
        {"std.string.char_at", "std.string.char_at(value, index)"},
        {"std.string.repeat", "std.string.repeat(value, count)"}, {"std.string.reverse", "std.string.reverse(value)"},
        {"std.string.parse_int", "std.string.parse_int(value[, base])"},
        {"std.string.parse_float", "std.string.parse_float(value)"},
        {"std.list.length", "std.list.length(list)"}, {"std.list.first", "std.list.first(list)"},
        {"std.list.last", "std.list.last(list)"}, {"std.list.get", "std.list.get(list, index[, fallback])"},
        {"std.list.slice", "std.list.slice(list, start, end)"}, {"std.list.append", "std.list.append(list, value)"},
        {"std.list.prepend", "std.list.prepend(list, value)"}, {"std.list.concat", "std.list.concat(lists...)"},
        {"std.list.reverse", "std.list.reverse(list)"}, {"std.list.contains", "std.list.contains(list, value)"},
        {"std.list.filter", "std.list.filter(list, predicate)"}, {"std.list.count_if", "std.list.count_if(list, predicate)"},
        {"std.list.find", "std.list.find(list, predicate)"}, {"std.list.sum", "std.list.sum(list)"},
        {"std.list.sort", "std.list.sort(list)"}, {"std.list.sort_desc", "std.list.sort_desc(list)"},
        {"std.list.zip", "std.list.zip(left, right)"}, {"std.map.length", "std.map.length(object)"},
        {"std.map.size", "std.map.size(object)"}, {"std.map.has", "std.map.has(object, key)"},
        {"std.map.get", "std.map.get(object, key)"}, {"std.map.get_or", "std.map.get_or(object, key, fallback)"},
        {"std.map.keys", "std.map.keys(object)"}, {"std.map.values", "std.map.values(object)"},
        {"std.map.entries", "std.map.entries(object)"}, {"std.map.put", "std.map.put(object, key, value)"},
        {"std.map.remove", "std.map.remove(object, key)"}, {"std.map.merge", "std.map.merge(left, right)"},
        {"std.pred.is_null", "std.pred.is_null(value)"}, {"std.pred.is_int", "std.pred.is_int(value)"},
        {"std.pred.is_float", "std.pred.is_float(value)"}, {"std.pred.is_number", "std.pred.is_number(value)"},
        {"std.pred.is_bool", "std.pred.is_bool(value)"}, {"std.pred.is_string", "std.pred.is_string(value)"},
        {"std.pred.is_list", "std.pred.is_list(value)"}, {"std.pred.is_object", "std.pred.is_object(value)"},
        {"std.pred.is_empty", "std.pred.is_empty(value)"}, {"std.pred.contains", "std.pred.contains(value, needle)"},
        {"std.pred.has_key", "std.pred.has_key(object, key)"}, {"std.pred.positive", "std.pred.positive(number)"},
        {"std.pred.nonnegative", "std.pred.nonnegative(number)"}, {"std.pred.negative", "std.pred.negative(number)"},
        {"std.pred.even", "std.pred.even(number)"}, {"std.pred.odd", "std.pred.odd(number)"},
        {"std.pred.in_range", "std.pred.in_range(value, lower, upper)"},
        {"std.json.parse", "std.json.parse(text)"}, {"std.json.stringify", "std.json.stringify(value)"},
        {"std.json.pretty", "std.json.pretty(value)"}, {"std.json.is_valid", "std.json.is_valid(text)"},
        {"std.url.encode_component", "std.url.encode_component(value)"},
        {"std.url.decode_component", "std.url.decode_component(value)"},
        {"std.url.parse_query", "std.url.parse_query(query)"}, {"std.html.escape", "std.html.escape(value)"},
        {"std.html.escape_text", "std.html.escape_text(value)"},
        {"std.html.escape_attribute", "std.html.escape_attribute(value)"},
        {"std.security.untrusted", "std.security.untrusted(value)"},
        {"std.security.secret", "std.security.secret(value)"}, {"std.security.personal", "std.security.personal(value)"},
        {"std.security.escape_html", "std.security.escape_html(value)"},
        {"std.security.encode_url", "std.security.encode_url(value)"},
        {"std.security.parameterize_sql", "std.security.parameterize_sql(value)"},
        {"std.security.flow_label", "std.security.flow_label(value)"},
        {"std.http.response", "std.http.response(status, headers, body)"},
        {"std.http.json_response", "std.http.json_response(status, body)"},
        {"std.io.read_line", "std.io.read_line()"}, {"std.io.print", "std.io.print(value)"},
        {"std.io.println", "std.io.println(value)"}, {"std.opt.rule_count", "std.opt.rule_count()"},
        {"std.opt.rule_names", "std.opt.rule_names()"}
    };
    return specs;
}

const BuiltinSpec *findBuiltin(const std::string &name) {
    for (const BuiltinSpec &spec : builtinSpecs()) {
        if (name == spec.name) return &spec;
    }
    return nullptr;
}

std::string definitionDescription(const SymbolEntry &symbol) {
    return "```kond\n" + symbol.detail + "\n```";
}

class LspServer {
public:
    int run() {
        std::ios::sync_with_stdio(false);
        std::cin.tie(nullptr);

        std::string payload;
        while (readMessage(payload)) {
            try {
                const Value message = JsonParser(payload, SourcePos{"<lsp>", 0, 1, 1}).parse();
                if (!handleMessage(message)) break;
            } catch (const KondError &error) {
                sendError("null", -32700, error.what());
            } catch (const std::exception &error) {
                sendError("null", -32603, error.what());
            }
        }
        return 0;
    }

private:
    std::unordered_map<std::string, Document> documents_;
    std::string rootPath_;
    bool workspaceDiscovered_ = false;
    bool shutdownRequested_ = false;

    static bool readMessage(std::string &payload) {
        std::string line;
        std::size_t contentLength = 0;
        bool hasContentLength = false;
        bool hasHeader = false;
        while (std::getline(std::cin, line)) {
            if (!line.empty() && line.back() == '\r') line.pop_back();
            if (line.empty()) break;
            hasHeader = true;
            const std::size_t separator = line.find(':');
            if (separator == std::string::npos) continue;
            std::string header = line.substr(0, separator);
            for (char &ch : header) ch = static_cast<char>(std::tolower(static_cast<unsigned char>(ch)));
            if (header != "content-length") continue;
            try {
                contentLength = static_cast<std::size_t>(std::stoull(line.substr(separator + 1)));
                hasContentLength = true;
            } catch (const std::exception &) {
                return false;
            }
        }
        if (!hasHeader || !hasContentLength || contentLength > 64 * 1024 * 1024) return false;
        payload.assign(contentLength, '\0');
        std::cin.read(payload.data(), static_cast<std::streamsize>(contentLength));
        return static_cast<std::size_t>(std::cin.gcount()) == contentLength;
    }

    static void sendPayload(const std::string &payload) {
        std::cout << "Content-Length: " << payload.size() << "\r\n\r\n" << payload;
        std::cout.flush();
    }

    static void sendError(const std::string &id, int code, const std::string &message) {
        sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")},
                                 {"id", id},
                                 {"error", jsonObject({{"code", std::to_string(code)},
                                                        {"message", jsonQuoted(message)}})}}));
    }

    static bool validMessage(const Value &message) {
        return message.kind == ValueKind::Object && objectField(message, "method") != nullptr;
    }

    bool handleMessage(const Value &message) {
        if (!validMessage(message)) {
            sendError("null", -32600, "JSON-RPC リクエストが不正です");
            return true;
        }
        const std::string method = stringField(message, "method");
        const Value *idValue = objectField(message, "id");
        const std::string id = idValue ? jsonValue(*idValue) : std::string{};
        const Value *params = objectField(message, "params");

        if (method == "initialize") {
            handleInitialize(params);
            if (idValue) {
                sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")},
                                        {"id", id},
                                        {"result", initializeResult()}}));
            }
            return true;
        }
        if (method == "initialized" || method == "$/cancelRequest") return true;
        if (method == "shutdown") {
            shutdownRequested_ = true;
            if (idValue) {
                sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")}, {"id", id}, {"result", "null"}}));
            }
            return true;
        }
        if (method == "exit") return false;

        if (method == "textDocument/didOpen") {
            if (params) handleDidOpen(*params);
            return true;
        }
        if (method == "textDocument/didChange") {
            if (params) handleDidChange(*params);
            return true;
        }
        if (method == "textDocument/didClose") {
            if (params) handleDidClose(*params);
            return true;
        }
        if (method == "textDocument/didSave") {
            if (params) handleDidSave(*params);
            return true;
        }

        if (shutdownRequested_) {
            if (idValue) sendError(id, -32600, "shutdown 後はリクエストを受け付けません");
            return true;
        }

        if (method == "textDocument/completion") {
            if (idValue) sendResult(id, completion(params));
            return true;
        }
        if (method == "textDocument/hover") {
            if (idValue) sendResult(id, hover(params));
            return true;
        }
        if (method == "textDocument/definition") {
            if (idValue) sendResult(id, definition(params));
            return true;
        }
        if (method == "textDocument/references") {
            if (idValue) sendResult(id, references(params));
            return true;
        }
        if (method == "textDocument/documentSymbol") {
            if (idValue) sendResult(id, documentSymbolsResult(params));
            return true;
        }
        if (method == "workspace/symbol") {
            if (idValue) sendResult(id, workspaceSymbols(params));
            return true;
        }

        if (idValue) sendError(id, -32601, "未対応のLSPメソッドです: " + method);
        return true;
    }

    void sendResult(const std::string &id, const std::string &result) {
        sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")}, {"id", id}, {"result", result}}));
    }

    void handleInitialize(const Value *params) {
        if (!params || params->kind != ValueKind::Object) return;
        const std::string rootUri = stringField(*params, "rootUri");
        if (!rootUri.empty()) {
            rootPath_ = uriToPath(rootUri);
        } else if (const Value *folders = objectField(*params, "workspaceFolders")) {
            if (folders->kind == ValueKind::Array && folders->array && !folders->array->empty()) {
                rootPath_ = uriToPath(stringField(folders->array->front(), "uri"));
            }
        }
    }

    static std::string initializeResult() {
        const std::string sync = jsonObject({{"openClose", "true"}, {"change", "1"},
                                              {"save", jsonObject({{"includeText", "false"}})}});
        const std::string completion = jsonObject({{"triggerCharacters", jsonArray({jsonQuoted(".")})}});
        const std::string capabilities = jsonObject({
            {"textDocumentSync", sync}, {"completionProvider", completion}, {"hoverProvider", "true"},
            {"definitionProvider", "true"}, {"referencesProvider", "true"},
            {"documentSymbolProvider", "true"}, {"workspaceSymbolProvider", "true"}
        });
        return jsonObject({{"capabilities", capabilities},
                           {"serverInfo", jsonObject({{"name", jsonQuoted("kond-lsp")}, {"version", jsonQuoted("0.1")}})}});
    }

    void analyze(Document &document) {
        document.program.reset();
        document.tokens.clear();
        document.diagnostics.clear();
        try {
            document.program.emplace(parseProgram(document.text, document.path));
        } catch (const KondError &error) {
            document.diagnostics.push_back(Diagnostic{error.pos, error.code, error.what()});
            return;
        } catch (const std::exception &error) {
            document.diagnostics.push_back(Diagnostic{SourcePos{document.path, 0, 1, 1}, "KOND", error.what()});
            return;
        }

        try {
            document.tokens = tokenize(document.text, document.path);
        } catch (const std::exception &) {
            // parseProgram already ran the same lexer.  Keep AST features
            // available if a future parser implementation changes that path.
        }

        try {
            auto interpreter = makeInterpreter(*document.program, Mode::Safe, document.path, false, false, false);
            validateInterpreter(*interpreter);
        } catch (const KondError &error) {
            document.diagnostics.push_back(Diagnostic{error.pos, error.code, error.what()});
        } catch (const std::exception &error) {
            document.diagnostics.push_back(Diagnostic{SourcePos{document.path, 0, 1, 1}, "KOND", error.what()});
        }
    }

    Document *documentForUri(const std::string &uri) {
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    const Document *documentForUri(const std::string &uri) const {
        const auto found = documents_.find(uri);
        return found == documents_.end() ? nullptr : &found->second;
    }

    void publishDiagnostics(const Document &document) {
        std::vector<std::string> diagnostics;
        diagnostics.reserve(document.diagnostics.size());
        for (const Diagnostic &diagnostic : document.diagnostics) diagnostics.push_back(diagnosticJson(document, diagnostic));
        const std::string params = jsonObject({{"uri", jsonQuoted(document.uri)}, {"diagnostics", jsonArray(diagnostics)}});
        sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")},
                                {"method", jsonQuoted("textDocument/publishDiagnostics")}, {"params", params}}));
    }

    void handleDidOpen(const Value &params) {
        const Value *textDocument = objectField(params, "textDocument");
        if (!textDocument) return;
        const std::string uri = stringField(*textDocument, "uri");
        if (uri.empty()) return;
        Document document;
        document.uri = uri;
        document.path = uriToPath(uri);
        document.text = stringField(*textDocument, "text");
        document.version = integerField(*textDocument, "version");
        document.fromClient = true;
        analyze(document);
        documents_[uri] = std::move(document);
        publishDiagnostics(documents_.at(uri));
    }

    static void applyTextChange(std::string &text, const Value &change) {
        const std::string replacement = stringField(change, "text");
        const Value *range = objectField(change, "range");
        if (!range) {
            text = replacement;
            return;
        }
        const Value *start = objectField(*range, "start");
        const Value *end = objectField(*range, "end");
        if (!start || !end) {
            text = replacement;
            return;
        }
        const std::size_t startOffset = offsetForPosition(text, LspPosition{integerField(*start, "line"),
                                                                               integerField(*start, "character")});
        const std::size_t endOffset = offsetForPosition(text, LspPosition{integerField(*end, "line"),
                                                                             integerField(*end, "character")});
        const std::size_t first = std::min(startOffset, endOffset);
        const std::size_t last = std::max(startOffset, endOffset);
        text.replace(first, last - first, replacement);
    }

    void handleDidChange(const Value &params) {
        const Value *textDocument = objectField(params, "textDocument");
        const Value *changes = objectField(params, "contentChanges");
        if (!textDocument || !changes || changes->kind != ValueKind::Array || !changes->array) return;
        const std::string uri = stringField(*textDocument, "uri");
        if (uri.empty()) return;
        Document *document = documentForUri(uri);
        if (!document) {
            Document created;
            created.uri = uri;
            created.path = uriToPath(uri);
            created.fromClient = true;
            documents_[uri] = std::move(created);
            document = &documents_.at(uri);
        }
        for (const Value &change : *changes->array) applyTextChange(document->text, change);
        document->version = integerField(*textDocument, "version", document->version);
        document->fromClient = true;
        analyze(*document);
        publishDiagnostics(*document);
    }

    void handleDidClose(const Value &params) {
        const Value *textDocument = objectField(params, "textDocument");
        if (!textDocument) return;
        const std::string uri = stringField(*textDocument, "uri");
        if (uri.empty()) return;
        const Document *document = documentForUri(uri);
        if (document) {
            const std::string empty = jsonObject({{"uri", jsonQuoted(uri)}, {"diagnostics", "[]"}});
            sendPayload(jsonObject({{"jsonrpc", jsonQuoted("2.0")},
                                    {"method", jsonQuoted("textDocument/publishDiagnostics")}, {"params", empty}}));
        }
        documents_.erase(uri);
    }

    void handleDidSave(const Value &params) {
        const Value *textDocument = objectField(params, "textDocument");
        if (!textDocument) return;
        const std::string uri = stringField(*textDocument, "uri");
        Document *document = documentForUri(uri);
        if (!document) return;
        if (objectField(params, "text") != nullptr) document->text = stringField(params, "text", document->text);
        analyze(*document);
        publishDiagnostics(*document);
    }

    void discoverWorkspace() {
        if (workspaceDiscovered_ || rootPath_.empty()) return;
        workspaceDiscovered_ = true;
        std::error_code error;
        const std::filesystem::path root(rootPath_);
        if (!std::filesystem::is_directory(root, error)) return;
        std::size_t loaded = 0;
        std::filesystem::recursive_directory_iterator iterator(
            root, std::filesystem::directory_options::skip_permission_denied, error);
        const std::filesystem::recursive_directory_iterator end;
        for (; iterator != end && !error && loaded < 512; iterator.increment(error)) {
            if (iterator->is_directory(error)) {
                const std::string name = iterator->path().filename().string();
                if (name == ".git" || name == ".next" || name == "node_modules" || name == "build" || name == "target") {
                    iterator.disable_recursion_pending();
                }
                continue;
            }
            if (iterator->path().extension() != ".kd") continue;
            const std::string path = iterator->path().string();
            if (std::filesystem::file_size(iterator->path(), error) > 2 * 1024 * 1024 || error) continue;
            const std::string uri = pathToUri(path);
            if (documents_.count(uri) != 0) continue;
            std::ifstream input(iterator->path(), std::ios::binary);
            if (!input) continue;
            std::ostringstream contents;
            contents << input.rdbuf();
            Document document;
            document.uri = uri;
            document.path = path;
            document.text = contents.str();
            analyze(document);
            documents_.emplace(uri, std::move(document));
            ++loaded;
        }
    }

    std::vector<SymbolEntry> allSymbols(bool includeLocals) {
        discoverWorkspace();
        std::vector<SymbolEntry> result;
        for (const auto &entry : documents_) {
            std::vector<SymbolEntry> symbols = documentSymbols(entry.second, includeLocals);
            result.insert(result.end(), symbols.begin(), symbols.end());
        }
        return result;
    }

    std::vector<SymbolEntry> definitionsFor(const std::string &name, const std::string &uri, std::size_t offset) {
        std::vector<SymbolEntry> candidates;
        for (const SymbolEntry &symbol : allSymbols(true)) {
            if (symbol.name == name || symbol.selectionText == name) candidates.push_back(symbol);
        }
        std::vector<SymbolEntry> localCandidates;
        const Document *currentDocumentPtr = documentForUri(uri);
        const std::size_t currentScope = currentDocumentPtr ? scopeAt(*currentDocumentPtr, offset)
                                                            : std::numeric_limits<std::size_t>::max();
        for (const SymbolEntry &candidate : candidates) {
            if (candidate.local && candidate.uri == uri && candidate.pos.offset <= offset) {
                if (currentDocumentPtr) {
                    const std::size_t candidateScope = scopeAt(*currentDocumentPtr, candidate.pos.offset);
                    if (candidateScope != currentScope) continue;
                }
                localCandidates.push_back(candidate);
            }
        }
        if (!localCandidates.empty()) {
            std::sort(localCandidates.begin(), localCandidates.end(), [](const SymbolEntry &left, const SymbolEntry &right) {
                return left.pos.offset > right.pos.offset;
            });
            return {localCandidates.front()};
        }

        // A top-level declaration in the current document is more specific
        // than same-named symbols discovered in another workspace file.
        std::vector<SymbolEntry> currentDocument;
        for (const SymbolEntry &candidate : candidates) {
            if (!candidate.local && candidate.uri == uri) currentDocument.push_back(candidate);
        }
        if (!currentDocument.empty()) return currentDocument;

        candidates.erase(std::remove_if(candidates.begin(), candidates.end(),
                                        [](const SymbolEntry &candidate) { return candidate.local; }),
                         candidates.end());
        return candidates;
    }

    std::string completion(const Value *params) {
        if (!params) return "null";
        const Value *textDocument = objectField(*params, "textDocument");
        const Value *position = objectField(*params, "position");
        if (!textDocument || !position) return jsonObject({{"isIncomplete", "false"}, {"items", "[]"}});
        const std::string uri = stringField(*textDocument, "uri");
        Document *document = documentForUri(uri);
        if (!document) return jsonObject({{"isIncomplete", "false"}, {"items", "[]"}});
        const std::size_t offset = offsetForPosition(document->text,
                                                      LspPosition{integerField(*position, "line"),
                                                                  integerField(*position, "character")});
        std::size_t prefixStart = offset;
        const std::string prefix = completionPrefix(document->text, offset, prefixStart);
        const std::size_t dot = prefix.rfind('.');
        const std::string namespacePrefix = dot == std::string::npos ? std::string{} : prefix.substr(0, dot + 1);
        const std::string filter = dot == std::string::npos ? prefix : prefix.substr(dot + 1);

        std::vector<std::string> names;
        for (const BuiltinSpec &spec : builtinSpecs()) names.emplace_back(spec.name);
        static const std::vector<std::string> namespaces{
            "std", "std.core", "std.math", "std.string", "std.list", "std.map", "std.pred", "std.json",
            "std.url", "std.html", "std.security", "std.http", "std.io", "std.opt"
        };
        names.insert(names.end(), namespaces.begin(), namespaces.end());
        static const std::vector<std::string> keywords{
            "fn", "condition", "rewrite", "route", "extern", "unsafe", "let", "check", "prove", "require",
            "assume", "if", "else", "while", "invariant", "for", "in", "match", "when", "return", "update",
            "move", "mut", "where", "requires", "ensures", "flow", "from", "as", "always", "never", "and", "or",
            "not", "is", "has", "true", "false", "null", "Int", "Float", "Bool", "String", "List", "Object"
        };
        names.insert(names.end(), keywords.begin(), keywords.end());
        for (const SymbolEntry &symbol : documentSymbols(*document, true)) names.push_back(symbol.name);
        std::sort(names.begin(), names.end());
        names.erase(std::unique(names.begin(), names.end()), names.end());

        std::vector<std::string> items;
        std::unordered_set<std::string> seen;
        for (const std::string &fullName : names) {
            std::string label = fullName;
            std::string insertText = fullName;
            std::string detail;
            int kind = 14;
            if (const BuiltinSpec *builtin = findBuiltin(fullName)) {
                detail = builtin->detail;
                kind = 3;
            } else if (fullName == "fn" || fullName == "condition" || fullName == "let" ||
                       fullName == "if" || fullName == "return") {
                detail = "Kond keyword";
            } else {
                detail = "Kond symbol";
                kind = 6;
            }

            if (!namespacePrefix.empty()) {
                if (fullName.rfind(namespacePrefix, 0) != 0) continue;
                const std::string remainder = fullName.substr(namespacePrefix.size());
                if (remainder.find('.') != std::string::npos) continue;
                label = remainder;
                insertText = remainder;
            } else if (fullName.find('.') != std::string::npos) {
                // Namespaced APIs are offered after typing the namespace, not
                // in the global completion list.
                continue;
            }
            if (label.rfind(filter, 0) != 0 || label.empty() || !seen.insert(label).second) continue;
            const std::size_t itemStart = namespacePrefix.empty() ? prefixStart : prefixStart + dot + 1;
            const LspRange replaceRange{positionForOffset(document->text, itemStart), positionForOffset(document->text, offset)};
            items.push_back(jsonObject({{"label", jsonQuoted(label)},
                                        {"kind", std::to_string(kind)},
                                        {"detail", jsonQuoted(detail)},
                                        {"insertText", jsonQuoted(insertText)},
                                        {"textEdit", jsonObject({{"range", rangeJson(replaceRange)},
                                                                   {"newText", jsonQuoted(insertText)}})}}));
        }
        return jsonObject({{"isIncomplete", "false"}, {"items", jsonArray(items)}});
    }

    std::string hover(const Value *params) {
        if (!params) return "null";
        const Value *textDocument = objectField(*params, "textDocument");
        const Value *position = objectField(*params, "position");
        if (!textDocument || !position) return "null";
        const std::string uri = stringField(*textDocument, "uri");
        const Document *document = documentForUri(uri);
        if (!document) return "null";
        const std::size_t offset = offsetForPosition(document->text,
                                                      LspPosition{integerField(*position, "line"),
                                                                  integerField(*position, "character")});
        const WordAtPosition word = wordAtPosition(document->text, offset);
        if (word.text.empty()) return "null";
        const std::vector<SymbolEntry> definitions = definitionsFor(word.text, uri, word.start);
        std::string contents;
        if (!definitions.empty()) {
            contents = definitionDescription(definitions.front());
        } else if (const BuiltinSpec *builtin = findBuiltin(word.text)) {
            contents = "```kond\n" + std::string(builtin->detail) + "\n```";
        } else {
            for (const BuiltinSpec &builtin : builtinSpecs()) {
                const std::string name = builtin.name;
                if (name.size() > word.text.size() + 1 && name.substr(name.size() - word.text.size()) == word.text) {
                    contents = "```kond\n" + name + "\n" + builtin.detail + "\n```";
                    break;
                }
            }
        }
        if (contents.empty()) return "null";
        const LspRange range{positionForOffset(document->text, word.start), positionForOffset(document->text, word.end)};
        return jsonObject({{"contents", jsonObject({{"kind", jsonQuoted("markdown")}, {"value", jsonQuoted(contents)}})},
                           {"range", rangeJson(range)}});
    }

    std::string definition(const Value *params) {
        if (!params) return "[]";
        const Value *textDocument = objectField(*params, "textDocument");
        const Value *position = objectField(*params, "position");
        if (!textDocument || !position) return "[]";
        const std::string uri = stringField(*textDocument, "uri");
        const Document *document = documentForUri(uri);
        if (!document) return "[]";
        const std::size_t offset = offsetForPosition(document->text,
                                                      LspPosition{integerField(*position, "line"),
                                                                  integerField(*position, "character")});
        const WordAtPosition word = wordAtPosition(document->text, offset);
        if (word.text.empty()) return "[]";
        std::vector<std::string> locations;
        for (const SymbolEntry &symbol : definitionsFor(word.text, uri, word.start)) {
            const Document *target = documentForUri(symbol.uri);
            if (target) locations.push_back(locationJson(*target, symbol));
        }
        return jsonArray(locations);
    }

    std::string references(const Value *params) {
        if (!params) return "[]";
        discoverWorkspace();
        const Value *textDocument = objectField(*params, "textDocument");
        const Value *position = objectField(*params, "position");
        if (!textDocument || !position) return "[]";
        const std::string uri = stringField(*textDocument, "uri");
        const Document *document = documentForUri(uri);
        if (!document) return "[]";
        const std::size_t offset = offsetForPosition(document->text,
                                                      LspPosition{integerField(*position, "line"),
                                                                  integerField(*position, "character")});
        const WordAtPosition word = wordAtPosition(document->text, offset);
        if (word.text.empty()) return "[]";
        const Value *context = objectField(*params, "context");
        const bool includeDeclaration = context && booleanField(*context, "includeDeclaration");
        const std::vector<SymbolEntry> definitions = definitionsFor(word.text, uri, word.start);
        const bool localReference = !definitions.empty() && definitions.front().local;
        const bool documentReference = !definitions.empty() && !definitions.front().local &&
                                       definitions.front().uri == uri;
        std::unordered_set<std::string> definitionOffsets;
        for (const SymbolEntry &symbol : definitions) {
            definitionOffsets.insert(symbol.uri + ":" + std::to_string(symbol.pos.offset));
        }

        std::vector<std::pair<std::string, std::size_t>> ordered;
        for (const auto &entry : documents_) {
            const Document &candidate = entry.second;
            if ((localReference || documentReference) && candidate.uri != uri) continue;
            for (const Token &token : candidate.tokens) {
                if (token.kind != TokenKind::Identifier || token.text != word.text) continue;
                const std::string key = candidate.uri + ":" + std::to_string(token.pos.offset);
                if (!includeDeclaration && definitionOffsets.count(key) != 0) continue;
                ordered.emplace_back(candidate.uri, token.pos.offset);
            }
        }
        std::sort(ordered.begin(), ordered.end());
        std::vector<std::string> locations;
        for (const auto &entry : ordered) {
            const Document *candidate = documentForUri(entry.first);
            if (!candidate) continue;
            const LspRange range = sourceRange(*candidate, SourcePos{candidate->path, entry.second, 1, 1}, word.text.size());
            locations.push_back(jsonObject({{"uri", jsonQuoted(candidate->uri)}, {"range", rangeJson(range)}}));
        }
        return jsonArray(locations);
    }

    std::string documentSymbolsResult(const Value *params) {
        if (!params) return "[]";
        const Value *textDocument = objectField(*params, "textDocument");
        if (!textDocument) return "[]";
        const Document *document = documentForUri(stringField(*textDocument, "uri"));
        if (!document) return "[]";
        std::vector<SymbolEntry> symbols = documentSymbols(*document, false);
        std::sort(symbols.begin(), symbols.end(), [](const SymbolEntry &left, const SymbolEntry &right) {
            return left.pos.offset < right.pos.offset;
        });
        std::vector<std::string> result;
        for (const SymbolEntry &symbol : symbols) result.push_back(documentSymbolJson(*document, symbol));
        return jsonArray(result);
    }

    std::string workspaceSymbols(const Value *params) {
        discoverWorkspace();
        const std::string query = params ? stringField(*params, "query") : std::string{};
        std::vector<SymbolEntry> symbols = allSymbols(false);
        std::sort(symbols.begin(), symbols.end(), [](const SymbolEntry &left, const SymbolEntry &right) {
            if (left.name != right.name) return left.name < right.name;
            return left.uri < right.uri;
        });
        std::vector<std::string> result;
        for (const SymbolEntry &symbol : symbols) {
            if (!query.empty() && symbol.name.find(query) == std::string::npos) continue;
            const Document *document = documentForUri(symbol.uri);
            if (document) result.push_back(symbolInformationJson(*document, symbol));
        }
        return jsonArray(result);
    }
};

} // namespace

int runLspServer() {
    return LspServer().run();
}

} // namespace kond
