# Hardware as capabilities

Detected hardware becomes a `Component` providing capabilities, so a
profile can require `gpu-vendor-amd` and have the solver resolve it
exactly the way it resolves everything else. No special-case matching.

## What is read

Everything comes from sysfs. Nothing executes, probes, or writes.

| Source | Yields |
|---|---|
| `sys/class/drm/card*/device/{vendor,device,driver}` | graphics devices |
| `sys/firmware/efi` | UEFI vs BIOS |
| `sys/firmware/efi/efivars/SecureBoot-*` | Secure Boot state (fifth byte; the first four are attribute flags) |
| `sys/class/power_supply/BAT*` | laptop vs desktop |
| `sys/class/input/js*` | gamepad present |
| `proc/cpuinfo` | CPU model, for reporting only |

`card0-DP-1` and similar connector entries sit alongside cards and must
not be mistaken for them.

## Capabilities emitted

```
gpu-vendor-amd | gpu-vendor-intel | gpu-vendor-nvidia | gpu-vendor-virtual
gpu-driver-amdgpu | gpu-driver-i915 | gpu-driver-nvidia | ...
firmware-uefi | firmware-bios
secure-boot-enabled | secure-boot-disabled
chassis-laptop | chassis-desktop
gamepad-present
cpu-arch-amd64 | cpu-arch-i386 | cpu-arch-arm64 | ...
```

Hyphens, not colons — see decision 6.

Virtual display adapters (virtio, VMware, QEMU, Hyper-V) are named
`virtual` rather than `unknown`, because a virtual machine must not look
like a machine with no graphics at all.

## Facts and capabilities are kept apart

A fact is what the machine reports. A capability is what the resolver
matches against. Mixing them would make it impossible to tell a reading
from an interpretation.

Anything unreadable goes in `unreadable` with a reason and is printed by
`nexus hardware`. A machine with no readable GPU and a machine with no
GPU are different answers.

## Testing

`HardwareDetector` takes a **root path**. Real hardware cannot be
arranged on demand, so the tests build fake sysfs trees in a temporary
directory and point the detector at them.

`nexus hardware --sysfs <dir>` and `nexus profile check <name> --sysfs
<dir>` expose the same thing, which is how an NVIDIA machine can be
simulated without owning one.
