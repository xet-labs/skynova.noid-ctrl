# AGENTS.md (skynoid-v1-ctrl)

Repo-specific rules. The full rules are in the vault: read `/x.obs/@projects/skynoid-v1/AGENTS.md` first, then `09-agents/conventions.md`.

- This repo is the **ESP32 firmware + canonical specs**. The Python side is `../skynoid-v1-cmd`.
- One step = explainer in `docs/steps/` + 30 to 120 lines of code + one commit `step N.M: what`.
- You cannot flash or measure. Say "run X, expect Y", never "it works".
- Pure logic goes in `firmware/lib/` (no ESP-IDF includes) and gets a host test in the same commit.
- `spec/` is canonical. Change the spec first, regenerate (`scripts/gen_vectors_h.py`), then copy `spec/vectors.json` to the cmd repo.
- Do not invent hardware numbers; mark `#verify` and ask. No servos, daemon or simulator in the sprint.
- Update the vault `09-agents/handoff-log.md` and the task Handoff after every step.
