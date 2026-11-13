# TTGO T-Beam V1.1 — Nodo LoRaWAN para Gateway Milesight UG56

Firmware para **TTGO T-Beam V1.1** (ESP32 + SX1262) que envía datos de geolocalización GPS, estados GPIO y lectura ADC vía LoRaWAN a un gateway **Milesight UG56** con servidor de red embebido. Control remoto de LED integrado vía downlink.

---

## 🎯 Funcionalidades

| Funcionalidad | Detalle |
|---------------|---------|
| **Join OTAA** | AU915 subbanda 2, LoRaWAN 1.0.4 |
| **Uplink** | Cada 60s — 11 bytes: GPS + 7 GPIOs + ADC |
| **Downlink** | Control de LED (ON/OFF/TOGGLE/BLINK) |
| **GPS** | u-blox NEO-M8N, fix ≥4 satélites |
| **GPIOs** | 2, 13, 14, 15, 16, 17, 25, 26 como entradas pull-up |
| **ADC** | GPIO36, 12-bit escalado a 8-bit (0-3.3V) |
| **Reconexión** | Auto-rejoin cada 10s si pierde sesión |

---

## 🔧 Resolución del problema de RF

El TTGO T-Beam V1.1 tiene un **RF switch PE4259** entre el SX1262 y el conector SMA de la antena, controlado por el **pin DIO2 del SX1262**. Sin configurarlo, las transmisiones nunca llegan a la antena.

```cpp
// CRÍTICO: sin esto, el gateway no ve ningún paquete
radio.setDio2AsRfSwitch(true);
```

Además:
- **Región AU915** (no US915) — el UG56 opera en frecuencias AU915
- **JoinEUI `24E124C0002ADD01`** — asignado automáticamente por el NS del UG56
- **`node.scanGuard = 200`** — tiempo de guarda entre canales
- **TX Power 20dBm**, **TCXO 1.8V** para precisión de frecuencia

---

## 📦 Formato del Payload (11 bytes)

| Byte | Campo | Descripción |
|------|-------|-------------|
| 0-3 | `lat` | Latitud × 10⁷, int32 LE |
| 4-7 | `lng` | Longitud × 10⁷, int32 LE |
| 8 | `sats+fix+gp26` | [7:4]=satélites, [3]=GPS fix, [0]=GPIO26 |
| 9 | `gpios` | [7]=GPIO25, [6]=17, [5]=16, [4]=15, [3]=14, [2]=13, [1]=reservado, [0]=GPIO2 |
| 10 | `adc` | ADC GPIO36, 0-255 (0-3.3V) |

---

## 🔌 Pinout

| GPIO | Función |
|------|---------|
| 18,5,27,19 | SX1262 SPI |
| 23 | SX1262 RST |
| 32 | SX1262 BUSY |
| 33 | SX1262 DIO1 |
| DIO2 | RF Switch (interno, no GPIO) |
| 12,34 | GPS UART |
| 21,22 | AXP192 I2C |
| 4 | LED azul (LOW=ON) |
| 36 | ADC (0-3.3V) |
| 2,13,14,15,16,17,25,26 | GPIOs entrada |

---

## 📡 Comandos Downlink (Port 1)

| Hex | Acción |
|-----|--------|
| `01 01` | LED ON |
| `01 00` | LED OFF |
| `01 02` | LED TOGGLE |
| `01 03 N` | LED BLINK N veces |
| `02` | STATUS (triple flash) |

---

## 🛠️ Compilación y Carga

```bash
# Compilar
arduino-cli compile --fqbn esp32:esp32:t-beam .

# Subir
arduino-cli upload --fqbn esp32:esp32:t-beam --port /dev/ttyUSB0 .

# Monitor serial
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

### Requisitos
- **Core ESP32:** esp32:esp32 3.3.11 (Espressif)
- **RadioLib:** 7.7.1
- **TinyGPSPlus:** 1.1.0
- **XPowersLib:** 0.3.1

---

## 🔑 Credenciales (v9)

| Parámetro | Valor |
|-----------|-------|
| DevEUI | `70B3D57E5F0040E4` |
| JoinEUI | `24E124C0002ADD01` |
| AppKey | `CBD4C4992A115BC20118C53A4C1CC0D5` |
| Región | AU915, subbanda 2 |

---

## 📊 Decoder para UG56

El archivo `docs/decoder_ug56.txt` contiene el payload decoder JavaScript. Pegarlo en:
**UG56 → Device Profile → Custom Payload Codec**

---

## ⚠️ Advertencias

- **Conectar antena LoRa antes de energizar** — sin antena se daña la etapa de RF
- **GPS requiere cielo abierto** — indoor: sats=0
- **ADC GPIO36 flotante** — conectar a GND o fuente para lectura estable
- **NO usar GPIO12** (strap MTDI), **GPIO0** (BOOT), **GPIO5** (strap)

---

## 🐛 Troubleshooting

| Error | Causa | Solución |
|-------|-------|----------|
| `-1116 NO_JOIN_ACCEPT` | NS rechaza join | Verificar credenciales, endianidad, reset DevNonces |
| `-4 PACKET_TOO_LONG` | Payload >11B en DR bajo | Reducir payload o esperar ADR |
| Stack smashing | Buffer overflow | Verificar PAYLOAD_LEN |
| Gateway no ve paquetes | RF switch no configurado | `radio.setDio2AsRfSwitch(true)` |
| LED no enciende | GPIO4 pisado por INPUT_PULLUP | GPIO4 es solo LED, no está en gpioPins[] |
