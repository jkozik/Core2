# Core2 WiFi Status Board

An [M5Stack **Core2**](https://www.amazon.com/s?k=m5stack+core2&adgrpid=201272510787&hvadid=816679901262&hvdev=c&hvexpln=0&hvlocphy=9028784&hvnetw=g&hvocijid=16996597048249632579--&hvqmt=e&hvrand=16996597048249632579&hvtargid=kwd-970340317113&hydadcr=13895_13840379_2527010&mcid=50ea2329a8753c0c97803e24d3b913df&tag=googhydr-20&ref=pd_sl_7ach5rxuvi_e) that checks three home WiFi networks and shows one colored bar for each:
green = working, yellow = degraded, red = failed. It retests every 2 minutes. Touch the screen to test now.

For each network it does: scan, connect, get an IP, make one tiny HTTP request, then look up the public IP and ISP.
It also shows the battery level at the bottom right.

I set the little gadget on my kitchen counter and I can see at a glance if everything is ok.  Because of bad storms, I have had multi-week internet outages.  I have learned to depend on 5G Fixed Wireless service as a backup.  Storm related outages have happened enough that I setup a dual WAN w/failover network using a [NetGate pfSense router](https://shop.netgate.com/products/1100-pfsense?srsltid=AYCX9vLRhOgdlCtM2WNxEx5C-u3cRrrAk8aiSQNQYXRx-QnrMROhzJsf).  The T-Mobile service is really pretty good (>50Mbps), but I only use it when my Fiber service fails.  T-Mobile has a Home Backup pricing plan.  For $20/month, the service is always on, but rarely used.  This is worth it for me.  

Because my setup is a little confusing, I did a nice little diagram to remind me how it is wired together, and the Core2 gadget gives me the right level of detail so tha I know things are working.  So unique to many monitoring applications, I have this gadget tune to three different WIFI networks to confirm that everything is working.  Further, I the fiber network is down, the Core2 display will indicate yellow.  

It also sends a small UDP heartbeat so Zabbix can alert if the board stops working. The receiving side is in the companion
repository [m5-core2-chk](https://github.com/jkozik/m5-core2-chk).

<img width="1588" height="943" alt="image" src="https://github.com/user-attachments/assets/f578b7f3-18cf-424a-adaf-10e2af79292f" />


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
