# skynoid-v1-ctrl

SkyNoid V1 **controller**: the ESP32 firmware (ESP-IDF on PlatformIO, C11) plus the canonical machine-readable specs.
Its partner repo is `../skynoid-v1-cmd` (the Python "commander" that runs on the Pi / laptop and talks to this firmware).

Design docs live in the Obsidian vault: `/x.obs/@projects/skynoid-v1/` (start at `AGENTS.md` there).

## Quick start
```
cd firmware
pio test -e native            # host tests of the pure-C libraries, no board needed
pio run -e single             # build for the ESP32
pio run -e single -t upload   # flash
pio device monitor            # 115200 baud
python3 ../scripts/gen_vectors_h.py --check   # test vectors header in sync with spec/
```

## Map
| folder | what |
|---|---|
| `firmware/src/` | ESP32 code (`main.c` for now; by domain as steps add it) |
| `firmware/lib/skynoid_wire/` | pure-C wire codec: CRC16, COBS, frames, parser (no ESP-IDF includes) |
| `firmware/lib/skynoid_text/` | pure-C console text core: line editor, tokenizer, number parser (no ESP-IDF includes) |
| `firmware/test/` | host tests (`pio test -e native`) |
| `spec/` | **canonical** contracts. `vectors.json` today; pins/devices/params arrive with their steps |
| `scripts/` | repo scripts (`gen_vectors_h.py`) |
| `docs/steps/` | one explainer per step, read it before the code |

## Two-repo split
`ctrl` = everything that runs on or specifies the ESP32. `cmd` = everything that runs on the Pi or laptop. The vault's `repo-layout` note assumed one repo (`skynoid-v1-hw`); this is the split in use. Canonical specs live here in `spec/`; `cmd/spec/` holds copies and must stay identical.

## Status
Step 1.1 skeleton written, not yet built on the bench. Step 1.2 (codec, C side) done and checked off-board. Step 1.3a (console text core) written and checked off-board. See the vault `handoff-log` for the current state.
