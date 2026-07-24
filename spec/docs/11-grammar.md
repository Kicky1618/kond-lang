# 11. Draft Surface Grammar

This grammar is intentionally incomplete and non-normative. It exists to make the semantic draft concrete.

## 11.1 Bindings

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

## 11.2 Conditions

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

## 11.3 Condition declarations

```kond
condition Adult(user) =
    User(user) and user.age >= 18
```

## 11.4 Validation/proof statements

```kond
prove P
check P
require P

unsafe {
    assume P
}
```

## 11.5 Functions

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

## 11.6 Branches

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

Collection iteration:

```kond
for item in values {
    use(item)
}
```

The reference interpreter requires the expression after `in` to evaluate to
a `List`. It evaluates that expression once and visits a bounded snapshot of
the elements in source order.

## 11.7 Ownership

Draft syntax:

```kond
let y = move x
let r = &x
let r = &mut x
```

The exact syntax may change; the linear semantics are more important than surface spelling.

## 11.8 Transactional mutation

```kond
update state {
    self.field = value
}
```

## 11.9 Structured safe literals

Potential standard forms:

```kond
sql"SELECT * FROM users WHERE id = ${id}"
html"<div>${text}</div>"
```

These forms are not ordinary string interpolation. They create domain-specific structured values and proofs.

## 11.10 Routes

Implemented route declaration syntax:

```kond
route POST "/users" (req) {
    let body = req.json()
    require UserInput(body)
    ...
}
```

The reference runtime exposes these declarations through `kond serve`. The Draft 0.2 server uses exact method/path matching; request parsing and response mapping are specified in `08-web-security.md`. Path parameters and asynchronous handlers are not part of this draft.

## 11.11 Optimization library declarations

An explicitly loaded optimization library may contain `condition` declarations
and proof-classified rewrite declarations:

```text
rewrite exact add_zero(x) when x is Int {
    from x + 0
    to x
}
```

The complete standard-library and rewrite semantics are specified in
`15-standard-library.md`. `exact` rules are executable only after their
instantiated `when` condition is proven; `real`, `approx`, and `heuristic`
rules are retained as non-executable candidates in Draft 0.2.
