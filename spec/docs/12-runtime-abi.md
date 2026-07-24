# 12. Runtime and ABI Model

## 12.1 Runtime `Any`

An implementation may represent `Any` using:

- tagged unions
- NaN boxing
- pointer tagging
- boxed objects
- specialized unboxed SSA values after proof-driven optimization

The source semantics must not depend on one representation.

## 12.2 Guards

When a condition is not statically proven, Safe mode inserts guards.

Conceptual lowering:

```text
candidate = evaluate(input)

if not runtime_check(Invariant(candidate)):
    condition_failure(...)

commit(candidate)
```

## 12.3 Specialized code

A JIT may specialize based on proven or guarded conditions.

Generic:

```text
add_any(x, y)
```

Specialized:

```text
guard Int(x)
guard Int(y)
add_i64(x, y)
```

Guard failure deoptimizes or falls back to generic execution.

## 12.4 Proof erasure

Most proof objects are compile-time artifacts and may be erased.

Runtime evidence is retained only when needed for:

- dynamic checks
- reflective condition values
- security audit trails
- dynamic capability tokens
- deoptimization guards

## 12.5 Linear runtime resources

Some linear conditions correspond to real runtime state:

```text
file handles
locks
transactions
one-time tokens
unique mutable references
```

Compile-time linear proof consumption must correspond correctly to runtime ownership transfer/drop semantics.

## 12.6 Drop

Owning values may have deterministic drop behavior.

Dropping requires:

```text
CanDrop(x)
```

A resource can forbid implicit drop through a condition such as:

```text
MustCommit(tx) or MustRollback(tx)
```

The exact destructor model is deferred.

## 12.7 FFI boundary

Foreign values enter as weakly known `Any` values unless trusted ABI contracts establish stronger conditions.

Unsafe FFI adapters may produce proofs only after:

- ABI validation,
- runtime validation,
- or explicit unsafe assumptions.

## 12.8 Concurrency

Concurrency semantics are deferred, but ownership/linear conditions are intended to support:

```text
Sendable(x)
Shareable(x)
ThreadSafe(x)
```

A future memory model should derive cross-thread transfer from these conditions rather than bypassing the ownership system.
