# Nexus-CORE security ledger

Every finding from every review, and what became of it. Last updated:
0.2.3, 17 November 2026.

**The rule:** an item leaves "open" only with evidence: a test that
fails if the fix is undone, a later review that re-ran the attack, or a
check on a real machine. An item may also be **accepted**, but only with
a written reason. Nothing is dropped from this file. When a patch
changes a status, it changes this file too.

Status words:

| Status | Meaning |
|---|---|
| **Closed** | Fixed. The evidence column says how it is known. |
| **Partly** | Some of it is fixed; what is left is said. |
| **Open** | Not fixed. |
| **Accepted** | Deliberately left as it is, for the reason given. |
| **Test needed** | Code is in place; it still has to be tried on real hardware or in a VM. |
| **Confirm** | Depends on a setting outside the repository (GitHub, the Asus); the maintainer has to look. |

Sources:

| Source | Version reviewed | Date |
|---|---|---|
| Round 1, adversarial security audit | 0.1.82 | 11 Nov 2026 |
| Round 2, adversarial security audit | 0.1.84 | 11 Nov 2026 |
| Round 3, security and privacy audit | 0.1.85 | 11 Nov 2026 |
| Round 4, red-team audit | 0.1.89 | 12 Nov 2026 |
| Round 5, red-team audit | 0.1.92 | 14 Nov 2026 |
| Full code review | 0.1.93 | 14 Nov 2026 |
| Fable's notes with patches 123/124 | 0.1.96 | 15 Nov 2026 |
| ChatGPT's production-readiness list | 0.1.97 | 15 Nov 2026 |
| Fable's notes with patches 129/130 | 0.1.99 | 15 Nov 2026 |

The reviews were done by AI models working from the source, with
proof-of-concept code run in their own sandboxes. They are not
independent human audits, and this file does not call them that.

---

## 1. What is open, in order

| # | Item | Why it matters | What closes it |
|---|---|---|---|
| 1 | **L1 / Rekor:** signatures are not in a public transparency log (`--tlog-upload=false` in `image/publish.sh`, `image/promote.sh`) | A stolen key plus password could sign an image with no public trace. Rekor makes such a signature visible; it does not stop it. | Step 1: sign with the log on. Step 2: machines require the log entry (`rekorPublicKeyPath` in the signature policy), only after every image in use has one, and only once Fedora 44's containers library is confirmed to support it. Plus someone, or a scheduled job, watching the log for signatures the maintainer did not make. |
| 2 | **R4-H1: Test needed** (0.2.2). The signing key's protection was a GitHub setting only | If the `release` environment lost its reviewer rule, a run would sign without asking. | **Done in code:** the first step of the `sign` job (`weekly-image.yml`) reads `release` from GitHub's API and stops unless it has a required reviewer (`tools/check-release-environment.py`), before anything touches the key. `tools/check-workflow-secrets.py` fails the tests if a `COSIGN_` secret is used in another job, outside a job, before that check, or with every secret handed over at once. Evidence: ctest `ReleaseGuard` (12 answers judged, the real workflows, 7 planted breaks caught). **Confirmed 16 Nov:** no repository-level secrets; `COSIGN_*` exist only in `release`. **17 Nov:** GitHub's real answer for `release` (required reviewer DAVIDOSDAS-official, custom branch policy, `can_admins_bypass: false`) passes the check. **Left:** the next signing run must pass the new step on GitHub (it has not run yet); then this is closed. What it cannot stop: someone who can push can remove the step; that is item 14 (account protection). |
| 3 | **R3:** root boot units run without systemd sandboxing | Defence in depth: if one gets a bug, nothing limits what it can touch. | `ProtectSystem=`, `ProtectHome=`, `PrivateTmp=`, `NoNewPrivileges=` per unit, tuned from `systemd-analyze security <unit>` on the Asus, each unit tested to still do its job. |
| 4 | **Shell execution: Partly** (0.2.1). 44 of 108 `runCommand` calls moved to `runProgram` with an argument vector, and the 6 `std::system` calls beside them: all of `system/src/plan_apply.cpp`, `cli/src/server.cpp`, `dualboot.cpp`, `update.cpp`, `language.cpp`. (The 109 counted here before included the line that defines `runCommand`.) **Left:** 64 `runCommand` and 20 `std::system` calls in 26 files, and 5 in the Shop. Of those, run as root with `--apply`: `rollback.cpp` (`rpm-ostree rollback`, `rebase`), `security.cpp` (`sysctl`), `install.cpp`, `setup.cpp`, `gaming.cpp`, `vpn.cpp`, `main.cpp`. Every read-only command still starts `sh`, as root when run under sudo (`security.cpp` 11, `doctor.cpp` 10, `services.cpp` 6 ...). `commandExists` runs `command -v` through `sh`. | One forgotten `shellQuote` is an injection. In the moved files there is no command line left to forget it in. Elsewhere the guard is still the quoting linter with exact counts per name (N8) and package-name checks (M4). | Move the rest in batches: next the files above that run as root with `--apply`, then `commandExists`, then the read-only commands. A file is done when it is in `MOVED` in `tools/check-shell-quoting.py`: there every `runCommand`, `std::system`, `popen` and `runProgram({"sh", ...})` fails ctest `ShellQuoting`. Evidence so far: `ProcessTest.OptionsPassEveryArgumentAsItIs`, `…OptionsSendErrorsWhereTheyAreAskedTo`, `…OptionsSayWhenAProgramCannotBeStarted`, `…HandedOverWorkTakesCtrlCItselfAndNexusLivesOn` and the other `ProcessTest.Options*`; `PlanApplyTest.LayeringCallsRpmOstreeWithEachNameAsOneArgument`, `…DnfAndAptAreCalledWithEachNameAsOneArgument`; ctest `ServerBackup`, `UpdateTrust`. |
| 5 | **R5-M3:** after the yes, rpm-ostree works out the packages again | What runs may differ from what was confirmed. Since 0.1.94 the result is compared and the history says "not as confirmed"; it is detected, not prevented. | Layer the exact versions confirmed (name-version-release) instead of names. On the Asus on 15 Nov the 56 packages added were exactly the 56 confirmed. |
| 6 | **L6 / base image by tag:** `FROM quay.io/fedora/fedora-bootc:44` | The digest is recorded after the build, not fixed before it. | Look the digest up once when the build starts, build every stage from `@sha256:…`, record it. |
| 7 | **L8:** server containers run rootful with default capabilities | A hole in a served app is a hole in a root container. | Rootless or user-namespaced quadlets, dropped capabilities, read-only where possible. |
| 12 | **Fedora 45:** the image is on Fedora 44 | Fedora 45 came out on 3 November 2026; Fedora 44 gets updates until about a month after Fedora 46 (around June 2027). | Move to 45 before spring 2027, tested on the Asus. |
| 13 | **Firewall, by Nexus itself: Closed** 0.2.2, T12 | `nexus server add`, `remove` and `cockpit --apply` ran `firewall-cmd --reload` after saving, dropping session-only rules. | Each change is now made in the saved rules and in the running ones (`firewallChanges()`), nothing reloaded; if the running firewall refuses, it is said and applies from its next start. Evidence: `SecurityAuditTest.NexussOwnFirewallChangesReloadNothing`; `tools/check-shell-quoting.py` fails on any `"--reload"` in Nexus's code (a planted one was caught). On real firewalld: T12. |
| 14 | **The maintainer's accounts** (from two outside threat reviews, 16 Nov) | Taking over the GitHub account, the email that resets its password, or the Acer reaches every Nexus machine: it is the one target worth an attacker's effort. | Two FIDO2 hardware keys on GitHub and on that email, SMS and app codes removed, recovery codes offline; a GitHub audit of sessions, SSH keys, apps and tokens; commits signed with the hardware key; branch protection on `main`; AI coding agents run without access to `~/.ssh` and the main login. Manual, not code. |
| 15 | **Kali toolbox shares the home folder: Partly** 0.2.3 (outside threat review, 16 Nov; verified in `cyber.cpp`) | A hostile or compromised tool in the toolbox can write `~/.bashrc` or `~/.config/autostart/` and run as the person at the next login. Distrobox's `--home`, the review's suggested fix, does not help: its manual says it does not prevent the mount of the host's home, and distrobox also mounts the whole host file system at `/run/host`. | **Done:** `nexus cyber ... --isolated`, a plain rootless podman container that sees only `~/.local/share/nexus-cyber-isolated/work` (`/work`), its own network namespace (pasta), `no-new-privileges`, no export. Tests `CyberTest.TheIsolatedBoxSeesOnlyItsOwnFolder` (no other mount, no host network, no devices, no privileged), `…IsEnteredInItsFolder`, `…StateIsReadFromPodman`. **Accepted:** the ordinary toolbox still shares the home folder (needed for GUI tools, export, raw sockets) and says so in `nexus cyber`. **Test needed:** a real create on the Asus. |
| 16 | **Kernel: ptrace and kexec open, lockdown off** (outside threat review; on the Asus 17 Nov: `ptrace_scope = 0`, lockdown `[none]`): **Closed** 0.2.3 as opt-in | With ptrace_scope 0 any program the person runs can read another's memory (the browser's). | `kernel.yama.ptrace_scope = 1` and `kernel.kexec_load_disabled = 1` joined `nexus security harden` (the maintainer's choice, 17 Nov), the cost said in the preview (`gdb -p`/`strace -p` need sudo). Lockdown is reported by `nexus security`, never set: it refuses unsigned drivers. Test `SecurityAuditTest.PtraceIsInTheTierWithItsCostUserNamespacesAreNot`. |

Items 8 to 11 were closed in 0.2.0 (patch 129) and keep their numbers:
8 is R4-L2 (section 5), 9 is I3 (section 2), 10 and 11 are in section 10.

Tests that need hardware or a VM, not code:

| # | Test | Where |
|---|---|---|
| T1 | Installer on many disk layouts, including interrupted runs | VM with throwaway virtual disks only, never a real disk |
| T2 | Dual boot with other distributions, EFI layouts, USB disks | VM first, then a spare machine |
| T3 | Power cut during an update and during layering | VM (kill the VM mid-way); rpm-ostree keeps the old deployment by design, this checks it |
| T4 | Network and DNS traffic of a fresh install, watched from outside | Asus, with a capture on the router or a second machine |
| T5 | First boot and the Shop by someone who has never used a terminal | Asus, a real person |
| T6 | Patch 123 checks 4/5 on the next update | Asus |
| T7 | `about:policies` and `ls -l /etc/firefox/policies/` (P1) | Asus |
| T8 | N7 boot check: no false "bad boot" on a normal start | Asus (has passed in daily use; not a deliberate test) |
| T9 | Server website from the phone (M3 of the server tests) | Blocked: the VPN on the Asus and phone stops it |
| T10 | Server backup interrupted with Ctrl-C | Asus |
| T11 | `nexus dualboot` with a USB stick and an SD card in: both said to be "not looked at", `--disk` brings one in. The tests use `lsblk` output written by hand, not recorded from such hardware; a card in a built-in slot is told from soldered storage by `/sys/block/<name>/device/type`. Also: `add`, with an entry in the menu for a system on a disk that is now left out, says it takes that entry out | Asus |
| T12 | The audit's firewall suggestion on a real firewalld: the pair runs to the end when the rule exists only in the running firewall, and only in the saved one | Asus |

---

## 2. Round 1 (0.1.82)

| ID | Finding | Status | Evidence |
|---|---|---|---|
| H1 | Signing key and password were plain repository secrets | **Partly** → R4-H1 | 0.1.86: signing moved to its own `sign` job with `environment: release` (`weekly-image.yml`); every run waits for the maintainer's approval. What is left is R4-H1. |
| M1 | WireGuard private key readable through `/proc/<pid>/cmdline` | **Closed** 0.1.84 | `runProgram({"wg","pubkey"}, key)`: key on stdin, no shell. Round 2: watcher caught 0 of 20 (was 20 of 20). |
| M2 | Other systems' EFI folder names written unescaped into GRUB | **Closed** 0.1.84 | Name whitelist and quoted `chainloader`. Test `Dualboot.AHostileLoaderNameNeverReachesGrub`; round 2 ran the old PoC and `grub-script-check`. |
| M3 | Root first-boot `chown` through user symlinks | **Closed** 0.1.84 | Per-user work runs as the user (`runuser`), no `chown`. Round 2 re-ran it. |
| M4 | Package names from repo metadata into a root shell | **Closed** 0.1.84 | `isPackageName` before any shell, names quoted. Test `PlanApplyTest.LayeringRefusesWhatIsNotAPackageName`; round 2 re-ran the PoC. |
| M5 | Advice for unsigned origins could not fix them | **Closed** 0.1.84 | `nexus rebase` moves unverified Nexus origins to signed ones. Test `RollbackTest.UnverifiedNexusOriginsMoveToSigned`. |
| M6 | ISO builder image `:latest`, privileged, unpinned | **Closed** 0.1.85 | `bootc-image-builder` and `skopeo` pinned by digest (round 3). |
| L1 | Update freeze undetectable; no Rekor | **Partly** | Going *back* is refused since 0.1.95 (tests `UpdateRollbackTest.*`). A frozen tag (no new build) still looks up to date, and there is no Rekor: open item 1. |
| L2 | Secure Boot status overstated | **Closed** 0.1.84 | Exact GUIDs, `MokSBStateRT`. Tests `SecureBootTest.*` (11). |
| L3 | Dual boot mounts other disks with default options | **Closed** 0.2.0, T11 | `nosuid,nodev,noexec`, 4 MiB read cap (0.1.84); no log replay (N4, 0.1.85). USB disks, SD cards and removable disks are not looked into unless named (0.2.0): see "USB disks in dual boot" in section 10. |
| L4 | Shop shows catalogue text as rich text | **Closed** 0.1.84 | `Text.PlainText` everywhere, https screenshots only. Test `CatalogueTest.DescriptionIsPlainTextWithListItems`. |
| L5 | Debian version compare hangs on a NUL byte | **Closed** 0.1.84 | Test `VersionTest.ANulByteDoesNotHang`; 20 min fuzzing in round 2. |
| L6 | Build inputs trusted by tag | **Partly** | Builder images by digest (M6); GitHub actions by full commit (`tools/pin-actions.py`). Base image by tag: open item 6. |
| L7 | Installer shrink could damage a hibernated system | **Closed** 0.1.85 | `noload`, `needs_recovery`, swap-partition and swap-file check (N6). Residual: a swap file at a path only the other system's fstab names; it fails safe when unreadable. |
| L8 | Servers run as rootful containers | **Open** | Open item 7. |
| I1 | Boot check fooled by `pgrep -f` | **Closed** 0.1.85 | See N7. |
| I2 | `github.actor` in `run:`; checkout kept credentials | **Closed** 0.1.84 | `env:` for the login; `persist-credentials: false` everywhere (round 2). |
| I3 | History mixes the user-writable log with root's | **Closed** 0.2.0 | Every record carries the log it was read from (`TransactionSource`); the file is never asked. `nexus history` and the Shop mark rows from the person's own log and never turn one into "applied"; `nexus doctor` counts interrupted changes from root's log only (before, without sudo, it read only the person's own). Text from a log is printed through `printable()`, so a row cannot hide its mark. Tests `TransactionLogTest.AForgedLineInTheOwnLogIsLabelledAndNeverTheMachines`, `…AnInterruptedChangeCountsOnlyFromTheMachinesLog`, `…AtTheSameTimeTheMachinesRecordComesFirst`; ctest `HistoryLabels` (`tests/history-labels-check.sh`, which fails on 0.1.99 on every point). The Shop's row was looked at offscreen (a screenshot), not by an automated test. |
| I4 | Uninitialised status in `ResolutionPlan` | **Closed** 0.1.84 | Defaults to Failed (round 2). |
| I5 | `nexus gaming` turns off split-lock mitigation silently | **Closed** 0.1.84 | The output explains the trade-off. |
| I6 | `rd.luks.options=tries=0` | **Accepted** | Harmless: Argon2id makes guessing slow either way (round 1's own words). |
| I7 | `nexus-boot-entry` removes other systems' stale NVRAM entries without `--apply` | **Closed** | Listed in `SECURITY.md` under the things that run by themselves: it removes only entries whose partition no longer holds that loader. |

## 3. Round 2 (0.1.84)

All checked fixed by round 3 (re-run PoCs for N1, N3, N5).

| ID | Finding | Status | Evidence |
|---|---|---|---|
| N1 | Unsigned image's version label shown unfiltered | **Closed** 0.1.85 | Label must match `^[0-9][0-9A-Za-z.+~-]{0,31}$`. Test `UpdateTest.AHostileLabelIsNotShown`. |
| N2 | Server backups world-readable | **Closed** 0.1.85 | Folder 0700, archives 0600, `umask(077)` around `tar`. |
| N3 | Escape sequences from other disks, Steam, repos printed to root terminals | **Closed** 0.1.85 | `printable()`. Test `ProcessTest.PrintableRemovesEveryControlCharacter`. |
| N4 | btrfs log replayed on a "read-only" mount | **Closed** 0.1.85 | `rescue=nologreplay`; `norecovery` for f2fs, bcachefs. |
| N5 | A FIFO in a home folder hangs first boot | **Closed** 0.1.85 | Plain-file check plus `timeout 10 runuser`; round 3: returns in 13 ms. |
| N6 | Hibernation into a swap file not seen | **Closed** 0.1.85 | See L7. |
| N7 | Boot check satisfied by any process named plasmashell | **Closed** 0.1.85 | Needs `display-manager.service` active and `sddm-greeter` owned by `sddm` or a logind graphical session. T8. |
| N8 | Quoting checker allow-list by name only | **Closed** 0.1.85 | Keyed by (file, name) with exact counts. Since 0.1.98 an allowance larger than its use also fails. |

## 4. Round 3 (0.1.85)

Round 4 confirmed every item below is in the code and checked on each image build.

| ID | Finding | Status | Evidence |
|---|---|---|---|
| P1 | Firefox runs with Fedora's telemetry, studies and ad measurement | **Closed** 0.1.86, T7 | `/etc/firefox/policies/policies.json` in the image; `tools/check-network-hygiene.py` checks it in every test run. |
| P2 | Random hostname sent in DHCP to every network | **Closed** 0.1.86 | `image/network/30-nexus-privacy.conf`: `dhcp-send-hostname=false`, checked in the built image by `image/check-add-ons.sh`; on the Asus: false. |
| P3 | Fedora Count Me on | **Closed** 0.1.86 | Timer off, `countme=0`; on the Asus `rpm-ostree-countme.timer` is not found. |
| P4 | No IPv6 temporary addresses | **Closed** 0.1.86 | `ip6-privacy=2` in the same file, checked in the built image by `image/check-add-ons.sh`. |
| P5 | Daily update check with no off switch | **Closed** 0.1.86 | `nexus update notify off --apply`. Test `UpdateTest.NotifySwitchWritesAndRemovesTheFlag`. |
| P6 | gamecheck's `curl` followed redirects to http | **Closed** 0.1.86 | `--proto =https --proto-redir =https`; `tools/check-network-hygiene.py` fails on any `curl` without them. |
| R1 | Flatpak version strings unfiltered | **Closed** 0.1.86 | Test `UpdateTest.FlatpakVersionIsPrintable`. |
| R2 | Direction-changing and zero-width characters | **Closed** 0.1.86, widened 0.1.99 | Tests `ProcessTest.PrintableReplacesBidiControls`, `…ZeroWidthCharacters`, `…TheRestOfTheInvisibleCharacters`. |
| R3 | Root boot units unsandboxed | **Open** | Open item 3. |

## 5. Round 4 (0.1.89)

| ID | Finding | Status | Evidence |
|---|---|---|---|
| R4-H1 | Key protection rests on GitHub settings | **Test needed** 0.2.2 | Open item 2: the signing job now checks the reviewer rule itself and the lint keeps the key in that job; closed after the next signing run passes it. Repository secrets: none (16 Nov). |
| R4-M1 | `nexus remove` had no package-manager check | **Closed** 0.1.91 | dnf/apt dry run first; the count typed is dnf's. Tests `PlanCheckTest.ARemovalTakesWhatDnfSaysNotWhatNexusPlanned`, `…ARemovalThatInstallsIsRefused`; round 5: fixed. On the image (0.1.93): `PlanCheckTest.OnTheImageWhatGoesIsWhatIsLayeredAndNotKept`. |
| R4-M2 | Install check blind to removals, downgrades, replacements | **Closed** 0.1.91, 0.1.98 | Every section read. Test `PlanCheckTest.EveryKindOfChangeInADnf5TableIsRead`; unknown headings since 0.1.98 (R5-L3). dnf5 format checked on the Asus. |
| R4-M3 | dnf checks, rpm-ostree runs | **Partly** → R5-M3 | rpm-ostree asked first since 0.1.91–0.1.92; Asus 0.1.92: 56 packages, 44 not in dnf's list, shown. |
| R4-M4 | `nexus setup` approved once, installed with `--yes` | **Closed** 0.1.91 | Each item asks with its own list. Round 5: fixed. Flatpaks too since 0.1.98. |
| R4-M5 | Old signed images stay valid; going back possible | **Closed** 0.1.95–0.1.96 | Version and build-time floor; the confirmed build digest is checked after staging, else `cleanup --pending`. Tests `UpdateRollbackTest.*` (11), `UpdateStagedTest.*` (5); `tests/update-trust-check.sh`. Asus checks 1–3 passed; T6. |
| R4-L1 | doctor, summary and security said "signed" differently | **Closed** 0.1.95 | One function for all three (Fable, patch 123). |
| R4-L2 | "Starts by itself" looks in two places | **Closed** 0.2.0 | `findAutoStarts`: autostart, autostart-scripts, plasma-workspace/env and shutdown, `.service` `.timer` `.socket` `.path` units with their `*.d` drop-ins and `*.wants` links in both user unit folders, environment.d; shell and login files named, not judged. Names only, at most 500 a folder, and a folder that is a link out of the home is not read. Every run ends with what was not looked at (the whole system's services and autostart, cron and at, other accounts). Tests `SecurityAuditTest.StartsByItselfLooksInEveryPlace`, `…ALinkLeadingOutOfTheHomeIsNotFollowed`, `…AnEmptyHomeStartsNothingAndSaysWhatWasNotLookedAt`, `…ShellFilesAreNamedNotJudged`, `…AFolderWithTooManyNamesIsReadInPartAndSaid`, `…AFileNameCannotSteerTheTerminal`. Not looked at, and said in the notes of patch 129: a config folder moved with `XDG_CONFIG_HOME`, `ZDOTDIR`. |
| R4-L3 | More invisible characters pass `printable()` | **Closed** 0.1.99 | Soft hyphen, U+2028/2029, Hangul fillers, U+180E, tag characters. Test `ProcessTest.PrintableReplacesTheRestOfTheInvisibleCharacters`. |
| R4-L4 | Flatpak permissions granted without being shown | **Closed** 0.1.97, 0.1.98 | Shown before install in `nexus install`, the Shop and `nexus setup`; broad ones need a typed yes. Tests `FlatpakPermissionsTest.*`, fuzz target. Asus: VLC and GeoGebra lists read correctly. |
| R4-L5 | Transaction log readable by every account | **Closed** 0.1.91 | 0640, group wheel; only ever narrowed. Test `TransactionLogTest.FinishingKeepsPermissionsAndLeavesNoScraps`; round 5: fixed. |
| R4-L6 | Float-to-integer overflow on absurd sizes and uptimes | **Closed** 0.1.99 | Clamped before the cast. Tests `FlatpakTest.AnAbsurdSizeIsUnknownNotUndefined`, `SpecsTest.AnAbsurdUptimeIsZeroNotUndefined`. |
| R4-L7 | Write permissions at workflow level | **Closed** 0.1.99 | `permissions: {}` at the top of `fedora-version-check.yml`, `security-watch.yml`, `installer-iso.yml`; each job names its own. |
| R4-L8 | Network checker scanned `shop/`, the Shop lives in `gui/shop/` | **Closed** 0.1.99 | `tools/check-network-hygiene.py` scans `gui`. |
| R4-L9 | Version shown came from an unchecked inspect; the upgrade pulled whatever the tag said later | **Closed** 0.1.95 | The confirmed digest is pinned and compared with what was staged (see R4-M5). |

## 6. Round 5 (0.1.92)

| ID | Finding | Status | Evidence |
|---|---|---|---|
| R5-M1 | New 32-bit packages hidden as "layered earlier" | **Closed** 0.1.94 | Name and architecture throughout. Tests `PlanCheckTest.DnfRowsKeepTheirArchitecture`, `…OnTheImageA32BitLayeredPackageIsShownAsGoing`. |
| R5-M2 | An unrecognised rpm-ostree answer read as "nothing to add, agrees" | **Closed** 0.1.94 | Refused. Test `PlanCheckTest.AnRpmOstreeAnswerWithNoListIsRefusedNotAgreed`. |
| R5-M3 | Re-resolved after the prompt; nothing compared afterwards | **Partly** | Since 0.1.94 the installed set is compared and the history says "not as confirmed". Prevention: open item 5. |
| R5-L1 | Hardening checker passed a program with only variable-size frames | **Closed** 0.1.94 | Those frames are judged; "nothing to judge" is not "verified". `tests/check-hardening-selftest.sh`. |
| R5-L2 | No-apply test excused files the trace did not name | **Closed** 0.1.94 | Excused only if the change does not repeat on a second run; io_uring traced. `tests/no-apply-trace-rules.sh`. |
| R5-L3 | Unknown dnf headings skipped; replacements matched by name only | **Closed** 0.1.98 | Tests `PlanCheckTest.ARowUnderAnUnknownHeadingIsAskedAboutNotSkipped`, `…AnUnknownHeadingInAnInstallPlanNeedsATypedYes`, `…AReplacedLibraryOfAnotherArchitectureIsNotTheUpgradesOwn`. |
| R5-L4 | "May move to newer versions" named nothing | **Closed** 0.1.98 | Versions compared, each move listed, going back needs a typed yes. Test `PlanCheckTest.PackagesLayeredEarlierAreListedWithTheirVersions`. Asus: "137 keep their version". |
| R5-L5 | Enter was yes even when the list was longer than the plan | **Closed** 0.1.98 | `[y/N]` then. Test `PlanCheckTest.EnterIsNoWhenTheListIsLongerThanNexussOwnPlan`. Asus: prompt shown. |

## 7. Full code review (0.1.93)

| Point | Status | Evidence or reason |
|---|---|---|
| Bus factor of one; no outside verification | **Accepted** | True of a one-person project. Answered by publishing everything (code, reviews, this file) so others can check, not by claims. |
| No Rekor | **Open** | Open item 1. |
| Fedora 44 base ageing | **Open** | Open item 12. |
| `main.cpp` monolith | **Closed** 0.1.96 | Split into `cli/src/*.cpp` (patch 124); every command's output proven identical with `tools/compare-outputs.sh`. |
| Duplicate `#include <vector>` in `plan_apply.hpp` | **Closed** 0.1.99 | Removed. |
| `NEXUS_SHOP_*` variables in the release Shop | **Accepted** | They change which catalogue folders the Shop reads, whether it animates or refreshes, and (only with `NEXUS_SHOP_SCREENSHOT` set) open a page, save one screenshot and quit. `NEXUS_SHOP_CONFIRM` only opens the confirm dialog; it installs nothing. Same account, no privilege. They make the Shop testable without a screen. |
| Grep-based checks in shell scripts | **Partly** | Quoting linter (N8); `shellcheck` clean in round 4. Same direction as open item 4. |

## 8. Fable's notes (0.1.96)

| # | Note | Status | Evidence or reason |
|---|---|---|---|
| 1 | Linter allowed `unit` 3 times, used once | **Closed** 0.1.98 | Allowance 1; an unused allowance now fails. |
| 2 | `dev.cpp` ignored `std::system` three times | **Closed** 0.1.98 | Failures are said; no compiler warning. |
| 3 | Comment at the top of `install.cpp` described other code | **Closed** 0.1.98 | Moved to the code it describes. |
| 4 | Two globals visible in `cli.hpp` | **Accepted** | Set once by `main()` before any command runs; read-only after. Worth tidying, not a risk. |
| 5 | `ostree-remote-image:` refs called "not signature-checked" | **Accepted** | Those are checked by ostree's own keys; no Nexus image uses them. Wording only. |
| 6 | `HANDOFF.md` head said 0.1.85 | **Closed** 0.1.98 | Updated. |
| 7 | Fuzz seed for `parseSystemUpdate` was JSON | **Closed** 0.1.95 | Text seed added in patch 123. |

## 9. ChatGPT's production-readiness list (15 Nov 2026)

| Priority given | Item | Where it is in this file |
|---|---|---|
| Critical | Rekor | Open item 1. Rated **High** here: it detects a stolen-key signature, it does not prevent one; key custody (R4-H1) matters more. |
| Critical | Independent systemd sandboxing review | Open item 3 (R3). |
| High | Destructive-disk tests of the installer | T1, VM only. |
| High | Permission analysis on every install path | Flatpak: closed (R4-L4). rpm packages have no permission model to analyse. Server containers: open item 7. |
| High | Every external image and dependency by digest | Actions and builder images: closed. Base image: open item 6. |
| High | Dual boot across distributions, EFI layouts, USB disks | USB disks: closed 0.2.0 (section 10), T11. Other distributions and EFI layouts: T2. |
| Medium | Interrupted updates, power failures | T3. |
| Medium | Network and DNS behaviour verified independently | T4. |
| Medium | First boot and Shop without terminal knowledge | T5. |
| — | Shell execution to argument vectors | Open item 4: partly done in 0.2.1 (44 of 108, the five files that ran most as root). |
| — | Items must close with evidence, not vanish | This file. |

## 10. Other items carried between rounds

| Item | Status | Evidence |
|---|---|---|
| `runProgram`: deadlock with over 64 KiB of input; process-wide SIGPIPE swap | **Closed** | Output read while input is written (`poll`); SIGPIPE blocked per thread (`system/src/process.cpp`). Round 4: holds up under the sanitizers. |
| `firewall-cmd --reload` in suggestions (was open item 11) | **Closed** 0.2.0, T12 | Each suggested change is given twice, saved and running (`--permanent ... && ...` without it), and nothing is reloaded; the one reload that is meant says what it drops and how to keep it instead (`--runtime-to-permanent`). Tests `SecurityAuditTest.FirewallFixesChangeSavedAndRunningAndReloadNothing`, `…TheOneReloadSaysWhatItDrops`. What Nexus runs itself is open item 13. |
| USB disks in dual boot (was open item 10) | **Closed** 0.2.0, T11 | `lsblk` is asked for RM and TRAN; a disk that is USB, removable, or an SD card is not looked into and nothing on it reaches the boot menu, unless named with `--disk <path>` or it is the disk Nexus runs from. What was left out is said, with the command that includes it; a `lsblk` that gives no RM is said too. Tests `Dualboot.AUsbDiskIsNotLookedAtUnlessNamed`, `…SkippedDisksAreSaidWithHowToIncludeThem`, `…RemovableIsReadHoweverLsblkWritesIt`, `…AMissingRemovableFieldIsBuiltInAndSaid`, `…TheDiskThisSystemRunsFromIsAlwaysLookedAt`, `…ACardInABuiltInSlotIsLeftOutByWhatTheKernelCallsIt`; fuzz target `parseLsblk`. |

## 11. Fable's notes with patches 129/130 (0.1.99)

| # | Note | Status | Evidence or reason |
|---|---|---|---|
| 1 | `nexus server add`, `remove` and `cockpit --apply` run `firewall-cmd --reload` themselves | **Open** | Open item 13. |
| 2 | `nexus history` and `nexus doctor` printed log text as it was: escape sequences in a person's own log reached the terminal, a root terminal under `sudo -E` | **Closed** 0.2.0 | Through `printable()`. ctest `HistoryLabels` looks for an escape character in what both print. |
| 3 | "Starts by itself" showed the first six names and "..." | **Closed** 0.2.0 | Up to 40 are shown and the rest counted ("... and N more"). Test `SecurityAuditTest.AFolderWithTooManyNamesIsReadInPartAndSaid`. |
| 4 | `nexus dualboot add` rewrites Nexus's part of the boot menu from what it finds; an entry whose system is no longer found went without a word | **Test needed** (T11) | Since 0.2.0 the plan says "Take ... out of the boot menu" for each. No automated test: it is in the CLI, behind `lsblk` and a mount. |
| 5 | `nexus language set --apply` changes the system language and writes nothing to `nexus history` | **Open** | Every change is meant to be in the history (`CONTRIBUTING.md`). Not a security hole; listed so it is not lost. |
| 6 | `runProgram` gave exit code 0 for a program that could not be started (`ran` was false, `exitCode` still 0) | **Closed** 0.2.1 | 127 when it is not there, 126 when it cannot be started, as a shell answers. Nothing read it before 0.2.1 (the one caller looked at `ok`); the moved calls do. Test `ProcessTest.OptionsSayWhenAProgramCannotBeStarted`. |
| 7 | With `--apply`, a package tool that cannot be started is now reported in Nexus's own words ("rpm-ostree: no such program"; for `rpm-ostree` the result is "unavailable" where it was "failed") where the shell's "not found" line and exit 127 used to be | **Accepted** | The one place where 0.2.1 answers differently from 0.2.0, and only when `rpm-ostree`, `dnf` or `apt-get` is missing after Nexus has just seen it. Nothing is installed either way. |
