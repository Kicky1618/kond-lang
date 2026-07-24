#pragma once

#include "kond_frontend.hpp"

namespace kond {

// The first LLVM backend targets a deliberately small, statically
// representable subset.  Values in that subset become signed i64 values and
// conditions become i1 values.  The interpreter remains the implementation
// for dynamic values such as List, Object, HTTP values, and borrow references.
struct JitOptions {
    bool dumpIr = false;
    bool verifiedMode = false;
    bool unsafeMode = false;
};

[[nodiscard]] bool llvmJitAvailable();
void runJit(const Program &program, const std::string &entry, const JitOptions &options = {});

} // namespace kond
