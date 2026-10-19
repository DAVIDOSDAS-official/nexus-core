# First boot

`nexus-first-boot` runs once on a new machine, shows what it can be,
and installs what is chosen.

It is a wrapper, not a second brain. Every question it can answer is
answered by asking `nexus`: which profiles exist, what each costs on
this hardware, what is already satisfied. A first-boot experience that
knew those things itself would eventually disagree with the tool.

## Trying it without booting anything

```
NEXUS_BINARY=./build/cli/nexus \
NEXUS_SETUP_MARKER=/tmp/nexus-setup-marker \
    ./image/first-boot/nexus-first-boot
```

Delete the marker to run it again.

## Running once

The unit carries `ConditionPathExists=!/var/lib/nexus/setup-complete`,
so systemd skips it rather than starting a process that immediately
exits.

Since 0.1.51 it asks once, at the first start, whatever happens: a
choice, a skip, no answer within a minute (`NEXUS_SETUP_WAIT`, in
seconds), or no network. Before, it waited fifteen minutes and asked
again at every start until answered. Only a console that could not
ask at all, or went away mid-question, is asked again: nobody decided
anything there.

When nothing was chosen it leaves `/var/lib/nexus/setup-not-chosen`.
On desktops, `nexus-setup-reminder` (a user timer, a minute after
login) shows one notification per person: add-ons were not chosen,
choose with `nexus setup`, and a "Choose add-ons" button that opens
`choose-add-ons` in Konsole or QTerminal. Choosing there removes the
file.

## What is not handled

- **No graphical version.** This is a console prompt before the
  display manager. A graphical picker would be a separate program that
  must also ask `nexus` rather than deciding for itself.
- **No network check.** `After=network-online.target` asks systemd to
  wait; it does not guarantee anything is reachable. An install that
  fails for lack of network reports the failure and leaves the rest of
  the system alone.
- **No reboot.** Nothing installed here needs one.
