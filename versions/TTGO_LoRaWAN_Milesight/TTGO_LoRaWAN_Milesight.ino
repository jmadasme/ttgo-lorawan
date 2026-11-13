#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <XPowersLib.h>
#include <TinyGPSPlus.h>
#include <Preferences.h>

// ─── Serial ───────────────────────────────────────────────
#define SERIAL_BAUD           115200
#define GPS_BAUD              9600

// ─── Pines LoRa SX1262 (SPI) ──────────────────────────────
#define PIN_LORA_NSS          18
#define PIN_LORA_SCK          5
#define PIN_LORA_MOSI         27
#define PIN_LORA_MISO         19
#define PIN_LORA_RST          23
#define PIN_LORA_BUSY         32
#define PIN_LORA_DIO1         33

// ─── Pines GPS ────────────────────────────────────────────
#define PIN_GPS_RX            34
#define PIN_GPS_TX            12

// ─── Pines I2C (AXP192) ───────────────────────────────────
#define PIN_I2C_SDA           21
#define PIN_I2C_SCL           22

// ─── Credenciales LoRaWAN OTAA ────────────────────────────
// v5: DevEUI=...E0, AppKey=...D1
const uint64_t joinEUI = 0x24E124C0002ADD01;
const uint64_t devEUI  = 0x70B3D57E5F0040E5;
const uint8_t  appKey[] = {0xCB, 0xD4, 0xC4, 0x99, 0x2A, 0x11, 0x5B, 0xC2,
                           0x01, 0x18, 0xC5, 0x3A, 0x4C, 0x1C, 0xC0, 0xD6};
const uint8_t  nwkKey[] = {0xCB, 0xD4, 0xC4, 0x99, 0x2A, 0x11, 0x5B, 0xC2,
                           0x01, 0x18, 0xC5, 0x3A, 0x4C, 0x1C, 0xC0, 0xD6};

// ─── LoRaWAN ──────────────────────────────────────────────
#define LORAWAN_SUBBAND       2
#define LORAWAN_PORT          1

// ─── Timing ───────────────────────────────────────────────
#define TX_INTERVAL_MS        60000
#define LOOP_DELAY_MS         100
#define GPS_FIX_TIMEOUT_MS    120000
#define JOIN_RETRY_DELAY_MS   10000

// ─── Payload ──────────────────────────────────────────────
// 11 bytes: [lat LE 4B][lng LE 4B][sats|fix|gp26][GPIOs 1B][ADC 1B]
#define PAYLOAD_LEN           11
#define PAYLOAD_TYPE_GPS      0x01

// ─── GPS ──────────────────────────────────────────────────
#define MIN_SATS              4

// ─── LED onboard ─────────────────────────────────────────
#define PIN_LED               4        // LED T-Beam V1.1 confirmado en GPIO4
#define DOWNLINK_MAX          64       // Buffer maximo para downlink

// ─── GPIOs disponibles ────────────────────────────────────
const uint8_t gpioPins[] = {2, 13, 14, 15, 16, 17, 25, 26};  // GPIO4 es LED
const uint8_t gpioCount  = 8;
#define PIN_ADC              36       // ADC1_CH0 (GPIO36, solo entrada)

// ─── Objetos globales ─────────────────────────────────────
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
LoRaWANNode node(&radio, &AU915, LORAWAN_SUBBAND);
XPowersAXP192 PMU;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);

// ─── Estado ───────────────────────────────────────────────
bool    joined          = false;
bool    gpsFixObtained  = false;
bool    gpsTimedOut     = false;
int32_t lastLat         = 0;
int32_t lastLng         = 0;
uint8_t lastSats        = 0;

// ─── Downlink ─────────────────────────────────────────────
uint8_t downlinkBuf[DOWNLINK_MAX];
size_t  downlinkLen = 0;

// ─── Helpers ──────────────────────────────────────────────
void printHex(const char* label, const uint8_t* buf, size_t len) {
  Serial.print(label);
  for (size_t i = 0; i < len; i++) {
    if (buf[i] < 0x10) Serial.print('0');
    Serial.print(buf[i], HEX);
  }
  Serial.println();
}

const char* lorawanErrorStr(int16_t code) {
  switch (code) {
    case 0:                                  return "OK";
    case RADIOLIB_LORAWAN_NEW_SESSION:       return "NEW_SESSION";
    case RADIOLIB_LORAWAN_SESSION_RESTORED:  return "SESSION_RESTORED";
    case RADIOLIB_ERR_NETWORK_NOT_JOINED:    return "NOT_JOINED";
    case RADIOLIB_ERR_TX_TIMEOUT:            return "TX_TIMEOUT";
    case RADIOLIB_ERR_RX_TIMEOUT:            return "RX_TIMEOUT";
    case RADIOLIB_ERR_CHIP_NOT_FOUND:        return "CHIP_NOT_FOUND";
    case RADIOLIB_ERR_SPI_CMD_FAILED:        return "SPI_FAILED";
    case RADIOLIB_LORAWAN_DOWNLINK:          return "DOWNLINK";
    case RADIOLIB_ERR_INVALID_FREQUENCY:     return "INVALID_FREQ";
    case -1116:                              return "NO_JOIN_ACCEPT";
    case -1115:                              return "JOIN_NONCE_ERROR";
    default:                                 return "UNKNOWN";
  }
}

// ─── Inicialización AXP192 ────────────────────────────────
bool axp192_init() {
  Serial.print(F("[AXP192] Init ... "));
  if (!PMU.init(Wire, PIN_I2C_SDA, PIN_I2C_SCL, AXP192_SLAVE_ADDRESS)) {
    Serial.println(F("FAIL"));
    return false;
  }
  PMU.setLDO2Voltage(3300);
  PMU.enableLDO2();
  PMU.setLDO3Voltage(3300);
  PMU.enableLDO3();
  Serial.println(F("OK (LDO2=LoRa, LDO3=GPS)"));
  return true;
}

// ─── Inicialización SX1262 + DIO2 RF switch + TCXO ─────────
bool lora_init() {
  SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);

  // begin() simple + TCXO explicito (necesario para timing RX preciso)
  Serial.print(F("[SX1262] begin() ... "));
  int state = radio.begin();
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("FAIL (%d: %s)\n", state, lorawanErrorStr(state));
    return false;
  }
  Serial.println(F("OK"));

  // TCXO: probar 1.8V, luego 3.3V (esencial para ventanas RX precisas)
  Serial.print(F("[SX1262] TCXO 1.8V ... "));
  state = radio.setTCXO(1.8);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("fail(%d) ", state);
    state = radio.setTCXO(3.3);
    Serial.println(state == RADIOLIB_ERR_NONE ? F("3.3V OK") : F("WARN sin TCXO"));
  } else {
    Serial.println(F("OK"));
  }

  // ─── CRÍTICO: DIO2 como RF switch control ───────────────
  Serial.print(F("[SX1262] DIO2 RF switch ... "));
  state = radio.setDio2AsRfSwitch(true);
  if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("OK"));
  } else {
    Serial.printf("WARN (%d) - continuando\n", state);
  }

  // ─── Configurar potencia de transmisión ──────────────────
  Serial.print(F("[SX1262] TX power 20dBm ... "));
  state = radio.setOutputPower(20);
  Serial.println(state == RADIOLIB_ERR_NONE ? "OK" : "WARN");

  return true;
}

// ─── Inicialización GPS ───────────────────────────────────
void gps_init() {
  gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
  Serial.println(F("[GPS] Serial2 iniciado (9600 baud)"));
}

// ─── Inicialización LoRaWAN con NVS persistencia ─────────────
bool lorawan_init() {
  Serial.print(F("[LoRaWAN] Config OTAA ... "));
  int state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
  if (state != RADIOLIB_ERR_NONE) {
    Serial.printf("FAIL (%d: %s)\n", state, lorawanErrorStr(state));
    return false;
  }
  Serial.println(F("OK"));

  // Restaurar sesion desde NVS (DevNonce, frame counters, etc)
  Preferences prefs;
  prefs.begin("lorawan", false);
  size_t savedLen = prefs.getBytesLength("session");
  if (savedLen > 0) {
    uint8_t* sessionBuf = new uint8_t[savedLen];
    prefs.getBytes("session", sessionBuf, savedLen);
    int16_t restoreState = node.setBufferNonces(sessionBuf);
    delete[] sessionBuf;
    if (restoreState == RADIOLIB_ERR_NONE) {
      Serial.println(F("[LoRaWAN] Sesion restaurada desde NVS"));
    }
  }
  prefs.end();

  node.scanGuard = 200;
  Serial.println(F("[LoRaWAN] scanGuard=200ms"));

  Serial.print(F("[LoRaWAN] Join OTAA ... "));
  state = node.activateOTAA();
  if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
    joined = true;
    Serial.println(F("JOINED !!!"));
    // Guardar sesion en NVS para sobrevivir power-cycles
    uint8_t* buf = node.getBufferNonces();
    Preferences p;
    p.begin("lorawan", false);
    p.putBytes("session", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
    p.end();
    Serial.println(F("[LoRaWAN] Sesion guardada en NVS"));
    Serial.printf("[LoRaWAN] DevAddr: 0x%08X\n", node.getDevAddr());
  } else {
    Serial.printf("FAIL (%d: %s) - reintentando en loop\n", state, lorawanErrorStr(state));
    return false;
  }
  return true;
}

// ─── Actualización GPS (no bloqueante) ────────────────────
void gps_update() {
  while (gpsSerial.available()) {
    gps.encode(gpsSerial.read());
  }
}

// ─── Verificar fix GPS ────────────────────────────────────
bool gps_check_fix() {
  return gps.location.isValid() &&
         gps.satellites.isValid() &&
         gps.satellites.value() >= MIN_SATS &&
         gps.location.age() < 15000;
}

// ─── Leer 8 GPIOs (byte único) ────────────────────────────
uint8_t read_gpio_byte() {
  uint8_t val = 0;
  // GPIO4 es LED (OUTPUT), no se lee
  if (digitalRead(2))  val |= 0x01;
  // bit 1 = libre (era GPIO4)
  if (digitalRead(13)) val |= 0x04;
  if (digitalRead(14)) val |= 0x08;
  if (digitalRead(15)) val |= 0x10;
  if (digitalRead(16)) val |= 0x20;
  if (digitalRead(17)) val |= 0x40;
  if (digitalRead(25)) val |= 0x80;
  return val;
}

// ─── Construir payload 11B: [lat LE][lng LE][sats|fix|gp26][gpios][adc] ──
void build_payload(uint8_t* payload, bool hasFix) {
  // Bytes 0-3: latitud * 1e7, int32 LE
  int32_t lat = hasFix ? (int32_t)(gps.location.lat() * 1e7) : lastLat;
  memcpy(payload, &lat, 4);

  // Bytes 4-7: longitud * 1e7, int32 LE
  int32_t lng = hasFix ? (int32_t)(gps.location.lng() * 1e7) : lastLng;
  memcpy(payload + 4, &lng, 4);

  // Byte 8: [7:4]=sats(0-15), [3]=GPS_fix, [2:1]=reserved(0), [0]=GPIO26
  uint8_t sats = hasFix ? (uint8_t)gps.satellites.value() : lastSats;
  payload[8] = ((sats & 0x0F) << 4);
  if (hasFix) payload[8] |= 0x08;           // bit 3 = GPS fix
  if (digitalRead(26)) payload[8] |= 0x01;  // bit 0 = GPIO26

  // Byte 9: GPIOs 2,4,13,14,15,16,17,25
  payload[9] = read_gpio_byte();

  // Byte 10: ADC GPIO36 escalado a 8 bits (0-255 → 0-3.3V)
  uint16_t adcRaw = analogRead(PIN_ADC);
  payload[10] = (uint8_t)((adcRaw * 255UL) / 4095);
}

// ─── Procesar comando downlink ─────────────────────────────
// Protocolo: [CMD 1B] [PARAM...]
//   0x01 = LED:  0x01=ON, 0x00=OFF, 0x02=TOGGLE, 0x03=BLINK(count)
//   0x02 = STATUS: responder con estado en proximo uplink
void processDownlink(const uint8_t* data, size_t len) {
  if (len == 0) return;

  Serial.print(F("[DOWNLINK] CMD="));
  Serial.print(data[0], HEX);

  switch (data[0]) {
    case 0x01:  // LED control (LOW=ON, HIGH=OFF)
      if (len >= 2) {
        if (data[1] == 0x01) {
          digitalWrite(PIN_LED, LOW);
          Serial.println(F(" -> LED ON"));
        } else if (data[1] == 0x00) {
          digitalWrite(PIN_LED, HIGH);
          Serial.println(F(" -> LED OFF"));
        } else if (data[1] == 0x02) {
          digitalWrite(PIN_LED, !digitalRead(PIN_LED));
          Serial.println(F(" -> LED TOGGLE"));
        } else if (data[1] == 0x03 && len >= 3) {
          Serial.printf(" -> LED BLINK x%d\n", data[2]);
          for (int i = 0; i < data[2]; i++) {
            digitalWrite(PIN_LED, LOW);  delay(200);
            digitalWrite(PIN_LED, HIGH); delay(200);
          }
        }
      }
      break;
    case 0x02:  // Status
      Serial.println(F(" -> STATUS"));
      digitalWrite(PIN_LED, LOW);  delay(100);
      digitalWrite(PIN_LED, HIGH); delay(100);
      digitalWrite(PIN_LED, LOW);  delay(100);
      digitalWrite(PIN_LED, HIGH);
      break;
    default:
      Serial.printf(" -> CMD desconocido (len=%d): ", len);
      for (size_t i = 0; i < len; i++) Serial.printf("%02X ", data[i]);
      Serial.println();
  }
}

// ─── Manejar resultado de transmisión (I-001 corregido) ───
void handle_tx_result(int16_t state) {
  if (state > 0) {
    Serial.printf("[TX] OK + downlink (%d bytes): ", downlinkLen);
    for (size_t i = 0; i < downlinkLen; i++) Serial.printf("%02X ", downlinkBuf[i]);
    Serial.println();
    processDownlink(downlinkBuf, downlinkLen);
  } else if (state == RADIOLIB_ERR_NONE) {
    Serial.println(F("[TX] OK (sin downlink)"));
  } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
    Serial.println(F("[TX] OK (no downlink pendiente - clase A normal)"));
  } else {
    Serial.printf("[TX] ERROR %d (%s)\n", state, lorawanErrorStr(state));
    if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
      joined = false;
    }
  }
}

// ─── SETUP ────────────────────────────────────────────────
void setup() {
  Serial.begin(SERIAL_BAUD);
  Serial.println();  // línea en blanco inicial
  Serial.println(F("BOOT: Serial OK"));

  // LED onboard (LOW=ON, HIGH=OFF)
  pinMode(PIN_LED, OUTPUT);
  digitalWrite(PIN_LED, HIGH);  // OFF
  for (int i = 0; i < 3; i++) {
    digitalWrite(PIN_LED, LOW);  delay(150);   // ON
    digitalWrite(PIN_LED, HIGH); delay(150);   // OFF
  }
  Serial.println(F("BOOT: LED GPIO4 listo"));

  // GPIOs como entradas pull-up (HIGH=abierto, LOW=cerrado a GND)
  for (uint8_t i = 0; i < gpioCount; i++) {
    pinMode(gpioPins[i], INPUT_PULLUP);
  }
  Serial.printf("BOOT: %d GPIOs configurados INPUT_PULLUP\n", gpioCount);

  // ADC 12-bit en GPIO36
  analogReadResolution(12);
  Serial.println(F("BOOT: ADC GPIO36 (12-bit, 0-4095) listo"));

  Serial.println(F("BOOT: TTGO T-Beam LoRaWAN -> Milesight UG56"));
  Serial.print(F("BOOT: DevEUI=")); Serial.println((uint32_t)(devEUI>>32), HEX);
  Serial.print(F("BOOT: JoinEUI=0x")); Serial.println((uint32_t)(joinEUI>>32), HEX);
  Serial.print(F("BOOT: Subbanda=")); Serial.println(LORAWAN_SUBBAND);

  // 1. I2C + AXP192
  Serial.println(F("BOOT: Iniciando I2C..."));
  Wire.begin(PIN_I2C_SDA, PIN_I2C_SCL);
  Serial.println(F("BOOT: Iniciando AXP192..."));
  axp192_init();

  // 2. GPS
  Serial.println(F("BOOT: Iniciando GPS..."));
  gps_init();

  // 3. LoRa SX1262 + DIO2 RF switch
  Serial.println(F("BOOT: Iniciando LoRa..."));
  if (!lora_init()) {
    Serial.println(F("FATAL: SX1262 no responde."));
    while (1) delay(1000);
  }

  // 4. LoRaWAN join
  Serial.println(F("BOOT: Iniciando LoRaWAN join..."));
  if (!lorawan_init()) {
    Serial.println(F("WARN: Join inicial fallido - reintentando en loop"));
  }

  Serial.println(F("BOOT: Listo. Entrando en loop.\n"));
}

// ─── LOOP ─────────────────────────────────────────────────
void loop() {
  // 1. Procesar GPS
  gps_update();

  // 2a. Timeout de fix GPS (I-002)
  static uint32_t bootTime = millis();
  if (!gpsFixObtained && !gpsTimedOut && (millis() - bootTime > GPS_FIX_TIMEOUT_MS)) {
    gpsTimedOut = true;
    Serial.println(F("[GPS] Timeout 120s - usando placeholder (0,0,0) para uplinks"));
  }

  // 2b. Verificar fix GPS
  if (gps_check_fix()) {
    if (!gpsFixObtained) {
      gpsFixObtained = true;
      Serial.printf("[GPS] FIX: lat=%.7f lng=%.7f sats=%d hdop=%.1f\n",
                    gps.location.lat(), gps.location.lng(),
                    (int)gps.satellites.value(),
                    gps.hdop.isValid() ? gps.hdop.hdop() : 0.0);
    }
    lastLat  = (int32_t)(gps.location.lat() * 1e7);
    lastLng  = (int32_t)(gps.location.lng() * 1e7);
    lastSats = (uint8_t)gps.satellites.value();
  }

  // 3. Estado periódico cada 15s
  static uint32_t lastStatus = 0;
  if (millis() - lastStatus > 15000) {
    lastStatus = millis();
    Serial.printf("[GPS] chars=%u sats=%d fix=%s joined=%s\n",
                  gps.charsProcessed(),
                  gps.satellites.isValid() ? (int)gps.satellites.value() : -1,
                  gpsFixObtained ? "SI" : "no",
                  joined ? "SI" : "no");
  }

  // 4a. Rejoin con timer independiente de 10s (I-003, I-004)
  static uint32_t lastJoinAttempt = 0;
  if (!joined && (millis() - lastJoinAttempt >= JOIN_RETRY_DELAY_MS)) {
    lastJoinAttempt = millis();
    Serial.print(F("[LoRaWAN] Rejoin OTAA ... "));
    node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
    // Restaurar sesion NVS antes del rejoin
    Preferences pr;
    pr.begin("lorawan", false);
    size_t sl = pr.getBytesLength("session");
    if (sl > 0) {
      uint8_t* sb = new uint8_t[sl];
      pr.getBytes("session", sb, sl);
      node.setBufferNonces(sb);
      delete[] sb;
    }
    pr.end();
    int16_t j = node.activateOTAA();
    if (j == RADIOLIB_LORAWAN_NEW_SESSION || j == RADIOLIB_LORAWAN_SESSION_RESTORED) {
      joined = true;
      Serial.println(F("JOINED !!!"));
      uint8_t* buf = node.getBufferNonces();
      Preferences p;
      p.begin("lorawan", false);
      p.putBytes("session", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);
      p.end();
      Serial.println(F("[LoRaWAN] Sesion guardada en NVS"));
      Serial.printf("[LoRaWAN] DevAddr: 0x%08X\n", node.getDevAddr());
    } else {
      Serial.printf("FAIL (%d: %s)\n", j, lorawanErrorStr(j));
    }
  }

  // 4b. Transmisión LoRaWAN cada 60s
  static uint32_t lastTx = 0;
  if (millis() - lastTx >= TX_INTERVAL_MS) {
    lastTx = millis();

    // Transmitir si hay sesión o si hay timeout/placeholder GPS
    if (joined && (gpsFixObtained || gpsTimedOut)) {
      uint8_t payload[PAYLOAD_LEN];
      build_payload(payload, gpsFixObtained);

      Serial.print(F("[TX] Puerto 1, payload="));
      printHex("", payload, PAYLOAD_LEN);
      downlinkLen = DOWNLINK_MAX;
      int16_t state = node.sendReceive(payload, PAYLOAD_LEN, LORAWAN_PORT,
                                       downlinkBuf, &downlinkLen);
      handle_tx_result(state);
    } else if (!joined) {
      Serial.println(F("[TX] Sin sesion - omitiendo uplink"));
    } else {
      Serial.println(F("[TX] Esperando fix GPS - omitiendo uplink"));
    }
  }

  delay(LOOP_DELAY_MS);
}
