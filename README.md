# Skyrim Companion

A live map and character readout for Skyrim Special Edition that runs in a browser window, so you can keep it on a second monitor while you play.

## What it shows

A second-monitor HUD, MMO style:

- **World map**, always open: your position and facing, discovered map markers with their in-game icons, and a trail of where you've walked. Drag to pan, scroll to zoom, "Follow" to lock on.
- **Mini map** that rotates with you. Outdoors it shows the map around you; indoors it traces your path.
- **Compass**, location, and in-game time and date.
- **Vitals**: health, magicka, stamina, plus combat, sneak, and mounted status.
- **Active effects**: every effect on you with its icon, strength, time left, description, and source (potion, spell, enchanted item, or who cast it).
- **Survival** (Survival Mode): live hunger, cold, and fatigue meters with stage names.
- **Equipment**: both hands, ammo count, shout/power, every worn piece with armor values and enchantments, total armor rating, resistances, and carry weight.
- **Consumables**: potions grouped by what they do (Restore Health, Magicka, Stamina, cures, resistances, and so on), food, and poisons, with counts.

Undiscovered places stay hidden unless you turn on "Undiscovered".

## Game visuals (assets pack)

The map picture, icons, and font come from your own game install (your map mod, CoMAP, SkyUI, and your UI font). They are packaged separately as "Skyrim Companion - Assets" and are not part of this repository. Install that pack in Vortex alongside the plugin. Without it the window still works, with plain shapes instead of icons.

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
- The game's real local map (captured from the engine) for the mini map
- Custom markers and notes

## License

GPL-3.0 with the exceptions in `EXCEPTIONS`, inherited from the CommonLibSSE-NG template. Built on [CommonLibSSE-NG](https://github.com/alandtse/CommonLibVR/tree/ng).
