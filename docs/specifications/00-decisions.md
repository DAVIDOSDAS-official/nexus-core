# Decisions

Choices that are settled, and why. Written down because the reasoning
is not recoverable from the code, and because the version of this
project six months from now will be tempted to undo several of them.

---

## 1. Inherit or build

**Inherit anything that is a repository asset or kernel-adjacent
plumbing. Build anything that is selection logic, policy, or
explanation.**

Nexus's product is the decision layer, not the bits.

Build here:
capability model, version comparison, conflict model, source-aware
provider selection, the explanation engine, transaction planning and
preview, profile definitions, one configuration model shared by every
frontend.

Never build:
kernel, drivers, Mesa, libc, desktop environments, compilers, the
packages themselves, the sandbox, the snapshot mechanism, the package
transactions.

Reuse rather than reimplement:
Nix or filesystem snapshots for rollback, Flatpak and containers for
isolation, an existing base distribution.

The README already says Nexus should leverage the existing ecosystem.
This is that sentiment turned into a test that can be applied to a
specific proposal.

## 2. Binary packages from different distributions are not mixable

The goal — "take the good parts of each distro" — is right. Mixing
binary packages is not the mechanism, and no amount of engineering
makes it one.

File layouts differ (`/usr/lib/x86_64-linux-gnu` vs `/usr/lib64`).
libc builds are not interchangeable at the same upstream version.
The package databases do not know about each other, so files land
unowned and get overwritten. Maintainer scripts assume their own
distribution's users, groups, units, and alternatives.

What works instead: one base system, plus **isolated sources** for
everything else — Flatpak, distrobox containers, Nix, AppImage. A
container genuinely gives a user another distribution's packages,
running, because the base is insulated rather than mixed.

So the design is a **source-aware resolver**: a capability may be
satisfied from the base repository, a Flatpak, a container, or Nix,
and Nexus says which it chose, why, and what the trade-off is.

## 3. Choosing and analysing are different problems

The solver exists to **choose**: it backtracks, it can fail, and on a
large system it can explore itself into its own decision limit.

Analysing an already-installed system needs no choosing. Everything is
already installed and already coexisting. That work is a **reachability
closure**: linear, cannot fail, no limits.

`planRemoval` learned this the hard way. Written as a re-solve, it hit
the decision limit on a 1,056-root desktop and could answer nothing.
Rewritten as two closures, it is exact and instant.

Rule: if the question is "what would I install", solve. If the question
is "what is here and what holds it up", walk the graph.

## 4. Never silently substitute, never silently discard

A preference is a tie-breaker. A hard requirement is absolute: if it
cannot be honoured, the operation fails rather than quietly using
something else.

Anything the model cannot represent is **recorded**, not dropped. A
machine with no readable GPU and a machine with no GPU must not
produce the same output. A conditional preference that did not apply is
reported alongside those that did, because one that silently did not
apply is indistinguishable from one that was never written.

## 5. Be wrong in the safe direction

Where a judgement call is unavoidable, choose the error that is visible
and harmless over the one that is invisible and destructive.

- No version comparator available: assume a version condition holds, so
  a conflict is over-reported rather than missed.
- Several installed components satisfy a requirement: keep all of them.
  Something removable may survive; nothing in use is ever deleted.
- No record of what was auto-installed: treat everything as explicitly
  wanted, so nothing looks removable.
- The system does not resolve: propose nothing rather than a partial
  answer, because a partial answer here means deleting the wrong files.

## 6. Capability names use hyphens, not colons

`gpu-vendor-amd`, not `gpu-vendor:amd`.

Profile `Requires:` fields use Debian dependency grammar, where a colon
introduces an architecture qualifier. `gpu-vendor:amd` parses as the
package `gpu-vendor` at architecture `amd`, and silently never matches.

Names must be legal in the grammar they are written in.

## 7. Removal is a set difference, not reference counting

```
before = reachableFrom(roots)
after  = reachableFrom(roots minus target)
removed = before minus after
```

Two closures, not one. A single closure blames the removal for anything
that was *already* orphaned, which makes every removal report the same
collateral whatever is being removed — the tell being that removing two
unrelated packages produced identical output.

Roots are what the user asked for: installed, minus what apt recorded as
automatic in `/var/lib/apt/extended_states`.

## 8. Anything that scales with system size needs a test that builds the size

Three real bugs shipped past a full green suite because every test used
a handful of components while a real desktop has thousands:

- the search recursed once per resolved component and overflowed the
  stack;
- the conflict check copied every selected component on every candidate
  evaluation;
- the satisfaction check scanned the whole selection per requirement.

The development container was roughly a third the size of a real
desktop, which was enough to hide all three. Scale tests now construct
5,000 roots and a 20,000-deep chain — chosen to be past where any call
stack fails, so the test catches it on any machine rather than only on
a big enough one.

## 9. Print derived numbers, then disbelieve them

Every wrong answer in this project was found by computing a summary and
noticing it was implausible, never by a failing test:

- "35 of 35 selections involved a real choice" — the capability index
  was inserting each component several times under the same key.
- Removing `curl` and removing `git` reported identical collateral —
  the already-orphaned bug above.
- A Bulgarian dictionary satisfied `hunspell-en-us | ...` — architecture
  preference was being compared before alternative order.

Summaries are not decoration. They are a check on the data underneath.
