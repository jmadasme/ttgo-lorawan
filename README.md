# TTGO LoRaWAN · T-Beam · SX1262

![Estado](https://img.shields.io/badge/estado-preservado-2A9D8F?style=flat-square)
![Versiones](https://img.shields.io/badge/versiones-1-457B9D?style=flat-square)
![Sección](https://img.shields.io/badge/sección-iot-5B7C99?style=flat-square)
![Licencia](https://img.shields.io/badge/licencia-MIT-06141b?style=flat-square)

> Nodo TTGO T-Beam v1.1 con SX1262, GPS y telemetría hacia un gateway Milesight UG56.

Este repositorio reúne **1 versión** de TTGO LoRaWAN en `versions/`.
Cada carpeta conserva el contenido original de su etapa, sin `.git`, dependencias
instaladas ni binarios pesados. La documentación de operación y de código está en
[`docs/`](docs/), y el sitio del proyecto en [`docs/index.html`](docs/index.html).

## Índice de versiones

| Versión | Qué aporta | Archivos |
|---|---|---:|
| [`TTGO_LoRaWAN_Milesight`](versions/TTGO_LoRaWAN_Milesight/) | Nodo T-Beam con GPS, GPIO, ADC, downlink de LED y ajuste del switch RF. | 8 |

La versión de referencia de esta familia es **`TTGO_LoRaWAN_Milesight`**: es la que se usa
para el README base y para los documentos canónicos. Las demás se mantienen
completas para que se pueda reconstruir la evolución y comparar decisiones.

## Arranque rápido

```bash
git clone https://github.com/jmadasme/ttgo-lorawan.git
cd ttgo-lorawan/versions/TTGO_LoRaWAN_Milesight
# revisá pinout, región y claves antes de compilar
arduino-cli compile --fqbn esp32:esp32:tbeam .
```

El FQBN exacto depende del core instalado. Las claves LoRaWAN y la configuración regional no se incluyen.

<!-- ficha -->
## Ficha de evaluación

**Madurez global: 3.5/5**

| Criterio | Nota | Evidencia |
|---|---:|---|
| Madurez | 3/5 | Diez iteraciones de firmware con join estable y downlink probado; no hay cola ni confirmación de aplicación. |
| Reproducibilidad | 3/5 | Pinout, región y claves están documentados; el datasheet aún declara US915 mientras el código final usa AU915. |
| Documentación | 5/5 | Bitácora con síntoma, causa y corrección para ocho problemas, más datasheet y guía de desarrollador. |
| Honestidad de límites | 4/5 | Expone el conflicto de GPIO4, la dependencia de un gateway concreto y la falta de confirmación en downlink. |
| Valor demostrativo | 4/5 | Concentra los errores típicos de un bring-up LoRaWAN con SX1262 en un caso verificable de punta a punta. |
| Facilidad de despliegue | 2/5 | Requiere claves OTAA, gateway compatible y revisión de pinout antes de compilar y cargar el nodo. |

Bring-up LoRaWAN documentado con detalle: cuatro causas distintas de un nodo que no se une y su corrección. El firmware sirve para T-Beam v1.1 y un gateway Milesight UG56.

## Lo que no funcionó

### TTGO_LoRaWAN_Milesight — Switch RF sin configurar

El código original no activaba el control del switch PE4259, así que el SX1262 respondía por SPI pero no emitía señal. El gateway no veía ni los Join Requests y el dispositivo figuraba como nunca activado. Se resolvió con radio.setDio2AsRfSwitch(true).

### TTGO_LoRaWAN_Milesight — Región y JoinEUI equivocados

El firmware usaba US915 y un JoinEUI generado a mano, mientras el UG56 operaba en AU915 con un JoinEUI propio que asigna al registrar. Los paquetes salían en una banda que el gateway no monitoreaba. Se cambió la región y se tomó el JoinEUI observado en los Incoming Packets.

### TTGO_LoRaWAN_Milesight — Payload demasiado largo

Catorce bytes provocaban PACKET_TOO_LONG en DR0-DR2, y doce seguían desbordando hasta causar un crash por stack smashing. Se redujo a once bytes exactos, con GPS, GPIO y ADC comprimidos en ese espacio, y el error desapareció en las pruebas siguientes.

### TTGO_LoRaWAN_Milesight — DevNonce repetido tras cada reinicio

Cada ciclo de alimentación reiniciaba el DevNonce a cero y el servidor rechazaba el join por anti-replay, con código -1116. La solución fue guardar y restaurar el buffer de nonces en NVS, de modo que solo un reflash del firmware limpia la sesión.

## Preguntas típicas

**¿Qué placa es?**

TTGO T-Beam v1.1 con ESP32 y SX1262.

**¿Por qué no transmite?**

Revisá la región, las claves y el switch RF PE4259/DIO2.

**¿El GPS funciona en interiores?**

No. Necesita cielo abierto para obtener fix.

**¿Qué gateway usa?**

Milesight UG56, aunque otro gateway LoRaWAN compatible debería servir.

**¿El downlink es confiable?**

Es una acción simple sobre el LED; no hay cola ni confirmación de aplicación.

<!-- /ficha -->

## Documentación

- [`docs/datasheet.md`](docs/datasheet.md) — instalación, operación y límites.
- [`docs/developer.md`](docs/developer.md) — arquitectura, módulos y extensión.
- [`docs/REFERENCES.md`](docs/REFERENCES.md) — estándares, librerías y fuentes.
- [`docs/index.html`](docs/index.html) — página del proyecto (GitHub Pages).

## El proyecto en detalle

### TTGO T-Beam V1.1 — Nodo LoRaWAN para Gateway Milesight UG56

Firmware para **TTGO T-Beam V1.1** (ESP32 + SX1262) que envía datos de geolocalización GPS, estados GPIO y lectura ADC vía LoRaWAN a un gateway **Milesight UG56** con servidor de red embebido. Control remoto de LED integrado vía downlink.

---

### 🎯 Funcionalidades

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

### 🔧 Resolución del problema de RF

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

### 📦 Formato del Payload (11 bytes)

| Byte | Campo | Descripción |
|------|-------|-------------|
| 0-3 | `lat` | Latitud × 10⁷, int32 LE |
| 4-7 | `lng` | Longitud × 10⁷, int32 LE |
| 8 | `sats+fix+gp26` | [7:4]=satélites, [3]=GPS fix, [0]=GPIO26 |
| 9 | `gpios` | [7]=GPIO25, [6]=17, [5]=16, [4]=15, [3]=14, [2]=13, [1]=reservado, [0]=GPIO2 |
| 10 | `adc` | ADC GPIO36, 0-255 (0-3.3V) |

---

### 🔌 Pinout

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

### 📡 Comandos Downlink (Port 1)

| Hex | Acción |
|-----|--------|
| `01 01` | LED ON |
| `01 00` | LED OFF |
| `01 02` | LED TOGGLE |
| `01 03 N` | LED BLINK N veces |
| `02` | STATUS (triple flash) |

---

### 🛠️ Compilación y Carga

```bash
# Compilar
arduino-cli compile --fqbn esp32:esp32:t-beam .

# Subir
arduino-cli upload --fqbn esp32:esp32:t-beam --port /dev/ttyUSB0 .

# Monitor serial
arduino-cli monitor --port /dev/ttyUSB0 --config baudrate=115200
```

#### Requisitos
- **Core ESP32:** esp32:esp32 3.3.11 (Espressif)
- **RadioLib:** 7.7.1
- **TinyGPSPlus:** 1.1.0
- **XPowersLib:** 0.3.1

---

### 🔑 Credenciales (v9)

| Parámetro | Valor |
|-----------|-------|
| DevEUI | `70B3D57E5F0040E4` |
| JoinEUI | `24E124C0002ADD01` |
| AppKey | `CBD4C4992A115BC20118C53A4C1CC0D5` |
| Región | AU915, subbanda 2 |

---

### 📊 Decoder para UG56

El archivo `docs/decoder_ug56.txt` contiene el payload decoder JavaScript. Pegarlo en:
**UG56 → Device Profile → Custom Payload Codec**

---

### ⚠️ Advertencias

- **Conectar antena LoRa antes de energizar** — sin antena se daña la etapa de RF
- **GPS requiere cielo abierto** — indoor: sats=0
- **ADC GPIO36 flotante** — conectar a GND o fuente para lectura estable
- **NO usar GPIO12** (strap MTDI), **GPIO0** (BOOT), **GPIO5** (strap)

---

### 🐛 Troubleshooting

| Error | Causa | Solución |
|-------|-------|----------|
| `-1116 NO_JOIN_ACCEPT` | NS rechaza join | Verificar credenciales, endianidad, reset DevNonces |
| `-4 PACKET_TOO_LONG` | Payload >11B en DR bajo | Reducir payload o esperar ADR |
| Stack smashing | Buffer overflow | Verificar PAYLOAD_LEN |
| Gateway no ve paquetes | RF switch no configurado | `radio.setDio2AsRfSwitch(true)` |
| LED no enciende | GPIO4 pisado por INPUT_PULLUP | GPIO4 es solo LED, no está en gpioPins[] |

---

Licencia MIT © 2026 jmadasme. Las versiones se publican como material de
referencia; los nombres de hardware y marcas pertenecen a sus respectivos dueños.
