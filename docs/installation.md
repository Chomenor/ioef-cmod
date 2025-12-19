# Installation

## Method 1 - Existing Installation

Download the [cMod release](https://github.com/Chomenor/ioef-cmod/releases), extract it into your Elite Force directory, and run the included application. In most cases your existing EF client will continue to work without conflicts, but if you want to be extra safe, make a backup of your EF directory before installing cMod.

## Method 2 - TLO Installer (Windows Only)

A simple installation method is to use the installer from The Last Outpost. This includes a working version of cMod and all necessary files. However, the installer is for Windows only, and the version of cMod included may not always be the latest.

The installer can be downloaded from [this link](https://last-outpost.net/dl/file?id=1).

## Method 3 - TLO Standalone Version (Mac, Windows, Linux)

This method creates a "portable" installation for any supported operating system.

- Download the [TLO Linux release](https://last-outpost.net/dl/file?id=9) and extract it to somewhere on your system.
- Download the [cMod release](https://github.com/Chomenor/ioef-cmod/releases) corresponding to your operating system. Extract it to the same location.
- Run the included application.

## Method 4 - Good Old Games (Windows Only)

If you have purchased Elite Force through Good Old Games, you can run cMod using these steps:

- Install Elite Force normally through Good Old Games (either via GOG Galaxy or the standalone installer).
- Download the [cMod release](https://github.com/Chomenor/ioef-cmod/releases) for Windows. Extract it to an empty folder anywhere on your system.
- Run the included application.

You can also extract cMod directly to the install directory (Method 1) but this way may be more convenient.

## Method 5 - Clean Install

- Download the [cMod release](https://github.com/Chomenor/ioef-cmod/releases) corresponding to your operating system. Extract it to an empty folder anywhere on your system.
- Within that folder there should be a subfolder called "baseEF". Copy the files "pak0.pk3", "pak1.pk3", "pak2.pk3" and "pak3.pk3" to this folder. If you don't have these paks available, you may be able to obtain them from the TLO [Linux release](https://last-outpost.net/dl/file?id=9).
- Run the included application.

A minimal install for Windows should look something like this:

![Directory Structure](img/client-structure.png)

# Linux Notes

The Linux releases require the SDL 2 library to be installed. On Ubuntu 24 you can use this command:

```
sudo apt install libsdl2-2.0-0
```

Depending on the Linux distribution, the exact command to install this package may vary, and there may be other packages required as well. The package requirements for cMod should generally be the same as for [ioquake3](https://ioquake3.org).
