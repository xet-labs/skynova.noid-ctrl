# _superseded

Files moved out of `lib/skynoid_logic/src/` on 2026-10-24 because they did not compile against the
current `skynoid_logic.h` and defined symbols that `drive.c` already defines, which broke
`pio test -e native` for every test. Nothing here is built (PlatformIO only compiles `lib/` and `src/`).

| file | why it was superseded | replaced by |
|---|---|---|
| `motion.c` | defined `skl_clamp`, `skl_mix` (struct return) and `skl_ramp` (3 args) in conflict with the header | `lib/skynoid_logic/src/drive.c` (`skl_mix`, `skl_ramp`, `skl_duty_permille`) |
| `hazard.c` | defined `skl_hazard_allows` a second time and used `SKL_HZ_FRONT_MASK`, which the header does not have | `lib/skynoid_logic/src/drive.c` (same rule, tested in `test_logic`) |

`ring.c` and `devstat.c` from the same earlier attempt were kept: they now include `skynoid_core.h`
(new header) and are tested in `test/test_core`.

Safe to delete once you have looked at them.
