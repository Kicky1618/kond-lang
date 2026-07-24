# 5. Ownership, Borrowing, and Linear Conditions

## 5.1 Ownership is a linear fact

Ownership is represented as a non-duplicable logical resource:

```text
Own(scope, value)
```

Unlike ordinary persistent facts such as `Int(x)`, ownership facts cannot be copied arbitrarily.

The proof system therefore distinguishes:

```text
Persistent facts
Linear facts
```

## 5.2 Move

Ownership-bearing values move by default unless a `Copy`-like condition is proven.

```kond
let y = move x
```

Proof transition:

```text
consume Own(scope, x)
produce Own(scope, y)
```

Using `x` afterward requires a fact that no longer exists.

## 5.3 Shared borrow

```kond
let r = &x
```

Conceptually:

```text
Own(scope, x)
  -> FrozenOwn(scope, x)
   + BorrowShared(r, x)
```

While shared borrows exist:

```text
CanRead(x)
not CanWrite(x)
not CanMove(x)
```

The implementation may model borrow multiplicity through regions rather than literal token counting.

## 5.4 Unique borrow

```kond
let r = &mut x
```

Conceptually:

```text
Own(scope, x)
  -> SuspendedOwn(scope, x)
   + BorrowUnique(r, x)
```

A unique borrow grants:

```text
CanRead(x)
CanWrite(x)
```

but does not grant unrestricted movement of the owner unless specifically permitted.

## 5.5 Borrow restoration

At the end of the borrow region:

```text
consume BorrowUnique(r, x)
consume SuspendedOwn(scope, x)
produce Own(scope, x_new_version)
```

Mutation may produce a new value version.

Any condition referring to the pre-mutation version remains valid only for that old version.

## 5.6 Capability derivation

Operations require capabilities rather than directly special-casing ownership.

Example derivations:

```text
Own(_, x)          => CanRead(x), CanWrite(x), CanMove(x), CanDrop(x)
BorrowUnique(_,x)  => CanRead(x), CanWrite(x)
BorrowShared(_,x)  => CanRead(x)
```

These relations form a partial order/capability lattice.

## 5.7 Linear security resources

Linear conditions generalize beyond memory ownership:

```text
ValidResetToken(token)
OneTimeNonce(nonce)
WriteCapability(resource)
TransactionOpen(tx)
Permit(action)
```

An operation may consume them:

```kond
reset_password(token)
```

Contract:

```text
requires Own(_, token)
requires ValidResetToken(token)
consumes ValidResetToken(token)
```

A second use fails because the linear proof was consumed.

## 5.8 Typestate through conditions

A resource may have persistent or linear state facts.

```text
Open(file)
Closed(file)
Connected(socket)
Authenticated(session)
```

`close(file)` may require:

```text
Own(_, file)
Open(file)
```

and produce:

```text
Own(_, file')
Closed(file')
```

## 5.9 Aliasing and optimization

Uniqueness facts may justify optimization metadata:

```text
NoAlias(x, y)
UniqueBorrow(_, x)
Own(_, x) with no escaping aliases
```

These can enable:

- load elimination
- scalar replacement
- vectorization
- LICM
- store forwarding
- backend `noalias`-like guarantees

Such metadata may be emitted only when backed by verified ownership/alias proofs.

## 5.10 Decision procedure boundary

Ordinary ownership and borrow legality is decided by the deterministic checker in `05a-deterministic-ownership.md`. The general solver may consume certified ownership facts but does not decide basic borrow legality. Generic solver timeout/proof budgets therefore cannot cause ordinary borrow errors.
