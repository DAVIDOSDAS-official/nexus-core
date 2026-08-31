# Profile files

A profile is a named intent: "I want this machine for gaming."

It is not a package list. It is a set of capability requirements plus
preferences, resolved against whatever is actually available. That is
what lets one profile work on different machines without being
rewritten.

Profiles live in `components/profiles/*.profile` and are plain data.
Anyone can add one without touching C++.

## Format

The same control-file format the rest of the system layer reads, so
there is one format to learn and one parser to trust.

```
Profile: gaming
Description: Steam, graphics drivers and the 32-bit libraries games need
Architecture: amd64
Enables-Architectures: i386
Requires: steam-installer | steam,
 mesa-vulkan-drivers | nvidia-driver-libs,
 libgl1,
 pipewire | pulseaudio,
 gamemode,
 gpu-vendor-amd | gpu-vendor-intel | gpu-vendor-nvidia
Prefers: audio=pipewire
Prefers-When: gpu-vendor-amd -> mesa-vulkan-drivers,
 gpu-vendor-intel -> mesa-vulkan-drivers,
 gpu-vendor-nvidia -> nvidia-driver-libs
Requires-Exactly: java=openjdk-21-jre
```

| Field | Meaning |
|---|---|
| `Profile` | Name. Required. |
| `Description` | One line, shown in `nexus profile list`. |
| `Architecture` | Resolve for this architecture. Empty means native. |
| `Enables-Architectures` | Additional architectures the profile needs, such as i386 for 32-bit game libraries. |
| `Requires` | Debian `Depends` grammar: comma-separated clauses, `\|` for alternatives, `(>= 1.2)` for version conditions. |
| `Prefers` | `capability=component`. A tie-breaker only. |
| `Prefers-When` | `capability -> component`. Applied only when the condition capability is present. |
| `Requires-Exactly` | `capability=component`. Absolute; the solve fails rather than substituting. |

Continuation lines start with a space, as in any control file.

## Notes

**`Requires` uses the full dependency grammar**, so alternatives and
version conditions work exactly as they do in package metadata. No
separate syntax to learn.

**Conditions are just capabilities.** `Prefers-When` checks its
condition the same way every other capability is checked — there is no
special matching logic. Hardware capabilities (below) are the usual
source, but anything that can be provided works.

**A requirement that cannot fail is not telling anyone anything.**
`gpu-vendor-amd | gpu-vendor-intel | gpu-vendor-nvidia` passes on
essentially every machine; its real job is being the condition for the
preferences. Worth remembering when writing new profiles.

**Malformed entries are reported, not ignored.** A `Prefers` entry
without `=`, or a `Prefers-When` without `->`, produces a warning and
is skipped; the rest of the profile still loads.

## minimal is not minimalism

Two different words for two different goals, and conflating them
produces a profile that serves neither.

**minimal** is the least that still works: a machine with little to
spare, a lightweight session, no compositing, one of everything and
nothing else. It is measured in megabytes, and it exists for hardware
that cannot afford anything more.

**minimalism** is a full desktop kept simple: proper session, good
defaults, one tool per job and no duplicates. It is not small -- it
may well be larger than a careless install -- and it exists for people
who want a system that feels calm rather than one that is cheap to
run.

A profile named for the second while described as the first will
disappoint everybody who picks it.

## The generated list exposes what a profile forgot

Building the desktop image from a hand-written package list, then from
the `minimalism` profile, produced different sets. The hand-written one
quietly included a network applet, a volume applet and a desktop
portal. The profile asked for none of them, because "connect to wifi"
and "file dialogs work inside sandboxed apps" had not been written down
as capabilities.

The generated list was right and the profile was incomplete. That is
the useful direction for the disagreement to run: a hand-written list
hides an omission by silently including things nobody asked for, while
a generated one makes the omission visible as a missing package.

## Checking

`nexus profile check <name>` resolves each requirement **separately**.

Solving the whole profile at once answers "does all of this work
together", which is one bit. Checking requirement by requirement
answers "what exactly is missing", which is what somebody setting up a
machine actually needs. One failure never masks the state of the rest.

With `--with-available`, a missing requirement is re-checked against
the archive, so the report says "steam-installer is available and would
bring 294 components" rather than only "steam is missing".
