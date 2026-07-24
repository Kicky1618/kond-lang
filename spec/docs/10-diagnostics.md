# 10. Diagnostic Specification

## 10.1 Core rule

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

## 10.2 Error categories

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
E0113  unknown optimization proof class
E0114  duplicate optimization rule
E0115  optimization library contains a non-library declaration
E0116  duplicate optimization parameter
E0117  source library contains an executable top-level statement
E0118  source library contains an optimization rewrite
E0119  source library contains a route declaration
```

## 10.3 Invariant violation

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

## 10.4 Missing proof

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

## 10.5 Counterexample

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

## 10.6 Invalidated proof

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

## 10.7 Ownership loss

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

## 10.8 Linear resource reuse

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

## 10.9 Security sink

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

## 10.10 Unknown versus false

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

## 10.11 Optimization library diagnostics

An optimization library is rejected if it contains functions or executable
top-level statements. A rule whose instantiated precondition is `Unknown` is
not an error during ordinary `safe` execution; it is skipped and the original
expression is evaluated. With `--explain-optimizations`, successful rules are
reported with `ExactEq`, `RealEq`, `ApproxEq`, or `HeuristicImprovement`.

## 10.12 Proof-diff display

When many requirements are already proven:

```text
required by xs[i]:
    [ok] List(xs)
    [ok] Int(i)
    [ok] 0 <= i
    [missing] i < len(xs)
```

The diagnostic should emphasize the minimal missing proof set.

## 10.13 Mathematical set visualization

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
