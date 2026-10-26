# Leak Alert

A cheap Wi-Fi water leak sensor built on an ESP8266. Two bare wires sit on the floor. When water bridges them, the board sends an urgent push notification to your phone through [ntfy](https://ntfy.sh).

## How it works

The sensor pin (D5) is held HIGH by the ESP8266's internal pull-up resistor. The other probe wire goes to GND. When water connects the two wires, D5 is pulled LOW and the sketch treats the floor as wet.

To avoid false alarms and notification spam, the sketch debounces both directions:

- The sensor must read wet for **2 seconds** before a leak is reported.
- The sensor must read dry for **30 seconds** before the leak is cleared.

The onboard LED turns on while a leak is active.

### Notifications

| Event     | Title                | Message                | ntfy priority | When it's sent              |
| --------- | -------------------- | ---------------------- | ------------- | --------------------------- |
| Startup   | `Leak sensor online` | `<LOCATION>`           | low           | Every power-up or reset     |
| Leak      | `LEAK DETECTED`      | `Water at: <LOCATION>` | urgent        | Wet for 2 s                 |
| Reminder  | `Still wet`          | `Water at: <LOCATION>` | urgent        | Every 5 min while still wet |
| Cleared   | `Dry again`          | `<LOCATION>`           | default       | Dry for 30 s after a leak   |
| Heartbeat | `Leak sensor OK`     | `<LOCATION>`           | min           | Every 24 h after startup    |

If a leak alert or reminder fails to send (Wi-Fi down, ntfy unreachable), the sketch tries again every 30 seconds while the floor is still wet. A failed heartbeat is also retried every 30 seconds. The startup and **Dry again** messages are sent once and not retried.

**If you stop getting the daily heartbeat, the sensor is offline** (no power, no Wi-Fi, or crashed). Check on it.

## Hardware

- An ESP8266 board: **NodeMCU 1.0 (ESP-12E)** or **Wemos D1 mini**
- Two lengths of insulated wire, with about 1 cm of insulation stripped off one end of each
- A USB cable and a USB power adapter (the board stays on Wi-Fi all the time, so it isn't suited to battery power)

### Wiring

```
  ESP8266                         Floor
 ┌─────────┐
 │      D5 ├──────── wire ──────── bare tip ┐
 │         │                                ├─ a few mm apart, not touching
 │     GND ├──────── wire ──────── bare tip ┘
 └─────────┘
```

No resistor is needed. The sketch uses the board's internal pull-up.

Probe placement tips:

- Tape the bare tips flat to the floor at the lowest spot where water would collect (under a sink, behind a toilet, next to a water heater).
- Keep the tips a few millimeters apart. Too far apart and a small puddle won't bridge them.
- Make sure the tips can't touch each other, or the sensor will report a permanent leak.
- Bare copper slowly corrodes. Check the tips occasionally and re-strip them if they look green or dull.

Use D5 (GPIO14) as written. Avoid D3, D4 and D8: those pins control how the board boots, and a wet sensor on one of them at power-up can stop the board from starting.

## Files

| File                | Purpose                                                       |
| ------------------- | ------------------------------------------------------------- |
| `leak_alert.ino`    | The sketch                                                    |
| `secrets.example.h` | Template for `secrets.h`, with placeholder values             |
| `secrets.h`         | Your Wi-Fi name, password and ntfy topic. Not committed.      |
| `.gitignore`        | Keeps `secrets.h` out of git                                  |

## Setup

### 1. Install the ntfy app and pick a topic

1. Install ntfy on your phone ([Android](https://play.google.com/store/apps/details?id=io.heckel.ntfy) / [iOS](https://apps.apple.com/us/app/ntfy/id1625396347)).
2. Make up a long, random topic name, for example `myname-leak-k3v9x2qz`. Anyone who knows the topic name can read and post to it, so don't use something guessable like `leak`.
3. In the app, subscribe to that topic on the default server (`ntfy.sh`).

You can also watch the topic in a browser at `https://ntfy.sh/<your-topic>`.

### 2. Install the ESP8266 board package

1. Install the [Arduino IDE](https://www.arduino.cc/en/software).
2. Open **Arduino IDE → Settings** (**File → Preferences** on Windows/Linux) and add this URL to **Additional boards manager URLs**:
   ```
   https://arduino.esp8266.com/stable/package_esp8266com_index.json
   ```
3. Open **Boards Manager**, search for **esp8266**, and install **esp8266 by ESP8266 Community**.

### 3. Configure the sketch

The Wi-Fi password and ntfy topic go in `secrets.h`, which is kept out of git by `.gitignore`. Create it from the template:

```bash
cp secrets.example.h secrets.h
```

Then fill in these settings:

| Constant     | File             | What to put there                                                      |
| ------------ | ---------------- | ---------------------------------------------------------------------- |
| `WIFI_SSID`  | `secrets.h`      | Your Wi-Fi network name. The ESP8266 only supports **2.4 GHz** Wi-Fi.  |
| `WIFI_PASS`  | `secrets.h`      | Your Wi-Fi password                                                    |
| `NTFY_TOPIC` | `secrets.h`      | The topic you subscribed to in step 1                                  |
| `LOCATION`   | `leak_alert.ino` | Where this sensor is, e.g. `"Kitchen sink"`. Appears in every message. |

If `secrets.h` is missing, the build stops with `secrets.h not found: copy secrets.example.h to secrets.h and fill it in`.

Building more than one sensor? Give each one a different `LOCATION`. They can all share the same `secrets.h` and topic.

### 4. Upload

1. Plug the board in over USB.
2. In the Arduino IDE, select the board: **Tools → Board → ESP8266 Boards →** **NodeMCU 1.0 (ESP-12E Module)** or **LOLIN(WEMOS) D1 R2 & mini**.
3. Select the port under **Tools → Port**.
4. Click **Upload**.

Within about 20 seconds you should get a **Leak sensor online** notification.

The sketch must stay in a folder named `leak_alert`, with `secrets.h` next to it. The Arduino IDE requires the folder name to match the `.ino` file name.

<details>
<summary>Using arduino-cli instead</summary>

```bash
arduino-cli compile --fqbn esp8266:esp8266:nodemcuv2 .
```

```bash
arduino-cli upload --fqbn esp8266:esp8266:nodemcuv2 -p /dev/cu.usbserial-XXXX .
```

For a D1 mini, use `--fqbn esp8266:esp8266:d1_mini`. Run `arduino-cli board list` to find the port.

</details>

### 5. Test it

Dip both probe tips in a cup of water (or bridge them with a wet finger) for a few seconds. You should get a **LEAK DETECTED** alert and the onboard LED should turn on. Dry the tips off, and about 30 seconds later you should get **Dry again**.

## Tuning

The timing constants near the top of the sketch control the behavior:

| Constant          | Default | Meaning                                                    |
| ----------------- | ------- | ---------------------------------------------------------- |
| `WET_CONFIRM_MS`  | 2 s     | How long the sensor must stay wet before alerting          |
| `DRY_CONFIRM_MS`  | 30 s    | How long the sensor must stay dry before clearing the leak |
| `REMINDER_MS`     | 5 min   | How often to re-alert while still wet                      |
| `RETRY_MS`        | 30 s    | How soon to retry a notification that failed to send       |
| `HEARTBEAT_MS`    | 24 h    | How often to send the "still alive" message                |
| `WIFI_TIMEOUT_MS` | 20 s    | How long to wait for Wi-Fi before giving up on a send      |

If you get false alarms from condensation or splashes, raise `WET_CONFIRM_MS` or move the probe tips slightly farther apart.

## Troubleshooting

Open **Tools → Serial Monitor** at **115200 baud** to see what the board is doing.

| What you see                         | Likely cause                                                                      |
| ------------------------------------ | --------------------------------------------------------------------------------- |
| `Connecting to ... FAILED`           | Wrong SSID or password, a 5 GHz-only network, or the board is out of range.       |
| `ntfy error <negative number>`       | The board couldn't reach ntfy.sh. Check that the network has internet access.     |
| `ntfy error 429`                     | ntfy.sh is rate-limiting this network. Wait a few minutes.                        |
| `ntfy sent: ...` but no notification | The phone isn't subscribed to the exact `NTFY_TOPIC` (topics are case-sensitive). |
| Board doesn't show up under Port     | Try another USB cable (some are charge-only) or install the CH340/CP2102 driver.  |
| Board won't boot with the probes wet | The sensor is on a boot pin. Move it back to D5.                                  |

## Security notes

- **Your Wi-Fi password is stored in plain text in `secrets.h`.** `.gitignore` keeps it out of git, so `leak_alert.ino` is safe to share or publish. Don't share `secrets.h` itself.
- **ntfy.sh topics are public.** Anyone who knows the topic name can read your alerts or send fake ones. A long random topic name is the main protection. For more control, [self-host ntfy](https://docs.ntfy.sh/install/) with access control.
- **The TLS certificate isn't checked** (`setInsecure()`). Traffic to ntfy.sh is still encrypted, but the board doesn't verify it's talking to the real ntfy.sh. That's an acceptable trade-off for a home leak alert, not for anything sensitive.
