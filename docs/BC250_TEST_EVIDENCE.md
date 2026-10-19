# BC250 software verification evidence

## Scope

Journeys derived from the user's BC250 hardware project and clarified behavior:
short press requests clean shutdown, long press forces power-off, Linux lifecycle
notifications supplement STATE, and authenticated remote control stays on the LAN.
Baseline repository commit: `d3a18f9f97ad66745648bf4a44c3993963299a54`.

## RED → GREEN

| Behavior | RED checkpoint and observed failure | GREEN guarantee |
|---|---|---|
| Power state policy | `7f05d04`: missing `power_fsm.h/.c` | 15 native state scenarios pass, including no ATX cut on signal/host loss, shutdown confirmation, sleep, button debounce and forced shutdown |
| Linux lifecycle integration | `3bd4fe8`: missing `power_agent`, `power_event`, final hook source | Agent uses allowlisted argv, private socket and one serial owner; hook is tested on pseudo-terminals only |
| Strict authentication/ACK parser | `313d778`: missing `power_security.h/.c` | Bearer token validation, constant-work secret comparison and bounded ACK parsing pass native tests |
| Contradictory STATE safety | `3cbc86c`: assertion `!bc_power_command(...BC_CMD_START...)` failed with raw STATE HIGH | Start is rejected rather than toggling a latch believed OFF when STATE contradicts it |

The initial sleep-cancellation test was refined: an ordinary heartbeat must not
cancel pending suspend, whereas an explicit RESUMED notification does. This is an
intentional distinction, not a claim that the original test passed unchanged.

## Commands and results

Executed on the Linux development host, not on the BC250:

```sh
cc -std=c11 -Wall -Wextra -Werror -Imain/power tests/test_power_fsm.c main/power/power_fsm.c -o /tmp/opencode/bc250-fsm-tests
/tmp/opencode/bc250-fsm-tests
cc -std=c11 -Wall -Wextra -Werror -Imain/power tests/test_power_security.c main/power/power_security.c -o /tmp/opencode/bc250-security-tests
/tmp/opencode/bc250-security-tests
python3 -m unittest discover -s tests -v
```

Results: native targets compile without warnings and pass; **26 Python tests
pass**. The Python tests compile the shutdown hook with `BC250_HOOK_TEST` and
exercise pseudo-terminals, not real serial devices. The production hook also
compiles with strict warnings, but was **not executed**.

Coverage collected using isolated Python tooling and GCC `--coverage`:

```sh
PYTHONPATH=linux_client /tmp/opencode/bc250-test-venv/bin/python -m coverage run --branch --source=power_agent,power_event,power_protocol -m unittest discover -s tests -v
/tmp/opencode/bc250-test-venv/bin/python -m coverage report --fail-under=80
```

| Tested module | Coverage |
|---|---:|
| `power_fsm.c` | 99.37% lines |
| `power_security.c` | 100% lines |
| `power_protocol.py` | 100% combined statements/branches |
| `power_agent.py` | 92% combined statements/branches |
| `power_event.py` | 86% combined statements/branches |
| Python total | 94% rounded (93.70% exact) |

TLS generation was smoke-tested in a temporary copy: key/token permissions,
certificate SAN, matching key/certificate, valid private LAN address, refusal to
overwrite, unchanged existing hashes and rejection of public/loopback IPs all
passed. Secret contents were not printed. Python/shell syntax and
`git diff --check` passed. Initial Ruff findings were style-only and are tracked
separately from functional results.

Full local logs and exit codes are outside the repository in
`/home/user/kicad-workspace/verification/software-final/`; earlier RED logs are
under adjacent `software-red*` directories. Checkpoint history preserves the
RED stages above; do not interpret those intermediate commits as release builds.

## Not established by these tests

- **ESP-IDF firmware compilation was not run:** no ESP-IDF/toolchain installed.
  The added GitHub workflow is unexecuted locally and has not been pushed/run.
- GPIO adapter, TLS server, Wi-Fi lifecycle, browser UI and display integration
  do not have target runtime coverage. The host percentages above do not apply
  to those files and are not whole-firmware coverage.
- `systemd-analyze verify` recognized the unit but exited 1 because the proposed
  `/opt/pc-monitor/.venv/bin/python` installation is absent on this host.
- No service was installed/enabled; no real power, suspend or flash command ran.
- TPMS1.9 behavior, ATX latch feedback, ACPI sleep support, USB standby power,
  pulse timing and real hardware reset behavior still require physical tests.

See [BC250_POWER.md](BC250_POWER.md) for setup and the safe handling of unknown
latch state. Host tests passing is **not** approval to operate untested hardware.
