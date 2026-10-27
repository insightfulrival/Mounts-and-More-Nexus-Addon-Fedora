# Mounts & More — Nexus Addon for Linux

Contextual mount menus, mastery skills and novelties for **Guild Wars 2 + Nexus through Wine/Proton**, adapted from Blish HUD Mounts & More 1.5.3.

This addon is a **Windows x64 DLL built on Fedora**. It runs inside GW2 through Nexus API 6 and uses Dear ImGui 1.80. Blish HUD and .NET are not required. Other Linux distributions with a working GW2/Nexus installation may work, but have not been individually tested.

## Status

Development build **0.1.1**. Native core tests and Windows adapter tests run successfully with assertions enabled, including loading the actual DLL through Wine against a simulated Nexus API. Full visual and gameplay acceptance testing in GW2 is still pending. This is not a claim of complete in-game parity.

## Features

- 21 actions: nine mounts, dismount, six mastery actions including Skiff and Homestead Doorway, and five novelty slots.
- Seven contextual profiles: mounted, WvW, combat, underwater, water surface, gliding/falling, and default.
- Configurable radial menus with tap actions, instant single-action selection, default/last-used center, ordering, radius, angle, opacity and positioning.
- Custom radials with individual Nexus input binds; horizontal or vertical icon bars; Nexus Quick Access shortcuts; custom PNG icons.
- Optional out-of-combat queue and remount after loading on the same character. Both are disabled by default.
- Separate status indicators for queued, deferred and ground-targeted actions, with a movable, lockable panel and saved position.
- Water/falling heuristics and the original 11 SkyLake polygons.
- Normal, instant and fast ground targeting. Match the selected mode to your GW2 setting.
- Optional Combat Launch mastery verification using a temporary GW2 API token with account/progression permissions. Tokens are not saved or logged.

## Build

Requirements: Python 3 and Zig with its C++ frontend. Tested with **Zig 0.14.1**.

```bash
git clone https://github.com/insightfulrival/Mounts-and-More-Nexus-Addon-Fedora.git
cd Mounts-and-More-Nexus-Addon-Fedora
ZIG=/absolute/path/to/zig ./build.sh
```

The build generates lake data, runs native tests, compiles the addon and Windows test harness, verifies the PE/export contract, and packages the DLL, icons and third-party notices.

Outputs:

- `dist/MountsNexus.dll`
- `dist/MountsNexus-local-0.1.1.zip`
- `dist/SHA256SUMS.json`

## Install

Install Nexus first. Close GW2 before updating an already loaded addon, then run:

```bash
python3 tools/install.py --game-dir "$HOME/.local/share/Steam/steamapps/common/Guild Wars 2"
```

Adjust the path for your installation. The script verifies build checksums, backs up an existing DLL and settings, installs `addons/MountsNexus.dll`, and copies resources into `addons/MountsNexus/`. Existing PNG files and user settings are preserved.

For manual installation, copy the DLL into `Guild Wars 2/addons/` and the entire `dist/MountsNexus/` directory into `Guild Wars 2/addons/MountsNexus/`.

In **Nexus → Addons**, refresh the list and load **Mounts & More (Fedora port)**. Restarting GW2 is an alternative if it is not discovered. Follow the [official Nexus installation guide](https://raidcore.gg/gw2/guides/tools/nexus/installation) for Wine/Proton loader configuration.

## Controls and settings

| Shortcut | Action |
|---|---|
| Hold **Ctrl+Alt+M** | Open contextual radial; release to select |
| **Ctrl+Shift+M** | Open settings |
| **Ctrl+Alt+Backspace** | Cancel pending actions |
| **Escape** | Cancel radial or pending ground targeting |

Configure action keys in GW2 and **Nexus → Binds → Guild Wars 2**. Change addon shortcuts in **Nexus → Binds → Nexus**, under **Mounts & More (Fedora port)**. Click the key beside **Mounts & More: Main radial (hold)** (`MM_CONTEXT`) to change the main shortcut. A configured bind does not establish mount ownership or skill availability.

The **Bindings** tab in addon settings explains both steps and shows the game-key status of every action. Mount icons stay visible before keys are configured; selecting an unbound action opens setup without sending input to GW2.

Settings are stored in `addons/MountsNexus/settings.json`. Use Save in the settings window; remaining changes are also saved on unload. Invalid or unsupported settings files are preserved rather than overwritten.

Custom icons belong in `addons/MountsNexus/icons/`. Enter the PNG filename in settings and select Reload icons. Jump detection defaults to Space (Windows virtual-key code 32), and can be configured to match your jump key.

Input is gated while chat or the map is open, or GW2 lacks focus. Combat queues are cleared on character/map changes, loading or invalid telemetry, and are disabled in competitive maps. Ground-target cancellation sends Escape to the game before releasing the held skill key.

## Tests

```bash
# Run build.sh first; the test uses a separate Wine prefix.
WINE=/absolute/path/to/wine ./test-wine.sh
```

Tests cover catalog and context rules, water polygons, radial geometry, falling detection, queue ownership, loading transitions, ABI layout, all 21 game binds, focus/chat/map gates, tap/release ordering, ground-target cancellation versus confirmation, configuration preservation and DLL lifecycle. Test assertions remain enabled in optimized builds.

Wine tests use simulated Nexus APIs and do not send input to GW2. In-game verification is still needed for rendering/UI scale, action camera, keyboard layouts, actual mastery/novelty behavior, water/gliding, combat queue, remount and unload.

## Known differences and limits

- The interface is rewritten in ImGui, not a pixel-for-pixel MonoGame reproduction.
- Nexus Game Binds replace duplicate Blish key configuration. Blish settings are not automatically imported.
- Custom radial deletion preserves empty slots to keep existing shortcuts stable; this version supports 32 created slots.
- Mount ownership, cooldowns and successful action completion are not exposed by the telemetry used here.
- API mastery verification is explicit. Recheck after changing accounts.
- The addon uses a local development signature, not a registered Raidcore library identity.

## Credits and provenance

Original addon: **Manlaan and Bennieboj**, [BlishHud-Mounts](https://github.com/bennieboj/BlishHud-Mounts), reference commit `a02d8b1c3e867e7f3141396e96061501d195309d`. The supplied DLL was inspected using REA; the original artifact is not included in this repository.

The port preserves original icon and lake resources with attribution. See [THIRD_PARTY.md](THIRD_PARTY.md) for dependency versions, licenses and the unresolved licensing status of the original addon resources. No blanket license is asserted over those materials.
