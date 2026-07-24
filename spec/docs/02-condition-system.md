# 2. Condition System

## 2.1 Conditions denote sets of valid states

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

## 2.2 Comparison result

The compiler exposes the following semantic comparison:

```text
Equivalent
Stronger
Weaker
Incomparable
Unknown
```

`Unknown` means the proof engine could not decide the relation within supported theories or budgets.

## 2.3 Logical composition

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

## 2.4 Meet and join

For facts at control-flow merge points, the compiler computes an abstract join.

For intervals:

```text
join([0, 10], [5, 20]) = [0, 20]
meet([0, 10], [5, 20]) = [5, 10]
```

A precise implementation may retain disjunctions; an optimizing implementation may widen them.

## 2.5 Condition declaration

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

## 2.6 Stable and volatile conditions

Conditions are classified by stability.

### Stable conditions

Remain valid while referenced immutable value versions remain unchanged.

```text
Int(x)
x >= 0
HasField(x, "name")
```

### Volatile conditions

Depend on external mutable state, time, or environment.

```text
FileExists(path)
ServiceHealthy(url)
ClockBefore(deadline)
```

A volatile observation must not be promoted to a permanent fact without an explicit capability or snapshot guaranteeing stability.

## 2.7 Decidability classes

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

## 2.8 Proof, check, require, assume

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

## 2.9 First-class condition values

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
