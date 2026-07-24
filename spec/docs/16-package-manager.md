# 16. Package Manager

The reference CLI provides a small source-package manager for local
development. It deliberately reuses the existing `--lib` source-library
boundary: packages do not introduce a second execution or trust model.

## 16.1 Manifest

Each package is a directory containing `kond.json`. A package that exposes a
POSIX C FFI adapter can list package-relative shared libraries in `native`:

```json
{
  "name": "app",
  "version": "0.1.0",
  "entry": "main.kd",
  "native": ["native/libmath.so"],
  "dependencies": {
    "greeting": { "path": "../greeting", "version": "0.1.0" }
  }
}
```

`name` is required. `version` defaults to `0.1.0`, and `entry` defaults to
`main.kd`. Dependency paths are relative to the package containing the
manifest and may point outside that package. A dependency's manifest name
must match its dependency key, and a specified version must match exactly.

An optional `library` field selects the source file exported to dependents. If
it is absent, `entry` is used. The selected file must satisfy the normal
`--lib` rules: it may contain `fn` and `condition` declarations, but no
top-level executable statements, `route`, or `rewrite` declarations.
Every `native` item must be a unique, project-relative regular file. FFI
declarations should refer to it with a path relative to their `.kd` file, for
example `from "native/libmath.so"`. Native artifacts are platform-specific and
use the limited POSIX C ABI documented in the runtime ABI specification; they
are not a stable Kond binary ABI.

## 16.2 Commands

```sh
kond new app
kond add ../greeting --project app
kond install app
kond list app
kond run app
kond check app
kond remove greeting --project app
```

`run` and `check` accept a package directory in addition to a `.kd` file. They
resolve the manifest dependency graph and load dependency libraries in
dependency-first order. `serve` accepts a package directory as well, so the
same source graph is used for route programs.

`add` accepts a local package directory, writes its relative path and exact
version to `kond.json`, and refreshes `kond.lock`. `remove` deletes a named
dependency and refreshes the lockfile. `install` validates the graph and writes
the lockfile; local path resolution itself does not perform network access.

## 16.3 Resolution and lockfiles

The resolver rejects missing manifests, name/version mismatches, cycles, and
two different directories that claim the same package name. Resolution is
deterministic because dependency names are processed in sorted order and
transitive packages are recorded dependency-first.

`kond.lock` records the lockfile version, root package identity, and each
resolved package's exact version and path relative to the root. The current
implementation resolves local paths directly from `kond.json`; the lockfile
is the reproducible resolution record and is not a registry cache.

Checksums, semver ranges, artifact signatures, namespace isolation, and a
stable package ABI are future extensions.

## 16.4 Source/native registry server

The reference CLI can host source packages and their declared native artifacts
with:

```sh
kond registry .kond-registry --bind 127.0.0.1 --port 8787
```

The server is synchronous and defaults to the loopback address. Its storage
layout is:

```text
.kond-registry/
└── greeting/
    └── 0.1.0/
        └── package.json
```

The stored `package.json` is a JSON bundle containing `name`, `version`, and a
`files` object. The files include the package `kond.json` and its entry or
exported library source. If `kond.json` has a `native` array, the bundle also
contains a `binary` object whose values are base64-encoded bytes keyed by the
same relative paths. A published bundle must have a safe package name and
version, matching identity in both locations, and no path traversal. Packages
with non-empty `dependencies` are rejected until remote dependency resolution
is specified.

The HTTP API is intentionally small:

```text
GET  /healthz
GET  /index.json
GET  /packages/<name>/<version>
POST /packages/<name>/<version>  (publish)
PUT  /packages/<name>/<version>  (publish)
```

The CLI maps these operations to `publish` and `fetch`:

```sh
kond publish greeting --registry http://127.0.0.1:8787
kond fetch greeting 0.1.0 --registry http://127.0.0.1:8787 --project app
```

`fetch` expands the bundle under `vendor/<name>`, records a local dependency,
and refreshes `kond.lock`. Existing non-empty vendor directories are not
overwritten, and native bytes are restored before dependency validation. The
registry currently has no TLS, authentication, semver range selection,
checksums, or artifact signatures; it is intended for trusted local
development or for use behind an appropriately secured proxy.
