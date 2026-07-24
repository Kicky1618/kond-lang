# 9A. Exact and Approximate Rewrite Proofs

Floating-point optimization requires proof kinds that cannot be silently interchanged.

## 9A.1 PIR proof classes

```text
ExactEq(a, b, semantics)
RealEq(a, b, domain)
ApproxEq(a, b, metric, bound, domain)
HeuristicImprovement(a, b, metric, evidence)
```

### ExactEq

Exact equivalence under named operational language semantics.

For floating point this includes relevant IEEE behavior: rounding, NaNs, infinities, signed zero, and specified exception behavior.

### RealEq

Equality of ideal mathematical real-valued expressions over a domain.

`RealEq` is not sufficient for an exact floating-point rewrite.

### ApproxEq

Certified approximation with explicit metric, error bound, and input domain.

### HeuristicImprovement

Evidence from search/testing/sampling that a candidate appears numerically better. It is not a proof certificate.

## 9A.2 No unsafe coercion between proof classes

The proof kernel rejects:

```text
RealEq -> ExactEq
HeuristicImprovement -> ApproxEq
```

unless a separately verified theorem constructs the stronger certificate.

## 9A.3 Orthogonal matrix example

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

## 9A.4 Optimization policy

Compilation/function policy declares accepted proof classes:

```text
exact_only
approximate(max_error = ...)
heuristic_profile_guided
```

Safety-critical passes default to `exact_only`.

## 9A.5 Heuristic numerical optimizer pipeline

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

## 9A.6 Algebraic model qualification

Lemmas must name the algebraic model in which they hold:

```text
Field<Real>
Field<Rational>
Ring<WrappingU32>
IEEE754Binary64
```

A theorem from one model cannot be silently used in another.
