# Step 1.2: the wire codec, in C and in Python

**Goal (one line):** the ESP32 and the Pi turn the same message into the same bytes, and reject damaged ones.

## The idea, in plain words
The ESP32 and the Pi talk over one USB serial cable, which is just a stream of bytes with no "message start". So every message is wrapped in a **frame** that answers three questions for the receiver: where does it begin and end, is it intact, and what does it say.

```
 what you want to say          what goes on the cable
 ---------------------         ------------------------------------------
 type, msg, seq, payload  -->  00 | COBS( header | payload | CRC ) | 00
                                ^                                    ^
                                delimiter: the ONLY zero bytes on the wire
```

- **Header** (10 bytes): version, type, msg id, flags, seq, ack, payload length.
- **CRC16** (2 bytes): a checksum over header + payload. One flipped bit changes it, so the receiver drops the frame.
- **COBS**: rewrites the body so it contains no `0x00`. Then `0x00` can only mean "frame edge", and a receiver that starts in the middle of a stream is back in step at the next `0x00`. Example: `11 22 00 33` becomes `03 11 22 02 33`.
- **Text vs frames:** a byte that arrives *outside* a frame is plain text (console typing, boot messages). That is how one UART carries both a binary link and a human console (step 1.3).

Both sides are written twice (C for the ESP32, Python for the Pi) and checked against **one file of fixed examples**, `spec/vectors.json`. If both pass every row, they speak the same bytes.

## Files you will touch
| file | what |
|---|---|
| `spec/vectors.json` | the fixed examples (from vault `protocol-v0` section 8). The single source of truth |
| `scripts/gen_vectors_h.py` | turns the JSON into `firmware/test/test_wire/vectors.h` for the C test |
| `firmware/lib/skynoid_wire/` | the C codec: `crc16.c`, `cobs.c`, `frame.c` (frame build, decode, byte parser) |
| `firmware/test/test_wire/` | the C test |
| `../skynoid-v1-cmd/skynoid/wire/codec.py` | the Python codec |
| `../skynoid-v1-cmd/tests/test_wire.py` | the Python test |
| `../skynoid-v1-cmd/spec/vectors.json` | a **copy** of the vectors; keep identical to this repo's file |

## What you will see when it works
```
cd skynoid-v1-ctrl
python3 scripts/gen_vectors_h.py --check        -> vectors.h is up to date
cd firmware && pio test -e native               -> 11 test cases: 11 succeeded
cd ../../skynoid-v1-cmd && python -m pytest     -> 37 passed
```
(Exact PlatformIO wording differs by version; what matters is that nothing fails.)

## If it does not work (three likeliest causes)
1. `pio test -e native` cannot find `skynoid_wire.h`: PlatformIO did not pick up `lib/skynoid_wire/include`. Tell me the error; the fix is a one-line `library.json` or `-I` flag.
2. `vectors.h is out of date`: someone edited the JSON or the header. Run `python3 scripts/gen_vectors_h.py`.
3. A Python row fails but C passes (or the reverse): the two `vectors.json` files differ. `diff` them.

## Checkpoint (answer in your own words)
Why does COBS let the receiver recover from garbage in the middle of the stream, when a plain "length prefix" would not?

---
**Status notes for the record.** The C library and C test were already in this folder when I started (written earlier, not run by me on your machine). In my sandbox I compiled them with a stand-in for Unity and all 11 tests passed against the vectors; I wrote the Python codec and its 37 tests, which pass there too. I could not run `pio` or touch the board, so please run the three commands above: the real PlatformIO run is the one that counts.
