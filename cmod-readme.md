# Project Description

cMod is an ioEF-based multiplayer client and server for Star Trek: Voyager - Elite Force, intended to add new features and fix bugs and limitations in the standard ioEF implementation.

Author: Noah Metzger <chomenor@gmail.com>

Feel free to contact me if you have any questions, feedback or bug reports.

# Client

To use the client, download the latest release zip, unzip it into your Elite Force install directory, and run the ioEF-cMod* application. In most cases your existing EF client will continue to work without conflicts, but if you want to be extra safe, make a backup of your EF directory before installing cMod.

## Settings Handling

Settings are managed differently in cMod than other versions of EF. Here is an overview of how the cMod settings system works.

- The settings are stored in a file called "cmod.cfg" directly in the EF install directory. This is separate from the settings files used by other versions of EF, such as hmconfig.cfg. The cmod.cfg file is designed to be human-editable, so feel free to open it and make changes.

- When you first run cMod (specifically, if the "cmod.cfg" file does not exist), cMod will attempt to import your binds and a limited subset of settings from an existing hmconfig.file and transfer them to cmod.cfg. If you want to reset cMod to a clean configuration, manually empty (but not delete) the cmod.cfg file.

- By default, cMod only loads a limited subset of settings from the autoexec.cfg file. This is to protect against autoexec.cfg files that are prepackaged in EF distributions and may be incompatible with cMod. If you created an autoexec.cfg file yourself and believe it is compatible with cMod, set cmod_restrict_autoexec to 0 (in the console, not in the autoexec.cfg itself).

## Directory Handling

By default, cMod will use the install location as the read/write directory, like the original version of Elite Force. This allows you to have multiple EF installations that use separate settings. The ioEF homepath location, which is %appdata%\STVEF on Windows, is used as a secondary read location in case you have any files left over there from a previous ioEF installation.

If cMod is run from a location where it does not have write permissions, such as under Program Files on Windows, it will revert to the standard ioEF behavior and use the homepath as the write location.

## Graphics Detail Settings

By default, cMod uses higher graphics settings than the original version of EF. These settings work best on newer computers and those with at least low end gaming-grade video cards. Check your video card/GPU model at [videocardbenchmark.net](http://videocardbenchmark.net). I recommend a score of 600 minimum, and 1200+ ideally, to run EF at full detail and 1080p resolution. If your hardware scores below these numbers you might consider upgrading your PC/video card.

If you are having trouble with framerates in cMod you can try lowering the graphics quality settings. Two of the most significant settings that might increase framerates (at the expense of graphics quality) are r_picmip 1 and r_ext_texture_filter_anisotropic 0. You can also try reducing the resolution using the options in the following section.

## Resolution Handling

cMod uses separate mode settings for fullscreen and windowed display.

- r_mode: Applies when display is in windowed mode (default 3).
- r_fullscreenMode: Applies when display is in fullscreen mode (default -2).

The most common values which you can use for these settings are as follows:

- -2: Uses the current maximum resolution of your display.
- -1: Uses the resolution specified by r_customWidth and r_customHeight.
- 3: Uses a 640x480 resolution (the original EF default).

## Brightness Settings

The following are some of the main settings that can be used to adjust the brightness of the game. Most of these settings require cMod version 1.04 or higher. Check the version you are running in the console and if you have an old version download the latest version from the Release page.

- r_gamma: Performs a gamma adjustment on the final renderer output. This is the default brightness setting changed by the in-game brightness slider. Values over 1 increase the game brightness on a curve, increasing the midrange levels without changing the minimum and maximum levels. Current default value is 1.4.

- r_mapLightingGamma: Performs a gamma-style adjustment on map lighting intensity. Values over 1 increase the map lighting levels on a curve, increasing the midrange levels without changing the minimum and maximum levels. Current default value is 1 (no change). Reasonable values for this setting range from 1.0 to around 2.0.

- r_mapLightingFactor: Performs a linear scaling on map lighting intensity. If any rgb component is scaled passed the maximum, values are scaled back so the highest component equals the maximum. Current default is 2 (EF standard). Values from around 2.5 to 8 can reasonably be used to increase brightness. Note while this setting has very good results brightening some maps, it is relatively inconsistent between maps, so use with caution.

- r_mapLightingClampMin: Sets the minimum lighting intensity (per rgb component), on a scale from 0 to 1. This can be used to force a minimum lighting level in maps. The default setting is 0. A value around 0.2 can be used to make the darkest parts of maps brighter without affecting the rest of the map.

- r_mapLightingClampMax: Sets the maximum lighting intensity (per rgb component), on a scale from 0 to 1. The default setting is 1. A value around 0.8 can be used to reduce lighting in areas that are overly bright and washing out the colors to white without affecting the rest of the map.

There are a number of other graphics settings that can affect brightness, but these are the ones that generally had the best results in my testing. If you don't like the way the graphics look in cMod, or you prefer the way an older EF client looks, feel free to send me an email so I can look into whether there is a way to improve your configuration. In general it is possible to replicate the graphics of any older client in cMod, although the exact settings you need to change to do that will vary depending on your system and configuration.

## Custom Fastsky Color

cMod supports customizing the color of the sky in fastsky mode (when r_fastsky is set to 1). The color is controlled by the r_fastskyColor cvar and specified in RGB hex format. You can experiment with different combinations or use an online RGB picker tool that generates a standard 6-digit hex value to choose the color. Some sample values are as follows:

- `ccb366` - yellow (original Elite Force default)
- `777755` - darker shade of yellow
- `777777` - light gray
- `000000` - black (original ioEF default)
- `ffffff` - white

## Font Scaling

In the original game, the console and chat font are displayed at a fixed size, unlike other graphical components that scale with the resolution of the display. This can lead to the font appearing too small on high resolution displays. cMod adds a new setting called "cmod_font_scaling" intended to help compensate for this effect.

- A setting of 1 scales the font to the same relative size as the original game running at 640x480 (the original default resolution).
- A setting between 0 and 1 scales to some fraction of the font size represented by 1. However it will not scale below the original unscaled font size.
- A setting of 0 produces the original unscaled font size.

## Anti Burn-in Support

The setting "cmod_anti_burnin" can be used to fade the in-game HUD graphics, to reduce possible burn-in effects on OLED-type displays. Valid values range from 0 (disabled) to 1 (maximum).

## Download Folder Support

Two new settings are supported to customize pk3 download handling:

- fs_saveto_dlfolder - When enabled (set to 1) this causes incoming downloads to be stored in the "downloads" folder in the target directory, e.g. baseEF/downloads. Pk3s in this folder are still loaded normally, but deprioritized compared to non-download pk3s. This can help organize downloads and reduce pk3 conflicts.
- fs_restrict_dlfolder - When enabled (set to 1) this prevents certain types of content (cfg files and qvm files from untrusted mods) from being loaded from the downloads folder. Combined with fs_saveto_dlfolder, this setting can increase security when downloading from untrusted servers.

## Crosshair Handling

cMod uses a new crosshair indexing system, which allows all the installed crosshairs to be accessed from the crosshair menu at once, and also adds new built-in crosshairs.

If you encounter crosshair-related issues, you can try disabling this feature by setting cmod_crosshair_enable to 0.

## Mouse Input Mode

By default, cMod tries to use raw input mode for mouse input, which bypasses OS acceleration settings and is generally more accurate for FPS games. If you prefer to make the mouse input work the same as older versions of EF, try setting in_mouse_warping to 1 and restart cMod.

## Console Key Notes

There are currently some issues with the console-opening key on non-English keyboard layouts. If you experience problems, try using the alternative combination to open the console, Shift + ESC. If you still have problems send me an email so I can investigate it.

# Server

It is possible to use cMod for hosting servers, but this is currently very experimental. Let me know if you encounter any bugs. I hope to add more features, documentation, and server config templates in the future.

## Usage

The separate dedicated server application is recommended for hosting servers. The binaries are included in the zip releases, or you can compile it yourself.

## Server-Side Recording

This feature is used to record games on a server. The recordings are created in a custom format which stores the game from every player's perspective, and can be converted to the standard demo format for playback later.

To enable recording, set record_auto_recording to 1. The recording will automatically start when players connect to the server, and stop when all players have disconnected. You can also manually start and stop recording for a single game by using the "record_start" and "record_stop" commands. Records are stored as .rec files sorted by date and time under the "records" directory where the server is running.

To view the available demos that can be extracted from a certain .rec file, use the record_scan <filename> command, where the filename is the path to the file within the records directory. Example: "record_scan 2018-02-08/16-07-27.rec". The output shows the client number and instance numbers. The client number is the number assigned to the client when they connected to the server. The instance numbers differentiate sessions when a client reconnected during a recording session, or one client disconnected and another client connected and was assigned the same number.

To convert the demo, use the record_convert <filename> <clientnum> <instance> command, with a valid client and instance as provided by the record_scan command. The output will be written to the file demos/output.efdemo under baseEF or the current mod directory.

## Admin Spectator Support

This feature allows admins to spectate players on the server without joining the server, which can be useful to monitor for cheating.

To enable this feature, set admin_spectator_enabled to 1 and admin_spectator_password to a password of your choosing on the server.

To connect to the server in spectator mode, set "password" on the client to "spect_" plus the password on the server. For example if admin_spectator_password is set to "xyz" on the server, then set the password value on the client to "spect_xyz" before connecting to the server in order to enter spectator mode.

# Compiling

You should be able to compile this project using the ioquake3 steps [here](http://wiki.ioquake3.org/Building_ioquake3), substituting the cMod files/git address for the ioquake3 ones. If this doesn't work send me an email.

# Credits

- Thilo Schulz - ioEF
- id Software and ioquake3 project - base engine
- Raven Software - Elite Force
