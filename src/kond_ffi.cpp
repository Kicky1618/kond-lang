#include "kond_runtime.hpp"
#include "kond_ffi.hpp"

#include <cstring>
#include <filesystem>
#include <limits>

#if defined(__has_include)
#  if __has_include(<ffi.h>)
#    define KOND_FFI_HAS_LIBFFI 1
#    include <ffi.h>
#  endif
#endif
#ifndef KOND_FFI_HAS_LIBFFI
#  define KOND_FFI_HAS_LIBFFI 0
#endif

#if defined(__unix__) || defined(__APPLE__)
#  define KOND_FFI_HAS_DLOPEN 1
#  include <dlfcn.h>
#else
#  define KOND_FFI_HAS_DLOPEN 0
#endif

namespace kond {

namespace {

static Value dereferenceFfi(Value value, const SourcePos &pos) {
    while (value.kind == ValueKind::Reference && value.reference) {
        if (value.reference->moved) {
            fail("E2101", pos, "移動済みの値をFFIへ渡そうとしています");
        }
        value = value.reference->value;
    }
    return value;
}

static std::string stringValueForFfi(const Value &rawValue, const SourcePos &pos) {
    const Value value = dereferenceFfi(rawValue, pos);
    if (value.kind != ValueKind::String) {
        fail("E3304", pos, "FFIのString引数にはString値が必要です");
    }
    const std::size_t separator = value.string.find('\x1f');
    if (separator == std::string::npos) return value.string;
    const std::string tag = value.string.substr(0, separator);
    if (tag == "html" || tag == "json" || tag == "sql" || tag == "text") {
        return value.string.substr(separator + 1);
    }
    return value.string;
}

#if KOND_FFI_HAS_DLOPEN && KOND_FFI_HAS_LIBFFI

template <typename Function>
static Function functionPointer(void *symbol) {
    static_assert(sizeof(Function) == sizeof(void *),
                  "the platform cannot represent a dlsym function pointer");
    Function result{};
    std::memcpy(&result, &symbol, sizeof(result));
    return result;
}

struct LibffiApi {
    using PrepCif = ffi_status (*)(ffi_cif *, ffi_abi, unsigned int, ffi_type *, ffi_type **);
    using Call = void (*)(ffi_cif *, void (*)(void), void *, void **);

    void *handle = nullptr;
    PrepCif prepCif = nullptr;
    Call call = nullptr;
    ffi_type *typeVoid = nullptr;
    ffi_type *typeInt64 = nullptr;
    ffi_type *typeDouble = nullptr;
    ffi_type *typePointer = nullptr;
    std::string failure;

    ~LibffiApi() {
        if (handle) dlclose(handle);
    }

    bool load() {
        if (handle || !failure.empty()) return handle != nullptr;

        const char *names[] = {
#if defined(__APPLE__)
            "libffi.dylib",
#endif
            "libffi.so.8", "libffi.so.7", "libffi.so"
        };
        for (const char *name : names) {
            handle = dlopen(name, RTLD_NOW | RTLD_LOCAL);
            if (handle) break;
        }
        if (!handle) {
            const char *message = dlerror();
            failure = message ? message : "libffi.so が見つかりません";
            return false;
        }

        prepCif = functionPointer<PrepCif>(dlsym(handle, "ffi_prep_cif"));
        call = functionPointer<Call>(dlsym(handle, "ffi_call"));
        typeVoid = reinterpret_cast<ffi_type *>(dlsym(handle, "ffi_type_void"));
        typeInt64 = reinterpret_cast<ffi_type *>(dlsym(handle, "ffi_type_sint64"));
        if (!typeInt64) typeInt64 = reinterpret_cast<ffi_type *>(dlsym(handle, "ffi_type_slong"));
        typeDouble = reinterpret_cast<ffi_type *>(dlsym(handle, "ffi_type_double"));
        typePointer = reinterpret_cast<ffi_type *>(dlsym(handle, "ffi_type_pointer"));

        if (!prepCif || !call || !typeVoid || !typeInt64 || !typeDouble || !typePointer) {
            failure = "libffi の必要なシンボルを解決できません";
            dlclose(handle);
            handle = nullptr;
            return false;
        }
        return true;
    }

    ffi_type *type(FfiType kind) const {
        switch (kind) {
        case FfiType::Void: return typeVoid;
        case FfiType::Int64: return typeInt64;
        case FfiType::Float64: return typeDouble;
        case FfiType::Bool: return typeInt64;
        case FfiType::CString: return typePointer;
        }
        return typeVoid;
    }
};

#endif

static std::vector<std::string> libraryCandidates(const FunctionDef &function) {
    namespace fs = std::filesystem;
    const fs::path declared(function.ffiLibrary);
    std::vector<std::string> candidates;
    if (declared.is_absolute()) {
        candidates.push_back(declared.string());
        return candidates;
    }

    const fs::path source(function.pos.file);
    if (!source.parent_path().empty()) {
        candidates.push_back((source.parent_path() / declared).lexically_normal().string());
    }
    candidates.push_back(declared.string());
    return candidates;
}

} // namespace

struct FfiRuntime::State {
#if KOND_FFI_HAS_DLOPEN && KOND_FFI_HAS_LIBFFI
    LibffiApi libffi;
    std::unordered_map<std::string, void *> libraries;

    ~State() {
        for (const auto &entry : libraries) {
            if (entry.second) dlclose(entry.second);
        }
    }
#endif
};

FfiRuntime::FfiRuntime() : state_(std::make_unique<State>()) {}

FfiRuntime::~FfiRuntime() = default;

Value FfiRuntime::call(const FunctionDef &function, const std::vector<Value> &args,
                       const SourcePos &pos) {
#if !(KOND_FFI_HAS_DLOPEN && KOND_FFI_HAS_LIBFFI)
    (void)function;
    (void)args;
    fail("E3301", pos,
         "このビルドではFFIを利用できません (POSIXのdlfcn.hとlibffiが必要です)");
#else
    if (function.ffiParameterTypes.size() != args.size()) {
        fail("E3304", pos, "FFI関数 " + function.name + " の引数個数が一致しません");
    }
    if (!state_->libffi.load()) {
        fail("E3301", pos, "libffiを読み込めません: " + state_->libffi.failure);
    }

    void *library = nullptr;
    std::string lastError;
    for (const std::string &candidate : libraryCandidates(function)) {
        auto cached = state_->libraries.find(candidate);
        if (cached != state_->libraries.end()) {
            library = cached->second;
            break;
        }
        dlerror();
        void *loaded = dlopen(candidate.c_str(), RTLD_NOW | RTLD_LOCAL);
        if (loaded) {
            state_->libraries.emplace(candidate, loaded);
            library = loaded;
            break;
        }
        const char *message = dlerror();
        lastError = message ? message : "不明なロードエラー";
    }
    if (!library) {
        fail("E3302", pos, "共有ライブラリを読み込めません: " + function.ffiLibrary +
                              " (" + lastError + ")");
    }

    dlerror();
    void *symbol = dlsym(library, function.ffiSymbol.c_str());
    const char *symbolError = dlerror();
    if (symbolError != nullptr || symbol == nullptr) {
        fail("E3303", pos, "FFIシンボルが見つかりません: " + function.ffiSymbol +
                              " (" + (symbolError ? std::string(symbolError) : "null") + ")");
    }

    std::vector<ffi_type *> argumentTypes;
    argumentTypes.reserve(args.size());
    std::vector<void *> argumentValues(args.size(), nullptr);
    std::vector<std::int64_t> integerStorage(args.size(), 0);
    std::vector<double> floatStorage(args.size(), 0.0);
    std::vector<std::string> stringStorage(args.size());
    std::vector<const char *> stringPointers(args.size(), nullptr);

    std::uint32_t flow = FlowPublic;
    for (std::size_t i = 0; i < args.size(); ++i) {
        const Value value = dereferenceFfi(args[i], pos);
        flow |= value.flow;
        const FfiType type = function.ffiParameterTypes[i];
        argumentTypes.push_back(state_->libffi.type(type));
        switch (type) {
        case FfiType::Int64:
            if (value.kind != ValueKind::Integer) {
                fail("E3304", pos, "FFIのInt引数にはInt値が必要です");
            }
            integerStorage[i] = value.integer;
            argumentValues[i] = &integerStorage[i];
            break;
        case FfiType::Float64:
            if (value.kind == ValueKind::Integer) floatStorage[i] = static_cast<double>(value.integer);
            else if (value.kind == ValueKind::Float) floatStorage[i] = value.floating;
            else fail("E3304", pos, "FFIのFloat引数にはFloatまたはInt値が必要です");
            argumentValues[i] = &floatStorage[i];
            break;
        case FfiType::Bool:
            if (value.kind != ValueKind::Boolean) {
                fail("E3304", pos, "FFIのBool引数にはBool値が必要です");
            }
            integerStorage[i] = value.boolean ? 1 : 0;
            argumentValues[i] = &integerStorage[i];
            break;
        case FfiType::CString:
            stringStorage[i] = stringValueForFfi(value, pos);
            stringPointers[i] = stringStorage[i].c_str();
            argumentValues[i] = &stringPointers[i];
            break;
        case FfiType::Void:
            fail("E3304", pos, "FFIの引数にVoidは指定できません");
        }
    }

    if (argumentTypes.size() > std::numeric_limits<unsigned int>::max()) {
        fail("E3304", pos, "FFIの引数が多すぎます");
    }

    ffi_cif cif{};
    if (state_->libffi.prepCif(&cif, FFI_DEFAULT_ABI,
                               static_cast<unsigned int>(argumentTypes.size()),
                               state_->libffi.type(function.ffiReturnType),
                               argumentTypes.empty() ? nullptr : argumentTypes.data()) != FFI_OK) {
        fail("E3301", pos, "FFI呼び出し規約の準備に失敗しました");
    }

    std::int64_t integerResult = 0;
    double floatResult = 0.0;
    const char *stringResult = nullptr;
    void *returnValue = nullptr;
    switch (function.ffiReturnType) {
    case FfiType::Void: returnValue = nullptr; break;
    case FfiType::Int64:
    case FfiType::Bool: returnValue = &integerResult; break;
    case FfiType::Float64: returnValue = &floatResult; break;
    case FfiType::CString: returnValue = &stringResult; break;
    }

    state_->libffi.call(&cif, functionPointer<void (*)(void)>(symbol), returnValue,
                        argumentValues.empty() ? nullptr : argumentValues.data());

    Value result;
    switch (function.ffiReturnType) {
    case FfiType::Void:
        result = Value::null();
        break;
    case FfiType::Int64:
        result = Value::integerValue(integerResult);
        break;
    case FfiType::Bool:
        result = Value::booleanValue(integerResult != 0);
        break;
    case FfiType::Float64:
        result = Value::floatValue(floatResult);
        break;
    case FfiType::CString:
        result = stringResult == nullptr ? Value::null() : Value::stringValue(std::string(stringResult));
        break;
    }
    // An external function may read ambient state or return data unrelated to
    // its arguments.  Explicit flow clauses can add facts, but cannot erase
    // this conservative opaque provenance mark.
    result.flow = flow | FlowOpaque;
    return result;
#endif
}

} // namespace kond
