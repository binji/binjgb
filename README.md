# binjgb

A simple GB emulator.

## Features

* One file, less than 5000 lines of C!
* Cycle accurate, passes many timing tests (see below)
* Supports MBC1, MBC2 and MBC3
* Save/load battery backup
* Save/load emulator state to file
* Fast-forward, pause and step one frame
* Disable/enable each audio channel
* Disable/enable BG, Window and Sprite layers
* Convenient Python test harness using hashes to validate

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
| Save state | F6 |
| Load state | F9 |
| Disable audio channel 1-4 | 1-4 |
| Disable BG layer | b |
| Disable Window layer | w |
| Disable OBJ (sprites) | o |
| Fast-forward | tab |
| Pause | space |
| Step one frame | n |

## Running tests

You'll have to get the various test suites below, build the `.gb` files if
necessary, and copy the results to the appropriate directories. See `tester.py`
for the expected locations. Probably should write a script that does this
automatically.

`tester.py` will only run the tests that match a filter passed on the command
line. Some examples:

```
# Run all tests
$ ./tester.py

# Run all tests mooneye tests
$ ./tester.py mooneye

# Run all gpu tests
$ ./tester.py gpu
```

## Test status

[Blargg's tests](http://gbdev.gg8.se/wiki/articles/Test_ROMs):

| Test | Result |
| --- | --- |
| cpu\_instrs | :ok: |
| dmg\_sound-2 | :ok: |
| instr\_timing | :ok: |
| mem\_timing-2 | :ok: |
| oam\_bug-2 | :x: |

[Gekkio's mooneye-gb tests](https://github.com/Gekkio/mooneye-gb):

| Test | Result |
| --- | --- |
| acceptance/add\_sp\_e\_timing | :ok: |
| acceptance/boot\_hwio-G | :ok: |
| acceptance/boot\_regs-dmg.gb | :ok: |
| acceptance/call\_cc\_timing2 | :ok: |
| acceptance/call\_cc\_timing | :ok: |
| acceptance/call\_timing2 | :ok: |
| acceptance/call\_timing | :ok: |
| acceptance/di\_timing-GS | :ok: |
| acceptance/div\_timing | :ok: |
| acceptance/ei\_timing | :ok: |
| acceptance/halt\_ime0\_ei | :ok: |
| acceptance/halt\_ime0\_nointr\_timing | :ok: |
| acceptance/halt\_ime1\_timing2-GS | :ok: |
| acceptance/halt\_ime1\_timing | :ok: |
| acceptance/if\_ie\_registers | :ok: |
| acceptance/intr\_timing | :ok: |
| acceptance/jp\_cc\_timing | :ok: |
| acceptance/jp\_timing | :ok: |
| acceptance/ld\_hl\_sp\_e\_timing | :ok: |
| acceptance/oam\_dma\_restart | :ok: |
| acceptance/oam\_dma\_start | :ok: |
| acceptance/oam\_dma\_timing | :ok: |
| acceptance/pop\_timing | :ok: |
| acceptance/push\_timing | :ok: |
| acceptance/rapid\_di\_ei | :ok: |
| acceptance/ret\_cc\_timing | :ok: |
| acceptance/reti\_intr\_timing | :ok: |
| acceptance/reti\_timing | :ok: |
| acceptance/ret\_timing | :ok: |
| acceptance/rst\_timing | :ok: |
| acceptance/bits/mem\_oam | :ok: |
| acceptance/bits/reg\_f | :ok: |
| acceptance/bits/unused\_hwio-GS | :ok: |
| acceptance/gpu/hblank\_ly\_scx\_timing-GS | :x: |
| acceptance/gpu/intr\_1\_2\_timing-GS | :ok: |
| acceptance/gpu/intr\_2\_0\_timing | :ok: |
| acceptance/gpu/intr\_2\_mode0\_timing | :ok: |
| acceptance/gpu/intr\_2\_mode0\_timing\_sprites | :x: |
| acceptance/gpu/intr\_2\_mode3\_timing | :ok: |
| acceptance/gpu/intr\_2\_oam\_ok\_timing | :ok: |
| acceptance/gpu/stat\_irq\_blocking | :ok: |
| acceptance/gpu/vblank\_stat\_intr-GS | :ok: |
| acceptance/timer/div\_write | :ok: |
| acceptance/timer/rapid\_toggle | :ok: |
| acceptance/timer/tim00\_div\_trigger | :ok: |
| acceptance/timer/tim00 | :ok: |
| acceptance/timer/tim01\_div\_trigger | :ok: |
| acceptance/timer/tim01 | :ok: |
| acceptance/timer/tim10\_div\_trigger | :ok: |
| acceptance/timer/tim10 | :ok: |
| acceptance/timer/tim11\_div\_trigger | :ok: |
| acceptance/timer/tim11 | :ok: |
| acceptance/timer/tima\_reload | :ok: |
| acceptance/timer/tima\_write\_reloading | :x: |
| acceptance/timer/tma\_write\_reloading | :x: |
| emulator-only/mbc1\_rom\_4banks | :ok: |
| manual-only/sprite\_priority | :ok: |

[Wilbert Pol's mooneye-gb tests](https://github.com/wilbertpol/mooneye-gb):

| Test | Result |
| --- | --- |
| acceptance/gpu/hblank\_ly\_scx\_timing\_nops | :x: |
| acceptance/gpu/intr\_0\_timing | :ok: |
| acceptance/gpu/intr\_1\_timing | :ok: |
| acceptance/gpu/intr\_2\_mode0\_scx1\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx2\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx3\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx4\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx5\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx6\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx7\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_scx8\_timing\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_timing\_sprites\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_timing\_sprites\_scx1\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_timing\_sprites\_scx2\_nops | :x: |
| acceptance/gpu/intr\_2\_mode0\_timing\_sprites\_scx3\_nops | :x: |
| acceptance/gpu/intr\_2\_timing | :ok: |
| acceptance/gpu/lcdon\_mode\_timing | :ok: |
| acceptance/gpu/ly00\_01\_mode0\_2 | :ok: |
| acceptance/gpu/ly00\_mode0\_2-GS | :ok: |
| acceptance/gpu/ly00\_mode1\_0-GS | :ok: |
| acceptance/gpu/ly00\_mode2\_3 | :ok: |
| acceptance/gpu/ly00\_mode3\_0 | :ok: |
| acceptance/gpu/ly143\_144\_145 | :ok: |
| acceptance/gpu/ly143\_144\_152\_153 | :ok: |
| acceptance/gpu/ly143\_144\_mode0\_1 | :ok: |
| acceptance/gpu/ly143\_144\_mode3\_0 | :ok: |
| acceptance/gpu/ly\_lyc-GS | :ok: |
| acceptance/gpu/ly\_lyc\_0-GS | :ok: |
| acceptance/gpu/ly\_lyc\_0\_write-GS | :ok: |
| acceptance/gpu/ly\_lyc\_144-GS | :ok: |
| acceptance/gpu/ly\_lyc\_153-GS | :ok: |
| acceptance/gpu/ly\_lyc\_153\_write-GS | :ok: |
| acceptance/gpu/ly\_lyc\_write-GS | :ok: |
| acceptance/gpu/ly\_new\_frame-GS | :ok: |
| acceptance/gpu/stat\_write\_if-GS | :x: |
| acceptance/gpu/vblank\_if\_timing | :ok: |
| acceptance/timer/timer\_if | :ok: |
