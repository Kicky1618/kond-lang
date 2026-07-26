#include "kond_interpreter_api.hpp"
#include "kond_ffi.hpp"

namespace kond {

enum class Truth { False, True, Unknown };
enum class ProofState { Refuted, Proven, Unknown };
using RewriteProofKind = OptimizationProofKind;

struct RewriteCertificate {
    RewriteProofKind kind = RewriteProofKind::ExactEq;
    std::string rule;
    std::string semantics;
    SourcePos pos;
};

static std::string numberString(double value) {
    std::ostringstream output;
    output << std::setprecision(15) << value;
    return output.str();
}

static bool checkedAdd(std::int64_t left, std::int64_t right, std::int64_t &result) {
    if ((right > 0 && left > std::numeric_limits<std::int64_t>::max() - right) ||
        (right < 0 && left < std::numeric_limits<std::int64_t>::min() - right)) {
        return true;
    }
    result = left + right;
    return false;
}

static bool checkedSub(std::int64_t left, std::int64_t right, std::int64_t &result) {
    if ((right > 0 && left < std::numeric_limits<std::int64_t>::min() + right) ||
        (right < 0 && left > std::numeric_limits<std::int64_t>::max() + right)) {
        return true;
    }
    result = left - right;
    return false;
}

static bool checkedMul(std::int64_t left, std::int64_t right, std::int64_t &result) {
    constexpr std::int64_t minimum = std::numeric_limits<std::int64_t>::min();
    constexpr std::int64_t maximum = std::numeric_limits<std::int64_t>::max();
    if (left == 0 || right == 0) {
        result = 0;
        return false;
    }
    if ((left == -1 && right == minimum) || (right == -1 && left == minimum)) return true;
    if (left > 0) {
        if (right > 0 ? left > maximum / right : right < minimum / left) return true;
    } else if (right > 0 ? left < minimum / right : left < maximum / right) {
        return true;
    }
    result = left * right;
    return false;
}

static std::string valueToString(const Value &value);
static Value dereference(Value value);

static std::string jsonEscape(std::string_view text) {
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
                output << "\\u00" << std::hex << std::uppercase << std::setw(2)
                       << std::setfill('0') << static_cast<int>(ch) << std::dec << std::setfill(' ');
            } else {
                output << static_cast<char>(ch);
            }
            break;
        }
    }
    return output.str();
}

static std::string jsonSerialize(const Value &rawValue) {
    const Value value = dereference(rawValue);
    switch (value.kind) {
    case ValueKind::Null: return "null";
    case ValueKind::Integer: return std::to_string(value.integer);
    case ValueKind::Float: return std::isfinite(value.floating) ? numberString(value.floating) : "null";
    case ValueKind::Boolean: return value.boolean ? "true" : "false";
    case ValueKind::String: return "\"" + jsonEscape(valueToString(value)) + "\"";
    case ValueKind::Array: {
        std::string result = "[";
        if (value.array) {
            for (std::size_t i = 0; i < value.array->size(); ++i) {
                if (i != 0) result += ',';
                result += jsonSerialize((*value.array)[i]);
            }
        }
        return result + "]";
    }
    case ValueKind::Object: {
        std::string result = "{";
        bool first = true;
        if (value.object) {
            for (const auto &field : *value.object) {
                if (!first) result += ',';
                first = false;
                result += "\"" + jsonEscape(field.first) + "\":" + jsonSerialize(field.second);
            }
        }
        return result + "}";
    }
    case ValueKind::Condition: return value.condition && value.condition->holds ? "true" : "false";
    case ValueKind::HttpResponse:
        return value.response ? jsonSerialize(value.response->body) : "null";
    default: return "null";
    }
}

static Value cloneDeep(const Value &value) {
    switch (value.kind) {
    case ValueKind::Array: {
        std::vector<Value> elements;
        if (value.array) {
            elements.reserve(value.array->size());
            for (const Value &element : *value.array) elements.push_back(cloneDeep(element));
        }
        Value result = Value::arrayValue(std::move(elements));
        result.flow = value.flow;
        result.safety = value.safety;
        return result;
    }
    case ValueKind::Object: {
        std::map<std::string, Value> fields;
        if (value.object) {
            for (const auto &entry : *value.object) fields.emplace(entry.first, cloneDeep(entry.second));
        }
        Value result = Value::objectValue(std::move(fields));
        result.flow = value.flow;
        result.safety = value.safety;
        return result;
    }
    default:
        return value;
    }
}

static Value dereference(Value value) {
    while (value.kind == ValueKind::Reference && value.reference) {
        if (value.reference->moved) {
            fail("E2101", value.reference->pos, "移動済みの値を借用経由で使用しています");
        }
        value = value.reference->value;
    }
    return value;
}

static bool valuesEqual(const Value &leftValue, const Value &rightValue) {
    const Value left = dereference(leftValue);
    const Value right = dereference(rightValue);
    if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
        return left.integer == right.integer;
    }
    if (left.isNumber() && right.isNumber()) {
        const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
        const double b = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
        return a == b;
    }
    if (left.kind != right.kind) return false;
    switch (left.kind) {
    case ValueKind::Null: return true;
    case ValueKind::Integer: return left.integer == right.integer;
    case ValueKind::Float: return left.floating == right.floating;
    case ValueKind::Boolean: return left.boolean == right.boolean;
    case ValueKind::String: return left.string == right.string;
    case ValueKind::Array:
        if (!left.array || !right.array || left.array->size() != right.array->size()) return left.array == right.array;
        for (std::size_t i = 0; i < left.array->size(); ++i) {
            if (!valuesEqual((*left.array)[i], (*right.array)[i])) return false;
        }
        return true;
    case ValueKind::Object:
        if (!left.object || !right.object || left.object->size() != right.object->size()) return left.object == right.object;
        for (const auto &entry : *left.object) {
            auto found = right.object->find(entry.first);
            if (found == right.object->end() || !valuesEqual(entry.second, found->second)) return false;
        }
        return true;
    default:
        return &left == &right || left.name == right.name;
    }
}

static std::string valueToString(const Value &rawValue) {
    const Value value = dereference(rawValue);
    switch (value.kind) {
    case ValueKind::Null: return "null";
    case ValueKind::Integer: return std::to_string(value.integer);
    case ValueKind::Float: return numberString(value.floating);
    case ValueKind::Boolean: return value.boolean ? "true" : "false";
    case ValueKind::String: {
        const std::size_t separator = value.string.find('\x1f');
        if (separator == std::string::npos) return value.string;
        const std::string tag = value.string.substr(0, separator);
        if (tag != "html" && tag != "json" && tag != "sql" && tag != "text") return value.string;
        return value.string.substr(separator + 1);
    }
    case ValueKind::Array: {
        std::ostringstream output;
        output << '[';
        if (value.array) {
            for (std::size_t i = 0; i < value.array->size(); ++i) {
                if (i != 0) output << ", ";
                output << valueToString((*value.array)[i]);
            }
        }
        output << ']';
        return output.str();
    }
    case ValueKind::Object: {
        std::ostringstream output;
        output << '{';
        bool first = true;
        if (value.object) {
            for (const auto &entry : *value.object) {
                if (!first) output << ", ";
                first = false;
                output << entry.first << ": " << valueToString(entry.second);
            }
        }
        output << '}';
        return output.str();
    }
    case ValueKind::Function: return "<fn " + value.name + ">";
    case ValueKind::Builtin: return "<builtin " + value.name + ">";
    case ValueKind::Method: return "<method " + value.name + ">";
    case ValueKind::Lambda: return "<lambda>";
    case ValueKind::Condition:
        return value.condition && value.condition->holds ? "true" : "false";
    case ValueKind::Reference: return "<borrow>";
    case ValueKind::HttpRequest: return "<http request>";
    case ValueKind::HttpResponse: return value.response ? valueToString(value.response->body) : "null";
    }
    return "null";
}

class Interpreter {
public:
    Interpreter(const Program &program, Mode mode, std::string file, bool strictIfc = false,
                bool explainOptimizations = false, bool traceOwnership = false)
        : program_(program),
          mode_(mode),
          file_(std::move(file)),
          ownershipLog_(std::make_shared<OwnershipLog>()),
          env_(ownershipLog_),
          strictIfc_(strictIfc),
          explainOptimizations_(explainOptimizations),
          traceOwnership_(traceOwnership) {}

    void run(const std::string &entry) {
        OwnershipChecker ownershipChecker(program_);
        ownershipChecker.check();
        const std::string selectedEntry = entry.empty() && program_.functions.count("main") != 0 ? "main" : entry;
        if (!selectedEntry.empty()) {
            auto found = program_.functions.find(selectedEntry);
            if (found == program_.functions.end()) {
                fail("E2001", SourcePos{file_, 0, 1, 1}, "エントリ関数が見つかりません: " + selectedEntry);
            }
            callFunction(found->second, {}, SourcePos{file_, 0, 1, 1}, {});
            finishDiagnostics();
            return;
        }
        for (const StatementPtr &statement : program_.topLevel) {
            execute(statement);
        }
        finishDiagnostics();
    }

    void checkOnly() const {
        validateProgram();
        std::cout << "kond: syntax, declaration, and ownership check passed\n";
    }

    void validateProgram() const {
        OwnershipChecker checker(program_);
        checker.check();
    }

    HttpResponse handleHttpRequest(const HttpRequestData &request) {
        const FunctionDef *selected = nullptr;
        bool pathMatched = false;
        std::vector<std::string> allowedMethods;
        for (const auto &entry : program_.functions) {
            const FunctionDef &function = entry.second;
            if (!function.route || function.routePath != request.path) continue;
            pathMatched = true;
            allowedMethods.push_back(function.routeMethod);
            if (function.routeMethod == request.method) selected = &function;
        }
        if (!selected && request.method == "HEAD") {
            for (const auto &entry : program_.functions) {
                const FunctionDef &function = entry.second;
                if (function.route && function.routePath == request.path && function.routeMethod == "GET") {
                    selected = &function;
                    break;
                }
            }
        }
        if (!selected) {
            if (pathMatched) {
                HttpResponse response = plainHttpResponse(405, "Method Not Allowed\n");
                std::sort(allowedMethods.begin(), allowedMethods.end());
                allowedMethods.erase(std::unique(allowedMethods.begin(), allowedMethods.end()), allowedMethods.end());
                std::string allow;
                for (std::size_t i = 0; i < allowedMethods.size(); ++i) {
                    if (i != 0) allow += ", ";
                    allow += allowedMethods[i];
                }
                response.headers["Allow"] = std::move(allow);
                return response;
            }
            return plainHttpResponse(404, "Not Found\n");
        }
        if (selected->params.size() != 1) {
            return plainHttpResponse(500, "Internal Server Error\n");
        }

        Value requestValue;
        requestValue.kind = ValueKind::HttpRequest;
        requestValue.flow = FlowUntrusted;
        requestValue.request = std::make_shared<HttpRequestData>(request);
        try {
            Value result = callFunction(*selected, {std::move(requestValue)},
                                        SourcePos{file_, 0, 1, 1}, {});
            return responseFromValue(result);
        } catch (const KondError &error) {
            if (isHttpClientError(error.code)) return plainHttpResponse(400, "Bad Request\n");
            return plainHttpResponse(500, "Internal Server Error\n");
        } catch (const std::exception &) {
            return plainHttpResponse(500, "Internal Server Error\n");
        }
    }

private:
    static HttpResponse plainHttpResponse(int status, std::string body) {
        HttpResponse response;
        response.status = status;
        response.body = std::move(body);
        response.headers["Content-Type"] = "text/plain; charset=utf-8";
        return response;
    }

    static bool isHttpClientError(const std::string &code) {
        return code == "E4001" || code == "E1101" || code == "E1201" || code == "E1202" ||
               code == "E1206" || code == "E1302" || code == "E1303" || code == "E1405" ||
               code == "E1701" || code == "E1702";
    }

    static std::string taggedStringTag(const Value &rawValue) {
        const Value value = dereference(rawValue);
        if (value.kind != ValueKind::String) return {};
        const std::size_t separator = value.string.find('\x1f');
        if (separator == std::string::npos) return {};
        const std::string tag = value.string.substr(0, separator);
        if (tag != "html" && tag != "json" && tag != "sql" && tag != "text") return {};
        return tag;
    }

    static std::string responseContentType(const Value &rawValue) {
        const Value value = dereference(rawValue);
        const std::string tag = taggedStringTag(value);
        if (tag == "html") return "text/html; charset=utf-8";
        if (tag == "json" || value.kind == ValueKind::Array || value.kind == ValueKind::Object) {
            return "application/json; charset=utf-8";
        }
        return "text/plain; charset=utf-8";
    }

    static bool hasResponseHeader(const std::map<std::string, std::string> &headers, const std::string &name) {
        const std::string wanted = asciiLower(name);
        for (const auto &header : headers) {
            if (asciiLower(header.first) == wanted) return true;
        }
        return false;
    }

    static HttpResponse responseFromValue(const Value &rawValue) {
        const Value value = dereference(rawValue);
        HttpResponse response;
        const Value *bodyValue = &value;
        if (value.kind == ValueKind::HttpResponse && value.response) {
            response.status = value.response->status;
            for (const auto &header : value.response->headers) {
                response.headers[header.first] = valueToString(header.second);
            }
            bodyValue = &value.response->body;
        }
        const Value body = dereference(*bodyValue);
        if (body.kind == ValueKind::Null) {
            response.body.clear();
            if (response.status == 200) response.status = 204;
        } else if (body.kind == ValueKind::String) {
            response.body = valueToString(body);
        } else if (body.kind == ValueKind::Object || body.kind == ValueKind::Array) {
            response.body = jsonSerialize(body);
        } else {
            response.body = valueToString(body);
        }
        if (response.status == 204 || response.status == 304) response.body.clear();
        if (!hasResponseHeader(response.headers, "Content-Type")) {
            response.headers["Content-Type"] = responseContentType(body);
        }
        return response;
    }

    struct ResolvedPath {
        std::shared_ptr<Binding> root;
        std::vector<std::pair<bool, std::string>> path;
        std::vector<std::int64_t> indexes;
        bool throughUniqueBorrow = false;
    };

    const Program &program_;
    Mode mode_;
    std::string file_;
    std::shared_ptr<OwnershipLog> ownershipLog_;
    Environment env_;
    FfiRuntime ffi_;
    bool strictIfc_ = false;
    bool explainOptimizations_ = false;
    bool traceOwnership_ = false;
    bool staticControl_ = true;
    std::uint32_t pcFlow_ = FlowPublic;
    std::vector<RewriteCertificate> rewriteCertificates_;
    std::size_t rewriteDepth_ = 0;

    Value applyPcFlow(Value value) const {
        if (strictIfc_) value.flow |= pcFlow_;
        return value;
    }

    void recordRewrite(RewriteProofKind kind, const std::string &rule,
                      const std::string &semantics, const SourcePos &pos) {
        rewriteCertificates_.push_back(RewriteCertificate{kind, rule, semantics, pos});
    }

    void recordExactRewrite(const std::string &rule, const SourcePos &pos) {
        recordRewrite(RewriteProofKind::ExactEq, rule, "Kond.Int64", pos);
    }

    void explainCertificates() const {
        if (!explainOptimizations_) return;
        for (const RewriteCertificate &certificate : rewriteCertificates_) {
            const char *kind = certificate.kind == RewriteProofKind::ExactEq ? "ExactEq" :
                               certificate.kind == RewriteProofKind::RealEq ? "RealEq" :
                               certificate.kind == RewriteProofKind::ApproxEq ? "ApproxEq" : "HeuristicImprovement";
            std::cerr << "optimization[" << kind << "]: " << certificate.rule
                      << " (" << certificate.semantics << ") at " << certificate.pos.file << ':'
                      << certificate.pos.line << ':' << certificate.pos.column << '\n';
        }
    }

    void explainOwnership() const {
        if (!traceOwnership_ || !ownershipLog_) return;
        for (const OwnershipEvent &event : ownershipLog_->events) {
            std::cerr << "ownership: " << event.binding << ": " << event.transition
                      << " at " << event.pos.file << ':' << event.pos.line << ':' << event.pos.column << '\n';
        }
    }

    void finishDiagnostics() const {
        explainCertificates();
        explainOwnership();
    }

    static std::string flowLabelName(std::uint32_t flow) {
        if (flow == FlowPublic) return "Public";
        std::string result;
        const auto append = [&](const std::string &name, std::string &target) {
            if (!target.empty()) target += '|';
            target += name;
        };
        if ((flow & FlowUntrusted) != 0) append("Untrusted", result);
        if ((flow & FlowSecret) != 0) append("Secret", result);
        if ((flow & FlowPersonal) != 0) append("Personal", result);
        if ((flow & FlowOpaque) != 0) append("Opaque", result);
        return result.empty() ? "Public" : result;
    }

    static std::string operatorName(const ExprPtr &expression) {
        if (!expression) return "<expr>";
        switch (expression->kind) {
        case ExprKind::Variable: return expression->text;
        case ExprKind::Member: return operatorName(expression->object) + "." + expression->text;
        default: return "<expr>";
        }
    }

    std::shared_ptr<Binding> bindingForExpression(const ExprPtr &expression) {
        if (!expression) return nullptr;
        if (expression->kind == ExprKind::Variable) return env_.find(expression->text);
        if (expression->kind == ExprKind::Member) return bindingForExpression(expression->object);
        if (expression->kind == ExprKind::Index) return bindingForExpression(expression->object);
        return nullptr;
    }

    static bool expressionIsPureLiteral(const ExprPtr &expression) {
        if (!expression) return false;
        switch (expression->kind) {
        case ExprKind::Literal: return true;
        case ExprKind::Array:
            return std::all_of(expression->items.begin(), expression->items.end(), expressionIsPureLiteral);
        case ExprKind::Object:
            return std::all_of(expression->fields.begin(), expression->fields.end(),
                               [](const auto &field) { return expressionIsPureLiteral(field.second); });
        case ExprKind::Unary: return expressionIsPureLiteral(expression->left);
        case ExprKind::Binary:
            return expressionIsPureLiteral(expression->left) && expressionIsPureLiteral(expression->right);
        default: return false;
        }
    }

    static ExprPtr cloneExprWithSubstitution(const ExprPtr &expression,
                                             const std::unordered_map<std::string, ExprPtr> &substitutions) {
        if (!expression) return nullptr;
        if (expression->kind == ExprKind::Variable) {
            auto found = substitutions.find(expression->text);
            if (found != substitutions.end()) return cloneExprWithSubstitution(found->second, {});
        }
        auto copy = Expr::make(expression->kind, expression->pos);
        copy->literalKind = expression->literalKind;
        copy->text = expression->text;
        copy->op = expression->op;
        copy->mutableBorrow = expression->mutableBorrow;
        copy->lambdaParam = expression->lambdaParam;
        copy->condition = cloneConditionWithSubstitution(expression->condition, substitutions);
        copy->lambdaBody = cloneConditionWithSubstitution(expression->lambdaBody, substitutions);
        copy->left = cloneExprWithSubstitution(expression->left, substitutions);
        copy->right = cloneExprWithSubstitution(expression->right, substitutions);
        copy->object = cloneExprWithSubstitution(expression->object, substitutions);
        copy->index = cloneExprWithSubstitution(expression->index, substitutions);
        copy->callee = cloneExprWithSubstitution(expression->callee, substitutions);
        for (const ExprPtr &item : expression->items) copy->items.push_back(cloneExprWithSubstitution(item, substitutions));
        for (const auto &field : expression->fields) copy->fields.emplace_back(field.first, cloneExprWithSubstitution(field.second, substitutions));
        return copy;
    }

    static ConditionPtr cloneConditionWithSubstitution(const ConditionPtr &condition,
                                                        const std::unordered_map<std::string, ExprPtr> &substitutions) {
        if (!condition) return nullptr;
        auto copy = Condition::make(condition->kind, condition->pos);
        copy->op = condition->op;
        copy->predicate = condition->predicate;
        copy->left = cloneExprWithSubstitution(condition->left, substitutions);
        copy->right = cloneExprWithSubstitution(condition->right, substitutions);
        copy->value = cloneExprWithSubstitution(condition->value, substitutions);
        for (const ConditionPtr &item : condition->items) copy->items.push_back(cloneConditionWithSubstitution(item, substitutions));
        for (const ExprPtr &arg : condition->args) copy->args.push_back(cloneExprWithSubstitution(arg, substitutions));
        copy->fields = condition->fields;
        for (const ShapeFieldSchema &field : condition->shapeFields) {
            ShapeFieldSchema cloned = field;
            cloned.condition = cloneConditionWithSubstitution(field.condition, substitutions);
            copy->shapeFields.push_back(std::move(cloned));
        }
        return copy;
    }

    static void collectVariables(const ExprPtr &expression, std::vector<std::string> &names) {
        if (!expression) return;
        if (expression->kind == ExprKind::Variable) {
            names.push_back(expression->text);
            return;
        }
        collectVariables(expression->left, names);
        collectVariables(expression->right, names);
        collectVariables(expression->object, names);
        collectVariables(expression->index, names);
        collectVariables(expression->callee, names);
        for (const ExprPtr &item : expression->items) collectVariables(item, names);
        for (const auto &field : expression->fields) collectVariables(field.second, names);
        if (expression->lambdaBody) collectVariables(expression->lambdaBody, names);
        if (expression->condition) collectVariables(expression->condition, names);
    }

    static void collectVariables(const ConditionPtr &condition, std::vector<std::string> &names) {
        if (!condition) return;
        collectVariables(condition->left, names);
        collectVariables(condition->right, names);
        collectVariables(condition->value, names);
        for (const ConditionPtr &item : condition->items) collectVariables(item, names);
        for (const ExprPtr &arg : condition->args) collectVariables(arg, names);
        for (const ShapeFieldSchema &field : condition->shapeFields) collectVariables(field.condition, names);
    }

    std::uint32_t conditionFlow(const ConditionPtr &condition) {
        std::vector<std::string> names;
        collectVariables(condition, names);
        std::unordered_set<std::string> seen;
        std::uint32_t result = FlowPublic;
        for (const std::string &name : names) {
            if (!seen.insert(name).second) continue;
            if (auto binding = env_.find(name)) result |= binding->value.flow;
        }
        return result;
    }

    std::uint32_t expressionFlow(const ExprPtr &expression) {
        std::vector<std::string> names;
        collectVariables(expression, names);
        std::unordered_set<std::string> seen;
        std::uint32_t result = FlowPublic;
        for (const std::string &name : names) {
            if (!seen.insert(name).second) continue;
            if (auto binding = env_.find(name)) result |= binding->value.flow;
        }
        return result;
    }

    std::shared_ptr<RuntimeCondition> captureCondition(const ConditionPtr &condition) {
        auto result = std::make_shared<RuntimeCondition>();
        result->condition = condition;
        std::vector<std::string> names;
        collectVariables(condition, names);
        std::unordered_set<std::string> seen;
        std::unordered_set<const Binding *> seenBindings;
        for (const std::string &name : names) {
            if (!seen.insert(name).second) continue;
            if (auto binding = env_.find(name)) {
                if (!seenBindings.insert(binding.get()).second) continue;
                result->captures.push_back(RuntimeCondition::Capture{name, binding, binding->version});
                result->flow |= binding->value.flow;
            }
        }
        // Composition keeps the complete value-version dependency set, not
        // only the intermediate Condition-valued bindings.
        for (std::size_t i = 0; i < result->captures.size(); ++i) {
            const auto &capture = result->captures[i];
            if (!capture.binding || capture.binding->value.kind != ValueKind::Condition ||
                !capture.binding->value.condition) {
                continue;
            }
            const std::shared_ptr<RuntimeCondition> nestedCondition = capture.binding->value.condition;
            for (const auto &nested : nestedCondition->captures) {
                if (!nested.binding || !seenBindings.insert(nested.binding.get()).second) continue;
                result->captures.push_back(nested);
                result->flow |= nested.binding->value.flow;
            }
        }
        const ProofState state = proofState(condition);
        result->evidence = state == ProofState::Unknown ? EvidenceKind::RuntimeProof : EvidenceKind::StaticProof;
        if (state == ProofState::Proven) {
            result->holds = true;
        } else if (state == ProofState::Refuted) {
            result->holds = false;
        } else {
            const Truth decision = runtimeCondition(condition);
            if (decision == Truth::Unknown) {
                fail("E1101", condition->pos, "Condition<P> を実行時に決定できません");
            }
            result->holds = decision == Truth::True;
        }
        return result;
    }

    void validateCapturedCondition(const RuntimeCondition &condition, const SourcePos &pos) {
        for (const auto &capture : condition.captures) {
            if (!capture.binding || capture.binding->version != capture.version) {
                fail("E1302", pos, "値の更新によって以前の条件証拠が無効化されています");
            }
        }
    }

    std::string expressionKey(const ExprPtr &expression) {
        if (!expression) return "<null-expr>";
        switch (expression->kind) {
        case ExprKind::Literal:
            return "lit:" + expression->text;
        case ExprKind::Variable: {
            auto binding = env_.find(expression->text);
            return expression->text + "@" + (binding ? std::to_string(binding->version) : "?");
        }
        case ExprKind::Member:
            return expressionKey(expression->object) + "." + expression->text;
        case ExprKind::Index:
            return expressionKey(expression->object) + "[" + expressionKey(expression->index) + "]";
        case ExprKind::Unary:
            return expression->op + expressionKey(expression->left);
        case ExprKind::Binary:
            return "(" + expressionKey(expression->left) + expression->op + expressionKey(expression->right) + ")";
        case ExprKind::Call: {
            std::string key = expressionKey(expression->callee) + "(";
            for (const ExprPtr &item : expression->items) key += expressionKey(item) + ",";
            return key + ")";
        }
        case ExprKind::ConditionValue: return "condition";
        default: return "<complex-expr>";
        }
    }

    std::string conditionKey(const ConditionPtr &condition) {
        if (!condition) return "<null-condition>";
        switch (condition->kind) {
        case ConditionKind::Always: return "always";
        case ConditionKind::Never: return "never";
        case ConditionKind::Not: return "not(" + conditionKey(condition->items.front()) + ")";
        case ConditionKind::And: {
            std::string key = "and(";
            for (const ConditionPtr &item : condition->items) key += conditionKey(item) + ",";
            return key + ")";
        }
        case ConditionKind::Or: {
            std::string key = "or(";
            for (const ConditionPtr &item : condition->items) key += conditionKey(item) + ",";
            return key + ")";
        }
        case ConditionKind::Relation:
            return expressionKey(condition->left) + condition->op + expressionKey(condition->right);
        case ConditionKind::Is:
            return expressionKey(condition->left) + " is " + condition->predicate;
        case ConditionKind::Has: {
            std::string key = expressionKey(condition->left) + " has ";
            if (!condition->fields.empty()) {
                for (const ShapeFieldSchema &field : condition->shapeFields) {
                    key += field.name;
                    if (!field.predicate.empty()) key += ":" + field.predicate;
                    if (field.condition) key += " where " + conditionKey(field.condition);
                    key += ",";
                }
            } else {
                key += expressionKey(condition->right);
            }
            return key;
        }
        case ConditionKind::Call: {
            std::string key = condition->predicate + "(";
            for (const ExprPtr &arg : condition->args) key += expressionKey(arg) + ",";
            return key + ")";
        }
        case ConditionKind::ValueRef: {
            if (condition->value && condition->value->kind == ExprKind::ConditionValue && condition->value->condition) {
                return conditionKey(condition->value->condition);
            }
            Value value = evalExpr(condition->value);
            if (value.kind == ValueKind::Condition && value.condition) return conditionKey(value.condition->condition);
            return "value(" + expressionKey(condition->value) + ")";
        }
        }
        return "<condition>";
    }

    ConditionPtr conditionFromValue(const ExprPtr &expression) {
        if (expression && expression->kind == ExprKind::ConditionValue) return expression->condition;
        return nullptr;
    }

    static std::string negatedKey(const std::string &key) { return "not{" + key + "}"; }

    void addFact(const ConditionPtr &condition) {
        if (!condition) return;
        switch (condition->kind) {
        case ConditionKind::And:
            for (const ConditionPtr &item : condition->items) addFact(item);
            break;
        case ConditionKind::Not:
            env_.addFact(negatedKey(conditionKey(condition->items.front())));
            break;
        default:
            env_.addFact(conditionKey(condition));
            break;
        }
    }

    ProofState proofState(const ConditionPtr &condition) {
        if (!condition) return ProofState::Unknown;
        const std::string key = conditionKey(condition);
        if (env_.hasFact(key)) return ProofState::Proven;
        if (env_.hasFact(negatedKey(key))) return ProofState::Refuted;
        switch (condition->kind) {
        case ConditionKind::Always: return ProofState::Proven;
        case ConditionKind::Never: return ProofState::Refuted;
        case ConditionKind::Not: {
            const ProofState inner = proofState(condition->items.front());
            if (inner == ProofState::Proven) return ProofState::Refuted;
            if (inner == ProofState::Refuted) return ProofState::Proven;
            return ProofState::Unknown;
        }
        case ConditionKind::And: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                const ProofState itemState = proofState(item);
                if (itemState == ProofState::Refuted) return ProofState::Refuted;
                if (itemState == ProofState::Unknown) unknown = true;
            }
            return unknown ? ProofState::Unknown : ProofState::Proven;
        }
        case ConditionKind::Or: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                const ProofState itemState = proofState(item);
                if (itemState == ProofState::Proven) return ProofState::Proven;
                if (itemState == ProofState::Unknown) unknown = true;
            }
            return unknown ? ProofState::Unknown : ProofState::Refuted;
        }
        case ConditionKind::ValueRef: {
            Value value = evalExpr(condition->value);
            if (value.kind == ValueKind::Condition && value.condition) {
                validateCapturedCondition(*value.condition, condition->pos);
                if (value.condition->evidence == EvidenceKind::StaticProof) {
                    return value.condition->holds ? ProofState::Proven : ProofState::Refuted;
                }
            }
            return ProofState::Unknown;
        }
        default:
            break;
        }
        if (auto staticValue = staticCondition(condition)) {
            return *staticValue ? ProofState::Proven : ProofState::Refuted;
        }
        return ProofState::Unknown;
    }

    std::optional<bool> staticCondition(const ConditionPtr &condition) {
        if (!condition) return std::nullopt;
        switch (condition->kind) {
        case ConditionKind::Always: return true;
        case ConditionKind::Never: return false;
        case ConditionKind::Not: {
            auto value = staticCondition(condition->items.front());
            return value ? std::optional<bool>{!*value} : std::nullopt;
        }
        case ConditionKind::And: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                auto value = staticCondition(item);
                if (value && !*value) return false;
                if (!value) unknown = true;
            }
            return unknown ? std::nullopt : std::optional<bool>{true};
        }
        case ConditionKind::Or: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                auto value = staticCondition(item);
                if (value && *value) return true;
                if (!value) unknown = true;
            }
            return unknown ? std::nullopt : std::optional<bool>{false};
        }
        case ConditionKind::ValueRef: {
            Value value = evalExpr(condition->value);
            if (value.kind == ValueKind::Condition && value.condition) {
                validateCapturedCondition(*value.condition, condition->pos);
                if (value.condition->evidence == EvidenceKind::StaticProof) return value.condition->holds;
            }
            if (auto staticValue = staticExpr(condition->value)) {
                if (staticValue->kind == ValueKind::Boolean) return staticValue->boolean;
            }
            return std::nullopt;
        }
        case ConditionKind::Is: {
            auto value = staticExpr(condition->left);
            if (!value) return std::nullopt;
            return predicateHolds(*value, condition->predicate);
        }
        case ConditionKind::Has: {
            auto value = staticExpr(condition->left);
            if (!value) return std::nullopt;
            return hasFields(*value, condition);
        }
        case ConditionKind::Relation: {
            auto left = staticExpr(condition->left);
            auto right = staticExpr(condition->right);
            if (!left || !right) return std::nullopt;
            return compareValues(*left, *right, condition->op);
        }
        case ConditionKind::Call: {
            bool allStatic = true;
            for (const ExprPtr &arg : condition->args) {
                if (!staticExpr(arg)) allStatic = false;
            }
            if (!allStatic) return std::nullopt;
            return runtimeCondition(condition) == Truth::True;
        }
        }
        return std::nullopt;
    }

    static std::string qualifiedExpressionName(const ExprPtr &expression) {
        if (!expression) return {};
        if (expression->kind == ExprKind::Variable) return expression->text;
        if (expression->kind != ExprKind::Member) return {};
        const std::string prefix = qualifiedExpressionName(expression->object);
        return prefix.empty() ? std::string{} : prefix + "." + expression->text;
    }

    static bool isPureStandardCallName(const std::string &name) {
        static const std::unordered_set<std::string> pureStandardCalls{
            "std.core.clone", "std.core.type_of", "std.core.to_string", "std.core.coalesce",
            "std.core.range", "std.core.repeat", "std.math.abs", "std.math.sqrt", "std.math.floor",
            "std.math.ceil", "std.math.round", "std.math.pow", "std.math.min", "std.math.max",
            "std.math.clamp", "std.math.sign", "std.math.gcd", "std.math.lcm", "std.string.length",
            "std.string.trim", "std.string.uppercase", "std.string.lowercase", "std.string.contains",
            "std.string.starts_with", "std.string.ends_with", "std.string.split", "std.string.join",
            "std.string.replace", "std.string.substring", "std.string.char_at", "std.string.repeat",
            "std.string.reverse", "std.string.parse_int", "std.string.parse_float", "std.list.length",
            "std.list.first", "std.list.last", "std.list.get", "std.list.slice", "std.list.append",
            "std.list.prepend", "std.list.concat", "std.list.reverse", "std.list.contains", "std.list.sum",
            "std.list.sort", "std.list.sort_desc", "std.list.zip", "std.map.length", "std.map.size",
            "std.map.has", "std.map.get", "std.map.get_or", "std.map.keys", "std.map.values",
            "std.map.entries", "std.map.put", "std.map.remove", "std.map.merge", "std.json.parse",
            "std.json.stringify", "std.json.pretty", "std.json.is_valid", "std.url.encode_component",
            "std.url.decode_component", "std.url.parse_query", "std.html.escape", "std.html.escape_text",
            "std.html.escape_attribute"
        };
        return pureStandardCalls.count(name) != 0;
    }

    bool isCompileTimePureExpression(const ExprPtr &expression) {
        if (!expression) return false;
        switch (expression->kind) {
        case ExprKind::Literal:
            return true;
        case ExprKind::Variable: {
            const auto binding = env_.find(expression->text);
            return binding && binding->known && !binding->moved;
        }
        case ExprKind::Array:
            return std::all_of(expression->items.begin(), expression->items.end(),
                               [&](const ExprPtr &item) { return isCompileTimePureExpression(item); });
        case ExprKind::Object:
            return std::all_of(expression->fields.begin(), expression->fields.end(),
                               [&](const auto &field) { return isCompileTimePureExpression(field.second); });
        case ExprKind::Unary:
            return isCompileTimePureExpression(expression->left);
        case ExprKind::Binary:
            return isCompileTimePureExpression(expression->left) &&
                   isCompileTimePureExpression(expression->right);
        case ExprKind::Member:
            return isCompileTimePureExpression(expression->object);
        case ExprKind::Index:
            return isCompileTimePureExpression(expression->object) &&
                   isCompileTimePureExpression(expression->index);
        case ExprKind::Call: {
            const std::string name = qualifiedExpressionName(expression->callee);
            if (name != "len" && name != "abs" && !isPureStandardCallName(name)) return false;
            return std::all_of(expression->items.begin(), expression->items.end(),
                               [&](const ExprPtr &item) { return isCompileTimePureExpression(item); });
        }
        default:
            return false;
        }
    }

    std::optional<Value> staticExpr(const ExprPtr &expression) {
        if (!expression) return std::nullopt;
        switch (expression->kind) {
        case ExprKind::Literal:
            if (expression->literalKind == LiteralKind::Integer) return Value::integerValue(std::stoll(expression->text));
            if (expression->literalKind == LiteralKind::Float) return Value::floatValue(std::stod(expression->text));
            if (expression->text.find('\x1f') != std::string::npos && expression->text.find("${") != std::string::npos) return std::nullopt;
            return Value::stringValue(expression->text);
        case ExprKind::Variable: {
            auto binding = env_.find(expression->text);
            if (!binding || binding->moved || !binding->known) return std::nullopt;
            return binding->value;
        }
        case ExprKind::Array: {
            std::vector<Value> values;
            for (const ExprPtr &item : expression->items) {
                auto value = staticExpr(item);
                if (!value) return std::nullopt;
                values.push_back(*value);
            }
            return Value::arrayValue(std::move(values));
        }
        case ExprKind::Object: {
            std::map<std::string, Value> fields;
            for (const auto &field : expression->fields) {
                auto value = staticExpr(field.second);
                if (!value) return std::nullopt;
                fields.emplace(field.first, *value);
            }
            return Value::objectValue(std::move(fields));
        }
        case ExprKind::Unary: {
            auto value = staticExpr(expression->left);
            if (!value) return std::nullopt;
            return applyUnary(expression->op, *value, expression->pos);
        }
        case ExprKind::Binary: {
            auto left = staticExpr(expression->left);
            auto right = staticExpr(expression->right);
            if (!left || !right) return std::nullopt;
            return applyBinary(expression->op, *left, *right, expression->pos);
        }
        case ExprKind::Member: {
            auto object = staticExpr(expression->object);
            if (!object) return std::nullopt;
            return memberValue(*object, expression->text);
        }
        case ExprKind::Index: {
            auto object = staticExpr(expression->object);
            auto index = staticExpr(expression->index);
            if (!object || !index) return std::nullopt;
            return indexValue(*object, *index, expression->pos);
        }
        case ExprKind::Call: {
            const std::string name = qualifiedExpressionName(expression->callee);
            if (name != "len" && name != "abs" && !isPureStandardCallName(name)) return std::nullopt;
            std::vector<Value> args;
            for (const ExprPtr &arg : expression->items) {
                auto value = staticExpr(arg);
                if (!value) return std::nullopt;
                args.push_back(*value);
            }
            if (name == "len" || name == "abs") return callBuiltin(name, args, expression->pos, true);
            const std::size_t separator = name.rfind('.');
            if (separator == std::string::npos) return std::nullopt;
            return callStdLibrary(name.substr(0, separator), name.substr(separator + 1),
                                  args, expression->pos, true);
        }
        default: return std::nullopt;
        }
    }

    Truth runtimeCondition(const ConditionPtr &condition) {
        if (!condition) return Truth::Unknown;
        switch (condition->kind) {
        case ConditionKind::Always: return Truth::True;
        case ConditionKind::Never: return Truth::False;
        case ConditionKind::Not: {
            const Truth inner = runtimeCondition(condition->items.front());
            if (inner == Truth::True) return Truth::False;
            if (inner == Truth::False) return Truth::True;
            return Truth::Unknown;
        }
        case ConditionKind::And: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                const Truth value = runtimeCondition(item);
                if (value == Truth::False) return Truth::False;
                if (value == Truth::Unknown) unknown = true;
            }
            return unknown ? Truth::Unknown : Truth::True;
        }
        case ConditionKind::Or: {
            bool unknown = false;
            for (const ConditionPtr &item : condition->items) {
                const Truth value = runtimeCondition(item);
                if (value == Truth::True) return Truth::True;
                if (value == Truth::Unknown) unknown = true;
            }
            return unknown ? Truth::Unknown : Truth::False;
        }
        case ConditionKind::ValueRef: {
            Value value = evalExpr(condition->value);
            if (value.kind == ValueKind::Boolean) return value.boolean ? Truth::True : Truth::False;
            if (value.kind != ValueKind::Condition || !value.condition) return Truth::Unknown;
            validateCapturedCondition(*value.condition, condition->pos);
            return value.condition->holds ? Truth::True : Truth::False;
        }
        case ConditionKind::Is: {
            Value value = evalExpr(condition->left);
            return predicateHolds(value, condition->predicate) ? Truth::True : Truth::False;
        }
        case ConditionKind::Has: {
            Value value = evalExpr(condition->left);
            return hasFields(value, condition) ? Truth::True : Truth::False;
        }
        case ConditionKind::Relation: {
            Value left = evalExpr(condition->left);
            Value right = evalExpr(condition->right);
            auto result = compareValues(left, right, condition->op);
            return result && *result ? Truth::True : Truth::False;
        }
        case ConditionKind::Call:
            return evaluateConditionCall(condition);
        }
        return Truth::Unknown;
    }

    Truth evaluateLambdaCondition(const Value &rawLambda, const Value &item, const SourcePos &pos) {
        const Value lambda = dereference(rawLambda);
        if (lambda.kind != ValueKind::Lambda || !lambda.lambda) return Truth::Unknown;
        auto scope = env_.scoped(false);
        env_.define(lambda.lambda->lambdaParam, item, nullptr, false, true, pos, false);
        return runtimeCondition(lambda.lambda->lambdaBody);
    }

    Truth evaluateStdPredicateValues(const std::string &predicate, const std::vector<Value> &args,
                                     const SourcePos &pos) {
        const auto arity = [&](std::size_t expected) {
            return args.size() == expected;
        };
        const auto one = [&]() -> Value { return dereference(args.front()); };
        const auto truth = [](bool value) { return value ? Truth::True : Truth::False; };

        if (predicate == "std.pred.is_null" && arity(1)) return truth(one().kind == ValueKind::Null);
        if (predicate == "std.pred.is_int" && arity(1)) return truth(one().kind == ValueKind::Integer);
        if (predicate == "std.pred.is_float" && arity(1)) return truth(one().kind == ValueKind::Float);
        if (predicate == "std.pred.is_number" && arity(1)) return truth(one().isNumber());
        if (predicate == "std.pred.is_bool" && arity(1)) return truth(one().kind == ValueKind::Boolean);
        if (predicate == "std.pred.is_string" && arity(1)) return truth(one().kind == ValueKind::String);
        if (predicate == "std.pred.is_list" && arity(1)) return truth(one().kind == ValueKind::Array);
        if (predicate == "std.pred.is_object" && arity(1)) return truth(one().kind == ValueKind::Object);
        if (predicate == "std.pred.is_finite" && arity(1)) {
            const Value value = one();
            return truth(value.kind == ValueKind::Integer ||
                         (value.kind == ValueKind::Float && std::isfinite(value.floating)));
        }
        if ((predicate == "std.pred.equal" || predicate == "std.pred.not_equal") && arity(2)) {
            const bool equal = valuesEqual(args[0], args[1]);
            return truth(predicate == "std.pred.equal" ? equal : !equal);
        }
        if ((predicate == "std.pred.positive" || predicate == "std.pred.nonnegative" ||
             predicate == "std.pred.negative" || predicate == "std.pred.even" ||
             predicate == "std.pred.odd") && arity(1)) {
            const Value value = one();
            if (value.kind != ValueKind::Integer && value.kind != ValueKind::Float) return Truth::False;
            if (predicate == "std.pred.positive") {
                return truth(value.kind == ValueKind::Integer ? value.integer > 0 : value.floating > 0.0);
            }
            if (predicate == "std.pred.nonnegative") {
                return truth(value.kind == ValueKind::Integer ? value.integer >= 0 : value.floating >= 0.0);
            }
            if (predicate == "std.pred.negative") {
                return truth(value.kind == ValueKind::Integer ? value.integer < 0 : value.floating < 0.0);
            }
            if (value.kind != ValueKind::Integer) return Truth::False;
            return truth(predicate == "std.pred.even" ? (value.integer % 2 == 0) : (value.integer % 2 != 0));
        }
        if (predicate == "std.pred.in_range" && arity(3)) {
            auto lower = compareValues(args[1], args[0], "<=");
            auto upper = compareValues(args[0], args[2], "<=");
            return truth(lower && *lower && upper && *upper);
        }
        if (predicate == "std.pred.contains" && arity(2)) {
            const Value container = one();
            const Value needle = dereference(args[1]);
            if (container.kind == ValueKind::String && needle.kind == ValueKind::String) {
                return truth(container.string.find(needle.string) != std::string::npos);
            }
            if (container.kind == ValueKind::Array && container.array) {
                for (const Value &item : *container.array) if (valuesEqual(item, needle)) return Truth::True;
                return Truth::False;
            }
            return Truth::False;
        }
        if (predicate == "std.pred.has_key" && arity(2)) {
            const Value object = one();
            const Value key = dereference(args[1]);
            return truth(object.kind == ValueKind::Object && object.object && key.kind == ValueKind::String &&
                         object.object->count(key.string) != 0);
        }
        if (predicate == "std.pred.is_empty" && arity(1)) {
            const Value value = one();
            if (value.kind == ValueKind::String) return truth(value.string.empty());
            if (value.kind == ValueKind::Array) return truth(!value.array || value.array->empty());
            if (value.kind == ValueKind::Object) return truth(!value.object || value.object->empty());
            if (value.kind == ValueKind::Null) return Truth::True;
            return Truth::False;
        }
        if ((predicate == "std.pred.all" || predicate == "std.pred.any" || predicate == "std.pred.none" ||
             predicate == "std.list.all" || predicate == "std.list.any" || predicate == "std.list.none") &&
            arity(2)) {
            const Value collection = dereference(args[0]);
            if (collection.kind != ValueKind::Array || !collection.array) return Truth::Unknown;
            if (dereference(args[1]).kind != ValueKind::Lambda) return Truth::Unknown;
            const bool wantAny = predicate == "std.pred.any" || predicate == "std.list.any";
            const bool wantNone = predicate == "std.pred.none" || predicate == "std.list.none";
            bool unknown = false;
            for (const Value &item : *collection.array) {
                const Truth result = evaluateLambdaCondition(args[1], item, pos);
                if (result == Truth::True && wantAny) return Truth::True;
                if (result == Truth::False && !wantAny && !wantNone) return Truth::False;
                if (result == Truth::True && wantNone) return Truth::False;
                if (result == Truth::Unknown) unknown = true;
            }
            if (wantAny) return unknown ? Truth::Unknown : Truth::False;
            if (wantNone) return unknown ? Truth::Unknown : Truth::True;
            return unknown ? Truth::Unknown : Truth::True;
        }
        if (predicate == "std.map.has" && arity(2)) {
            const Value object = one();
            const Value key = dereference(args[1]);
            return truth(object.kind == ValueKind::Object && object.object && key.kind == ValueKind::String &&
                         object.object->count(key.string) != 0);
        }
        return Truth::Unknown;
    }

    Truth evaluateStdPredicate(const std::string &predicate, const std::vector<ExprPtr> &expressions,
                               const SourcePos &pos) {
        std::vector<Value> args;
        args.reserve(expressions.size());
        for (const ExprPtr &expression : expressions) args.push_back(evalExpr(expression));
        return evaluateStdPredicateValues(predicate, args, pos);
    }

    Truth evaluateConditionCall(const ConditionPtr &condition) {
        if (condition->predicate.rfind("std.", 0) == 0) {
            return evaluateStdPredicate(condition->predicate, condition->args, condition->pos);
        }
        if (condition->predicate == "all") {
            if (condition->args.size() != 2) return Truth::Unknown;
            Value collection = dereference(evalExpr(condition->args[0]));
            Value lambda = evalExpr(condition->args[1]);
            if (collection.kind != ValueKind::Array || lambda.kind != ValueKind::Lambda || !lambda.lambda) return Truth::Unknown;
            for (const Value &item : *collection.array) {
                auto scope = env_.scoped(false);
                Truth result = Truth::Unknown;
                env_.define(lambda.lambda->lambdaParam, item, nullptr, false, true, condition->pos, false);
                result = runtimeCondition(lambda.lambda->lambdaBody);
                if (result == Truth::False) return Truth::False;
                if (result == Truth::Unknown) return Truth::Unknown;
            }
            return Truth::True;
        }
        auto definition = program_.conditions.find(condition->predicate);
        if (definition == program_.conditions.end()) return Truth::Unknown;
        if (definition->second.params.size() != condition->args.size()) return Truth::Unknown;
        std::vector<Value> arguments;
        arguments.reserve(condition->args.size());
        for (const ExprPtr &arg : condition->args) arguments.push_back(evalExpr(arg));

        auto scope = env_.scoped(false);
        for (std::size_t i = 0; i < arguments.size(); ++i) {
            env_.define(definition->second.params[i].name, arguments[i], nullptr, false,
                        expressionIsPureLiteral(condition->args[i]), condition->pos, false);
        }
        return runtimeCondition(definition->second.body);
    }

    bool predicateHolds(const Value &rawValue, const std::string &predicate) {
        const Value value = dereference(rawValue);
        if (predicate == "Any" || predicate == "Value") return true;
        if (predicate == "Int") return value.kind == ValueKind::Integer;
        if (predicate == "Float") return value.kind == ValueKind::Float;
        if (predicate == "Bool") return value.kind == ValueKind::Boolean;
        if (predicate == "Number") return value.isNumber();
        if (predicate == "String") return value.kind == ValueKind::String;
        if (predicate == "List") return value.kind == ValueKind::Array;
        if (predicate == "Object") return value.kind == ValueKind::Object;
        if (predicate == "Null") return value.kind == ValueKind::Null;
        if (predicate == "Finite") {
            return value.kind == ValueKind::Integer || (value.kind == ValueKind::Float && std::isfinite(value.floating));
        }
        if (predicate == "Callable") return value.kind == ValueKind::Function || value.kind == ValueKind::Builtin || value.kind == ValueKind::Method;
        if (predicate == "Condition") return value.kind == ValueKind::Condition;
        if (predicate == "HttpRequest") return value.kind == ValueKind::HttpRequest;
        if (predicate == "HttpResponse") return value.kind == ValueKind::HttpResponse;
        auto condition = program_.conditions.find(predicate);
        if (condition != program_.conditions.end() && condition->second.params.size() == 1) {
            auto arg = Expr::make(ExprKind::Literal, SourcePos{file_, 0, 1, 1});
            // Custom predicates used with `is` receive the value as their
            // sole argument.  Evaluate directly through a temporary scope.
            auto scope = env_.scoped(false);
            env_.define(condition->second.params.front().name, value, nullptr, false, true, arg->pos, false);
            return runtimeCondition(condition->second.body) == Truth::True;
        }
        return false;
    }

    bool hasFields(const Value &rawValue, const ConditionPtr &condition) {
        const Value value = dereference(rawValue);
        if (value.kind != ValueKind::Object || !value.object) return false;
        if (!condition->fields.empty()) {
            for (const ShapeFieldSchema &field : condition->shapeFields) {
                auto found = value.object->find(field.name);
                if (found == value.object->end()) return false;
                if (!field.predicate.empty() && !predicateHolds(found->second, field.predicate)) return false;
                if (field.condition) {
                    auto scope = env_.scoped(false);
                    env_.define("self", found->second, nullptr, false, true, field.pos, false);
                    if (runtimeCondition(field.condition) != Truth::True) return false;
                }
            }
            return true;
        }
        if (!condition->right) return false;
        const Value field = dereference(evalExpr(condition->right));
        if (field.kind != ValueKind::String) return false;
        return value.object->count(field.string) != 0;
    }

    std::optional<bool> compareValues(const Value &rawLeft, const Value &rawRight, const std::string &op) {
        const Value left = dereference(rawLeft);
        const Value right = dereference(rawRight);
        if (op == "==") return valuesEqual(left, right);
        if (op == "!=") return !valuesEqual(left, right);
        if (op == "in") {
            if (right.kind == ValueKind::Array && right.array) {
                for (const Value &element : *right.array) if (valuesEqual(left, element)) return true;
                return false;
            }
            if (right.kind == ValueKind::Object && left.kind == ValueKind::String) return right.object->count(left.string) != 0;
            return false;
        }
        if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
            if (op == "<") return left.integer < right.integer;
            if (op == "<=") return left.integer <= right.integer;
            if (op == ">") return left.integer > right.integer;
            if (op == ">=") return left.integer >= right.integer;
        }
        if (left.isNumber() && right.isNumber()) {
            const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
            const double b = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
            if (op == "<") return a < b;
            if (op == "<=") return a <= b;
            if (op == ">") return a > b;
            if (op == ">=") return a >= b;
        }
        if (left.kind == ValueKind::String && right.kind == ValueKind::String) {
            if (op == "<") return left.string < right.string;
            if (op == "<=") return left.string <= right.string;
            if (op == ">") return left.string > right.string;
            if (op == ">=") return left.string >= right.string;
        }
        return false;
    }

    Value applyUnary(const std::string &op, const Value &rawValue, const SourcePos &pos) {
        const Value value = dereference(rawValue);
        const auto finish = [&](Value result) {
            result.flow = value.flow;
            return result;
        };
        if (op == "+") {
            if (!value.isNumber()) fail("E1202", pos, "単項 '+' には数値が必要です");
            return value;
        }
        if (op == "-") {
            if (value.kind == ValueKind::Integer) {
                if (value.integer == std::numeric_limits<std::int64_t>::min()) {
                    fail("E1208", pos, "Int64 の単項マイナスがオーバーフローします");
                }
                return finish(Value::integerValue(-value.integer));
            }
            if (value.kind == ValueKind::Float) return finish(Value::floatValue(-value.floating));
            fail("E1202", pos, "単項 '-' には数値が必要です");
        }
        if (op == "!") {
            if (value.kind != ValueKind::Condition) fail("E1202", pos, "Kond では条件値に対してのみ '!' を使用できます");
            if (!value.condition) fail("E1202", pos, "条件値の証拠がありません");
            auto negated = std::make_shared<RuntimeCondition>(*value.condition);
            negated->holds = !negated->holds;
            auto proposition = Condition::make(ConditionKind::Not, pos);
            proposition->items.push_back(value.condition->condition);
            negated->condition = std::move(proposition);
            return finish(Value::conditionValue(std::move(negated)));
        }
        fail("E1202", pos, "未対応の単項演算子です: " + op);
    }

    Value applyBinary(const std::string &op, const Value &rawLeft, const Value &rawRight, const SourcePos &pos) {
        const Value left = dereference(rawLeft);
        const Value right = dereference(rawRight);
        const auto finish = [&](Value result) {
            result.flow = left.flow | right.flow;
            if (op == "+" && result.kind == ValueKind::String) result.safety = left.safety & right.safety;
            return result;
        };
        if (op == "+") {
            if (left.kind == ValueKind::String || right.kind == ValueKind::String) {
                return finish(Value::stringValue(valueToString(left) + valueToString(right)));
            }
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                std::int64_t result = 0;
                if (checkedAdd(left.integer, right.integer, result)) {
                    fail("E1208", pos, "Int64 の加算がオーバーフローします");
                }
                return finish(Value::integerValue(result));
            }
            if (left.isNumber() && right.isNumber()) {
                const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
                const double b = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
                return finish(Value::floatValue(a + b));
            }
        } else if (op == "-") {
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                std::int64_t result = 0;
                if (checkedSub(left.integer, right.integer, result)) {
                    fail("E1208", pos, "Int64 の減算がオーバーフローします");
                }
                return finish(Value::integerValue(result));
            }
            if (left.isNumber() && right.isNumber()) {
                const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
                const double b = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
                return finish(Value::floatValue(a - b));
            }
        } else if (op == "*") {
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                std::int64_t result = 0;
                if (checkedMul(left.integer, right.integer, result)) {
                    fail("E1208", pos, "Int64 の乗算がオーバーフローします");
                }
                return finish(Value::integerValue(result));
            }
            if (left.isNumber() && right.isNumber()) {
                const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
                const double b = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
                return finish(Value::floatValue(a * b));
            }
        } else if (op == "/") {
            if (!right.isNumber()) fail("E1202", pos, "除算の右辺には数値が必要です");
            const double divisor = right.kind == ValueKind::Integer ? static_cast<double>(right.integer) : right.floating;
            if (divisor == 0.0) fail("E1203", pos, "ゼロ除算です");
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                if (left.integer == std::numeric_limits<std::int64_t>::min() && right.integer == -1) {
                    fail("E1208", pos, "Int64 の除算がオーバーフローします");
                }
                return finish(Value::integerValue(left.integer / right.integer));
            }
            if (left.isNumber()) {
                const double a = left.kind == ValueKind::Integer ? static_cast<double>(left.integer) : left.floating;
                return finish(Value::floatValue(a / divisor));
            }
        } else if (op == "%") {
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                if (right.integer == 0) fail("E1203", pos, "ゼロ除算です");
                if (left.integer == std::numeric_limits<std::int64_t>::min() && right.integer == -1) {
                    fail("E1208", pos, "Int64 の剰余演算がオーバーフローします");
                }
                return finish(Value::integerValue(left.integer % right.integer));
            }
        } else if (op == "&" || op == "|" || op == "^") {
            if (left.kind == ValueKind::Integer && right.kind == ValueKind::Integer) {
                if (op == "&") return finish(Value::integerValue(left.integer & right.integer));
                if (op == "|") return finish(Value::integerValue(left.integer | right.integer));
                return finish(Value::integerValue(left.integer ^ right.integer));
            }
        }
        fail("E1202", pos, "演算子 " + op + " のオペランド型が不正です");
    }

    Value memberValue(const Value &rawObject, const std::string &member) {
        const Value object = dereference(rawObject);
        if (object.kind == ValueKind::Builtin &&
            (object.name == "std" || object.name.rfind("std.", 0) == 0)) {
            return Value::builtinValue(object.name + "." + member);
        }
        if (object.kind == ValueKind::HttpRequest && object.request) {
            const auto requestString = [&](const std::string &text) {
                Value result = Value::stringValue(text);
                result.flow = object.flow | FlowUntrusted;
                return result;
            };
            if (member == "method") return requestString(object.request->method);
            if (member == "target") return requestString(object.request->target);
            if (member == "path") return requestString(object.request->path);
            if (member == "body") return requestString(object.request->body);
            if (member == "query") {
                Value result = Value::objectValue(object.request->query);
                result.flow |= object.flow | FlowUntrusted;
                return result;
            }
            if (member == "headers") {
                Value result = Value::objectValue(object.request->headers);
                result.flow |= object.flow | FlowUntrusted;
                return result;
            }
            if (member == "cookies") {
                Value result = Value::objectValue(object.request->cookies);
                result.flow |= object.flow | FlowUntrusted;
                return result;
            }
        }
        if (object.kind == ValueKind::HttpResponse && object.response) {
            if (member == "status") return Value::integerValue(object.response->status);
            if (member == "headers") {
                Value result = Value::objectValue(object.response->headers);
                result.flow |= object.flow;
                return result;
            }
            if (member == "body") {
                Value result = object.response->body;
                result.flow |= object.flow;
                return result;
            }
        }
        if (object.kind == ValueKind::Object && object.object) {
            auto found = object.object->find(member);
            if (found != object.object->end()) {
                Value result = found->second;
                result.flow |= object.flow;
                return result;
            }
        }
        if (object.kind == ValueKind::String && (member == "length" || member == "len")) {
            Value result = Value::integerValue(static_cast<std::int64_t>(object.string.size()));
            result.flow = object.flow;
            return result;
        }
        return Value::null();
    }

    Value indexValue(const Value &rawObject, const Value &rawIndex, const SourcePos &pos) {
        const Value object = dereference(rawObject);
        const Value index = dereference(rawIndex);
        if (object.kind == ValueKind::Array && index.kind == ValueKind::Integer) {
            if (index.integer < 0 || static_cast<std::size_t>(index.integer) >= object.array->size()) {
                fail("E1204", pos, "配列インデックスが範囲外です");
            }
            Value result = (*object.array)[static_cast<std::size_t>(index.integer)];
            result.flow |= object.flow | index.flow;
            return result;
        }
        if (object.kind == ValueKind::Object && index.kind == ValueKind::String) {
            auto found = object.object->find(index.string);
            Value result = found == object.object->end() ? Value::null() : found->second;
            result.flow |= object.flow | index.flow;
            return result;
        }
        fail("E1205", pos, "インデックス対象が配列またはオブジェクトではありません");
    }

    static std::string escapeHtml(std::string_view text) {
        std::string result;
        result.reserve(text.size());
        for (char ch : text) {
            switch (ch) {
            case '&': result += "&amp;"; break;
            case '<': result += "&lt;"; break;
            case '>': result += "&gt;"; break;
            case '"': result += "&quot;"; break;
            case '\'': result += "&#39;"; break;
            default: result.push_back(ch); break;
            }
        }
        return result;
    }

    static std::string encodeUrl(std::string_view text) {
        static constexpr char hex[] = "0123456789ABCDEF";
        std::string result;
        result.reserve(text.size());
        for (unsigned char ch : text) {
            const bool unreserved = (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') ||
                                    (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~';
            if (unreserved) {
                result.push_back(static_cast<char>(ch));
            } else {
                result.push_back('%');
                result.push_back(hex[ch >> 4]);
                result.push_back(hex[ch & 0x0f]);
            }
        }
        return result;
    }

    Value evalTaggedString(const ExprPtr &expression) {
        const std::size_t separator = expression->text.find('\x1f');
        if (separator == std::string::npos) return Value::stringValue(expression->text);
        const std::string tag = expression->text.substr(0, separator);
        const std::string content = expression->text.substr(separator + 1);
        std::string rendered;
        std::uint32_t flow = FlowPublic;
        std::size_t cursor = 0;
        while (cursor < content.size()) {
            const std::size_t open = content.find("${", cursor);
            if (open == std::string::npos) {
                rendered.append(content, cursor, std::string::npos);
                break;
            }
            rendered.append(content, cursor, open - cursor);
            std::size_t close = open + 2;
            int depth = 1;
            for (; close < content.size() && depth > 0; ++close) {
                if (content[close] == '{') ++depth;
                else if (content[close] == '}') --depth;
            }
            if (depth != 0) fail("E0112", expression->pos, "タグ付き文字列の補間が閉じられていません");
            const std::string embeddedSource = content.substr(open + 2, close - open - 3);
            Value embedded = evalExpr(parseExpression(embeddedSource, expression->pos.file + ":interpolation"));
            flow |= embedded.flow;
            if (tag == "sql") rendered += '?';
            else if (tag == "html") rendered += escapeHtml(valueToString(embedded));
            else rendered += valueToString(embedded);
            cursor = close;
        }
        Value result = Value::stringValue(tag + std::string(1, '\x1f') + rendered);
        result.flow = flow;
        return result;
    }

    static ExprPtr integerLiteral(std::int64_t value, const SourcePos &pos) {
        auto result = Expr::make(ExprKind::Literal, pos);
        result->literalKind = LiteralKind::Integer;
        result->text = std::to_string(value);
        return result;
    }

    static ConditionPtr relationCondition(ExprPtr left, std::string op, ExprPtr right, const SourcePos &pos) {
        auto result = Condition::make(ConditionKind::Relation, pos);
        result->left = std::move(left);
        result->op = std::move(op);
        result->right = std::move(right);
        return result;
    }

    static std::optional<std::int64_t> integerLiteralValue(const ExprPtr &expression) {
        if (!expression || expression->kind != ExprKind::Literal ||
            expression->literalKind != LiteralKind::Integer) {
            return std::nullopt;
        }
        try {
            return std::stoll(expression->text);
        } catch (const std::exception &) {
            return std::nullopt;
        }
    }

    static bool rewriteExprEqual(const ExprPtr &left, const ExprPtr &right) {
        if (!left || !right) return left == right;
        if (left->kind != right->kind || left->literalKind != right->literalKind ||
            left->text != right->text || left->op != right->op ||
            left->lambdaParam != right->lambdaParam || left->mutableBorrow != right->mutableBorrow) {
            return false;
        }
        if (!rewriteExprEqual(left->left, right->left) || !rewriteExprEqual(left->right, right->right) ||
            !rewriteExprEqual(left->object, right->object) || !rewriteExprEqual(left->index, right->index) ||
            !rewriteExprEqual(left->callee, right->callee) ||
            left->items.size() != right->items.size() || left->fields.size() != right->fields.size()) {
            return false;
        }
        for (std::size_t i = 0; i < left->items.size(); ++i) {
            if (!rewriteExprEqual(left->items[i], right->items[i])) return false;
        }
        for (std::size_t i = 0; i < left->fields.size(); ++i) {
            if (left->fields[i].first != right->fields[i].first ||
                !rewriteExprEqual(left->fields[i].second, right->fields[i].second)) return false;
        }
        return true;
    }

    static bool isRewriteParameter(const OptimizationRule &rule, const std::string &name) {
        return std::find(rule.parameters.begin(), rule.parameters.end(), name) != rule.parameters.end();
    }

    static bool matchRewriteExpr(const ExprPtr &pattern, const ExprPtr &candidate,
                                 const OptimizationRule &rule,
                                 std::unordered_map<std::string, ExprPtr> &bindings) {
        if (!pattern || !candidate) return pattern == candidate;
        if (pattern->kind == ExprKind::Variable && isRewriteParameter(rule, pattern->text)) {
            auto found = bindings.find(pattern->text);
            if (found == bindings.end()) {
                bindings.emplace(pattern->text, candidate);
                return true;
            }
            return rewriteExprEqual(found->second, candidate);
        }
        if (pattern->kind != candidate->kind || pattern->literalKind != candidate->literalKind ||
            pattern->text != candidate->text || pattern->op != candidate->op ||
            pattern->lambdaParam != candidate->lambdaParam ||
            pattern->mutableBorrow != candidate->mutableBorrow ||
            pattern->items.size() != candidate->items.size() ||
            pattern->fields.size() != candidate->fields.size()) {
            return false;
        }
        if (!matchRewriteExpr(pattern->left, candidate->left, rule, bindings) ||
            !matchRewriteExpr(pattern->right, candidate->right, rule, bindings) ||
            !matchRewriteExpr(pattern->object, candidate->object, rule, bindings) ||
            !matchRewriteExpr(pattern->index, candidate->index, rule, bindings) ||
            !matchRewriteExpr(pattern->callee, candidate->callee, rule, bindings)) {
            return false;
        }
        for (std::size_t i = 0; i < pattern->items.size(); ++i) {
            if (!matchRewriteExpr(pattern->items[i], candidate->items[i], rule, bindings)) return false;
        }
        for (std::size_t i = 0; i < pattern->fields.size(); ++i) {
            if (pattern->fields[i].first != candidate->fields[i].first ||
                !matchRewriteExpr(pattern->fields[i].second, candidate->fields[i].second, rule, bindings)) {
                return false;
            }
        }
        return true;
    }

    static bool isRewritePure(const ExprPtr &expression) {
        if (!expression) return true;
        switch (expression->kind) {
        case ExprKind::Literal:
        case ExprKind::Variable:
            return true;
        case ExprKind::Array:
            return std::all_of(expression->items.begin(), expression->items.end(), isRewritePure);
        case ExprKind::Object:
            return std::all_of(expression->fields.begin(), expression->fields.end(),
                               [](const auto &field) { return isRewritePure(field.second); });
        case ExprKind::Unary:
            return isRewritePure(expression->left);
        case ExprKind::Binary:
            return isRewritePure(expression->left) && isRewritePure(expression->right);
        case ExprKind::Member:
            return isRewritePure(expression->object);
        case ExprKind::Index:
            return isRewritePure(expression->object) && isRewritePure(expression->index);
        case ExprKind::Call: {
            const std::string name = qualifiedExpressionName(expression->callee);
            if (name != "len" && name != "abs" && !isPureStandardCallName(name)) return false;
            return std::all_of(expression->items.begin(), expression->items.end(), isRewritePure);
        }
        default:
            // User functions, moves, borrows, and lambdas are excluded until
            // an effect summary is attached to the rule.  Pure standard calls
            // have a closed effect summary above, so a library can optimize
            // them without silently dropping I/O or mutation.
            return false;
        }
    }

    std::optional<Value> tryRegisteredRewrite(const ExprPtr &expression) {
        if (!expression || rewriteDepth_ >= 32 || program_.optimizationRules.empty() ||
            !isRewritePure(expression)) {
            return std::nullopt;
        }
        for (const OptimizationRule &rule : program_.optimizationRules) {
            if (rule.proofKind != OptimizationProofKind::ExactEq ||
                rewriteExprEqual(rule.pattern, rule.replacement)) {
                continue;
            }
            std::unordered_map<std::string, ExprPtr> bindings;
            if (!matchRewriteExpr(rule.pattern, expression, rule, bindings)) continue;
            if (bindings.size() != rule.parameters.size()) continue;
            bool hasReferenceBinding = false;
            for (const auto &binding : bindings) {
                // Arithmetic and comparison expressions dereference their
                // operands and return an owned value.  Replacing such an
                // expression with a bare borrow would change the observable
                // ValueKind/ownership behavior, so exact library rules stay
                // on the safe value side of that boundary.
                if (evalExpr(binding.second).kind == ValueKind::Reference) {
                    hasReferenceBinding = true;
                    break;
                }
            }
            if (hasReferenceBinding) continue;
            const ConditionPtr instantiated = cloneConditionWithSubstitution(rule.precondition, bindings);
            if (proofState(instantiated) != ProofState::Proven) continue;

            const ExprPtr replacement = cloneExprWithSubstitution(rule.replacement, bindings);
            ++rewriteDepth_;
            try {
                Value result = evalExpr(replacement);
                --rewriteDepth_;
                recordRewrite(OptimizationProofKind::ExactEq, "library " + rule.name,
                              "Kond.Draft0.2", expression->pos);
                return result;
            } catch (const KondError &) {
                --rewriteDepth_;
                // A candidate that does not execute under the current value
                // domain is simply left for the ordinary interpreter.  The
                // original expression remains the semantic fallback.
            }
        }
        return std::nullopt;
    }

    bool proven(const ConditionPtr &condition) {
        return proofState(condition) == ProofState::Proven;
    }

    std::optional<Value> tryExactStandardRewrite(const ExprPtr &expression) {
        if (!expression || expression->kind != ExprKind::Call || !isRewritePure(expression) ||
            expression->items.size() != 1) {
            return std::nullopt;
        }

        const std::string outerName = qualifiedExpressionName(expression->callee);
        const ExprPtr &innerExpression = expression->items.front();
        if (!innerExpression || innerExpression->kind != ExprKind::Call ||
            !isRewritePure(innerExpression)) {
            return std::nullopt;
        }
        const std::string innerName = qualifiedExpressionName(innerExpression->callee);

        const auto requireList = [&](const Value &rawValue, const std::string &signature) {
            const Value value = dereference(rawValue);
            if (value.kind != ValueKind::Array || !value.array) {
                fail("E1303", expression->pos, signature + " の対象はListである必要があります");
            }
            return value;
        };
        const auto requireString = [&](const Value &rawValue, const std::string &signature) {
            const Value value = dereference(rawValue);
            if (value.kind != ValueKind::String) {
                fail("E1303", expression->pos, signature + " の対象はStringである必要があります");
            }
            return valueToString(value);
        };
        const auto requireObject = [&](const Value &rawValue, const std::string &signature) {
            const Value value = dereference(rawValue);
            if (value.kind != ValueKind::Object || !value.object) {
                fail("E1303", expression->pos, signature + " の対象はObjectである必要があります");
            }
            return value;
        };
        const auto requireInteger = [&](const Value &rawValue, const std::string &signature) {
            const Value value = dereference(rawValue);
            if (value.kind != ValueKind::Integer) {
                fail("E1303", expression->pos, signature + " の対象はIntである必要があります");
            }
            return value;
        };
        const auto finishLength = [&](std::int64_t length, std::uint32_t flow,
                                      const std::string &rule) -> std::optional<Value> {
            Value result = Value::integerValue(length);
            result.flow = flow;
            recordExactRewrite(rule, expression->pos);
            return result;
        };

        if ((outerName == "std.list.length" || outerName == "len") && innerName == "std.core.range" &&
            (innerExpression->items.size() == 2 || innerExpression->items.size() == 3)) {
            const Value start = requireInteger(evalExpr(innerExpression->items[0]), "std.core.range");
            const Value end = requireInteger(evalExpr(innerExpression->items[1]), "std.core.range");
            Value step = Value::integerValue(1);
            if (innerExpression->items.size() == 3) {
                step = requireInteger(evalExpr(innerExpression->items[2]), "std.core.range");
            }
            if (step.integer == 0) fail("E1303", expression->pos, "std.core.range の step は0ではいけません");

            std::int64_t current = start.integer;
            std::size_t count = 0;
            while ((step.integer > 0 && current < end.integer) ||
                   (step.integer < 0 && current > end.integer)) {
                ++count;
                if (count > 1000000) {
                    fail("E1303", expression->pos, "std.core.range の要素数が上限を超えています");
                }
                std::int64_t next = 0;
                if (checkedAdd(current, step.integer, next)) break;
                current = next;
            }
            std::uint32_t flow = start.flow | end.flow | step.flow;
            return finishLength(static_cast<std::int64_t>(count), flow,
                                "range length without materialization");
        }

        if ((outerName == "std.list.length" || outerName == "len") && innerName == "std.core.repeat" &&
            innerExpression->items.size() == 2) {
            const Value value = evalExpr(innerExpression->items[0]);
            const Value count = requireInteger(evalExpr(innerExpression->items[1]), "std.core.repeat");
            if (count.integer < 0 || count.integer > 1000000) {
                fail("E1303", expression->pos, "std.core.repeat の count が範囲外です");
            }
            return finishLength(count.integer, value.flow | count.flow,
                                "repeat length without materialization");
        }

        if ((outerName == "std.list.length" || outerName == "len") && innerName == "std.list.concat" &&
            !innerExpression->items.empty()) {
            std::int64_t length = 0;
            std::uint32_t flow = FlowPublic;
            for (const ExprPtr &item : innerExpression->items) {
                const Value list = requireList(evalExpr(item), "std.list.concat");
                flow |= list.flow;
                const auto size = static_cast<std::int64_t>(list.array->size());
                if (checkedAdd(length, size, length)) {
                    fail("E1208", expression->pos, "std.list.concat の長さがInt64を超えます");
                }
            }
            return finishLength(length, flow, "concat length without materialization");
        }

        if ((outerName == "std.list.length" || outerName == "len") &&
            (innerName == "std.list.append" || innerName == "std.list.prepend") &&
            innerExpression->items.size() == 2) {
            const Value list = requireList(evalExpr(innerExpression->items[0]), innerName);
            const Value value = evalExpr(innerExpression->items[1]);
            return finishLength(static_cast<std::int64_t>(list.array->size()) + 1,
                                list.flow | value.flow, "append/prepend length without materialization");
        }

        if ((outerName == "std.list.length" || outerName == "len") &&
            (innerName == "std.list.reverse" || innerName == "std.list.sort" ||
             innerName == "std.list.sort_desc") && innerExpression->items.size() == 1) {
            const Value list = requireList(evalExpr(innerExpression->items.front()), innerName);
            if (innerName == "std.list.sort" || innerName == "std.list.sort_desc") {
                // Sorting compares elements after dereferencing them.  A
                // list containing a borrow is left to the ordinary path so
                // moved-borrow diagnostics and ownership behavior remain
                // observable exactly as written.
                if (std::any_of(list.array->begin(), list.array->end(),
                                [](const Value &item) { return item.kind == ValueKind::Reference; })) {
                    return std::nullopt;
                }
            }
            return finishLength(static_cast<std::int64_t>(list.array->size()), list.flow,
                                "list length without copy/sort");
        }

        if ((outerName == "std.list.length" || outerName == "len") && innerName == "std.list.zip" &&
            innerExpression->items.size() == 2) {
            const Value left = requireList(evalExpr(innerExpression->items[0]), "std.list.zip");
            const Value right = requireList(evalExpr(innerExpression->items[1]), "std.list.zip");
            return finishLength(static_cast<std::int64_t>(std::min(left.array->size(), right.array->size())),
                                left.flow | right.flow, "zip length without materialization");
        }

        if ((outerName == "std.list.length" || outerName == "len") && innerName == "std.list.slice" &&
            innerExpression->items.size() == 3) {
            const Value list = requireList(evalExpr(innerExpression->items[0]), "std.list.slice");
            const Value start = requireInteger(evalExpr(innerExpression->items[1]), "std.list.slice");
            const Value end = requireInteger(evalExpr(innerExpression->items[2]), "std.list.slice");
            if (start.integer < 0 || end.integer < start.integer ||
                static_cast<std::uint64_t>(start.integer) > list.array->size()) {
                fail("E1204", expression->pos, "std.list.slice の範囲が不正です");
            }
            const std::size_t finish = std::min(static_cast<std::size_t>(end.integer), list.array->size());
            return finishLength(static_cast<std::int64_t>(finish - static_cast<std::size_t>(start.integer)),
                                list.flow | start.flow | end.flow, "slice length without materialization");
        }

        if (outerName == "std.string.length" && innerName == "std.string.repeat" &&
            innerExpression->items.size() == 2) {
            const Value rawValue = evalExpr(innerExpression->items[0]);
            const std::string value = requireString(rawValue, "std.string.repeat");
            const Value count = requireInteger(evalExpr(innerExpression->items[1]), "std.string.repeat");
            if (count.integer < 0 || count.integer > 1000000) {
                fail("E1303", expression->pos, "std.string.repeat の count が範囲外です");
            }
            const auto repeated = static_cast<std::uint64_t>(count.integer);
            if (repeated != 0 && value.size() > std::numeric_limits<std::size_t>::max() / repeated) {
                fail("E1303", expression->pos, "std.string.repeat の結果が大きすぎます");
            }
            const std::size_t length = value.size() * static_cast<std::size_t>(repeated);
            return finishLength(static_cast<std::int64_t>(length),
                                rawValue.flow | count.flow,
                                "string repeat length without materialization");
        }

        if (outerName == "std.string.length" &&
            (innerName == "std.string.reverse" || innerName == "std.string.uppercase" ||
             innerName == "std.string.lowercase") && innerExpression->items.size() == 1) {
            const Value rawValue = evalExpr(innerExpression->items.front());
            const std::string value = requireString(rawValue, innerName);
            return finishLength(static_cast<std::int64_t>(value.size()), rawValue.flow,
                                "string length without copy");
        }

        if ((outerName == "std.map.length" || outerName == "std.map.size" || outerName == "len") &&
            innerName == "std.map.put" && innerExpression->items.size() == 3) {
            const Value object = requireObject(evalExpr(innerExpression->items[0]), "std.map.put");
            const Value key = dereference(evalExpr(innerExpression->items[1]));
            const Value value = evalExpr(innerExpression->items[2]);
            if (key.kind != ValueKind::String) fail("E1303", expression->pos, "std.map.put の key はStringです");
            const std::int64_t length = static_cast<std::int64_t>(object.object->size()) +
                                        (object.object->count(key.string) == 0 ? 1 : 0);
            return finishLength(length, object.flow | key.flow | value.flow,
                                "map put length without materialization");
        }

        if ((outerName == "std.map.length" || outerName == "std.map.size" || outerName == "len") &&
            innerName == "std.map.remove" && innerExpression->items.size() == 2) {
            const Value object = requireObject(evalExpr(innerExpression->items[0]), "std.map.remove");
            const Value key = dereference(evalExpr(innerExpression->items[1]));
            if (key.kind != ValueKind::String) fail("E1303", expression->pos, "std.map.remove の key はStringです");
            const std::int64_t length = static_cast<std::int64_t>(object.object->size()) -
                                        (object.object->count(key.string) == 0 ? 0 : 1);
            return finishLength(length, object.flow | key.flow,
                                "map remove length without materialization");
        }

        if ((outerName == "std.map.length" || outerName == "std.map.size" || outerName == "len") &&
            innerName == "std.map.merge" && innerExpression->items.size() == 2) {
            const Value left = requireObject(evalExpr(innerExpression->items[0]), "std.map.merge");
            const Value right = requireObject(evalExpr(innerExpression->items[1]), "std.map.merge");
            std::size_t length = left.object->size();
            for (const auto &entry : *right.object) {
                if (left.object->count(entry.first) == 0) ++length;
            }
            return finishLength(static_cast<std::int64_t>(length), left.flow | right.flow,
                                "map merge length without materialization");
        }

        return std::nullopt;
    }

    std::optional<Value> tryExactArithmeticRewrite(const ExprPtr &expression) {
        if (!expression) return std::nullopt;

        // An identity is allowed to inspect a variable or literal once.  We
        // deliberately keep this domain small: evaluating an arbitrary
        // expression and then falling back would evaluate it twice, while a
        // member/index expression can still fail even though it has no call.
        const auto stableInteger = [&](const ExprPtr &candidate) -> std::optional<Value> {
            if (!candidate || (candidate->kind != ExprKind::Variable &&
                               candidate->kind != ExprKind::Literal)) {
                return std::nullopt;
            }
            Value value = dereference(evalExpr(candidate));
            if (value.kind != ValueKind::Integer) return std::nullopt;
            return value;
        };
        const auto identity = [&](const ExprPtr &candidate, const std::string &rule)
            -> std::optional<Value> {
            auto value = stableInteger(candidate);
            if (!value) return std::nullopt;
            recordExactRewrite(rule, expression->pos);
            return *value;
        };
        const auto integerConstant = [&](const ExprPtr &candidate, std::int64_t expected) {
            const auto value = integerLiteralValue(candidate);
            return value && *value == expected;
        };

        if (expression->kind == ExprKind::Unary && expression->op == "+") {
            if (auto value = identity(expression->left, "unary plus -> identity")) return value;
        }
        if (expression->kind == ExprKind::Unary && expression->op == "-" &&
            expression->left && expression->left->kind == ExprKind::Unary &&
            expression->left->op == "-") {
            if (auto value = stableInteger(expression->left->left)) {
                if (value->integer != std::numeric_limits<std::int64_t>::min()) {
                    recordExactRewrite("double negation -> identity", expression->pos);
                    return *value;
                }
            }
        }

        if (expression->kind == ExprKind::Binary) {
            const ExprPtr &left = expression->left;
            const ExprPtr &right = expression->right;
            if (expression->op == "+") {
                if (integerConstant(right, 0)) {
                    if (auto value = identity(left, "integer add-zero -> identity")) return value;
                }
                if (integerConstant(left, 0)) {
                    if (auto value = identity(right, "integer zero-add -> identity")) return value;
                }
            } else if (expression->op == "-") {
                if (integerConstant(right, 0)) {
                    if (auto value = identity(left, "integer subtract-zero -> identity")) return value;
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = stableInteger(left)) {
                        recordExactRewrite("integer self-subtract -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
            } else if (expression->op == "*") {
                if (integerConstant(right, 1)) {
                    if (auto value = identity(left, "integer multiply-one -> identity")) return value;
                }
                if (integerConstant(left, 1)) {
                    if (auto value = identity(right, "integer one-multiply -> identity")) return value;
                }
                if (integerConstant(right, 0)) {
                    if (auto value = stableInteger(left)) {
                        recordExactRewrite("integer multiply-zero -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
                if (integerConstant(left, 0)) {
                    if (auto value = stableInteger(right)) {
                        recordExactRewrite("integer zero-multiply -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
            } else if (expression->op == "/") {
                if (integerConstant(right, 1)) {
                    if (auto value = identity(left, "integer divide-one -> identity")) return value;
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = stableInteger(left); value && value->integer != 0) {
                        recordExactRewrite("integer self-divide -> one", expression->pos);
                        return Value::integerValue(1);
                    }
                }
                if (integerConstant(left, 0)) {
                    if (auto value = stableInteger(right); value && value->integer != 0) {
                        recordExactRewrite("integer zero-divide -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
            } else if (expression->op == "%") {
                if (integerConstant(right, 1)) {
                    if (auto value = identity(left, "integer modulo-one -> zero")) {
                        return Value::integerValue(0);
                    }
                }
                if (integerConstant(right, -1)) {
                    if (auto value = stableInteger(left);
                        value && value->integer != std::numeric_limits<std::int64_t>::min()) {
                        recordExactRewrite("integer modulo-minus-one -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = stableInteger(left); value && value->integer != 0) {
                        recordExactRewrite("integer self-modulo -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
            } else if (expression->op == "&") {
                if (integerConstant(right, 0) || integerConstant(left, 0)) {
                    const ExprPtr &other = integerConstant(right, 0) ? left : right;
                    if (auto value = stableInteger(other)) {
                        recordExactRewrite("integer and-zero -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
                if (integerConstant(right, -1)) {
                    if (auto value = identity(left, "integer and-all-ones -> identity")) return value;
                }
                if (integerConstant(left, -1)) {
                    if (auto value = identity(right, "integer all-ones-and -> identity")) return value;
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = identity(left, "integer self-and -> identity")) return value;
                }
            } else if (expression->op == "|") {
                if (integerConstant(right, 0)) {
                    if (auto value = identity(left, "integer or-zero -> identity")) return value;
                }
                if (integerConstant(left, 0)) {
                    if (auto value = identity(right, "integer zero-or -> identity")) return value;
                }
                if (integerConstant(right, -1) || integerConstant(left, -1)) {
                    const ExprPtr &other = integerConstant(right, -1) ? left : right;
                    if (auto value = stableInteger(other)) {
                        recordExactRewrite("integer or-all-ones -> all-ones", expression->pos);
                        return Value::integerValue(-1);
                    }
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = identity(left, "integer self-or -> identity")) return value;
                }
            } else if (expression->op == "^") {
                if (integerConstant(right, 0)) {
                    if (auto value = identity(left, "integer xor-zero -> identity")) return value;
                }
                if (integerConstant(left, 0)) {
                    if (auto value = identity(right, "integer zero-xor -> identity")) return value;
                }
                if (rewriteExprEqual(left, right)) {
                    if (auto value = stableInteger(left)) {
                        recordExactRewrite("integer self-xor -> zero", expression->pos);
                        return Value::integerValue(0);
                    }
                }
            }
        }

        // (x / d) * d == x for checked Int64 when d > 0 and x % d == 0.
        if (expression->kind == ExprKind::Binary && expression->op == "*" &&
            expression->left && expression->left->kind == ExprKind::Binary &&
            expression->left->op == "/" && expression->left->left &&
            expression->left->left->kind == ExprKind::Variable) {
            const auto divisor = integerLiteralValue(expression->left->right);
            const auto multiplier = integerLiteralValue(expression->right);
            if (divisor && multiplier && *divisor > 0 && *divisor == *multiplier) {
                auto modulo = Expr::make(ExprKind::Binary, expression->pos);
                modulo->op = "%";
                modulo->left = expression->left->left;
                modulo->right = expression->left->right;
                auto divisible = relationCondition(modulo, "==", integerLiteral(0, expression->pos), expression->pos);
                if (proven(divisible)) {
                    Value value = dereference(evalExpr(expression->left->left));
                    if (value.kind != ValueKind::Integer) {
                        fail("E1202", expression->pos, "ExactEq の整数除算最適化には Int が必要です");
                    }
                    recordExactRewrite("divisible integer roundtrip -> identity", expression->pos);
                    return value;
                }
            }
        }

        // abs(x) == x if the current fact environment proves x >= 0.
        if (expression->kind == ExprKind::Call && expression->callee &&
            expression->callee->kind == ExprKind::Variable && expression->callee->text == "abs" &&
            expression->items.size() == 1 && expression->items.front()->kind == ExprKind::Variable) {
            const ExprPtr &valueExpression = expression->items.front();
            auto nonnegative = relationCondition(valueExpression, ">=", integerLiteral(0, expression->pos), expression->pos);
            auto nonnegativeReversed = relationCondition(integerLiteral(0, expression->pos), "<=", valueExpression, expression->pos);
            if (proven(nonnegative) || proven(nonnegativeReversed)) {
                Value value = dereference(evalExpr(valueExpression));
                if (value.kind != ValueKind::Integer) {
                    fail("E1202", expression->pos, "ExactEq の abs 最適化には Int が必要です");
                }
                recordExactRewrite("nonnegative abs -> identity", expression->pos);
                return value;
            }
        }

        // x & (2^n - 1) == x when 0 <= x < 2^n.
        if (expression->kind == ExprKind::Binary && expression->op == "&" &&
            expression->left && expression->left->kind == ExprKind::Variable) {
            const auto mask = integerLiteralValue(expression->right);
            if (mask && *mask >= 0 && *mask < std::numeric_limits<std::int64_t>::max() &&
                ((*mask & (*mask + 1)) == 0)) {
                const ExprPtr &valueExpression = expression->left;
                auto lower = relationCondition(integerLiteral(0, expression->pos), "<=", valueExpression, expression->pos);
                auto lowerReversed = relationCondition(valueExpression, ">=", integerLiteral(0, expression->pos), expression->pos);
                auto upper = relationCondition(valueExpression, "<", integerLiteral(*mask + 1, expression->pos), expression->pos);
                auto upperInclusive = relationCondition(valueExpression, "<=", integerLiteral(*mask, expression->pos), expression->pos);
                if ((proven(lower) || proven(lowerReversed)) && (proven(upper) || proven(upperInclusive))) {
                    Value value = dereference(evalExpr(valueExpression));
                    if (value.kind != ValueKind::Integer) {
                        fail("E1202", expression->pos, "ExactEq のビットマスク最適化には Int が必要です");
                    }
                    recordExactRewrite("bounded integer mask -> identity", expression->pos);
                    return value;
                }
            }
        }

        // Fold a fully known, effect-free expression including pure standard
        // library calls.  This is intentionally after algebraic identities so
        // explain mode reports the more useful rule for x + 0, x & 0, etc.
        const bool compoundExpression = expression->kind == ExprKind::Array ||
                                         expression->kind == ExprKind::Object ||
                                         expression->kind == ExprKind::Unary ||
                                         expression->kind == ExprKind::Binary ||
                                         expression->kind == ExprKind::Member ||
                                         expression->kind == ExprKind::Index ||
                                         expression->kind == ExprKind::Call;
        if (compoundExpression && isCompileTimePureExpression(expression)) {
            if (auto value = staticExpr(expression)) {
                recordRewrite(RewriteProofKind::ExactEq, "compile-time constant fold",
                              "Kond.Draft0.2", expression->pos);
                return *value;
            }
        }

        return std::nullopt;
    }

    Value evalExpr(const ExprPtr &expression) {
        if (!expression) return Value::null();
        if (auto rewritten = tryRegisteredRewrite(expression)) return *rewritten;
        if (auto rewritten = tryExactStandardRewrite(expression)) return *rewritten;
        if (auto rewritten = tryExactArithmeticRewrite(expression)) return *rewritten;
        switch (expression->kind) {
        case ExprKind::Literal:
            if (expression->literalKind == LiteralKind::Integer) return Value::integerValue(std::stoll(expression->text));
            if (expression->literalKind == LiteralKind::Float) return Value::floatValue(std::stod(expression->text));
            return expression->text.find('\x1f') == std::string::npos
                       ? Value::stringValue(expression->text)
                       : evalTaggedString(expression);
        case ExprKind::Variable:
            if (expression->text == "database") return Value::builtinValue("database");
            if (expression->text == "std") return Value::builtinValue("std");
            return env_.require(expression->text, expression->pos)->value;
        case ExprKind::Array: {
            std::vector<Value> values;
            for (const ExprPtr &item : expression->items) values.push_back(evalExpr(item));
            return Value::arrayValue(std::move(values));
        }
        case ExprKind::Object: {
            std::map<std::string, Value> fields;
            for (const auto &field : expression->fields) fields.emplace(field.first, evalExpr(field.second));
            return Value::objectValue(std::move(fields));
        }
        case ExprKind::Unary:
            return applyUnary(expression->op, evalExpr(expression->left), expression->pos);
        case ExprKind::Binary:
            return applyBinary(expression->op, evalExpr(expression->left), evalExpr(expression->right), expression->pos);
        case ExprKind::Member: {
            Value object = evalExpr(expression->object);
            Value field = memberValue(object, expression->text);
            if (field.kind != ValueKind::Null || dereference(object).kind == ValueKind::Object) return field;
            return Value::methodValue(std::move(object), expression->text);
        }
        case ExprKind::Index:
            return indexValue(evalExpr(expression->object), evalExpr(expression->index), expression->pos);
        case ExprKind::Call:
            return evalCall(expression);
        case ExprKind::Lambda:
            return Value::lambdaValue(expression);
        case ExprKind::ConditionValue:
            return Value::conditionValue(captureCondition(expression->condition));
        case ExprKind::Move: {
            if (expression->left->kind != ExprKind::Variable) fail("E2102", expression->pos, "move の対象は束縛名である必要があります");
            auto binding = env_.require(expression->left->text, expression->left->pos);
            if (binding->sharedBorrows != 0 || binding->uniqueBorrow) fail("E2203", expression->pos, "借用中の値は move できません");
            Value result = binding->value;
            binding->moved = true;
            ++binding->version;
            if (auto ownershipLog = binding->ownershipLog.lock()) {
                ownershipLog->record(binding->name, "Own -> Moved", expression->pos);
            }
            return result;
        }
        case ExprKind::Borrow: {
            if (expression->left->kind != ExprKind::Variable) fail("E2201", expression->pos, "借用の対象は束縛名である必要があります");
            auto binding = env_.require(expression->left->text, expression->left->pos);
            if (expression->mutableBorrow) {
                if (binding->sharedBorrows != 0 || binding->uniqueBorrow) fail("E2203", expression->pos, "すでに借用されている値を可変借用できません");
                return Value::referenceValue(binding, BorrowKind::Unique, expression->pos);
            }
            if (binding->uniqueBorrow) fail("E2203", expression->pos, "可変借用中の値を共有借用できません");
            return Value::referenceValue(binding, BorrowKind::Shared, expression->pos);
        }
        }
        return Value::null();
    }

    Value evalCall(const ExprPtr &expression) {
        if (expression->callee->kind == ExprKind::Member) {
            const auto &member = expression->callee;
            Value receiver = evalExpr(member->object);
            std::vector<Value> args;
            for (const ExprPtr &arg : expression->items) args.push_back(evalExpr(arg));
            return callMethod(member->text, std::move(receiver), std::move(args), member->object,
                              expression->pos);
        }
        if (expression->callee->kind != ExprKind::Variable) {
            fail("E1301", expression->pos, "呼び出し対象が関数名ではありません");
        }
        const std::string name = expression->callee->text;
        if (program_.conditions.count(name) != 0) {
            const ConditionDef &definition = program_.conditions.at(name);
            if (definition.params.size() != expression->items.size()) {
                fail("E1302", expression->pos, "condition " + name + " の引数個数が不正です");
            }
            // Keep condition calls usable as values even when the condition
            // came from an explicitly loaded source library.  The parser can
            // know local condition declarations, but library declarations are
            // merged after the application has already been parsed.
            auto condition = Condition::make(ConditionKind::Call, expression->pos);
            condition->predicate = name;
            condition->args = expression->items;
            return Value::conditionValue(captureCondition(std::move(condition)));
        }
        std::vector<Value> args;
        for (const ExprPtr &arg : expression->items) args.push_back(evalExpr(arg));
        if (program_.functions.count(name) != 0) {
            return callFunction(program_.functions.at(name), std::move(args), expression->pos, expression->items);
        }
        return callBuiltin(name, std::move(args), expression->pos, false);
    }

    Value callStdLibrary(const std::string &namespaceName, const std::string &name,
                        const std::vector<Value> &args, const SourcePos &pos, bool staticCall) {
        std::uint32_t argsFlow = FlowPublic;
        for (const Value &arg : args) argsFlow |= arg.flow;
        const auto withFlow = [&](Value result) {
            result.flow |= argsFlow;
            return result;
        };
        const auto requireCount = [&](std::size_t count, const std::string &signature) {
            if (args.size() != count) fail("E1302", pos, signature);
        };
        const auto stringArg = [&](std::size_t index, const std::string &signature) {
            if (index >= args.size() || dereference(args[index]).kind != ValueKind::String) {
                fail("E1303", pos, signature + " の対象はStringである必要があります");
            }
            return valueToString(args[index]);
        };
        const auto integerArg = [&](std::size_t index, const std::string &signature) {
            if (index >= args.size() || dereference(args[index]).kind != ValueKind::Integer) {
                fail("E1303", pos, signature + " の対象はIntである必要があります");
            }
            return dereference(args[index]).integer;
        };
        const auto numberArg = [&](std::size_t index, const std::string &signature) {
            const Value value = index < args.size() ? dereference(args[index]) : Value::null();
            if (!value.isNumber()) fail("E1303", pos, signature + " の対象はNumberである必要があります");
            return value.kind == ValueKind::Integer ? static_cast<double>(value.integer) : value.floating;
        };
        const auto listArg = [&](std::size_t index, const std::string &signature) {
            if (index >= args.size() || dereference(args[index]).kind != ValueKind::Array ||
                !dereference(args[index]).array) {
                fail("E1303", pos, signature + " の対象はListである必要があります");
            }
            return dereference(args[index]);
        };
        const auto objectArg = [&](std::size_t index, const std::string &signature) {
            if (index >= args.size() || dereference(args[index]).kind != ValueKind::Object ||
                !dereference(args[index]).object) {
                fail("E1303", pos, signature + " の対象はObjectである必要があります");
            }
            return dereference(args[index]);
        };
        const auto boolValue = [&](bool value) { return withFlow(Value::booleanValue(value)); };
        const auto stringValue = [&](std::string value) {
            return withFlow(Value::stringValue(std::move(value)));
        };

        if (namespaceName == "std.core") {
            if (name == "clone") {
                requireCount(1, "std.core.clone は引数を1つ取ります");
                return withFlow(cloneDeep(dereference(args.front())));
            }
            if (name == "type_of") {
                requireCount(1, "std.core.type_of は引数を1つ取ります");
                return callBuiltin("type", args, pos, staticCall);
            }
            if (name == "to_string") {
                requireCount(1, "std.core.to_string は引数を1つ取ります");
                return stringValue(valueToString(args.front()));
            }
            if (name == "coalesce") {
                requireCount(2, "std.core.coalesce は引数を2つ取ります");
                const Value first = dereference(args[0]);
                Value result = first.kind == ValueKind::Null ? args[1] : args[0];
                result.flow |= argsFlow;
                return result;
            }
            if (name == "range") {
                if (args.size() != 2 && args.size() != 3) {
                    fail("E1302", pos, "std.core.range は start, end[, step] を取ります");
                }
                const std::int64_t start = integerArg(0, "std.core.range");
                const std::int64_t end = integerArg(1, "std.core.range");
                const std::int64_t step = args.size() == 3 ? integerArg(2, "std.core.range") : 1;
                if (step == 0) fail("E1303", pos, "std.core.range の step は0ではいけません");
                std::vector<Value> values;
                std::int64_t current = start;
                while ((step > 0 && current < end) || (step < 0 && current > end)) {
                    values.push_back(Value::integerValue(current));
                    std::int64_t next = 0;
                    if (checkedAdd(current, step, next)) break;
                    current = next;
                    if (values.size() > 1000000) fail("E1303", pos, "std.core.range の要素数が上限を超えています");
                }
                return withFlow(Value::arrayValue(std::move(values)));
            }
            if (name == "repeat") {
                requireCount(2, "std.core.repeat は value, count の2引数を取ります");
                const std::int64_t count = integerArg(1, "std.core.repeat");
                if (count < 0 || count > 1000000) fail("E1303", pos, "std.core.repeat の count が範囲外です");
                std::vector<Value> values;
                values.reserve(static_cast<std::size_t>(count));
                for (std::int64_t i = 0; i < count; ++i) values.push_back(cloneDeep(dereference(args[0])));
                return withFlow(Value::arrayValue(std::move(values)));
            }
        }

        if (namespaceName == "std.math") {
            if (name == "abs" || name == "sqrt") return callBuiltin(name, args, pos, staticCall);
            if (name == "floor" || name == "ceil" || name == "round") {
                requireCount(1, "std.math." + name + " は引数を1つ取ります");
                const Value value = dereference(args.front());
                if (!value.isNumber()) fail("E1303", pos, "std.math." + name + " の対象はNumberです");
                if (value.kind == ValueKind::Integer) return withFlow(value);
                double result = value.floating;
                if (name == "floor") result = std::floor(result);
                if (name == "ceil") result = std::ceil(result);
                if (name == "round") result = std::round(result);
                return withFlow(Value::floatValue(result));
            }
            if (name == "pow") {
                requireCount(2, "std.math.pow は引数を2つ取ります");
                return withFlow(Value::floatValue(std::pow(numberArg(0, "std.math.pow"),
                                                           numberArg(1, "std.math.pow"))));
            }
            if (name == "min" || name == "max") {
                if (args.empty()) fail("E1302", pos, "std.math." + name + " は1つ以上の引数を取ります");
                bool allIntegers = true;
                Value selected = dereference(args.front());
                if (!selected.isNumber()) fail("E1303", pos, "std.math." + name + " の対象はNumberです");
                for (std::size_t i = 1; i < args.size(); ++i) {
                    const Value value = dereference(args[i]);
                    if (!value.isNumber()) fail("E1303", pos, "std.math." + name + " の対象はNumberです");
                    allIntegers = allIntegers && value.kind == ValueKind::Integer;
                    auto comparison = compareValues(value, selected, name == "min" ? "<" : ">");
                    if (comparison && *comparison) selected = value;
                }
                if (allIntegers) return withFlow(Value::integerValue(selected.integer));
                return withFlow(Value::floatValue(selected.kind == ValueKind::Integer
                                                       ? static_cast<double>(selected.integer)
                                                       : selected.floating));
            }
            if (name == "clamp") {
                requireCount(3, "std.math.clamp は value, lower, upper の3引数を取ります");
                const Value value = dereference(args[0]);
                const Value lower = dereference(args[1]);
                const Value upper = dereference(args[2]);
                if (!value.isNumber() || !lower.isNumber() || !upper.isNumber()) {
                    fail("E1303", pos, "std.math.clamp の引数はNumberである必要があります");
                }
                if (!compareValues(lower, upper, "<=").value_or(false)) {
                    fail("E1303", pos, "std.math.clamp の lower は upper 以下である必要があります");
                }
                if (compareValues(value, lower, "<").value_or(false)) return withFlow(lower);
                if (compareValues(upper, value, "<").value_or(false)) return withFlow(upper);
                return withFlow(value);
            }
            if (name == "sign") {
                requireCount(1, "std.math.sign は引数を1つ取ります");
                const double value = numberArg(0, "std.math.sign");
                return withFlow(Value::integerValue(value < 0.0 ? -1 : value > 0.0 ? 1 : 0));
            }
            if (name == "gcd" || name == "lcm") {
                requireCount(2, "std.math." + name + " は引数を2つ取ります");
                const std::int64_t left = integerArg(0, "std.math." + name);
                const std::int64_t right = integerArg(1, "std.math." + name);
                if (left == std::numeric_limits<std::int64_t>::min() ||
                    right == std::numeric_limits<std::int64_t>::min()) {
                    fail("E1208", pos, "std.math." + name + " の絶対値が範囲外です");
                }
                const std::int64_t gcd = std::gcd(left < 0 ? -left : left, right < 0 ? -right : right);
                if (name == "gcd") return withFlow(Value::integerValue(gcd));
                if (gcd == 0) return withFlow(Value::integerValue(0));
                const std::int64_t quotient = left / gcd;
                std::int64_t result = 0;
                if (checkedMul(quotient, right, result)) {
                    fail("E1208", pos, "std.math.lcm がオーバーフローします");
                }
                return withFlow(Value::integerValue(result < 0 ? -result : result));
            }
        }

        if (namespaceName == "std.string") {
            if (name == "length") {
                requireCount(1, "std.string.length は引数を1つ取ります");
                return withFlow(Value::integerValue(static_cast<std::int64_t>(stringArg(0, "std.string.length").size())));
            }
            if (name == "trim") {
                requireCount(1, "std.string.trim は引数を1つ取ります");
                std::string value = stringArg(0, "std.string.trim");
                const auto whitespace = [](unsigned char ch) {
                    return ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\f' || ch == '\v';
                };
                std::size_t begin = 0;
                while (begin < value.size() && whitespace(static_cast<unsigned char>(value[begin]))) ++begin;
                std::size_t end = value.size();
                while (end > begin && whitespace(static_cast<unsigned char>(value[end - 1]))) --end;
                return stringValue(value.substr(begin, end - begin));
            }
            if (name == "uppercase" || name == "lowercase") {
                requireCount(1, "std.string." + name + " は引数を1つ取ります");
                std::string value = stringArg(0, "std.string." + name);
                for (char &ch : value) {
                    if (name == "uppercase" && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
                    if (name == "lowercase" && ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
                }
                return stringValue(std::move(value));
            }
            if (name == "contains" || name == "starts_with" || name == "ends_with") {
                requireCount(2, "std.string." + name + " はString引数を2つ取ります");
                const std::string value = stringArg(0, "std.string." + name);
                const std::string needle = stringArg(1, "std.string." + name);
                if (name == "contains") return boolValue(value.find(needle) != std::string::npos);
                if (name == "starts_with") return boolValue(value.rfind(needle, 0) == 0);
                return boolValue(needle.size() <= value.size() &&
                                 value.compare(value.size() - needle.size(), needle.size(), needle) == 0);
            }
            if (name == "split") {
                requireCount(2, "std.string.split は value, separator の2引数を取ります");
                const std::string value = stringArg(0, "std.string.split");
                const std::string separator = stringArg(1, "std.string.split");
                if (separator.empty()) fail("E1303", pos, "std.string.split の separator は空であってはいけません");
                std::vector<Value> result;
                std::size_t begin = 0;
                while (true) {
                    const std::size_t at = value.find(separator, begin);
                    result.push_back(Value::stringValue(value.substr(begin, at == std::string::npos ? at : at - begin)));
                    if (at == std::string::npos) break;
                    begin = at + separator.size();
                }
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "join") {
                requireCount(2, "std.string.join は list, separator の2引数を取ります");
                const Value list = listArg(0, "std.string.join");
                const std::string separator = stringArg(1, "std.string.join");
                std::string result;
                for (std::size_t i = 0; i < list.array->size(); ++i) {
                    if (i != 0) result += separator;
                    result += valueToString((*list.array)[i]);
                }
                return stringValue(std::move(result));
            }
            if (name == "replace") {
                requireCount(3, "std.string.replace は value, old, new の3引数を取ります");
                std::string value = stringArg(0, "std.string.replace");
                const std::string oldValue = stringArg(1, "std.string.replace");
                const std::string newValue = stringArg(2, "std.string.replace");
                if (oldValue.empty()) fail("E1303", pos, "std.string.replace の old は空であってはいけません");
                std::size_t at = 0;
                while ((at = value.find(oldValue, at)) != std::string::npos) {
                    value.replace(at, oldValue.size(), newValue);
                    at += newValue.size();
                }
                return stringValue(std::move(value));
            }
            if (name == "substring") {
                if (args.size() != 2 && args.size() != 3) fail("E1302", pos, "std.string.substring は2または3引数を取ります");
                const std::string value = stringArg(0, "std.string.substring");
                const std::int64_t start = integerArg(1, "std.string.substring");
                if (start < 0 || static_cast<std::size_t>(start) > value.size()) fail("E1204", pos, "substring の start が範囲外です");
                std::size_t length = value.size() - static_cast<std::size_t>(start);
                if (args.size() == 3) {
                    const std::int64_t requested = integerArg(2, "std.string.substring");
                    if (requested < 0) fail("E1303", pos, "substring の length は0以上である必要があります");
                    length = std::min(length, static_cast<std::size_t>(requested));
                }
                return stringValue(value.substr(static_cast<std::size_t>(start), length));
            }
            if (name == "char_at") {
                requireCount(2, "std.string.char_at は value, index の2引数を取ります");
                const std::string value = stringArg(0, "std.string.char_at");
                const std::int64_t index = integerArg(1, "std.string.char_at");
                if (index < 0 || static_cast<std::size_t>(index) >= value.size()) return withFlow(Value::null());
                return stringValue(std::string(1, value[static_cast<std::size_t>(index)]));
            }
            if (name == "repeat") {
                requireCount(2, "std.string.repeat は value, count の2引数を取ります");
                const std::string value = stringArg(0, "std.string.repeat");
                const std::int64_t count = integerArg(1, "std.string.repeat");
                if (count < 0 || count > 1000000) fail("E1303", pos, "std.string.repeat の count が範囲外です");
                if (count > 0 && value.size() > std::numeric_limits<std::size_t>::max() /
                                      static_cast<std::size_t>(count)) {
                    fail("E1303", pos, "std.string.repeat の結果が大きすぎます");
                }
                std::string result;
                result.reserve(value.size() * static_cast<std::size_t>(count));
                for (std::int64_t i = 0; i < count; ++i) result += value;
                return stringValue(std::move(result));
            }
            if (name == "reverse") {
                requireCount(1, "std.string.reverse は引数を1つ取ります");
                std::string result = stringArg(0, "std.string.reverse");
                std::reverse(result.begin(), result.end());
                return stringValue(std::move(result));
            }
            if (name == "parse_int") {
                if (args.size() != 1 && args.size() != 2) fail("E1302", pos, "std.string.parse_int は1または2引数を取ります");
                const std::string value = stringArg(0, "std.string.parse_int");
                const int base = args.size() == 2 ? static_cast<int>(integerArg(1, "std.string.parse_int")) : 10;
                if (base < 2 || base > 36) fail("E1303", pos, "parse_int の base は2から36です");
                try {
                    std::size_t consumed = 0;
                    const std::int64_t result = std::stoll(value, &consumed, base);
                    if (consumed != value.size()) throw std::invalid_argument("trailing");
                    return withFlow(Value::integerValue(result));
                } catch (const std::exception &) {
                    fail("E1303", pos, "std.string.parse_int の入力が不正です");
                }
            }
            if (name == "parse_float") {
                requireCount(1, "std.string.parse_float は引数を1つ取ります");
                const std::string value = stringArg(0, "std.string.parse_float");
                try {
                    std::size_t consumed = 0;
                    const double result = std::stod(value, &consumed);
                    if (consumed != value.size() || !std::isfinite(result)) throw std::invalid_argument("invalid");
                    return withFlow(Value::floatValue(result));
                } catch (const std::exception &) {
                    fail("E1303", pos, "std.string.parse_float の入力が不正です");
                }
            }
        }

        if (namespaceName == "std.list") {
            if (name == "all" || name == "any" || name == "none") {
                const Truth result = evaluateStdPredicateValues("std.list." + name, args, pos);
                if (result == Truth::Unknown) fail("E1206", pos, "std.list." + name + " を判定できません");
                return boolValue(result == Truth::True);
            }
            if (name == "length") {
                requireCount(1, "std.list.length は引数を1つ取ります");
                const Value list = listArg(0, "std.list.length");
                return withFlow(Value::integerValue(static_cast<std::int64_t>(list.array->size())));
            }
            if (name == "first" || name == "last") {
                requireCount(1, "std.list." + name + " は引数を1つ取ります");
                const Value list = listArg(0, "std.list." + name);
                if (list.array->empty()) return withFlow(Value::null());
                Value result = (*list.array)[name == "first" ? 0 : list.array->size() - 1];
                result.flow |= list.flow;
                return result;
            }
            if (name == "get") {
                if (args.size() != 2 && args.size() != 3) fail("E1302", pos, "std.list.get は2または3引数を取ります");
                const Value list = listArg(0, "std.list.get");
                const std::int64_t index = integerArg(1, "std.list.get");
                if (index >= 0 && static_cast<std::size_t>(index) < list.array->size()) {
                    Value result = (*list.array)[static_cast<std::size_t>(index)];
                    result.flow |= list.flow;
                    return result;
                }
                if (args.size() == 3) {
                    Value result = args[2];
                    result.flow |= argsFlow;
                    return result;
                }
                return withFlow(Value::null());
            }
            if (name == "slice") {
                requireCount(3, "std.list.slice は list, start, end の3引数を取ります");
                const Value list = listArg(0, "std.list.slice");
                const std::int64_t start = integerArg(1, "std.list.slice");
                const std::int64_t end = integerArg(2, "std.list.slice");
                if (start < 0 || end < start || static_cast<std::size_t>(start) > list.array->size()) {
                    fail("E1204", pos, "std.list.slice の範囲が不正です");
                }
                const std::size_t finish = std::min(static_cast<std::size_t>(end), list.array->size());
                std::vector<Value> result(list.array->begin() + start, list.array->begin() + finish);
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "append" || name == "prepend") {
                requireCount(2, "std.list." + name + " は list, value の2引数を取ります");
                const Value list = listArg(0, "std.list." + name);
                std::vector<Value> result = *list.array;
                if (name == "append") result.push_back(args[1]);
                else result.insert(result.begin(), args[1]);
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "concat") {
                if (args.empty()) fail("E1302", pos, "std.list.concat は1つ以上のListを取ります");
                std::vector<Value> result;
                for (std::size_t i = 0; i < args.size(); ++i) {
                    const Value list = listArg(i, "std.list.concat");
                    result.insert(result.end(), list.array->begin(), list.array->end());
                }
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "reverse") {
                requireCount(1, "std.list.reverse は引数を1つ取ります");
                const Value list = listArg(0, "std.list.reverse");
                std::vector<Value> result = *list.array;
                std::reverse(result.begin(), result.end());
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "contains") {
                requireCount(2, "std.list.contains は list, value の2引数を取ります");
                const Value list = listArg(0, "std.list.contains");
                for (const Value &item : *list.array) if (valuesEqual(item, args[1])) return boolValue(true);
                return boolValue(false);
            }
            if (name == "filter" || name == "count_if" || name == "find") {
                requireCount(2, "std.list." + name + " は list, predicate の2引数を取ります");
                const Value list = listArg(0, "std.list." + name);
                if (dereference(args[1]).kind != ValueKind::Lambda) {
                    fail("E1303", pos, "std.list." + name + " の predicate はLambdaである必要があります");
                }
                std::vector<Value> filtered;
                std::int64_t count = 0;
                for (const Value &item : *list.array) {
                    const Truth result = evaluateLambdaCondition(args[1], item, pos);
                    if (result == Truth::Unknown) fail("E1206", pos, "std.list の predicate を判定できません");
                    if (result != Truth::True) continue;
                    ++count;
                    if (name == "filter") filtered.push_back(item);
                    if (name == "find") {
                        Value result = item;
                        result.flow |= list.flow;
                        return result;
                    }
                }
                if (name == "filter") return withFlow(Value::arrayValue(std::move(filtered)));
                if (name == "count_if") return withFlow(Value::integerValue(count));
                return withFlow(Value::null());
            }
            if (name == "sum") {
                requireCount(1, "std.list.sum は引数を1つ取ります");
                const Value list = listArg(0, "std.list.sum");
                bool floating = false;
                std::int64_t integerSum = 0;
                double floatSum = 0.0;
                for (const Value &item : *list.array) {
                    const Value value = dereference(item);
                    if (!value.isNumber()) fail("E1303", pos, "std.list.sum の要素はNumberである必要があります");
                    if (value.kind == ValueKind::Integer && !floating) {
                        if (checkedAdd(integerSum, value.integer, integerSum)) fail("E1208", pos, "std.list.sum がオーバーフローします");
                    } else {
                        if (!floating) {
                            floatSum = static_cast<double>(integerSum);
                            floating = true;
                        }
                        floatSum += value.kind == ValueKind::Integer ? static_cast<double>(value.integer) : value.floating;
                    }
                }
                return floating ? withFlow(Value::floatValue(floatSum)) : withFlow(Value::integerValue(integerSum));
            }
            if (name == "sort" || name == "sort_desc") {
                requireCount(1, "std.list." + name + " は引数を1つ取ります");
                const Value list = listArg(0, "std.list." + name);
                std::vector<Value> result = *list.array;
                const bool descending = name == "sort_desc";
                std::stable_sort(result.begin(), result.end(), [&](const Value &left, const Value &right) {
                    auto less = compareValues(left, right, descending ? ">" : "<");
                    return less.value_or(false);
                });
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "zip") {
                requireCount(2, "std.list.zip は2つのListを取ります");
                const Value left = listArg(0, "std.list.zip");
                const Value right = listArg(1, "std.list.zip");
                const std::size_t count = std::min(left.array->size(), right.array->size());
                std::vector<Value> result;
                result.reserve(count);
                for (std::size_t i = 0; i < count; ++i) {
                    result.push_back(Value::arrayValue({(*left.array)[i], (*right.array)[i]}));
                }
                return withFlow(Value::arrayValue(std::move(result)));
            }
        }

        if (namespaceName == "std.map") {
            if (name == "length" || name == "size") {
                requireCount(1, "std.map." + name + " は引数を1つ取ります");
                const Value object = objectArg(0, "std.map." + name);
                return withFlow(Value::integerValue(static_cast<std::int64_t>(object.object->size())));
            }
            if (name == "has") {
                requireCount(2, "std.map.has は object, key の2引数を取ります");
                const Value object = objectArg(0, "std.map.has");
                const Value key = dereference(args[1]);
                if (key.kind != ValueKind::String) fail("E1303", pos, "std.map.has の key はStringです");
                return boolValue(object.object->count(key.string) != 0);
            }
            if (name == "get" || name == "get_or") {
                if ((name == "get" && args.size() != 2) || (name == "get_or" && args.size() != 3)) {
                    fail("E1302", pos, "std.map." + name + " の引数個数が不正です");
                }
                const Value object = objectArg(0, "std.map." + name);
                const Value key = dereference(args[1]);
                if (key.kind != ValueKind::String) fail("E1303", pos, "std.map." + name + " の key はStringです");
                auto found = object.object->find(key.string);
                if (found != object.object->end()) {
                    Value result = found->second;
                    result.flow |= argsFlow;
                    return result;
                }
                if (name == "get_or") {
                    Value result = args[2];
                    result.flow |= argsFlow;
                    return result;
                }
                return withFlow(Value::null());
            }
            if (name == "keys" || name == "values" || name == "entries") {
                requireCount(1, "std.map." + name + " は引数を1つ取ります");
                const Value object = objectArg(0, "std.map." + name);
                std::vector<Value> result;
                for (const auto &entry : *object.object) {
                    if (name == "keys") result.push_back(Value::stringValue(entry.first));
                    else if (name == "values") result.push_back(entry.second);
                    else result.push_back(Value::objectValue({{"key", Value::stringValue(entry.first)}, {"value", entry.second}}));
                }
                return withFlow(Value::arrayValue(std::move(result)));
            }
            if (name == "put" || name == "remove") {
                if ((name == "put" && args.size() != 3) || (name == "remove" && args.size() != 2)) {
                    fail("E1302", pos, "std.map." + name + " の引数個数が不正です");
                }
                const Value object = objectArg(0, "std.map." + name);
                const Value key = dereference(args[1]);
                if (key.kind != ValueKind::String) fail("E1303", pos, "std.map." + name + " の key はStringです");
                std::map<std::string, Value> result = *object.object;
                if (name == "put") result[key.string] = args[2];
                else result.erase(key.string);
                return withFlow(Value::objectValue(std::move(result)));
            }
            if (name == "merge") {
                requireCount(2, "std.map.merge は2つのObjectを取ります");
                const Value left = objectArg(0, "std.map.merge");
                const Value right = objectArg(1, "std.map.merge");
                std::map<std::string, Value> result = *left.object;
                for (const auto &entry : *right.object) result[entry.first] = entry.second;
                return withFlow(Value::objectValue(std::move(result)));
            }
        }

        if (namespaceName == "std.pred") {
            static const std::unordered_set<std::string> predicates{
                "is_null", "is_int", "is_float", "is_number", "is_bool", "is_string", "is_list",
                "is_object", "is_finite", "equal", "not_equal", "positive", "nonnegative", "negative",
                "even", "odd", "in_range", "contains", "has_key", "is_empty", "all", "any", "none"
            };
            if (predicates.count(name) != 0) {
                const Truth result = evaluateStdPredicateValues("std.pred." + name, args, pos);
                if (result == Truth::Unknown) fail("E1206", pos, "std.pred." + name + " を判定できません");
                return boolValue(result == Truth::True);
            }
        }

        if (namespaceName == "std.json") {
            if (name == "parse") {
                requireCount(1, "std.json.parse は引数を1つ取ります");
                const std::string text = stringArg(0, "std.json.parse");
                Value result = JsonParser(text, pos).parse();
                result.flow |= argsFlow;
                return result;
            }
            if (name == "stringify" || name == "pretty") {
                requireCount(1, "std.json." + name + " は引数を1つ取ります");
                Value result = Value::stringValue("json\x1f" + jsonSerialize(args.front()));
                result.flow = argsFlow;
                return result;
            }
            if (name == "is_valid") {
                requireCount(1, "std.json.is_valid は引数を1つ取ります");
                const std::string text = stringArg(0, "std.json.is_valid");
                try {
                    (void)JsonParser(text, pos).parse();
                    return boolValue(true);
                } catch (const KondError &) {
                    return boolValue(false);
                }
            }
        }

        if (namespaceName == "std.url") {
            if (name == "encode_component") {
                requireCount(1, "std.url.encode_component は引数を1つ取ります");
                Value result = stringValue(encodeUrl(stringArg(0, "std.url.encode_component")));
                result.safety |= SafetyUrlComponent;
                return result;
            }
            if (name == "decode_component") {
                requireCount(1, "std.url.decode_component は引数を1つ取ります");
                const std::string input = stringArg(0, "std.url.decode_component");
                std::string output;
                auto hex = [](char ch) -> int {
                    if (ch >= '0' && ch <= '9') return ch - '0';
                    if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                    if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                    return -1;
                };
                for (std::size_t i = 0; i < input.size(); ++i) {
                    if (input[i] != '%') {
                        output.push_back(input[i]);
                        continue;
                    }
                    if (i + 2 >= input.size()) fail("E1303", pos, "std.url.decode_component の % エスケープが不正です");
                    const int high = hex(input[i + 1]);
                    const int low = hex(input[i + 2]);
                    if (high < 0 || low < 0) fail("E1303", pos, "std.url.decode_component の % エスケープが不正です");
                    output.push_back(static_cast<char>((high << 4) | low));
                    i += 2;
                }
                return stringValue(std::move(output));
            }
            if (name == "parse_query") {
                requireCount(1, "std.url.parse_query は引数を1つ取ります");
                const std::string query = stringArg(0, "std.url.parse_query");
                const auto decode = [&](const std::string &input) {
                    std::string output;
                    auto hex = [](char ch) -> int {
                        if (ch >= '0' && ch <= '9') return ch - '0';
                        if (ch >= 'a' && ch <= 'f') return ch - 'a' + 10;
                        if (ch >= 'A' && ch <= 'F') return ch - 'A' + 10;
                        return -1;
                    };
                    for (std::size_t i = 0; i < input.size(); ++i) {
                        if (input[i] == '+') {
                            output.push_back(' ');
                            continue;
                        }
                        if (input[i] != '%') {
                            output.push_back(input[i]);
                            continue;
                        }
                        if (i + 2 >= input.size()) fail("E1303", pos, "std.url.parse_query の % エスケープが不正です");
                        const int high = hex(input[i + 1]);
                        const int low = hex(input[i + 2]);
                        if (high < 0 || low < 0) fail("E1303", pos, "std.url.parse_query の % エスケープが不正です");
                        output.push_back(static_cast<char>((high << 4) | low));
                        i += 2;
                    }
                    return output;
                };
                std::map<std::string, Value> fields;
                std::size_t begin = 0;
                while (begin <= query.size()) {
                    const std::size_t end = query.find('&', begin);
                    const std::string item = query.substr(begin, end == std::string::npos ? end : end - begin);
                    const std::size_t separator = item.find('=');
                    const std::string key = item.substr(0, separator);
                    const std::string value = separator == std::string::npos ? std::string{} : item.substr(separator + 1);
                    if (!key.empty()) {
                        fields[decode(key)] = Value::stringValue(decode(value));
                    }
                    if (end == std::string::npos) break;
                    begin = end + 1;
                }
                return withFlow(Value::objectValue(std::move(fields)));
            }
        }

        if (namespaceName == "std.html") {
            if (name == "escape" || name == "escape_text" || name == "escape_attribute") {
                requireCount(1, "std.html." + name + " は引数を1つ取ります");
                Value result = Value::stringValue(escapeHtml(stringArg(0, "std.html." + name)));
                result.flow = argsFlow;
                result.safety |= SafetyHtmlText;
                return result;
            }
        }

        if (namespaceName == "std.security") {
            if (name == "untrusted" || name == "secret" || name == "personal") {
                return callBuiltin(name, args, pos, staticCall);
            }
            if (name == "escape_html" || name == "encode_url" || name == "parameterize_sql") {
                const std::string builtin = name == "escape_html" ? "escape_html_text" :
                                            name == "encode_url" ? "encode_url_component" : "parameterize_sql";
                return callBuiltin(builtin, args, pos, staticCall);
            }
            if (name == "flow_label") return callBuiltin("flow_label", args, pos, staticCall);
            if (name == "declassify" || name == "endorse") return callBuiltin(name, args, pos, staticCall);
        }

        if (namespaceName == "std.http") {
            if (name == "response" || name == "json_response") return callBuiltin(name, args, pos, staticCall);
        }

        if (namespaceName == "std.io") {
            if (name == "read_line") return callBuiltin("input", args, pos, staticCall);
            if (name == "print" || name == "println") {
                if (name == "print") {
                    for (std::size_t i = 0; i < args.size(); ++i) {
                        if (i != 0) std::cout << ' ';
                        std::cout << valueToString(args[i]);
                    }
                    std::cout.flush();
                } else {
                    callBuiltin("print", args, pos, staticCall);
                }
                return Value::null();
            }
        }

        if (namespaceName == "std.opt") {
            if (name == "rule_count") {
                requireCount(0, "std.opt.rule_count は引数を取りません");
                return Value::integerValue(static_cast<std::int64_t>(program_.optimizationRules.size()));
            }
            if (name == "rule_names") {
                requireCount(0, "std.opt.rule_names は引数を取りません");
                std::vector<Value> result;
                for (const OptimizationRule &rule : program_.optimizationRules) result.push_back(Value::stringValue(rule.name));
                return Value::arrayValue(std::move(result));
            }
        }

        fail("E1304", pos, "未定義の標準ライブラリ関数です: " + namespaceName + "." + name);
    }

    Value callBuiltin(const std::string &name, const std::vector<Value> &args, const SourcePos &pos, bool staticCall) {
        std::uint32_t argsFlow = FlowPublic;
        for (const Value &arg : args) argsFlow |= arg.flow;
        const auto withArgsFlow = [&](Value result) {
            result.flow |= argsFlow;
            return result;
        };
        if (name == "print") {
            for (std::size_t i = 0; i < args.size(); ++i) {
                if (i != 0) std::cout << ' ';
                std::cout << valueToString(args[i]);
            }
            std::cout << '\n';
            return Value::null();
        }
        if (name == "len") {
            if (args.size() != 1) fail("E1302", pos, "len は引数を1つ取ります");
            const Value value = dereference(args[0]);
            if (value.kind == ValueKind::String) return withArgsFlow(Value::integerValue(static_cast<std::int64_t>(value.string.size())));
            if (value.kind == ValueKind::Array) return withArgsFlow(Value::integerValue(static_cast<std::int64_t>(value.array->size())));
            if (value.kind == ValueKind::Object) return withArgsFlow(Value::integerValue(static_cast<std::int64_t>(value.object->size())));
            fail("E1303", pos, "len の対象がコレクションではありません");
        }
        if (name == "abs") {
            if (args.size() != 1) fail("E1302", pos, "abs は引数を1つ取ります");
            const Value value = dereference(args[0]);
            if (value.kind == ValueKind::Integer) {
                if (value.integer == std::numeric_limits<std::int64_t>::min()) {
                    fail("E1208", pos, "Int64 の abs がオーバーフローします");
                }
                return withArgsFlow(Value::integerValue(value.integer < 0 ? -value.integer : value.integer));
            }
            if (value.kind == ValueKind::Float) return withArgsFlow(Value::floatValue(std::fabs(value.floating)));
            fail("E1303", pos, "abs の対象が数値ではありません");
        }
        if (name == "sqrt") {
            if (args.size() != 1) fail("E1302", pos, "sqrt は引数を1つ取ります");
            const Value value = dereference(args[0]);
            if (!value.isNumber()) fail("E1303", pos, "sqrt の対象が数値ではありません");
            const double number = value.kind == ValueKind::Integer ? static_cast<double>(value.integer) : value.floating;
            if (number < 0) fail("E1303", pos, "sqrt の引数は0以上である必要があります");
            return withArgsFlow(Value::floatValue(std::sqrt(number)));
        }
        if (name == "input") {
            if (staticCall) return Value::null();
            std::string line;
            if (!std::getline(std::cin, line)) return Value::null();
            Value result = Value::stringValue(std::move(line));
            result.flow = FlowUntrusted;
            return result;
        }
        if (name == "untrusted" || name == "secret" || name == "personal") {
            if (args.size() != 1) fail("E1302", pos, name + " は引数を1つ取ります");
            Value result = args.front();
            if (name == "untrusted") result.flow |= FlowUntrusted;
            if (name == "secret") result.flow |= FlowSecret;
            if (name == "personal") result.flow |= FlowPersonal;
            return result;
        }
        if (name == "escape_html_text" || name == "encode_url_component" || name == "parameterize_sql") {
            if (args.size() != 1) fail("E1302", pos, name + " は引数を1つ取ります");
            const Value input = dereference(args.front());
            Value result = args.front();
            if (input.kind != ValueKind::String && name != "parameterize_sql") {
                fail("E1303", pos, name + " の対象はStringである必要があります");
            }
            if (name == "escape_html_text") result = Value::stringValue(escapeHtml(valueToString(input)));
            if (name == "encode_url_component") result = Value::stringValue(encodeUrl(valueToString(input)));
            result.flow = args.front().flow;
            if (name == "escape_html_text") result.safety |= SafetyHtmlText;
            if (name == "encode_url_component") result.safety |= SafetyUrlComponent;
            if (name == "parameterize_sql") result.safety |= SafetySqlParameter;
            return result;
        }
        if (name == "declassify" || name == "endorse") {
            if (args.size() != 1) fail("E1302", pos, name + " は引数を1つ取ります");
            if (mode_ != Mode::Unsafe && !env_.unsafe()) {
                fail("E3104", pos, name + " は明示的な unsafe 信頼境界でのみ使用できます");
            }
            Value result = args.front();
            if (name == "declassify") result.flow &= ~(FlowSecret | FlowPersonal);
            if (name == "endorse") result.flow &= ~FlowUntrusted;
            return result;
        }
        if (name == "flow_label") {
            if (args.size() != 1) fail("E1302", pos, "flow_label は引数を1つ取ります");
            return Value::stringValue(flowLabelName(args.front().flow));
        }
        if (name == "html_emit") {
            if (args.size() != 1) fail("E1302", pos, "html_emit は引数を1つ取ります");
            if ((args.front().safety & SafetyHtmlText) == 0) {
                fail("E3201", pos, "HTML sink には HtmlText の証拠が必要です");
            }
            std::cout << valueToString(args.front()) << '\n';
            return Value::null();
        }
        if (name == "sql_exec") {
            if (args.size() != 1) fail("E1302", pos, "sql_exec は引数を1つ取ります");
            if ((args.front().safety & SafetySqlQuery) == 0) {
                fail("E3204", pos, "SQL sink には構造化 SqlQuery の証拠が必要です");
            }
            return Value::null();
        }
        if (name == "json_response") {
            if (args.size() != 1) fail("E1302", pos, "json_response は引数を1つ取ります");
            Value result = Value::stringValue("json\x1f" + jsonSerialize(args.front()));
            result.flow = args.front().flow;
            return result;
        }
        if (name == "http_response" || name == "response") {
            if (args.size() != 3) fail("E1302", pos, name + " は status, headers, body の3引数を取ります");
            const Value status = dereference(args[0]);
            const Value headers = dereference(args[1]);
            if (status.kind != ValueKind::Integer || status.integer < 100 || status.integer > 599) {
                fail("E1303", pos, "HTTP status は100以上599以下のIntである必要があります");
            }
            if (headers.kind != ValueKind::Object || !headers.object) {
                fail("E1303", pos, "HTTP headers はObjectである必要があります");
            }
            auto response = std::make_shared<HttpResponseData>();
            response->status = static_cast<int>(status.integer);
            response->body = args[2];
            for (const auto &header : *headers.object) {
                const Value value = dereference(header.second);
                if (value.kind != ValueKind::String) {
                    fail("E1303", pos, "HTTP header の値はStringである必要があります");
                }
                response->headers[header.first] = value;
            }
            Value result;
            result.kind = ValueKind::HttpResponse;
            result.flow = argsFlow;
            result.response = std::move(response);
            return result;
        }
        if (name == "type") {
            if (args.size() != 1) fail("E1302", pos, "type は引数を1つ取ります");
            const Value value = dereference(args[0]);
            switch (value.kind) {
            case ValueKind::Integer: return withArgsFlow(Value::stringValue("Int"));
            case ValueKind::Float: return withArgsFlow(Value::stringValue("Float"));
            case ValueKind::Boolean: return withArgsFlow(Value::stringValue("Bool"));
            case ValueKind::String: return withArgsFlow(Value::stringValue("String"));
            case ValueKind::Array: return withArgsFlow(Value::stringValue("List"));
            case ValueKind::Object: return withArgsFlow(Value::stringValue("Object"));
            case ValueKind::Null: return withArgsFlow(Value::stringValue("Null"));
            case ValueKind::HttpRequest: return withArgsFlow(Value::stringValue("HttpRequest"));
            case ValueKind::HttpResponse: return withArgsFlow(Value::stringValue("HttpResponse"));
            default: return withArgsFlow(Value::stringValue("Any"));
            }
        }
        fail("E1304", pos, "未定義の関数です: " + name);
    }

    Value callMethod(const std::string &name, Value receiver, std::vector<Value> args,
                     const ExprPtr &receiverExpr, const SourcePos &pos) {
        Value actual = dereference(receiver);
        if (actual.kind == ValueKind::Builtin && actual.name.rfind("std.", 0) == 0) {
            return callStdLibrary(actual.name, name, args, pos, false);
        }
        if (actual.kind == ValueKind::HttpRequest && actual.request) {
            if (name == "json") {
                if (!args.empty()) fail("E1302", pos, "req.json は引数を取りません");
                return JsonParser(actual.request->body, pos).parse();
            }
            if (name == "header" || name == "cookie") {
                if (args.size() != 1 || dereference(args.front()).kind != ValueKind::String) {
                    fail("E1302", pos, name + " はString引数を1つ取ります");
                }
                const std::string key = asciiLower(valueToString(args.front()));
                const auto &values = name == "header" ? actual.request->headers : actual.request->cookies;
                const auto found = values.find(key);
                if (found == values.end()) {
                    Value result = Value::null();
                    result.flow = actual.flow | FlowUntrusted;
                    return result;
                }
                return found->second;
            }
        }
        if (actual.kind == ValueKind::Builtin && actual.name == "database" && name == "query") {
            if (args.size() != 1) fail("E1302", pos, "database.query は引数を1つ取ります");
            if ((args.front().safety & SafetySqlQuery) == 0) {
                fail("E3204", pos, "database.query には構造化 SqlQuery の証拠が必要です");
            }
            return Value::null();
        }
        if (name == "push") {
            if (args.size() != 1) fail("E1302", pos, "push は引数を1つ取ります");
            mutateReceiver(receiverExpr, [&](Value &target) {
                if (target.kind != ValueKind::Array) fail("E1303", pos, "push の対象がListではありません");
                target.array->push_back(args.front());
                target.flow |= args.front().flow;
                if (strictIfc_) target.flow |= pcFlow_;
            });
            return Value::null();
        }
        if (name == "pop") {
            if (!args.empty()) fail("E1302", pos, "pop は引数を取りません");
            Value result = Value::null();
            mutateReceiver(receiverExpr, [&](Value &target) {
                if (target.kind != ValueKind::Array) fail("E1303", pos, "pop の対象がListではありません");
                if (!target.array->empty()) {
                    result = target.array->back();
                    target.array->pop_back();
                }
            });
            return result;
        }
        if (name == "uppercase" || name == "lowercase") {
            if (!args.empty()) fail("E1302", pos, name + " は引数を取りません");
            if (actual.kind != ValueKind::String) fail("E1303", pos, name + " の対象がStringではありません");
            std::string text = actual.string;
            for (char &ch : text) {
                if (name == "uppercase" && ch >= 'a' && ch <= 'z') ch = static_cast<char>(ch - 'a' + 'A');
                if (name == "lowercase" && ch >= 'A' && ch <= 'Z') ch = static_cast<char>(ch - 'A' + 'a');
            }
            Value result = Value::stringValue(std::move(text));
            result.flow = actual.flow;
            return result;
        }
        if (name == "get") {
            if (args.size() != 1) fail("E1302", pos, "get は引数を1つ取ります");
            return memberValue(actual, valueToString(args.front()));
        }
        if (actual.kind == ValueKind::Object && actual.object) {
            auto field = actual.object->find(name);
            if (field != actual.object->end() && field->second.kind == ValueKind::Function) {
                return callFunction(program_.functions.at(field->second.name), std::move(args), pos, {});
            }
        }
        fail("E1304", pos, "未定義のメソッドです: " + name);
    }

    std::optional<ResolvedPath> resolvePath(const ExprPtr &expression) {
        if (!expression) return std::nullopt;
        if (expression->kind == ExprKind::Variable) {
            auto root = env_.find(expression->text);
            if (!root) return std::nullopt;
            ResolvedPath result;
            result.root = root;
            if (root->value.kind == ValueKind::Reference && root->value.reference) {
                result.throughUniqueBorrow = root->value.borrow && root->value.borrow->kind == BorrowKind::Unique;
                result.root = root->value.reference;
            }
            return result;
        }
        if (expression->kind == ExprKind::Member) {
            auto result = resolvePath(expression->object);
            if (!result) return std::nullopt;
            result->path.emplace_back(true, expression->text);
            result->indexes.push_back(0);
            return result;
        }
        if (expression->kind == ExprKind::Index) {
            auto result = resolvePath(expression->object);
            if (!result) return std::nullopt;
            Value index = dereference(evalExpr(expression->index));
            if (index.kind != ValueKind::Integer) return std::nullopt;
            result->path.emplace_back(false, std::string{});
            result->indexes.push_back(index.integer);
            return result;
        }
        return std::nullopt;
    }

    Value *followPath(Value &root, const ResolvedPath &path, bool allowMissing = false) {
        Value *current = &root;
        std::size_t indexNumber = 0;
        for (const auto &part : path.path) {
            *current = dereference(*current);
            if (part.first) {
                if (current->kind != ValueKind::Object || !current->object) fail("E1305", rootValuePos(path), "メンバー対象がObjectではありません");
                auto found = current->object->find(part.second);
                if (found == current->object->end()) {
                    if (!allowMissing) fail("E1306", rootValuePos(path), "存在しないメンバーです: " + part.second);
                    found = current->object->emplace(part.second, Value::null()).first;
                }
                current = &found->second;
            } else {
                if (current->kind != ValueKind::Array || !current->array) fail("E1305", rootValuePos(path), "インデックス対象がListではありません");
                const std::int64_t index = path.indexes[indexNumber];
                ++indexNumber;
                if (index < 0 || static_cast<std::size_t>(index) >= current->array->size()) fail("E1204", rootValuePos(path), "配列インデックスが範囲外です");
                current = &(*current->array)[static_cast<std::size_t>(index)];
            }
        }
        return current;
    }

    SourcePos rootValuePos(const ResolvedPath &path) const {
        return path.root ? path.root->pos : SourcePos{file_, 0, 1, 1};
    }

    void ensureWritable(const ResolvedPath &path, const SourcePos &pos) {
        if (!path.root) fail("E1404", pos, "代入対象が見つかりません");
        if (path.root->moved) fail("E2101", pos, "移動済みの束縛を変更しています");
        if (path.root->sharedBorrows != 0) fail("E2205", pos, "共有借用中の値を変更できません");
        if (path.root->uniqueBorrow && !path.throughUniqueBorrow) fail("E2203", pos, "可変借用中の値を直接変更できません");
        if (!path.root->mutableSlot && !path.throughUniqueBorrow) fail("E1401", pos, "不変束縛に代入しています。'where' で不変条件付きスロットを宣言してください");
    }

    bool checkInvariant(const std::shared_ptr<Binding> &binding, const Value &candidate, bool staticOnly, const SourcePos &pos) {
        if (!binding->invariant) return true;
        auto scope = env_.scoped(false);
        auto self = env_.define("self", candidate, nullptr, false, staticOnly, pos, false);
        (void)self;
        const ProofState state = proofState(binding->invariant);
        if (state == ProofState::Proven) {
            return true;
        }
        if (staticOnly || mode_ == Mode::Verified) {
            fail("E1405", pos, "不変条件を静的に証明できません");
        }
        if (runtimeCondition(binding->invariant) != Truth::True) {
            fail("E1405", pos, "不変条件を満たさない値です");
        }
        return true;
    }

    void assign(const StatementPtr &statement) {
        const bool rhsIsStatic = staticExpr(statement->expr).has_value();
        Value rhs = applyPcFlow(evalExpr(statement->expr));
        if (statement->assignmentOp != "=") {
            Value current = evalExpr(statement->target);
            const std::string op = statement->assignmentOp.substr(0, 1);
            rhs = applyBinary(op, current, rhs, statement->pos);
        }
        auto resolved = resolvePath(statement->target);
        if (!resolved) fail("E1404", statement->pos, "代入対象が束縛またはそのメンバーではありません");
        ensureWritable(*resolved, statement->pos);
        const bool replacesWholeRoot = resolved->path.empty() && statement->assignmentOp == "=";
        const bool candidateIsStatic = staticControl_ && rhsIsStatic &&
                                       (replacesWholeRoot || resolved->root->known);
        Value candidate = cloneDeep(resolved->root->value);
        Value *slot = followPath(candidate, *resolved, false);
        *slot = std::move(rhs);
        candidate.flow |= slot->flow;
        checkInvariant(resolved->root, candidate, candidateIsStatic, statement->pos);
        resolved->root->value = std::move(candidate);
        ++resolved->root->version;
        resolved->root->known = candidateIsStatic;
    }

    template <typename Mutation>
    void mutateReceiver(const ExprPtr &receiverExpression, Mutation mutation) {
        auto resolved = resolvePath(receiverExpression);
        if (!resolved) fail("E1404", receiverExpression->pos, "変更対象が束縛またはそのメンバーではありません");
        ensureWritable(*resolved, receiverExpression->pos);
        Value candidate = cloneDeep(resolved->root->value);
        Value *slot = followPath(candidate, *resolved, false);
        mutation(*slot);
        checkInvariant(resolved->root, candidate, false, receiverExpression->pos);
        resolved->root->value = std::move(candidate);
        ++resolved->root->version;
        resolved->root->known = false;
    }

    Value executeBlockBody(const StatementPtr &block) {
        for (const StatementPtr &statement : block->statements) {
            execute(statement);
        }
        return Value::null();
    }

    Value execute(const StatementPtr &statement) {
        if (!statement) return Value::null();
        switch (statement->kind) {
        case StatementKind::Block: {
            auto scope = env_.scoped(false);
            return executeBlockBody(statement);
        }
        case StatementKind::Let: {
            const bool valueIsStatic = staticControl_ && staticExpr(statement->expr).has_value();
            Value value = applyPcFlow(evalExpr(statement->expr));
            auto binding = env_.define(statement->name, value, statement->invariant, statement->invariant != nullptr,
                                       valueIsStatic, statement->pos);
            if (binding->invariant) checkInvariant(binding, binding->value, valueIsStatic, statement->pos);
            return Value::null();
        }
        case StatementKind::Expression:
            evalExpr(statement->expr);
            return Value::null();
        case StatementKind::Assign:
            assign(statement);
            return Value::null();
        case StatementKind::Check:
            validateCondition(statement->condition, false, statement->pos, "check");
            return Value::null();
        case StatementKind::Prove:
            validateCondition(statement->condition, true, statement->pos, "prove");
            return Value::null();
        case StatementKind::Require:
            validateCondition(statement->condition, false, statement->pos, "require");
            return Value::null();
        case StatementKind::Assume:
            if (mode_ != Mode::Unsafe && !env_.unsafe()) fail("E1602", statement->pos, "assume は unsafe ブロックまたは --mode unsafe でのみ使用できます");
            addFact(statement->condition);
            return Value::null();
        case StatementKind::If:
            executeIf(statement);
            return Value::null();
        case StatementKind::While:
            executeWhile(statement);
            return Value::null();
        case StatementKind::For:
            executeFor(statement);
            return Value::null();
        case StatementKind::Return: {
            const bool known = staticControl_ &&
                               (!statement->expr || staticExpr(statement->expr).has_value());
            throw ReturnSignal{
                statement->expr ? applyPcFlow(evalExpr(statement->expr)) : applyPcFlow(Value::null()),
                known
            };
        }
        case StatementKind::Update:
            executeUpdate(statement);
            return Value::null();
        case StatementKind::UnsafeBlock:
            {
                auto scope = env_.scoped(true);
                return executeBlockBody(statement->body);
            }
        case StatementKind::Match:
            executeMatch(statement);
            return Value::null();
        }
        return Value::null();
    }

    struct ReturnSignal {
        Value value;
        bool known = false;
    };

    void validateCondition(const ConditionPtr &condition, bool staticRequired, const SourcePos &pos, const std::string &kind) {
        const ProofState state = proofState(condition);
        if (state == ProofState::Proven) {
            addFact(condition);
            return;
        }
        if (staticRequired || mode_ == Mode::Verified) {
            fail("E1201", pos, kind + " の条件を静的に証明できません (Unknown)");
        }
        const Truth result = runtimeCondition(condition);
        if (result != Truth::True) {
            fail("E1202", pos, kind + " の条件が成立しません");
        }
        addFact(condition);
    }

    void executeIf(const StatementPtr &statement) {
        const ProofState conditionProof = proofState(statement->condition);
        if (mode_ == Mode::Verified && conditionProof == ProofState::Unknown) {
            fail("E1201", statement->pos, "if の条件を静的に証明できません (Unknown)");
        }
        const Truth result = runtimeCondition(statement->condition);
        if (result == Truth::Unknown) fail("E1206", statement->pos, "if の条件を実行時に判定できません");
        const StatementPtr branch = result == Truth::True ? statement->body : statement->elseBody;
        if (!branch) return;
        const std::uint32_t savedPc = pcFlow_;
        const bool savedStaticControl = staticControl_;
        if (strictIfc_) pcFlow_ |= conditionFlow(statement->condition);
        staticControl_ = staticControl_ && conditionProof != ProofState::Unknown;
        auto scope = env_.scoped(false);
        try {
            addFact(result == Truth::True ? statement->condition : makeNot(statement->condition));
            executeBlockBody(branch);
            pcFlow_ = savedPc;
            staticControl_ = savedStaticControl;
        } catch (...) {
            pcFlow_ = savedPc;
            staticControl_ = savedStaticControl;
            throw;
        }
    }

    static ConditionPtr makeNot(const ConditionPtr &condition) {
        auto result = Condition::make(ConditionKind::Not, condition ? condition->pos : SourcePos{});
        result->items.push_back(condition);
        return result;
    }

    static bool isVariable(const ExprPtr &expression, const std::string &name) {
        return expression && expression->kind == ExprKind::Variable && expression->text == name;
    }

    bool isConvexIntegerInvariant(const ConditionPtr &condition, const std::string &name) {
        if (!condition) return true;
        switch (condition->kind) {
        case ConditionKind::Always:
            return true;
        case ConditionKind::And:
            return std::all_of(condition->items.begin(), condition->items.end(),
                               [&](const ConditionPtr &item) { return isConvexIntegerInvariant(item, name); });
        case ConditionKind::Is:
            return isVariable(condition->left, name) &&
                   (condition->predicate == "Int" || condition->predicate == "Number");
        case ConditionKind::Relation: {
            const bool variableOnLeft = isVariable(condition->left, name) && staticExpr(condition->right).has_value();
            const bool variableOnRight = isVariable(condition->right, name) && staticExpr(condition->left).has_value();
            if (!variableOnLeft && !variableOnRight) return false;
            return condition->op == "<" || condition->op == "<=" || condition->op == ">" ||
                   condition->op == ">=" || condition->op == "==";
        }
        default:
            return false;
        }
    }

    bool tryExecuteCountedLoop(const StatementPtr &statement) {
        const ConditionPtr &condition = statement->condition;
        if (!condition || condition->kind != ConditionKind::Relation || !condition->left ||
            condition->left->kind != ExprKind::Variable || !statement->body ||
            statement->body->statements.size() != 1) {
            return false;
        }
        const std::string variable = condition->left->text;
        auto bound = staticExpr(condition->right);
        if (!bound || bound->kind != ValueKind::Integer) return false;

        const StatementPtr &bodyStatement = statement->body->statements.front();
        if (!bodyStatement || bodyStatement->kind != StatementKind::Assign ||
            !isVariable(bodyStatement->target, variable) ||
            (bodyStatement->assignmentOp != "+=" && bodyStatement->assignmentOp != "-=") ||
            !bodyStatement->expr || bodyStatement->expr->kind != ExprKind::Literal ||
            bodyStatement->expr->literalKind != LiteralKind::Integer) {
            return false;
        }

        std::int64_t amount = 0;
        try {
            amount = std::stoll(bodyStatement->expr->text);
        } catch (const std::exception &) {
            return false;
        }
        if (amount <= 0) return false;
        const bool increasing = bodyStatement->assignmentOp == "+=";
        if ((increasing && condition->op != "<" && condition->op != "<=") ||
            (!increasing && condition->op != ">" && condition->op != ">=")) {
            return false;
        }

        auto binding = env_.find(variable);
        if (!binding || binding->moved || binding->value.kind != ValueKind::Integer ||
            binding->sharedBorrows != 0 || binding->uniqueBorrow || !binding->mutableSlot ||
            !isConvexIntegerInvariant(binding->invariant, "self")) {
            return false;
        }
        for (const ConditionPtr &invariant : statement->loopInvariants) {
            if (!isConvexIntegerInvariant(invariant, variable)) return false;
        }

        const std::int64_t start = binding->value.integer;
        const std::int64_t limit = bound->integer;
        std::uint64_t iterations = 0;
        const std::uint64_t stride = static_cast<std::uint64_t>(amount);
        if (increasing) {
            if (condition->op == "<" && start < limit) {
                const std::uint64_t distance = static_cast<std::uint64_t>(limit) - static_cast<std::uint64_t>(start);
                iterations = distance / stride + (distance % stride != 0);
            } else if (condition->op == "<=" && start <= limit) {
                const std::uint64_t distance = static_cast<std::uint64_t>(limit) - static_cast<std::uint64_t>(start);
                if (distance == std::numeric_limits<std::uint64_t>::max()) return false;
                const std::uint64_t inclusiveDistance = distance + 1;
                iterations = inclusiveDistance / stride + (inclusiveDistance % stride != 0);
            }
        } else {
            if (condition->op == ">" && start > limit) {
                const std::uint64_t distance = static_cast<std::uint64_t>(start) - static_cast<std::uint64_t>(limit);
                iterations = distance / stride + (distance % stride != 0);
            } else if (condition->op == ">=" && start >= limit) {
                const std::uint64_t distance = static_cast<std::uint64_t>(start) - static_cast<std::uint64_t>(limit);
                if (distance == std::numeric_limits<std::uint64_t>::max()) return false;
                const std::uint64_t inclusiveDistance = distance + 1;
                iterations = inclusiveDistance / stride + (inclusiveDistance % stride != 0);
            }
        }
        if (iterations == 0) return true;
        if (iterations > static_cast<std::uint64_t>(std::numeric_limits<std::int64_t>::max())) return false;

        const std::int64_t signedIterations = static_cast<std::int64_t>(iterations);
        std::int64_t movement = 0;
        std::int64_t finalValue = 0;
        const std::int64_t signedStride = increasing ? amount : -amount;
        if (checkedMul(signedIterations, signedStride, movement) ||
            checkedAdd(start, movement, finalValue)) {
            return false;
        }

        Value candidate = Value::integerValue(finalValue);
        candidate.flow = binding->value.flow;
        if (strictIfc_) candidate.flow |= conditionFlow(condition);
        const bool resultIsStatic = staticControl_ && binding->known;
        checkInvariant(binding, candidate, resultIsStatic, statement->pos);
        binding->value = std::move(candidate);
        binding->known = resultIsStatic;
        if (std::numeric_limits<std::uint64_t>::max() - binding->version < iterations) {
            binding->version = std::numeric_limits<std::uint64_t>::max();
        } else {
            binding->version += iterations;
        }
        for (const ConditionPtr &invariant : statement->loopInvariants) {
            validateCondition(invariant, mode_ == Mode::Verified, invariant->pos, "loop invariant preservation");
        }
        recordExactRewrite("counted-loop closed form (" + std::to_string(iterations) + " iterations)", statement->pos);
        return true;
    }

    void executeWhile(const StatementPtr &statement) {
        for (const ConditionPtr &invariant : statement->loopInvariants) {
            validateCondition(invariant, mode_ == Mode::Verified, invariant->pos, "loop invariant");
        }
        if (tryExecuteCountedLoop(statement)) return;
        std::size_t iterations = 0;
        while (true) {
            const ProofState conditionProof = proofState(statement->condition);
            if (mode_ == Mode::Verified && conditionProof == ProofState::Unknown) {
                fail("E1201", statement->pos, "while の条件を静的に証明できません (Unknown)");
            }
            const Truth result = runtimeCondition(statement->condition);
            if (result == Truth::Unknown) fail("E1206", statement->pos, "while の条件を実行時に判定できません");
            if (result == Truth::False) break;
            if (++iterations > 1000000) fail("E1207", statement->pos, "while の反復回数が上限を超えました");
            const std::uint32_t savedPc = pcFlow_;
            const bool savedStaticControl = staticControl_;
            if (strictIfc_) pcFlow_ |= conditionFlow(statement->condition);
            staticControl_ = staticControl_ && conditionProof != ProofState::Unknown;
            auto scope = env_.scoped(false);
            try {
                addFact(statement->condition);
                executeBlockBody(statement->body);
                for (const ConditionPtr &invariant : statement->loopInvariants) {
                    validateCondition(invariant, mode_ == Mode::Verified, invariant->pos, "loop invariant preservation");
                }
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
            } catch (...) {
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
                throw;
            }
        }
    }

    void executeFor(const StatementPtr &statement) {
        const bool iterableIsStatic = staticControl_ && staticExpr(statement->expr).has_value();
        const Value rawCollection = evalExpr(statement->expr);
        const Value collection = dereference(rawCollection);
        if (collection.kind != ValueKind::Array || !collection.array) {
            fail("E1303", statement->pos, "for の対象はListである必要があります");
        }

        // Iterate over a snapshot.  This keeps the semantics deterministic if
        // the body mutates the original list through an explicit push/pop.
        const std::vector<Value> items = *collection.array;
        std::size_t iterations = 0;
        for (const Value &rawItem : items) {
            if (++iterations > 1000000) {
                fail("E1207", statement->pos, "for の反復回数が上限を超えました");
            }

            const std::uint32_t savedPc = pcFlow_;
            const bool savedStaticControl = staticControl_;
            if (strictIfc_) pcFlow_ |= collection.flow;
            staticControl_ = staticControl_ && iterableIsStatic;

            auto scope = env_.scoped(false);
            Value item = rawItem;
            item.flow |= collection.flow;
            item = applyPcFlow(std::move(item));
            env_.define(statement->name, std::move(item), nullptr, false,
                        staticControl_, statement->pos);
            try {
                executeBlockBody(statement->body);
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
            } catch (...) {
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
                throw;
            }
        }
    }

    void executeMatch(const StatementPtr &statement) {
        const bool matchedIsStatic = staticControl_ && staticExpr(statement->expr).has_value();
        const Value matched = evalExpr(statement->expr);
        bool precedingConditionsStatic = true;
        for (const MatchArm &arm : statement->arms) {
            const ProofState conditionProof = proofState(arm.condition);
            if (mode_ == Mode::Verified && conditionProof == ProofState::Unknown) {
                fail("E1201", arm.condition->pos, "match の条件を静的に証明できません (Unknown)");
            }
            const Truth result = runtimeCondition(arm.condition);
            if (result == Truth::Unknown) {
                precedingConditionsStatic = false;
                continue;
            }
            if (result == Truth::True) {
                const std::uint32_t savedPc = pcFlow_;
                const bool savedStaticControl = staticControl_;
                if (strictIfc_) pcFlow_ |= matched.flow | conditionFlow(arm.condition);
                staticControl_ = staticControl_ && matchedIsStatic && precedingConditionsStatic &&
                                 conditionProof != ProofState::Unknown;
                auto scope = env_.scoped(false);
                try {
                    addFact(arm.condition);
                    execute(arm.action);
                    pcFlow_ = savedPc;
                    staticControl_ = savedStaticControl;
                } catch (...) {
                    pcFlow_ = savedPc;
                    staticControl_ = savedStaticControl;
                    throw;
                }
                return;
            }
            precedingConditionsStatic = precedingConditionsStatic && conditionProof != ProofState::Unknown;
        }
        if (statement->elseBody) {
            const std::uint32_t savedPc = pcFlow_;
            const bool savedStaticControl = staticControl_;
            if (strictIfc_) pcFlow_ |= matched.flow;
            staticControl_ = staticControl_ && matchedIsStatic && precedingConditionsStatic;
            try {
                execute(statement->elseBody);
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
            } catch (...) {
                pcFlow_ = savedPc;
                staticControl_ = savedStaticControl;
                throw;
            }
        }
    }

    void executeUpdate(const StatementPtr &statement) {
        auto resolved = resolvePath(statement->target);
        if (!resolved) fail("E1404", statement->pos, "update の対象は place である必要があります");
        ensureWritable(*resolved, statement->pos);
        auto original = resolved->root;
        Value candidate = cloneDeep(original->value);
        Value *targetSlot = followPath(candidate, *resolved, false);

        auto temporary = std::make_shared<Binding>();
        temporary->name = "self";
        temporary->value = cloneDeep(*targetSlot);
        temporary->mutableSlot = true;
        temporary->known = false;
        temporary->pos = statement->pos;
        auto scope = env_.scoped(false);
        if (!statement->name.empty()) env_.defineExisting(statement->name, temporary, statement->pos);
        env_.defineExisting("self", temporary, statement->pos);
        executeBlockBody(statement->body);
        *targetSlot = std::move(temporary->value);
        targetSlot->flow |= strictIfc_ ? pcFlow_ : FlowPublic;
        candidate.flow |= targetSlot->flow;
        checkInvariant(original, candidate, false, statement->pos);
        original->value = std::move(candidate);
        ++original->version;
        original->known = false;
    }

    Value callFunction(const FunctionDef &function, std::vector<Value> args, const SourcePos &callPos,
                       const std::vector<ExprPtr> &argumentExpressions) {
        if (function.params.size() != args.size()) {
            fail("E1307", callPos, "関数 " + function.name + " の引数個数が一致しません");
        }

        std::vector<ConditionPtr> actualRequirements;
        std::vector<bool> argumentKnown(args.size(), false);
        std::unordered_map<std::string, ExprPtr> substitutions;
        for (std::size_t i = 0; i < function.params.size(); ++i) {
            if (i < argumentExpressions.size()) {
                substitutions[function.params[i].name] = argumentExpressions[i];
                argumentKnown[i] = staticControl_ && staticExpr(argumentExpressions[i]).has_value();
            }
        }
        for (const ConditionPtr &requirement : function.requiresList) {
            actualRequirements.push_back(cloneConditionWithSubstitution(requirement, substitutions));
        }
        for (const ConditionPtr &requirement : actualRequirements) {
            const ProofState state = proofState(requirement);
            if (state == ProofState::Proven) continue;
            if (mode_ == Mode::Verified) fail("E1701", callPos, "関数 " + function.name + " の requires を静的に証明できません");
            // Safe mode checks the requirement below, in the callee scope,
            // against the already-evaluated arguments. Re-evaluating the
            // caller expression here would duplicate effects such as input().
        }

        if (function.foreign && mode_ != Mode::Unsafe && !env_.unsafe()) {
            fail("E3305", callPos,
                 "FFI関数 " + function.name + " の呼び出しには unsafe ブロックまたは --mode unsafe が必要です");
        }

        auto functionScope = env_.scoped(function.unsafe || mode_ == Mode::Unsafe);
        for (std::size_t i = 0; i < args.size(); ++i) {
            auto binding = env_.define(function.params[i].name, args[i], function.params[i].invariant,
                                       function.params[i].invariant != nullptr,
                                       argumentKnown[i], callPos);
            if (binding->invariant) checkInvariant(binding, binding->value, false, callPos);
        }
        for (const ConditionPtr &requirement : function.requiresList) {
            const Truth result = runtimeCondition(requirement);
            if (result != Truth::True) fail("E1701", callPos, "関数 " + function.name + " の requires を満たしていません");
            addFact(requirement);
        }

        Value returned = Value::null();
        bool returnedKnown = !function.foreign;
        if (function.foreign) {
            returned = ffi_.call(function, args, callPos);
        } else {
            try {
                executeBlockBody(function.body);
            } catch (const ReturnSignal &signal) {
                returned = signal.value;
                returnedKnown = signal.known;
            }
        }
        {
            auto postconditionScope = env_.scoped(false);
            auto resultBinding = env_.define("result", returned, nullptr, false, returnedKnown, callPos, false);
            for (const FlowClause &flow : function.flows) {
                std::uint32_t summaryFlow = FlowPublic;
                for (const ExprPtr &source : flow.sources) summaryFlow |= expressionFlow(source);
                if (flow.target == "result") {
                    returned.flow |= summaryFlow;
                    resultBinding->value.flow |= summaryFlow;
                } else if (auto target = env_.find(flow.target)) {
                    target->value.flow |= summaryFlow;
                } else {
                    fail("E3202", flow.pos, "flow の出力名が未定義です: " + flow.target);
                }
            }
            for (const ConditionPtr &ensure : function.ensures) {
                const ProofState state = proofState(ensure);
                if (state == ProofState::Proven) continue;
                if (mode_ == Mode::Verified) fail("E1702", callPos, "関数 " + function.name + " の ensures を静的に証明できません");
                if (runtimeCondition(ensure) != Truth::True) fail("E1702", callPos, "関数 " + function.name + " の ensures を満たしていません");
            }
        }
        return returned;
    }
};



InterpreterPtr makeInterpreter(const Program &program, Mode mode, std::string file,
                               bool strictIfc, bool explainOptimizations, bool traceOwnership) {
    return InterpreterPtr(new Interpreter(program, mode, std::move(file), strictIfc,
                                          explainOptimizations, traceOwnership),
                          &destroyInterpreter);
}

void destroyInterpreter(Interpreter *interpreter) {
    delete interpreter;
}

void runInterpreter(Interpreter &interpreter, const std::string &entry) {
    interpreter.run(entry);
}

void checkInterpreter(const Interpreter &interpreter) {
    interpreter.checkOnly();
}

void validateInterpreter(const Interpreter &interpreter) {
    interpreter.validateProgram();
}

HttpResponse dispatchHttpRequest(Interpreter &interpreter, const HttpRequestData &request) {
    return interpreter.handleHttpRequest(request);
}

} // namespace kond
