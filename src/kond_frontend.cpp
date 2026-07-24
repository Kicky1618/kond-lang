#include "kond_frontend.hpp"

namespace kond {

class Lexer {
public:
    Lexer(std::string source, std::string file) : source_(std::move(source)), file_(std::move(file)) {}

    std::vector<Token> tokenize() {
        std::vector<Token> tokens;
        while (true) {
            Token token = next();
            tokens.push_back(token);
            if (token.kind == TokenKind::End) {
                break;
            }
        }
        return tokens;
    }

private:
    std::string source_;
    std::string file_;
    std::size_t index_ = 0;
    int line_ = 1;
    int column_ = 1;

    char peek(std::size_t lookahead = 0) const {
        const std::size_t at = index_ + lookahead;
        return at < source_.size() ? source_[at] : '\0';
    }

    char advance() {
        const char ch = peek();
        if (ch == '\0') {
            return ch;
        }
        ++index_;
        if (ch == '\n') {
            ++line_;
            column_ = 1;
        } else {
            ++column_;
        }
        return ch;
    }

    SourcePos position() const {
        return SourcePos{file_, index_, line_, column_};
    }

    static bool isIdentifierStart(char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_';
    }

    static bool isIdentifierPart(char ch) {
        return isIdentifierStart(ch) || (ch >= '0' && ch <= '9');
    }

    static bool isTemplatePrefix(const std::string &word) {
        return word == "sql" || word == "html" || word == "json" || word == "text";
    }

    void skipSpaceAndComments() {
        while (true) {
            while (peek() == ' ' || peek() == '\t' || peek() == '\r' || peek() == '\n') {
                advance();
            }
            if (peek() == '/' && peek(1) == '/') {
                while (peek() != '\0' && peek() != '\n') {
                    advance();
                }
                continue;
            }
            break;
        }
    }

    Token stringToken(const SourcePos &start, const std::string &prefix = {}) {
        if (advance() != '"') {
            fail("E0002", start, "内部エラー: 文字列の開始位置が不正です");
        }
        std::string value;
        while (peek() != '\0' && peek() != '"') {
            char ch = advance();
            if (ch == '\\') {
                const char escaped = advance();
                switch (escaped) {
                case 'n': value.push_back('\n'); break;
                case 'r': value.push_back('\r'); break;
                case 't': value.push_back('\t'); break;
                case '\\': value.push_back('\\'); break;
                case '"': value.push_back('"'); break;
                default:
                    fail("E0003", start, "未対応の文字列エスケープです: \\" + std::string(1, escaped));
                }
            } else {
                value.push_back(ch);
            }
        }
        if (peek() != '"') {
            fail("E0004", start, "文字列リテラルが閉じられていません");
        }
        advance();
        if (!prefix.empty()) {
            // The current runtime keeps structured literals as strings.  The
            // prefix is retained in the token for diagnostics and future
            // standard-library lowering.
            value = prefix + "\x1f" + value;
        }
        return Token{TokenKind::String, std::move(value), start};
    }

    Token next() {
        skipSpaceAndComments();
        const SourcePos start = position();
        const char ch = peek();
        if (ch == '\0') {
            return Token{TokenKind::End, {}, start};
        }

        if (isIdentifierStart(ch)) {
            std::string word;
            while (isIdentifierPart(peek())) {
                word.push_back(advance());
            }
            if (peek() == '"' && isTemplatePrefix(word)) {
                return stringToken(start, word);
            }
            return Token{TokenKind::Identifier, std::move(word), start};
        }

        if ((ch >= '0' && ch <= '9') || (ch == '.' && peek(1) >= '0' && peek(1) <= '9')) {
            std::string number;
            if (ch == '.') {
                number.push_back(advance());
            }
            while (peek() >= '0' && peek() <= '9') {
                number.push_back(advance());
            }
            if (peek() == '.') {
                number.push_back(advance());
                while (peek() >= '0' && peek() <= '9') {
                    number.push_back(advance());
                }
            }
            if (peek() == 'e' || peek() == 'E') {
                number.push_back(advance());
                if (peek() == '+' || peek() == '-') {
                    number.push_back(advance());
                }
                if (!(peek() >= '0' && peek() <= '9')) {
                    fail("E0005", start, "指数部には数字が必要です");
                }
                while (peek() >= '0' && peek() <= '9') {
                    number.push_back(advance());
                }
            }
            return Token{TokenKind::Number, std::move(number), start};
        }

        if (ch == '"') {
            return stringToken(start);
        }

        advance();
        switch (ch) {
        case '(': return Token{TokenKind::LParen, "(", start};
        case ')': return Token{TokenKind::RParen, ")", start};
        case '{': return Token{TokenKind::LBrace, "{", start};
        case '}': return Token{TokenKind::RBrace, "}", start};
        case '[': return Token{TokenKind::LBracket, "[", start};
        case ']': return Token{TokenKind::RBracket, "]", start};
        case ',': return Token{TokenKind::Comma, ",", start};
        case '.': return Token{TokenKind::Dot, ".", start};
        case ':': return Token{TokenKind::Colon, ":", start};
        case ';': return Token{TokenKind::Semicolon, ";", start};
        case '+':
            if (peek() == '=') {
                advance();
                return Token{TokenKind::PlusEqual, "+=", start};
            }
            return Token{TokenKind::Plus, "+", start};
        case '-':
            if (peek() == '>') {
                advance();
                return Token{TokenKind::ReturnArrow, "->", start};
            }
            if (peek() == '=') {
                advance();
                return Token{TokenKind::MinusEqual, "-=", start};
            }
            return Token{TokenKind::Minus, "-", start};
        case '*':
            if (peek() == '=') {
                advance();
                return Token{TokenKind::StarEqual, "*=", start};
            }
            return Token{TokenKind::Star, "*", start};
        case '/':
            if (peek() == '=') {
                advance();
                return Token{TokenKind::SlashEqual, "/=", start};
            }
            return Token{TokenKind::Slash, "/", start};
        case '%': return Token{TokenKind::Percent, "%", start};
        case '&': return Token{TokenKind::Amp, "&", start};
        case '|': return Token{TokenKind::Pipe, "|", start};
        case '^': return Token{TokenKind::Caret, "^", start};
        case '!':
            if (peek() == '=') {
                advance();
                return Token{TokenKind::NotEqual, "!=", start};
            }
            return Token{TokenKind::Bang, "!", start};
        case '=':
            if (peek() == '>') {
                advance();
                return Token{TokenKind::Arrow, "=>", start};
            }
            if (peek() == '=') {
                advance();
                return Token{TokenKind::EqualEqual, "==", start};
            }
            return Token{TokenKind::Equal, "=", start};
        case '<':
            if (peek() == '-') {
                advance();
                return Token{TokenKind::LeftArrow, "<-", start};
            }
            if (peek() == '=') {
                advance();
                return Token{TokenKind::LessEqual, "<=", start};
            }
            return Token{TokenKind::Less, "<", start};
        case '>':
            if (peek() == '=') {
                advance();
                return Token{TokenKind::GreaterEqual, ">=", start};
            }
            return Token{TokenKind::Greater, ">", start};
        default:
            fail("E0001", start, "認識できない文字です: " + std::string(1, ch));
        }
    }
};


class Parser {
public:
    Parser(std::string source, std::string file) : tokens_(Lexer(std::move(source), std::move(file)).tokenize()) {
        for (const Token &token : tokens_) {
            if (token.kind == TokenKind::End) {
                file_ = token.pos.file;
                break;
            }
        }
    }

    Program parse() {
        Program program;
        program_ = &program;
        while (!atEnd()) {
            if (consume("condition")) {
                parseConditionDeclaration();
            } else if (consume("rewrite")) {
                parseOptimizationDeclaration();
            } else if (check("extern") ||
                       (check("unsafe") && peek(1).kind == TokenKind::Identifier && peek(1).text == "extern")) {
                parseForeignFunctionDeclaration();
            } else if (check("unsafe") || check("fn")) {
                parseFunctionDeclaration(false);
            } else if (consume("route")) {
                parseRouteDeclaration();
            } else {
                program.topLevel.push_back(parseStatement());
            }
        }
        return program;
    }

    ExprPtr parseStandaloneExpression() {
        ExprPtr expression = parseValueExpression();
        if (!atEnd()) fail("E0107", peek().pos, "補間式の後ろに不要なトークンがあります");
        return expression;
    }

private:
    std::vector<Token> tokens_;
    std::size_t current_ = 0;
    std::string file_;
    Program *program_ = nullptr;
    std::unordered_set<std::string> conditionNames_;

    const Token &peek(std::size_t offset = 0) const {
        const std::size_t at = std::min(current_ + offset, tokens_.size() - 1);
        return tokens_[at];
    }

    const Token &previous() const {
        return tokens_[current_ == 0 ? 0 : current_ - 1];
    }

    bool atEnd() const {
        return peek().kind == TokenKind::End;
    }

    bool check(TokenKind kind) const {
        return peek().kind == kind;
    }

    bool check(const std::string &text) const {
        return peek().kind == TokenKind::Identifier && peek().text == text;
    }

    bool checkAny(const std::initializer_list<const char *> &words) const {
        for (const char *word : words) {
            if (check(word)) {
                return true;
            }
        }
        return false;
    }

    Token advance() {
        if (!atEnd()) {
            return tokens_[current_++];
        }
        return tokens_[current_];
    }

    bool consume(const std::string &text) {
        if (!check(text)) {
            return false;
        }
        advance();
        return true;
    }

    bool consume(TokenKind kind) {
        if (!check(kind)) {
            return false;
        }
        advance();
        return true;
    }

    Token expect(const std::string &text, const std::string &message = {}) {
        if (!check(text)) {
            fail("E0101", peek().pos, message.empty() ? "'" + text + "' が必要です" : message);
        }
        return advance();
    }

    Token expect(TokenKind kind, const std::string &message) {
        if (!check(kind)) {
            fail("E0101", peek().pos, message);
        }
        return advance();
    }

    Token expectIdentifier(const std::string &message = "識別子が必要です") {
        if (!check(TokenKind::Identifier)) {
            fail("E0102", peek().pos, message);
        }
        return advance();
    }

    bool isStatementBoundary(const Token &token) const {
        if (token.kind == TokenKind::End || token.kind == TokenKind::RBrace || token.kind == TokenKind::Semicolon) {
            return true;
        }
        if (token.kind != TokenKind::Identifier) {
            return false;
        }
        return token.text == "let" || token.text == "check" || token.text == "prove" || token.text == "require" ||
               token.text == "assume" || token.text == "return" || token.text == "if" || token.text == "else" ||
               token.text == "update" || token.text == "while" || token.text == "match" || token.text == "unsafe" ||
               token.text == "for" || token.text == "route" || token.text == "fn" || token.text == "extern" ||
               token.text == "condition" ||
               token.text == "rewrite";
    }

    bool conditionCandidate() const {
        if (checkAny({"always", "never", "not"})) {
            return true;
        }
        int depth = 0;
        for (std::size_t i = current_; i < tokens_.size(); ++i) {
            const Token &token = tokens_[i];
            if (depth == 0 && token.kind == TokenKind::Comma) break;
            if (token.kind == TokenKind::LParen || token.kind == TokenKind::LBracket || token.kind == TokenKind::LBrace) {
                ++depth;
            } else if (token.kind == TokenKind::RParen || token.kind == TokenKind::RBracket || token.kind == TokenKind::RBrace) {
                if (depth == 0) {
                    break;
                }
                --depth;
            }
            if (depth == 0 && isStatementBoundary(token) && i != current_) {
                break;
            }
            if (token.kind == TokenKind::EqualEqual || token.kind == TokenKind::NotEqual || token.kind == TokenKind::Less ||
                token.kind == TokenKind::LessEqual || token.kind == TokenKind::Greater || token.kind == TokenKind::GreaterEqual ||
                (token.kind == TokenKind::Identifier &&
                 (token.text == "is" || token.text == "has" || token.text == "and" || token.text == "or" || token.text == "in" ||
                  token.text == "not" || conditionNames_.count(token.text) != 0))) {
                return true;
            }
        }
        return false;
    }

    static std::string qualifiedCalleeName(const ExprPtr &expression) {
        if (!expression) return {};
        if (expression->kind == ExprKind::Variable) return expression->text;
        if (expression->kind != ExprKind::Member) return {};
        const std::string prefix = qualifiedCalleeName(expression->object);
        return prefix.empty() ? std::string{} : prefix + "." + expression->text;
    }

    void parseConditionDeclaration() {
        const Token name = expectIdentifier("condition 名には識別子が必要です");
        expect(TokenKind::LParen, "condition の引数リスト '(' が必要です");
        std::vector<Param> params = parseParams();
        expect(TokenKind::RParen, "condition の引数リスト ')' が必要です");
        expect(TokenKind::Equal, "condition 本体には '=' が必要です");
        ConditionDef definition{name.text, std::move(params), parseCondition(), name.pos};
        if (program_->conditions.count(name.text) != 0) {
            fail("E0103", name.pos, "condition が重複しています: " + name.text);
        }
        program_->conditions.emplace(name.text, std::move(definition));
        conditionNames_.insert(name.text);
        consume(TokenKind::Semicolon);
    }

    static OptimizationProofKind parseOptimizationProofKind(const Token &token) {
        if (token.text == "exact") return OptimizationProofKind::ExactEq;
        if (token.text == "real") return OptimizationProofKind::RealEq;
        if (token.text == "approx") return OptimizationProofKind::ApproxEq;
        if (token.text == "heuristic") return OptimizationProofKind::HeuristicImprovement;
        fail("E0113", token.pos, "rewrite の証明種別は exact, real, approx, heuristic のいずれかです");
    }

    void parseOptimizationDeclaration() {
        const Token proof = expectIdentifier("rewrite の証明種別が必要です");
        const Token name = expectIdentifier("rewrite 名には識別子が必要です");
        expect(TokenKind::LParen, "rewrite の引数リスト '(' が必要です");
        std::vector<std::string> parameters;
        if (!check(TokenKind::RParen)) {
            do {
                const Token parameter = expectIdentifier("rewrite のパラメータ名が必要です");
                if (std::find(parameters.begin(), parameters.end(), parameter.text) != parameters.end()) {
                    fail("E0116", parameter.pos, "rewrite のパラメータが重複しています: " + parameter.text);
                }
                parameters.push_back(parameter.text);
            } while (consume(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "rewrite の引数リスト ')' が必要です");
        expect("when", "rewrite には when 条件が必要です");
        ConditionPtr precondition = parseCondition();
        expect(TokenKind::LBrace, "rewrite 本体には '{' が必要です");
        expect("from", "rewrite 本体には from 式が必要です");
        ExprPtr pattern = parseExpression();
        consume(TokenKind::Semicolon);
        expect("to", "rewrite 本体には to 式が必要です");
        ExprPtr replacement = parseExpression();
        consume(TokenKind::Semicolon);
        expect(TokenKind::RBrace, "rewrite 本体が閉じられていません");

        for (const OptimizationRule &existing : program_->optimizationRules) {
            if (existing.name == name.text) {
                fail("E0114", name.pos, "rewrite が重複しています: " + name.text);
            }
        }
        OptimizationRule rule;
        rule.name = name.text;
        rule.parameters = std::move(parameters);
        rule.precondition = std::move(precondition);
        rule.pattern = std::move(pattern);
        rule.replacement = std::move(replacement);
        rule.proofKind = parseOptimizationProofKind(proof);
        rule.pos = name.pos;
        program_->optimizationRules.push_back(std::move(rule));
        consume(TokenKind::Semicolon);
    }

    std::vector<Param> parseParams() {
        std::vector<Param> params;
        if (check(TokenKind::RParen)) {
            return params;
        }
        do {
            const Token name = expectIdentifier("引数名には識別子が必要です");
            Param param{name.text, nullptr, name.pos};
            if (consume("where")) {
                param.invariant = parseCondition();
            }
            params.push_back(std::move(param));
        } while (consume(TokenKind::Comma));
        return params;
    }

    void parseFunctionDeclaration(bool route) {
        bool unsafe = consume("unsafe");
        expect("fn");
        const Token name = expectIdentifier("fn 名には識別子が必要です");
        expect(TokenKind::LParen, "fn の引数リスト '(' が必要です");
        std::vector<Param> params = parseParams();
        expect(TokenKind::RParen, "fn の引数リスト ')' が必要です");

        FunctionDef function;
        function.name = name.text;
        function.params = std::move(params);
        function.pos = name.pos;
        function.unsafe = unsafe;
        function.route = route;
        while (check("requires") || check("ensures") || check("flow")) {
            if (consume("requires")) {
                function.requiresList.push_back(parseCondition());
            } else if (consume("ensures")) {
                function.ensures.push_back(parseCondition());
            } else {
                const Token flow = expect("flow");
                FlowClause clause;
                clause.pos = flow.pos;
                clause.target = expectIdentifier("flow の出力名が必要です").text;
                expect(TokenKind::LeftArrow, "flow には '<-' が必要です");
                do {
                    clause.sources.push_back(parseExpression());
                } while (consume(TokenKind::Comma));
                function.flows.push_back(std::move(clause));
            }
            consume(TokenKind::Semicolon);
        }
        function.body = parseBlock();
        if (program_->functions.count(function.name) != 0) {
            fail("E0104", name.pos, "fn が重複しています: " + function.name);
        }
        program_->functions.emplace(function.name, std::move(function));
    }

    static bool isFfiTypeName(const std::string &name) {
        return name == "Void" || name == "void" || name == "Unit" ||
               name == "Int" || name == "int" || name == "Int64" || name == "i64" ||
               name == "Float" || name == "float" || name == "Float64" || name == "f64" ||
               name == "Bool" || name == "bool" || name == "Boolean" ||
               name == "String" || name == "string" || name == "CStr" || name == "cstring";
    }

    static FfiType parseFfiType(const Token &token) {
        if (token.text == "Void" || token.text == "void" || token.text == "Unit") {
            return FfiType::Void;
        }
        if (token.text == "Int" || token.text == "int" || token.text == "Int64" || token.text == "i64") {
            return FfiType::Int64;
        }
        if (token.text == "Float" || token.text == "float" || token.text == "Float64" || token.text == "f64") {
            return FfiType::Float64;
        }
        if (token.text == "Bool" || token.text == "bool" || token.text == "Boolean") {
            return FfiType::Bool;
        }
        if (token.text == "String" || token.text == "string" || token.text == "CStr" || token.text == "cstring") {
            return FfiType::CString;
        }
        fail("E0120", token.pos,
             "FFI型は Void, Int, Float, Bool, String のいずれかである必要があります: " + token.text);
    }

    FfiType parseFfiTypeName(const std::string &context) {
        const Token type = expectIdentifier(context + " の型名が必要です");
        return parseFfiType(type);
    }

    void parseForeignFunctionDeclaration() {
        bool unsafe = consume("unsafe");
        expect("extern", "FFI宣言には 'extern' が必要です");

        // Both `unsafe extern fn ... from "..."` and
        // `extern unsafe fn ... from "..."` are accepted so the trust
        // boundary remains visible whichever declaration style a project
        // prefers.
        unsafe = consume("unsafe") || unsafe;

        std::string library;
        if (check(TokenKind::String)) {
            library = advance().text;
        }
        expect("fn", "FFI宣言には 'fn' が必要です");
        const Token name = expectIdentifier("FFI関数名には識別子が必要です");
        expect(TokenKind::LParen, "FFI関数の引数リスト '(' が必要です");

        std::vector<Param> params;
        std::vector<FfiType> parameterTypes;
        std::size_t parameterIndex = 0;
        if (!check(TokenKind::RParen)) {
            do {
                const Token first = expectIdentifier("FFI引数には名前または型が必要です");
                std::string parameterName;
                FfiType parameterType;
                if (consume(TokenKind::Colon)) {
                    parameterName = first.text;
                    parameterType = parseFfiTypeName("FFI引数 " + parameterName);
                } else if (isFfiTypeName(first.text)) {
                    // Also accept the compact C-like form `fn(Int, String)`.
                    parameterName = "arg" + std::to_string(parameterIndex);
                    parameterType = parseFfiType(first);
                } else {
                    fail("E0121", first.pos,
                         "FFI引数には 'name: Type' または型名だけを指定してください");
                }
                params.push_back(Param{std::move(parameterName), nullptr, first.pos});
                parameterTypes.push_back(parameterType);
                ++parameterIndex;
            } while (consume(TokenKind::Comma));
        }
        expect(TokenKind::RParen, "FFI関数の引数リスト ')' が必要です");

        FfiType returnType = FfiType::Void;
        if (consume(TokenKind::Arrow) || consume(TokenKind::ReturnArrow) || consume(TokenKind::Colon)) {
            returnType = parseFfiTypeName("FFI戻り値");
        }

        if (consume("from")) {
            const Token libraryToken = expect(TokenKind::String, "FFIには共有ライブラリのパスが必要です");
            library = libraryToken.text;
        }
        if (library.empty()) {
            fail("E0122", name.pos,
                 "FFI宣言には共有ライブラリを指定してください (from \"lib.so\")");
        }

        std::string symbol = name.text;
        if (consume("as")) {
            symbol = expect(TokenKind::String, "FFIの 'as' にはシンボル名文字列が必要です").text;
        }

        std::vector<ConditionPtr> requiresList;
        std::vector<ConditionPtr> ensuresList;
        std::vector<FlowClause> flows;
        while (check("requires") || check("ensures") || check("flow")) {
            if (consume("requires")) {
                requiresList.push_back(parseCondition());
            } else if (consume("ensures")) {
                ensuresList.push_back(parseCondition());
            } else {
                const Token flow = expect("flow");
                FlowClause clause;
                clause.pos = flow.pos;
                clause.target = expectIdentifier("flow の出力名が必要です").text;
                expect(TokenKind::LeftArrow, "flow には '<-' が必要です");
                do {
                    clause.sources.push_back(parseExpression());
                } while (consume(TokenKind::Comma));
                flows.push_back(std::move(clause));
            }
            consume(TokenKind::Semicolon);
        }

        FunctionDef function;
        function.name = name.text;
        function.params = std::move(params);
        function.pos = name.pos;
        function.unsafe = unsafe;
        function.foreign = true;
        function.ffiLibrary = std::move(library);
        function.ffiSymbol = std::move(symbol);
        function.ffiParameterTypes = std::move(parameterTypes);
        function.ffiReturnType = returnType;
        function.requiresList = std::move(requiresList);
        function.ensures = std::move(ensuresList);
        function.flows = std::move(flows);
        consume(TokenKind::Semicolon);
        if (program_->functions.count(function.name) != 0) {
            fail("E0104", name.pos, "fn が重複しています: " + function.name);
        }
        program_->functions.emplace(function.name, std::move(function));
    }

    void parseRouteDeclaration() {
        const Token method = expectIdentifier("route の HTTP メソッドが必要です");
        static const std::unordered_set<std::string> methods{
            "GET", "POST", "PUT", "PATCH", "DELETE", "OPTIONS", "HEAD"
        };
        if (methods.count(method.text) == 0) {
            fail("E0105", method.pos, "未対応の HTTP メソッドです: " + method.text);
        }
        const Token path = expect(TokenKind::String, "route のパス文字列が必要です");
        expect(TokenKind::LParen, "route の引数リスト '(' が必要です");
        std::vector<Param> params = parseParams();
        expect(TokenKind::RParen, "route の引数リスト ')' が必要です");
        FunctionDef route;
        route.name = "__route_" + method.text + "_" + path.text;
        route.params = std::move(params);
        route.pos = method.pos;
        route.route = true;
        route.routeMethod = method.text;
        route.routePath = path.text;
        route.body = parseBlock();
        if (program_->functions.count(route.name) != 0) {
            fail("E0104", method.pos, "route が重複しています: " + method.text + " " + path.text);
        }
        program_->functions.emplace(route.name, std::move(route));
    }

    StatementPtr parseBlock() {
        const Token open = expect(TokenKind::LBrace, "ブロックには '{' が必要です");
        auto block = Statement::make(StatementKind::Block, open.pos);
        while (!check(TokenKind::RBrace) && !atEnd()) {
            block->statements.push_back(parseStatement());
        }
        expect(TokenKind::RBrace, "ブロックが閉じられていません");
        return block;
    }

    StatementPtr parseStatement() {
        if (consume(TokenKind::Semicolon)) {
            return parseStatement();
        }
        if (check(TokenKind::LBrace)) {
            return parseBlock();
        }
        if (check("let")) return parseLet();
        if (check("check")) return parseProofStatement(StatementKind::Check);
        if (check("prove")) return parseProofStatement(StatementKind::Prove);
        if (check("require")) return parseProofStatement(StatementKind::Require);
        if (check("assume")) return parseProofStatement(StatementKind::Assume);
        if (check("if")) return parseIf();
        if (check("while")) return parseWhile();
        if (check("for")) return parseFor();
        if (check("match")) return parseMatch();
        if (check("return")) return parseReturn();
        if (check("update")) return parseUpdate();
        if (check("unsafe")) {
            const Token unsafe = advance();
            auto result = Statement::make(StatementKind::UnsafeBlock, unsafe.pos);
            result->body = parseBlock();
            return result;
        }

        const Token start = peek();
        ExprPtr target = parseExpression();
        const std::string assignment = parseAssignmentOp();
        if (!assignment.empty()) {
            auto result = Statement::make(StatementKind::Assign, start.pos);
            result->target = std::move(target);
            result->assignmentOp = assignment;
            result->expr = parseValueExpression();
            consume(TokenKind::Semicolon);
            return result;
        }
        auto result = Statement::make(StatementKind::Expression, start.pos);
        result->expr = std::move(target);
        consume(TokenKind::Semicolon);
        return result;
    }

    std::string parseAssignmentOp() {
        if (consume(TokenKind::Equal)) return "=";
        if (consume(TokenKind::PlusEqual)) return "+=";
        if (consume(TokenKind::MinusEqual)) return "-=";
        if (consume(TokenKind::StarEqual)) return "*=";
        if (consume(TokenKind::SlashEqual)) return "/=";
        return {};
    }

    StatementPtr parseLet() {
        const Token letToken = expect("let");
        const Token name = expectIdentifier("let の束縛名には識別子が必要です");
        ConditionPtr invariant;
        if (consume("where")) {
            invariant = parseCondition();
        }
        expect(TokenKind::Equal, "let には '=' が必要です");

        ExprPtr value = parseValueExpression();
        consume(TokenKind::Semicolon);
        auto result = Statement::make(StatementKind::Let, letToken.pos);
        result->name = name.text;
        result->invariant = std::move(invariant);
        result->expr = std::move(value);
        return result;
    }

    StatementPtr parseProofStatement(StatementKind kind) {
        const Token token = advance();
        auto result = Statement::make(kind, token.pos);
        result->condition = parseCondition();
        consume(TokenKind::Semicolon);
        return result;
    }

    StatementPtr parseIf() {
        const Token token = expect("if");
        auto result = Statement::make(StatementKind::If, token.pos);
        result->condition = parseCondition();
        result->body = parseBlock();
        if (consume("else")) {
            result->elseBody = check("if") ? parseIf() : parseBlock();
        }
        return result;
    }

    StatementPtr parseWhile() {
        const Token token = expect("while");
        auto result = Statement::make(StatementKind::While, token.pos);
        result->condition = parseCondition();
        while (consume("invariant")) result->loopInvariants.push_back(parseCondition());
        result->body = parseBlock();
        return result;
    }

    StatementPtr parseFor() {
        const Token token = expect("for");
        const Token name = expectIdentifier("for の束縛名には識別子が必要です");
        expect("in", "for には 'in' が必要です");
        auto result = Statement::make(StatementKind::For, token.pos);
        result->name = name.text;
        result->expr = parseValueExpression();
        result->body = parseBlock();
        return result;
    }

    StatementPtr parseMatch() {
        const Token token = expect("match");
        auto result = Statement::make(StatementKind::Match, token.pos);
        result->expr = parseExpression();
        expect(TokenKind::LBrace, "match には '{' が必要です");
        while (consume("when")) {
            MatchArm arm;
            arm.condition = parseCondition();
            expect(TokenKind::Arrow, "match の分岐には '=>' が必要です");
            arm.action = check(TokenKind::LBrace) ? parseBlock() : parseStatement();
            result->arms.push_back(std::move(arm));
        }
        if (consume("else")) {
            expect(TokenKind::Arrow, "match の else には '=>' が必要です");
            result->elseBody = check(TokenKind::LBrace) ? parseBlock() : parseStatement();
        }
        expect(TokenKind::RBrace, "match が閉じられていません");
        return result;
    }

    StatementPtr parseReturn() {
        const Token token = expect("return");
        auto result = Statement::make(StatementKind::Return, token.pos);
        if (!check(TokenKind::Semicolon) && !check(TokenKind::RBrace) && !atEnd()) {
            result->expr = parseValueExpression();
        }
        consume(TokenKind::Semicolon);
        return result;
    }

    StatementPtr parseUpdate() {
        const Token token = expect("update");
        auto result = Statement::make(StatementKind::Update, token.pos);
        result->target = parseExpression();
        if (result->target->kind == ExprKind::Variable) result->name = result->target->text;
        result->body = parseBlock();
        return result;
    }

    ConditionPtr parseCondition() {
        return parseConditionOr();
    }

    ConditionPtr parseConditionOr() {
        ConditionPtr left = parseConditionAnd();
        while (consume("or")) {
            auto result = Condition::make(ConditionKind::Or, left->pos);
            result->items.push_back(std::move(left));
            result->items.push_back(parseConditionAnd());
            left = std::move(result);
        }
        return left;
    }

    ConditionPtr parseConditionAnd() {
        ConditionPtr left = parseConditionNot();
        while (consume("and")) {
            auto result = Condition::make(ConditionKind::And, left->pos);
            result->items.push_back(std::move(left));
            result->items.push_back(parseConditionNot());
            left = std::move(result);
        }
        return left;
    }

    ConditionPtr parseConditionNot() {
        if (consume("not")) {
            auto result = Condition::make(ConditionKind::Not, previous().pos);
            result->items.push_back(parseConditionNot());
            return result;
        }
        return parseConditionAtom();
    }

    ConditionPtr parseConditionAtom() {
        if (consume("always")) {
            return Condition::make(ConditionKind::Always, previous().pos);
        }
        if (consume("never")) {
            return Condition::make(ConditionKind::Never, previous().pos);
        }
        if (consume(TokenKind::LParen)) {
            ConditionPtr result = parseCondition();
            expect(TokenKind::RParen, "条件の ')' が必要です");
            return result;
        }

        ExprPtr left = parseExpression();
        if (consume("is")) {
            const Token predicate = expectIdentifier("is の後ろには述語名が必要です");
            auto result = Condition::make(ConditionKind::Is, left->pos);
            result->left = std::move(left);
            result->predicate = predicate.text;
            return result;
        }
        if (consume("has")) {
            auto result = Condition::make(ConditionKind::Has, left->pos);
            result->left = std::move(left);
            if (consume(TokenKind::LBrace)) {
                if (!check(TokenKind::RBrace)) {
                    do {
                        const Token field = expectIdentifier("has のフィールド名が必要です");
                        ShapeFieldSchema schema;
                        schema.name = field.text;
                        schema.pos = field.pos;
                        result->fields.push_back(field.text);
                        if (consume(TokenKind::Colon)) {
                            if (consume(TokenKind::LParen)) {
                                schema.condition = parseCondition();
                                expect(TokenKind::RParen, "shape schema の ')' が必要です");
                            } else {
                                schema.predicate = expectIdentifier("shape schema の述語名が必要です").text;
                                if (consume("where")) schema.condition = parseCondition();
                            }
                        }
                        result->shapeFields.push_back(std::move(schema));
                    } while (consume(TokenKind::Comma));
                }
                expect(TokenKind::RBrace, "has のフィールド集合が閉じられていません");
            } else {
                result->right = parseExpression();
            }
            return result;
        }
        if (check(TokenKind::EqualEqual) || check(TokenKind::NotEqual) || check(TokenKind::Less) ||
            check(TokenKind::LessEqual) || check(TokenKind::Greater) || check(TokenKind::GreaterEqual) || check("in")) {
            std::vector<ConditionPtr> relations;
            ExprPtr middle = std::move(left);
            while (true) {
                std::string op;
                if (consume(TokenKind::EqualEqual)) op = "==";
                else if (consume(TokenKind::NotEqual)) op = "!=";
                else if (consume(TokenKind::Less)) op = "<";
                else if (consume(TokenKind::LessEqual)) op = "<=";
                else if (consume(TokenKind::Greater)) op = ">";
                else if (consume(TokenKind::GreaterEqual)) op = ">=";
                else if (consume("in")) op = "in";
                else break;
                ExprPtr right = parseExpression();
                auto relation = Condition::make(ConditionKind::Relation, middle->pos);
                relation->op = op;
                relation->left = middle;
                relation->right = right;
                relations.push_back(std::move(relation));
                middle = std::move(right);
            }
            if (relations.size() == 1) {
                return relations.front();
            }
            auto result = Condition::make(ConditionKind::And, relations.front()->pos);
            for (auto &relation : relations) {
                result->items.push_back(std::move(relation));
            }
            return result;
        }

        if (left->kind == ExprKind::Call) {
            auto result = Condition::make(ConditionKind::Call, left->pos);
            result->predicate = qualifiedCalleeName(left->callee);
            if (result->predicate.empty()) {
                fail("E0110", left->pos, "条件呼び出しは識別子で指定してください");
            }
            result->args = std::move(left->items);
            return result;
        }
        if (left->kind == ExprKind::Variable || left->kind == ExprKind::ConditionValue) {
            auto result = Condition::make(ConditionKind::ValueRef, left->pos);
            result->value = std::move(left);
            return result;
        }
        fail("E0111", left->pos, "条件式には比較、述語、always/never のいずれかが必要です");
    }

    static int expressionPrecedence(const Token &token) {
        switch (token.kind) {
        case TokenKind::Pipe: return 1;
        case TokenKind::Caret: return 2;
        case TokenKind::Amp: return 3;
        case TokenKind::Plus:
        case TokenKind::Minus: return 4;
        case TokenKind::Star:
        case TokenKind::Slash:
        case TokenKind::Percent: return 5;
        default: return -1;
        }
    }

    ExprPtr parseExpression(int minimumPrecedence = 0) {
        ExprPtr left = parseUnary();
        while (true) {
            const int precedence = expressionPrecedence(peek());
            if (precedence < minimumPrecedence) {
                break;
            }
            const Token operatorToken = advance();
            ExprPtr right = parseExpression(precedence + 1);
            auto binary = Expr::make(ExprKind::Binary, operatorToken.pos);
            binary->op = operatorToken.text;
            binary->left = std::move(left);
            binary->right = std::move(right);
            left = std::move(binary);
        }
        return left;
    }

    ExprPtr parseValueExpression() {
        if (conditionCandidate()) {
            const std::size_t saved = current_;
            try {
                ConditionPtr condition = parseCondition();
                // Namespaced standard-library calls are value APIs by
                // default.  Their use in `if`/`check` is still parsed as a
                // condition by parseCondition directly, but `let x =
                // std.string.length(...)` must remain an ordinary call.
                if (condition->kind == ConditionKind::Call &&
                    condition->predicate.rfind("std.", 0) == 0) {
                    current_ = saved;
                    return parseExpression();
                }
                auto value = Expr::make(ExprKind::ConditionValue, condition->pos);
                value->condition = std::move(condition);
                return value;
            } catch (const KondError &) {
                current_ = saved;
            }
        }
        return parseExpression();
    }

    ExprPtr parseUnary() {
        if (consume(TokenKind::Minus)) {
            auto result = Expr::make(ExprKind::Unary, previous().pos);
            result->op = "-";
            result->left = parseUnary();
            return result;
        }
        if (consume(TokenKind::Plus)) {
            auto result = Expr::make(ExprKind::Unary, previous().pos);
            result->op = "+";
            result->left = parseUnary();
            return result;
        }
        if (consume(TokenKind::Bang)) {
            auto result = Expr::make(ExprKind::Unary, previous().pos);
            result->op = "!";
            result->left = parseUnary();
            return result;
        }
        if (consume("move")) {
            auto result = Expr::make(ExprKind::Move, previous().pos);
            result->left = parseUnary();
            return result;
        }
        if (consume(TokenKind::Amp)) {
            auto result = Expr::make(ExprKind::Borrow, previous().pos);
            result->mutableBorrow = consume("mut");
            result->left = parseUnary();
            return result;
        }
        return parsePostfix();
    }

    ExprPtr parsePostfix() {
        ExprPtr expression = parsePrimary();
        while (true) {
            if (consume(TokenKind::LParen)) {
                auto call = Expr::make(ExprKind::Call, expression->pos);
                call->callee = std::move(expression);
                if (!check(TokenKind::RParen)) {
                    do {
                        call->items.push_back(parseValueExpression());
                    } while (consume(TokenKind::Comma));
                }
                expect(TokenKind::RParen, "呼び出しの ')' が必要です");
                expression = std::move(call);
            } else if (consume(TokenKind::Dot)) {
                const Token member = expectIdentifier("'.' の後ろにはメンバー名が必要です");
                auto result = Expr::make(ExprKind::Member, member.pos);
                result->object = std::move(expression);
                result->text = member.text;
                expression = std::move(result);
            } else if (consume(TokenKind::LBracket)) {
                auto result = Expr::make(ExprKind::Index, previous().pos);
                result->object = std::move(expression);
                result->index = parseExpression();
                expect(TokenKind::RBracket, "インデックスの ']' が必要です");
                expression = std::move(result);
            } else {
                break;
            }
        }
        return expression;
    }

    ExprPtr parsePrimary() {
        if (check(TokenKind::Number)) {
            const Token token = advance();
            auto result = Expr::make(ExprKind::Literal, token.pos);
            result->literalKind = token.text.find_first_of(".eE") == std::string::npos ? LiteralKind::Integer : LiteralKind::Float;
            result->text = token.text;
            return result;
        }
        if (check(TokenKind::String)) {
            const Token token = advance();
            auto result = Expr::make(ExprKind::Literal, token.pos);
            result->literalKind = LiteralKind::String;
            result->text = token.text;
            return result;
        }
        if (consume(TokenKind::LParen)) {
            ExprPtr result = parseExpression();
            expect(TokenKind::RParen, "式の ')' が必要です");
            return result;
        }
        if (consume(TokenKind::LBracket)) {
            auto result = Expr::make(ExprKind::Array, previous().pos);
            if (!check(TokenKind::RBracket)) {
                do {
                    result->items.push_back(parseValueExpression());
                } while (consume(TokenKind::Comma));
            }
            expect(TokenKind::RBracket, "配列の ']' が必要です");
            return result;
        }
        if (consume(TokenKind::LBrace)) {
            auto result = Expr::make(ExprKind::Object, previous().pos);
            if (!check(TokenKind::RBrace)) {
                do {
                    const Token key = check(TokenKind::String) ? advance() : expectIdentifier("オブジェクトのキーが必要です");
                    expect(TokenKind::Colon, "オブジェクトのキーと値の間には ':' が必要です");
                    result->fields.emplace_back(key.text, parseValueExpression());
                } while (consume(TokenKind::Comma));
            }
            expect(TokenKind::RBrace, "オブジェクトの '}' が必要です");
            return result;
        }
        if (check(TokenKind::Identifier)) {
            const Token token = advance();
            if (consume(TokenKind::Arrow)) {
                auto result = Expr::make(ExprKind::Lambda, token.pos);
                result->lambdaParam = token.text;
                result->lambdaBody = parseCondition();
                return result;
            }
            auto result = Expr::make(ExprKind::Variable, token.pos);
            result->text = token.text;
            return result;
        }
        fail("E0106", peek().pos, "式が必要です");
    }
};




Program parseProgram(std::string source, std::string file) {
    return Parser(std::move(source), std::move(file)).parse();
}

ExprPtr parseExpression(std::string source, std::string file) {
    return Parser(std::move(source), std::move(file)).parseStandaloneExpression();
}

void mergeLibrary(Program &program, const Program &library) {
    if (!library.topLevel.empty()) {
        fail("E0117", library.topLevel.front()->pos,
             "ソースライブラリにはトップレベル実行文を含められません");
    }
    if (!library.optimizationRules.empty()) {
        fail("E0118", library.optimizationRules.front().pos,
             "ソースライブラリには rewrite を含められません（--opt-lib を使用してください）");
    }
    for (const auto &entry : library.functions) {
        if (entry.second.route) {
            fail("E0119", entry.second.pos,
                 "ソースライブラリには route 宣言を含められません");
        }
        if (program.functions.count(entry.first) != 0) {
            fail("E0104", entry.second.pos, "fn が重複しています: " + entry.first);
        }
    }
    for (const auto &entry : library.conditions) {
        if (program.conditions.count(entry.first) != 0) {
            fail("E0103", entry.second.pos, "condition が重複しています: " + entry.first);
        }
    }

    for (const auto &entry : library.conditions) {
        program.conditions.emplace(entry.first, entry.second);
    }
    for (const auto &entry : library.functions) {
        program.functions.emplace(entry.first, entry.second);
    }
}

void mergeOptimizationLibrary(Program &program, const Program &library) {
    if (!library.functions.empty() || !library.topLevel.empty()) {
        const SourcePos pos = !library.functions.empty()
                                  ? library.functions.begin()->second.pos
                                  : library.topLevel.front()->pos;
        fail("E0115", pos, "最適化ライブラリには rewrite と condition 以外を含められません");
    }
    for (const auto &entry : library.conditions) {
        if (program.conditions.count(entry.first) != 0) {
            fail("E0103", entry.second.pos, "condition が重複しています: " + entry.first);
        }
        program.conditions.emplace(entry.first, entry.second);
    }
    for (const OptimizationRule &rule : library.optimizationRules) {
        const auto duplicate = std::find_if(
            program.optimizationRules.begin(), program.optimizationRules.end(),
            [&](const OptimizationRule &existing) { return existing.name == rule.name; });
        if (duplicate != program.optimizationRules.end()) {
            fail("E0114", rule.pos, "rewrite が重複しています: " + rule.name);
        }
        program.optimizationRules.push_back(rule);
    }
}

} // namespace kond
