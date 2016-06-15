# binjgb

A simple GB emulator.

## Building

Requires [CMake](https://cmake.org) and [SDL 1.2](https://www.libsdl.org/download-1.2.php).
If you run `make`, it will run CMake for you and put the output in the `out/`
directory.

## Running

```
$ out/binjgb <filename>
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
| Disable audio channel 1-4 | 1-4 |
| Disable BG layer | b |
| Disable Window layer | w |
| Disable OBJ (sprites) | o |

## Test status

Blaarg's tests:

| Test | Result |
| --- | --- |
| cpu\_instrs | :ok: |
| dmg\_sound-2 | :ok: |
| instr\_timing | :ok: |
| mem\_timing-2 | :ok: |
| oam\_bug-2 | :x: |

Haven't tried gekkio's tests yet, but I'm guessing most fail.
