# cinder

ESP-IDF firmware for the VIEWE round AMOLED knob: Ember's desk display (bot face, agent status, Pomodoro, weather, now playing) and later a BLE media remote. Server side: `~/Github/Ember` (read its `AGENTS.md` before changing it).

## Docs

- [`docs/workflow.md`](docs/workflow.md): issues, PRs, build, sdkconfig fixes, flashing, dev tools. Read before building or flashing.
- [`docs/llm.md`](docs/llm.md): verified pin map, board facts, toolchain decisions. Read before touching hardware, pins or the BSP.
- [`docs/features.md`](docs/features.md): design, measurements and user decisions per feature. Read the feature's section before changing it.
- [`docs/firmware-plan.md`](docs/firmware-plan.md): pages, input model, build order. Read before adding a page or input.
- `AGENTS.local.md` (gitignored; template `AGENTS.local.md.example`): toolchain notes, doc sources, notes location. Read before installing tools or editing `docs/enclosure.html`.

## Maintainer rules (live device)

These protect the maintainer's paired knob, machine and secrets. Agents working in the maintainer's checkout follow them as written; on your own knob, adapt them.

- **The knob is the user's live, paired device.** Never point it away from the live Ember server (`set_ember` to a scratch URL, a scratch device token, a test build that does either), re-mint it, change its Wi-Fi or factory-reset it without asking. If allowed, restore it and confirm a live checkin before you report.
- Flash app only (`app-flash erase-otadata`, see `docs/workflow.md`), under the flash lock; never erase NVS. A full `flash` (bootloader) only with the user's approval. Never toggle DTR/RTS yourself on the knob's USB port (no `idf.py monitor`); esptool's reset during a flash is expected.
- Never burn eFuses. Back up or verify `backup/` before anything that erases flash.
- Never print or commit `sdkconfig.secrets`, Wi-Fi passwords or tokens. Builds are secret-free; never upload a dev-seed build. Release images come only from CI (`tools/release.sh`); never upload a local build to channel `release`.
- Install software only after the user approves it.
- `vendor/` and `backup/` are local-only and git-ignored.

## Rules

- **No code comments.** The why goes in the commit message, PR description or `docs/features.md`. Exceptions, one line each: hardware/protocol quirks that look like bugs (name the quirk, point at the doc), a header's non-obvious contract (units, ownership, ISR/task context), generated-file headers and licence notes.
- Conventional Commits; no `Co-Authored-By` trailers.
