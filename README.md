# cMod Readme

cMod is a multiplayer client and server for [Star Trek: Voyager - Elite Force](https://en.wikipedia.org/wiki/Star_Trek:_Voyager_%E2%80%93_Elite_Force) based on ioEF, with many new features:

- Improved ingame server browser.
- Widescreen aspect correction and configurable HUD centering.
- Improved default settings and configuration menus.
- Improved map brightness options, and auto-correct brightness to fix maps (especially ported from other games) being too bright or dark.
- Improved settings system which keeps the same settings for all servers and prevents servers from making unwanted changes.
- Improved filesystem which fixes pk3 conflicts and limits, and improves load times.
- New settings/console commands for advanced users and dedicated server operators.
- Many tweaks and bugfixes.

Author: Noah Metzger chomenor@gmail.com

## Installation

Download the [release](https://github.com/Chomenor/ioef-cmod/releases), extract it into your Elite Force directory, and run the included application. In most cases your existing EF client will continue to work without conflicts, but if you want to be extra safe, make a backup of your EF directory before installing cMod.

GOG users: If you installed Elite Force through [Good Old Games](https://www.gog.com/en/game/star_trek_voyager_elite_force), you can alternatively extract the zip into an empty directory and run it from there. The game files should be automatically located from the GOG installation.

## Documentation

- [Installation Guide](docs/installation.md) - For additional install instructions and platform-specific information.
- [Client Guide](docs/client.md) - Covers client configuration details and troubleshooting.
- [Server Guide](docs/server.md) - Information about hosting dedicated servers.
- [Compile Guide](docs/build.md) - How to build the project from source.

## Limitations

- No single player support.
- Currently no Vulkan renderer support (OpenGL only).
- Currently no monitor selection for multi-monitor systems. You will need to set the preferred monitor as default in OS settings.
- Limited screen resolution options in the menu (but it can be configured through the console).
- Although other platforms are supported, the Windows releases are the most reliable and well tested.

## Credits

- Raven Software - For the game and game SDK source code release.
- id Software - For the engine and source code release.
- Thilo Schulz - For creating [ioEF](https://github.com/thiloschulz/ioef), the basis for this project.
- Zack Middleton - For porting [opengl2 renderer](opengl2-readme.md) to EF and other contributions via [ioquake3](https://github.com/ioquake/ioq3) and [Lilium Voyager](https://github.com/zturtleman/lilium-voyager) projects.
- ioquake3 project - For their work maintaining the engine.

Special thanks to [The Last Outpost](https://last-outpost.net) for supporting this project and including it in the [holomat.ch](https://holomat.ch) releases.
