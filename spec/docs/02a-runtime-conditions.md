# 2A. Runtime Semantics of Conditions

Draft 0.1 left an essential ambiguity: what does a first-class condition become at runtime?

## 2A.1 Evidence-carrying binary decisions

A runtime-decidable proposition `P` evaluates conceptually to:

```text
Condition<P> =
    Holds(Proof<P>)
  | Fails(Proof<not P>)
```

Thus, "Kond has no bool" means that predicate results are not semantically represented as an **unindexed truth value**. They are binary decisions whose proposition of origin is preserved by the static semantics.

This is intentionally close to informative decidable propositions such as Rocq/Coq `sumbool`.

An optimized implementation may erase proof payloads. A non-reflective `Condition<P>` may therefore occupy one discriminator bit at runtime. Erasure does not erase the static proposition index or the compiler's proof obligations.

## 2A.2 Compile-time state is not a runtime third truth value

The compiler uses:

```text
Proven(P)
Refuted(P)
Unknown(P)
```

A runtime decision is only:

```text
Holds(P)
Fails(P)
```

`Unknown` means the compiler has not decided the proposition. It is never a third runtime truth value.

## 2A.3 Binding and printing

```kond
let positive = x > 0
```

If runtime evaluation is required:

```text
positive : Condition<x0 > 0>
```

The standard `Display` representation is `true` or `false`:

```kond
print(positive)
```

A debug formatter may retain richer metadata:

```text
Condition<x0 > 0> = true
```

The textual words `true` and `false` do not imply the existence of a primitive source-level `bool` type.

## 2A.4 Branching imports evidence

```kond
if positive {
    // proof: x0 > 0
} else {
    // proof: not (x0 > 0)
}
```

`if` is logically a pattern match over `Holds` / `Fails` with evidence import.

## 2A.5 Logical composition

Given:

```text
a : Condition<P>
b : Condition<Q>
```

Kond defines evidence-preserving combinators:

```text
a and b : Condition<P and Q>
a or  b : Condition<P or Q>
not a    : Condition<not P>
```

Short-circuiting `and` may evaluate its RHS under evidence for `P`, allowing refinement-sensitive RHS expressions.

Conceptually:

```text
and(
  lhs: Condition<P>,
  rhs: fn(using Proof<P>) -> Condition<Q>
) -> Condition<P and Q>
```

Surface syntax hides this evidence parameter.

## 2A.6 Conditions in collections

Conditions are ordinary values. They may be stored, passed, returned, and placed in collections.

A collection whose elements decide the same proposition can retain:

```text
Condition<P>
```

A collection of unrelated propositions requires existential packaging:

```text
SomeCondition = exists P. Condition<P>
```

A heterogeneous raw `Any` collection may also store them, but retrieving an element yields `Any` and loses proposition-specific static power until the value is checked/unpacked.

## 2A.7 Dynamic condition composition

For existential conditions:

```text
exists P. Condition<P>
exists Q. Condition<Q>
```

a dynamic rule engine may construct:

```text
exists R. Condition<R>
```

where a reified runtime descriptor records `R = P and Q` or `R = P or Q`.

Such reified conditions support generic printing/evaluation/composition, but static proposition-specific facts cannot be recovered unless the descriptor is matched to a known proposition.

## 2A.8 Reification boundary

Kond distinguishes:

```text
Condition<P>     statically indexed decision
SomeCondition    existential/reified decision
raw truth bit    ABI implementation detail only
```

The slogan "no bool" applies to the semantic interface, not transistor-level representation.

## 2A.9 Value-version binding

A condition refers to exact value versions:

```text
Condition<x0 > 0>
```

Mutation producing `x1` never allows the old condition to refine `x1`. This remains true if the runtime representation has been erased to one bit.
