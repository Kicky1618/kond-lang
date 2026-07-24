# 1. Core Semantic Model

## 1.1 Universal value domain

Every runtime value inhabits the universal value domain `Any`.

Kond does not treat `Int`, `String`, `List`, `User`, `OpenFile`, or similar names as ordinary static types. They are predicates or condition constructors.

```kond
let x = input()
```

At this point, the compiler knows only:

```text
Value(x)
```

and, for external input, typically:

```text
Untrusted(x)
```

After a check:

```kond
check x is Int
```

the compiler may add:

```text
Int(x)
```

to the current fact environment.

A "type" in source-level discussion is therefore usually shorthand for a condition.

## 1.2 No built-in Boolean type

Kond has no primitive `bool`.

A logical expression produces a condition value:

```kond
x > 0
```

has a logical form equivalent to:

```text
Condition<x > 0>
```

The compiler tracks a proof state for a condition:

```text
Proven(P)
Refuted(P)
Unknown(P)
```

This three-way state is primarily a compile-time/proof-engine concept.

At runtime, a decidable condition used for branching is evaluated to one of two outcomes:

```text
holds(P)
fails(P)
```

The branch itself introduces evidence:

- `holds(P)` introduces `P`
- `fails(P)` introduces `not P`

`Unknown(P)` means the compiler has not proven either side. It does **not** mean a third runtime truth value.

## 1.3 Condition values are version-bound

A condition refers to specific SSA-like value versions.

```kond
let x where self is Int = 10
let c = x > 0
x = -10
```

Internally:

```text
x0 = 10
c : Condition<x0 > 0>
x1 = -10
```

`c` proves nothing about `x1`.

This rule prevents stale validation from being reused after mutation.

## 1.4 Primitive predicates

The initial language provides built-in predicates such as:

```text
Int(x)
Float(x)
Number(x)
String(x)
List(x)
Object(x)
Null(x)
Finite(x)
HasField(x, field)
Length(x) = n
Callable(x)
```

These are not nominal types. They are facts.

Runtime representation tags may exist for performance, but source semantics are defined in terms of conditions.

The runtime also has a `Bool` value representation for JSON and standard
library/data boundaries. A `std.pred.*` call used as a value may produce that
representation; using the same call or value in a condition position does
not introduce a source-level Boolean literal or static Boolean type.

## 1.5 Constant logical values

Because there is no `bool`, the language uses logical constants:

```text
always   = Condition<true proposition>
never    = Condition<false proposition>
```

Example:

```kond
while always {
    work()
}
```

A dedicated `loop` construct may be preferred for infinite loops.

## 1.6 Safety modes

An implementation should support at least two checking modes.

### Safe mode

When an operation requires a decidable condition that cannot be statically proven, the compiler inserts a runtime check.

### Verified mode

Every required condition must be statically proven. `Unknown` is a compile error.

### Unsafe escape hatch

`assume P` may inject a condition without proof or runtime checking, but only in an explicitly unsafe context.

```kond
unsafe {
    assume ValidPointer(ptr)
}
```

The unsafe boundary must be visible in diagnostics and proof metadata.
