# Project Description

cMod is a client and server for Star Trek: Voyager - Elite Force, intended to add new features and fix bugs and limitations in the standard ioEF implementation.

Author: Noah Metzger <chomenor@gmail.com>

Feel free to contact me if you have any questions, feedback or bug reports!

This project is based on ioEF by Thilo Schulz.

# Client

To use this project, download the latest release zip, unzip it into your Elite Force install directory, and run the application.

## Console Key Notes

There are currently some issues with the console-opening key on non-English keyboard layouts. If you experience this problem, try using the alternative combination to open the console, Shift + ESC.

If this doesn't work or you would prefer a single-key fix, send me an email and I'll try to find a fix that works for your keyboard layout.

## Settings Handling

The settings for the cMod client are stored in the cmod.cfg file, located directly in the install directory. This file is separate from the hmconfig.cfg files most other versions of EF use, which allows cMod to use its own default settings without interfering with the settings of other EF versions.

The cmod.cfg file is designed to be human-editable, so feel free to open it in a text editor and make changes.

By default, cMod restricts which settings can be loaded from autoexec.cfg files. This is to protect against config files designed for earlier versions of EF and not fully compatible with cMod, especially config files from online templates or third-party apps. To disable this protection, set cmod_restrict_autoexec to 0 (in the console, not in the autoexec.cfg itself).

I don't recommend directly executing an hmconfig.cfg file from other versions of EF in cMod, as this can also lead to problems and sub-optimal performance. For best results I recommend manually transferring settings you want to cMod on a case-by-case basis, then making a backup of cmod.cfg in case you need to restore your settings later.

## Directory Handling

By default, cMod will use the install location as the read/write directory, like the original version of Elite Force. This allows you to have multiple EF installations that use separate settings. The ioEF homepath location, which is %appdata%\STVEF on Windows, is used as a secondary read location in case you have any files left over there from a previous ioEF installation.

If cMod is run from a location where it does not have write permissions, such as under Program Files on Windows, it will revert to the standard ioEF behavior and use the homepath as the write location.

## Resolution Handling

cMod uses separate mode settings for fullscreen and windowed display.

- r_mode: Applies when display is in windowed mode (default 3).
- r_fullscreenMode: Applies when display is in fullscreen mode (default -2).

The most common values which you can use for these settings are as follows:

- -2: Uses the current maximum resolution of your display.
- -1: Uses the resolution specified by r_customWidth and r_customHeight.
- 3: Uses a 640x480 resolution (the original EF default).

## Graphics Detail Settings

By default, cMod uses higher graphics settings than the original version of EF. These settings work best on newer computers and those with at least low end gaming-grade video cards. Check your video card/GPU model at videocardbenchmark.net. I recommend a score of 600 minimum, and 1200+ ideally, to run EF at full detail and 1080p resolution. If your hardware scores below these numbers you might consider upgrading your PC/video card.

If you are having trouble with framerates in cMod you can try lowering the graphics quality settings. Two of the most significant settings that might increase framerates (at the expense of graphics quality) are r_picmip 1 and r_ext_texture_filter_anisotropic 0. You can also try reducing the resolution using the settings in the previous section.

## Crosshair Handling

cMod uses a new crosshair indexing system, which allows all the installed crosshairs to be accessed from the crosshair menu at once, and also adds new built-in crosshairs.

If you encounter crosshair-related issues, you can try disabling this feature by setting cmod_crosshair_enable to 0.

# Server

It is possible to use cMod for hosting servers, but this is currently very experimental. Let me know if you encounter any bugs. I hope to add more features, documentation, and server config templates sometime in the future.

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
