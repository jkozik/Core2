# Notes: Core2 WiFi Status Board

Written for future me. Covers how this was built, why, and how to change it. For the short intro, see [README.md](README.md).

## Quick reference

| Thing | Value |
|---|---|
| Device | M5Stack Core2 (ESP32, 320x240 touch screen), USB-C |
| USB chip / port | CH9102 (VID 1A86, PID 55D4), was `COM6` |
| Python | 3.11.9, in project venv `.venv/` |
| Build tool | PlatformIO Core 6.2.0 (pinned in `requirements.txt`) |
| Framework / library | Arduino framework, `M5Unified` library |
| Build + flash | `.\.venv\Scripts\python.exe -m platformio run --target upload` |
| Serial monitor | `.\.venv\Scripts\python.exe -m platformio device monitor` |
| Poll interval | `POLL_MS` in `src/main.cpp` (2 minutes) |

Pinned versions (verified building and running, 2026-10-30):

| Package | Version |
|---|---|
| PlatformIO Core | 6.2.0 (`requirements.txt`) |
| espressif32 platform | 55.3.39 (Arduino-ESP32 core 3.3.9, ESP-IDF 5.5.4) |
| M5Unified | 0.2.28 |
| M5GFX | 0.2.35 |

PlatformIO is **not** on PATH. Always call it through the venv Python as above.
Everything is run from the project folder.

## What the app does

For each entry in `WIFI_NETS` (in `include/secrets.h`), in order:

1. **Scan** (up to 3 tries) for the SSID. Not seen: red.
2. **Connect** to the strongest AP for that SSID, 15 s timeout. Failed: red ("Auth failed" or "Connect timed out").
3. **IP check.** Connected but no IP: red.
4. **Internet check.** One HTTP GET to `connectivitycheck.gstatic.com/generate_204`. Anything but 204: yellow.
5. **ISP lookup.** One HTTP GET to `ip-api.com/line` returns public IP and ISP name (shown on the bar).
6. **Verdict.** Green, unless RSSI is weaker than -75 dBm or latency is over 1000 ms (yellow).
7. Wi-Fi radio is turned off again before the next network.

After all networks are tested, **failover check:** if Naboo's ISP equals the T-Mobile bar's ISP and not
Ghorman's, Naboo turns yellow with "FAILOVER: <ISP>". It compares ISP names measured in the same cycle,
so no ISP strings are hard-coded.

Traffic is tiny on purpose: the T-Mobile plan is a backup, so no speed tests.

## How it was set up (history, in order)

1. **Checked the laptop.** Windows 11, VS Code and Git present, no PlatformIO. The Core2 showed up as `COM6`
   (CH9102 driver already installed). An old disconnected `COM5` entry from a previous device also exists. Ignore it.
2. **Chose PlatformIO** (Arduino framework + M5Unified) over Arduino IDE so builds and uploads can be run from the command line.
3. **Installed PlatformIO.** First done with a global `pip install --user`, then **redone in a project venv** because
   I prefer environments for portability. The global copy was uninstalled. `requirements.txt` pins the exact versions;
   `.python-version` records 3.11.9 (informational).
4. **Scaffolded** `platformio.ini`, a hello-world `src/main.cpp`, and `.gitignore` (`.pio/`, `.vscode/`, `secrets.h`, `.venv/`).
   First build took about 13 minutes (toolchain download), then flashed. Screen and serial both confirmed.
5. **Checked WiFi bands.** The Windows laptop's `netsh wlan show networks mode=bssid` showed only 5 GHz APs, which was misleading.
   A scan from the Debian mini-PC showed Ghorman on 2.4 GHz ch 1. The Core2's own scan then confirmed all three networks are visible.
   **Lesson: trust a scan from the Core2 itself.**
6. **Credentials.** `include/secrets.h.example` (committed template) and `include/secrets.h` (real, git-ignored).
7. **Single-network test** against Ghorman: scan, connect, IP, HTTP check, PASS/FAIL screen. Worked first try.
8. **Three-bar status board** with 2-minute polling, public-IP/ISP lookup, and Naboo failover detection.
9. **Battery percentage** on the bottom line, plus `battery=…% …mV charging=…` in the serial log each cycle.

## Gotchas I hit (and fixes)

- **ESP32 is 2.4 GHz only.** A 5 GHz-only SSID can never work. WPA2 is safest. WPA3-only can be unreliable.
- **Windows laptop scan can lie about bands.** See history step 5.
- **`ip-api.com/line` returns fields in its own fixed order**, not the order requested (ISP first, then IP). The code detects the IP line by content.
- **ESP32 scans sometimes miss an AP** (T-Mobile once showed "SSID not seen"). The scan now retries 3 times.
- **T-Mobile bar "Auth failed"** was simply a wrong password. The gateway has its own password (label on the gateway), different from the shared one for Ghorman/Naboo.
- **Cosmetic log line** `STA not started! You must call begin first` was removed by dropping the early `disconnect()`.
- **"Windows Long Path Support is disabled"** warning during builds is harmless.
- **COM port can change.** If upload fails with a port error, check Device Manager and update `upload_port` / `monitor_port` in `platformio.ini`.
- **Reset the board from a script** (serial monitor not needed): with pyserial, set `dtr=False`, pulse `rts=True` then `False`.
- **ip-api.com** is a free, HTTP-only service with a rate limit (about 45 requests/minute). One check per network every 2 minutes is far below that. It does see the public IP.

## Keeping secrets out of git

- Passwords live **only** in `include/secrets.h`. Nothing else (docs, `main.cpp`) should ever contain one.
- `.gitignore` must keep these lines: `secrets.h` (matches in any folder), `.pio/` (compiled firmware has the passwords baked in),
  `.venv/`, `.vscode/`. Do not remove or loosen them.
- Checked 2026-10-30: in a scratch repo, the root `.gitignore` excluded `secrets.h` at the root, in `include/` and in `src/`, plus `.pio/` and `.venv/`.
  Only `.gitignore`, `NOTES.md`, `README.md`, `include/secrets.h.example`, `platformio.ini`, `requirements.txt` and `src/main.cpp` were staged.
- Before the first commit and any push: run `git init`, then `git status` and `git add -n .` (dry run) and confirm `secrets.h` is not listed.
  `git check-ignore -v include/secrets.h` should print the matching rule.
- The `.example` file must keep placeholders (`CHANGE_ME`) only.
- If a password ever lands in a commit, deleting the file is not enough. Change the WiFi password, then clean the history.

## How-to: change things

### Add a WiFi network
1. Add a line to `WIFI_NETS` in **both** `include/secrets.h` and `include/secrets.h.example` (template without the real password).
2. **Screen space:** three rows fill the screen. `ROW_H = 68` and `ROW_GAP = 4` in `src/main.cpp`; the footer sits below the rows.
   A 4th row won't fit unless you shrink `ROW_H` (and likely the text sizes in `drawRow`). 4 rows need about 50 px each.
3. **Failover logic** uses `IDX_PRIMARY`, `IDX_PFSENSE`, `IDX_BACKUP` (indexes into `WIFI_NETS`). If you reorder the list, update these.
4. Rebuild and flash.

### Change the polling interval
Edit `POLL_MS` in `src/main.cpp`.

### Change what counts as green/yellow
`GOOD_RSSI` (dBm) and `GOOD_LATENCY_MS` at the top of `src/main.cpp`.

### Change the Core2's password for a network
Edit `include/secrets.h`, rebuild, flash. (No need to touch the example file.)

## How-to: migrate to another laptop

1. Install **Python 3.11** (and VS Code / Git if wanted).
2. Copy the project folder, **including `include/secrets.h`** (it is not in git). Or recreate it from `secrets.h.example`.
3. Recreate the venv (do not copy `.venv/`):
   ```powershell
   python -m venv .venv
   .\.venv\Scripts\pip install -r requirements.txt
   ```
4. Plug in the Core2, find its COM port in Device Manager, update `platformio.ini` if it isn't `COM6`.
   Install the CH9102 driver if the port doesn't appear.
5. Run the build + upload command. The first build re-downloads the toolchain into `~/.platformio` (about 15 minutes).

PlatformIO's downloads live in `~/.platformio`, **outside** the project. It grew large during setup, and PlatformIO suggested
`pio system prune` to reclaim about 400 MB (not run).

## How-to: update the tool or its libraries

- **PlatformIO:** edit the pin in `requirements.txt` and rerun `pip install -r requirements.txt` in the venv.
- **ESP32 platform / libraries are pinned** in `platformio.ini` (see the table below), so a fresh build reproduces the same toolchain.
  To upgrade: change the version numbers, rebuild, and confirm the screen and serial output still work.
  `platformio pkg list` shows what is installed; Arduino core 3.3.9 and the toolchain come with the platform version.
  M5Unified also pulls M5GFX (`>=0.2.35`); M5GFX is pinned here too.
- **Re-test after any router change** (for example the planned Ghorman change from WPA1/WPA2 to WPA2-AES only).
  The Core2 should still connect, but run a cycle to confirm.

## Decisions

- **PlatformIO over Arduino IDE:** command-line builds, scripted.
- **Project venv, not global Python:** portability.
- **USB flashing, not OTA:** OTA needs the radio always listening, which fights both the WiFi-testing role and battery life,
  and a bad OTA image still needs USB to recover. Revisit if the device ends up mounted somewhere hard to reach.
- **Secrets in `secrets.h`, git-ignored,** with a committed `.example` template.
- **Tiny checks only** (one 204 request + one small text lookup per network) to protect the T-Mobile backup plan.

## Planned: Zabbix heartbeat (not built yet)

Why: the Core2 is not pingable. It joins one SSID at a time for about 10 s, radio off otherwise, with a different IP on each network,
so Zabbix ICMP checks would show it down most of the time. Ping also would not catch a hung app loop.

Design, revised 2026-10-31 (replaces the earlier Zabbix-trapper push idea; the user prefers Zabbix to keep polling, not receive pushes):
- The **Core2 sends a tiny UDP datagram** to the Debian TinyPC (`osprey`) once per cycle, **while connected to Ghorman**
  (that LAN is where the TinyPC and the Zabbix proxy live). Payload: shared token, uptime, the three network verdicts, battery %.
- A **small Python listener** on the TinyPC (systemd service, stdlib only, bound to the LAN address, one UDP port) writes the
  time of the last valid datagram to a file. Python 3 is already installed there; `nc` and `socat` are not.
- A **`UserParameter`** in `/etc/zabbix/zabbix_agentd.conf.d/` (new file, same one-file-per-feature pattern as the existing ones)
  returns the **age in seconds** of that file, like the existing `bckosprey.backup.age` item, and optionally a 0/1 OK/FAIL key.
  Zabbix polls the agent as it does today. Trigger on age greater than 420 s (heartbeats arrive every ~142 s; this alerts on two missed in a row).
- Item name: "Health Test" fits better than "Ping Test", since it is a heartbeat, not an ICMP ping.
- Optional: self-reboot watchdog so a hung loop recovers on its own.
- If Ghorman is down the report cannot be sent, and Zabbix will see the Core2 as silent. The alert cannot distinguish
  "Core2 dead" from "Ghorman down". That is acceptable.
- UDP is fire-and-forget, and it can be spoofed by anyone on the LAN, so the token is a light guard. A lost packet is tolerated.
- Core2 settings (TinyPC address, port, token) go in `include/secrets.h` with placeholders in the `.example`, never in `main.cpp`.
- Firmware change needed: Core2 test order so Ghorman is the last network connected (or the report goes while on Ghorman using the
  previous cycle's other results). Display order can stay the same.

What was learned about the TinyPC's Zabbix setup (2026-11-01, read-only inspection):
- The agent (classic agentd, active) reports **directly to the Zabbix server**, not through the proxy.
- The proxy is a Docker container (`zabbix-proxy-sqlite3:alpine-7.4-latest`, compose file in `~/projects/zabbixproxy` on the TinyPC),
  an **active proxy** that connects out to the server with a PSK. It publishes port 10051 and polls the IoT devices.
- Other containers there: Home Assistant (about 530 MB RAM) and cloudflared. The box has about 3 GB RAM with some swap in use, 18 GB disk free.

Decisions (2026-11-01):
- **The item lives on the existing Zabbix host `osprey`**, not on a separate `m5-core2` host. A separate host would need a passive check
  through the proxy and a wider `Server=` setting on the agent. Not worth it.
- **Listener runs as a Docker container** (matches the proxy, Home Assistant and cloudflared; compose file under version control).
- Item name: "Core2 Health Test" (a heartbeat, not an ICMP ping). Listener port: UDP 9099 (appeared free).
- Firewall: ufw, nftables and firewalld are inactive on the TinyPC. Raw iptables rules could not be checked without root.

Built so far (2026-11-01). The files live in their own project, `~/projects/m5-core2-chk` on the TinyPC (its own README.md and NOTES.md are there; there is no copy in this repository):
- `listener.py` (stdlib UDP listener, token check, atomic state file), `Dockerfile` (`python:3.13-alpine`, no build stage needed),
  `docker-compose.yml` (LAN-only port, read-only filesystem, 32 MB cap, runs as uid 1000), `m5core2_check.sh`, `m5core2.conf`.
- Tested: the listener locally with good, malformed, wrong-token and oversize datagrams (one crash bug found and fixed);
  the check script on the TinyPC against a temp folder. The container is built and running (image 45.7 MB, about 7 MiB RAM) and the Core2 heartbeat arrives; the agent config is not installed yet (needs root).
- Zabbix item keys on host `osprey`: `m5core2.heartbeat.age`, `m5core2.health[420]` (**1 = OK, 0 = FAIL**, same polarity as the existing
  `tailscale.status`; flip it in the script if you prefer 0 = OK), `m5core2.field[g|n|t|bat|up]`.

Still to do:
- DONE (2026-11-01): `.env` created on the TinyPC (mode 600) and the same token added to the Core2's `secrets.h`; verified equal by hash.
  Never commit either. The project folder on the TinyPC was renamed to `m5-core2-chk` ("chk" = it checks the m5-core2).
- DONE (2026-11-01): container running, `m5core2.conf` installed, Zabbix items, triggers and value mappings created; Latest Data shows all items.
- Core2 firmware: WRITTEN and compiles (2026-11-01), flashed and verified end to end (2026-11-01). Ghorman is now tested last
  and stays connected while two copies of the UDP heartbeat are sent; the footer shows `HB:ok` / `HB:fail`. Format:
  `tok=... up=<s> g=<0-3> n=<0-3> t=<0-3> bat=<%>` (0 green, 1 yellow, 2 red, 3 unknown). Flash it, then confirm `[heartbeat] sent` in the serial log.
- Future: add a container health check and tighter resource limits to `docker-compose.yml` (a 32 MB memory cap, pids limit and log rotation are already set).
- Record the actual image size after the first build.

## Status and open items

- **Zabbix heartbeat**: see the section above. Top of the to-do list.

Working: all three bars green at last check, battery display, 2-minute polling.

Not done / not tested:
- **Failover path never exercised live.** The yellow "FAILOVER" display has not been seen. One way to test: briefly disconnect
  Ghorman's WAN on pfSense and watch Naboo.
- **Red/yellow paths** were seen only by accident (wrong T-Mobile password gave red). Not deliberately tested for every case.
- **Not a git repository yet.** Run `git init` before committing; `secrets.h` is already in `.gitignore`.
- **Power saving** (dim/turn off screen, lower CPU clock, light sleep) not done. Battery is about 500 mAh; rough guess 2 to 4 hours on battery, never measured.
  The serial log records battery % and mV each cycle if you want to measure drain.
- **Running without USB** (wall adapter) is untested but expected to work.
- **Pending router change:** Ghorman still had WPA1 enabled and was on channel 1. The plan is to switch it to WPA2 (AES) without
  turning off its 2.4 GHz radio (WiFiLogger and Roku need it).
