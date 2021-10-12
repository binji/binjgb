[![Github CI Status](https://github.com/binji/binjgb/workflows/CI/badge.svg)](https://github.com/binji/binjgb)

# binjgb

A simple GB/GBC emulator.

## Features

* [Runs in the browser using WebAssembly](https://binji.github.io/binjgb)
* Hacky-but-passable **CGB support**!
* Mostly-there **Super GB support**!
* Cycle accurate, passes many timing tests (see below)
* Supports MBC1, MBC1M, MMM01, MBC2, MBC3, MBC5 and HuC1
* Save/load battery backup
* Save/load emulator state to file
* **Fast-forward**, pause and step one frame
* **Rewind** and seek to specific cycle
* Disable/enable each audio channel
* Disable/enable BG, Window and Sprite layers
* Convenient Python test harness using hashes to validate
* (WIP) **Debugger** with various visualizations (see below)

## DMG Screenshots

![Bionic Commando](/images/bionic.png)
![Donkey Kong](/images/dk.png)
![Kirby's Dreamland 2](/images/kirby2.png)
![Mole Mania](/images/mole.png)
![Mario's Picross](/images/picross.png)
![Trip World](/images/trip.png)
![Wario Land](/images/wario.png)
![Game Boy Wars](/images/wars.png)
![Is That a Demo in Your Pocket?](/images/pocket.png)

## CGB Screenshots

![Dragon Warrior](/images/dw.png)
![Hamtaro](/images/ham.png)
![Metal Gear Solid](/images/mgs.png)
![It Came From Planet Zilog](/images/pz.png)
![Survival Kids](/images/sk.png)
![Aevilia](/images/aevilia.png)
![Toki Tori](/images/toki.png)
![Wario 3](/images/wario3.png)

## SGB Screenshots

![Donkey Kong](/images/dk-sgb.png)
![Kirby's Dreamland 2](/images/kirby2-sgb.png)
![Mole Mania](/images/mole-sgb.png)

## Debugger Screenshots

![Debugger](/images/debugger.png)
![OBJ](/images/obj-window.png)
![Map](/images/map-window.png)
![Tile Data](/images/tiledata-window.png)
![Breakpoints](/images/breakpoint.png)

## Embedding binjgb in your own web page

Copy `docs/simple.html`, `docs/simple.js` and `docs/simple.css` and your `.gb`
or `.gbc` file to your webserver. `simple.html` will fill the entire page, so
if you don't want that, you should put it into an
[iframe](https://developer.mozilla.org/en-US/docs/Web/HTML/Element/iframe).

The emulator will also display an on-screen gamepad if the device supports
touch events.

You can configure the emulator by editing `simple.js`:

| Constant name | Description |
| - | - |
| ROM_FILENAME | The path to your `.gb` or `.gbc` file |
| ENABLE_REWIND | Whether to enable rewinding with the backspace key |
| ENABLE_PAUSE | Whether to enable pausing with the space bar |
| ENABLE_SWITCH_PALETTES | Whether to enable switching palettes with `[` and `]` |
| OSGP_DEADZONE | How wide to make the deadzone for the onscreen gamepad, as a decimal between between `0` and `1` |
| CGB_COLOR_CURVE | How to tint the CGB colors so they look more like a real CGB. <ol><li>none</li><li>Use Sameboy's "Emulate Hardware" colors</li><li>Use Gambatte/Gameboy Online colors</li></ol> |
| DEFAULT_PALETTE_IDX | Which palette to use by default, as an index into `PALETTES` |
| PALETTES | An array of built-in palette IDs, between `0` and `83`. Useful if you only want the player to switch between a few of the built-in palettes |

See `simple.js` for more info.

## Cloning

Use a recursive clone, to include the submodules:

```
$ git clone --recursive https://github.com/binji/binjgb
```

If you've already cloned without initializing submodules, you can run this:

```
$ git submodule update --init
```

## Building

Requires [CMake](https://cmake.org) and
[SDL2](http://libsdl.org/download-2.0.php). Debugger uses
[dear imgui,](https://github.com/ocornut/imgui) (included as a git submodule).

### Building (Linux/Mac)

If you run `make`, it will run CMake for you and put the output in the `bin/`
directory.

```
$ make
$ bin/binjgb foo.gb
```

You can also just use cmake directly:

```
$ mkdir build
$ cd build
$ cmake ..
$ make
```

### Building (Windows)

When building on Windows, you'll probably have to set the SDL2 directory:

```
> mkdir build
> cd build
> cmake .. -G "Visual Studio 15 2017" -DSDL2_ROOT_DIR="C:\path\to\SDL\"
```

Then load this solution into Visual Studio and build it. Make sure to build the
`INSTALL` target, so the exectuables are built to the `bin` directory.

### Building WebAssembly

You can build binjgb as a WebAssembly module. You'll need an incoming build of
emscripten. See https://github.com/kripken/emscripten/wiki/WebAssembly and
http://kripken.github.io/emscripten-site/docs/building_from_source/index.html#installing-from-source.

Put a symlink to Emscripten in the `emscripten` directory, then run make.

```
$ ln -s ${PATH_TO_EMSCRIPTEN} emscripten
$ make wasm
```
Or set Makefile variables via command line:
```
$ make wasm EMSCRIPTEN_CMAKE="/path/to/Emscripten.cmake"
```

### Changing the Build Configuration

If you change the build config (e.g. update the submodules), you may need to run CMake again.
The simplest way to do this is to remove the `out/` directory.

```
$ rm -rf out/
$ make
```

## Running

```
$ bin/binjgb <filename>
$ bin/binjgb-debugger <filename>
```

Keys:

| Action | Key |
| --- | --- |
| DPAD-UP | <kbd>↑</kbd> |
| DPAD-DOWN | <kbd>↓</kbd> |
| DPAD-LEFT | <kbd>←</kbd> |
| DPAD-RIGHT | <kbd>→</kbd> |
| B | <kbd>Z</kbd> |
| A | <kbd>X</kbd> |
| START | <kbd>Enter</kbd> |
| SELECT | <kbd>Tab</kbd> |
| Quit | <kbd>Esc</kbd> |
| Save state | <kbd>F6</kbd> |
| Load state | <kbd>F9</kbd> |
| Toggle fullscreen | <kbd>F11</kbd> |
| Disable audio channel 1-4 | <kbd>1</kbd>-<kbd>4</kbd> |
| Disable BG layer | <kbd>B</kbd> |
| Disable Window layer | <kbd>W</kbd> |
| Disable OBJ (sprites) | <kbd>O</kbd> |
| Fast-forward | <kbd>Lshift</kbd> |
| Rewind | <kbd>Backspace</kbd> |
| Pause | <kbd>Space</kbd> |
| Step one frame | <kbd>N</kbd> |

## INI file

Binjgb tries to read from `binjgb.ini` on startup for configuration. The
following keys are supported:

```
# Load this file automatically on startup
autoload=filename.gb

# Set the audio frequency in Hz
audio-frequency=44100

# Set the number of audio frames per buffer
# lower=better latency, more pops/clicks
# higher=worse latency, fewer pops/clicks
audio-frames=2048

# Set to the index of a builtin palette
# (valid numbers are 0..82)
builtin-palette=0

# Force the emulator to run in DMG (original gameboy) mode.
# 0=Don't force DMG
# 1=Force DMG
force-dmg=0

# The number of video frames to display before storing a full dump of
# the emulator state in the rewind buffer. Probably best to leave this
# alone
rewind-frames-per-base-state=45

# The number of megabytes to allocate to the rewind buffer.
# lower=less memory usage, less rewind time
# higher=more memory usage, more rewind time
rewind-buffer-capacity-megabytes=32

# The speed at which to rewind the game, as a scale.
# 1=rewind at 1x
# 2=rewind at 2x
# etc.
rewind-scale=1.5

# How much to scale the emulator window at startup.
render-scale=4

# What to set the random seed to when initializing memory. Using 0
# disables memory randomization.
random-seed=0

# Whether to display the SGB border or not.
# 0=Don't display SGB border
# 1=Display SGB border, even if it doesn't exist.
sgb-border=0
```

The INI file is loaded before parsing the command line flags, so you can use
the command line to override the values in the INI file.

## Running tests

Run `scripts/build_tests.py` to download and build the necessary testsuites.
This works on Linux and Mac, not sure about Windows.

`scripts/tester.py` will only run the tests that match a filter passed on the
command line. Some examples:

```
# Run all tests
$ scripts/tester.py

# Run all tests mooneye tests
$ scripts/tester.py mooneye

# Run all gpu tests
$ scripts/tester.py gpu
```

## Test status

[See test results](test_results.md)
