# 13. Implementation Roadmap

## Stage 0 — Minimal interpreter

Implement:

- parser
- `Any` runtime
- immutable `let`
- basic predicates: Int/String/List/Object/Null
- `Condition<P>` expressions
- `if`
- `check`
- runtime validation
- basic diagnostics

No theorem proving beyond direct facts.

## Stage 1 — FIR and refinement

Add:

- SSA/value versioning
- fact environments
- equality propagation
- interval domain
- tag domain
- shape domain
- branch refinement
- immutable proof caching

This stage already enables bounds-check elimination and useful diagnostics.

## Stage 2 — Invariant-bearing mutable slots

Add:

- `let x where ...`
- assignment proof obligations
- transactional updates
- invalidation rules
- `Verified` versus `Safe` mode

## Stage 3 — Ownership and linear conditions

Add:

- ownership tokens
- moves
- shared borrows
- unique borrows
- regions
- linear proof environment
- deterministic resource handling

Keep the first implementation simpler than Rust where possible; prove the semantic core before adding ergonomic lifetime inference.

## Stage 4 — PIR proof kernel

Add:

- proof certificates
- small verifier
- interval proofs
- tag/shape subset proofs
- equality rewrites
- linear-resource transitions

Fuzz compiler solver output against the proof kernel.

## Stage 5 — Web-safe standard library

The reference interpreter now provides the first executable `std.*` surface
for core values, collections, strings, JSON, URL/HTML/security, HTTP, I/O, and
predicates. A limited POSIX C FFI bridge is available for `Int`, `Float`,
`Bool`, `String`, and `Void`; package/module versioning and a stable
Kond-specific external ABI remain future work.

Add proof-annotated APIs for:

- HTTP request inputs
- JSON validation
- SQL structured queries
- HTML templates
- URL parsing/redirect policy
- filesystem paths
- auth/authz capabilities

Create compile-fail security test suites.

## Stage 6 — Native backend/JIT

Recommended implementation language: Rust.

Candidate backends:

- Cranelift for early JIT/AOT
- LLVM for aggressive optimization
- WASM for sandboxed execution

Implement proof-driven specialization and guard elimination.

## Stage 7 — Mathematical optimizer

The reference interpreter now accepts explicitly loaded `rewrite` libraries;
the executable subset is proof-checked, effect-free `ExactEq`. General
congruence solving, proof-producing e-graphs, and non-exact numerical passes
remain in this stage.

Add:

- congruence domain
- linear arithmetic
- lemma registry
- e-graph experimentation
- proof-producing rewrites
- optimization explain mode

Do not begin with arbitrary SMT. Start with cheap decidable domains.

## Stage 8 — External solver integration

Optional:

- SMT solver
- proof certificate translation
- bounded nonlinear reasoning
- user theorem libraries

External solver answers must not bypass the trusted proof kernel unless explicitly configured as trusted.

## Testing strategy

Required test classes:

```text
parser tests
semantic positive tests
compile-fail tests
runtime-check tests
proof-kernel unit tests
solver differential tests
fuzz tests
ownership alias tests
security sink/source tests
optimizer equivalence tests
diagnostic snapshot tests
```

For every safety optimization:

```text
proof required
proof serialized
proof verified
optimized and unoptimized semantics differential-tested
```
