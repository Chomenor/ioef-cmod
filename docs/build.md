# Compiling

The cMod project is divided into two parts, the engine and game modules. Both are included in the release packages but currently use separate build processes.

## Engine

The following methods can be used to build the engine (main game executable). Generally the build process is the same as for [ioquake3](https://ioquake3.org/help/building-ioquake3/).

### Building for Windows (Linux Cross Compile)

This method creates Windows builds using a Linux system or VM. These commands have been tested to work on Ubuntu 22. The package installation may be different in other environments, but otherwise the process should be similar.

```
sudo apt install git build-essential gcc-mingw-w64
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
make PLATFORM=mingw32 ARCH=x86_64
```

### Building for Windows (Cygwin)

This method can be used to build directly on Windows.

1) Download Cygwin from [https://cygwin.com/install.html](https://cygwin.com/install.html) and run the installer.
2) Select the following packages during installation: ```git```, ```make```, ```mingw64-i686-gcc-core```, ```mingw64-x86_64-gcc-core```
3) Open the Cygwin64 Terminal and run the following commands:

```
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
make PLATFORM=mingw32 ARCH=x86_64
```

### Building for Linux

The following commands can be used to build on Ubuntu 22. Package installation may vary on other environments.

```
sudo apt install git build-essential libsdl2-dev
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
make ARCH=x86_64
```

### Building for Mac

You will need Xcode Command Line Tools installed. Typically you will be automatically prompted to install this if it is not already installed. Start by cloning the repository:

```
git clone https://github.com/Chomenor/ioef-cmod.git
cd ioef-cmod
```

Then run one of these commands, depending on platform (only 64-bit x86 and arm architectures are currently supported).

- x86_64: ```./make-macosx.sh x86_64```
- arm64: ```./make-macosx.sh arm64```
- universal (x86_64 + arm64): ```./make-macosx-ub2.sh```

### Feature Selection

All changes in cMod compared to the base ioquake3 source code are listed in the [cmod_defs.h](https://github.com/Chomenor/ioef-cmod/blob/master/code/cmod/cmod_defs.h) file. Specific features can be disabled by commenting out the corresponding ```#define``` statement.

## Game Modules

The game modules are included in the cMod releases via the pakcmod pk3 file. If you have built the engine from source, you can simply copy the pakcmod pk3 from the latest cMod release to your baseEF folder. The pk3 is platform-neutral.

### Building QVM Modules

Currently the build process for the game modules is for Windows only. This is a holdover from the fact that the original Raven game SDK was based on a Windows toolchain.

1) Download the contents of the [cmod-game-code](https://github.com/Chomenor/cmod-game-code) repository.
2) Run the ```build/qvm_build/build_all.bat``` script. The ```build/qvm_build/output/vm``` folder should now contain 3 files: ```cgame.qvm```, ```qagame.qvm```, and ```ui.qvm```.
3) Add the ```vm``` folder to a zip file, and rename it to a pk3 extention, such as ```mod.pk3```. Note that vm folder must be included in the pk3, so the path inside the pk3 will be like ```vm/cgame.qvm```.

### Loading Modules (Standard Method)

This method can be used to load the modules on any Elite Force engine.

1) Create a folder in your EF installation, next to baseEF. It could have any name, "testmod" for example.
2) Copy the "mod.pk3" file from the previous step into this folder.
3) Start the game, click the ```MODS``` button, and select the mod from the list.

### Loading Modules (Engine Selection)

In the cMod releases, the game module pk3 is located in baseEF and loaded specially by the engine. This allows the cMod modules and features to work when playing on remote servers when they would otherwise not be loaded by the standard mod method.

To enable this using a custom-built game module pk3, follow these steps:

1) Copy the module pk3 file into baseEF.
2) Start the game and run the ```/path``` command from the console. Locate the name of the module pk3 in the output, and note the integer hash value beneath it.
3) Edit the [fslocal.h](https://github.com/Chomenor/ioef-cmod/blob/master/code/filesystem/fslocal.h) file. Locate the ```CMOD_PAKS``` definition, and add the new hash value to the *bottom* of this list.
4) Recompile the engine.
