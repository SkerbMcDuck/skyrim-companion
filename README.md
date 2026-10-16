# Skyrim Companion

A live map and character readout for Skyrim Special Edition that runs in a browser window, so you can keep it on a second monitor while you play.

## What it shows

- **World map** with your position and facing, every map marker you've found, and a trail of where you've walked. Drag to pan, scroll to zoom, "Follow player" to lock on.
- **Mini map** that rotates with you (can be turned off), with nearby places and your trail.
- **Local map** that traces your path inside dungeons and buildings, or a 500 m view outdoors.
- **Readouts**: health, magicka, stamina, level, gold, in-game time and date, heading, nearest known place, places discovered, and combat/sneak/mounted status.
- **Optional map picture**: load any map image you own (a screenshot of your in-game map works) and line it up in two clicks.

Undiscovered places stay hidden unless you turn on "Show undiscovered places".

## Requirements

- Skyrim Special Edition on Steam, launched through SKSE64
- Address Library for SKSE Plugins (Gate to Sovngarde already includes it)

## Install with Vortex

1. Download the latest build (see "Getting a build" below). It's a zip containing an `SKSE` folder.
2. In Vortex, drag the zip onto the **Mods** page (or use "Install From File").
3. Enable it and deploy. Use a separate Vortex profile for testing if you want your main playthrough untouched.
4. Start Skyrim through SKSE and load a save.
5. On your second monitor, open a browser and go to **http://localhost:7171**
6. Press F11 in the browser for full screen.

Tip: set Skyrim to borderless windowed so clicking the second monitor doesn't minimize the game.

## Getting a build

Every change pushed to this repository is compiled automatically by GitHub:

1. Open the **Actions** tab of this repository.
2. Click the newest successful "Build plugin" run (green check).
3. Under **Artifacts**, download `SkyrimCompanion-<number>`. That zip is the mod.

## Try it without the game

Open `web/index.html?demo` in a browser, or click "Try demo mode" on the page when the game isn't running.

## Troubleshooting

- **Page won't load at localhost:7171**: the plugin isn't running. Check `Documents\My Games\Skyrim Special Edition\SKSE\SkyrimCompanion.log` and `skse64.log` in the same folder.
- **"Waiting for a loaded save"**: the game is at the main menu. Load a save.
- **Port in use**: another program is using port 7171. The log will say so.
- Disable this plugin before asking for help with a modlist (for example Gate to Sovngarde support), and confirm the problem still happens.

## Privacy and safety

The plugin only reads game data. It writes nothing to your saves. Its web server listens on 127.0.0.1, which means only programs on your own computer can reach it.

## Planned

- Quest target markers
- A true local map image for interiors
- Custom markers and notes

## License

GPL-3.0 with the exceptions in `EXCEPTIONS`, inherited from the CommonLibSSE-NG template. Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng).
