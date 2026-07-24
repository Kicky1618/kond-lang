# 8A. Information Flow, Taint, and Provenance

Draft 0.1 specified sources and sinks but not propagation. Draft 0.2 makes propagation explicit.

## 8A.1 Dedicated flow domain in the common fact substrate

`Untrusted(x)` is not a magical ordinary predicate that automatically propagates.

FIR contains a dedicated flow-label/provenance domain:

```text
FlowLabel(value)
Provenance(value)
InfluencedBy(output, input)
```

The key engineering rule is:

> Conceptual unification does not require one inference algorithm.

## 8A.2 Default explicit-flow rule

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

## 8A.3 Function flow summaries

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

## 8A.4 Provenance is not context safety

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

## 8A.5 Context-specific safety transformations

Examples:

```text
escape_html_text(String(x)) -> HtmlText(result)
encode_url_component(String(x)) -> UrlComponent(result)
parameterize_sql(Any(x)) -> SqlParameter(result)
```

Sinks require exactly the context-safe condition they need.

## 8A.6 Declassification and endorsement

Policy-sensitive trust changes require explicit operations and proofs:

```text
declassify
endorse
sanitize
validate
```

These transitions are visible in proof/audit metadata. Ordinary arithmetic or string operations cannot silently upgrade trust.

## 8A.7 Explicit-flow and Strict IFC profiles

### Explicit-flow profile

Tracks direct data dependencies.

### Strict IFC profile

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

## 8A.8 Side-channel boundary

This model does not automatically prove freedom from timing, termination, allocation, cache, exception, or microarchitectural side channels. Security claims must name modeled channels.

## 8A.9 Built-in completeness rule

Every value-producing built-in must have a checked flow rule. A missing summary is conservative, never optimistic.

The complete security pipeline is:

```text
source -> propagation -> context-specific transformation -> sink proof
```
