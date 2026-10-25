/*
 * XIAO nRF52840 + BME280  ->  BTHome v2 BLE beacon
 * Wake -> read sensor -> broadcast unencrypted BTHome v2 adv -> sleep.
 * Target: OpenMQTTGateway -> MQTT -> Home Assistant.
 *
 * Board : Seeed XIAO nRF52840  (Adafruit nRF52 / Bluefruit core)
 * Sensor: BME280 over I2C  (SDA = D9/P1.14, SCL = D10/P1.15)
 * Power : 2x AAA lithium on the BAT+/BAT- pads
 *
 * Libraries: Adafruit BME280, Adafruit Unified Sensor, Adafruit SPIFlash
 */

#include <Arduino.h>
#include <bluefruit.h>
#include <Wire.h>
#include <Adafruit_Sensor.h>
#include <Adafruit_BME280.h>
#include <Adafruit_SPIFlash.h>

// ---------------- USER CONFIG ----------------
#ifdef DEBUG
#define REPORT_INTERVAL_MS 10000UL // 10 s while testing
#define ADV_BURST_MS 1000          // longer burst = easier to catch on a phone
#else
#define REPORT_INTERVAL_MS (15UL * 60UL * 1000UL) // 15 minutes
#define ADV_BURST_MS 300                          // broadcast window per cycle
#endif
#define BME280_I2C_ADDR 0x76 // 0x76 (generic) or 0x77 (Adafruit)
#define TX_POWER_DBM 0       // 0 dBm; raise a step only if range is marginal

// Battery mapping (2x lithium AAA: ~3.0 V full in use, ~2.2 V empty)
#define VBAT_FULL_MV 3000
#define VBAT_EMPTY_MV 2200
#define VBAT_DIVIDER 2.961f // XIAO onboard divider ratio - CALIBRATE
// ---------------------------------------------

Adafruit_BME280 bme;
Adafruit_FlashTransport_QSPI flashTransport;

uint16_t readBatteryMillivolts()
{
  // Connect the divider only for the read, then disconnect to save ~2.3 uA.
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, LOW); // LOW = divider engaged
  delay(5);
  analogReadResolution(12);
  analogReference(AR_INTERNAL_2_4); // 2.4 V full-scale reference
  uint32_t raw = 0;
  for (int i = 0; i < 8; i++)
  {
    raw += analogRead(PIN_VBAT);
    delay(1);
  }
  raw /= 8;
  digitalWrite(VBAT_ENABLE, HIGH); // HIGH = divider disconnected
  float adc_mv = (raw * 2400.0f) / 4096.0f;
  return (uint16_t)(adc_mv * VBAT_DIVIDER);
}

uint8_t batteryPercent(uint16_t mv)
{
  if (mv >= VBAT_FULL_MV)
    return 100;
  if (mv <= VBAT_EMPTY_MV)
    return 0;
  return (uint8_t)(100UL * (mv - VBAT_EMPTY_MV) / (VBAT_FULL_MV - VBAT_EMPTY_MV));
}

void buildAndBroadcast()
{
  // ---- read sensor (forced mode: sensor sleeps between reads) ----
  bme.takeForcedMeasurement();
  float tC = bme.readTemperature();         // degC
  float rh = bme.readHumidity();            // %
  float pHpa = bme.readPressure() / 100.0f; // hPa

  uint16_t vbat = readBatteryMillivolts();
  uint8_t pct = batteryPercent(vbat);

#ifdef DEBUG
  Serial.print("T=");
  Serial.print(tC);
  Serial.print(" C  ");
  Serial.print("RH=");
  Serial.print(rh);
  Serial.print(" %  ");
  Serial.print("P=");
  Serial.print(pHpa);
  Serial.print(" hPa  ");
  Serial.print("Vbat=");
  Serial.print(vbat);
  Serial.print(" mV (");
  Serial.print(pct);
  Serial.println(" %)");
#endif

  int16_t t = (int16_t)lround(tC * 100.0);     // 0x02  sint16 x0.01
  uint16_t h = (uint16_t)lround(rh * 100.0);   // 0x03  uint16 x0.01
  uint32_t p = (uint32_t)lround(pHpa * 100.0); // 0x04  uint24 x0.01
  static uint8_t packetId = 0;

  // ---- BTHome v2 service data (UUID 0xFCD2, unencrypted) ----
  uint8_t sd[22];
  uint8_t n = 0;
  sd[n++] = 0xD2;
  sd[n++] = 0xFC; // UUID 0xFCD2 (LE)
  sd[n++] = 0x40; // BTHome v2, no encryption
  sd[n++] = 0x00;
  sd[n++] = packetId++; // packet id (wraps at 255)
  sd[n++] = 0x01;
  sd[n++] = pct; // battery %
  sd[n++] = 0x02;
  sd[n++] = t & 0xFF;
  sd[n++] = (t >> 8) & 0xFF; // temp
  sd[n++] = 0x03;
  sd[n++] = h & 0xFF;
  sd[n++] = (h >> 8) & 0xFF; // humidity
  sd[n++] = 0x04;
  sd[n++] = p & 0xFF;
  sd[n++] = (p >> 8) & 0xFF;
  sd[n++] = (p >> 16) & 0xFF; // pressure
  sd[n++] = 0x0C;
  sd[n++] = vbat & 0xFF;
  sd[n++] = (vbat >> 8) & 0xFF; // voltage

  // ---- advertise a short burst, then stop ----
  Bluefruit.Advertising.stop();
  Bluefruit.Advertising.clearData();
  Bluefruit.Advertising.addFlags(BLE_GAP_ADV_FLAGS_LE_ONLY_GENERAL_DISC_MODE);
  Bluefruit.Advertising.addData(0x16 /* Service Data, 16-bit UUID */, sd, n);
  Bluefruit.Advertising.setInterval(160, 160); // 100 ms (units of 0.625 ms)
  Bluefruit.Advertising.start(0);              // 0 = run until we stop it
  delay(ADV_BURST_MS);
  Bluefruit.Advertising.stop();
}

void setup()
{
#ifdef DEBUG
  Serial.begin(115200);
  while (!Serial && millis() < 3000)
  {
  } // give the monitor up to 3 s to attach
  Serial.println("Boot");
#endif

  // On-board LEDs are active-LOW: drive HIGH = off.
  pinMode(LED_RED, OUTPUT);
  digitalWrite(LED_RED, HIGH);
  pinMode(LED_GREEN, OUTPUT);
  digitalWrite(LED_GREEN, HIGH);
  pinMode(LED_BLUE, OUTPUT);
  digitalWrite(LED_BLUE, HIGH);

  // POWER STEP 1: put the QSPI flash into deep power-down (~ -15 uA).
  flashTransport.begin();
  flashTransport.runCommand(0xB9); // 0xB9 = Deep Power-Down
  delay(1);
  flashTransport.end();

  // POWER STEP 2: keep the battery-voltage divider disconnected by default.
  pinMode(VBAT_ENABLE, OUTPUT);
  digitalWrite(VBAT_ENABLE, HIGH);

  // I2C + BME280 in forced mode, no oversampling overkill.
  // nRF52840 routes I2C to any GPIO; default is D4/D5, so remap to D9/D10 first.
  Wire.setPins(D9, D10); // SDA = D9 (P1.14), SCL = D10 (P1.15) -- before begin()
  Wire.begin();
  bool ok = bme.begin(BME280_I2C_ADDR);
#ifdef DEBUG
  Serial.print("BME280 init: ");
  Serial.println(ok ? "OK" : "FAILED (check wiring / 0x76 vs 0x77)");
#endif
  bme.setSampling(Adafruit_BME280::MODE_FORCED,
                  Adafruit_BME280::SAMPLING_X1, // temperature
                  Adafruit_BME280::SAMPLING_X1, // pressure
                  Adafruit_BME280::SAMPLING_X1, // humidity
                  Adafruit_BME280::FILTER_OFF);

  // BLE: beacon only, never restart advertising on disconnect.
  Bluefruit.begin();
  Bluefruit.autoConnLed(false); // stop Bluefruit from blinking the blue LED while advertising
  Bluefruit.setTxPower(TX_POWER_DBM);
  Bluefruit.setName("XIAO-BME280");
  Bluefruit.Advertising.restartOnDisconnect(false);

#ifdef DEBUG
  uint8_t mac[6];
  Bluefruit.getAddr(mac);
  Serial.print("BLE addr: ");
  for (int i = 5; i >= 0; i--)
  { // stored LSB-first, print MSB-first
    if (mac[i] < 0x10)
      Serial.print('0');
    Serial.print(mac[i], HEX);
    if (i)
      Serial.print(':');
  }
  Serial.println();
#endif
}

void loop()
{
  buildAndBroadcast();
  // The Adafruit core's tickless FreeRTOS idle drops the SoC into
  // low-power System-ON sleep during this long delay (~5 uA when tuned).
  delay(REPORT_INTERVAL_MS - ADV_BURST_MS);
}
