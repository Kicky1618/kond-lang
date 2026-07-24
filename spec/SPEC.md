# Kond Language Specification — Draft 0.2

**Status:** Experimental design draft. `Kond` is a provisional name.

## Core thesis

Kond treats runtime values as `Any` and represents knowledge as evidence-indexed conditions. A common fact vocabulary connects refinement, invariants, certified ownership capabilities, information flow, security policy, and optimization, while specialized domains retain deterministic/predictable decision procedures.

```text
Value world:      Any
Knowledge world:  Condition<P>
Runtime decision: Holds(Proof<P>) | Fails(Proof<not P>)
Resource world:   LinearCondition<L>
Mutable slot:     Invariant<P>
```

Draft 0.2 explicitly adopts: **one semantic vocabulary does not imply one universal solver**.

---

## 1. Core Semantic Model

### 1.1 Universal value domain

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

### 1.2 No built-in Boolean type

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

### 1.3 Condition values are version-bound

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

### 1.4 Primitive predicates

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

### 1.5 Constant logical values

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

### 1.6 Safety modes

An implementation should support at least two checking modes.

#### Safe mode

When an operation requires a decidable condition that cannot be statically proven, the compiler inserts a runtime check.

#### Verified mode

Every required condition must be statically proven. `Unknown` is a compile error.

#### Unsafe escape hatch

`assume P` may inject a condition without proof or runtime checking, but only in an explicitly unsafe context.

```kond
unsafe {
    assume ValidPointer(ptr)
}
```

The unsafe boundary must be visible in diagnostics and proof metadata.

---

## 2. Condition System

### 2.1 Conditions denote sets of valid states

A condition `P(x)` denotes the set of values for which `P` holds.

The central comparison is implication:

```text
P <= Q  iff  for all x, P(x) implies Q(x)
```

`P` is then **stronger** than `Q`, and its accepted value set is smaller.

Example:

```text
Int(x) and 0 <= x < 100
    <= Int(x) and x >= 0
    <= Int(x)
    <= AnyCondition(x)
```

This relation is a partial order, not a total order.

`Int(x)` and `String(x)` may be incomparable.

### 2.2 Comparison result

The compiler exposes the following semantic comparison:

```text
Equivalent
Stronger
Weaker
Incomparable
Unknown
```

`Unknown` means the proof engine could not decide the relation within supported theories or budgets.

### 2.3 Logical composition

Condition expressions support:

```text
P and Q
P or Q
not P
```

The implementation normalizes negation toward atomic predicates where possible.

Examples:

```text
not (x >= 0 and x < 100)
=> x < 0 or x >= 100
```

### 2.4 Meet and join

For facts at control-flow merge points, the compiler computes an abstract join.

For intervals:

```text
join([0, 10], [5, 20]) = [0, 20]
meet([0, 10], [5, 20]) = [5, 10]
```

A precise implementation may retain disjunctions; an optimizing implementation may widen them.

### 2.5 Condition declaration

Reusable conditions are declared with `condition`.

```kond
condition User(u) =
    u has { name, age } and
    u.name is String and
    u.age is Int and
    0 <= u.age < 150
```

A condition declaration is logically a predicate definition, not a nominal class declaration.

Conditions may overlap arbitrarily.

```kond
condition Adult(u) = User(u) and u.age >= 18
condition Admin(u) = User(u) and "admin" in u.permissions
```

### 2.6 Stable and volatile conditions

Conditions are classified by stability.

#### Stable conditions

Remain valid while referenced immutable value versions remain unchanged.

```text
Int(x)
x >= 0
HasField(x, "name")
```

#### Volatile conditions

Depend on external mutable state, time, or environment.

```text
FileExists(path)
ServiceHealthy(url)
ClockBefore(deadline)
```

A volatile observation must not be promoted to a permanent fact without an explicit capability or snapshot guaranteeing stability.

### 2.7 Decidability classes

An implementation may annotate condition expressions internally as:

```text
StaticallyDecidable
RuntimeDecidable
SemiDecidable
UndecidableOrUnsupported
```

A runtime branch requires either:

- a statically proven condition, or
- a runtime-decidable condition expression.

### 2.8 Proof, check, require, assume

Recommended semantics:

```kond
prove P
```

Requires `P` to be statically proven. No runtime branch/check may be inserted.

```kond
check P
```

Statically proves when possible; otherwise evaluates at runtime and fails if false. On success, introduces `P`.

```kond
require P
```

A high-level boundary validation form. Semantically similar to `check`, but intended to map failure into the surrounding effect domain, such as HTTP 400 or function contract failure.

```kond
assume P
```

Introduces `P` without proof. Allowed only in `unsafe`.

### 2.9 First-class condition values

A condition may be bound:

```kond
let valid = x is Int and 0 <= x < 100
```

Conceptually:

```text
valid : Condition<Int(x0) and 0 <= x0 < 100>
```

Using it in a branch introduces its proposition:

```kond
if valid {
    // Int(x0), 0 <= x0 < 100
}
```

Condition values preserve the identity/version of referenced values.

---

## 2A. Runtime Semantics of Conditions

Draft 0.1 left an essential ambiguity: what does a first-class condition become at runtime?

### 2A.1 Evidence-carrying binary decisions

A runtime-decidable proposition `P` evaluates conceptually to:

```text
Condition<P> =
    Holds(Proof<P>)
  | Fails(Proof<not P>)
```

Thus, "Kond has no bool" means that predicate results are not semantically represented as an **unindexed truth value**. They are binary decisions whose proposition of origin is preserved by the static semantics.

This is intentionally close to informative decidable propositions such as Rocq/Coq `sumbool`.

An optimized implementation may erase proof payloads. A non-reflective `Condition<P>` may therefore occupy one discriminator bit at runtime. Erasure does not erase the static proposition index or the compiler's proof obligations.

### 2A.2 Compile-time state is not a runtime third truth value

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

### 2A.3 Binding and printing

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

### 2A.4 Branching imports evidence

```kond
if positive {
    // proof: x0 > 0
} else {
    // proof: not (x0 > 0)
}
```

`if` is logically a pattern match over `Holds` / `Fails` with evidence import.

### 2A.5 Logical composition

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

### 2A.6 Conditions in collections

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

### 2A.7 Dynamic condition composition

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

### 2A.8 Reification boundary

Kond distinguishes:

```text
Condition<P>     statically indexed decision
SomeCondition    existential/reified decision
raw truth bit    ABI implementation detail only
```

The slogan "no bool" applies to the semantic interface, not transistor-level representation.

### 2A.9 Value-version binding

A condition refers to exact value versions:

```text
Condition<x0 > 0>
```

Mutation producing `x1` never allows the old condition to refine `x1`. This remains true if the runtime representation has been erased to one bit.

---

## 3. Control Flow and Refinement

### 3.1 `if`

`if` consumes a runtime-decidable `Condition<P>`.

```kond
if x is Int {
    use_integer(x)
} else {
    handle_other(x)
}
```

Then-branch facts:

```text
Int(x)
```

Else-branch facts:

```text
not Int(x)
```

### 3.2 Compound refinement

```kond
if x is Int and 0 <= x < 100 {
    xs[x]
}
```

The branch may establish all conjuncts:

```text
Int(x)
0 <= x
x < 100
```

If `xs[x]` requires `List(xs)`, that condition is still independently required.

### 3.3 Early-return refinement

```kond
fn process(x) {
    if not (x is String) {
        return Error("not a string")
    }

    // String(x) is known here
    return x.uppercase()
}
```

A terminating branch allows the negation to refine the continuation.

### 3.4 `match`

`match` is a condition-oriented branch system.

```kond
match x {
    when x is Int and x >= 0 => positive_integer(x)
    when x is Int            => negative_integer(x)
    when x is String         => text(x)
    else                     => fallback(x)
}
```

Later branches inherit negations of previous branches.

The second branch may simplify to:

```text
Int(x) and x < 0
```

when arithmetic reasoning is available.

### 3.5 Loops and invariants

Loops may declare or infer invariants.

```kond
while i < len(xs)
    invariant Int(i) and 0 <= i <= len(xs)
{
    use(xs[i])
    i += 1
}
```

A verified loop requires proof that:

1. the invariant holds before entry,
2. the body preserves the invariant,
3. every operation in the body has its requirements satisfied.

Termination is not required unless a termination contract is declared.

### 3.6 Collection iteration

`for` evaluates its collection expression once and iterates a snapshot of a
`List` in source order:

```kond
let total where self is Int and self >= 0 = 0
for value in std.core.range(1, 6) {
    total += value
}
```

The iteration binding is immutable and exists only for the current iteration.
The body may update an explicitly mutable outer slot or mutate a nested
collection under the normal ownership rules. Non-`List` targets are rejected
at runtime, and the reference interpreter caps iteration at one million
elements. The snapshot keeps iteration deterministic when the body mutates
the original list with `push` or `pop`.

### 3.7 Control-flow merge

Facts from branches are merged conservatively.

Example:

```kond
if c {
    check 0 <= x <= 10
} else {
    check 5 <= x <= 20
}
```

After the branch, an interval domain may retain:

```text
0 <= x <= 20
```

A more precise mode may retain the disjunction.

### 3.8 Conditions are not implicit coercions

A condition being provable does not automatically mutate a value representation.

For example:

```text
Int(x)
```

does not imply that `x` has been unboxed into a machine integer. It only permits an implementation to specialize representation if that transformation is semantics-preserving.

---

## 4. Condition-Preserving Mutable Slots

### 4.1 Immutability by default

A plain binding is immutable:

```kond
let x = 10
x = 20   // error
```

### 4.2 Mutation is introduced by an invariant

A binding with a slot invariant may be reassigned only to values satisfying that invariant.

```kond
let age where self is Int and 0 <= self < 150 = 16

age = 20      // valid
age = -1      // invalid
age = "old"   // invalid
```

There is no required `mut` keyword.

The language interpretation is:

> A binding is mutable exactly to the extent that replacement values can preserve its declared invariant.

### 4.3 Assignment semantics

For:

```kond
x = candidate
```

the compiler must establish:

```text
Facts(candidate) => Invariant_x(candidate)
```

If statically proven, assignment proceeds without checks.

If unknown in Safe mode, runtime validation is inserted.

If unknown in Verified mode, compilation fails.

The old value is replaced only after validation succeeds.

Conceptually:

```text
candidate = evaluate(rhs)
validate invariant(candidate)
commit candidate into slot
```

### 4.4 Mutation capability as a set

The assignable set of a slot is:

```text
Assignable(x) = { v | Invariant_x(v) }
```

A weaker invariant admits more mutations.

Examples:

```text
where AnyCondition(self)        // maximally permissive
where Int(self)                 // integer-only
where Int(self) and self >= 0   // non-negative integers
where self == 10                // effectively immutable
```

### 4.5 Compound updates

```kond
count += 1
```

is checked as a replacement:

```text
new = count + 1
prove/check Invariant_count(new)
commit
```

The compiler may use current facts to eliminate the check.

### 4.6 Transactional object mutation

An object invariant may span multiple fields.

```kond
let range where
    self.start is Int and
    self.end is Int and
    self.start <= self.end
= { start: 0, end: 10 }
```

This update may temporarily violate the invariant:

```kond
update range {
    self.start = 20
    self.end = 30
}
```

`update` is transactional:

1. create a temporary mutable view/version,
2. execute mutations,
3. validate the full slot invariant,
4. atomically commit the new version.

If validation fails, the original slot remains unchanged.

### 4.7 External dependencies in invariants

Persistent slot invariants should normally depend only on:

- `self`
- constants
- immutable bindings
- stable pure functions

An invariant depending on another mutable slot introduces dependency invalidation and is rejected by default.

Instead, related mutable values should be grouped into a single invariant-bearing state object.

### 4.8 Interior mutation

Interior mutation requires ownership or unique-borrow permission and must preserve all affected invariants.

Persistent collections are the simplest semantic model:

```kond
xs = xs.push(4)
```

An optimized implementation may mutate in place when uniqueness is proven.

---

## 5. Ownership, Borrowing, and Linear Conditions

### 5.1 Ownership is a linear fact

Ownership is represented as a non-duplicable logical resource:

```text
Own(scope, value)
```

Unlike ordinary persistent facts such as `Int(x)`, ownership facts cannot be copied arbitrarily.

The proof system therefore distinguishes:

```text
Persistent facts
Linear facts
```

### 5.2 Move

Ownership-bearing values move by default unless a `Copy`-like condition is proven.

```kond
let y = move x
```

Proof transition:

```text
consume Own(scope, x)
produce Own(scope, y)
```

Using `x` afterward requires a fact that no longer exists.

### 5.3 Shared borrow

```kond
let r = &x
```

Conceptually:

```text
Own(scope, x)
  -> FrozenOwn(scope, x)
   + BorrowShared(r, x)
```

While shared borrows exist:

```text
CanRead(x)
not CanWrite(x)
not CanMove(x)
```

The implementation may model borrow multiplicity through regions rather than literal token counting.

### 5.4 Unique borrow

```kond
let r = &mut x
```

Conceptually:

```text
Own(scope, x)
  -> SuspendedOwn(scope, x)
   + BorrowUnique(r, x)
```

A unique borrow grants:

```text
CanRead(x)
CanWrite(x)
```

but does not grant unrestricted movement of the owner unless specifically permitted.

### 5.5 Borrow restoration

At the end of the borrow region:

```text
consume BorrowUnique(r, x)
consume SuspendedOwn(scope, x)
produce Own(scope, x_new_version)
```

Mutation may produce a new value version.

Any condition referring to the pre-mutation version remains valid only for that old version.

### 5.6 Capability derivation

Operations require capabilities rather than directly special-casing ownership.

Example derivations:

```text
Own(_, x)          => CanRead(x), CanWrite(x), CanMove(x), CanDrop(x)
BorrowUnique(_,x)  => CanRead(x), CanWrite(x)
BorrowShared(_,x)  => CanRead(x)
```

These relations form a partial order/capability lattice.

### 5.7 Linear security resources

Linear conditions generalize beyond memory ownership:

```text
ValidResetToken(token)
OneTimeNonce(nonce)
WriteCapability(resource)
TransactionOpen(tx)
Permit(action)
```

An operation may consume them:

```kond
reset_password(token)
```

Contract:

```text
requires Own(_, token)
requires ValidResetToken(token)
consumes ValidResetToken(token)
```

A second use fails because the linear proof was consumed.

### 5.8 Typestate through conditions

A resource may have persistent or linear state facts.

```text
Open(file)
Closed(file)
Connected(socket)
Authenticated(session)
```

`close(file)` may require:

```text
Own(_, file)
Open(file)
```

and produce:

```text
Own(_, file')
Closed(file')
```

### 5.9 Aliasing and optimization

Uniqueness facts may justify optimization metadata:

```text
NoAlias(x, y)
UniqueBorrow(_, x)
Own(_, x) with no escaping aliases
```

These can enable:

- load elimination
- scalar replacement
- vectorization
- LICM
- store forwarding
- backend `noalias`-like guarantees

Such metadata may be emitted only when backed by verified ownership/alias proofs.

### 5.10 Decision procedure boundary

Ordinary ownership and borrow legality is decided by the deterministic checker in `05a-deterministic-ownership.md`. The general solver may consume certified ownership facts but does not decide basic borrow legality. Generic solver timeout/proof budgets therefore cannot cause ordinary borrow errors.

---

## 5A. Deterministic Ownership and Borrow Checking

Kond unifies ownership with conditions **semantically**, but does not require a universal proof search algorithm.

### 5A.1 One vocabulary, specialized decision procedure

Ordinary ownership, move, borrow, and lifetime legality is checked by a dedicated deterministic ownership/borrow checker.

It is not delegated to SMT and is not subject to heuristic proof budgets.

A normal borrow error must never be reported as:

```text
proof budget exceeded
```

### 5A.2 Linear state environment

The checker maintains a structural/dataflow state such as:

```text
Owned
Moved
SharedBorrowed(regions)
UniqueBorrowed(region)
PartiallyMoved(paths)
Frozen
```

The exact representation is implementation-defined, but ordinary legality must be deterministic.

### 5A.3 Certified output into the common fact system

The dedicated checker emits facts/capabilities such as:

```text
Own(scope, x)
BorrowShared(r, x)
BorrowUnique(r, x)
CanRead(x)
CanWrite(x)
NoAlias(x, y)
```

The general condition solver may consume these facts for contracts and optimization.

The general solver may not invent ownership or borrow facts that were not certified by the ownership checker.

### 5A.4 Linear proof certificate

The checker emits a compact certificate/state-transition trace. The proof kernel validates transitions such as:

```text
Own -> Moved
Own -> SharedBorrow
Own -> UniqueBorrow
BorrowEnd -> Own(new_version)
```

Thus:

```text
conceptual semantics: unified
checking algorithm: specialized/deterministic
certificate validation: unified kernel
```

### 5A.5 User-defined linear protocols

User-defined linear capabilities may use deterministic protocol automata:

```text
TransactionOpen -> Committed
TransactionOpen -> RolledBack
UnusedResetToken -> ConsumedResetToken
```

Arbitrary logical preconditions may surround a transition, but the consumption/production of the linear token itself remains structural and deterministic.

### 5A.6 Optional advanced alias reasoning

Optional analyses may certify more aggressive disjointness or sub-borrow facts. Failure of an optional analysis must have a predictable fallback, explicit annotation, or unsafe escape hatch; it must not destabilize basic borrow semantics.

---

## 6. Functions, Contracts, Effects, and Capabilities

### 6.1 Preconditions and postconditions

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

### 6.2 Inline parameter invariants

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

### 6.3 Effects as conditions/capabilities

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

### 6.4 Capability passing

Capabilities may be persistent or linear.

Read-only ambient permission may be persistent.

Exclusive write authority may be linear.

Example:

```kond
fn overwrite(file, data)
    requires CanWrite(file)
    requires Own(_, file)
```

### 6.5 Result conditions

Returned values may carry conditions inferred from implementation or contracts.

```kond
fn normalize_path(path)
    ensures NormalizedPath(result)
```

The caller gains `NormalizedPath(result)`.

### 6.6 Generic behavior through predicates

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

### 6.7 Unsafe functions

An unsafe function is one whose correctness requires caller-provided assumptions not enforced by ordinary checks.

Unsafe APIs must explicitly state obligations.

```kond
unsafe fn from_raw_pointer(ptr)
    requires ValidPointer(ptr)
    requires ProperlyAligned(ptr)
```

Unsafe blocks may discharge these requirements using `assume`, making the trust boundary auditable.

---

## 7. Proof Architecture: CIR, FIR, PIR

### 7.1 Goals

The compiler must compare conditions by meaning, not source syntax.

It therefore uses three conceptual IR layers:

```text
CIR — Condition Intermediate Representation
FIR — Fact Intermediate Representation
PIR — Proof Intermediate Representation
```

### 7.2 CIR

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

### 7.3 FIR

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

### 7.4 Solver composition

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

### 7.5 PIR

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

### 7.6 Trusted proof kernel

The optimizing compiler and complex solvers need not be fully trusted.

A small proof kernel validates PIR before proof-derived safety decisions are accepted.

The kernel should be:

- small
- deterministic
- side-effect free
- fuzzable
- independently testable

If a proof certificate fails validation, compilation must fail or fall back to runtime checking. It must never silently trust the optimizer.

### 7.7 Counterexamples

When a solver can produce a model demonstrating failure, diagnostics should display a minimized counterexample.

Example:

```text
Known: x <= 150
Required: x < 150
Counterexample: x = 150
```

### 7.8 Proof budgets

Complex proof search must be bounded.

`Unknown` diagnostics should report why:

```text
nonlinear arithmetic exceeded proof budget
unsupported quantifier
external solver timeout
condition depends on volatile state
```

`Unknown` must never be reported as `Refuted`.

---

## 8. Web and Input-Security Model

### 8.1 Boundary rule

All externally controlled data begins as `Any` plus an untrusted provenance condition.

Examples:

```text
Untrusted(request.body)
Untrusted(request.query)
Untrusted(request.headers)
Untrusted(request.cookies)
```

Parsing changes structure knowledge, not trust automatically.

```text
JSONParsed(body)
Object(body)
```

does not imply:

```text
Trusted(body)
```

### 8.2 Route-boundary validation

A route may declare input conditions:

```kond
route POST "/users" (
    body where
        self has {
            name: String where 1 <= len(self) <= 32,
            age: Int where 0 <= self < 150
        }
) {
    create_user(body)
}
```

The compiler/framework generates a validation boundary.

After successful validation:

```text
String(body.name)
1 <= len(body.name) <= 32
Int(body.age)
0 <= body.age < 150
```

are available as facts.

Validation failure may map to a framework-defined HTTP 400 response.

### 8.3 SQL safety

SQL execution APIs require structured SQL conditions.

```text
SqlStatement(query)
```

Plain strings do not satisfy this automatically.

Unsafe concatenation:

```kond
database.execute(
    "SELECT * FROM users WHERE name = '" + input + "'"
)
```

must fail unless `SqlStatement(...)` is proven.

A structured interpolation form:

```kond
database.query(
    sql"SELECT * FROM users WHERE name = ${input}"
)
```

must parameterize interpolated values and produce:

```text
SqlStatement(result)
```

This design prevents injection only when all SQL sinks honor these contracts and unsafe escape hatches are controlled.

### 8.4 HTML and XSS

HTML sinks require a trusted rendering condition.

Possible distinctions:

```text
HtmlText(x)
TrustedHtml(x)
AttributeValue(x)
UrlAttribute(x)
```

Raw untrusted text is not `TrustedHtml`.

Structured templates escape according to context:

```kond
html"<div>${user_input}</div>"
```

may transform:

```text
Untrusted(String)
  -> HtmlText
```

Context-sensitive escaping is required; generic string escaping is not sufficient for every HTML/JS/CSS/URL context.

### 8.5 Redirect safety

Redirect APIs may require:

```text
AllowedRedirectUrl(url)
```

Validation may establish:

```text
ParsedUrl(url)
HttpOrHttps(url)
SameOrigin(url)
```

according to application policy.

An arbitrary request query string does not satisfy the redirect sink requirement.

### 8.6 Path traversal

Filesystem APIs can require:

```text
NormalizedPath(path)
Within(path, allowed_root)
```

A safe path constructor must normalize and validate before producing these proofs.

String concatenation alone must not create `Within`.

### 8.7 Authentication and authorization

Authentication creates identity/session facts:

```text
Authenticated(user)
```

Authorization is separate:

```text
Owner(user, post)
HasRole(user, Admin)
CanDelete(user, post)
```

Dangerous operations declare requirements:

```kond
fn delete_post(user, post)
    requires Authenticated(user)
    requires Owner(user, post) or HasRole(user, Admin)
```

Missing authorization becomes a proof failure rather than a convention enforced only by code review.

### 8.8 One-time security resources

Reset tokens, nonces, permits, and transaction capabilities may be linear:

```text
ValidResetToken(token)
OneTimeNonce(nonce)
```

Successful use consumes the condition.

This prevents accidental double use in the language's linear resource model.

### 8.9 Security claim boundary

Kond does not magically make arbitrary code secure.

Security guarantees depend on:

- correct standard-library contracts
- correct trusted runtime implementation
- sound proof-kernel rules
- absence or auditing of unsafe assumptions
- correct encoding of application policy

The language makes missing checks and trust transitions explicit and machine-verifiable.

### 8.10 Propagation requirement

`Untrusted` and related information-flow labels use the dedicated propagation semantics in `08a-information-flow.md`. Every primitive/built-in has a flow summary. Missing summaries are handled conservatively. Source and sink declarations without propagation rules are not a complete taint system.

### 8.11 Standard HTTP server runtime

The Draft 0.2 runtime provides a small synchronous HTTP server for route declarations:

```sh
kond serve app.kd --bind 127.0.0.1 --port 8080
```

The implementation has no third-party HTTP or JSON dependency. It uses the C++17 runtime and the host operating system's socket interface. `--bind` defaults to `127.0.0.1`, `--port` defaults to `8080`, `--max-body` defaults to `1048576` bytes, and `--once` accepts one connection and exits. The server accepts HTTP/1.0 and HTTP/1.1 origin-form requests and closes each connection after one response.

Each `route METHOD "/path" (req) { ... }` is registered by its exact method and path. A missing path returns 404; a matching path with another method returns 405 and an `Allow` header. `HEAD` uses the matching `GET` handler when no explicit `HEAD` route exists and suppresses the response body.

The route request value is an `HttpRequest` with these fields:

```text
req.method       // String
req.target       // raw target, including the query
req.path         // path without the query
req.query        // Object<String, String>, percent-decoded
req.headers      // Object<String, String>, lower-case names
req.cookies      // Object<String, String>, lower-case names
req.body         // raw String
req.json()       // parsed JSON value
req.header(name) // one header, or Null
req.cookie(name) // one cookie, or Null
```

All request fields and all values returned by `req.json()` carry `Untrusted`. JSON parsing adds structure knowledge only; it does not endorse the input. Invalid JSON, failed `require`, and failed route input conditions are returned as HTTP 400. The default body limit is enforced before the handler runs; an oversized body returns 413.

Route results are mapped to responses as follows:

- an `html"..."` result is `text/html; charset=utf-8` and uses the existing context escaping rules;
- an Object or List result is serialized as JSON;
- `json_response(value)` returns an `application/json` response;
- `http_response(status, headers, body)` (also available as `response`) sets an explicit status and headers;
- other values are returned as UTF-8 text, and `Null` is a 204 response.

The server is deliberately sequential in Draft 0.2. It is a reference runtime and security-boundary implementation, not a production reverse proxy, TLS terminator, connection pool, or database driver. Deployments requiring those properties must put an audited component in front of it and must keep the standard-library contracts and the proof-kernel boundary intact.

---

## 8A. Information Flow, Taint, and Provenance

Draft 0.1 specified sources and sinks but not propagation. Draft 0.2 makes propagation explicit.

### 8A.1 Dedicated flow domain in the common fact substrate

`Untrusted(x)` is not a magical ordinary predicate that automatically propagates.

FIR contains a dedicated flow-label/provenance domain:

```text
FlowLabel(value)
Provenance(value)
InfluencedBy(output, input)
```

The key engineering rule is:

> Conceptual unification does not require one inference algorithm.

### 8A.2 Default explicit-flow rule

Every primitive and built-in declares a flow summary.

For ordinary pure deterministic primitives, the conservative default is:

```text
label(result) = join(label(input1), ..., label(inputN))
```

Example:

```kond
let z = x + y
```

implies:

```text
InfluencedBy(z, x)
InfluencedBy(z, y)
label(z) = join(label(x), label(y))
```

This default covers arithmetic, concatenation, collection construction, interpolation, and ordinary pure helpers unless a more precise checked summary exists.

### 8A.3 Function flow summaries

A function's flow behavior comes from:

1. compiler inference from the body,
2. an explicit checked summary,
3. a trusted/unsafe summary for opaque FFI/native code.

Example:

```kond
fn concat(a, b)
    flow result <- a, b
{
    return a + b
}
```

Opaque code with no trustworthy summary is treated conservatively.

### 8A.4 Provenance is not context safety

A sanitizer should not simply delete `Untrusted`.

After HTML escaping:

```text
Untrusted(input)
InfluencedBy(output, input)
HtmlText(output)
```

may all be true.

The useful new fact is `HtmlText(output)`, not a fictional universal `Trusted(output)`.

This prevents SQL-safe, HTML-safe, URL-safe, shell-safe, and JS-safe from being conflated.

### 8A.5 Context-specific safety transformations

Examples:

```text
escape_html_text(String(x)) -> HtmlText(result)
encode_url_component(String(x)) -> UrlComponent(result)
parameterize_sql(Any(x)) -> SqlParameter(result)
```

Sinks require exactly the context-safe condition they need.

### 8A.6 Declassification and endorsement

Policy-sensitive trust changes require explicit operations and proofs:

```text
declassify
endorse
sanitize
validate
```

These transitions are visible in proof/audit metadata. Ordinary arithmetic or string operations cannot silently upgrade trust.

### 8A.7 Explicit-flow and Strict IFC profiles

#### Explicit-flow profile

Tracks direct data dependencies.

#### Strict IFC profile

Also tracks a program-counter (`pc`) label for implicit control flow.

Conceptually:

```text
label(result) = join(label(explicit_inputs), pc_label)
```

For:

```kond
if secret {
    x = 1
} else {
    x = 0
}
```

Strict IFC marks `x` as influenced by `secret` even though constants were assigned.

Security-critical libraries may require Strict IFC.

### 8A.8 Side-channel boundary

This model does not automatically prove freedom from timing, termination, allocation, cache, exception, or microarchitectural side channels. Security claims must name modeled channels.

### 8A.9 Built-in completeness rule

Every value-producing built-in must have a checked flow rule. A missing summary is conservative, never optimistic.

The complete security pipeline is:

```text
source -> propagation -> context-specific transformation -> sink proof
```

---

## 9. Proof-Backed Optimization

### 9.1 Principle

Every nontrivial semantics-changing-looking rewrite must be justified by:

- language semantics,
- a proven condition,
- or a verified lemma.

The optimizer may be aggressive, but proof validation remains conservative.

### 9.2 Examples

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

### 9.3 Congruence domain

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

### 9.4 Mathematical properties as conditions

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

### 9.5 E-graph integration

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

### 9.6 Floating-point caution

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

### 9.7 Ownership-driven optimization

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

### 9.8 Optimization diagnostics

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

### 9.9 Floating-point proof classes

Floating-point rewrites use `09a-exact-approx-proofs.md`. `ExactEq`, `RealEq`, `ApproxEq`, and `HeuristicImprovement` are distinct certificate classes. A mathematical real identity does not automatically justify an IEEE floating-point rewrite.

---

## 9A. Exact and Approximate Rewrite Proofs

Floating-point optimization requires proof kinds that cannot be silently interchanged.

### 9A.1 PIR proof classes

```text
ExactEq(a, b, semantics)
RealEq(a, b, domain)
ApproxEq(a, b, metric, bound, domain)
HeuristicImprovement(a, b, metric, evidence)
```

#### ExactEq

Exact equivalence under named operational language semantics.

For floating point this includes relevant IEEE behavior: rounding, NaNs, infinities, signed zero, and specified exception behavior.

#### RealEq

Equality of ideal mathematical real-valued expressions over a domain.

`RealEq` is not sufficient for an exact floating-point rewrite.

#### ApproxEq

Certified approximation with explicit metric, error bound, and input domain.

#### HeuristicImprovement

Evidence from search/testing/sampling that a candidate appears numerically better. It is not a proof certificate.

### 9A.2 No unsafe coercion between proof classes

The proof kernel rejects:

```text
RealEq -> ExactEq
HeuristicImprovement -> ApproxEq
```

unless a separately verified theorem constructs the stronger certificate.

### 9A.3 Orthogonal matrix example

The property must name its semantic domain:

```text
Orthogonal<Real>(A)
Orthogonal<ExactRational>(A)
ApproximatelyOrthogonal<IEEE754>(A, epsilon)
```

`Orthogonal<Real>(A)` alone does not justify replacing ordinary floating-point:

```text
fp_matmul(transpose(A), A)
```

with an exact identity matrix.

Such a rewrite requires either `ExactEq` under floating-point semantics or an explicitly permitted `ApproxEq` with a certified bound.

### 9A.4 Optimization policy

Compilation/function policy declares accepted proof classes:

```text
exact_only
approximate(max_error = ...)
heuristic_profile_guided
```

Safety-critical passes default to `exact_only`.

### 9A.5 Heuristic numerical optimizer pipeline

A Herbie-like optimizer can be a candidate generator:

```text
heuristic search
 -> candidate
 -> exact/approx verifier
 -> certificate
 -> kernel validation
 -> rewrite
```

A failed verification leaves the candidate as advice, not a proved transformation.

### 9A.6 Algebraic model qualification

Lemmas must name the algebraic model in which they hold:

```text
Field<Real>
Field<Rational>
Ring<WrappingU32>
IEEE754Binary64
```

A theorem from one model cannot be silently used in another.

---

## 10. Diagnostic Specification

### 10.1 Core rule

Diagnostics should explain proof obligations, not merely report "type mismatch."

The preferred structure is:

```text
1. What operation was attempted?
2. What conditions were required?
3. What facts are currently known?
4. What is missing or contradictory?
5. Why did proof fail?
6. How can the program be repaired?
```

### 10.2 Error categories

Suggested codes:

```text
E1xxx  Conditions / proofs / invariants
E2xxx  Ownership / borrowing / linear resources
E3xxx  Security / trust / capability
E4xxx  Optimization proof
E5xxx  Effects / lifetime / resource state
```

Suggested initial codes:

```text
E1101  initial value violates invariant
E1104  assignment condition not proven
E1204  required condition not proven
E1207  nearly sufficient condition / boundary mismatch
E1302  previously proven condition invalidated
E1401  invalid condition strengthening
E1501  proof unknown

E2101  ownership no longer available
E2203  unique-borrow conflict
E2205  mutation during shared borrow
E2301  linear condition already consumed

E3102  authorization not proven
E3201  untrusted data at trusted sink
E3204  unsafe SQL construction

E4101  optimization applied with proof
E4102  optimization rejected due to missing proof
```

### 10.3 Invariant violation

Source:

```kond
let age where self is Int and 0 <= self < 150 = 200
```

Diagnostic:

```text
error[E1101]: initial value violates variable invariant
  --> src/main.kd:3:45
   |
 3 | let age where self is Int and 0 <= self < 150 = 200
   |                                                  ^^^
   |
   = required:
       Int(200)
       0 <= 200
       200 < 150

   = disproven:
       200 < 150

   = invariant accepts:
       Int intersect [0, 150)

   = provided:
       200
```

### 10.4 Missing proof

```text
error[E1204]: required condition could not be proven
  --> src/user.kd:18:5

   = operation requires:
       ValidUser(user)

   = currently known:
       Object(user)
       HasField(user, "name")
       String(user.name)
       HasField(user, "age")
       Int(user.age)

   = missing:
       0 <= user.age < 150

help: prove or check the missing condition before this operation
```

### 10.5 Counterexample

When possible:

```text
error[E1207]: condition is almost sufficient

   = required:
       x < 150

   = known:
       x <= 150

   = counterexample:
       x = 150
```

### 10.6 Invalidated proof

```text
error[E1302]: previously proven condition was invalidated

 8 | check user.age >= 18
   |       -------------- proven for user0 here

10 | user.age = input()
   | ------------------ creates user1 and invalidates field-dependent proof

12 | adult_only(user)
   | ^^^^^^^^^^^^^^^^ requires user1.age >= 18

   = old proof:
       user0.age >= 18

   = current value:
       user1
```

### 10.7 Ownership loss

```text
error[E2101]: value is no longer owned here

 5 | let y = move x
   |         ------ ownership transferred here

 7 | print(x)
   |       ^ requires Own(current_scope, x)

   = current ownership:
       Own(current_scope, y)

help: use `y` or restructure the move
```

### 10.8 Linear resource reuse

```text
error[E2301]: linear condition was already consumed

30 | reset_password(token)
   |                ----- ValidResetToken(token) consumed here

32 | reset_password(token)
   |                ^^^^^ required again here

   = required:
       Own(current_scope, token)
       ValidResetToken(token)

   = available linear facts:
       none
```

### 10.9 Security sink

```text
error[E3201]: untrusted data cannot be used as trusted HTML

   = required:
       TrustedHtml(value)

   = known:
       String(value)
       Untrusted(value)

   = no verified transformation:
       Untrusted -> TrustedHtml

help: use a context-aware HTML template or escaping function
```

### 10.10 Unknown versus false

A proof timeout or unsupported theory is not a contradiction.

```text
error[E1501]: proof could not be completed

   = goal:
       x*x + y*y >= 2*x*y

   = assumptions:
       Real(x)
       Real(y)

   = solver:
       nonlinear arithmetic

   = result:
       Unknown

   = reason:
       proof budget exhausted

note: `Unknown` does not mean the condition is false
```

### 10.11 Proof-diff display

When many requirements are already proven:

```text
required by xs[i]:
    [ok] List(xs)
    [ok] Int(i)
    [ok] 0 <= i
    [missing] i < len(xs)
```

The diagnostic should emphasize the minimal missing proof set.

### 10.12 Mathematical set visualization

Optional human-friendly rendering:

```text
expected:
    Int intersect [0, 100)

known:
    Int intersect [0, 256)

invalid region:
    [100, 256)
```

Diagnostics are effectively a human-readable rendering of proof obligations and PIR failure paths.

---

## 11. Draft Surface Grammar

This grammar is intentionally incomplete and non-normative. It exists to make the semantic draft concrete.

### 11.1 Bindings

Immutable binding:

```kond
let x = expression
```

Invariant-bearing mutable slot:

```kond
let x where condition_using_self = expression
```

Examples:

```kond
let count where self is Int and self >= 0 = 0
let name where self is String and 1 <= len(self) <= 32 = "Kicky1618"
```

### 11.2 Conditions

```kond
x is Int
x >= 0
x < len(xs)
x has "name"
P and Q
P or Q
not P
```

Recommended keywords are words (`and`, `or`, `not`) to emphasize that these construct conditions rather than machine booleans. Symbolic aliases may be supported later.

### 11.3 Condition declarations

```kond
condition Adult(user) =
    User(user) and user.age >= 18
```

### 11.4 Validation/proof statements

```kond
prove P
check P
require P

unsafe {
    assume P
}
```

### 11.5 Functions

```kond
fn name(arg1, arg2)
    requires P
    requires Q
    ensures R
{
    ...
}
```

Mutable parameter slot:

```kond
fn countdown(n where self is Int and self >= 0) {
    ...
}
```

### 11.6 Branches

```kond
if condition {
    ...
} else {
    ...
}
```

```kond
match value {
    when condition => ...
    when condition => ...
    else => ...
}
```

### 11.7 Ownership

Draft syntax:

```kond
let y = move x
let r = &x
let r = &mut x
```

The exact syntax may change; the linear semantics are more important than surface spelling.

### 11.8 Transactional mutation

```kond
update state {
    self.field = value
}
```

### 11.9 Structured safe literals

Potential standard forms:

```kond
sql"SELECT * FROM users WHERE id = ${id}"
html"<div>${text}</div>"
```

These forms are not ordinary string interpolation. They create domain-specific structured values and proofs.

### 11.10 Routes

Potential framework-integrated syntax:

```kond
route POST "/users" (
    body where UserInput(self)
) {
    ...
}
```

Route syntax may belong to a standard framework rather than the language core.

---

## 12. Runtime and ABI Model

### 12.1 Runtime `Any`

An implementation may represent `Any` using:

- tagged unions
- NaN boxing
- pointer tagging
- boxed objects
- specialized unboxed SSA values after proof-driven optimization

The source semantics must not depend on one representation.

### 12.2 Guards

When a condition is not statically proven, Safe mode inserts guards.

Conceptual lowering:

```text
candidate = evaluate(input)

if not runtime_check(Invariant(candidate)):
    condition_failure(...)

commit(candidate)
```

### 12.3 Specialized code

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

### 12.4 Proof erasure

Most proof objects are compile-time artifacts and may be erased.

Runtime evidence is retained only when needed for:

- dynamic checks
- reflective condition values
- security audit trails
- dynamic capability tokens
- deoptimization guards

### 12.5 Linear runtime resources

Some linear conditions correspond to real runtime state:

```text
file handles
locks
transactions
one-time tokens
unique mutable references
```

Compile-time linear proof consumption must correspond correctly to runtime ownership transfer/drop semantics.

### 12.6 Drop

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

### 12.7 FFI boundary

Foreign values enter as weakly known `Any` values unless trusted ABI contracts establish stronger conditions.

Unsafe FFI adapters may produce proofs only after:

- ABI validation,
- runtime validation,
- or explicit unsafe assumptions.

### 12.8 Concurrency

Concurrency semantics are deferred, but ownership/linear conditions are intended to support:

```text
Sendable(x)
Shareable(x)
ThreadSafe(x)
```

A future memory model should derive cross-thread transfer from these conditions rather than bypassing the ownership system.

---

## 13. Implementation Roadmap

### Stage 0 — Minimal interpreter

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

### Stage 1 — FIR and refinement

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

### Stage 2 — Invariant-bearing mutable slots

Add:

- `let x where ...`
- assignment proof obligations
- transactional updates
- invalidation rules
- `Verified` versus `Safe` mode

### Stage 3 — Ownership and linear conditions

Add:

- ownership tokens
- moves
- shared borrows
- unique borrows
- regions
- linear proof environment
- deterministic resource handling

Keep the first implementation simpler than Rust where possible; prove the semantic core before adding ergonomic lifetime inference.

### Stage 4 — PIR proof kernel

Add:

- proof certificates
- small verifier
- interval proofs
- tag/shape subset proofs
- equality rewrites
- linear-resource transitions

Fuzz compiler solver output against the proof kernel.

### Stage 5 — Web-safe standard library

Add proof-annotated APIs for:

- HTTP request inputs
- JSON validation
- SQL structured queries
- HTML templates
- URL parsing/redirect policy
- filesystem paths
- auth/authz capabilities

Create compile-fail security test suites.

### Stage 6 — Native backend/JIT

Recommended implementation language: Rust.

Candidate backends:

- Cranelift for early JIT/AOT
- LLVM for aggressive optimization
- WASM for sandboxed execution

Implement proof-driven specialization and guard elimination.

### Stage 7 — Mathematical optimizer

Add:

- congruence domain
- linear arithmetic
- lemma registry
- e-graph experimentation
- proof-producing rewrites
- optimization explain mode

Do not begin with arbitrary SMT. Start with cheap decidable domains.

### Stage 8 — External solver integration

Optional:

- SMT solver
- proof certificate translation
- bounded nonlinear reasoning
- user theorem libraries

External solver answers must not bypass the trusted proof kernel unless explicitly configured as trusted.

### Testing strategy

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

---

## 14. Related Work and Positioning

Kond combines ideas with substantial prior art. The research claim is not that refinement, contracts, ownership, proof certificates, taint tracking, or equality saturation are individually new.

### 14.1 Whiley

Whiley developed flow-sensitive typing and constrained types. Kond's `let x where ...` and flow refinement are closely related.

Intended difference: Kond pushes classification aggressively into facts over an `Any` value world, and tries to expose ownership-derived capabilities, security facts, and optimization preconditions through the same semantic fact interface.

### 14.2 Refinement / Liquid Types

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

### 14.3 Dafny

Dafny integrates specifications (`requires`, `ensures`, invariants) with automated verification and executable programs.

Kond is in the same verification-aware tradition. Its intended distinction is continuity between static proof, runtime decisions in Safe mode, proof-carrying flow facts, ownership/capability facts, and optimizer preconditions.

### 14.4 Racket contracts

Racket contracts dynamically guard boundaries, including higher-order function arguments/results and structured values.

Kond Safe mode is related: an unresolved but runtime-decidable proposition becomes a dynamic check. A successful check, however, becomes a proof-indexed flow fact that can later eliminate checks or enable optimization.

### 14.5 Rocq/Coq `sumbool` and decidable propositions

Rocq/Coq `sumbool A B` is an informative disjunction carrying evidence of either side and has a Boolean-like extracted representation.

This is a close precedent for:

```text
Condition<P> = Holds(Proof<P>) | Fails(Proof<not P>)
```

Therefore Kond's "no bool" slogan should be read as "no unindexed predicate-result bool", not "binary decisions disappeared from runtime machines".

### 14.6 Rust ownership and borrowing

Rust demonstrates practical ownership, moves, shared borrowing, mutable borrowing, and a dedicated borrow checker.

Kond's semantic experiment is to expose ownership results as linear facts/capabilities in the common vocabulary. Draft 0.2 nevertheless mandates a dedicated deterministic borrow checker. This is conceptual unification with procedural specialization.

### 14.7 Information-flow control and taint tracking

IFC/taint systems track labels/provenance through computation and enforce source/flow/sink policies.

Kond's `Untrusted` model belongs here. Draft 0.2 explicitly adds label propagation, function summaries, optional `pc` labels for implicit flows, and explicit declassification/endorsement. Merely writing `Untrusted(x)` as a condition is not sufficient.

### 14.8 Proof-Carrying Code / Foundational PCC

PCC associates code with machine-checkable safety evidence; foundational PCC reduces trusted infrastructure through smaller foundational checking.

Kond's:

```text
complex solver/optimizer -> certificate -> small kernel
```

is directly related. Kond applies the pattern internally to implication proofs, linear transitions, guard elimination, and optimization—not only binary distribution.

### 14.9 Equality saturation / egg

`egg` provides a practical, extensible e-graph/equality-saturation infrastructure.

Kond may use e-graphs for search, but does not trust the e-graph itself:

```text
e-graph search -> candidate -> PIR certificate -> kernel validation
```

### 14.10 Herbie and numerical rewriting

Herbie automatically searches for floating-point expression rewrites that improve numerical accuracy using heuristic methods.

Kond may use similar search as candidate generation, but Draft 0.2 separates `ExactEq`, `RealEq`, `ApproxEq`, and `HeuristicImprovement` so heuristic numerical improvement cannot masquerade as exact semantic equivalence.

### 14.11 Intended differentiation

The strongest intended differences are:

1. **Extreme Any-first model** — familiar "types" are largely facts about `Any` values.
2. **One fact vocabulary across normally separate domains** — refinement, invariants, ownership-derived capabilities, typestate, security policy, taint/provenance, and optimization preconditions share a semantic interface while retaining specialized solvers.
3. **Proof as compiler infrastructure** — certificates justify safety, guard elimination, specialization, capability derivation, and rewrites.
4. **Static/dynamic continuity** — `Unknown` may become a runtime `Condition<P>` decision whose branch evidence later feeds static reasoning.

A defensible research statement is:

> Kond explores whether an Any-first, evidence-indexed condition calculus can serve as a common semantic interface between refinement, runtime contracts, ownership capabilities, information flow, security policy, and proof-backed optimization—without sacrificing specialized predictable decision procedures.

---

## 15. Standard library and optimization libraries

The Draft 0.2 reference interpreter exposes a built-in `std.*` namespace. It
is available without an import declaration and is not an ordinary mutable
object. The namespaces are `std.core`, `std.math`, `std.string`, `std.list`,
`std.map`, `std.json`, `std.url`, `std.html`, `std.security`, `std.http`,
`std.io`, `std.pred`, and `std.opt`.

Collection APIs are persistent by default: they return fresh values and do
not mutate their arguments. Ranges and slices are half-open. Draft 0.2 string
indexing is byte-oriented. Flow labels join through every operation; standard
APIs do not declassify or endorse values. Security-marking APIs add only
their documented `HtmlText`, `UrlComponent`, or SQL-parameter evidence.
Overflow, invalid JSON, invalid indexes, and invalid encodings are errors.

Examples:

```kd
let xs = std.core.range(0, 10)
let positive = std.list.filter(xs, x => x > 0)
require std.list.all(positive, x => x is Int)
```

Predicate calls used as values may materialize the existing runtime `Bool`
representation at a data/library boundary. This does not add source-level
`true`/`false` literals or a primitive static Boolean type; condition
positions continue to use proposition-oriented `Condition<P>` semantics.

The complete API index and compatibility rules are in
`docs/15-standard-library.md`.

An optimization library is an explicitly loaded `.kd` file containing only
conditions and rewrite declarations:

```kd
rewrite exact add_zero(x) when x is Int {
    from x + 0
    to x
}
```

The `from` expression is matched with declared expression parameters. The
instantiated `when` condition must be proven in the current fact environment.
The reference interpreter executes only `ExactEq` rules and only for
effect-free matches, with a bounded recursive rewrite depth and ordinary
execution as fallback. Pure standard-library calls have a closed effect
summary; I/O, mutation, user functions, moves, and borrows are excluded.
`real`, `approx`, and `heuristic` declarations remain non-executable
candidates; they are never silently coerced to `ExactEq`. Load a library with
`--opt-lib FILE`. The shipped 30-rule exact integer library is in
`std/optimization.kd`. The core optimizer also performs checked constant
folding, arithmetic/bitwise identities, and collection-length rewrites that
avoid materializing intermediate ranges, repeats, concatenations, sorts, and
slices.
