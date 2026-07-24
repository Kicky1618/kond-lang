#pragma once

#include "kond_value.hpp"

namespace kond {

// Runtime bridge for the explicit C ABI used by `extern fn` declarations.
// The implementation loads libffi dynamically, so building the interpreter
// does not make libffi a link-time dependency; a clear Kond error is emitted
// only when an FFI call is actually attempted without it.
class FfiRuntime final {
public:
    FfiRuntime();
    ~FfiRuntime();

    FfiRuntime(const FfiRuntime &) = delete;
    FfiRuntime &operator=(const FfiRuntime &) = delete;

    Value call(const FunctionDef &function, const std::vector<Value> &args,
               const SourcePos &pos);

private:
    struct State;
    std::unique_ptr<State> state_;
};

} // namespace kond
