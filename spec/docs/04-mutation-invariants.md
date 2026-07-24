# 4. Condition-Preserving Mutable Slots

## 4.1 Immutability by default

A plain binding is immutable:

```kond
let x = 10
x = 20   // error
```

## 4.2 Mutation is introduced by an invariant

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

## 4.3 Assignment semantics

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

## 4.4 Mutation capability as a set

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

## 4.5 Compound updates

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

## 4.6 Transactional object mutation

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

## 4.7 External dependencies in invariants

Persistent slot invariants should normally depend only on:

- `self`
- constants
- immutable bindings
- stable pure functions

An invariant depending on another mutable slot introduces dependency invalidation and is rejected by default.

Instead, related mutable values should be grouped into a single invariant-bearing state object.

## 4.8 Interior mutation

Interior mutation requires ownership or unique-borrow permission and must preserve all affected invariants.

Persistent collections are the simplest semantic model:

```kond
xs = xs.push(4)
```

An optimized implementation may mutate in place when uniqueness is proven.
