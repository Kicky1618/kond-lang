#pragma once

#include "kond_value.hpp"

namespace kond {

struct Binding {
    std::string name;
    Value value;
    ConditionPtr invariant;
    bool mutableSlot = false;
    bool moved = false;
    bool known = false;
    std::uint64_t version = 0;
    std::size_t sharedBorrows = 0;
    bool uniqueBorrow = false;
    SourcePos pos;
    std::weak_ptr<OwnershipLog> ownershipLog;
};

inline BorrowHandle::~BorrowHandle() {
    if (auto binding = owner.lock()) {
        if (kind == BorrowKind::Shared) {
            if (binding->sharedBorrows > 0) {
                --binding->sharedBorrows;
            }
        } else {
            binding->uniqueBorrow = false;
        }
        if (auto ownershipLog = log.lock()) {
            const bool restored = binding->sharedBorrows == 0 && !binding->uniqueBorrow;
            ownershipLog->record(bindingName,
                                 restored ? "BorrowEnd -> Own(v" + std::to_string(binding->version) + ")"
                                          : "SharedBorrowEnd -> SharedBorrowed",
                                 pos);
        }
    }
}

inline Value Value::referenceValue(std::shared_ptr<Binding> binding, BorrowKind kind, const SourcePos &pos) {
    Value result;
    result.kind = ValueKind::Reference;
    result.reference = binding;
    result.borrow = std::make_shared<BorrowHandle>();
    result.borrow->owner = binding;
    result.borrow->log = binding->ownershipLog;
    result.borrow->kind = kind;
    result.borrow->bindingName = binding->name;
    result.borrow->pos = pos;
    const bool alreadyShared = binding->sharedBorrows != 0;
    if (kind == BorrowKind::Shared) {
        ++binding->sharedBorrows;
    } else {
        binding->uniqueBorrow = true;
    }
    if (auto ownershipLog = binding->ownershipLog.lock()) {
        ownershipLog->record(binding->name,
                             kind == BorrowKind::Unique
                                 ? "Own -> UniqueBorrow"
                                 : (alreadyShared ? "SharedBorrowed -> SharedBorrowed" : "Own -> SharedBorrow"),
                             pos);
    }
    return result;
}

inline Value Value::methodValue(Value receiver, std::string methodName) {
    Value result;
    result.kind = ValueKind::Method;
    result.name = std::move(methodName);
    result.methodReceiver = std::make_shared<Value>(std::move(receiver));
    return result;
}

struct RuntimeCondition {
    struct Capture {
        std::string name;
        std::shared_ptr<Binding> binding;
        std::uint64_t version = 0;
    };

    ConditionPtr condition;
    std::vector<Capture> captures;
    bool holds = false;
    EvidenceKind evidence = EvidenceKind::RuntimeProof;
    std::uint32_t flow = FlowPublic;
};

inline Value Value::conditionValue(std::shared_ptr<RuntimeCondition> value) {
    Value result;
    result.kind = ValueKind::Condition;
    result.flow = value ? value->flow : FlowPublic;
    result.condition = std::move(value);
    return result;
}

class Environment {
public:
    struct Frame {
        std::unordered_map<std::string, std::shared_ptr<Binding>> bindings;
        bool unsafe = false;
    };

    // Owns one environment scope so exceptional control flow cannot leak it.
    class Scope final {
    public:
        Scope(Environment &environment, bool unsafe) : environment_(&environment) {
            environment_->pushScope(unsafe);
        }

        Scope(const Scope &) = delete;
        Scope &operator=(const Scope &) = delete;

        Scope(Scope &&other) noexcept : environment_(std::exchange(other.environment_, nullptr)) {}

        Scope &operator=(Scope &&other) noexcept {
            if (this == &other) return *this;
            if (environment_) environment_->popScope();
            environment_ = std::exchange(other.environment_, nullptr);
            return *this;
        }

        ~Scope() {
            if (environment_) environment_->popScope();
        }

    private:
        Environment *environment_ = nullptr;
    };

    explicit Environment(std::shared_ptr<OwnershipLog> ownershipLog = {})
        : ownershipLog_(std::move(ownershipLog)) {
        pushScope(false);
    }

    void pushScope(bool unsafe) {
        frames_.push_back(Frame{});
        frames_.back().unsafe = unsafe;
        if (unsafe) {
            ++unsafeDepth_;
        }
        facts_.emplace_back();
    }

    void popScope() {
        if (frames_.empty()) return;
        if (frames_.back().unsafe && unsafeDepth_ > 0) {
            --unsafeDepth_;
        }
        frames_.pop_back();
        facts_.pop_back();
    }

    [[nodiscard]] Scope scoped(bool unsafe) { return Scope(*this, unsafe); }

    std::shared_ptr<Binding> define(std::string name, Value value, ConditionPtr invariant, bool mutableSlot, bool known,
                                    SourcePos pos, bool trackOwnership = true) {
        if (frames_.back().bindings.count(name) != 0) {
            fail("E1402", pos, "同じスコープで束縛が重複しています: " + name);
        }
        auto binding = std::make_shared<Binding>();
        binding->name = name;
        binding->value = std::move(value);
        binding->invariant = std::move(invariant);
        binding->mutableSlot = mutableSlot;
        binding->known = known;
        binding->pos = std::move(pos);
        binding->ownershipLog = ownershipLog_;
        if (trackOwnership && ownershipLog_) {
            ownershipLog_->record(binding->name, "Initial -> Own(v0)", binding->pos);
        }
        frames_.back().bindings.emplace(std::move(name), binding);
        return binding;
    }

    void defineExisting(const std::string &name, std::shared_ptr<Binding> binding, const SourcePos &pos) {
        if (frames_.back().bindings.count(name) != 0) {
            fail("E1402", pos, "同じスコープで束縛が重複しています: " + name);
        }
        if (binding->ownershipLog.expired()) binding->ownershipLog = ownershipLog_;
        frames_.back().bindings.emplace(name, std::move(binding));
    }

    std::shared_ptr<Binding> find(const std::string &name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            auto found = it->bindings.find(name);
            if (found != it->bindings.end()) {
                return found->second;
            }
        }
        return nullptr;
    }

    std::shared_ptr<Binding> require(const std::string &name, const SourcePos &pos) const {
        auto binding = find(name);
        if (!binding) {
            fail("E1403", pos, "未定義の束縛です: " + name);
        }
        if (binding->moved) {
            fail("E2101", pos, "所有権が移動済みの束縛を使用しています: " + name);
        }
        if (binding->uniqueBorrow) {
            fail("E2203", pos, "可変借用中の値へ所有者から直接アクセスしています: " + name);
        }
        return binding;
    }

    bool unsafe() const { return unsafeDepth_ > 0; }

    void addFact(std::string fact) {
        facts_.back().insert(std::move(fact));
    }

    bool hasFact(const std::string &fact) const {
        for (auto it = facts_.rbegin(); it != facts_.rend(); ++it) {
            if (it->count(fact) != 0) return true;
        }
        return false;
    }

private:
    std::vector<Frame> frames_;
    std::vector<std::unordered_set<std::string>> facts_;
    std::shared_ptr<OwnershipLog> ownershipLog_;
    int unsafeDepth_ = 0;
};

class OwnershipChecker {
public:
    explicit OwnershipChecker(const Program &program) : program_(program) {}

    void check() {
        std::vector<std::string> conditionNames;
        conditionNames.reserve(program_.conditions.size());
        for (const auto &entry : program_.conditions) conditionNames.push_back(entry.first);
        std::sort(conditionNames.begin(), conditionNames.end());
        for (const std::string &name : conditionNames) {
            reset();
            const ConditionDef &definition = program_.conditions.at(name);
            for (const Param &param : definition.params) define(param.name, param.pos);
            checkCondition(definition.body);
        }

        std::vector<std::string> functionNames;
        functionNames.reserve(program_.functions.size());
        for (const auto &entry : program_.functions) functionNames.push_back(entry.first);
        std::sort(functionNames.begin(), functionNames.end());
        for (const std::string &name : functionNames) checkFunction(program_.functions.at(name));

        reset();
        for (const StatementPtr &statement : program_.topLevel) checkStatement(statement);
    }

private:
    struct StaticBinding {
        std::string name;
        SourcePos pos;
        bool moved = false;
        std::size_t sharedBorrows = 0;
        bool uniqueBorrow = false;
        std::weak_ptr<StaticBinding> aliasOwner;
        std::optional<BorrowKind> aliasKind;
    };

    struct Frame {
        std::unordered_map<std::string, std::shared_ptr<StaticBinding>> bindings;
        std::vector<std::shared_ptr<StaticBinding>> declarationOrder;
    };

    struct BorrowEffect {
        std::shared_ptr<StaticBinding> owner;
        BorrowKind kind = BorrowKind::Shared;
    };

    struct StateImage {
        bool moved = false;
        std::size_t sharedBorrows = 0;
        bool uniqueBorrow = false;
    };

    using Snapshot = std::unordered_map<StaticBinding *, StateImage>;

    const Program &program_;
    std::vector<Frame> frames_;

    void reset() {
        frames_.clear();
        pushScope();
    }

    void pushScope() {
        frames_.push_back(Frame{});
    }

    void popScope() {
        if (frames_.empty()) return;
        for (auto it = frames_.back().declarationOrder.rbegin();
             it != frames_.back().declarationOrder.rend(); ++it) {
            releaseAlias(*it);
        }
        frames_.pop_back();
    }

    std::shared_ptr<StaticBinding> define(const std::string &name, const SourcePos &pos,
                                          const std::optional<BorrowEffect> &borrow = std::nullopt) {
        if (frames_.back().bindings.count(name) != 0) {
            fail("E1402", pos, "同じスコープで束縛が重複しています: " + name);
        }
        auto binding = std::make_shared<StaticBinding>();
        binding->name = name;
        binding->pos = pos;
        if (borrow) {
            binding->aliasOwner = borrow->owner;
            binding->aliasKind = borrow->kind;
        }
        frames_.back().bindings[name] = binding;
        frames_.back().declarationOrder.push_back(binding);
        return binding;
    }

    std::shared_ptr<StaticBinding> find(const std::string &name) const {
        for (auto it = frames_.rbegin(); it != frames_.rend(); ++it) {
            auto found = it->bindings.find(name);
            if (found != it->bindings.end()) return found->second;
        }
        return nullptr;
    }

    void releaseBorrow(const BorrowEffect &borrow) {
        if (!borrow.owner) return;
        if (borrow.kind == BorrowKind::Shared) {
            if (borrow.owner->sharedBorrows > 0) --borrow.owner->sharedBorrows;
        } else {
            borrow.owner->uniqueBorrow = false;
        }
    }

    void releaseAlias(const std::shared_ptr<StaticBinding> &binding) {
        if (!binding || !binding->aliasKind) return;
        if (auto owner = binding->aliasOwner.lock()) {
            releaseBorrow(BorrowEffect{owner, *binding->aliasKind});
        }
        binding->aliasKind.reset();
        binding->aliasOwner.reset();
    }

    void releaseTemporary(const std::optional<BorrowEffect> &borrow) {
        if (borrow) releaseBorrow(*borrow);
    }

    void requireReadable(const std::shared_ptr<StaticBinding> &binding, const SourcePos &pos) const {
        if (!binding) return;
        if (binding->moved) {
            fail("E2101", pos, "所有権が移動済みの束縛を使用しています: " + binding->name);
        }
        if (binding->aliasKind) return;
        if (binding->uniqueBorrow) {
            fail("E2203", pos, "可変借用中の値へ所有者から直接アクセスしています: " + binding->name);
        }
    }

    std::shared_ptr<StaticBinding> rootBinding(const ExprPtr &expression) {
        if (!expression) return nullptr;
        if (expression->kind == ExprKind::Variable) return find(expression->text);
        if (expression->kind == ExprKind::Member) return rootBinding(expression->object);
        if (expression->kind == ExprKind::Index) {
            releaseTemporary(checkExpr(expression->index));
            return rootBinding(expression->object);
        }
        return nullptr;
    }

    void requireWritable(const ExprPtr &expression, const SourcePos &pos) {
        auto binding = rootBinding(expression);
        if (!binding) return;
        if (binding->moved) {
            fail("E2101", pos, "移動済みの束縛を変更しています: " + binding->name);
        }
        if (binding->aliasKind) {
            if (*binding->aliasKind == BorrowKind::Shared) {
                fail("E2205", pos, "共有借用から値を変更できません: " + binding->name);
            }
            return;
        }
        if (binding->sharedBorrows != 0) {
            fail("E2205", pos, "共有借用中の値を変更できません: " + binding->name);
        }
        if (binding->uniqueBorrow) {
            fail("E2203", pos, "可変借用中の値を所有者から変更できません: " + binding->name);
        }
    }

    std::optional<BorrowEffect> checkExpr(const ExprPtr &expression) {
        if (!expression) return std::nullopt;
        switch (expression->kind) {
        case ExprKind::Literal:
            return std::nullopt;
        case ExprKind::Variable:
            requireReadable(find(expression->text), expression->pos);
            return std::nullopt;
        case ExprKind::Array:
            for (const ExprPtr &item : expression->items) releaseTemporary(checkExpr(item));
            return std::nullopt;
        case ExprKind::Object:
            for (const auto &field : expression->fields) releaseTemporary(checkExpr(field.second));
            return std::nullopt;
        case ExprKind::Unary:
            releaseTemporary(checkExpr(expression->left));
            return std::nullopt;
        case ExprKind::Binary:
            releaseTemporary(checkExpr(expression->left));
            releaseTemporary(checkExpr(expression->right));
            return std::nullopt;
        case ExprKind::Member:
            releaseTemporary(checkExpr(expression->object));
            return std::nullopt;
        case ExprKind::Index:
            releaseTemporary(checkExpr(expression->object));
            releaseTemporary(checkExpr(expression->index));
            return std::nullopt;
        case ExprKind::Call: {
            if (expression->callee && expression->callee->kind == ExprKind::Member) {
                const std::string &method = expression->callee->text;
                if (method == "push" || method == "pop") {
                    requireWritable(expression->callee->object, expression->pos);
                } else {
                    releaseTemporary(checkExpr(expression->callee->object));
                }
            } else if (expression->callee && expression->callee->kind != ExprKind::Variable) {
                releaseTemporary(checkExpr(expression->callee));
            }
            for (const ExprPtr &item : expression->items) releaseTemporary(checkExpr(item));
            return std::nullopt;
        }
        case ExprKind::Lambda:
            pushScope();
            define(expression->lambdaParam, expression->pos);
            checkCondition(expression->lambdaBody);
            popScope();
            return std::nullopt;
        case ExprKind::ConditionValue:
            checkCondition(expression->condition);
            return std::nullopt;
        case ExprKind::Move: {
            if (!expression->left || expression->left->kind != ExprKind::Variable) {
                fail("E2102", expression->pos, "move の対象は束縛名である必要があります");
            }
            auto binding = find(expression->left->text);
            if (!binding) return std::nullopt;
            if (binding->moved) {
                fail("E2101", expression->pos, "所有権がすでに移動済みです: " + binding->name);
            }
            if (!binding->aliasKind &&
                (binding->sharedBorrows != 0 || binding->uniqueBorrow)) {
                fail("E2203", expression->pos, "借用中の値は move できません: " + binding->name);
            }
            std::optional<BorrowEffect> transferred;
            if (binding->aliasKind) {
                if (auto owner = binding->aliasOwner.lock()) {
                    transferred = BorrowEffect{owner, *binding->aliasKind};
                    binding->aliasKind.reset();
                    binding->aliasOwner.reset();
                }
            }
            binding->moved = true;
            return transferred;
        }
        case ExprKind::Borrow: {
            if (!expression->left || expression->left->kind != ExprKind::Variable) {
                fail("E2201", expression->pos, "借用の対象は束縛名である必要があります");
            }
            auto binding = find(expression->left->text);
            if (!binding) return std::nullopt;
            if (binding->moved) {
                fail("E2101", expression->pos, "移動済みの値は借用できません: " + binding->name);
            }
            if (binding->aliasKind) {
                fail("E2203", expression->pos, "借用参照の再借用には明示的な領域指定が必要です");
            }
            const BorrowKind kind = expression->mutableBorrow ? BorrowKind::Unique : BorrowKind::Shared;
            if (kind == BorrowKind::Unique) {
                if (binding->sharedBorrows != 0 || binding->uniqueBorrow) {
                    fail("E2203", expression->pos, "可変借用が既存の借用と競合しています: " + binding->name);
                }
                binding->uniqueBorrow = true;
            } else {
                if (binding->uniqueBorrow) {
                    fail("E2203", expression->pos, "共有借用が可変借用と競合しています: " + binding->name);
                }
                ++binding->sharedBorrows;
            }
            return BorrowEffect{binding, kind};
        }
        }
        return std::nullopt;
    }

    void checkCondition(const ConditionPtr &condition) {
        if (!condition) return;
        releaseTemporary(checkExpr(condition->left));
        releaseTemporary(checkExpr(condition->right));
        releaseTemporary(checkExpr(condition->value));
        for (const ConditionPtr &item : condition->items) checkCondition(item);
        for (const ExprPtr &arg : condition->args) releaseTemporary(checkExpr(arg));
        for (const ShapeFieldSchema &field : condition->shapeFields) checkCondition(field.condition);
    }

    Snapshot snapshot() const {
        Snapshot result;
        for (const Frame &frame : frames_) {
            for (const auto &entry : frame.bindings) {
                StaticBinding *binding = entry.second.get();
                result[binding] = StateImage{binding->moved, binding->sharedBorrows, binding->uniqueBorrow};
            }
        }
        return result;
    }

    Snapshot snapshotLike(const Snapshot &keys) const {
        Snapshot result;
        for (const auto &entry : keys) {
            StaticBinding *binding = entry.first;
            result[binding] = StateImage{binding->moved, binding->sharedBorrows, binding->uniqueBorrow};
        }
        return result;
    }

    static void restore(const Snapshot &state) {
        for (const auto &entry : state) {
            entry.first->moved = entry.second.moved;
            entry.first->sharedBorrows = entry.second.sharedBorrows;
            entry.first->uniqueBorrow = entry.second.uniqueBorrow;
        }
    }

    static void merge(const Snapshot &left, const Snapshot &right) {
        for (const auto &entry : left) {
            StaticBinding *binding = entry.first;
            auto other = right.find(binding);
            if (other == right.end()) continue;
            binding->moved = entry.second.moved || other->second.moved;
            binding->sharedBorrows = std::max(entry.second.sharedBorrows, other->second.sharedBorrows);
            binding->uniqueBorrow = entry.second.uniqueBorrow || other->second.uniqueBorrow;
        }
    }

    Snapshot checkAlternative(const StatementPtr &statement, const Snapshot &before) {
        restore(before);
        if (statement) checkStatement(statement);
        return snapshotLike(before);
    }

    Snapshot checkForAlternative(const StatementPtr &statement, const Snapshot &before) {
        restore(before);
        pushScope();
        define(statement->name, statement->pos);
        if (statement->body) checkStatement(statement->body);
        popScope();
        return snapshotLike(before);
    }

    void checkFunction(const FunctionDef &function) {
        reset();
        for (const Param &param : function.params) define(param.name, param.pos);
        for (const ConditionPtr &requirement : function.requiresList) checkCondition(requirement);
        checkStatement(function.body);
    }

    void checkStatement(const StatementPtr &statement) {
        if (!statement) return;
        switch (statement->kind) {
        case StatementKind::Block:
            pushScope();
            for (const StatementPtr &child : statement->statements) checkStatement(child);
            popScope();
            return;
        case StatementKind::Let: {
            const auto borrow = checkExpr(statement->expr);
            define(statement->name, statement->pos, borrow);
            return;
        }
        case StatementKind::Expression:
            releaseTemporary(checkExpr(statement->expr));
            return;
        case StatementKind::Assign:
            releaseTemporary(checkExpr(statement->expr));
            requireWritable(statement->target, statement->pos);
            return;
        case StatementKind::Check:
        case StatementKind::Prove:
        case StatementKind::Require:
        case StatementKind::Assume:
            checkCondition(statement->condition);
            return;
        case StatementKind::If: {
            checkCondition(statement->condition);
            const Snapshot before = snapshot();
            const Snapshot thenState = checkAlternative(statement->body, before);
            const Snapshot elseState = statement->elseBody ? checkAlternative(statement->elseBody, before) : before;
            restore(before);
            merge(thenState, elseState);
            return;
        }
        case StatementKind::While: {
            checkCondition(statement->condition);
            for (const ConditionPtr &invariant : statement->loopInvariants) checkCondition(invariant);
            const Snapshot before = snapshot();
            const Snapshot bodyState = checkAlternative(statement->body, before);
            restore(before);
            merge(before, bodyState);
            return;
        }
        case StatementKind::For: {
            const auto iterableBorrow = checkExpr(statement->expr);
            const Snapshot before = snapshot();
            const Snapshot bodyState = checkForAlternative(statement, before);
            restore(before);
            merge(before, bodyState);
            releaseTemporary(iterableBorrow);
            return;
        }
        case StatementKind::Return:
            releaseTemporary(checkExpr(statement->expr));
            return;
        case StatementKind::Update:
            requireWritable(statement->target, statement->pos);
            pushScope();
            define(statement->name.empty() ? "self" : statement->name, statement->pos);
            if (!statement->name.empty() && statement->name != "self") define("self", statement->pos);
            if (statement->body) {
                for (const StatementPtr &child : statement->body->statements) checkStatement(child);
            }
            popScope();
            return;
        case StatementKind::UnsafeBlock:
            checkStatement(statement->body);
            return;
        case StatementKind::Match: {
            releaseTemporary(checkExpr(statement->expr));
            const Snapshot before = snapshot();
            std::optional<Snapshot> joined;
            for (const MatchArm &arm : statement->arms) {
                restore(before);
                checkCondition(arm.condition);
                Snapshot armState = checkAlternative(arm.action, before);
                if (!joined) joined = armState;
                else {
                    restore(*joined);
                    merge(*joined, armState);
                    joined = snapshotLike(before);
                }
            }
            Snapshot fallback = statement->elseBody ? checkAlternative(statement->elseBody, before) : before;
            restore(joined ? *joined : before);
            merge(joined ? *joined : before, fallback);
            return;
        }
        }
    }
};



} // namespace kond
