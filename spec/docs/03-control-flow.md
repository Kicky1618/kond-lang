# 3. Control Flow and Refinement

## 3.1 `if`

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

## 3.2 Compound refinement

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

## 3.3 Early-return refinement

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

## 3.4 `match`

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

## 3.5 Loops and invariants

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

## 3.6 Collection iteration

`for` evaluates its collection expression once and iterates a snapshot of a
`List` in source order:

```kond
let total where self is Int and self >= 0 = 0
for value in std.core.range(1, 6) {
    total += value
}
```

The iteration binding is immutable and exists only for the current iteration.
The body may still update an explicitly mutable outer slot or mutate a nested
collection through the normal ownership rules. A non-`List` target is a
runtime type error, and iteration is capped at one million elements, matching
the reference interpreter's other bounded loops and collection constructors.

The snapshot makes iteration deterministic when the body uses `push` or
`pop` on the original list; those mutations do not change which elements are
visited.

## 3.7 Control-flow merge

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

## 3.8 Conditions are not implicit coercions

A condition being provable does not automatically mutate a value representation.

For example:

```text
Int(x)
```

does not imply that `x` has been unboxed into a machine integer. It only permits an implementation to specialize representation if that transformation is semantics-preserving.
