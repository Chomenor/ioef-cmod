# Client Readme

## Working Directory

If possible, cMod will store files such as settings and downloads in the same location it is installed, like the original version of Elite Force.

If cMod is installed somewhere where standard applications do not have write permissions, such as Program Files on Windows, it will store files in a folder under the user directory instead. On Windows this will typically be located at `%appdata%\STVEF`, on Mac at `~/Library/Application Support/STVEF`, and on Linux according to the [XDG Spec](https://specifications.freedesktop.org/basedir/latest/) (e.g. `~/.config/stvef`, `~/.local/share/stvef`, `~/.local/state/stvef`).

## Settings Handling

Settings are stored in a file called "cmod.cfg" directly in the EF working directory. This is separate from the settings files used by other versions of EF, such as hmconfig.cfg. The cmod.cfg file is designed to be human-editable, so feel free to open it and make changes.

*Autoexec Restriction:* By default, cMod restricts loading some settings from the autoexec.cfg file. This is to protect against autoexec.cfg files that are prepackaged in EF distributions and may be incompatible with cMod. If you encounter issues, set `cmod_restrict_autoexec` to 0 (in the console or cmod.cfg, not in the autoexec.cfg itself).

## Mod Handling

For stability reasons, cMod prioritizes the official game pk3s (pak0-pak3) over other pk3s in baseEF regardless of filename. This means certain kinds of mod pk3s may not work out of baseEF. To use such mods in cMod, create a folder called "basemod" alongside baseEF and place the mod pk3s in that folder instead.

Typical maps and models still work normally out of baseEF, and mods in other directories loaded via the mod menu or fs_game also work normally.

## Brightness

The brightness settings can be accessed through the menu via `Configure -> Video -> Brightness`. Four template settings are available. I recommend trying them to find the one you like best. If you want to customize the visuals even more, you can fine tune the other slider settings afterwards.

- Plain - Default setting. Safe values that work well with almost all maps.
- Full Contrast - Original EF defaults. Good visually, but can make parts of some maps too bright.
- Extra Bright - For better visibility.
- Full Bright - Removes all shadows and lighting effects. Results vary between maps.

## Resolution Handling

cMod uses separate resolution mode settings for fullscreen and windowed display.

- r_mode: Applies when display is in windowed mode (default 720x480).
- r_fullscreenMode: Applies when display is in fullscreen mode (default -2).

The most common values which you can use for these settings are as follows:

- [width]x[height]: Specifies a specific custom resolution.
- -2: Uses the current maximum resolution of your display.
- -1: Uses the resolution specified by r_customWidth and r_customHeight.
- 3: Uses a 640x480 resolution (the original EF default).

## Custom Fastsky Color

cMod supports customizing the color of the sky in fastsky mode (when r_fastsky is set to 1). The color is controlled by the r_fastskyColor cvar and specified in RGB hex format. You can experiment with different combinations or use an online RGB picker tool that generates a standard 6-digit hex value to choose the color. Some sample values are as follows:

- `ccb366` - yellow (original Elite Force default)
- `777755` - darker shade of yellow
- `777777` - light gray
- `000000` - black (original ioEF default)
- `ffffff` - white

## Custom Crosshair Color

The crosshair color can also be customized using the cg_crosshairColor cvar. The default value `ffffff ffff00 ff0000` makes the crosshair white with full health, yellow with medium health, and red with low health.

Any combination of three space-separated hex values can be used. A single value is also supported for a constant color that doesn't change with health.

## Aspect Ratio Correction

The following settings are supported to help with widescreen and aspect ratio related issues.

- cg_aspectCorrect: Setting to 1 causes graphical components to be displayed at the original width, instead of stretching on widescreen displays. __(default: 1)__
- cg_fov: A simple integer value such as "90" sets the horizontal FOV, same as in the original game. It is also possible to set a scaled value such as "85*", indicated by a trailing asterisk, which automatically scales with the width of the display. __(default: 85*)__
- cg_aspectCorrectCenterHud: 0 = place HUD at edge of screen, 1 = place HUD at center 4:3 region of screen. Fractional values such as 0.5 are also supported. Note this setting won't work when certain mods are loaded. __(default: 0)__
- cg_aspectCorrectGunPos: 0 = original behavior, 1 = fixes the gun view position so it doesn't drop on wider screens. A value of -1 uses whatever value is set in cg_aspectCorrect. __(default: -1)__

## Font Scaling

In the original game, the console and chat font are displayed at a fixed size, unlike other graphical components that scale with the resolution of the display. This can lead to the font appearing too small on high resolution displays. cMod adds a new setting called "cmod_font_scaling" intended to help compensate for this effect.

- A setting of 1 scales the font to the same relative size as the original game running at 640x480 (the original default resolution).
- A setting between 0 and 1 scales to some fraction of the font size represented by 1. However it will not scale below the original unscaled font size.
- A setting of 0 produces the original unscaled font size.

## URI Handler

To enable launching the game and joining servers directly from web-based server browsers such as [efservers.com](https://efservers.com/) on Windows, open the in-game console and run the command `uri register`.

The handler can also be unregistered using the command `uri remove`.

## Download Folder Support

The "fs_download_mode" setting can be used to customize pk3 download handling on the client. It supports the following values:

- 0: Pk3 files are downloaded to baseEF or the mod directory.
- 1 (default): Pk3 files are saved to the "downloads" folder within the target mod directory. For example, "baseEF/somefile.pk3" will be rewritten to "baseEF/downloads/somefile.pk3". Pk3s in the downloads folder are deprioritized compared to pk3s directly in the mod directory.
- 2: Same as 1, but also disables loading certain less secure content (cfg files and qvm files that don't match a list of trusted mod hashes) from the downloads folder.

A setting of 1 can help organize downloaded pk3s and reduce the chance of pk3 conflicts. A setting of 2 can help increase security when downloading from untrusted servers, but may cause mod compatibility issues.

# FAQ

**Q:** Mouse movement is different from other clients.  
**A:** By default, cMod uses raw mouse input mode, which bypasses OS acceleration settings and is generally considered more accurate for FPS games. If you prefer the mouse input to be the same as older versions of EF, try disabling the `RAW MOUSE` option in the Mouse menu.

Also make sure your sensitivity setting matches your old client. Use the `/sensitivity` console command to check the value in your old client, and `/sensitivity {value}` to set the same value in cMod.

**Q:** Problems opening console.  
**A:** There are known issues with the normal console key on some keyboard layouts. Try using the alternative combination, `Shift + ESC` to open the console.

**Q:** Settings "r_overBrightBits" and "r_mapOverBrightBits" not working.  
**A:** cMod uses r_overBrightFactor and r_mapLightingFactor instead, with the following mapping:

r_overBrightFactor = 2 ^ r_overBrightBits  
r_mapLightingFactor = 2 ^ r_mapOverBrightBits

For example, a setting of r_mapOverBrightBits 3 in another client translates to r_mapLightingFactor 8 in cMod.

**Q:** Problems with low framerates or jerkiness.  
**A:** By default, cMod uses higher graphics settings than the original version of EF. These settings work best on newer computers and those intended for gaming. Check your video card/GPU model at [videocardbenchmark.net](https://videocardbenchmark.net). I recommend a score of 600 minimum, and 1200+ ideally, to run EF at full detail and 1080p resolution. If your hardware scores below these numbers you might consider upgrading your PC/video card.

If you are having trouble with framerates in cMod you can try lowering the graphics quality settings in the menu.

**Q:** Problems opening ingame chat.  
**A:** There is a known bug in cMod versions prior to 1.08. Make sure you are using the latest version of cMod.
