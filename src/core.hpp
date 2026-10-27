#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <vector>
#include "../generated/lakes.hpp"

namespace mounts {

enum Action {
    Raptor, Springer, Skimmer, Jackal, Griffon, Roller, Warclaw, Skyscale,
    Turtle, Unmount, Skiff, Fishing, JadeWaypoint, Rift, SkyscaleLeap,
    Doorway, Chair, Music, Held, Toy, Tonic, Count
};
inline constexpr int ActionCount = Count;
inline constexpr std::array<const char*, ActionCount> actionNames = {{
    "Raptor", "Springer", "Skimmer", "Jackal", "Griffon", "Roller Beetle",
    "Warclaw", "Skyscale", "Siege Turtle", "Unmount", "Skiff", "Fishing",
    "Jade Bot Waypoint", "Scan for Rift", "Skyscale Leap", "Summon Conjured Doorway",
    "Chair", "Music", "Held Item", "Toy", "Tonic"
}};
inline constexpr std::array<const char*, ActionCount> iconStems = {{
    "raptor", "springer", "skimmer", "jackal", "griffon", "roller", "warclaw",
    "skyscale", "turtle", "unmount", "skiff", "fishing", "jadebotwaypoint",
    "scanforrift", "skyscaleleap", "summonconjureddoorway", "chair", "music", "held", "toy", "tonic"
}};
// Mumble EMountIndex values, not the action-list order. Zero means not a mount.
inline constexpr std::array<int, ActionCount> mountIndex = {{
    5, 3, 4, 1, 2, 6, 7, 8, 10, 0, 9, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0
}};
inline constexpr std::array<int, ActionCount> gameBinds = {{
    155, 156, 157, 158, 159, 161, 169, 170, 203, 152, 205, 204,
    206, 207, 208, 211, 163, 164, 165, 166, 167
}};

inline bool validAction(int action) { return action >= 0 && action < ActionCount; }
inline int actionForMount(int mount) {
    if (mount <= 0) return -1;
    for (int i = 0; i < ActionCount; ++i) if (mountIndex[i] == mount) return i;
    return -1;
}

struct Snapshot {
    bool valid = false, gameplay = false, focused = false, mapOpen = false;
    bool textbox = false, combat = false, competitive = false, falling = false;
    std::uint32_t mapId = 0, mapType = 0;
    int mount = 0;
    std::string character;
    // Blish coordinate convention: x/y horizontal, z vertical (metres).
    float x = 0, y = 0, z = 0;
};

struct RadialProfile {
    std::string name;
    bool enabled = true;
    std::vector<int> actions;
    int centerBehavior = 0; // 0 none, 1 default, 2 last used
    int defaultAction = -1;
    bool removeCenter = true, instantSingle = false;
    int tapAction = -1;
    bool unconditional = false;
};

enum Context { Mounted, Wvw, Combat, Underwater, Surface, Falling, Default, ContextCount };
inline constexpr std::array<const char*, ContextCount> contextNames = {{
    "Mounted", "WvW", "Combat", "Underwater", "Water surface", "Gliding or falling", "Default"
}};

inline bool isWvw(const Snapshot& s) {
    // Gw2Sharp Center is an alias of EternalBattlegrounds (9), not ObsidianSanctum (14).
    return s.mapType == 9 || s.mapType == 10 || s.mapType == 11 ||
           s.mapType == 12 || s.mapType == 18;
}

inline float waterSurface(const Snapshot& s) {
    // Preserve the original nearest-nearby-lake rule, including its default sea level.
    const generated::Lake* nearest = nullptr;
    float nearestDistance = std::numeric_limits<float>::infinity();
    for (const auto& lake : generated::lakes) {
        if (lake.map != s.mapId) continue;
        float cx = 0, cy = 0, cz = 0;
        for (std::size_t i = 0; i < lake.count; ++i) {
            cx += lake.bounds[i].x; cy += lake.bounds[i].y; cz += lake.bounds[i].z;
        }
        cx /= lake.count; cy /= lake.count; cz /= lake.count;
        float radius2 = 0;
        for (std::size_t i = 0; i < lake.count; ++i) {
            const auto& p = lake.bounds[i];
            const float dx = p.x - cx, dy = p.y - cy, dz = p.z - cz;
            radius2 = std::max(radius2, dx * dx + dy * dy + dz * dz);
        }
        const float dx = s.x - cx, dy = s.y - cy, dz = s.z - cz;
        const float distance2 = dx * dx + dy * dy + dz * dz;
        if (distance2 <= radius2 * 2.25f && distance2 < nearestDistance) {
            nearest = &lake; nearestDistance = distance2;
        }
    }
    if (!nearest || s.z < nearest->bottom || s.z > nearest->surface + 10) return 0;
    bool inside = false;
    for (std::size_t i = 0, j = nearest->count - 1; i < nearest->count; j = i++) {
        const auto& a = nearest->bounds[i]; const auto& b = nearest->bounds[j];
        if ((a.y > s.y) != (b.y > s.y)) {
            const float intersection = a.x + (s.y - a.y) / (b.y - a.y) * (b.x - a.x);
            if (s.x < intersection) inside = !inside;
        }
    }
    return inside ? nearest->surface : 0;
}
inline bool underwater(const Snapshot& s) { return s.z <= waterSurface(s) - 1.2f; }
inline bool onSurface(const Snapshot& s) {
    const float surface = waterSurface(s);
    return s.z > surface - 1.2f && s.z < surface;
}
inline bool contextApplicable(int context, const Snapshot& s) {
    switch (context) {
        case Mounted: return s.mount != 0;
        case Wvw: return isWvw(s);
        case Combat: return s.combat;
        case Underwater: return underwater(s);
        case Surface: return onSurface(s);
        case Falling: return s.falling;
        case Default: return true;
        default: return false;
    }
}
inline int contextIndex(const Snapshot& s) {
    for (int i = 0; i < ContextCount; ++i) if (contextApplicable(i, s)) return i;
    return Default;
}
inline bool usableInCombat(int action, bool combatLaunch) {
    return action == Unmount || action == Warclaw || action == Skimmer ||
           ((action == Skyscale || action == SkyscaleLeap) && combatLaunch);
}
inline bool isGroundTargeted(int action) { return action == Skiff || action == Doorway; }
inline bool delayedGroundTarget(int action, const Snapshot& s) {
    return action == Doorway || (action == Skiff && !onSurface(s));
}

// Screen coordinates (positive y downward), initial angle is the first sector's boundary.
// -1 denotes the center/deadzone, and never denotes a ring action.
inline int chooseSector(float dx, float dy, int count, float startDegrees, float deadzone) {
    if (count <= 0 || !std::isfinite(dx) || !std::isfinite(dy) ||
        !std::isfinite(startDegrees) || !std::isfinite(deadzone) ||
        dx * dx + dy * dy < std::max(0.0f, deadzone) * std::max(0.0f, deadzone)) return -1;
    constexpr double pi = 3.14159265358979323846;
    double angle = std::atan2(static_cast<double>(dy), static_cast<double>(dx)) - startDegrees * pi / 180;
    angle = std::fmod(angle, 2 * pi);
    if (angle < 0) angle += 2 * pi;
    return std::min(count - 1, static_cast<int>(angle / (2 * pi / count)));
}

class FallingDetector {
    bool initialized_ = false, falling_ = false;
    float z_ = 0;
    double seconds_ = 0, jumpSeconds_ = -1e30;
public:
    void reset(float z, double seconds) {
        initialized_ = true; falling_ = false; z_ = z; seconds_ = seconds; jumpSeconds_ = -1e30;
    }
    void jump(double seconds, bool mounted = false) { if (!mounted) jumpSeconds_ = seconds; }
    bool sample(float z, double seconds, double frequency = 0.1) {
        if (!std::isfinite(z) || !std::isfinite(seconds)) return falling_;
        if (!initialized_ || seconds < seconds_) { reset(z, seconds); return false; }
        const double elapsed = seconds - seconds_;
        if (elapsed <= 0 || elapsed + 1e-9 < std::max(0.001, frequency)) return falling_;
        const double velocity = (z - z_) / elapsed;
        falling_ = velocity > 10 || velocity < -10 ||
                   (seconds >= jumpSeconds_ && seconds - jumpSeconds_ < 5 && velocity < -2);
        z_ = z; seconds_ = seconds;
        return falling_;
    }
};

struct EngineOptions { bool queueEnabled = false, combatLaunch = false, autoRemount = false; };

// Policy only: returning an action authorizes the adapter to invoke one configured gamebind.
// No timers, threads, keyboard injection, or ownership detection are hidden here.
class Engine {
    Snapshot previous_;
    bool seen_ = false;
    std::string queuedCharacter_, pendingCharacter_;
    std::uint32_t queuedMap_ = 0;
    bool pendingAutomatic_ = false;

    void clearQueue() { queued = -1; queuedCharacter_.clear(); queuedMap_ = 0; }
    void clearPending() { pending = -1; pendingCharacter_.clear(); pendingAutomatic_ = false; }
public:
    int queued = -1, pending = -1, lastUsed = -1;

    static bool canActivate(const Snapshot& s) {
        return s.valid && s.gameplay && s.focused && !s.mapOpen && !s.textbox;
    }
    void cancel() { clearQueue(); clearPending(); }

    int request(int action, const Snapshot& s, const EngineOptions& opts, bool unconditional = false) {
        if (!validAction(action)) return -1;
        if (!s.focused || s.textbox || s.mapOpen) { cancel(); return -1; }
        // Every deliberate request replaces older automatic or queued intent.
        clearQueue(); clearPending();
        if (!s.valid || !s.gameplay) {
            // A deliberate loading-screen selection may wait for the same known character.
            if (!s.character.empty()) { pending = action; pendingCharacter_ = s.character; }
            return -1;
        }
        if (!unconditional && s.combat && opts.queueEnabled && !usableInCombat(action, opts.combatLaunch) &&
            !s.competitive && !isWvw(s)) {
            // There must be a known identity and map to bind a delayed action to.
            if (!s.character.empty() && s.mapId != 0) {
                queued = action; queuedCharacter_ = s.character; queuedMap_ = s.mapId;
            }
            return -1;
        }
        lastUsed = action;
        return action;
    }

    int update(const Snapshot& s, const EngineOptions& opts) {
        int result = -1;
        if (!s.focused || s.textbox || s.mapOpen) {
            cancel(); previous_ = s; seen_ = true; return -1;
        }
        // A combat queue belongs only to the current active character and map. Loading,
        // missing identity, changed settings and competitive maps invalidate that intent.
        if (!s.valid || !s.gameplay || s.character.empty() || s.character != queuedCharacter_ ||
            s.mapId != queuedMap_ || s.competitive || isWvw(s) || !opts.queueEnabled) clearQueue();
        if (seen_) {
            if (opts.autoRemount && previous_.valid && previous_.gameplay && !s.gameplay && previous_.focused &&
                previous_.mount != 0 && !previous_.character.empty()) {
                pending = actionForMount(previous_.mount); pendingCharacter_ = previous_.character;
                pendingAutomatic_ = true;
            }
        }
        if ((!s.character.empty() && s.character != pendingCharacter_) ||
            (pendingAutomatic_ && !opts.autoRemount)) clearPending();
        if (canActivate(s) && !s.combat && queued >= 0) {
            if (!s.mount && queuedCharacter_ == s.character) result = queued;
            clearQueue();
        }
        // Readiness may recover after IsGameplay did: do not require both state
        // changes to occur on the same sample, and never consume an unknown identity.
        if (canActivate(s) && !s.character.empty() && pending >= 0 && pendingCharacter_ == s.character) {
            if (!s.mount && result < 0) result = pending;
            clearPending();
        }
        previous_ = s; seen_ = true;
        if (result >= 0) {
            // A pending map action can itself require the combat queue.
            return request(result, s, opts);
        }
        return -1;
    }
};

} // namespace mounts
