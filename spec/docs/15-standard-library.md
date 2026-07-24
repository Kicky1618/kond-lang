# 15. Standard Library and Optimization Libraries

## 15.1 Status and availability

The Draft 0.2 reference interpreter exposes the standard library through the
`std` namespace. No import statement is required for these built-ins:

```kd
let xs = std.core.range(0, 10)
let large = std.list.filter(xs, x => x >= 5)
let encoded = std.url.encode_component("a value")
```

The namespace is a capability boundary, not a normal mutable object. A name
such as `std.math` can be passed or printed, but only registered library
operations can be called from it.

All collection ranges and slices use a half-open interval `[start, end)`.
String indexing APIs operate on UTF-8 bytes in Draft 0.2; they do not claim to
be Unicode grapheme operations. Collection-producing APIs return fresh values
and do not mutate their arguments. Existing `push` and `pop` methods remain
the explicit mutable-list API.

Implementations must preserve the normal Kond rules while executing a library
operation:

- argument flow labels are joined into the result;
- a library operation cannot remove `Untrusted`, `Secret`, or `Personal`;
- HTML/URL/SQL operations add only their documented safety mark;
- integer overflow, invalid indexes, invalid JSON, and invalid encodings are
  reported as Kond errors rather than silently wrapped;
- bounded constructors such as `range`, `repeat`, and string repetition have
  a one-million-element/iteration safety limit in the reference interpreter.

## 15.2 Namespace index

| Namespace | Main operations |
| --- | --- |
| `std.core` | `clone`, `type_of`, `to_string`, `coalesce`, `range`, `repeat` |
| `std.math` | `abs`, `sqrt`, `floor`, `ceil`, `round`, `pow`, `min`, `max`, `clamp`, `sign`, `gcd`, `lcm` |
| `std.string` | `length`, `trim`, case conversion, search, `split`, `join`, `replace`, slicing, parsing, repetition, reverse |
| `std.list` | length/access, slicing, append/prepend/concat, search, filtering, counting, sum, sorting, zip, `all`/`any`/`none` |
| `std.map` | size, lookup, membership, keys/values/entries, persistent `put`/`remove`, merge |
| `std.json` | `parse`, `stringify`, `pretty`, `is_valid` |
| `std.url` | component encode/decode and query parsing |
| `std.html` | text/attribute escaping with `HtmlText` evidence |
| `std.security` | flow-source constructors, sanitizers, flow inspection, unsafe boundary operations |
| `std.http` | `response`, `json_response` |
| `std.io` | `read_line`, `print`, `println` |
| `std.pred` | reusable conditions for type, numeric, collection, equality, range, and membership facts |
| `std.opt` | introspection of loaded optimization rules |

`std.list.filter`, `count_if`, and `find` accept the Draft 0.2 one-argument
condition lambda form, for example `x => x > 0`. `std.list.all`, `any`, and
`none` can be used directly in a condition:

```kd
require std.list.all(xs, x => x is Int)
if std.pred.in_range(score, 0, 100) {
    print("accepted")
}
```

The predicate namespace includes `is_null`, `is_int`, `is_float`,
`is_number`, `is_bool`, `is_string`, `is_list`, `is_object`, `is_finite`,
`equal`, `not_equal`, `positive`, `nonnegative`, `negative`, `even`, `odd`,
`in_range`, `contains`, `has_key`, `is_empty`, `all`, `any`, and `none`.
Namespaced predicate calls
used as values produce runtime `Bool` values; the same calls used in
`if`/`check`/`require` are evaluated as conditions.

This does not add a `true`/`false` source literal or a primitive static
Boolean type. It is the existing runtime `Bool` representation exposed at a
library/data boundary (also used by JSON); condition positions still retain
the proposition-oriented Kond semantics.

## 15.3 Security-sensitive APIs

`std.json.parse` retains the input flow label on parsed values. `std.html.escape`
returns a string carrying `HtmlText`; `std.url.encode_component` returns a
string carrying `UrlComponent`; and `std.security.parameterize_sql` retains
the SQL-parameter evidence used by SQL APIs. These marks do not declassify a
flow label.

`std.http.response(status, headers, body)` and
`std.http.json_response(value)` produce the same response values accepted by
the `route` runtime. `std.io.read_line` is an `Untrusted` source. The database
stub and HTML/SQL sinks continue to reject values lacking their required
evidence.

## 15.4 Optimization library syntax

An optimization library is a `.kd` file containing only `condition` and
`rewrite` declarations. It is loaded explicitly:

```sh
kond run program.kd --opt-lib std/optimization.kd
kond check program.kd --opt-lib my_rules.kd
```

The rule form is:

```kd
rewrite exact divisible_roundtrip(x, d) when d > 0 and x % d == 0 {
    from (x / d) * d
    to x
}
```

Parameters are expression pattern variables. `from` is matched against the
current expression, `when` is instantiated with the match and must be
`Proven`, and `to` is evaluated as the replacement. The reference interpreter
applies only `exact` (`ExactEq`) rules. It requires the matched expression to
be effect-free, applies a bounded recursive rewrite depth, and falls back to
the original interpreter when a candidate cannot be evaluated. Thus a library
cannot silently duplicate an effectful call or turn an unproved rewrite into
an optimization.

The parser also accepts `real`, `approx`, and `heuristic` proof classes so a
future verifier can retain candidate libraries without changing their source
format. They are registered for inspection but are not executable rewrites in
Draft 0.2. `--explain-optimizations` reports applied rules with their proof
class and semantics domain. `std.opt.rule_count()` and
`std.opt.rule_names()` expose the loaded registry to diagnostics.

The standard optimization library currently contains 30 exact rules for
divisible division round-trips, zero/one arithmetic, bitwise identities,
self-operations, nonnegative `abs`, and same-operand numeric extrema. The
core optimizer additionally performs checked Int64 constant folding, proves
safe arithmetic identities, folds pure standard-library calls, and eliminates
avoidable collection materialization. For example, the length of
`std.core.range`, `std.core.repeat`, `std.list.concat`, `std.list.zip`, or
`std.string.repeat` can be computed without constructing the intermediate
value. Counted monotone loops, bounded `abs`, and bounded integer masks remain
core ExactEq rewrites.

Pure standard calls have a closed effect summary. I/O, input, database, HTTP,
mutation methods, user functions, moves, and borrows are never treated as
compile-time-pure by this optimizer. A rejected or non-pure candidate falls
back to ordinary evaluation.

## 15.5 Compatibility and evolution

The names and error conditions in this document are the Draft 0.2 reference
surface. New APIs must be additive. A future module/package system may expose
the same namespaces as separately versioned packages, but it must preserve
flow/safety propagation and the distinction between an executable exact
rewrite and a non-exact optimization candidate.

## 15.6 Explicit source libraries

In addition to the built-in `std.*` namespace and optimization libraries,
the reference CLI accepts ordinary Kond source libraries with `--lib FILE`:

```sh
kond run program.kd --lib math_library.kd
kond check program.kd --lib math_library.kd
```

An explicit source library may contain `fn`, `condition`, and `extern fn`
declarations.
Those declarations are merged into the program before validation; library
top-level statements are never executed. `route` declarations and `rewrite`
declarations are rejected for `--lib`: routes belong to the selected server
program and rewrites must be loaded with `--opt-lib`. Function and condition
names are currently merged into one program-level namespace, so duplicate
exports are diagnosed. This is a source-loading facility, not yet a versioned
module or binary package ABI.
