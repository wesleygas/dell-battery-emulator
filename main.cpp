#include <Wire.h>

#define BATT_ADDR 0x0B
#define SDA_PIN   4
#define SCL_PIN   5

#ifndef LED_BUILTIN
  #define LED_BUILTIN 25
#endif

// Bus and State Tracking
volatile uint8_t current_reg = 0x00;
volatile uint8_t challenge_data[16];
volatile uint8_t challenge_len = 0;
volatile bool    challenge_received = false;

unsigned long last_led_toggle = 0;
bool led_state = false;

// Master writes register pointer or block data
void receiveEvent(int bytesReceived) {
  if (bytesReceived <= 0) return;
  
  current_reg = Wire.read();
  bytesReceived--;

  // Trap Dell Authentication Block Writes on Register 0x0A
  if (current_reg == 0x0A && bytesReceived >= 8) {
    challenge_len = 0;
    while (Wire.available() && challenge_len < sizeof(challenge_data)) {
      challenge_data[challenge_len++] = Wire.read();
    }
    challenge_received = true;
    return;
  }

  // Drain any remaining bytes from incoming writes (e.g. 0x06 keep-alive or 0x08 table config)
  while (Wire.available()) {
    Wire.read();
  }
}

// Master reads payload from selected register
void requestEvent() {
  uint8_t buffer[32];
  uint8_t len = 0;

  switch (current_reg) {
    // --- Standard SBS 1.1 Word Registers (Little-Endian) ---
    case 0x00: // ManufacturerAccess: Normal Ready State
      buffer[0] = 0x00; buffer[1] = 0x00; len = 2; break;

    case 0x01: // RemainingCapacityAlarm (400 mAh)
      buffer[0] = 0x90; buffer[1] = 0x01; len = 2; break;

    case 0x02: // RemainingTimeAlarm (10 min)
      buffer[0] = 0x0A; buffer[1] = 0x00; len = 2; break;

    case 0x03: // BatteryMode: Internal controller active
      buffer[0] = 0x01; buffer[1] = 0x00; len = 2; break;

    case 0x06: // Dell Watchdog / Safety Keep-Alive Reply
      buffer[0] = 6;
      buffer[1] = 0x02; buffer[2] = 0x00; buffer[3] = 0x00;
      buffer[4] = 0x00; buffer[5] = 0x00; buffer[6] = 0x01;
      len = 7;
      break;

    case 0x08: // Temperature: 25.0 °C (298.2K = 0x0BA6)
      buffer[0] = 0xA6; buffer[1] = 0x0B; len = 2; break;

    case 0x09: // Voltage: 11,400 mV (0x2C88)
      buffer[0] = 0x88; buffer[1] = 0x2C; len = 2; break;

    case 0x0A: // Current: Report nominal discharging current under boot (-1200 mA = 0xFB50)
      buffer[0] = 0x50; buffer[1] = 0xFB; len = 2; break;

    case 0x0B: // AverageCurrent: -1200 mA
      buffer[0] = 0x50; buffer[1] = 0xFB; len = 2; break;

    case 0x0D: // RelativeStateOfCharge: 85%
      buffer[0] = 85;   buffer[1] = 0x00; len = 2; break;

    case 0x0E: // AbsoluteStateOfCharge: 85%
      buffer[0] = 85;   buffer[1] = 0x00; len = 2; break;

    case 0x0F: // RemainingCapacity: 4420 mAh (0x1144)
      buffer[0] = 0x44; buffer[1] = 0x11; len = 2; break;

    case 0x10: // FullChargeCapacity: 5200 mAh (0x1450)
      buffer[0] = 0x50; buffer[1] = 0x14; len = 2; break;

    case 0x11: // RunTimeToEmpty: ~220 minutes (0x00DC)
      buffer[0] = 0xDC; buffer[1] = 0x00; len = 2; break;

    case 0x12: // AverageTimeToEmpty: ~220 minutes
      buffer[0] = 0xDC; buffer[1] = 0x00; len = 2; break;

    case 0x13: // AverageTimeToFull: 65535 (Not charging)
      buffer[0] = 0xFF; buffer[1] = 0xFF; len = 2; break;

    case 0x14: // ChargingCurrent: 0 mA (Prevents charge circuit activation)
      buffer[0] = 0x00; buffer[1] = 0x00; len = 2; break;

    case 0x15: // ChargingVoltage: 0 mV
      buffer[0] = 0x00; buffer[1] = 0x00; len = 2; break;

    case 0x16: // BatteryStatus: Discharging active (0x0080)
      buffer[0] = 0x80; buffer[1] = 0x00; len = 2; break;

    case 0x17: // CycleCount: 15 cycles
      buffer[0] = 15;   buffer[1] = 0x00; len = 2; break;

    case 0x18: // DesignCapacity: 5200 mAh (0x1450)
      buffer[0] = 0x50; buffer[1] = 0x14; len = 2; break;

    case 0x19: // DesignVoltage: 11,100 mV (0x2B5C)
      buffer[0] = 0x5C; buffer[1] = 0x2B; len = 2; break;

    case 0x1A: // SpecificationInfo: SBS v1.1 compliant (0x0031)
      buffer[0] = 0x31; buffer[1] = 0x00; len = 2; break;

    case 0x1B: // ManufactureDate: Valid date (0x56CF)
      buffer[0] = 0xCF; buffer[1] = 0x56; len = 2; break;

    case 0x1C: // SerialNumber: 0x1234
      buffer[0] = 0x34; buffer[1] = 0x12; len = 2; break;

    // --- Block Strings (Length byte + ASCII string) ---
    case 0x20: // ManufacturerName: "DELL"
      buffer[0] = 4; memcpy(&buffer[1], "DELL", 4);
      len = 5; break;

    case 0x21: // DeviceName: "DELL-BATT"
      buffer[0] = 9; memcpy(&buffer[1], "DELL-BATT", 9);
      len = 10; break;

    case 0x22: // Chemistry: "LION"
      buffer[0] = 4; memcpy(&buffer[1], "LION", 4);
      len = 5; break;

    // --- Dell Proprietary Identification Registers ---
    case 0x2B: // Dell Part Number / PPID Block Read
      buffer[0] = 8; memcpy(&buffer[1], "DELL0000", 8);
      len = 9; break;

    case 0x37: // State of Health (SOH): 100% (0x0064) -> "Health: Excellent"
      buffer[0] = 0x64; buffer[1] = 0x00;
      len = 2; break;

    default: // Catch-all fallback
      buffer[0] = 0x00; buffer[1] = 0x00;
      len = 2; break;
  }

  // Atomic buffer transmission prevents FIFO underruns at 335+ kHz
  Wire.write(buffer, len);
}

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);

  // Bind hardware I2C0 peripheral to chosen pins
  Wire.setSDA(SDA_PIN);
  Wire.setSCL(SCL_PIN);
  
  // Initialize as Slave at address 0x0B (7-bit standard SBS)
  Wire.begin(BATT_ADDR);
  Wire.onReceive(receiveEvent);
  Wire.onRequest(requestEvent);
}

void loop() {
  // Visual Heartbeat: Onboard LED blinks at 1 Hz when polling is healthy
  if (millis() - last_led_toggle >= 500) {
    last_led_toggle = millis();
    led_state = !led_state;
    digitalWrite(LED_BUILTIN, led_state);
  }
}
