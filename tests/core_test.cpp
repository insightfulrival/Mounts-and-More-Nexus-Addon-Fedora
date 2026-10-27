#ifdef NDEBUG
#undef NDEBUG
#endif
#include "../src/core.hpp"
#include <cassert>
#include <iostream>
#include <limits>

using namespace mounts;

Snapshot live() {
    Snapshot s;
    s.valid = s.gameplay = s.focused = true;
    s.character = "Test Character";
    s.mapId = 15; s.mapType = 5; s.z = 10;
    return s;
}

void testCatalog() {
    static_assert(ActionCount == 21);
    assert(gameBinds[Skiff] == 205 && gameBinds[Fishing] == 204);
    assert(gameBinds[Doorway] == 211 && gameBinds[Unmount] == 152);
    assert(actionForMount(1) == Jackal && actionForMount(5) == Raptor);
    assert(actionForMount(9) == Skiff && actionForMount(10) == Turtle);
    assert(actionForMount(0) == -1 && actionForMount(100) == -1);
    for (int action = 0; action < ActionCount; ++action) {
        assert(validAction(action));
        assert(actionNames[action] && iconStems[action]);
        if (mountIndex[action]) assert(actionForMount(mountIndex[action]) == action);
    }
    assert(!validAction(-1) && !validAction(Count));
    assert(usableInCombat(Skimmer, false)); // 1.5.3 change
    assert(usableInCombat(Warclaw, false) && usableInCombat(Unmount, false));
    assert(!usableInCombat(Skyscale, false) && usableInCombat(Skyscale, true));
    assert(usableInCombat(SkyscaleLeap, true) && !usableInCombat(SkyscaleLeap, false));
    assert(!usableInCombat(Raptor, true));
    assert(isGroundTargeted(Skiff) && isGroundTargeted(Doorway));
    assert(!isGroundTargeted(JadeWaypoint));
}

void testWaterAndContexts() {
    Snapshot s = live();
    assert(waterSurface(s) == 0 && !underwater(s) && !onSurface(s));
    s.z = -1.2f; assert(underwater(s));
    s.z = -0.5f; assert(onSurface(s) && !underwater(s));
    assert(!delayedGroundTarget(Skiff, s));
    s.z = 0; assert(!onSurface(s));
    assert(delayedGroundTarget(Skiff, s) && delayedGroundTarget(Doorway, s));
    s = live(); s.mapId = 1510; s.x = 0; s.y = 0; s.z = 396.4f;
    assert(std::abs(waterSurface(s) - 396.9f) < 0.001f);
    assert(onSurface(s) && !underwater(s));
    s.z = 390; assert(underwater(s));
    s.z = 299; assert(waterSurface(s) == 0); // below lake bottom
    s.z = 410; assert(waterSurface(s) == 0); // above lake surface + 10
    s.x = 5000; s.z = 396; assert(waterSurface(s) == 0); // outside polygon
    s = live(); assert(contextIndex(s) == Default);
    s.falling = true; assert(contextIndex(s) == Falling);
    s.z = -0.5f; assert(contextIndex(s) == Surface);
    s.z = -2; assert(contextIndex(s) == Underwater);
    s.combat = true; assert(contextIndex(s) == Combat);
    s.mapType = 18; assert(contextIndex(s) == Wvw);
    s.mount = 7; assert(contextIndex(s) == Mounted);
    assert(contextApplicable(Default, s) && contextApplicable(Wvw, s));
    assert(!contextApplicable(-1, s) && !contextApplicable(99, s));
    for (int map : {9, 10, 11, 12, 18}) { s.mapType = map; assert(isWvw(s)); }
    s.mapType = 13; assert(!isWvw(s));
    s.mapType = 14; assert(!isWvw(s)); // not present in original's warclaw-only list
}

void testSectors() {
    assert(chooseSector(0, 0, 4, 0, 20) == -1);
    assert(chooseSector(19.9f, 0, 4, 0, 20) == -1);
    assert(chooseSector(20, 0, 4, 0, 20) == 0);
    assert(chooseSector(100, 100, 4, 0, 20) == 0);
    assert(chooseSector(-100, 100, 4, 0, 20) == 1);
    assert(chooseSector(-100, -100, 4, 0, 20) == 2);
    assert(chooseSector(100, -100, 4, 0, 20) == 3);
    assert(chooseSector(100, 0, 4, 90, 20) == 3);
    assert(chooseSector(100, 0, 4, 360, 20) == 0);
    assert(chooseSector(100, 0, 0, 0, 20) == -1);
    assert(chooseSector(std::numeric_limits<float>::quiet_NaN(), 0, 4, 0, 20) == -1);
}

void testFalling() {
    FallingDetector d;
    assert(!d.sample(100, 0)); // baseline, no artificial startup velocity
    assert(!d.sample(0, 0)); // same timestamp: never divide by zero
    assert(!d.sample(99.5f, 0.05)); // below sampling interval
    assert(d.sample(98, 0.1));
    assert(!d.sample(98, 0.2));
    d.reset(100, 10); d.jump(10);
    assert(d.sample(99.5f, 10.1)); // recent jump allows <-2 m/s
    assert(!d.sample(99.0f, 15.1)); // recent-jump window expired
    d.reset(100, 20); d.jump(20, true);
    assert(!d.sample(99.5f, 20.1)); // mounted jump must not activate gliding heuristic
    assert(!d.sample(99, 19)); // clock reversal reinitializes
    assert(d.sample(101, 19.1)); // upward speed >10 also means flying
}

void testEngine() {
    Snapshot s = live();
    EngineOptions opts;
    assert(!opts.queueEnabled && !opts.autoRemount && !opts.combatLaunch);
    Engine e;
    assert(e.update(s, opts) == -1);
    assert(e.request(Raptor, s, opts) == Raptor);
    s.combat = true; opts.queueEnabled = true;
    e.update(s, opts);
    assert(e.request(Raptor, s, opts) == -1 && e.queued == Raptor);
    assert(e.request(Springer, s, opts) == -1 && e.queued == Springer);
    s.combat = false;
    assert(e.update(s, opts) == Springer && e.queued == -1);
    assert(e.update(s, opts) == -1); // once only

    s.combat = true; e.update(s, opts);
    e.request(Raptor, s, opts);
    s.mount = 8; s.combat = false;
    assert(e.update(s, opts) == -1 && e.queued == -1); // combat-launch must win over queue
    s.mount = 0; s.combat = true; s.competitive = true;
    assert(e.request(Raptor, s, opts) == Raptor && e.queued == -1);
    s.competitive = false; s.mapType = 18;
    assert(e.request(Raptor, s, opts) == Raptor && e.queued == -1);
    s.mapType = 5; e.update(s, opts); e.request(Raptor, s, opts);
    s.character = "Other Character"; s.combat = false;
    assert(e.update(s, opts) == -1 && e.queued == -1);

    s = live(); e.update(s, opts); s.combat = true; e.update(s, opts);
    e.request(Raptor, s, opts); s.focused = false;
    assert(e.update(s, opts) == -1 && e.queued == -1);
    s.focused = true; s.textbox = true;
    assert(e.request(Warclaw, s, opts) == -1);
    s.textbox = false; s.mapOpen = true;
    assert(e.request(Warclaw, s, opts) == -1);
    s.mapOpen = false; s.valid = false; s.gameplay = false;
    assert(e.request(Skimmer, s, opts) == -1 && e.pending == Skimmer);
    e.update(s, opts); s.valid = s.gameplay = true; s.combat = false;
    assert(e.update(s, opts) == Skimmer && e.pending == -1);

    opts.autoRemount = true; s.mount = 2; e.update(s, opts);
    s.valid = false; s.gameplay = false;
    assert(e.update(s, opts) == -1 && e.pending == Griffon);
    s.valid = s.gameplay = true; s.mount = 0;
    assert(e.update(s, opts) == Griffon && e.pending == -1);
    assert(e.update(s, opts) == -1);
    s.mount = 5; e.update(s, opts); s.gameplay = false; e.update(s, opts);
    s.gameplay = true; s.mount = 0; s.character = "Other Character";
    assert(e.update(s, opts) == -1 && e.pending == -1);
}

void testQueueOwnershipAndReplacement() {
    EngineOptions opts; opts.queueEnabled = true;
    Snapshot s = live(); s.combat = true;
    Engine e; e.update(s, opts);

    e.request(Raptor, s, opts);
    assert(e.queued == Raptor);
    // A directly usable choice replaces an older combat queue.
    assert(e.request(Skimmer, s, opts) == Skimmer && e.queued == -1);
    s.combat = false;
    assert(e.update(s, opts) == -1);

    s.combat = true; e.update(s, opts); e.request(Raptor, s, opts);
    opts.queueEnabled = false;
    assert(e.update(s, opts) == -1 && e.queued == -1);
    opts.queueEnabled = true; s.combat = false;
    assert(e.update(s, opts) == -1); // turning it back on never restores old intent

    s.combat = true; e.update(s, opts); e.request(Raptor, s, opts);
    ++s.mapId; // direct map identity change, even without a loading sample
    s.combat = false;
    assert(e.update(s, opts) == -1 && e.queued == -1);

    s.combat = true; e.update(s, opts); e.request(Raptor, s, opts);
    s.gameplay = false; // also cancel if loading has not published the new map ID yet
    assert(e.update(s, opts) == -1 && e.queued == -1);
    s.gameplay = true; s.combat = false;
    assert(e.update(s, opts) == -1);

    s.combat = true; e.update(s, opts); e.request(Raptor, s, opts);
    s.character.clear();
    assert(e.update(s, opts) == -1 && e.queued == -1);
    // A missing identity must not create a queue that can fire for an unknown character.
    assert(e.request(Raptor, s, opts) == -1 && e.queued == -1);
    s.combat = false;
    assert(e.update(s, opts) == -1);
    s.character = "Other Character";
    assert(e.update(s, opts) == -1);

    s.combat = true; e.update(s, opts); e.request(Raptor, s, opts);
    s.valid = false;
    assert(e.update(s, opts) == -1 && e.queued == -1);
    s.valid = true; s.combat = false;
    assert(e.update(s, opts) == -1);
    s.combat = true; s.mapId = 0;
    assert(e.request(Raptor, s, opts) == -1 && e.queued == -1);
}

void testPendingReadiness() {
    EngineOptions opts; opts.autoRemount = true;
    Snapshot s = live(); s.mount = 8;
    Engine e; e.update(s, opts);
    // Loading can retain a valid Mumble tick and later recover gameplay before validity.
    s.gameplay = false;
    assert(e.update(s, opts) == -1 && e.pending == Skyscale);
    s.valid = false; s.gameplay = true; s.mount = 0; ++s.mapId;
    assert(e.update(s, opts) == -1 && e.pending == Skyscale);
    s.valid = true;
    assert(e.update(s, opts) == Skyscale && e.pending == -1);
    assert(e.update(s, opts) == -1);

    s.mount = 2; e.update(s, opts); s.gameplay = false;
    assert(e.update(s, opts) == -1 && e.pending == Griffon);
    s.character.clear(); s.valid = false;
    assert(e.update(s, opts) == -1 && e.pending == Griffon); // preserve across absent loading identity
    s.gameplay = true; s.valid = true; s.mount = 0;
    assert(e.update(s, opts) == -1 && e.pending == Griffon); // never execute without known character
    s.character = "Test Character";
    assert(e.update(s, opts) == Griffon && e.pending == -1);

    s.mount = 2; e.update(s, opts); s.gameplay = false; e.update(s, opts);
    opts.autoRemount = false;
    assert(e.update(s, opts) == -1 && e.pending == -1);
    s.gameplay = true; s.mount = 0;
    assert(e.update(s, opts) == -1);

    // Explicit pending intent survives auto-remount being off and replaces previous intent.
    s.gameplay = false; e.update(s, opts);
    assert(e.request(Raptor, s, opts) == -1 && e.pending == Raptor);
    assert(e.request(Skimmer, s, opts) == -1 && e.pending == Skimmer);
    s.valid = false; s.character.clear();
    assert(e.request(Springer, s, opts) == -1 && e.pending == -1); // unknown request cancels old intent
    s.character = "Test Character"; s.valid = s.gameplay = true;
    assert(e.update(s, opts) == -1);

    // No initial update is required for a deliberate loading-screen choice.
    Engine fresh;
    s.gameplay = false;
    assert(fresh.request(Skimmer, s, opts) == -1 && fresh.pending == Skimmer);
    s.gameplay = true;
    assert(fresh.update(s, opts) == Skimmer && fresh.pending == -1);
}

int main() {
    testCatalog(); testWaterAndContexts(); testSectors(); testFalling(); testEngine();
    testQueueOwnershipAndReplacement(); testPendingReadiness();
    std::cout << "core: catalog, contexts, lake polygons, sectors, falling, queue and loading transitions passed\n";
}
