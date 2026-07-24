# 5A. Deterministic Ownership and Borrow Checking

Kond unifies ownership with conditions **semantically**, but does not require a universal proof search algorithm.

## 5A.1 One vocabulary, specialized decision procedure

Ordinary ownership, move, borrow, and lifetime legality is checked by a dedicated deterministic ownership/borrow checker.

It is not delegated to SMT and is not subject to heuristic proof budgets.

A normal borrow error must never be reported as:

```text
proof budget exceeded
```

## 5A.2 Linear state environment

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

## 5A.3 Certified output into the common fact system

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

## 5A.4 Linear proof certificate

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

## 5A.5 User-defined linear protocols

User-defined linear capabilities may use deterministic protocol automata:

```text
TransactionOpen -> Committed
TransactionOpen -> RolledBack
UnusedResetToken -> ConsumedResetToken
```

Arbitrary logical preconditions may surround a transition, but the consumption/production of the linear token itself remains structural and deterministic.

## 5A.6 Optional advanced alias reasoning

Optional analyses may certify more aggressive disjointness or sub-borrow facts. Failure of an optional analysis must have a predictable fallback, explicit annotation, or unsafe escape hatch; it must not destabilize basic borrow semantics.
