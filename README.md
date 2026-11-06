# Core2 WiFi Status Board

An M5Stack **Core2** that checks three home WiFi networks and shows one colored bar for each:
green = working, yellow = degraded, red = failed. It retests every 2 minutes. Touch the screen to test now.

For each network it does: scan, connect, get an IP, make one tiny HTTP request, then look up the public IP and ISP.
It also shows the battery level at the bottom right.

It also sends a small UDP heartbeat so Zabbix can alert if the board stops working. The receiving side is in the companion
repository [m5-core2-chk](https://github.com/jkozik/m5-core2-chk).

## What it monitors

```
                      Internet
                 ______/    \______
                /                  \
        Quantum Fiber          T-Mobile fixed wireless
        (CenturyLink)           (backup plan, light use)
               |                      |
         +-----+------+        +------+--------+
         |  Ghorman   |        | TMOBILE-C988  |   <-- direct T-Mobile gateway
         |  (SSID)    |        |    (SSID)     |
         +-----+------+        +------+--------+
               |                      |
               |   WAN1        WAN2   |
               +-----> pfSense <------+        pfSense: Ghorman is primary,
                         |                     T-Mobile is failover
                    +----+----+
                    |  Naboo  |   <-- the SSID the IoT devices use
                    |  (SSID) |
                    +---------+

  Core2 bars:  [ Ghorman ]  [ Naboo ]  [ T-Mobile ]
```

Naboo's traffic normally leaves via Ghorman's fiber. If the fiber fails, pfSense moves it to T-Mobile.
The Naboo bar turns **yellow, "FAILOVER: <ISP>"** when its ISP matches the T-Mobile bar instead of Ghorman's.

## Bar colors

| Color | Meaning |
|---|---|
| Green | Connected, got an IP, internet check passed, signal and latency fine |
| Yellow | Connected but slow, weak signal, no internet, or Naboo is on the T-Mobile backup |
| Red | Network not seen, wrong password, timeout, or no IP |
| Cyan | Currently being tested |

## Set up the development environment (Windows)

You need: a Windows laptop, **Python 3.11**, a USB-C data cable, and VS Code (optional).

1. **Plug the Core2 in.** Windows should show a `USB-Enhanced-SERIAL CH9102` port in Device Manager. If it doesn't, install the CH9102 driver from M5Stack.
2. **Create the Python environment** (from the project folder, PowerShell):
   ```powershell
   python -m venv .venv
   .\.venv\Scripts\pip install -r requirements.txt
   ```
3. **Create your credentials file:**
   ```powershell
   copy include\secrets.h.example include\secrets.h
   ```
   Edit `include\secrets.h` and fill in the SSIDs and passwords. This file is git-ignored.
4. **Check the COM port** (Device Manager, Ports). If it isn't `COM6`, edit `upload_port` and `monitor_port` in `platformio.ini`.
5. **Build and upload:**
   ```powershell
   .\.venv\Scripts\python.exe -m platformio run --target upload
   ```
   The first build downloads the ESP32 toolchain and takes about 15 minutes. Later builds take about 1 minute.
6. **Watch the serial log** (optional):
   ```powershell
   .\.venv\Scripts\python.exe -m platformio device monitor
   ```

## Project layout

| Path | What it is |
|---|---|
| `src/main.cpp` | All the application code |
| `include/secrets.h` | Your SSIDs and passwords (not committed) |
| `include/secrets.h.example` | Template for the above |
| `platformio.ini` | Board, libraries, COM port |
| `requirements.txt` | Pinned Python packages (PlatformIO) |

See [NOTES.md](NOTES.md) for the setup history, gotchas, and how to add a network or move to another laptop.
