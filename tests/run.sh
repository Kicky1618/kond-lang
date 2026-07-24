#!/bin/sh
set -eu

test "$#" -ge 1
KOND=$1
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)

new_root=$(mktemp -d)
trap 'rm -rf "$new_root"' EXIT HUP INT TERM
new_project="$new_root/hello"
"$KOND" new "$new_project" >/dev/null
test -f "$new_project/main.kd"
test -f "$new_project/README.md"
test "$("$KOND" run "$new_project/main.kd")" = "Hello, Kond!"
"$KOND" check "$new_project/main.kd" >/dev/null
"$KOND" new --help >/dev/null
"$KOND" registry --help >/dev/null
"$KOND" publish --help >/dev/null
"$KOND" fetch --help >/dev/null
"$KOND" publish --help | grep -q 'http://kond.j9.si'
if "$KOND" new "$new_project" >/dev/null 2>&1; then
    echo "kond new unexpectedly overwrote an existing project" >&2
    exit 1
fi
"$KOND" add "$ROOT/examples/package_library" --project "$new_project" >/dev/null
"$KOND" list "$new_project" | grep -q '^  greeting@0.1.0 '
"$KOND" remove greeting --project "$new_project" >/dev/null
if "$KOND" list "$new_project" | grep -q '^  greeting@'; then
    echo "kond remove unexpectedly retained a dependency" >&2
    exit 1
fi

"$KOND" install "$ROOT/examples/package_consumer" >/dev/null
test -f "$ROOT/examples/package_consumer/kond.lock"
output=$("$KOND" run "$ROOT/examples/package_consumer")
test "$output" = "hello Kond"
"$KOND" check "$ROOT/examples/package_consumer" >/dev/null

output=$("$KOND" run "$ROOT/examples/core.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "positive 4"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "20"

output=$("$KOND" run "$ROOT/examples/ownership.kd" --entry demo)
test "$(printf '%s\n' "$output" | sed -n '1p')" = "[1, 2, 3]"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "[1, 2, 3, 4]"

output=$("$KOND" run "$ROOT/examples/unique_borrow.kd")
test "$output" = "2"
"$KOND" check "$ROOT/examples/unique_borrow.kd" >/dev/null

output=$("$KOND" run "$ROOT/examples/contracts.kd")
test "$output" = "42"
output=$("$KOND" run "$ROOT/examples/contracts.kd" --mode verified)
test "$output" = "42"

output=$(printf 'hello\n' | "$KOND" run "$ROOT/examples/effectful_argument.kd")
test "$output" = "hello"

output=$("$KOND" run "$ROOT/examples/condition_value.kd")
test "$output" = "condition"

output=$("$KOND" run "$ROOT/examples/condition_composition.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "true"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "false"

output=$("$KOND" run "$ROOT/examples/for_loop.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "15"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "[1, 2, 3, 1, 2, 3]"
"$KOND" check "$ROOT/examples/for_loop.kd" >/dev/null
output=$("$KOND" run "$ROOT/examples/for_verified.kd" --mode verified)
test "$output" = "6"
if "$KOND" run "$ROOT/examples/for_invalid_target.kd" >/dev/null 2>&1; then
    echo "for unexpectedly accepted a non-List target" >&2
    exit 1
fi

output=$("$KOND" run "$ROOT/examples/v02_features.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "true"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "Untrusted"
test "$(printf '%s\n' "$output" | sed -n '3p')" = "<p>&lt;b&gt;Ada&lt;/b&gt;</p>"

output=$("$KOND" run "$ROOT/examples/v02_strict_ifc.kd" --ifc explicit)
test "$output" = "Public"
output=$("$KOND" run "$ROOT/examples/v02_strict_ifc.kd" --ifc strict)
test "$output" = "Untrusted"

output=$("$KOND" run "$ROOT/examples/v02_math_opt.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "42"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "9"
test "$(printf '%s\n' "$output" | sed -n '3p')" = "173"

if command -v llvm-config >/dev/null 2>&1; then
    jit_output=$("$KOND" run "$ROOT/examples/jit_arithmetic.kd" --jit)
    test "$(printf '%s\n' "$jit_output" | sed -n '1p')" = "sum 55"
    test "$(printf '%s\n' "$jit_output" | sed -n '2p')" = "7 9"
    jit_condition=$("$KOND" run "$ROOT/examples/condition_composition.kd" --jit)
    test "$(printf '%s\n' "$jit_condition" | sed -n '1p')" = "true"
    test "$(printf '%s\n' "$jit_condition" | sed -n '2p')" = "false"
    jit_ir=$("$KOND" run "$ROOT/examples/jit_arithmetic.kd" --jit --dump-llvm)
    printf '%s\n' "$jit_ir" | grep -q 'define i64 @kond.fn.main'
    printf '%s\n' "$jit_ir" | grep -q 'llvm.sadd.with.overflow.i64'
fi

output=$("$KOND" run "$ROOT/examples/std_library.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "List"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "6"
test "$(printf '%s\n' "$output" | sed -n '3p')" = "15"
test "$(printf '%s\n' "$output" | sed -n '4p')" = "[3, 4, 5]"
test "$(printf '%s\n' "$output" | sed -n '5p')" = "true"
test "$(printf '%s\n' "$output" | sed -n '6p')" = "library|proof|Kond"
test "$(printf '%s\n' "$output" | sed -n '7p')" = "proof-proof"
test "$(printf '%s\n' "$output" | sed -n '8p')" = "42"
test "$(printf '%s\n' "$output" | sed -n '9p')" = "[name, score]"
test "$(printf '%s\n' "$output" | sed -n '10p')" = "a%20b"
test "$(printf '%s\n' "$output" | sed -n '11p')" = "&lt;Ada&gt;"
test "$(printf '%s\n' "$output" | sed -n '12p')" = '{"name":"Ada","score":42}'
test "$(printf '%s\n' "$output" | sed -n '13p')" = "predicate-ok"

"$KOND" check "$ROOT/examples/std_library.kd" >/dev/null

output=$("$KOND" run "$ROOT/examples/std_data.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "3"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "{n: 2, q: a b}"
test "$(printf '%s\n' "$output" | sed -n '3p')" = "6"
test "$(printf '%s\n' "$output" | sed -n '4p')" = "24"
test "$(printf '%s\n' "$output" | sed -n '5p')" = "255"
test "$(printf '%s\n' "$output" | sed -n '6p')" = "Bool"

output=$("$KOND" run "$ROOT/examples/source_library_consumer.kd" --lib "$ROOT/examples/source_library.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "40"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "0"
"$KOND" check "$ROOT/examples/source_library_consumer.kd" --lib "$ROOT/examples/source_library.kd" >/dev/null

if "$KOND" run "$ROOT/examples/source_library_consumer.kd" >/dev/null 2>&1; then
    echo "source library dependency was unexpectedly available" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/source_library_consumer.kd" --lib "$ROOT/examples/invalid_source_library.kd" >/dev/null 2>&1; then
    echo "executable source library was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/source_library_consumer.kd" --lib "$ROOT/std/optimization.kd" >/dev/null 2>&1; then
    echo "optimization library was unexpectedly accepted as a source library" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/source_library_consumer.kd" --lib "$ROOT/spec/examples/server.kd" >/dev/null 2>&1; then
    echo "route library was unexpectedly accepted as a source library" >&2
    exit 1
fi

output=$("$KOND" run "$ROOT/examples/order_processing.kd")
test "$(printf '%s\n' "$output" | sed -n '1p')" = "items: 2"
test "$(printf '%s\n' "$output" | sed -n '2p')" = "customer flow: Untrusted"
test "$(printf '%s\n' "$output" | sed -n '3p')" = "<h1>Order accepted</h1><p>&lt;Ada&gt;: 300</p>"
"$KOND" check "$ROOT/examples/order_processing.kd" >/dev/null
"$KOND" check "$ROOT/spec/examples/server.kd" >/dev/null
"$KOND" check "$ROOT/examples/http_server.kd" >/dev/null

optimization_trace=$("$KOND" run "$ROOT/examples/v02_math_opt.kd" --explain-optimizations 2>&1)
printf '%s\n' "$optimization_trace" | grep -q 'optimization\[ExactEq\]: divisible integer roundtrip'
printf '%s\n' "$optimization_trace" | grep -q 'optimization\[ExactEq\]: nonnegative abs'
printf '%s\n' "$optimization_trace" | grep -q 'optimization\[ExactEq\]: bounded integer mask'

library_trace=$("$KOND" run "$ROOT/examples/optimization_library.kd" --opt-lib "$ROOT/std/optimization.kd" --explain-optimizations 2>&1)
printf '%s\n' "$library_trace" | grep -q '^41$'
printf '%s\n' "$library_trace" | grep -q '^42$'
printf '%s\n' "$library_trace" | grep -q 'optimization\[ExactEq\]: library add_zero'
printf '%s\n' "$library_trace" | grep -q 'optimization\[ExactEq\]: library mul_one'
printf '%s\n' "$library_trace" | grep -q 'optimization\[ExactEq\]: library divisible_roundtrip'
"$KOND" check "$ROOT/examples/optimization_library.kd" --opt-lib "$ROOT/std/optimization.kd" >/dev/null

stress_output=$("$KOND" run "$ROOT/examples/optimization_stress.kd" --opt-lib "$ROOT/std/optimization.kd")
test "$(printf '%s\n' "$stress_output" | sed -n '1p')" = "42"
test "$(printf '%s\n' "$stress_output" | sed -n '3p')" = "0"
test "$(printf '%s\n' "$stress_output" | sed -n '7p')" = "0"
test "$(printf '%s\n' "$stress_output" | sed -n '8p')" = "1"
test "$(printf '%s\n' "$stress_output" | sed -n '12p')" = "[1, 2, 3]"
test "$(printf '%s\n' "$stress_output" | sed -n '13p')" = "20"
test "$(printf '%s\n' "$stress_output" | sed -n '14p')" = "KOND"
test "$(printf '%s\n' "$stress_output" | sed -n '15p')" = "5"
test "$(printf '%s\n' "$stress_output" | sed -n '16p')" = "1000000"
test "$(printf '%s\n' "$stress_output" | sed -n '17p')" = "3"
test "$(printf '%s\n' "$stress_output" | sed -n '18p')" = "3"
test "$(printf '%s\n' "$stress_output" | sed -n '19p')" = "3"
test "$(printf '%s\n' "$stress_output" | sed -n '20p')" = "2"
test "$(printf '%s\n' "$stress_output" | sed -n '21p')" = "2"
test "$(printf '%s\n' "$stress_output" | sed -n '22p')" = "8"
test "$(printf '%s\n' "$stress_output" | sed -n '23p')" = "3"
test "$(printf '%s\n' "$stress_output" | sed -n '24p')" = "3"
test "$(printf '%s\n' "$stress_output" | sed -n '25p')" = "2"
test "$(printf '%s\n' "$stress_output" | sed -n '26p')" = "0"
test "$(printf '%s\n' "$stress_output" | sed -n '27p')" = "2"

stress_trace=$("$KOND" run "$ROOT/examples/optimization_stress.kd" --opt-lib "$ROOT/std/optimization.kd" --explain-optimizations 2>&1)
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: library minimum_self'
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: compile-time constant fold'
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: repeat length without materialization'
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: concat length without materialization'
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: string repeat length without materialization'
printf '%s\n' "$stress_trace" | grep -q 'optimization\[ExactEq\]: map merge length without materialization'

collection_trace=$("$KOND" run "$ROOT/bench/collection_length.kd" --explain-optimizations 2>&1)
printf '%s\n' "$collection_trace" | grep -q '^1000000$'
printf '%s\n' "$collection_trace" | grep -q '^4000000$'
printf '%s\n' "$collection_trace" | grep -q 'optimization\[ExactEq\]: range length without materialization'
printf '%s\n' "$collection_trace" | grep -q 'optimization\[ExactEq\]: repeat length without materialization'
printf '%s\n' "$collection_trace" | grep -q 'optimization\[ExactEq\]: string repeat length without materialization'

loop_trace=$("$KOND" run "$ROOT/bench/numeric_loop.kd" --explain-optimizations 2>&1)
printf '%s\n' "$loop_trace" | grep -q '^1000000$'
printf '%s\n' "$loop_trace" | grep -q 'optimization\[ExactEq\]: counted-loop closed form (1000000 iterations)'

ownership_trace=$("$KOND" run "$ROOT/examples/ownership.kd" --entry demo --trace-ownership 2>&1)
printf '%s\n' "$ownership_trace" | grep -q 'ownership: xs: Own -> SharedBorrow'
printf '%s\n' "$ownership_trace" | grep -q 'ownership: xs: BorrowEnd -> Own'
printf '%s\n' "$ownership_trace" | grep -q 'ownership: xs: Own -> Moved'

if "$KOND" run "$ROOT/examples/stale_condition.kd" >/dev/null 2>&1; then
    echo "stale condition was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/stale_composed_condition.kd" >/dev/null 2>&1; then
    echo "transitively stale condition was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/borrow_conflict.kd" >/dev/null 2>&1; then
    echo "borrow conflict was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" check "$ROOT/examples/borrow_conflict.kd" >/dev/null 2>&1; then
    echo "ownership preflight unexpectedly accepted a borrow conflict" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/verified_fail.kd" --mode verified >/dev/null 2>&1; then
    echo "verified mode unexpectedly accepted an unknown proof" >&2
    exit 1
fi

if printf 'yes\n' | "$KOND" run "$ROOT/examples/dynamic_proof_rejected.kd" >/dev/null 2>&1; then
    echo "runtime-derived value was unexpectedly accepted as a static proof" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/int_overflow.kd" >/dev/null 2>&1; then
    echo "Int64 overflow was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/unsafe_html.kd" >/dev/null 2>&1; then
    echo "unsafe HTML was unexpectedly accepted" >&2
    exit 1
fi

if "$KOND" run "$ROOT/examples/unsafe_sql.kd" >/dev/null 2>&1; then
    echo "unsafe SQL was unexpectedly accepted" >&2
    exit 1
fi

echo "kond tests: ok"
