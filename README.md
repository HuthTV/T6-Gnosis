# T6 Gnosis

A Plutonium T6 (Black Ops II Zombies) plugin that shows what the game knows but does
not show you: zombies through walls, despawn timing, spawn points, collision, triggers,
the navigation graph, a movement HUD and puzzle aids for Origins and Mob of the Dead.

T6 Gnosis only observes. It never changes points, weapons, rounds or map state, so it
runs alongside practice tools such as O-Prac. It is made for **solo LAN** games.

## Install

1. Download `T6Gnosis-<date>.zip` from the [releases](../../releases) and extract `T6Gnosis.dll`.
2. Put it in `%LOCALAPPDATA%\Plutonium\plugins`.
3. Start Plutonium T6 Zombies and load a map. Press **F8** to open the menu.

Files T6 Gnosis creates:

- `plugins\T6Gnosis.cfg`: your settings and quick-control keys (delete it, or press
  **Reset** in the menu, to restore the defaults).
- `storage\t6\scripts\zm\t6gnosis_helper.gsc`: a small helper script the plugin
  writes at game start. It does nothing when the plugin is not installed.

To uninstall, delete `T6Gnosis.dll`, `T6Gnosis.cfg` and the helper script.

## Features

| Section | What it shows |
| --- | --- |
| Overlay | Master switch for everything T6 Gnosis draws. |
| Movement HUD | Sprint stamina bar, speedometer and jump stats (input timing, jump speed). |
| Zombies | Wallhack arrows (diamonds for special enemies). Despawn tracker: zombie counts, the time to the next cleanup check, despawned and prevented zombies, arrows coloured by cleanup status. Only on maps with despawn cleanup (TranZit, Die Rise, Mob of the Dead, Buried, Origins). |
| Spawns | Zombie and special spawn points, which are active, labels. |
| Player collision | World collision surfaces (floors, walls, roofs, invisible clips), the player bounding box, zombie collision boxes. |
| Triggers | Trigger volumes and the playable area. |
| NAV | The zombie navigation graph: nodes, links, traversals. |
| Origins | Wind and fire puzzle 2 aids: hitboxes and where the held staff's shot lands. |
| Mob of the Dead | Blue skull aid: each skull's tomahawk catch zone and the predicted tomahawk check point. Afterlife shock boxes and their activation range. |

World overlays are drawn under the game's HUD; your weapon and zombies cover them.
Every top-level option can be bound to a key under **Quick controls** in the menu.

## Build

Clone with `git clone --recursive` (the Plutonium SDK is a submodule).
Requirements: Visual Studio 2022 or later with *Desktop development with C++*, and
[gsc-tool](https://github.com/xensik/gsc-tool) (`gsc-tool.exe` in `tools\`, or set
`GSC_TOOL`).

```bat
build.bat                 :: development build      -> build\T6Gnosis.dll
build.bat release         :: release, dated version -> dist\T6Gnosis.dll and dist\T6Gnosis-<date>.zip
powershell -ExecutionPolicy Bypass -File install.ps1   :: copy the newest build into Plutonium
```

Every build starts clean and runs the unit tests. Releases are versioned by build date
(`YYYY.MM.DD`); the version shows in the menu title and in the DLL's file properties. The DLL
itself is always named `T6Gnosis.dll`, so an update replaces the old file.

GitHub Actions builds and tests every push. Pushing a tag `vYYYY.MM.DD` builds that
version and publishes a release whose notes link to the exact workflow run, with SHA256
checksums and a build provenance attestation (`gh attestation verify T6Gnosis.dll --repo HuthTV/T6-Gnosis`).

## Third-party code

- [Dear ImGui](https://github.com/ocornut/imgui) 1.91.9b, MIT license (`vendor/imgui-1.91.9b/LICENSE.txt`).
- [MinHook](https://github.com/TsudaKageyu/minhook) 1.3.4, BSD 2-clause (`vendor/minhook-1.3.4/LICENSE.txt`).
- [Plutonium SDK](https://github.com/plutoniummod/plutonium-sdk) headers (`vendor/plutonium-sdk`).
