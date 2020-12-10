# Project Description

cMod is an ioEF-based multiplayer client and server for Star Trek: Voyager - Elite Force, intended to add new features and fix bugs and limitations in the standard ioEF implementation.

Author: Noah Metzger <chomenor@gmail.com>

Feel free to contact me if you have any questions, feedback or bug reports.

# Client

To use the client, download the latest release zip, unzip it into your Elite Force install directory, and run the ioEF-cMod* application. In most cases your existing EF client will continue to work without conflicts, but if you want to be extra safe, make a backup of your EF directory before installing cMod.

## Settings Handling

Settings are managed differently in cMod than other versions of EF. Here is an overview of how the cMod settings system works.

- The settings are stored in a file called "cmod.cfg" directly in the EF working directory. This is separate from the settings files used by other versions of EF, such as hmconfig.cfg. The cmod.cfg file is designed to be human-editable, so feel free to open it and make changes.

- When you first run cMod (specifically, if the "cmod.cfg" file does not exist), cMod will attempt to import your binds and a limited subset of settings from an existing hmconfig.file and transfer them to cmod.cfg. If you want to reset cMod to a clean configuration, manually empty (but not delete) the cmod.cfg file.

- By default, cMod only loads a limited subset of settings from the autoexec.cfg file. This is to protect against autoexec.cfg files that are prepackaged in EF distributions and may be incompatible with cMod. If you created an autoexec.cfg file yourself and believe it is compatible with cMod, set cmod_restrict_autoexec to 0 (in the console or cmod.cfg, not in the autoexec.cfg itself).

## Directory Handling

By default, cMod will use the install location as the read/write directory, like the original version of Elite Force. This allows you to have multiple EF installations that use separate settings. The ioEF homepath location, which is %appdata%\STVEF on Windows, is used as a secondary read location in case you have any files left over there from a previous ioEF installation.

If cMod is run from a location where it does not have write permissions, such as under Program Files on Windows, it will revert to the standard ioEF behavior and use the homepath as the write location.

## Mod Handling

For stability reasons, cMod prioritizes the official game pk3s (pak0-pak3) over other pk3s in baseEF regardless of filename. This means certain kinds of mod pk3s may not work out of baseEF. To use such mods in cMod, create a folder called "basemod" alongside baseEF and place the mod pk3s in that folder instead of baseEF.

Typical maps and models still work normally out of baseEF, and mods in other directories loaded via the mod menu or fs_game also work normally.

## Brightness Settings

The following settings are supported in cMod to adjust the brightness of the game.

- r_gamma: Adjusts overall game brightness. This is the default brightness setting changed by the in-game brightness slider.  
**Default:** 1.4 **Original EF Default:** 1.0 **Recommended range:** 1.0 - 2.0

- r_overBrightFactor: Adjusts lighting contrast (maximum lighting intensity).  
**Default:** 1.5 **Original EF Default:** 2.0 **Recommended range:** 1.0 - 2.0

- r_mapLightingGamma: Adjusts average lighting intensity.  
**Default:** 1.0 **Original EF Default:** 1.0 **Recommended range:** 1.0 - 2.0

There are other settings that can modify brightness, but these are the ones that had best results in my testing. If you have additional questions about brightness customization feel free to send me an email.

## Custom Fastsky Color

cMod supports customizing the color of the sky in fastsky mode (when r_fastsky is set to 1). The color is controlled by the r_fastskyColor cvar and specified in RGB hex format. You can experiment with different combinations or use an online RGB picker tool that generates a standard 6-digit hex value to choose the color. Some sample values are as follows:

- `ccb366` - yellow (original Elite Force default)
- `777755` - darker shade of yellow
- `777777` - light gray
- `000000` - black (original ioEF default)
- `ffffff` - white

## Resolution Handling

cMod uses separate resolution mode settings for fullscreen and windowed display.

- r_mode: Applies when display is in windowed mode (default 720x480).
- r_fullscreenMode: Applies when display is in fullscreen mode (default -2).

The most common values which you can use for these settings are as follows:

- \[width\]x\[height\]: Specifies a specific custom resolution.
- -2: Uses the current maximum resolution of your display.
- -1: Uses the resolution specified by r_customWidth and r_customHeight.
- 3: Uses a 640x480 resolution (the original EF default).

## Font Scaling

In the original game, the console and chat font are displayed at a fixed size, unlike other graphical components that scale with the resolution of the display. This can lead to the font appearing too small on high resolution displays. cMod adds a new setting called "cmod_font_scaling" intended to help compensate for this effect.

- A setting of 1 scales the font to the same relative size as the original game running at 640x480 (the original default resolution).
- A setting between 0 and 1 scales to some fraction of the font size represented by 1. However it will not scale below the original unscaled font size.
- A setting of 0 produces the original unscaled font size.

## Download Folder Support

The "fs_download_mode" setting can be used to customize pk3 download handling on the client. It supports the following values:

- 0 (default): Pk3 files are downloaded to the normal location.
- 1: Pk3 files are saved to the "downloads" folder within the target mod directory. For example, "baseEF/somefile.pk3" will be rewritten to "baseEF/downloads/somefile.pk3". Pk3s in the downloads folder are deprioritized compared to pk3s directly in the mod directory.
- 2: Same as 1, but also disables loading certain less secure content (cfg files and qvm files that don't match a list of trusted mod hashes) from the downloads folder.

A setting of 1 can help organize downloaded pk3s and reduce the chance of pk3 conflicts. A setting of 2 can help increase security when downloading from untrusted servers, but may cause mod compatibility issues.

## Anti Burn-in Support

The setting "cmod_anti_burnin" can be used to fade the in-game HUD graphics, to reduce possible burn-in effects on OLED-type displays. Valid values range from 0 (disabled) to 1 (maximum).

## Crosshair Handling

cMod uses a new crosshair indexing system, which allows all the installed crosshairs to be accessed from the crosshair menu at once, and also adds new built-in crosshairs.

If you encounter crosshair-related issues, you can try disabling this feature by setting cmod_crosshair_enable to 0.

# Server

The separate dedicated server application is recommended for hosting servers. You can use the binaries included in the zip releases or compile them yourself. Make sure you are using the latest cMod version!

## Settings Handling

The cMod dedicated server builds do not automatically load any settings files, such as cmod.cfg, hmconfig.cfg, or autoexec.cfg. No settings files are automatically written either. The dedicated server is meant to be launched with a startup config file manually specified on the command line. For example, if your startup config is located at baseEF/startup.cfg, you can use a command such as this to start the server:

```./cmod_dedicated.x86_64 +exec startup```

## Source Directory Handling

By default the dedicated server uses the same homepath/basepath configuration as the client, which may not be ideal for server configurations. However the default can easily be overridden on the command line. For example, this command sets a single absolute path to use as the filesystem root.

```./cmod_dedicated.x86_64 +set fs_dirs *fs_basepath +set fs_basepath /home/efuser/efinstall +exec startup```

In that example, the startup.cfg file would be expected to be in a location such as ```/home/efuser/efinstall/baseEF/startup.cfg```. For more detailed information about the source directory configuration options, refer to the filesystem documentation [here](new-filesystem-readme.md#source-directory-options).

## Mod Directory Handling

As with the client, the cMod dedicated server has special prioritization of the Raven paks (pak0 - pak3) in the baseEF folder. Therefore some mod loading methods involving baseEF may not work in cMod. The following methods are recommended to correctly load mods on cMod servers and should work for most mods.

### Server-Side Mods

If you have a mod that is entirely server-side, such as Gladiator, EFAdmin, and many other mods, or a custom mod that you compiled yourself, then follow these steps for loading the mod.

- Create a directory called "servercfg" alongside baseEF in your server install directory. This directory is handled specially by cMod so that its contents override baseEF, similar to "basemod" on the client.
- If your mod consists of one or more pk3 files, simply place them in the servercfg folder.
- If your mod consists of a qagame.qvm file, place it in the following location: "servercfg/vm/qagame.qvm"
- Don't set any fs_game setting on the command line.

Although the fs_game method below should work as well, the above approach is preferred for server-side mods because it avoids some unwanted side effects on clients that can be caused by changing fs_game.

### Client-Side Mods

If you have a client-side mod that depends on certain pk3s being loaded on the client, use the standard method of mod loading, which involves setting fs_game.

- Extract the mod so that it is saved to a directory alongside baseEF in your server install directory.
- Load the mod by including ```+set fs_game [mod-directory]``` in the server startup command.

If your server supports downloads (sv_allowDownload enabled) all pk3s in the mod directory will be included by default in the list of pk3s to be downloaded to clients.

## Server-Side Recording

This feature is used to record games on a server. The recordings are created in a custom format which stores the game from every player's perspective, and can be converted to the standard demo format for playback later.

To enable recording, set record_auto_recording to 1. The recording will automatically start when players connect to the server, and stop when all players have disconnected. You can also manually start and stop recording for a single game by using the "record_start" and "record_stop" commands. Records are stored as .rec files sorted by date and time under the "records" folder in the server directory.

To view the available demos that can be extracted from a certain .rec file, use the record_scan <filename> command, where the filename is the path to the file within the records directory. Example: "record_scan 2018-02-08/16-07-27.rec". The output shows the client number and instance numbers. The client number is the number assigned to the client when they connected to the server. The instance numbers differentiate sessions when a client reconnected during a recording session, or one client disconnected and another client connected and was assigned the same number.

To convert the demo, use the record_convert <filename> <clientnum> <instance> command, with a valid client and instance as provided by the record_scan command. The output will be written to the file demos/output.efdemo under baseEF or the current mod directory.

## Admin Spectator Support

This feature allows admins to spectate players on the server without joining the server, which can be useful to monitor for cheating.

To enable this feature, set admin_spectator_password to a password of your choosing on the server.

To connect to the server in spectator mode, set "password" on the client to "spect_" plus the password on the server. For example if admin_spectator_password is set to "xyz" on the server, then set the password value on the client to "spect_xyz" before connecting to the server in order to enter spectator mode.

# Compiling

You should be able to compile this project using the ioquake3 steps [here](http://wiki.ioquake3.org/Building_ioquake3), substituting the cMod files/git address for the ioquake3 ones. If this doesn't work send me an email.

# FAQ / Common Problems

**Q:** Problems opening console.  
**A:** There are known issues with the normal console key on some keyboard layouts. Try using the alternative combination, Shift + ESC to open the console.

**Q:** Problems opening ingame chat.  
**A:** There is a known bug in cMod versions prior to 1.08. Make sure you are using the latest version of cMod.

**Q:** Settings "r_overBrightBits" and "r_mapOverBrightBits" not working.  
**A:** cMod uses r_overBrightFactor and r_mapLightingFactor instead, with the following mapping:

r_overBrightFactor = 2 ^ r_overBrightBits  
r_mapLightingFactor = 2 ^ r_mapOverBrightBits

For example, a setting of r_mapOverBrightBits 3 in another client translates to r_mapLightingFactor 8 in cMod.

**Q:** Problems with jerkiness / low framerates.  
**A:** By default, cMod uses higher graphics settings than the original version of EF. These settings work best on newer computers and those with at least low end gaming-grade video cards. Check your video card/GPU model at [videocardbenchmark.net](http://videocardbenchmark.net). I recommend a score of 600 minimum, and 1200+ ideally, to run EF at full detail and 1080p resolution. If your hardware scores below these numbers you might consider upgrading your PC/video card.

If you are having trouble with framerates in cMod you can try lowering the graphics quality settings. Two of the most significant settings that might increase framerates (at the expense of graphics quality) are r_picmip 1 and r_ext_texture_filter_anisotropic 0.

**Q:** Mouse movement is different from original EF.  
**A:** By default, cMod tries to use raw input mode for mouse input, which bypasses OS acceleration settings and is generally more accurate for FPS games. If you prefer the mouse input to be the same as older versions of EF, try setting in_mouse_warping to 1 and restart cMod.

# Credits

- Thilo Schulz - ioEF
- id Software and ioquake3 project - base engine
- Raven Software - Elite Force
