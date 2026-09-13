# Dell Battery Emulator (RP2040 / Raspberry Pi Pico)

An open-source hardware interface and firmware that emulates a genuine Dell laptop battery over SMBus / I2C Fast-Mode. 

Allows modern Dell laptops to bypass hardware presence checks and complete POST/boot using custom lithium-ion battery packs, external DC sources, or bench power supplies without triggering critical EC shutdown loops.

### Features
- **Full SBS v1.1 Compliance:** Emulates standard gas-gauge registers (voltage, remaining capacity, discharge current, cycle count).
- **Dell Extended Telemetry:** Implements proprietary registers `0x37` (State of Health), `0x2B` (PPID string), and `0x06` (Watchdog heartbeat).
- **Fast-Mode I2C Support:** Runs reliably at Dell's high-speed ~335–400 kHz clock rate on RP2040 hardware I2C peripherals.
- **Protocol Analysis:** Includes documentation of Dell's HMAC-SHA1 crypto challenge sequence on register `0x0A` and on-board charger IC interactions.

This project was built together with Gemini 3.8 Flash.

---

## 1. System Overview & The Boot Gate

Modern Dell laptops enforce a multi-tier hardware and cryptographic policy before the Embedded Controller (EC) releases the CPU reset lines and permits the system to complete Power-On Self-Test (POST) on battery power.

When replacing an OEM battery with a custom pack, the EC evaluates two distinct operating regimes:
1. **Discharge / Boot Authority:** Governed by hardware presence line logic and basic SBS v1.1 telemetry compliance.
2. **Charge Authority:** Governed by an active, hardware-enforced **HMAC-SHA1 cryptographic handshake** and a proprietary watchdog loop.

---

## 2. Motherboard Battery Connector Pinout (10-Pin)

```
[ 1 | 2 | 3 ] [ 4 ] [ 5 ] [    6    ] [       7       ] [ 8 | 9 | 10 ]
   VCC / P+    SCL   SDA   PRESENCE    SYSTEM GROUND       GROUND / P-
 (Pack +11V)  Clock  Data   (3.3V IN)   (0Ω to System)      (Pack GND)
```

| Pin(s)       | Signal           | Logic / Level                 | Description                                                                                                                                                           |
| ------------ | ---------------- | ----------------------------- | --------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| **1, 2, 3**  | **`P+` / `VCC`** | +11.1V to +13.2V (3S nominal) | Main battery power rail (connects to battery pack positive).                                                                                                          |
| **4**        | **`SCL`**        | 3.3V Open-Drain               | SMBus Clock line (~335 kHz Fast-Mode).                                                                                                                                |
| **5**        | **`SDA`**        | 3.3V Open-Drain               | SMBus Data line.                                                                                                                                                      |
| **6**        | **`BATT_PRES`**  | **Active-HIGH (3.3V-5V)**     | **Hardware Presence Pin.** Must be tied to **+3.3V/+5V** to signal that a battery is seated. If low or floating, the EC turns off SMBus polling and aborts cold boot. |
| **7**        | **`GND_SENSE`**  | 0V (Ground)                   | Tied to motherboard system ground via a $0\,\Omega$ net tie.                                                                                                          |
| **8, 9, 10** | **`P-` / `GND`** | 0V (Ground)                   | High-current ground return.                                                                                                                                           |

> **Note on OEM Donor Packs:** If harvesting or awakening an official Dell battery pack, its internal battery-side presence pin (`PP`) must be tied to **Ground (`P-`)** to open the BMS discharge MOSFETs.

---

## 3. Bus & Protocol Mechanics: What and Why

### A. The 335 kHz Bus Glitch Filter Problem
Standard Texas Instruments battery microcontrollers (e.g., BQ20zxx, BQ30zxx, BQ40zxx) are configured by default for SMBus Standard-Mode ($\le 100\text{ kHz}$). Modern Dell ECs query the bus in **I2C Fast-Mode (~335–400 kHz)**. Legacy or standalone BMS chips often filter these pulses out as transient electrical noise, ignoring all queries and causing POST to fail. The RP2040’s hardware I2C controller operates at Fast-Mode speeds natively.

### B. Mandatory Extended Dell Telemetry
The EC does not simply request voltage; it queries Dell-specific registers:
* **Register `0x37` (State of Health / SOH):** Must return a valid percentage (e.g., `0x0064` = 100%). If this returns $0\%$, the EC displays an immediate "Battery Failure" halt screen.
* **Register `0x2B` (PPID / Serial Block):** Must return a valid ASCII string length byte and identity block (`DELL...`).
* **Register `0x06` (Watchdog / Safety Ping):** The EC sends a periodic keep-alive write (`0x06 0x02 0x00 0x00 0x00 0x00 0x01`) every ~1.5 seconds while powered.

### C. Why On-Board Laptop Charging Fails (The Crypto Challenge)
Bus captures reveal why the laptop initiates charging at ~250 mA but abruptly terminates it after 10–15 seconds:
1. **Pre-Charge / Grace Period:** When AC is connected, the EC configures the on-board buck charger IC (address `0x09`, e.g., ISL88739 / BQ24780S) to supply a low pre-charge current (~250 mA) to wake sleeping cells.
2. **Cryptographic Authentication:** Concurrently, the EC issues an SMBus block write to battery address `0x0B` using **Register `0x0A`**, delivering a **10-byte pseudo-random challenge nonce**.
3. **The 10-Retry Lockout:** The EC retries this challenge **exactly 10 times at ~756 ms intervals**.
4. **Charger Inhibit:** Because a software emulator lacks the factory-burned 128/160-bit symmetric private key shared between Dell's BIOS and the genuine TI battery microcontroller, it cannot compute the required HMAC-SHA1 signature. Once the 10th attempt fails, the EC writes `0x0000` (zero current / charge inhibit) to register `0x14` on the charger IC (`0x09`), permanently shutting off internal charging paths.

---

## 4. Hardware Architecture

Because software emulation handles discharge authority and telemetry negotiation, but cannot bypass Dell's private key check for charging, the system uses a **split discharge/charge architecture**:
* **Discharge & System Power:** The custom cells deliver power directly to Pins 1–3 (`VCC`), while the RP2040 satisfies the EC's presence and telemetry checks.
* **Charging Path:** Handled **externally** via a dedicated balance charger or an onboard CC/CV step-up/down charge controller that bypasses the laptop's internal charger rail.

```
          Raspberry Pi Pico (RP2040)               Dell Laptop Connector
         +--------------------------+             +----------------------+
         |                          |             |                      |
         |         GP4 (Pin 6, SDA) |<----------->| Pin 5 (SDA)          |
         |         GP5 (Pin 7, SCL) |<----------->| Pin 4 (SCL)          |
         |         GND (Pin 8, GND) |<----------->| Pins 8-10 (GND)      |
         |                          |             |                      |
         |              3V3 (Pin 36)|------------>| Pin 6 (BATT_PRES)    |
         +--------------------------+             +----------------------+
                                                  |                      |
             Custom Battery Pack                  |                      |
         +--------------------------+             |                      |
         |               Pack (+) --+------------>| Pins 1-3 (VCC)       |
         |               Pack (-) --+------------>| Pins 8-10 (GND)      |
         +--------------------------+             +----------------------+
                      |
                      +---> External CC/CV Charger (Independent Path)
```

* **Pull-up Resistors:** Not required on the breadboard/wiring. The motherboard has onboard $2.2\,\text{k}\Omega$ pull-ups to +3.3V on SCL and SDA.
* **Pico Power Supply:** Powered via a small buck converter stepping down pack voltage to 5V (into the Pico's `VSYS` pin), or powered directly from the motherboard's `+3.3V_ALW` rail.
