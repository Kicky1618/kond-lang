# 14. Related Work and Positioning

Kond combines ideas with substantial prior art. The research claim is not that refinement, contracts, ownership, proof certificates, taint tracking, or equality saturation are individually new.

## 14.1 Whiley

Whiley developed flow-sensitive typing and constrained types. Kond's `let x where ...` and flow refinement are closely related.

Intended difference: Kond pushes classification aggressively into facts over an `Any` value world, and tries to expose ownership-derived capabilities, security facts, and optimization preconditions through the same semantic fact interface.

## 14.2 Refinement / Liquid Types

Refinement types describe subsets of base types using logical predicates; Liquid Types demonstrate practical automated checking via restricted predicate languages and predicate abstraction.

Traditional view:

```text
{x : T | P(x)}
```

Kond's conceptual extreme:

```text
x : Any
facts = { T(x), P(x), ... }
```

The engineering lesson is adopted: use decidable/specialized domains for common reasoning rather than unrestricted theorem proving everywhere.

## 14.3 Dafny

Dafny integrates specifications (`requires`, `ensures`, invariants) with automated verification and executable programs.

Kond is in the same verification-aware tradition. Its intended distinction is continuity between static proof, runtime decisions in Safe mode, proof-carrying flow facts, ownership/capability facts, and optimizer preconditions.

## 14.4 Racket contracts

Racket contracts dynamically guard boundaries, including higher-order function arguments/results and structured values.

Kond Safe mode is related: an unresolved but runtime-decidable proposition becomes a dynamic check. A successful check, however, becomes a proof-indexed flow fact that can later eliminate checks or enable optimization.

## 14.5 Rocq/Coq `sumbool` and decidable propositions

Rocq/Coq `sumbool A B` is an informative disjunction carrying evidence of either side and has a Boolean-like extracted representation.

This is a close precedent for:

```text
Condition<P> = Holds(Proof<P>) | Fails(Proof<not P>)
```

Therefore Kond's "no bool" slogan should be read as "no unindexed predicate-result bool", not "binary decisions disappeared from runtime machines".

## 14.6 Rust ownership and borrowing

Rust demonstrates practical ownership, moves, shared borrowing, mutable borrowing, and a dedicated borrow checker.

Kond's semantic experiment is to expose ownership results as linear facts/capabilities in the common vocabulary. Draft 0.2 nevertheless mandates a dedicated deterministic borrow checker. This is conceptual unification with procedural specialization.

## 14.7 Information-flow control and taint tracking

IFC/taint systems track labels/provenance through computation and enforce source/flow/sink policies.

Kond's `Untrusted` model belongs here. Draft 0.2 explicitly adds label propagation, function summaries, optional `pc` labels for implicit flows, and explicit declassification/endorsement. Merely writing `Untrusted(x)` as a condition is not sufficient.

## 14.8 Proof-Carrying Code / Foundational PCC

PCC associates code with machine-checkable safety evidence; foundational PCC reduces trusted infrastructure through smaller foundational checking.

Kond's:

```text
complex solver/optimizer -> certificate -> small kernel
```

is directly related. Kond applies the pattern internally to implication proofs, linear transitions, guard elimination, and optimization—not only binary distribution.

## 14.9 Equality saturation / egg

`egg` provides a practical, extensible e-graph/equality-saturation infrastructure.

Kond may use e-graphs for search, but does not trust the e-graph itself:

```text
e-graph search -> candidate -> PIR certificate -> kernel validation
```

## 14.10 Herbie and numerical rewriting

Herbie automatically searches for floating-point expression rewrites that improve numerical accuracy using heuristic methods.

Kond may use similar search as candidate generation, but Draft 0.2 separates `ExactEq`, `RealEq`, `ApproxEq`, and `HeuristicImprovement` so heuristic numerical improvement cannot masquerade as exact semantic equivalence.

## 14.11 Intended differentiation

The strongest intended differences are:

1. **Extreme Any-first model** — familiar "types" are largely facts about `Any` values.
2. **One fact vocabulary across normally separate domains** — refinement, invariants, ownership-derived capabilities, typestate, security policy, taint/provenance, and optimization preconditions share a semantic interface while retaining specialized solvers.
3. **Proof as compiler infrastructure** — certificates justify safety, guard elimination, specialization, capability derivation, and rewrites.
4. **Static/dynamic continuity** — `Unknown` may become a runtime `Condition<P>` decision whose branch evidence later feeds static reasoning.

A defensible research statement is:

> Kond explores whether an Any-first, evidence-indexed condition calculus can serve as a common semantic interface between refinement, runtime contracts, ownership capabilities, information flow, security policy, and proof-backed optimization—without sacrificing specialized predictable decision procedures.
