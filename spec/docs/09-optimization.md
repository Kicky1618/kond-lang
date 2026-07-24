# 9. Proof-Backed Optimization

## 9.1 Principle

Every nontrivial semantics-changing-looking rewrite must be justified by:

- language semantics,
- a proven condition,
- or a verified lemma.

The optimizer may be aggressive, but proof validation remains conservative.

## 9.2 Examples

Given:

```text
Int(x)
x mod 2 = 0
```

the rewrite:

```text
(x / 2) * 2  =>  x
```

is valid under the specified integer-division semantics.

Given:

```text
x >= 0
```

the rewrite:

```text
abs(x) => x
```

is valid.

Given:

```text
0 <= x < 256
```

and integer bitwise semantics:

```text
x & 255 => x
```

is valid.

## 9.3 Congruence domain

FIR should support modular facts:

```text
x == 3 mod 8
```

This can optimize:

- `%`
- masking
- alignment checks
- loop induction
- address calculations

## 9.4 Mathematical properties as conditions

Potential conditions:

```text
Prime(p)
Coprime(a, b)
Monotonic(f)
Orthogonal(A)
Diagonal(A)
UnitVector(v)
PositiveDefinite(A)
```

The compiler may apply registered lemmas.

Example:

```text
Orthogonal(A)
=> transpose(A) * A == Identity
```

Such rewrites should be opt-in by imported theorem libraries or language-standard lemmas to control compile time.

## 9.5 E-graph integration

A high-level optimizer may use an e-graph:

```text
expression
 -> e-graph
 -> add equivalent expressions using proven lemmas
 -> extract cheapest expression
 -> emit PIR proof
 -> validate proof
```

The proof kernel, not the e-graph search, is trusted.

## 9.6 Floating-point caution

IEEE floating-point rewrites require explicit conditions.

Examples may require:

```text
Finite(x)
NotNaN(x)
NotNegativeZero(x)
NoOverflow(...)
```

Kond should avoid a global coarse `fast-math` switch where possible.

Instead, optimization permissions should be derived locally from proven properties.

## 9.7 Ownership-driven optimization

Ownership facts provide alias information.

Examples:

```text
UniqueBorrow(_, x)
NoAlias(x, y)
```

may justify:

- redundant load elimination
- reordering
- scalar replacement
- vectorization
- stronger backend alias metadata

## 9.8 Optimization diagnostics

An optional explain mode should show successful and rejected rewrites.

Example:

```text
optimization applied:
    (x / 2) * 2 -> x

because:
    Int(x)
    x mod 2 = 0

proof:
    lemma integer_even_roundtrip
```

Rejected:

```text
optimization not applied:
    required x mod 2 = 0
    known Int(x)
    result Unknown
```

## 9.9 User optimization libraries

Draft 0.2 permits an explicitly loaded library to register proof-classified
expression rewrites:

```text
rewrite exact add_zero(x) when x is Int {
    from x + 0
    to x
}
```

The matcher binds only the declared pattern variables. The instantiated
precondition is checked in the current fact environment, and the reference
interpreter executes only `ExactEq` rules over effect-free expressions. The
effect summary includes the pure standard-library calls but excludes I/O,
mutation, user functions, moves, and borrows. A candidate with an unknown
precondition remains ordinary execution. The
`real`, `approx`, and `heuristic` classes are intentionally not coerced to
`ExactEq`; they are registry metadata for future proof-producing passes.

Libraries are passed with `--opt-lib FILE` and can be inspected through
`std.opt.rule_count()` and `std.opt.rule_names()`. The standard library and
the exact rewrite file shipped with the repository are specified in
`15-standard-library.md`.

## 9.10 Core ExactEq catalogue

The reference interpreter applies a checked ExactEq pass before ordinary
expression evaluation. The pass includes:

- Int64 constant folding and pure standard-library constant folding;
- zero/one arithmetic identities, self subtraction/division/modulo, and
  zero/one/all-ones bitwise identities;
- double-negation and proven nonnegative `abs` identities;
- the divisible integer round-trip, bounded bit-mask identity, and monotone
  counted-loop closed form;
- length-only rewrites that avoid allocating intermediate `range`, `repeat`,
  `concat`, `append`, `reverse`, `sort`, `zip`, `slice`, map updates/merges,
  and string-repeat results.

The optimizer evaluates an identity operand at most once when it needs a
runtime value. A rewrite that would need to evaluate a non-pure candidate
twice is rejected. Pure standard calls have an explicit closed whitelist;
input, output, database, HTTP, mutation methods, user functions, moves, and
borrows remain ordinary execution. Collection rewrites preserve the existing
validation rules and the one-million-element limits, including zero-step,
overflow, type, and range errors.

The shipped `std/optimization.kd` library adds 30 proof-checked integer and
numeric rules. It is intentionally opt-in so applications can choose their
compile-time rule budget and can inspect applied certificates with
`--explain-optimizations`.

## 9.11 Floating-point proof classes

Floating-point rewrites use `09a-exact-approx-proofs.md`. `ExactEq`, `RealEq`, `ApproxEq`, and `HeuristicImprovement` are distinct certificate classes. A mathematical real identity does not automatically justify an IEEE floating-point rewrite.
