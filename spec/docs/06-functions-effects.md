# 6. Functions, Contracts, Effects, and Capabilities

## 6.1 Preconditions and postconditions

Functions declare requirements and guarantees.

```kond
fn sqrt(x)
    requires Number(x) and Finite(x) and x >= 0
    ensures Number(result) and result >= 0
{
    ...
}
```

At a call site, every `requires` condition must be:

- proven statically,
- checked at runtime in Safe mode when decidable, or
- provided through an explicit unsafe assumption.

After return, `ensures` conditions are introduced.

## 6.2 Inline parameter invariants

A mutable local parameter slot may declare an invariant:

```kond
fn countdown(n where self is Int and self >= 0) {
    while n > 0 {
        print(n)
        n -= 1
    }
}
```

This differs from a precondition.

A precondition constrains entry.

A slot invariant constrains every replacement value during execution.

## 6.3 Effects as conditions/capabilities

Effects may be represented using capabilities:

```text
CanReadFile(path)
CanWriteFile(path)
CanAccessNetwork(host)
CanUseClock
CanSpawnProcess
```

A function requiring an effect must require the corresponding capability.

This makes effect permissions compositional with the same proof engine.

## 6.4 Capability passing

Capabilities may be persistent or linear.

Read-only ambient permission may be persistent.

Exclusive write authority may be linear.

Example:

```kond
fn overwrite(file, data)
    requires CanWrite(file)
    requires Own(_, file)
```

## 6.5 Result conditions

Returned values may carry conditions inferred from implementation or contracts.

```kond
fn normalize_path(path)
    ensures NormalizedPath(result)
```

The caller gains `NormalizedPath(result)`.

## 6.6 Generic behavior through predicates

Instead of nominal generic bounds:

```text
T: Add + Copy
```

Kond may express operational conditions:

```text
CanAdd(x, y)
Copyable(x)
```

The exact generic syntax is deferred, but the semantic model is condition-based.

## 6.7 Unsafe functions

An unsafe function is one whose correctness requires caller-provided assumptions not enforced by ordinary checks.

Unsafe APIs must explicitly state obligations.

```kond
unsafe fn from_raw_pointer(ptr)
    requires ValidPointer(ptr)
    requires ProperlyAligned(ptr)
```

Unsafe blocks may discharge these requirements using `assume`, making the trust boundary auditable.
