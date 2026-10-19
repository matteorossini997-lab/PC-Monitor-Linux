# BC250: display power/sleep verification

## Scope and journeys

Derived from the user's request, not a separate plan file:

- Keep existing resource monitoring in RUNNING.
- Indicate shutdown with an animation; turn off only the LCD/backlight about
  ten seconds after the controller enters OFF, leaving the ESP32 alive.
- Show breathing `Zzzz` only for known SLEEPING, restore the screen on start/wake.
- Missing telemetry, Wi-Fi or an unavailable status snapshot must not invent OFF.

## RED checkpoint

`389f226` introduced `tests/test_power_display.c` and
`tests/test_power_overlay.c`. Both strict compilations exited **1**, respectively
because `power_display.h` and `power_overlay.h` did not yet exist. This was the
intended missing-feature RED, before any production implementation.

Logs: `/home/user/kicad-workspace/verification/software-display-red/`.

## Test commands

Policy and lifecycle integration (no LVGL/ESP-IDF required):

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain/power tests/test_power_display.c main/power/power_display.c main/power/power_fsm.c -o /tmp/opencode/bc250-display-green
/tmp/opencode/bc250-display-green
```

Real LVGL renderer, not fake widget stubs: use LVGL **9.3.0**, upstream commit
`108e5aff3c90cc4d969331ed61aff2bbd365d430`, matching `main/idf_component.yml`.
The host-only config is `tests/lvgl/lv_conf.h`. Configure/build upstream LVGL
using that file, then link `tests/test_power_overlay.c` and
`main/ui/power_overlay.c` with its static library, `-lm -lpthread` and
`-DLV_CONF_INCLUDE_SIMPLE`. Exact CI commands are in the `lvgl-overlay` job of
`.github/workflows/bc250-tests.yml`; running that workflow remotely requires a
push and has not been authorized/performed.

## GREEN results

Executed locally using the real LVGL library described above:

| Check | Result |
|---|---|
| New display policy, 5 scenarios including OFF at 9999/10000 ms | Strict C build and run exit 0 |
| Real-LVGL overlay integration | Strict C build and run exit 0 |
| Existing power FSM, 17 scenarios | Strict C build and run exit 0 |
| Existing bearer authorization / ACK parser | Strict C build and run exit 0 |
| Existing Python suite | 26 tests, OK |
| Whitespace check | `git diff --check` exit 0 |

GCC `--coverage`/gcov, instrumenting our objects but not the LVGL library:

| Module | Lines | Branches executed | Branch directions taken |
|---|---:|---:|---:|
| `power_display.c` | 100% (11/11) | 100% | 100% |
| `power_overlay.c` | 98.89% (89/90) | 100% | 84.85% (28/33) |

The only uncovered overlay line is its defensive switch default. Untaken branch
directions include repeated BLANK, timestamp skew, the other half of the breath
cycle and calling the OFF animation directly after its deadline (the policy
selects BLANK instead). These are **not whole-firmware coverage numbers**.

Framebuffer captures were generated with real LVGL in RGB565 and converted to
PNG without installing new dependencies. The main agent visually inspected the
montage: power symbol, ring countdown, shutdown label and Zzzz fit the round
240×240 display area. These are host previews, not photographs of the Waveshare.
Full logs, commands, gcov output, capture harness and PNGs are in
`/home/user/kicad-workspace/verification/software-display-green/`.

Optional ASAN/UBSAN links failed because the host lacks `libasan_preinit.o`,
`libasan` and `libubsan`. The C/C++ build resolver reproduced the same error with
an unrelated one-line program: environment blocker, no source fix applied.
No sanitizer PASS is claimed; rerun on a host with those runtimes.

## Test specification

| Guarantee under test | Test target | Type |
|---|---|---|
| OFF remains animated at 9999 ms, blanks at 10000 ms; 64-bit uptime and snapshot timestamp skew | `test_off_deadline` | native unit |
| No other power state blanks; STOPPING does not start an OFF timer | `test_only_off_blanks`, `test_shutdown_lifecycle` | native unit / FSM integration |
| Pending suspend is not Zzzz; confirmed sleep remains visible; wake returns telemetry | `test_sleep_and_resume` | FSM integration |
| Missing data is not OFF; forced-off/manual OFF confirmation share the deadline | `test_no_telemetry_is_not_off` | FSM integration |
| Real LVGL labels, countdown, blank composition, breathing Zzzz and top-layer persistence | `test_power_overlay.c` | headless rendering integration |

## What is not established

- Host LVGL rendering is not an ESP32 target build or a test of SPI, GPIO40,
  panel OFF/ON commands, backlight polarity, watchdog timing or physical brightness.
- The new snapshot accessor and hardware integration in `main_lvgl.c` are not
  covered by the standalone host policy/renderer coverage percentages.
- No ESP-IDF toolchain is installed here. Firmware flashing and measurement of
  the real ten-second sequence remain outstanding; no board was flashed.
- `Zzzz` presentation does not repair native suspend/resume or implement fake
  sleep. The BC250 sleep qualification gap remains documented in
  [BC250_TPMS1_RESEARCH.md](BC250_TPMS1_RESEARCH.md).
- This change never calls power commands from the UI, changes the FSM safety
  rules or puts the ESP32 itself into deep sleep.

## Delivery assessment

Overall **3.6/5**: host-verified presentation, not target-qualified firmware.

| Axis | Score | Evidence / improvement |
|---|---:|---|
| Accuracy | 4 | Boundary tests and real LVGL builds pass; validate ESP-IDF-specific calls with a target build |
| Completeness | 3 | Requested views and LCD/backlight control are implemented; actual Waveshare OFF/wake sequence remains untested |
| Clarity | 4 | Guide distinguishes STOPPING/OFF, pending/confirmed sleep and host previews; show the real-board sequence next |
| Actionability | 3 | Reproducible tests and a CI job exist; ESP-IDF toolchain and physical board are still needed before flashing |
| Conciseness | 4 | Procedural animation adds no asset pipeline; detailed test evidence stays separate from the user-facing summary |

Highest-impact follow-ups: build both ESP-IDF profiles, then verify GPIO40/panel
OFF/ON and the ten-second sequence on the Waveshare. The expected user assessment
is "feature implemented and previewed, not yet safe to treat as flash-ready".
