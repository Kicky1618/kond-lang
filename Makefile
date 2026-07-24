CXX ?= g++
CXXFLAGS ?= -std=c++17 -Wall -Wextra -Wpedantic -O3 -flto -DNDEBUG
LDFLAGS ?= -flto

TARGET := kond
SOURCE := src/main.cpp src/kond_frontend.cpp src/kond_package.cpp src/kond_registry.cpp src/kond_interpreter.cpp src/kond_http.cpp src/kond_jit.cpp
HEADERS := src/kond_common.hpp src/kond_frontend.hpp src/kond_value.hpp src/kond_runtime.hpp \
	src/kond_interpreter_api.hpp src/kond_http.hpp src/kond_jit.hpp src/kond_package.hpp src/kond_registry.hpp

LLVM_CONFIG ?= llvm-config
LLVM_AVAILABLE := $(shell command -v $(LLVM_CONFIG) >/dev/null 2>&1 && echo 1 || echo 0)
ifeq ($(LLVM_AVAILABLE),1)
LLVM_CXXFLAGS := $(shell $(LLVM_CONFIG) --cxxflags | sed 's/-fno-exceptions//g')
LLVM_LDFLAGS := $(shell $(LLVM_CONFIG) --ldflags --system-libs --libs core orcjit native)
CPPFLAGS += -DKOND_HAS_LLVM_JIT=1 $(LLVM_CXXFLAGS)
LDFLAGS += $(LLVM_LDFLAGS)
else
CPPFLAGS += -DKOND_HAS_LLVM_JIT=0
endif

.PHONY: all clean test native

all: $(TARGET)

$(TARGET): $(SOURCE) $(HEADERS)
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) $(SOURCE) $(LDFLAGS) -o $@

native:
	$(CXX) $(CPPFLAGS) $(CXXFLAGS) -march=native $(SOURCE) $(LDFLAGS) -o $(TARGET)

test: $(TARGET)
	./tests/run.sh ./$(TARGET)

clean:
	rm -f $(TARGET)
