# Endless Sky

Explore other star systems. Earn money by trading, carrying passengers, or completing missions. Use your earnings to buy a better ship or to upgrade the weapons and engines on your current one. Blow up pirates. Take sides in a civil war. Or leave human space behind and hope to find some friendly aliens whose culture is more civilized than your own...

------

Endless Sky is a sandbox-style space exploration game similar to Elite, Escape Velocity, or Star Control. You start out as the captain of a tiny spaceship and can choose what to do from there. The game includes a major plot line and many minor missions, but you can choose whether you want to play through the plot or strike out on your own as a merchant or bounty hunter or explorer.

See the [player's manual](https://github.com/endless-sky/endless-sky/wiki/PlayersManual) for more information, or the [home page](https://endless-sky.github.io/) for screenshots and the occasional blog post.

## UI layout editor and graphics tools

This branch includes a persistent, resolution-independent UI layout editor. It is intended for adjusting HUD elements, map panels, menus, and user-created overlays without requiring a source rebuild.

### Enabling the editor

Open the editor with **F4** (the binding can be changed in `Preferences → Controls → UI Layout`). The editor is modal: while it is active, clicks and text input cannot accidentally reach the game panel underneath. An on-screen indicator shows when it is enabled, and **Esc** always exits it.

The default editor shortcuts are:

| Shortcut | Action |
|---|---|
| `F4` | Enable/disable the layout editor |
| `F5` / `F6` | Increase/decrease the selected element size |
| `F7` | Cycle the selected element color |
| `F8` | Toggle the selected element visibility |
| `F9` | Add a custom text overlay |
| `F10` | Add the default custom sprite overlay |
| `F12` | Open the visual sprite asset browser |
| `Tab` / `Shift+Tab` | Select the next/previous registered element |
| `Shift` + drag | Fine-grained element movement |
| `Backspace` | Remove the selected custom overlay |
| `Delete` | Reset the selected element position |
| `Ctrl+Z` | Undo the last layout operation |
| `Ctrl+Shift+Z` or `Ctrl+Y` | Redo the last layout operation |

All primary shortcuts are represented by commands and can be rebound on the third **Controls** page. A complete drag is stored as one undo operation, rather than one operation per mouse-motion event.

### Custom overlays

Custom text and sprite elements are stored in `ui-layout.txt` as normalized screen-space positions and sizes. They support independent horizontal/vertical sprite scaling, color, opacity, duplication, visibility, and deletion. Deferred images are requested through the normal sprite-loading manager, so thumbnails, planets, stars, landscapes, and other deferred assets can be used without permanently staying unloaded.

The cheat-console interface provides precise editing commands:

```text
/layout list
/layout assets [filter]
/layout add sprite <name> [x y [width height]] [color]
/layout add text <text> [x y [width height]] [color]
/layout position <id> <x> <y>
/layout size <id> <width> <height>
/layout color <id> <color>
/layout opacity <id> <0-1>
/layout font <id> <size>
/layout set <id> <sprite-or-text>
/layout duplicate <id>
/layout show <id> / /layout hide <id>
/layout remove <id> / /layout clear
/layout undo / /layout redo
/layout reset all
```

### Visual asset browser

Press **F12** in the editor to open the sprite browser. It provides:

- a live text filter for registered sprite names;
- keyboard navigation and mouse selection;
- a preview with deferred-loading feedback;
- double-click or `Enter` insertion at the current cursor position;
- `Esc` cancellation without changing the layout.

The browser is modal while open and leaves the layout editor active underneath, so it can be closed and reopened without losing the current editing session.

### Dedicated server downloads

The headless multiplayer server is built as the separate `EndlessSkyServer` target. The continuous CD workflow publishes standalone archives for Linux x86_64, Windows x64, and macOS x86_64 under these names:

- `EndlessSky-server-continuous-linux-x86_64.tar.gz`
- `EndlessSky-server-continuous-win64.zip`
- `EndlessSky-server-continuous-macos-x86_64.zip`

The archives are attached to the `continuous` pre-release on GitHub, so a server can be downloaded without installing the graphical client. The server keeps its world and plugin data in its own folder; it does not read client saves or configuration. To build it locally, configure the project as usual and run:

```sh
cmake --build build/release --target EndlessSkyServer
```

On Windows, the archive also contains the MinGW runtime DLLs required by the server.

### Persistence and compatibility

Layout changes are written atomically through a temporary file before replacing `ui-layout.txt`, preventing a failed write from destroying the last valid layout. Schema version 2 is emitted for semantic layout identities. Existing numeric interface IDs remain supported and are migrated after interface data finishes loading. Data-defined elements can opt into stable identities with a child setting such as:

```text
layout "stable-element-name"
```

The editor also invalidates stale hit regions, prevents hidden controls from receiving mouse or keyboard events, and keeps map/orbit/HUD drawing and hit-testing geometry synchronized.

## Installing the game

Official releases of Endless Sky are available as direct downloads from [GitHub](https://github.com/endless-sky/endless-sky/releases/latest), on [Steam](https://store.steampowered.com/app/404410/Endless_Sky/), on [GOG](https://gog.com/game/endless_sky), and on [Flathub](https://flathub.org/apps/details/io.github.endless_sky.endless_sky). Other package managers may also include the game, though the specific version provided may not be up-to-date.

## System Requirements

Endless Sky has very minimal system requirements, meaning most systems should be able to run the game. The most restrictive requirement is likely that your device must support at least OpenGL 3.

|Hardware | Minimum | Recommended |
|---|----:|----:|
|RAM | 750 MB | 2 GB |
|Graphics | OpenGL 2.0* | OpenGL 3.0 |
|Screen Resolution | 1024x768 | 1920x1080 |
|Storage Free | 400 MB | 1.5 GB |

\* For OpenGL 2 devices, [custom shaders](https://github.com/endless-sky/endless-sky/wiki/CreatingPlugins#shaders) are needed.

|Operating System | Minimum Version |
|---|---|
|Linux | Any modern distribution (equivalent of Ubuntu 20.04) |
|MacOS | 10.15 |
|Windows | XP (5.1) |

It may be possible to run Endless Sky on other operating systems, though it is not officially supported.

## Building from source

Development is done using [CMake](https://cmake.org) to compile the project. Most popular IDEs are supported through their respective CMake integration.

For full installation instructions, consult the [Build Instructions](docs/readme-developer.md) readme.

## Contributing

As a free and open source game, Endless Sky is the product of many people's work. Contributions of artwork, storylines, and other writing are most in-demand, though there is a loosely defined [roadmap](https://github.com/endless-sky/endless-sky/wiki/DevelopmentRoadmap). Those who wish to [contribute](docs/CONTRIBUTING.md) are encouraged to review the [wiki](https://github.com/endless-sky/endless-sky/wiki), and to post in the [community-run Discord](https://discord.gg/ZeuASSx) beforehand. Those who prefer to use Steam can use its [discussion rooms](https://steamcommunity.com/app/404410/discussions/) as well, or GitHub's [discussion zone](https://github.com/endless-sky/endless-sky/discussions).

Endless Sky's main discussion and development area was once [Google Groups](https://groups.google.com/g/endless-sky), but due to factors outside our control, it is now inaccessible to new users, and should not be used anymore.

## Licensing

Endless Sky is a free, open source game. The [source code](https://github.com/endless-sky/endless-sky/) is available under the GPL v3 license, and all the artwork is either public domain or released under a variety of Creative Commons (and similarly permissive) licenses. (To determine the copyright status of any of the artwork, consult the [copyright file](https://github.com/endless-sky/endless-sky/blob/master/copyright).)
