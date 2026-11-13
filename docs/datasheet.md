# Datasheet — TTGO T-Beam LoRaWAN Milesight UG56

## Información General

### Descripción del Producto

Firmware para el nodo sensor LoRaWAN basado en **TTGO T-Beam V1.1** (ESP32 + SX1262 + GPS NEO-M8N) que reporta posición geográfica a un gateway **Milesight UG56** con servidor de red LoRaWAN embebido.

- **Nombre del firmware**: TTGO_LoRaWAN_Milesight v1.0
- **Tipo de dispositivo**: Nodo sensor LoRaWAN Clase A
- **Caso de uso principal**: Rastreo GPS de activos vía LoRaWAN US915

---

## Especificaciones Técnicas

### Placa Base

| Parámetro | Valor |
|-----------|-------|
| **Modelo** | LilyGO TTGO T-Beam V1.1 |
| **Microcontrolador** | ESP32-D0WDQ6-V3 |
| **Arquitectura CPU** | Xtensa LX6 Dual Core, 32-bit |
| **Frecuencia de reloj** | 240 MHz |
| **Memoria Flash** | 4 MB (SPI Flash externo) |
| **PSRAM** | 8 MB (SPI PSRAM externo) |
| **SRAM interna** | 520 KB |
| **Voltaje de operación** | 3.3V (regulado internamente por AXP192) |
| **Alimentación** | USB-C 5V o batería LiPo 3.7V (18650) |
| **Consumo en reposo** | ~35 mA (ESP32 + SX1262 idle + GPS activo) |
| **Consumo en TX (20 dBm)** | ~120 mA pico |
| **Temperatura de operación** | -10°C a +60°C |
| **Dimensiones** | 91.4 × 25.4 × 11 mm |
| **Peso** | ~35 g (sin batería) |

### Radio LoRa (SX1262)

| Parámetro | Valor |
|-----------|-------|
| **Chip** | Semtech SX1262 |
| **Banda de frecuencia** | 902-928 MHz (US915) |
| **Potencia máxima de transmisión** | +20 dBm (100 mW) |
| **Sensibilidad de recepción** | -148 dBm (SF12, BW=125 kHz) |
| **Modulación** | LoRa (CSS - Chirp Spread Spectrum) |
| **Spreading Factor** | SF7 a SF12 (adaptativo según ADR) |
| **Ancho de banda** | 125 kHz, 250 kHz, 500 kHz |
| **Data Rate** | DR0 (SF10/125kHz) a DR4 (SF8/500kHz) para US915 |
| **Interfaz** | SPI (hasta 10 MHz) |
| **RF Switch** | PE4259 (controlado por DIO2 del SX1262) |
| **Impedancia de antena** | 50 ohm, conector SMA hembra |

### GPS (NEO-M8N)

| Parámetro | Valor |
|-----------|-------|
| **Módulo** | u-blox NEO-M8N |
| **Constelaciones** | GPS, GLONASS, Galileo, BeiDou |
| **Canales** | 72 (búsqueda paralela) |
| **Sensibilidad** | -167 dBm (tracking), -148 dBm (cold start) |
| **Precisión horizontal** | 2.5 m CEP (GPS autónomo) |
| **TTFF Cold Start** | 26 segundos (típico, cielo despejado) |
| **TTFF Hot Start** | 1 segundo |
| **Frecuencia de actualización** | 1 Hz (1 posición/segundo) |
| **Protocolo** | NMEA 0183 (GPGGA, GPRMC, GPGSA, GPGSV) |
| **Interfaz** | UART Serial, 9600 baud, 8N1 |
| **Antena** | Cerámica activa, 25×25×4 mm, conector u.FL/IPEX |
| **Voltaje** | 3.3V (desde LDO3 del AXP192) |

### PMU (AXP192)

| Parámetro | Valor |
|-----------|-------|
| **Chip** | X-Powers AXP192 |
| **Dirección I2C** | 0x34 |
| **Canales de salida** | DCDC1 (3.3V ESP32), LDO2 (3.3V SX1262), LDO3 (3.3V GPS), DCDC2, DCDC3, LDO1 |
| **Carga de batería** | Integrada, hasta 1.2A, perfil LiPo 4.2V |
| **Protecciones** | Sobrevoltaje, sobrecorriente, sobretemperatura, descarga profunda |
| **Medición** | Voltaje de batería, corriente de carga/descarga, temperatura |

---

## LoRaWAN

### Parámetros de Red

| Parámetro | Valor |
|-----------|-------|
| **Especificación** | LoRaWAN 1.0.4 |
| **Clase** | A (dispositivo final bidireccional) |
| **Región** | US915 (FCC 902-928 MHz) |
| **Subbanda** | 2 (canales 8-15, 903.0-904.6 MHz uplink) |
| **Método de activación** | OTAA (Over-The-Air Activation) |
| **Canales de join** | 8 canales (902.3, 902.5, 902.7, 902.9, 903.1, 903.3, 903.5, 903.7 MHz) |
| **ADR** | Habilitado (Adaptive Data Rate) |
| **Puerto uplink** | 1 |
| **Intervalo TX** | 60 segundos (configurable) |
| **Tamaño máximo de payload** | 242 bytes (dependiendo del Data Rate) |

### Credenciales

| Parámetro | Valor (hex) |
|-----------|------------|
| **DevEUI** | `70 B3 D5 7E 5F 00 40 DC` |
| **JoinEUI** | `70 B3 D5 7E CD 80 89 C8` |
| **AppKey** | `CB D4 C4 99 2A 11 5B C2 01 18 C5 3A 4C 1C C0 ED` |
| **NwkKey** | Igual a AppKey (LoRaWAN 1.0.x) |

---

## Formato de Payload

### Payload Uplink (Puerto 1, 10 bytes)

```
Byte 0:     Tipo de mensaje
            0x01 = Fix GPS válido
            0x00 = Sin fix (placeholder)

Bytes 1-4:  Latitud (int32, little-endian)
            grados × 10,000,000
            Rango: -90,000,000 a +90,000,000

Bytes 5-8:  Longitud (int32, little-endian)
            grados × 10,000,000
            Rango: -180,000,000 a +180,000,000

Byte 9:     Satélites utilizados (uint8)
            Rango: 0-255
```

### Ejemplos

| Escenario | Payload (hex) | Latitud | Longitud | Sats |
|-----------|---------------|---------|----------|------|
| Bogotá, fix | `01 F8F2C502 60EAC5D3 0C` | 4.6527800 | -74.0900000 | 12 |
| Santiago, fix | `01 90C5B1EF A87BEE17 0A` | -33.4489000 | -70.6693000 | 10 |
| CDMX, fix | `01 50B1510D 92E39ED9 08` | 19.4326000 | -99.1332000 | 8 |
| Sin fix | `00 00000000 00000000 00` | 0.0000000 | 0.0000000 | 0 |

---

## Comportamiento Operativo

### Diagrama de Estados

```
                    ┌──────────┐
           Power ON │  BOOT    │
         ──────────▶│          │
                    └────┬─────┘
                         │
              ┌──────────▼──────────┐
              │   INIT              │
              │  • AXP192 LDO2/3 ON │
              │  • SX1262 + DIO2    │
              │  • GPS Serial       │
              └──────────┬──────────┘
                         │
              ┌──────────▼──────────┐
              │   JOIN              │◄──────────────┐
              │  • OTAA             │               │
              │  • 8 canales        │  cada 10s     │
              └──────┬──────┬───────┘               │
                     │      │                        │
              JOIN OK│      │ JOIN FAIL              │
                     │      └────────────────────────┘
              ┌──────▼──────┐
              │   JOINED    │
              │  • GPS fix  │
              │  • c/60s TX │
              └──────┬──────┘
                     │ session lost
                     │ (NOT_JOINED)
                     └──────────▶ REJOIN
```

### Secuencia de Inicialización

1. **AXP192 PMU** — Habilitar LDO2 (3.3V para SX1262) y LDO3 (3.3V para GPS)
2. **GPS** — Iniciar Serial2 a 9600 baud
3. **SX1262** — Inicializar SPI, verificar chip, **configurar DIO2 como RF switch**
4. **LoRaWAN** — Configurar OTAA con DevEUI/JoinEUI/AppKey, activar sesión
5. **Loop principal** — Procesar GPS + Transmitir cada 60s

### Timeouts y Reintentos

| Evento | Timeout | Acción |
|--------|---------|--------|
| **GPS fix inicial** | 120 segundos | Usar placeholder (lat=0, lng=0, sats=0) |
| **Join fallido** | 10 segundos | Reintentar `beginOTAA()` + `activateOTAA()` |
| **Intervalo TX** | 60 segundos | Transmitir payload de 10 bytes |
| **RX1 window** | 1 segundo tras TX | Abrir ventana de recepción |
| **RX2 window** | 2 segundos tras TX | Abrir segunda ventana (923.3 MHz, SF12/500kHz) |

---

## Diagnóstico Serial

### Formato de Mensajes

Todos los mensajes de diagnóstico se emiten por el puerto Serial USB a **115200 baud**.

#### Prefijos

| Prefijo | Significado |
|---------|-------------|
| `BOOT:` | Secuencia de inicialización |
| `[AXP192]` | Gestión de energía |
| `[SX1262]` | Radio LoRa |
| `[GPS]` | Receptor GPS |
| `[LoRaWAN]` | Pila LoRaWAN |
| `[TX]` | Transmisión de datos |
| `[RX]` | Recepción de downlink |

#### Ejemplos de Salida

```
BOOT: Serial OK
BOOT: TTGO T-Beam LoRaWAN -> Milesight UG56
BOOT: DevEUI=5F0040DC
BOOT: Subbanda=2
BOOT: Iniciando I2C...
BOOT: Iniciando AXP192...
[AXP192] Init ... OK (LDO2=LoRa, LDO3=GPS)
BOOT: Iniciando GPS...
[GPS] Serial2 iniciado (9600 baud)
BOOT: Iniciando LoRa...
[SX1262] begin() ... OK
[SX1262] DIO2 RF switch ... OK
BOOT: Iniciando LoRaWAN join...
[LoRaWAN] Config OTAA ... OK
[LoRaWAN] Join OTAA ... JOINED !!!
[LoRaWAN] DevAddr: 0x01A2B3C4
BOOT: Listo. Entrando en loop.
```

### Estado Periódico (cada 15 segundos)

```
[GPS] chars=12480 sats=12 fix=SI joined=SI
```

Campos:
- `chars`: Caracteres NMEA procesados desde el GPS
- `sats`: Satélites visibles (-1 si datos inválidos)
- `fix`: `SI` si tiene fijación GPS, `no` si no
- `joined`: `SI` si sesión LoRaWAN activa, `no` si no

---

## Guía de Solución de Problemas

### Tabla Rápida de Diagnóstico

| Mensaje de Error | Causa Probable | Acción Correctiva |
|-----------------|----------------|-------------------|
| `CHIP_NOT_FOUND` (-706) | SX1262 sin alimentación o mal conectado | Verificar AXP192 LDO2 (3.3V), revisar soldaduras SPI |
| `NO_JOIN_ACCEPT` (-1116) | Gateway no responde al Join Request | Verificar registro en UG56, credenciales, subbanda, endianness |
| `JOIN_NONCE_ERROR` (-1115) | Conflicto de nonce en el join | Reiniciar dispositivo (power cycle) |
| `NOT_JOINED` (-1106) | Sesión perdida | El firmware reintentará join automáticamente en 10s |
| `TX_TIMEOUT` (-1105) | Timeout esperando confirmación de TX | Verificar antena, distancia al gateway, interferencia |
| `SPI_FAILED` | Comunicación SPI con SX1262 interrumpida | Verificar conexiones, reiniciar SX1262 |
| `[AXP192] Init ... FAIL` | PMU no responde en I2C | Verificar alimentación, conexiones I2C, dirección 0x34 |
| `[GPS] Timeout 120s` | GPS no obtuvo fix en 2 minutos | Reubicar dispositivo con vista al cielo, esperar (cold start) |

### Árbol de Decisión — Join Fallido

```
¿[SX1262] DIO2 RF switch ... OK o WARN?
  ├── WARN ──▶ Falló configuración RF switch
  │           └─▶ Verificar versión RadioLib (7.7.1)
  │           └─▶ Verificar que el método setDio2AsRfSwitch existe
  │
  └── OK ────▶ ¿Gateway recibe paquetes?
                ├── NO ──▶ Problema de RF
                │         ├─▶ ¿Antena conectada al SMA correcto?
                │         ├─▶ ¿Antena es de 915 MHz?
                │         └─▶ ¿Distancia excesiva?
                │
                └── SÍ, pero NO_JOIN_ACCEPT ──▶ Problema de autenticación
                          ├─▶ ¿Dispositivo registrado en UG56?
                          ├─▶ ¿DevEUI correcto? (probar LSB/MSB)
                          ├─▶ ¿AppKey correcto?
                          ├─▶ ¿JoinEUI correcto? (probar 0000000000000000)
                          └─▶ ¿Subbanda correcta en el gateway?
```

### Árbol de Decisión — GPS sin Fix

```
¿[AXP192] Init ... OK (LDO2=LoRa, LDO3=GPS)?
  ├── NO o FAIL ──▶ El GPS no está recibiendo alimentación
  │                └─▶ Verificar PMU, batería, o usar solo USB
  │
  └── OK ────────▶ ¿chars procesados > 0?
                   ├── NO ──▶ El GPS no está enviando datos
                   │         └─▶ Verificar conexión Serial2 (GPIO 12/34)
                   │         └─▶ Probar invertir TX/RX
                   │
                   └── SÍ ──▶ ¿sats > 0 pero fix=no?
                              ├─▶ Esperar (GPS cold start: hasta 5 min)
                              ├─▶ Mover a exterior con vista al cielo
                              └─▶ Verificar orientación de antena GPS
```

---

## Instalación de Librerías

### Via Arduino Library Manager (IDE)

1. Abrir Arduino IDE
2. Ir a **Tools → Manage Libraries...**
3. Buscar e instalar:
   - **RadioLib** by Jan Gromeš (v7.7.1)
   - **TinyGPSPlus** by Mikal Hart (v1.1.0)
   - **XPowersLib** by Lewis He (v0.3.1)

### Via arduino-cli (Línea de comandos)

```bash
arduino-cli lib install "RadioLib@7.7.1"
arduino-cli lib install "TinyGPSPlus@1.1.0"
arduino-cli lib install "XPowersLib@0.3.1"
```

### Instalación Manual

1. Descargar los archivos ZIP de cada librería desde GitHub:
   - RadioLib: https://github.com/jgromes/RadioLib/releases/tag/7.7.1
   - TinyGPSPlus: https://github.com/mikalhart/TinyGPSPlus/releases/tag/1.1.0
   - XPowersLib: https://github.com/lewisxhe/XPowersLib/releases/tag/0.3.1
2. En Arduino IDE: **Sketch → Include Library → Add .ZIP Library...**
3. Seleccionar cada archivo ZIP descargado

### Verificación de Instalación

```bash
# Listar librerías instaladas
arduino-cli lib list

# Debe mostrar:
# RadioLib    7.7.1    Jan Gromeš
# TinyGPSPlus 1.1.0    Mikal Hart
# XPowersLib  0.3.1    Lewis He
```

---

## Compatibilidad con Otras Placas

### Tabla de Compatibilidad

| Placa | Compatibilidad | Notas |
|-------|---------------|-------|
| **TTGO T-Beam V1.0** | ✅ Compatible | Mismo pinout SX1262. Verificar versión de PMU (puede usar AXP2101 en lugar de AXP192) |
| **TTGO T-Beam V1.1** | ✅ Target | Placa objetivo de este firmware |
| **TTGO T-Beam V1.2** | ⚠️ Modificación requerida | Cambios en PMU (AXP2101). Requiere ajustar librería de gestión de energía |
| **TTGO T-Beam SX1276** | ❌ No compatible | Usa chip SX1276 en lugar de SX1262. Requiere cambios en driver de radio |
| **TTGO LILYGO LoRa32 V2.1** | ❌ No compatible | Sin GPS integrado ni PMU AXP192. Requiere cambios sustanciales |
| **Heltec WiFi LoRa 32 V3** | ❌ No compatible | Distinto pinout, sin GPS integrado, usa SX1262 pero sin RF switch externo |
| **Adafruit Feather M0 + LoRa** | ❌ No compatible | MCU diferente (SAMD21), distinto chip LoRa (RFM95/SX1276) |

### Adaptación para T-Beam V1.2 (AXP2101)

Si se desea portar a T-Beam V1.2, modificar la inicialización de PMU:

```cpp
// En lugar de XPowersAXP192:
XPowersAXP2101 PMU;

// En axp192_init():
bool axp192_init() {
    if (!PMU.init(Wire, PIN_I2C_SDA, PIN_I2C_SCL, AXP2101_SLAVE_ADDRESS)) {
        Serial.println(F("FAIL"));
        return false;
    }
    PMU.setALDO1Voltage(3300);  // LDO para SX1262 (nombre cambiado en AXP2101)
    PMU.enableALDO1();
    PMU.setALDO2Voltage(3300);  // LDO para GPS
    PMU.enableALDO2();
    Serial.println(F("OK"));
    return true;
}
```

### Adaptación para región EU868

Para usar en Europa (868 MHz), modificar las siguientes líneas:

```cpp
// Cambiar región en la creación del nodo:
LoRaWANNode node(&radio, &EU868, 1);  // EU868, subbanda 1

// Cambiar canales de join:
// Los canales estándar EU868 (867.1, 867.3, 867.5, 867.7, 867.9 MHz)
// son automáticos con la región EU868

// Cambiar potencia máxima a +14 dBm (límite EU):
// Añadir después de radio.begin():
radio.setOutputPower(14);
```

### Adaptación para región AU915

Para Australia (915-928 MHz, similar a US915 pero con subbandas diferentes):

```cpp
LoRaWANNode node(&radio, &AU915, 2);  // AU915 subbanda 2
```

---

## Especificaciones de Antena

### Antena LoRa (915 MHz)

| Parámetro | Valor |
|-----------|-------|
| **Rango de frecuencia** | 902-928 MHz |
| **Impedancia** | 50 ohm |
| **Conector** | SMA macho (rosca exterior, pin central) |
| **VSWR** | ≤ 2.0:1 |
| **Ganancia típica** | 2-5 dBi |
| **Polarización** | Vertical |
| **Tipo** | Monopolo de cuarto de onda, helicoidal o dipolo |

**Antenas recomendadas**:
- Antena de látigo SMA 915 MHz 3 dBi (incluida típicamente con el T-Beam)
- Antena dipolo 915 MHz 5 dBi (mayor alcance)
- Antena magnética 915 MHz 3 dBi (para montaje en vehículo)

### Antena GPS

| Parámetro | Valor |
|-----------|-------|
| **Tipo** | Parche cerámico activo |
| **Dimensiones** | 25 × 25 × 4 mm |
| **Frecuencia** | 1575.42 MHz (L1) |
| **Ganancia LNA** | 20-28 dB |
| **Conector** | u.FL / IPEX |
| **Polarización** | RHCP (circular derecha) |

---

## Certificaciones y Cumplimiento Normativo

### FCC (US915)

El SX1262 operando a 20 dBm con antena de 3 dBi cumple con los límites FCC Part 15.247 para dispositivos de espectro ensanchado en la banda 902-928 MHz:

- Potencia máxima de salida: 30 dBm (1W)
- Límite de PIRE (EIRP): 36 dBm (4W) con antena de hasta 6 dBi

### Seguridad Eléctrica

- La placa opera a 5V máximo (USB) o 4.2V (batería LiPo)
- Todos los pines GPIO operan a 3.3V (no tolerantes a 5V)
- La PMU AXP192 incluye protección de sobrecarga, sobrecorriente y cortocircuito para la batería

---

## Historial de Versiones

| Versión | Fecha | Cambios |
|---------|-------|---------|
| v1.0 | 2026-08 | Versión inicial. Soporte para T-Beam V1.1 con SX1262, DIO2 RF switch, OTAA US915, payload GPS 10 bytes. |
