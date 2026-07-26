cmake_minimum_required(VERSION 3.16)
project(kond LANGUAGES C CXX)

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

add_executable(kond
    src/main.cpp
    src/kond_frontend.cpp
    src/kond_lsp.cpp
    src/kond_package.cpp
    src/kond_registry.cpp
    src/kond_interpreter.cpp
    src/kond_ffi.cpp
    src/kond_jit.cpp
    src/kond_http.cpp
    src/kond_common.hpp
    src/kond_frontend.hpp
    src/kond_lsp.hpp
    src/kond_package.hpp
    src/kond_value.hpp
    src/kond_runtime.hpp
    src/kond_interpreter_api.hpp
    src/kond_ffi.hpp
    src/kond_jit.hpp
    src/kond_http.hpp
    src/kond_registry.hpp
    src/kond_socket.hpp
)
target_compile_features(kond PRIVATE cxx_std_17)
if(MSVC)
    target_compile_options(kond PRIVATE /W4)
else()
    target_compile_options(kond PRIVATE -Wall -Wextra -Wpedantic)
endif()
if(WIN32)
    target_link_libraries(kond PRIVATE ws2_32)
endif()
if(CMAKE_DL_LIBS)
    target_link_libraries(kond PRIVATE ${CMAKE_DL_LIBS})
endif()

option(KOND_ENABLE_LLVM_JIT "Build the LLVM ORC JIT backend when LLVM is available" ON)
set(KOND_HAS_LLVM_JIT OFF)
if(KOND_ENABLE_LLVM_JIT)
    find_package(LLVM CONFIG QUIET)
    if(LLVM_FOUND)
        set(KOND_HAS_LLVM_JIT ON)
        target_include_directories(kond SYSTEM PRIVATE ${LLVM_INCLUDE_DIRS})
        target_link_directories(kond PRIVATE ${LLVM_LIBRARY_DIRS})
        find_program(KOND_LLVM_CONFIG llvm-config)
        if(KOND_LLVM_CONFIG)
            execute_process(COMMAND ${KOND_LLVM_CONFIG} --libs core orcjit native
                            OUTPUT_VARIABLE KOND_LLVM_LIBS_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
            separate_arguments(KOND_LLVM_LIBS NATIVE_COMMAND "${KOND_LLVM_LIBS_RAW}")
            execute_process(COMMAND ${KOND_LLVM_CONFIG} --system-libs
                            OUTPUT_VARIABLE KOND_LLVM_SYSTEM_LIBS_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
            separate_arguments(KOND_LLVM_SYSTEM_LIBS NATIVE_COMMAND "${KOND_LLVM_SYSTEM_LIBS_RAW}")
            target_link_libraries(kond PRIVATE ${KOND_LLVM_LIBS} ${KOND_LLVM_SYSTEM_LIBS})
        else()
            llvm_map_components_to_libnames(KOND_LLVM_LIBS core orcjit native)
            target_link_libraries(kond PRIVATE ${KOND_LLVM_LIBS})
        endif()
        if(LLVM_DEFINITIONS)
            set(KOND_LLVM_DEFINITIONS ${LLVM_DEFINITIONS})
            list(REMOVE_ITEM KOND_LLVM_DEFINITIONS "-D_GNU_SOURCE")
            target_compile_options(kond PRIVATE ${KOND_LLVM_DEFINITIONS})
        endif()
    else()
        find_program(KOND_LLVM_CONFIG llvm-config)
        if(KOND_LLVM_CONFIG)
            execute_process(COMMAND ${KOND_LLVM_CONFIG} --includedir
                            OUTPUT_VARIABLE KOND_LLVM_INCLUDEDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
            execute_process(COMMAND ${KOND_LLVM_CONFIG} --libdir
                            OUTPUT_VARIABLE KOND_LLVM_LIBDIR OUTPUT_STRIP_TRAILING_WHITESPACE)
            if(KOND_LLVM_INCLUDEDIR AND KOND_LLVM_LIBDIR)
                set(KOND_HAS_LLVM_JIT ON)
                target_include_directories(kond SYSTEM PRIVATE ${KOND_LLVM_INCLUDEDIR})
                target_link_directories(kond PRIVATE ${KOND_LLVM_LIBDIR})
                execute_process(COMMAND ${KOND_LLVM_CONFIG} --libs core orcjit native
                                OUTPUT_VARIABLE KOND_LLVM_LIBS_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
                separate_arguments(KOND_LLVM_LIBS NATIVE_COMMAND "${KOND_LLVM_LIBS_RAW}")
                execute_process(COMMAND ${KOND_LLVM_CONFIG} --system-libs
                                OUTPUT_VARIABLE KOND_LLVM_SYSTEM_LIBS_RAW OUTPUT_STRIP_TRAILING_WHITESPACE)
                separate_arguments(KOND_LLVM_SYSTEM_LIBS NATIVE_COMMAND "${KOND_LLVM_SYSTEM_LIBS_RAW}")
                target_link_libraries(kond PRIVATE ${KOND_LLVM_LIBS} ${KOND_LLVM_SYSTEM_LIBS})
            endif()
        endif()
    endif()
endif()
if(KOND_HAS_LLVM_JIT)
    target_compile_definitions(kond PRIVATE KOND_HAS_LLVM_JIT=1)
else()
    target_compile_definitions(kond PRIVATE KOND_HAS_LLVM_JIT=0)
endif()

option(KOND_NATIVE_OPTIMIZATIONS "Optimize for the build machine CPU" OFF)
if(KOND_NATIVE_OPTIMIZATIONS)
    target_compile_options(kond PRIVATE -march=native)
endif()

include(CheckIPOSupported)
check_ipo_supported(RESULT KOND_IPO_SUPPORTED OUTPUT KOND_IPO_ERROR)
if(KOND_IPO_SUPPORTED)
    set_property(TARGET kond PROPERTY INTERPROCEDURAL_OPTIMIZATION_RELEASE TRUE)
endif()
