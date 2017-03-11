[![Travis](https://travis-ci.org/binji/binjgb.svg?branch=master)](https://travis-ci.org/binji/binjgb) [![AppVeyor](https://ci.appveyor.com/api/projects/status/github/binji/binjgb?branch=master&svg=true)](https://ci.appveyor.com/project/binji/binjgb/branch/master)

# binjgb

A simple GB emulator.

## Features

* Cycle accurate, passes many timing tests (see below)
* Supports MBC1, MBC1M, MMM01, MBC2, MBC3, MBC5 and HuC1
* Save/load battery backup
* Save/load emulator state to file
* Fast-forward, pause and step one frame
* Disable/enable each audio channel
* Disable/enable BG, Window and Sprite layers
* Convenient Python test harness using hashes to validate

## Screenshots

![Bionic Commando](/images/bionic.png)
![Donkey Kong](/images/dk.png)
![Kirby's Dreamland 2](/images/kirby2.png)
![Mole Mania](/images/mole.png)
![Mario's Picross](/images/picross.png)
![Trip World](/images/trip.png)
![Wario Land](/images/wario.png)
![Game Boy Wars](/images/wars.png)
![Is That a Demo in Your Pocket?](/images/pocket.png)

## Building

Requires [CMake](https://cmake.org) and
[SDL2](http://libsdl.org/download-2.0.php).

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
> cmake .. -G "Visual Studio 14 2015" -DSDL2_ROOT_DIR="C:\path\to\SDL\"
```

Then load this solution into Visual Studio and build it. Make sure to build the
`INSTALL` target, so the exectuables are built to the `bin` directory.

## Running

```
$ bin/binjgb <filename>
```

Keys:

| Action | Key |
| --- | --- |
| DPAD-UP | up |
| DPAD-DOWN | down |
| DPAD-LEFT | left |
| DPAD-RIGHT | right |
| B | z |
| A | x |
| START | enter |
| SELECT | backspace |
| Quit | escape |
| Save state | F6 |
| Load state | F9 |
| Toggle fullscreen | F11 |
| Disable audio channel 1-4 | 1-4 |
| Disable BG layer | b |
| Disable Window layer | w |
| Disable OBJ (sprites) | o |
| Fast-forward | tab |
| Pause | space |
| Step one frame | n |

## Running tests

You'll have to get the various test suites below, build the `.gb` files if
necessary, and copy the results to the appropriate directories. See
`scripts/tester.py` for the expected locations. Probably should write a script
that does this automatically.

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
