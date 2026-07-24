# 8. Web and Input-Security Model

## 8.1 Boundary rule

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

## 8.2 Route-boundary validation

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

## 8.3 SQL safety

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

## 8.4 HTML and XSS

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

## 8.5 Redirect safety

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

## 8.6 Path traversal

Filesystem APIs can require:

```text
NormalizedPath(path)
Within(path, allowed_root)
```

A safe path constructor must normalize and validate before producing these proofs.

String concatenation alone must not create `Within`.

## 8.7 Authentication and authorization

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

## 8.8 One-time security resources

Reset tokens, nonces, permits, and transaction capabilities may be linear:

```text
ValidResetToken(token)
OneTimeNonce(nonce)
```

Successful use consumes the condition.

This prevents accidental double use in the language's linear resource model.

## 8.9 Security claim boundary

Kond does not magically make arbitrary code secure.

Security guarantees depend on:

- correct standard-library contracts
- correct trusted runtime implementation
- sound proof-kernel rules
- absence or auditing of unsafe assumptions
- correct encoding of application policy

The language makes missing checks and trust transitions explicit and machine-verifiable.

## 8.10 Propagation requirement

`Untrusted` and related information-flow labels use the dedicated propagation semantics in `08a-information-flow.md`. Every primitive/built-in has a flow summary. Missing summaries are handled conservatively. Source and sink declarations without propagation rules are not a complete taint system.

## 8.11 Standard HTTP server runtime

The Draft 0.2 runtime provides a small synchronous HTTP server for route declarations:

```sh
kond serve app.kd --bind 127.0.0.1 --port 8080
```

The implementation has no third-party HTTP or JSON dependency. It uses the C++17 runtime and the host operating system's socket interface. `--bind` defaults to `127.0.0.1`, `--port` defaults to `8080`, `--max-body` defaults to `1048576` bytes, and `--once` accepts one connection and exits (useful for tests). The server accepts HTTP/1.0 and HTTP/1.1 origin-form requests and closes each connection after one response.

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

The executable standard-library forms of these APIs are `std.json`,
`std.html`, `std.url`, `std.security`, and `std.http`. Their complete
flow/safety propagation and persistent collection behavior are specified in
`15-standard-library.md`.
