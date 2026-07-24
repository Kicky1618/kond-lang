# Kond Language Specification

**Status:** Draft 0.2  
**Language name:** `Kond` is a provisional name and is not normative.

Kond is an experimental programming language built around one central idea:

> Values are not statically classified by ordinary types. Every value begins as `Any`, and safety is obtained by accumulating, proving, preserving, and consuming conditions about values.

The language combines:

- `Any` as the universal value representation
- first-class `Condition<P>` instead of a built-in Boolean type
- flow-sensitive refinement by proofs
- condition-preserving mutable slots
- ownership and borrowing represented as linear conditions
- proof-carrying API contracts
- taint/capability security for web and systems programming
- proof-backed compiler optimizations, including domain-specific mathematical rewrites
- a small proof kernel that validates compiler-generated proof certificates

The design goal is not "dynamic typing plus assertions." The design goal is:

> A program is a transformation of values and knowledge.  
> An operation is permitted only when its required conditions can be proven, checked, or explicitly trusted.

## Documents

- `SPEC.ja.md` — 日本語の統合仕様書（まずこれを読む）
- `SPEC.md` — consolidated English draft
- `docs/01-core-model.md` — value model, `Any`, conditions, truth and control flow
- `docs/02-condition-system.md` — condition algebra, implication, normalization, comparison
- `docs/03-control-flow.md` — `if`, `match`, loops, flow-sensitive facts
- `docs/04-mutation-invariants.md` — condition-preserving mutable slots
- `docs/05-ownership-linear.md` — ownership, borrowing, linear conditions
- `docs/06-functions-effects.md` — contracts, effects, capabilities, typestate
- `docs/07-proof-ir.md` — CIR/FIR/PIR and trusted proof kernel
- `docs/08-web-security.md` — input validation, taint, SQL, HTML, auth, path safety
- `docs/09-optimization.md` — proof-backed and mathematical optimization
- `docs/10-diagnostics.md` — error-message specification
- `docs/11-grammar.md` — draft surface grammar
- `docs/12-runtime-abi.md` — runtime representation and execution model
- `docs/13-implementation-roadmap.md` — staged implementation plan
- `docs/02a-runtime-conditions.md` — first-class runtime `Condition<P>` semantics
- `docs/05a-deterministic-ownership.md` — deterministic ownership/borrow checking
- `docs/08a-information-flow.md` — taint/provenance propagation and IFC
- `docs/09a-exact-approx-proofs.md` — exact vs approximate rewrite proofs
- `docs/14-related-work.md` — related work and Kond positioning
- `docs/15-standard-library.md` — standard library namespaces and optimization-library rules
- `docs/16-package-manager.md` — local package manifests, dependency resolution, and lockfiles
- `REFERENCES.md` — primary/project references
- `syntax.ebnf` — compact grammar sketch
- `examples/` — example programs

## Non-goals of Draft 0.2

This draft does not fully specify:

- module/package syntax
- macro systems
- asynchronous runtime semantics
- FFI soundness rules
- complete theorem proving
- a finalized package/module ABI (the Draft 0.2 `std.*` surface is specified by `docs/15-standard-library.md`)
- exact binary ABI
- concurrency memory model

Those are intentionally deferred until the core condition/proof/ownership model is implemented.

## Design slogan

```text
Value world:      Any
Knowledge world:  Condition<P>
Resource world:   LinearCondition<L>
Mutation:         preserve Invariant<P>
Execution:        prove requirements + consume linear resources
Optimization:     rewrite only with proof
```
