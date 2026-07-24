#include "kond_jit.hpp"

#ifndef KOND_HAS_LLVM_JIT
#define KOND_HAS_LLVM_JIT 0
#endif

#if KOND_HAS_LLVM_JIT

#include <llvm/ExecutionEngine/Orc/AbsoluteSymbols.h>
#include <llvm/ExecutionEngine/Orc/LLJIT.h>
#include <llvm/ExecutionEngine/Orc/Mangling.h>
#include <llvm/IR/Constants.h>
#include <llvm/IR/IRBuilder.h>
#include <llvm/IR/Intrinsics.h>
#include <llvm/IR/Module.h>
#include <llvm/IR/Verifier.h>
#include <llvm/Support/TargetSelect.h>

namespace kond {
namespace {

using namespace llvm;

thread_local bool jitPrintFirst = true;

extern "C" void kond_jit_print_begin() {
    jitPrintFirst = true;
}

extern "C" void kond_jit_print_i64(std::int64_t value) {
    if (!jitPrintFirst) std::cout << ' ';
    std::cout << value;
    jitPrintFirst = false;
}

extern "C" void kond_jit_print_bool(std::int64_t value) {
    if (!jitPrintFirst) std::cout << ' ';
    std::cout << (value != 0 ? "true" : "false");
    jitPrintFirst = false;
}

extern "C" void kond_jit_print_string(const char *value) {
    if (!jitPrintFirst) std::cout << ' ';
    std::cout << (value ? value : "null");
    jitPrintFirst = false;
}

extern "C" void kond_jit_print_end() {
    std::cout << '\n';
    jitPrintFirst = true;
}

extern "C" [[noreturn]] void kond_jit_fail(const char *message) {
    std::cerr << "error[EJIT]: " << (message ? message : "JIT guard failed") << '\n';
    std::exit(1);
}

class JitCompiler final {
public:
    JitCompiler(const Program &program, JitOptions options)
        : program_(program), options_(options) {
        InitializeNativeTarget();
        InitializeNativeTargetAsmPrinter();
        InitializeNativeTargetAsmParser();

        auto created = orc::LLJITBuilder().create();
        if (!created) {
            throw std::runtime_error("LLVM LLJIT の初期化に失敗しました: " + llvmError(created.takeError()));
        }
        jit_ = std::move(*created);

        context_ = std::make_unique<LLVMContext>();
        module_ = std::make_unique<Module>("kond.jit", *context_);
        module_->setTargetTriple(jit_->getTargetTriple());
        module_->setDataLayout(jit_->getDataLayout());
        builder_ = std::make_unique<IRBuilder<>>(*context_);

        declareRuntime();
        createFunctionPrototypes();
    }

    void run(const std::string &requestedEntry) {
        if (options_.verifiedMode) {
            throw std::runtime_error(
                "LLVM JIT backend は現在 --mode verified の静的証明をまだ扱えません。"
                " --mode safe または --mode unsafe を使用してください");
        }

        std::string entry = requestedEntry;
        if (entry.empty() && program_.functions.count("main") != 0) entry = "main";
        if (entry.empty()) {
            throw std::runtime_error("LLVM JIT backend はトップレベル文のみのプログラムをまだ扱えません。"
                                     " --entry で関数を指定してください");
        }
        auto found = program_.functions.find(entry);
        if (found == program_.functions.end()) {
            throw std::runtime_error("JIT のエントリ関数が見つかりません: " + entry);
        }
        if (!found->second.params.empty()) {
            throw std::runtime_error("JIT のエントリ関数は引数を取れません: " + entry);
        }

        compileFunction(entry);
        raw_string_ostream verifyOutput(verifyOutput_);
        const bool invalidModule = verifyModule(*module_, &verifyOutput);
        verifyOutput.flush();
        if (invalidModule) {
            throw std::runtime_error("LLVM IR の検証に失敗しました:\n" + verifyOutput_);
        }

        if (options_.dumpIr) {
            std::string ir;
            raw_string_ostream output(ir);
            module_->print(output, nullptr);
            output.flush();
            std::cout << ir;
        }

        defineRuntimeSymbols();
        if (auto error = jit_->addIRModule(
                orc::ThreadSafeModule(std::move(module_), std::move(context_)))) {
            throw std::runtime_error("LLVM IR モジュールを JIT に登録できません: " + llvmError(std::move(error)));
        }

        auto address = jit_->lookup(functionSymbol(entry));
        if (!address) throw std::runtime_error("JIT エントリを解決できません: " + llvmError(address.takeError()));
        using EntryPoint = std::int64_t();
        auto function = address->toPtr<EntryPoint>();
        (void)function();
    }

private:
    struct Local {
        AllocaInst *slot = nullptr;
        ConditionPtr invariant;
        bool mutableSlot = false;
        bool conditionValue = false;
    };

    using Scope = std::unordered_map<std::string, Local>;

    const Program &program_;
    JitOptions options_;
    std::unique_ptr<orc::LLJIT> jit_;
    std::unique_ptr<LLVMContext> context_;
    std::unique_ptr<Module> module_;
    std::unique_ptr<IRBuilder<>> builder_;
    std::unordered_map<std::string, Function *> functions_;
    std::unordered_map<std::string, Function *> conditions_;
    std::unordered_set<std::string> compilingFunctions_;
    std::unordered_set<std::string> compiledFunctions_;
    std::unordered_set<std::string> compilingConditions_;
    std::unordered_set<std::string> compiledConditions_;
    std::vector<Scope> scopes_;
    Function *currentFunction_ = nullptr;
    AllocaInst *returnSlot_ = nullptr;
    BasicBlock *returnBlock_ = nullptr;
    Function *printBegin_ = nullptr;
    Function *printI64_ = nullptr;
    Function *printBool_ = nullptr;
    Function *printString_ = nullptr;
    Function *printEnd_ = nullptr;
    Function *fail_ = nullptr;
    std::string verifyOutput_;

    static std::string llvmError(Error error) {
        return toString(std::move(error));
    }

    [[noreturn]] void unsupported(const SourcePos &pos, const std::string &message) const {
        std::ostringstream output;
        output << "LLVM JIT がこの構文を扱えません: " << message;
        if (!pos.file.empty()) output << " (" << pos.file << ':' << pos.line << ':' << pos.column << ')';
        throw std::runtime_error(output.str());
    }

    Type *i1() const { return Type::getInt1Ty(*context_); }
    Type *i64() const { return Type::getInt64Ty(*context_); }
    PointerType *i8Ptr() const { return PointerType::get(*context_, 0); }

    Function *declareExternal(const std::string &name, FunctionType *type) {
        return Function::Create(type, Function::ExternalLinkage, name, module_.get());
    }

    void declareRuntime() {
        printBegin_ = declareExternal("kond_jit_print_begin",
                                      FunctionType::get(Type::getVoidTy(*context_), {}, false));
        printI64_ = declareExternal("kond_jit_print_i64",
                                    FunctionType::get(Type::getVoidTy(*context_), {i64()}, false));
        printBool_ = declareExternal("kond_jit_print_bool",
                                     FunctionType::get(Type::getVoidTy(*context_), {i64()}, false));
        printString_ = declareExternal("kond_jit_print_string",
                                       FunctionType::get(Type::getVoidTy(*context_), {i8Ptr()}, false));
        printEnd_ = declareExternal("kond_jit_print_end",
                                    FunctionType::get(Type::getVoidTy(*context_), {}, false));
        fail_ = declareExternal("kond_jit_fail",
                                FunctionType::get(Type::getVoidTy(*context_), {i8Ptr()}, false));
        fail_->addFnAttr(Attribute::NoReturn);
    }

    void createFunctionPrototypes() {
        for (const auto &entry : program_.functions) {
            std::vector<Type *> parameters(entry.second.params.size(), i64());
            functions_.emplace(entry.first,
                               Function::Create(FunctionType::get(i64(), parameters, false),
                                                Function::ExternalLinkage,
                                                functionSymbol(entry.first), module_.get()));
        }
    }

    static std::string functionSymbol(const std::string &name) {
        return "kond.fn." + name;
    }

    static std::string conditionSymbol(const std::string &name) {
        return "kond.condition." + name;
    }

    static std::string qualifiedName(const ExprPtr &expression) {
        if (!expression) return {};
        if (expression->kind == ExprKind::Variable) return expression->text;
        if (expression->kind != ExprKind::Member) return {};
        const std::string prefix = qualifiedName(expression->object);
        return prefix.empty() ? std::string{} : prefix + "." + expression->text;
    }

    Function *functionFor(const std::string &name, const SourcePos &pos) {
        auto found = functions_.find(name);
        if (found == functions_.end()) unsupported(pos, "未定義の関数 " + name);
        return found->second;
    }

    Function *conditionFor(const std::string &name, const SourcePos &pos) {
        auto found = conditions_.find(name);
        if (found != conditions_.end()) return found->second;
        auto definition = program_.conditions.find(name);
        if (definition == program_.conditions.end()) unsupported(pos, "未定義の condition " + name);
        std::vector<Type *> parameters(definition->second.params.size(), i64());
        Function *function = Function::Create(FunctionType::get(i1(), parameters, false),
                                               Function::ExternalLinkage,
                                               conditionSymbol(name), module_.get());
        conditions_.emplace(name, function);
        return function;
    }

    void pushScope() { scopes_.emplace_back(); }

    void popScope() {
        if (!scopes_.empty()) scopes_.pop_back();
    }

    Local *findLocal(const std::string &name) {
        for (auto it = scopes_.rbegin(); it != scopes_.rend(); ++it) {
            auto found = it->find(name);
            if (found != it->end()) return &found->second;
        }
        return nullptr;
    }

    void defineLocal(const std::string &name, Local local, const SourcePos &pos) {
        if (scopes_.empty()) pushScope();
        if (scopes_.back().count(name) != 0) unsupported(pos, "同じスコープの束縛重複: " + name);
        scopes_.back().emplace(name, std::move(local));
    }

    Value *loadLocal(const std::string &name, const SourcePos &pos) {
        Local *local = findLocal(name);
        if (!local) unsupported(pos, "未定義の束縛 " + name);
        return builder_->CreateLoad(i64(), local->slot, name + ".load");
    }

    Value *stringLiteral(const std::string &value) {
        GlobalVariable *global = builder_->CreateGlobalString(value, ".kond.string");
        Value *zero = ConstantInt::get(Type::getInt32Ty(*context_), 0);
        return builder_->CreateInBoundsGEP(global->getValueType(), global, {zero, zero}, ".kond.string.ptr");
    }

    bool isConditionExpression(const ExprPtr &expression) const {
        if (!expression) return false;
        if (expression->kind == ExprKind::ConditionValue) return true;
        if (expression->kind == ExprKind::Unary && expression->op == "!") {
            return isConditionExpression(expression->left);
        }
        if (expression->kind == ExprKind::Variable) {
            const Local *local = nullptr;
            for (auto it = scopes_.rbegin(); it != scopes_.rend() && !local; ++it) {
                auto found = it->find(expression->text);
                if (found != it->end()) local = &found->second;
            }
            return local && local->conditionValue;
        }
        return false;
    }

    void emitPrintArgument(const ExprPtr &argument) {
        if (argument && argument->kind == ExprKind::Literal &&
            argument->literalKind == LiteralKind::String) {
            builder_->CreateCall(printString_, {stringLiteral(argument->text)});
            return;
        }
        Value *value = emitExpr(argument);
        if (isConditionExpression(argument)) {
            builder_->CreateCall(printBool_, {value});
        } else {
            builder_->CreateCall(printI64_, {value});
        }
    }

    void emitGuard(Value *condition, const std::string &message, const SourcePos &pos) {
        if (!condition || condition->getType() != i1()) unsupported(pos, "不正なガード条件");
        if (auto *constant = dyn_cast<ConstantInt>(condition)) {
            if (constant->isOne()) return;
            if (constant->isZero()) {
                builder_->CreateCall(fail_, {stringLiteral(message)});
                builder_->CreateUnreachable();
                BasicBlock *dead = BasicBlock::Create(*context_, "guard.dead", currentFunction_);
                builder_->SetInsertPoint(dead);
                return;
            }
        }
        Function *function = currentFunction_;
        BasicBlock *success = BasicBlock::Create(*context_, "guard.ok", function);
        BasicBlock *failure = BasicBlock::Create(*context_, "guard.fail", function);
        builder_->CreateCondBr(condition, success, failure);
        builder_->SetInsertPoint(failure);
        builder_->CreateCall(fail_, {stringLiteral(message)});
        builder_->CreateUnreachable();
        builder_->SetInsertPoint(success);
    }

    Value *checkedBinary(Intrinsic::ID intrinsic, Value *left, Value *right,
                         const SourcePos &pos, const std::string &operation) {
        Function *checked = Intrinsic::getOrInsertDeclaration(module_.get(), intrinsic, {i64()});
        Value *pair = builder_->CreateCall(checked, {left, right}, operation + ".checked");
        Value *result = builder_->CreateExtractValue(pair, {0}, operation + ".result");
        Value *overflow = builder_->CreateExtractValue(pair, {1}, operation + ".overflow");
        emitGuard(builder_->CreateNot(overflow), "Int64 の" + operation + "がオーバーフローします", pos);
        return result;
    }

    Value *checkedNeg(Value *value, const SourcePos &pos) {
        Value *minimum = ConstantInt::getSigned(i64(), std::numeric_limits<std::int64_t>::min());
        emitGuard(builder_->CreateICmpNE(value, minimum), "Int64 の単項マイナスがオーバーフローします", pos);
        return builder_->CreateNeg(value, "neg");
    }

    Value *checkedDivision(const std::string &op, Value *left, Value *right, const SourcePos &pos) {
        Value *zero = ConstantInt::get(i64(), 0);
        emitGuard(builder_->CreateICmpNE(right, zero), "ゼロ除算です", pos);
        Value *minimum = ConstantInt::getSigned(i64(), std::numeric_limits<std::int64_t>::min());
        Value *minusOne = ConstantInt::getSigned(i64(), -1);
        Value *overflow = builder_->CreateAnd(builder_->CreateICmpEQ(left, minimum),
                                              builder_->CreateICmpEQ(right, minusOne));
        emitGuard(builder_->CreateNot(overflow), "Int64 の" + op + "がオーバーフローします", pos);
        return op == "除算" ? builder_->CreateSDiv(left, right, "div")
                             : builder_->CreateSRem(left, right, "rem");
    }

    Value *emitExpr(const ExprPtr &expression) {
        if (!expression) return ConstantInt::get(i64(), 0);
        switch (expression->kind) {
        case ExprKind::Literal:
            if (expression->literalKind == LiteralKind::Integer) {
                try {
                    return ConstantInt::getSigned(i64(), std::stoll(expression->text));
                } catch (const std::exception &) {
                    unsupported(expression->pos, "Int64 リテラル " + expression->text);
                }
            }
            unsupported(expression->pos, "Float／String 値");
        case ExprKind::Variable:
            return loadLocal(expression->text, expression->pos);
        case ExprKind::ConditionValue:
            return builder_->CreateZExt(emitCondition(expression->condition), i64(), "condition.value");
        case ExprKind::Unary: {
            Value *value = emitExpr(expression->left);
            if (expression->op == "+") return value;
            if (expression->op == "-") return checkedNeg(value, expression->pos);
            if (expression->op == "!") {
                if (!isConditionExpression(expression->left)) {
                    unsupported(expression->pos, "条件値以外への '!'");
                }
                Value *truth = builder_->CreateICmpEQ(value, ConstantInt::get(i64(), 0));
                return builder_->CreateZExt(truth, i64(), "not");
            }
            unsupported(expression->pos, "単項演算子 " + expression->op);
        }
        case ExprKind::Binary: {
            Value *left = emitExpr(expression->left);
            Value *right = emitExpr(expression->right);
            if (expression->op == "+") return checkedBinary(Intrinsic::sadd_with_overflow, left, right, expression->pos, "加算");
            if (expression->op == "-") return checkedBinary(Intrinsic::ssub_with_overflow, left, right, expression->pos, "減算");
            if (expression->op == "*") return checkedBinary(Intrinsic::smul_with_overflow, left, right, expression->pos, "乗算");
            if (expression->op == "/") return checkedDivision("除算", left, right, expression->pos);
            if (expression->op == "%") return checkedDivision("剰余演算", left, right, expression->pos);
            if (expression->op == "&") return builder_->CreateAnd(left, right, "and");
            if (expression->op == "|") return builder_->CreateOr(left, right, "or");
            if (expression->op == "^") return builder_->CreateXor(left, right, "xor");
            unsupported(expression->pos, "二項演算子 " + expression->op);
        }
        case ExprKind::Call:
            return emitCall(expression);
        default:
            unsupported(expression->pos, "動的な値またはコレクション式");
        }
    }

    Value *emitCall(const ExprPtr &expression) {
        const std::string name = qualifiedName(expression->callee);
        if (name.empty()) unsupported(expression->pos, "間接呼び出し");
        if (name == "print") {
            builder_->CreateCall(printBegin_, {});
            for (const ExprPtr &argument : expression->items) emitPrintArgument(argument);
            builder_->CreateCall(printEnd_, {});
            return ConstantInt::get(i64(), 0);
        }

        std::vector<Value *> arguments;
        arguments.reserve(expression->items.size());
        for (const ExprPtr &argument : expression->items) arguments.push_back(emitExpr(argument));

        if (name == "abs" || name == "std.math.abs") {
            if (arguments.size() != 1) unsupported(expression->pos, "abs の引数個数");
            Value *value = arguments.front();
            Value *minimum = ConstantInt::getSigned(i64(), std::numeric_limits<std::int64_t>::min());
            emitGuard(builder_->CreateICmpNE(value, minimum), "Int64 の abs がオーバーフローします", expression->pos);
            Value *negative = builder_->CreateICmpSLT(value, ConstantInt::get(i64(), 0));
            return builder_->CreateSelect(negative, builder_->CreateNeg(value), value, "abs");
        }
        if (name == "std.math.min" || name == "std.math.max") {
            if (arguments.size() != 2) unsupported(expression->pos, name + " の引数個数");
            Value *comparison = name == "std.math.min"
                                    ? builder_->CreateICmpSLT(arguments[0], arguments[1])
                                    : builder_->CreateICmpSGT(arguments[0], arguments[1]);
            return builder_->CreateSelect(comparison, arguments[0], arguments[1], "minmax");
        }
        if (name == "std.math.clamp") {
            if (arguments.size() != 3) unsupported(expression->pos, "std.math.clamp の引数個数");
            Value *lower = builder_->CreateSelect(builder_->CreateICmpSLT(arguments[0], arguments[1]), arguments[1], arguments[0]);
            return builder_->CreateSelect(builder_->CreateICmpSGT(lower, arguments[2]), arguments[2], lower, "clamp");
        }
        if (program_.functions.count(name) == 0) unsupported(expression->pos, "未対応の関数 " + name);
        Function *callee = functionFor(name, expression->pos);
        const FunctionDef &definition = program_.functions.at(name);
        if (definition.params.size() != arguments.size()) {
            unsupported(expression->pos, "関数 " + name + " の引数個数");
        }
        compileFunction(name);
        return builder_->CreateCall(callee, arguments, "call." + name);
    }

    Value *emitShortCircuit(const std::vector<ConditionPtr> &items, bool conjunction,
                            const SourcePos &pos) {
        if (items.empty()) return conjunction ? ConstantInt::getTrue(*context_) : ConstantInt::getFalse(*context_);
        Value *result = emitCondition(items.front());
        for (std::size_t index = 1; index < items.size(); ++index) {
            Function *function = currentFunction_;
            BasicBlock *leftBlock = builder_->GetInsertBlock();
            if (!leftBlock) unsupported(pos, "条件の挿入位置");
            BasicBlock *rightBlock = BasicBlock::Create(*context_, "condition.rhs", function);
            BasicBlock *mergeBlock = BasicBlock::Create(*context_, "condition.merge", function);
            if (conjunction) {
                builder_->CreateCondBr(result, rightBlock, mergeBlock);
            } else {
                builder_->CreateCondBr(result, mergeBlock, rightBlock);
            }

            builder_->SetInsertPoint(rightBlock);
            Value *right = emitCondition(items[index]);
            BasicBlock *rightEnd = builder_->GetInsertBlock();
            if (!rightEnd->getTerminator()) builder_->CreateBr(mergeBlock);

            builder_->SetInsertPoint(mergeBlock);
            PHINode *phi = builder_->CreatePHI(i1(), 2, "short.condition");
            phi->addIncoming(conjunction ? ConstantInt::getFalse(*context_) : ConstantInt::getTrue(*context_),
                             leftBlock);
            phi->addIncoming(right, rightEnd);
            result = phi;
        }
        return result;
    }

    Value *emitCondition(const ConditionPtr &condition) {
        if (!condition) return ConstantInt::getTrue(*context_);
        switch (condition->kind) {
        case ConditionKind::Always:
            return ConstantInt::getTrue(*context_);
        case ConditionKind::Never:
            return ConstantInt::getFalse(*context_);
        case ConditionKind::Not:
            return builder_->CreateNot(emitCondition(condition->items.front()), "not.condition");
        case ConditionKind::And:
            return emitShortCircuit(condition->items, true, condition->pos);
        case ConditionKind::Or:
            return emitShortCircuit(condition->items, false, condition->pos);
        case ConditionKind::ValueRef: {
            if (!isConditionExpression(condition->value)) {
                unsupported(condition->pos, "Int64 値を条件として使用する式");
            }
            Value *value = emitExpr(condition->value);
            return builder_->CreateICmpNE(value, ConstantInt::get(i64(), 0), "condition.value");
        }
        case ConditionKind::Is: {
            // Every value entering this backend is an Int64.  Evaluate the
            // expression for side-effect ordering, then answer the type fact.
            (void)emitExpr(condition->left);
            if (condition->predicate == "Int" || condition->predicate == "Number" || condition->predicate == "Finite") {
                return ConstantInt::getTrue(*context_);
            }
            if (condition->predicate == "Bool" || condition->predicate == "String" ||
                condition->predicate == "List" || condition->predicate == "Object" ||
                condition->predicate == "Null") {
                return ConstantInt::getFalse(*context_);
            }
            unsupported(condition->pos, "is " + condition->predicate);
        }
        case ConditionKind::Has:
            unsupported(condition->pos, "has shape 条件");
        case ConditionKind::Relation: {
            Value *left = emitExpr(condition->left);
            Value *right = emitExpr(condition->right);
            if (condition->op == "==") return builder_->CreateICmpEQ(left, right, "eq");
            if (condition->op == "!=") return builder_->CreateICmpNE(left, right, "ne");
            if (condition->op == "<") return builder_->CreateICmpSLT(left, right, "lt");
            if (condition->op == "<=") return builder_->CreateICmpSLE(left, right, "le");
            if (condition->op == ">") return builder_->CreateICmpSGT(left, right, "gt");
            if (condition->op == ">=") return builder_->CreateICmpSGE(left, right, "ge");
            unsupported(condition->pos, "関係演算子 " + condition->op);
        }
        case ConditionKind::Call:
            return emitConditionCall(condition);
        }
        unsupported(condition->pos, "条件式");
    }

    Value *emitConditionCall(const ConditionPtr &condition) {
        const std::string &name = condition->predicate;
        if (name == "std.pred.is_int" || name == "std.pred.is_number" || name == "std.pred.is_finite") {
            for (const ExprPtr &argument : condition->args) (void)emitExpr(argument);
            if (condition->args.size() != 1) unsupported(condition->pos, name + " の引数個数");
            return ConstantInt::getTrue(*context_);
        }
        if (name == "std.pred.positive" || name == "std.pred.nonnegative" ||
            name == "std.pred.negative" || name == "std.pred.even" || name == "std.pred.odd") {
            if (condition->args.size() != 1) unsupported(condition->pos, name + " の引数個数");
            Value *value = emitExpr(condition->args.front());
            if (name == "std.pred.positive") return builder_->CreateICmpSGT(value, ConstantInt::get(i64(), 0));
            if (name == "std.pred.nonnegative") return builder_->CreateICmpSGE(value, ConstantInt::get(i64(), 0));
            if (name == "std.pred.negative") return builder_->CreateICmpSLT(value, ConstantInt::get(i64(), 0));
            Value *remainder = builder_->CreateSRem(value, ConstantInt::get(i64(), 2));
            Value *even = builder_->CreateICmpEQ(remainder, ConstantInt::get(i64(), 0));
            return name == "std.pred.even" ? even : builder_->CreateNot(even);
        }
        if (name == "std.pred.equal" || name == "std.pred.not_equal") {
            if (condition->args.size() != 2) unsupported(condition->pos, name + " の引数個数");
            Value *left = emitExpr(condition->args[0]);
            Value *right = emitExpr(condition->args[1]);
            Value *equal = builder_->CreateICmpEQ(left, right);
            return name == "std.pred.equal" ? equal : builder_->CreateNot(equal);
        }
        if (program_.conditions.count(name) == 0) unsupported(condition->pos, "未対応の条件呼び出し " + name);
        Function *callee = conditionFor(name, condition->pos);
        const ConditionDef &definition = program_.conditions.at(name);
        if (definition.params.size() != condition->args.size()) {
            unsupported(condition->pos, "condition " + name + " の引数個数");
        }
        std::vector<Value *> arguments;
        arguments.reserve(condition->args.size());
        for (const ExprPtr &argument : condition->args) arguments.push_back(emitExpr(argument));
        compileCondition(name);
        return builder_->CreateCall(callee, arguments, "condition.call." + name);
    }

    Value *emitConditionWithSelf(const ConditionPtr &condition, AllocaInst *self) {
        pushScope();
        defineLocal("self", Local{self, nullptr, false}, condition ? condition->pos : SourcePos{});
        Value *result = emitCondition(condition);
        popScope();
        return result;
    }

    bool emitStatement(const StatementPtr &statement) {
        if (!statement) return false;
        switch (statement->kind) {
        case StatementKind::Block: {
            pushScope();
            bool terminated = false;
            for (const StatementPtr &item : statement->statements) {
                if (terminated) break;
                terminated = emitStatement(item);
            }
            popScope();
            return terminated;
        }
        case StatementKind::Let: {
            AllocaInst *slot = builder_->CreateAlloca(i64(), nullptr, statement->name);
            Value *value = emitExpr(statement->expr);
            builder_->CreateStore(value, slot);
            defineLocal(statement->name, Local{slot, statement->invariant, statement->invariant != nullptr,
                                               isConditionExpression(statement->expr)}, statement->pos);
            if (statement->invariant) {
                emitGuard(emitConditionWithSelf(statement->invariant, slot),
                          "let の不変条件を満たしていません", statement->pos);
            }
            return false;
        }
        case StatementKind::Expression:
            (void)emitExpr(statement->expr);
            return false;
        case StatementKind::Assign: {
            if (!statement->target || statement->target->kind != ExprKind::Variable) {
                unsupported(statement->pos, "メンバー／インデックスへの代入");
            }
            Local *local = findLocal(statement->target->text);
            if (!local) unsupported(statement->pos, "未定義の代入対象 " + statement->target->text);
            if (!local->mutableSlot) unsupported(statement->pos, "不変束縛への代入 " + statement->target->text);
            Value *rhs = emitExpr(statement->expr);
            Value *value = rhs;
            if (statement->assignmentOp != "=") {
                Value *current = builder_->CreateLoad(i64(), local->slot, "assign.current");
                const std::string op = statement->assignmentOp.substr(0, 1);
                if (op == "+") value = checkedBinary(Intrinsic::sadd_with_overflow, current, rhs, statement->pos, "加算");
                else if (op == "-") value = checkedBinary(Intrinsic::ssub_with_overflow, current, rhs, statement->pos, "減算");
                else if (op == "*") value = checkedBinary(Intrinsic::smul_with_overflow, current, rhs, statement->pos, "乗算");
                else if (op == "/") value = checkedDivision("除算", current, rhs, statement->pos);
                else unsupported(statement->pos, "複合代入 " + statement->assignmentOp);
            }
            builder_->CreateStore(value, local->slot);
            if (statement->assignmentOp == "=") local->conditionValue = isConditionExpression(statement->expr);
            if (local->invariant) {
                emitGuard(emitConditionWithSelf(local->invariant, local->slot),
                          "代入後の不変条件を満たしていません", statement->pos);
            }
            return false;
        }
        case StatementKind::Check:
        case StatementKind::Require:
            emitGuard(emitCondition(statement->condition), "条件が成立しません", statement->pos);
            return false;
        case StatementKind::Prove:
            // `prove` is a compile-time obligation in Kond.  A runtime guard
            // would silently weaken that contract, so the partial backend
            // rejects it instead of lowering it as `check`.
            unsupported(statement->pos, "prove（静的証明）");
        case StatementKind::Assume:
            if (!options_.unsafeMode) unsupported(statement->pos, "safe mode の assume");
            return false;
        case StatementKind::If: {
            Value *condition = emitCondition(statement->condition);
            Function *function = currentFunction_;
            BasicBlock *thenBlock = BasicBlock::Create(*context_, "if.then", function);
            BasicBlock *mergeBlock = BasicBlock::Create(*context_, "if.end", function);
            BasicBlock *elseBlock = statement->elseBody
                                        ? BasicBlock::Create(*context_, "if.else", function)
                                        : mergeBlock;
            builder_->CreateCondBr(condition, thenBlock, elseBlock);

            builder_->SetInsertPoint(thenBlock);
            const bool thenTerminated = emitStatement(statement->body);
            if (!thenTerminated) builder_->CreateBr(mergeBlock);

            bool elseTerminated = false;
            if (statement->elseBody) {
                builder_->SetInsertPoint(elseBlock);
                elseTerminated = emitStatement(statement->elseBody);
                if (!elseTerminated) builder_->CreateBr(mergeBlock);
            }

            builder_->SetInsertPoint(mergeBlock);
            if (statement->elseBody && thenTerminated && elseTerminated) {
                builder_->CreateUnreachable();
                return true;
            }
            return false;
        }
        case StatementKind::While: {
            Function *function = currentFunction_;
            BasicBlock *conditionBlock = BasicBlock::Create(*context_, "while.condition", function);
            BasicBlock *bodyBlock = BasicBlock::Create(*context_, "while.body", function);
            BasicBlock *afterBlock = BasicBlock::Create(*context_, "while.end", function);
            builder_->CreateBr(conditionBlock);
            builder_->SetInsertPoint(conditionBlock);
            builder_->CreateCondBr(emitCondition(statement->condition), bodyBlock, afterBlock);
            builder_->SetInsertPoint(bodyBlock);
            const bool bodyTerminated = emitStatement(statement->body);
            if (!bodyTerminated) builder_->CreateBr(conditionBlock);
            builder_->SetInsertPoint(afterBlock);
            return false;
        }
        case StatementKind::Return: {
            Value *value = statement->expr ? emitExpr(statement->expr) : ConstantInt::get(i64(), 0);
            builder_->CreateStore(value, returnSlot_);
            builder_->CreateBr(returnBlock_);
            return true;
        }
        case StatementKind::UnsafeBlock:
            return emitStatement(statement->body);
        case StatementKind::Update:
        case StatementKind::Match:
        case StatementKind::For:
            if (statement->kind == StatementKind::Update) unsupported(statement->pos, "update");
            if (statement->kind == StatementKind::Match) unsupported(statement->pos, "match");
            unsupported(statement->pos, "for");
        }
        unsupported(statement->pos, "文");
    }

    struct SavedState {
        IRBuilderBase::InsertPoint insertPoint;
        Function *function = nullptr;
        AllocaInst *returnSlot = nullptr;
        BasicBlock *returnBlock = nullptr;
        std::vector<Scope> scopes;
    };

    SavedState saveState() {
        SavedState state;
        state.insertPoint = builder_->saveIP();
        state.function = currentFunction_;
        state.returnSlot = returnSlot_;
        state.returnBlock = returnBlock_;
        state.scopes = std::move(scopes_);
        return state;
    }

    void restoreState(SavedState state) {
        currentFunction_ = state.function;
        returnSlot_ = state.returnSlot;
        returnBlock_ = state.returnBlock;
        scopes_ = std::move(state.scopes);
        if (state.insertPoint.isSet()) builder_->restoreIP(state.insertPoint);
        else builder_->ClearInsertionPoint();
    }

    void compileFunction(const std::string &name) {
        if (compiledFunctions_.count(name) != 0 || compilingFunctions_.count(name) != 0) return;
        auto definition = program_.functions.find(name);
        if (definition == program_.functions.end()) throw std::runtime_error("JIT function not found: " + name);
        compilingFunctions_.insert(name);
        SavedState saved = saveState();
        Function *function = functionFor(name, definition->second.pos);
        BasicBlock *entry = BasicBlock::Create(*context_, "entry", function);
        BasicBlock *returnBlock = BasicBlock::Create(*context_, "return", function);
        builder_->SetInsertPoint(entry);
        currentFunction_ = function;
        returnBlock_ = returnBlock;
        returnSlot_ = builder_->CreateAlloca(i64(), nullptr, "return.slot");
        scopes_.clear();
        pushScope();

        auto argument = function->arg_begin();
        for (const Param &param : definition->second.params) {
            AllocaInst *slot = builder_->CreateAlloca(i64(), nullptr, param.name);
            builder_->CreateStore(&*argument++, slot);
            defineLocal(param.name, Local{slot, param.invariant, param.invariant != nullptr, false}, param.pos);
            if (param.invariant) {
                emitGuard(emitConditionWithSelf(param.invariant, slot),
                          "関数引数の不変条件を満たしていません", param.pos);
            }
        }
        for (const ConditionPtr &requirement : definition->second.requiresList) {
            emitGuard(emitCondition(requirement), "関数の requires を満たしていません", requirement->pos);
        }

        const bool bodyTerminated = emitStatement(definition->second.body);
        if (!bodyTerminated) {
            builder_->CreateStore(ConstantInt::get(i64(), 0), returnSlot_);
            builder_->CreateBr(returnBlock);
        }

        builder_->SetInsertPoint(returnBlock);
        Value *result = builder_->CreateLoad(i64(), returnSlot_, "result");
        pushScope();
        defineLocal("result", Local{returnSlot_, nullptr, false, false}, definition->second.pos);
        for (const ConditionPtr &ensure : definition->second.ensures) {
            emitGuard(emitCondition(ensure), "関数の ensures を満たしていません", ensure->pos);
        }
        popScope();
        builder_->CreateRet(result);

        restoreState(std::move(saved));
        compilingFunctions_.erase(name);
        compiledFunctions_.insert(name);
    }

    void compileCondition(const std::string &name) {
        if (compiledConditions_.count(name) != 0 || compilingConditions_.count(name) != 0) return;
        auto definition = program_.conditions.find(name);
        if (definition == program_.conditions.end()) throw std::runtime_error("JIT condition not found: " + name);
        compilingConditions_.insert(name);
        SavedState saved = saveState();
        Function *function = conditionFor(name, definition->second.pos);
        BasicBlock *entry = BasicBlock::Create(*context_, "entry", function);
        builder_->SetInsertPoint(entry);
        currentFunction_ = function;
        returnSlot_ = nullptr;
        returnBlock_ = nullptr;
        scopes_.clear();
        pushScope();
        auto argument = function->arg_begin();
        for (const Param &param : definition->second.params) {
            AllocaInst *slot = builder_->CreateAlloca(i64(), nullptr, param.name);
            builder_->CreateStore(&*argument++, slot);
            defineLocal(param.name, Local{slot, nullptr, false, false}, param.pos);
        }
        builder_->CreateRet(emitCondition(definition->second.body));
        restoreState(std::move(saved));
        compilingConditions_.erase(name);
        compiledConditions_.insert(name);
    }

    void defineRuntimeSymbols() {
        orc::MangleAndInterner mangle(jit_->getExecutionSession(), jit_->getDataLayout());
        orc::SymbolMap symbols;
        symbols[mangle("kond_jit_print_begin")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_print_begin);
        symbols[mangle("kond_jit_print_i64")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_print_i64);
        symbols[mangle("kond_jit_print_bool")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_print_bool);
        symbols[mangle("kond_jit_print_string")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_print_string);
        symbols[mangle("kond_jit_print_end")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_print_end);
        symbols[mangle("kond_jit_fail")] = orc::ExecutorSymbolDef::fromPtr(&kond_jit_fail);
        if (auto error = jit_->getMainJITDylib().define(orc::absoluteSymbols(std::move(symbols)))) {
            throw std::runtime_error("JIT ランタイムシンボルを登録できません: " + llvmError(std::move(error)));
        }
    }
};

} // namespace

bool llvmJitAvailable() { return true; }

void runJit(const Program &program, const std::string &entry, const JitOptions &options) {
    JitCompiler compiler(program, options);
    compiler.run(entry);
}

} // namespace kond

#else

namespace kond {

bool llvmJitAvailable() { return false; }

void runJit(const Program &, const std::string &, const JitOptions &) {
    throw std::runtime_error(
        "このビルドでは LLVM JIT が無効です。LLVM と llvm-config を導入して再ビルドしてください");
}

} // namespace kond

#endif
