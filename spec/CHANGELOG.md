# CHANGELOG

## Draft 0.2

1. Defined runtime `Condition<P>` as `Holds(Proof<P>) | Fails(Proof<not P>)`; specified printing, storage, existential packaging, dynamic logical composition, proof erasure, and value-version binding.
2. Added taint/information-flow propagation: flow-label lattice/domain, primitive/function summaries, strict IFC `pc` labels, context-specific sanitization, declassification/endorsement boundaries.
3. Made ordinary ownership/borrow checking a dedicated deterministic procedure that emits certified facts; it is not subject to generic theorem-prover budgets.
4. Added Related Work covering Whiley, refinement/Liquid Types, Dafny, Racket contracts, decidable propositions, Rust, IFC/taint, PCC/FPCC, egg, and Herbie.
5. Split optimizer certificates into `ExactEq`, `RealEq`, `ApproxEq`, and `HeuristicImprovement`.
6. Expanded `syntax.ebnf` to cover shape literals, chained comparisons, routes, tagged interpolation, lambdas, field/index/call forms, collections, and flow clauses used by the examples/spec.
7. Added the standard `serve` runtime: synchronous HTTP/1.0/1.1 route dispatch, untrusted request fields, bounded JSON parsing, HTML/JSON/text responses, explicit responses, and 400/404/405/413 error mapping without third-party HTTP or JSON dependencies.
8. Added the Draft 0.2 `std.*` standard-library surface for core values, math, strings, lists, maps, JSON, URL/HTML/security, HTTP, I/O, predicates, and optimization diagnostics.
9. Added explicit `rewrite` declarations and `--opt-lib FILE` loading. The reference interpreter applies only proven, effect-free `ExactEq` rules and preserves non-exact proof classes as non-executable candidates.

## Post-Draft 0.2 extensions

10. Added collection iteration with `for item in expression`. The collection
    expression is evaluated once, List elements are visited in source order,
    and the reference interpreter uses a bounded snapshot so explicit
    mutation of the source list cannot invalidate iteration.
11. Added `kond new DIRECTORY` to scaffold a runnable `main.kd` and a
    project README without overwriting a non-empty directory.
12. Added a local-path package manager with `kond.json`, `kond.lock`,
    `kond add`, `kond remove`, `kond install`, `kond list`, and automatic
    source-library loading for project-directory `run` and `check` commands.
13. Added a source-package registry server with `kond registry`, the
    `publish`/`fetch` commands, JSON bundles, and bounded HTTP transport.
14. Added package-declared native artifacts for POSIX C FFI. Registry bundles
    carry native bytes in a base64 `binary` object, and `fetch` restores them
    before registering the local dependency.
