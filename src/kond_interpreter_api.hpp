#pragma once

#include "kond_runtime.hpp"

namespace kond {

enum class Mode { Safe, Verified, Unsafe };

class Interpreter;

using InterpreterDeleter = void (*)(Interpreter *);
using InterpreterPtr = std::unique_ptr<Interpreter, InterpreterDeleter>;

InterpreterPtr makeInterpreter(const Program &program, Mode mode, std::string file,
                               bool strictIfc, bool explainOptimizations, bool traceOwnership);
void destroyInterpreter(Interpreter *interpreter);
void runInterpreter(Interpreter &interpreter, const std::string &entry);
void checkInterpreter(const Interpreter &interpreter);
void validateInterpreter(const Interpreter &interpreter);
HttpResponse dispatchHttpRequest(Interpreter &interpreter, const HttpRequestData &request);

} // namespace kond
