# 7. Proof Architecture: CIR, FIR, PIR

## 7.1 Goals

The compiler must compare conditions by meaning, not source syntax.

It therefore uses three conceptual IR layers:

```text
CIR — Condition Intermediate Representation
FIR — Fact Intermediate Representation
PIR — Proof Intermediate Representation
```

## 7.2 CIR

CIR represents logical propositions.

Illustrative structures:

```rust
enum Term {
    Value(ValueId),
    Constant(Constant),
    Field(TermId, FieldId),
    Index(TermId, TermId),
    Length(TermId),
    Tag(TermId),
}

enum Atom {
    Equal(TermId, TermId),
    NotEqual(TermId, TermId),
    Less(TermId, TermId),
    LessEqual(TermId, TermId),
    HasTag(TermId, RuntimeTag),
    HasField(TermId, FieldId),
    IsFinite(TermId),
    Predicate(PredicateId, Vec<TermId>),
}

enum Condition {
    True,
    False,
    Atom(AtomId),
    And(Vec<ConditionId>),
    Or(Vec<ConditionId>),
}
```

Negation should be normalized toward atoms where supported.

## 7.3 FIR

FIR stores normalized facts in domain-specific abstract domains.

Suggested domains:

```text
Equality domain
Runtime-tag domain
Integer/rational interval domain
Congruence domain
Object shape domain
Nullability domain
State/typestate domain
Capability domain
Ownership/alias domain
Taint/trust domain
```

Examples:

```text
x in [0, 100)
x == y
x == 3 mod 8
tag(x) in {Int}
fields(x) includes {name, age}
```

## 7.4 Solver composition

A query:

```text
Facts => Goal
```

is dispatched to cheap specialized solvers first.

Possible order:

1. syntactic/reflexive lookup
2. equality propagation
3. tag/shape lattice
4. interval reasoning
5. congruence reasoning
6. linear arithmetic
7. ownership/capability rules
8. registered lemmas
9. optional external solver

Every solver returns:

```text
Proven(certificate)
Refuted(counterexample or certificate)
Unknown(reason)
```

## 7.5 PIR

PIR records why an implication is valid.

Illustrative nodes:

```rust
enum ProofNode {
    Assumption(ConditionId),
    Reflexive(ConditionId),
    Transitive(ProofId, ProofId),

    AndIntro(Vec<ProofId>),
    AndElim { source: ProofId, index: u32 },
    OrIntro { source: ProofId, branch: u32 },

    Rewrite { equality: ProofId, source: ProofId },

    IntervalSubset {
        inner: Interval,
        outer: Interval,
    },

    TagSubset {
        inner: TagSet,
        outer: TagSet,
    },

    CongruenceStep(CongruenceCertificate),
    LinearArithmetic(LinearCertificate),

    ApplyLemma {
        lemma: LemmaId,
        premises: Vec<ProofId>,
        substitution: Substitution,
    },
}
```

Linear facts require linear proof nodes with explicit consumption/production.

## 7.6 Trusted proof kernel

The optimizing compiler and complex solvers need not be fully trusted.

A small proof kernel validates PIR before proof-derived safety decisions are accepted.

The kernel should be:

- small
- deterministic
- side-effect free
- fuzzable
- independently testable

If a proof certificate fails validation, compilation must fail or fall back to runtime checking. It must never silently trust the optimizer.

## 7.7 Counterexamples

When a solver can produce a model demonstrating failure, diagnostics should display a minimized counterexample.

Example:

```text
Known: x <= 150
Required: x < 150
Counterexample: x = 150
```

## 7.8 Proof budgets

Complex proof search must be bounded.

`Unknown` diagnostics should report why:

```text
nonlinear arithmetic exceeded proof budget
unsupported quantifier
external solver timeout
condition depends on volatile state
```

`Unknown` must never be reported as `Refuted`.
