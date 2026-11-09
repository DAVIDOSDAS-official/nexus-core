#pragma once

#include <optional>
#include <string>
#include <vector>

namespace nexus::system {

// `nexus gaming` (0.1.74): the fixes with evidence behind them, each
// one shown, switched on only with --apply, and undone with `off`.
//
// NTSync (0.1.5x) and, new, the Intel split-lock penalty: since Linux
// 6.2 the kernel deliberately slows a program that does split locks
// (kernel.split_lock_mitigate=1), which crippled some Windows games
// under Proton (God of War). 0 keeps the warning in the log and drops
// the slowdown. The slowdown exists so one program cannot stall a
// shared machine -- it matters on multi-user servers, not on a gaming
// laptop.
//
// The pure part: what to do, given what the machine has.

inline constexpr const char* kSplitLockConf = "/etc/sysctl.d/90-nexus-split-lock.conf";

struct GamingState {
    bool ntsyncModule = false;   // the kernel has it
    bool ntsyncDevice = false;   // /dev/ntsync now
    bool ntsyncAtStart = false;  // loaded at every start
    std::optional<int> splitLock;  // kernel.split_lock_mitigate; empty: CPU/kernel without it
    bool splitLockConf = false;  // Nexus's sysctl file is there
};

enum class GamingFix { NtsyncNow, NtsyncAtStart, SplitLockOff };

// What --apply would do. `only`: "", "ntsync" or "split-lock".
std::vector<GamingFix> gamingPlan(const GamingState& s, const std::string& only);

// What `off --apply` would undo (Nexus's own files only).
struct GamingUndo { bool ntsyncConf = false; bool splitLockConf = false; };
GamingUndo gamingUndoPlan(bool ntsyncConf, bool splitLockConf);

// "0" / "1" from /proc/sys/kernel/split_lock_mitigate
std::optional<int> parseSysctlInt(const std::string& text);

}  // namespace nexus::system
