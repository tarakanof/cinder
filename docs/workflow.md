# Workflow: issue to flashed knob

The issue → spec → worktree → PR → review → merge loop is the same as Ember's (specs are the maintainer's private notes, not in the repo):
[`~/Github/Ember/docs/WORKFLOW.md`](https://github.com/tarakanof/Ember/blob/main/docs/WORKFLOW.md).
This file holds what differs for firmware. Safety rules: [`../AGENTS.md`](../AGENTS.md).

Issue numbers written as #N or cinder#N refer to the original private repository; Ember#N refers to the public [Ember](https://github.com/tarakanof/Ember) repository.

## Issues and PRs

- Worktrees:
  `git worktree add ~/Github/cinder-wt/<slug> -b <type>/<issue>-<slug> origin/main`.
- A feature that needs server support gets a paired Ember issue; the PRs link
  each other. Ember stays backward compatible: the knob decodes non-strictly and
  the server ignores unknown checkin fields, so either side can ship first.
- Every firmware PR bumps `PROJECT_VER` in `firmware/CMakeLists.txt` (patch
  for fixes and small features). The version shows in the checkin.
- PR evidence: host test output, a screen snapshot for UI, measured numbers
  (frame time, CPU per core, heap, latency) before and after.
- Record the feature's design, measurements and user decisions in
  [`features.md`](features.md) in the same PR. Board facts go in [`llm.md`](llm.md).

## Build and test

```sh
. ~/.espressif/tools/activate_idf_v5.5.5.sh
cd firmware
test/host/run.sh          # host tests: pure C (bot, view, provision, ota, title font, ...) and tools/secret_scan.py
idf.py build
python3 tools/secret_scan.py build/cinder.bin build/cinder.elf
```

- The title font test needs `managed_components/lvgl__lvgl` (LVGL's font decoder). Without
  it `run.sh` skips that test with a warning and runs the rest; run `idf.py reconfigure` or a
  build first to include it.
- **Every build is secret-free** (since 0.9.16): no Wi-Fi password, Ember URL or token is
  compiled in. A knob gets its Wi-Fi over Improv USB provisioning and its device token
  from pairing in the Ember app. `tools/secret_scan.py` checks an image against the values
  in `sdkconfig.secrets` and the Ember master token (`~/.config/ember/producer.env`); it
  prints key names and booleans only.
- `sdkconfig` is generated and gitignored. When `sdkconfig.defaults` gains an
  option (a new font, a FreeRTOS flag), an old local `sdkconfig` hides it and the
  build fails with an undeclared symbol. Fix: back up and delete
  `firmware/sdkconfig`; the next build regenerates it from `sdkconfig.defaults`.
  **A `sdkconfig` from before 0.9.16 must be deleted** (back it up first if you like): it has no app
  rollback and may hold the dev seed and its secrets. The build stops with a CMake error in
  that case (no rollback, `BOOTLOADER_PROJECT_VER` < 2, or `CINDER_DEV_SEED=y` without
  `-D CINDER_SEED_SECRETS=1`); delete `firmware/sdkconfig` and build again to regenerate it.
- Opt-in dev seed (not recommended; only for a knob that cannot be provisioned over
  USB): copy `sdkconfig.secrets.example` to `sdkconfig.secrets` (`cp`, never `cat`;
  never print it) and build into its own directory:
  `idf.py -B build-seed -D SDKCONFIG=build-seed/sdkconfig -D CINDER_SEED_SECRETS=1 build`.
  It seeds Wi-Fi and the Ember URL only, never a token. Such an image fails the
  secret scan and Ember refuses it (`dev_seed_build`): never upload or release it.

## Releases and OTA (`firmware/tools/`)

| Script | Does |
|---|---|
| `build_release.sh [--version X.Y.Z] [--ota-test crash_boot\|no_checkin\|none] [--dir DIR]` | Secret-free build from `sdkconfig.defaults` only into `build-release/` (own `sdkconfig`); fails on `CINDER_DEV_SEED=y` or a secret scan hit; prints version, build, size, SHA-256 |
| `publish.sh [--release] [--replace] [--no-build]` (+ build options) | Builds, then uploads `cinder.bin` (`POST /v1/firmware?channel=test\|release`) and `cinder.elf` (`PUT /v1/firmware/<version>/elf`) with only `EMBER_TOKEN`/`EMBER_SERVER_URL` read from `producer.env` (or `$EMBER_ENV_FILE`; not exported, never echoed); promotes the channel with `PATCH` when Ember already held the bytes on the other one; fails unless `GET /v1/firmware` then lists the local SHA-256, the channel and the ELF |
| `release.sh X.Y.Z` | Clean, in-sync `main` with `PROJECT_VER` X.Y.Z: tags and pushes `vX.Y.Z`, waits for the release workflow, downloads the GitHub Release, checks it came from that run at that commit (author, time window, exact assets, equal to the run's artifact) and `SHA256SUMS`, then uploads those bytes (`publish.sh --release --no-build --dir`). Never builds locally; a re-run skips what is done, re-runs a failed workflow and resumes from the pushed tag even after `main` moved |

- CI (`.github/workflows/ci.yml`, every PR and push to `main`): host tests, a secret-free
  `idf.py build` in `espressif/idf:v5.5.5` (pinned by digest), the secret scan and `idf.py size`
  in the job summary. No secrets. Release builds come only from `.github/workflows/release.yml`
  (tag `vX.Y.Z`, on `main`, matching `PROJECT_VER`; host tests run there too; a `vX.Y.Z-suffix` tag makes a prerelease that
  `release.sh` never uploads). A release is never replaced with other bytes.
- Uploads default to channel `test`: they install only with the app's **Update** button.
  Automatic mode installs only `release` builds.
- OTA test images: `publish.sh --dir build-test --version <next> [--ota-test FAULT]`
  (`CONFIG_CINDER_OTA_TEST`: 2 min rollback timer, CINDER1 `ota_fault`). Delete test
  uploads afterwards (`DELETE /v1/firmware/{version}`, once the knob runs a release build).
- Design and the on-knob test plan: [`features.md`](features.md), "OTA from Ember".

## Flash

To flash your own knob, the steps in the [README](../README.md#flash) are enough. The rules
below are the maintainer's, for agents that work on the maintainer's paired, live knob.

### Maintainer rules (live device)

Flash only when the user asks or approved it for this change. The knob is live.

1. Take the lock (every session and agent shares one knob):
   `mkdir /tmp/cinder-knob-flash.lock && echo "$$ TASK" > /tmp/cinder-knob-flash.lock/owner`.
   If it exists, wait. It is stale only when it is older than 15 min and no
   `idf.py`/`esptool` process runs; then remove it.
2. Find the port: `ls /dev/cu.usbmodem*` (303a:1001). It changes after a replug
   (`usbmodem101`, `usbmodem1101`); the tools default to `usbmodem1101`, so
   pass `--port`.
3. Dev loop: `idf.py -p <port> app-flash erase-otadata`. After an OTA the knob may
   run `ota_1`; `app-flash` always writes `ota_0`, so without `erase-otadata` it would
   keep booting the old `ota_1`. Blank otadata boots `ota_0` and loses nothing else
   (NVS, coredump and `ota_1` stay). A full `flash` also rewrites the bootloader and
   partition table: only with the user's approval (the one-time rollback bootloader
   step, below). `erase-flash` wipes NVS (pairing, Wi-Fi, device token): never.
   esptool's own reset during a flash is expected; never toggle DTR/RTS yourself
   (no `idf.py monitor`, no serial terminal).
4. Verify: wait for the boot, then check the last checkin on the live server
   (`GET /v1/devices`, owner token): new `fw` version, `fw_build`, `ota.slot` and
   `ota.image`, `link_mhz`, heap. Take a snapshot.
5. Remove the lock (`rm -r /tmp/cinder-knob-flash.lock`).

If USB goes silent after a flash, ask the user to replug; don't toggle lines.

**One-time rollback bootloader (OTA, #10).** Two stages, both from `build-release`
only (`tools/build_release.sh`), never from a `build/` with an old `sdkconfig`. Each
stage takes the flash lock (steps 1-2 above) and releases it afterwards.

Stage 1, app only on the old bootloader (0.9.16 reports `ota.rollback:false` there, so
Ember sends no offers):

```sh
idf.py -B build-release -D SDKCONFIG=build-release/sdkconfig -p <port> app-flash erase-otadata
```

Check the checkin: `fw` 0.9.16, `fw_build`, `slot:0`, still paired.

Stage 2, once, with the user near the knob and their approval. First two read-only
backups (`read_flash` neither erases nor writes; esptool's reset is expected) into the
main checkout's git-ignored `backup/`. Use the date as `D`:

```sh
B=~/Github/cinder/backup D=$(date +%Y%m%d)
python -m esptool --chip esp32s3 -p <port> -b 921600 read_flash 0x0 0x8000 $B/knob-boot-$D.bin
python -m esptool --chip esp32s3 -p <port> -b 921600 read_flash 0x9000 0x8000 $B/knob-nvs-otadata-$D.bin
(cd $B && shasum -a 256 knob-boot-$D.bin knob-nvs-otadata-$D.bin > knob-pre-ota-$D.sha256)
idf.py -B build-release -D SDKCONFIG=build-release/sdkconfig -p <port> flash
```

- `knob-boot-*.bin` is 0x0-0x7FFF: the old bootloader. `knob-nvs-otadata-*.bin` is
  0x9000-0x10FFF: NVS (Wi-Fi password, device token, settings) and otadata. That file is
  a secret: never `cat`, `hexdump`, `strings` or commit it. Print only its SHA-256.
  `backup/` and `*.bin` are git-ignored.
- The flash writes 0x0 bootloader, 0x8000 partition table (identical bytes), 0xF000 blank
  otadata and 0x20000 `ota_0`. NVS (0x9000), phy (0x11000), `ota_1`, coredump and storage
  are not written.
- Check the checkin: `ota.rollback:true`, `slot:0`, `image:"valid"`.
- Restore, only if the knob no longer boots:
  `python -m esptool --chip esp32s3 -p <port> write_flash 0x0 $B/knob-boot-$D.bin`, after `shasum -a 256 -c`.

## Dev tools (`firmware/tools/`)

The port tools open it with raw termios and leave DTR/RTS alone; their effects are RAM only.

| Tool | Use |
|---|---|
| `snapshot.py --out x.png` | Screen as PNG (show the user, PR evidence) |
| `chase.py --style full\|half [--fps N] [--laps N]` | Start a glint chase now (needs working mood); `--fps` sets the chase eye rate until a reboot |
| `input.py push\|long\|turn\|touch\|page\|swipe_up\|swipe_down` | Simulate knob input (needs `CONFIG_CINDER_DEV_INPUT`, default on) |
| `measure_view_latency.py` | Mood latency and idle traffic against Ember |
| `gen_tool_marks.py` | Regenerate `components/bot/tool_marks.c` (venv; no port) |
| `gen_title_font.py [TTF]` | Regenerate `main/font_title_bold.c`, the OTA face's bold title glyphs (Pillow; no port) |

To put the bot in working mood without real work, post a demo session to Ember
and re-post every 10 s (Ember `docs/WORKFLOW.md` step 7).

## Human docs

`docs/enclosure.html` is generated from a separate source; do not edit it by hand.
