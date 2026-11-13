# Guía de Desarrollo — TTGO T-Beam LoRaWAN Milesight UG56

Documentación técnica para desarrolladores que deseen entender, modificar o extender el firmware del nodo LoRaWAN T-Beam.

---

## Tabla de Contenidos

- [Arquitectura del Sketch](#arquitectura-del-sketch)
- [Diagrama de Flujo](#diagrama-de-flujo)
- [Explicación del Código Fuente](#explicación-del-código-fuente)
- [API de Funciones](#api-de-funciones)
- [El RF Switch DIO2 — Análisis Técnico Profundo](#el-rf-switch-dio2--análisis-técnico-profundo)
- [Guía de Modificación y Extensión](#guía-de-modificación-y-extensión)
- [Depuración y Logging](#depuración-y-logging)
- [Pruebas Unitarias y Validación](#pruebas-unitarias-y-validación)
- [Glosario de Códigos de Error RadioLib](#glosario-de-códigos-de-error-radiolib)

---

## Arquitectura del Sketch

### Visión General

El sketch sigue una arquitectura **lineal en setup + cíclica no bloqueante en loop** típica de Arduino/ESP32. Está organizado en módulos funcionales autocontenidos, cada uno manejando un subsistema de hardware:

```
setup()
├── Inicialización Serial (115200 baud)
├── Inicialización I2C + AXP192 PMU
│   └── axp192_init() → XPowersLib
├── Inicialización GPS
│   └── gps_init() → Serial2 + TinyGPSPlus
├── Inicialización LoRa SX1262
│   └── lora_init() → RadioLib Module + SX1262 + setDio2AsRfSwitch()
├── Inicialización LoRaWAN
│   └── lorawan_init() → LoRaWANNode beginOTAA + activateOTAA

loop()
├── [100ms] Procesar datos GPS entrantes
│   └── gps_update() → TinyGPSPlus.encode()
├── [evento] Verificar timeout GPS (120s)
├── [evento] Detectar fix GPS y almacenar posición
├── [c/15s]  Imprimir estado de diagnóstico
├── [c/10s]  Reintentar join si no hay sesión
│   └── node.beginOTAA() + node.activateOTAA()
└── [c/60s]  Construir payload y transmitir
    └── build_payload() + node.sendReceive() + handle_tx_result()
```

### Gestión de Estado

El firmware mantiene 4 variables de estado globales que determinan el comportamiento del loop:

| Variable | Tipo | Valores | Significado |
|----------|------|---------|-------------|
| `joined` | `bool` | `true`/`false` | Si hay una sesión LoRaWAN activa |
| `gpsFixObtained` | `bool` | `true`/`false` | Si se ha obtenido al menos un fix GPS válido |
| `gpsTimedOut` | `bool` | `true`/`false` | Si el timeout de 120s para el primer fix se ha agotado |
| `lastLat`/`lastLng`/`lastSats` | `int32_t`/`int32_t`/`uint8_t` | posición GPS | Última posición válida (mantenida entre ciclos) |

### Modelo de Concurrencia

El ESP32 ejecuta FreeRTOS internamente. Este firmware usa el modelo de **single-thread cooperativo** de Arduino:

- **No hay tareas FreeRTOS explícitas**: todo corre en `loop()`.
- **No bloqueante**: todas las esperas usan `millis()` en lugar de `delay()` (excepto el `delay(100)` al final del loop para ceder tiempo a tareas del sistema).
- **Procesamiento de GPS**: los datos Serial2 se consumen en cada iteración del loop sin bloquear.
- **Transmisión LoRaWAN**: `node.sendReceive()` es bloqueante durante la transmisión y las ventanas RX (~1-3 segundos). Durante este tiempo, no se procesa GPS.

---

## Diagrama de Flujo

### Flujo Principal (loop)

```
                        ┌──────────────┐
                        │   loop()     │
                        └──────┬───────┘
                               │
                               ▼
                        ┌──────────────┐
                        │ gps_update() │  ← Leer bytes de Serial2, alimentar TinyGPSPlus
                        └──────┬───────┘
                               │
                               ▼
                   ┌───────────────────────┐
                   │ ¿Timeout 120s         │
                   │ sin fix?              │
                   └───────┬───────────────┘
                           │
                    ┌──────┴──────┐
                    │ SÍ          │ NO
                    ▼             ▼
            ┌───────────┐  ┌──────────────┐
            │ gpsTimedOut│  │ gps_check_fix│
            │ = true     │  │ ()           │
            └─────┬─────┘  └──────┬───────┘
                  │               │
                  │        ┌──────┴──────┐
                  │        │ ¿Fix válido?│
                  │        └──────┬──────┘
                  │         ┌─────┴─────┐
                  │         │ SÍ        │ NO
                  │         ▼           │
                  │  ┌─────────────┐    │
                  │  │ Almacenar    │    │
                  │  │ lastLat/Lng  │    │
                  │  │ /Sats        │    │
                  │  └──────┬──────┘    │
                  │         │           │
                  └────┬────┘           │
                       │                │
                       ▼                ▼
               ┌────────────────────────────┐
               │ ¿c/15s? Imprimir status    │
               └────────────┬───────────────┘
                            │
                            ▼
                    ┌───────────────┐
                    │ ¿joined?      │
                    └───┬───────┬───┘
                        │ NO    │ SÍ
                        ▼       │
                ┌──────────────┐│
                │ ¿c/10s?      ││
                │ Rejoin OTAA  ││
                └──────────────┘│
                                ▼
                        ┌───────────────┐
                        │ ¿c/60s?       │
                        │ Transmitir    │
                        └───┬───────┬───┘
                            │ NO    │ SÍ
                            │       ▼
                            │ ┌─────────────────┐
                            │ │ ¿joined Y        │
                            │ │ (fix o timeout)? │
                            │ └────┬───────┬────┘
                            │      │ NO    │ SÍ
                            │      ▼       ▼
                            │  ┌──────┐ ┌──────────────┐
                            │  │Omitir│ │build_payload │
                            │  │  TX  │ │()            │
                            │  └──────┘ └──────┬───────┘
                            │                  ▼
                            │          ┌───────────────┐
                            │          │sendReceive()  │
                            │          └──────┬────────┘
                            │                 ▼
                            │          ┌───────────────┐
                            │          │handle_tx_     │
                            │          │result()       │
                            │          └───────────────┘
                            │
                            ▼
                    ┌────────────────┐
                    │ delay(100ms)   │
                    └────────────────┘
                            │
                            └──────────▶ loop()
```

### Máquina de Estados del Join LoRaWAN

```
    ┌──────────┐
    │ JOIN_IDLE│◄──────────────────────────────┐
    │ joined=  │                               │
    │ false    │                               │
    └────┬─────┘                               │
         │                                     │
         │ node.beginOTAA() + activateOTAA()   │
         ▼                                     │
    ┌──────────────┐                           │
    │JOIN_IN_      │   error (-1116, etc.)     │
    │PROGRESS      │───────────────────────────┤
    │              │                           │
    └──────┬───────┘                           │
           │                                   │
           │ NEW_SESSION o SESSION_RESTORED    │
           ▼                                   │
    ┌──────────────┐   session lost            │
    │ JOIN_OK      │   (NOT_JOINED)            │
    │ joined=true  │───────────────────────────┘
    └──────────────┘
```

---

## Explicación del Código Fuente

### Sección 1: Includes y Dependencias

```cpp
#include <RadioLib.h>       # Driver SX1262 + pila LoRaWAN
#include <SPI.h>            # Comunicación SPI (SX1262)
#include <Wire.h>           # Comunicación I2C (AXP192)
#include <XPowersLib.h>     # Driver PMU AXP192
#include <TinyGPSPlus.h>    # Parser NMEA GPS
```

**RadioLib** es la única librería crítica. Proporciona:
- `Module`: abstracción de pines SPI
- `SX1262`: driver del transceptor (hereda de `PhysicalLayer`)
- `LoRaWANNode`: pila LoRaWAN completa (join, sesiones, subbandas, MAC commands, ADR, encriptación AES-128)

### Sección 2: Constantes de Configuración

#### Pines (líneas 11-26)

Los pines están definidos como macros `#define` para facilitar la portabilidad entre revisiones de hardware. No se usa `const` porque los pines se necesitan en tiempo de compilación para inicialización de objetos globales.

```cpp
#define PIN_LORA_NSS    18    // SPI: Chip Select del SX1262
#define PIN_LORA_SCK    5     // SPI: Clock (VSPI)
#define PIN_LORA_MOSI   27    // SPI: Master Out Slave In
#define PIN_LORA_MISO   19    // SPI: Master In Slave Out
#define PIN_LORA_RST    23    // Reset del SX1262 (activo bajo)
#define PIN_LORA_BUSY   32    // Indicador de busy del SX1262
#define PIN_LORA_DIO1   33    // IRQ para eventos LoRaWAN
```

**Nota sobre DIO1**: Este pin es fundamental para la pila LoRaWAN. El SX1262 genera interrupciones en DIO1 para:
- TX completada
- RX completada (ventanas RX1 y RX2)
- Timeout de RX
- Eventos de join

RadioLib configura automáticamente las interrupciones en DIO1 durante la inicialización.

#### Credenciales LoRaWAN (líneas 29-34)

```cpp
const uint64_t joinEUI = 0x70B3D57ECD8089C8;  // JoinEUI/AppEUI (64 bits)
const uint64_t devEUI  = 0x70B3D57E5F0040DC;  // DevEUI (64 bits)
const uint8_t  appKey[] = {0xCB, 0xD4, ...};  // AppKey (16 bytes)
const uint8_t  nwkKey[] = {0xCB, 0xD4, ...};  // NwkKey = AppKey (LoRaWAN 1.0.x)
```

**Importante**: Las credenciales se almacenan como arrays binarios, no como strings hexadecimales. RadioLib espera los EUIs como `uint64_t` y las keys como `uint8_t[16]`.

En LoRaWAN 1.0.x, NwkKey debe ser igual a AppKey. RadioLib 7.7.1 maneja esto automáticamente si se pasan ambos parámetros con el mismo valor. En LoRaWAN 1.1+, serían diferentes.

#### Timing (líneas 41-44)

```cpp
#define TX_INTERVAL_MS        60000     // 60 segundos entre transmisiones
#define LOOP_DELAY_MS         100       // Delay no bloqueante en loop
#define GPS_FIX_TIMEOUT_MS    120000    // 2 minutos para primer fix GPS
#define JOIN_RETRY_DELAY_MS   10000     // 10 segundos entre reintentos de join
```

Estos valores están optimizados para:
- **TX_INTERVAL**: Cumplir con la restricción de duty cycle US915 (no hay límite estricto en US915 como en EU868, pero 60s es conservador)
- **JOIN_RETRY_DELAY**: 10s evita saturar los canales de join, suficiente para que el gateway procese intentos previos

### Sección 3: Objetos Globales (líneas 54-59)

```cpp
SX1262 radio = new Module(PIN_LORA_NSS, PIN_LORA_DIO1, PIN_LORA_RST, PIN_LORA_BUSY);
LoRaWANNode node(&radio, &US915, LORAWAN_SUBBAND);
XPowersAXP192 PMU;
TinyGPSPlus gps;
HardwareSerial gpsSerial(2);     // Usa Serial2 (UART2 del ESP32)
```

**`Module`**: Constructor de RadioLib que agrupa los pines del SX1262:
```cpp
Module(int cs, int irq, int rst, int busy)
```
- `cs`: Chip Select (NSS)
- `irq`: Pin de interrupción (DIO1)
- `rst`: Reset del módulo
- `busy`: Indicador de busy

**`LoRaWANNode`**: Pila LoRaWAN que recibe:
- Puntero a `PhysicalLayer` (la radio, `SX1262` en este caso)
- Puntero a la banda de frecuencia (`&US915`, struct predefinido en RadioLib)
- Subbanda (2 = canales 8-15)

**`HardwareSerial(2)`**: Usa UART2 del ESP32. Serial0 está reservado para debug USB y Serial1 no se usa para evitar conflictos con pines reservados en la placa.

### Sección 4: Funciones Helper (líneas 69-94)

#### `printHex()` — Impresión de buffers binarios

```cpp
void printHex(const char* label, const uint8_t* buf, size_t len)
```

Itera sobre cada byte del buffer, añadiendo un cero a la izquierda si el valor es menor a `0x10`. No usa `String` (evita fragmentación de heap). Imprime directamente a `Serial`.

#### `lorawanErrorStr()` — Traducción de códigos de error

```cpp
const char* lorawanErrorStr(int16_t code)
```

Convierte códigos numéricos de RadioLib a strings legibles mediante un `switch`. Mapea tanto los códigos estándar (definidos en `RadioLib.h`) como los códigos de error LoRaWAN específicos:

| Código interno | Símbolo | Descripción |
|---------------|---------|-------------|
| `-1116` | `NO_JOIN_ACCEPT` | El gateway recibió el Join Request pero el NS rechazó el join (credenciales incorrectas, dispositivo no registrado) |
| `-1115` | `JOIN_NONCE_ERROR` | Error de nonce en el proceso de join (replay attack o conflicto de sesión previa) |

### Sección 5: Inicialización de Hardware

#### `axp192_init()` (líneas 97-109) — Inicialización de PMU

```cpp
bool axp192_init() {
    PMU.init(Wire, PIN_I2C_SDA, PIN_I2C_SCL, AXP192_SLAVE_ADDRESS);
    PMU.setLDO2Voltage(3300);   // 3.3V para alimentar SX1262
    PMU.enableLDO2();
    PMU.setLDO3Voltage(3300);   // 3.3V para alimentar GPS
    PMU.enableLDO3();
}
```

La secuencia es:
1. Inicializar comunicación I2C con el AXP192 en la dirección `0x34`
2. Configurar LDO2 a 3.3V y habilitarlo (alimenta el SX1262)
3. Configurar LDO3 a 3.3V y habilitarlo (alimenta el GPS NEO-M8N)

**Si LDO2 no se habilita**: el SX1262 no recibe alimentación → error `CHIP_NOT_FOUND` (-706).
**Si LDO3 no se habilita**: el GPS no funciona → no se reciben datos Serial2.

#### `lora_init()` (líneas 112-133) — Inicialización del SX1262

```cpp
bool lora_init() {
    SPI.begin(PIN_LORA_SCK, PIN_LORA_MISO, PIN_LORA_MOSI, PIN_LORA_NSS);
    int state = radio.begin();
    // ...
    state = radio.setDio2AsRfSwitch(true);
}
```

**Paso 1 — SPI.begin()**: Inicializa el bus SPI en los pines especificados. El ESP32 soporta múltiples buses SPI; este usa VSPI (SPI2 del ESP32), que es el bus por defecto para los pines 5/18/19/27.

**Paso 2 — radio.begin()**: RadioLib ejecuta la siguiente secuencia:
1. Configurar pines (CS, RST, BUSY)
2. Hacer reset físico del SX1262 (pulsar RST)
3. Leer registro de versión de silicon (`GetVersion`, opcode `0x1F`)
4. Verificar que la respuesta es válida
5. Configurar el SX1262 en modo STDBY_RC
6. Calibrar oscilador RC interno
7. Configurar frecuencia base, potencia, etc.

**Paso 3 — radio.setDio2AsRfSwitch(true)**: Ver [análisis técnico del DIO2](#el-rf-switch-dio2--análisis-técnico-profundo).

Si `setDio2AsRfSwitch()` retorna WARN (no RADIOLIB_ERR_NONE), el código continúa. Esta decisión de diseño evita que un fallo en este comando (que es no-esencial para la comunicación SPI) bloquee completamente el arranque.

#### `gps_init()` (líneas 136-139) — Inicialización del GPS

```cpp
void gps_init() {
    gpsSerial.begin(GPS_BAUD, SERIAL_8N1, PIN_GPS_RX, PIN_GPS_TX);
}
```

Usa `HardwareSerial::begin()` con parámetros completos (baudrate, configuración, pines RX y TX). El NEO-M8N viene configurado de fábrica a 9600 baud, 8N1. Los pines se asignan explícitamente en lugar de usar los defaults de Serial2.

#### `lorawan_init()` (líneas 142-163) — Join OTAA

```cpp
bool lorawan_init() {
    int state = node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
    state = node.activateOTAA();
    if (state == RADIOLIB_LORAWAN_NEW_SESSION) {
        joined = true;
    }
}
```

**`beginOTAA(joinEUI, devEUI, nwkKey, appKey)`**: Configura los parámetros de sesión OTAA (derivación de claves de sesión NwkSKey/AppSKey según LoRaWAN 1.0.4, inicialización de contadores de frame, configuración de canales).

**`activateOTAA()`**: Ejecuta el procedimiento completo de join:
1. Construir el mensaje Join Request (DevEUI + JoinEUI + DevNonce)
2. Calcular MIC (Message Integrity Code) con AppKey
3. Transmitir en cada uno de los 8 canales de join (902.3-903.7 MHz), ciclando entre ellos
4. Esperar Join Accept en RX1 o RX2
5. Verificar MIC del Join Accept
6. Derivar claves de sesión (NwkSKey, AppSKey) y almacenar DevAddr
7. Retornar `RADIOLIB_LORAWAN_NEW_SESSION` si exitoso

### Sección 6: Operación Principal (loop)

#### `gps_update()` (líneas 166-170) — Procesamiento GPS

```cpp
void gps_update() {
    while (gpsSerial.available()) {
        gps.encode(gpsSerial.read());
    }
}
```

**No bloqueante**: Consume todos los bytes disponibles en el buffer Serial2 de una vez. El `while` vacía el buffer en cada iteración del loop (~cada 100 ms). A 9600 baud, el GPS envía ~80-100 caracteres por segundo (sentencias NMEA cada 1 segundo), así que el buffer nunca debería saturarse.

#### `gps_check_fix()` (líneas 173-178) — Validación de Fix GPS

```cpp
bool gps_check_fix() {
    return gps.location.isValid() &&
           gps.satellites.isValid() &&
           gps.satellites.value() >= MIN_SATS &&
           gps.location.age() < 15000;
}
```

Cuatro criterios de validación:
1. **location.isValid()**: TinyGPSPlus ha parseado correctamente latitud y longitud
2. **satellites.isValid()**: El número de satélites está presente en la sentencia NMEA
3. **sats >= MIN_SATS (4)**: Mínimo para un fix 3D confiable (3 para 2D, 4+ para 3D)
4. **age < 15000** (15 segundos): La última posición no es más antigua que 15 segundos

#### `build_payload()` (líneas 181-188) — Construcción de Payload

```cpp
void build_payload(uint8_t* payload, bool hasFix) {
    payload[0] = hasFix ? PAYLOAD_TYPE_GPS : 0x00;
    int32_t lat = hasFix ? (int32_t)(gps.location.lat() * 1e7) : lastLat;
    int32_t lng = hasFix ? (int32_t)(gps.location.lng() * 1e7) : lastLng;
    memcpy(payload + 1, &lat, 4);   // Copia binaria en formato nativo (LE)
    memcpy(payload + 5, &lng, 4);
    payload[9] = hasFix ? (uint8_t)gps.satellites.value() : lastSats;
}
```

**Conversión de coordenadas**: `double * 1e7 → int32_t`. Ejemplo: `4.6527800 * 10000000 = 46527800`. Esto preserva precisión de ~1 cm en el ecuador (suficiente para cualquier aplicación GPS).

**Endianness**: `memcpy` copia la representación binaria nativa del ESP32 (little-endian). El receptor debe interpretar los 4 bytes como `int32` LE.

**Fallback**: Si no hay fix (`hasFix = false`), se usa la última posición válida almacenada en `lastLat`/`lastLng`/`lastSats`, que persisten entre ciclos.

#### `handle_tx_result()` (líneas 191-204) — Resultado de Transmisión

```cpp
void handle_tx_result(int16_t state) {
    if (state > 0) {
        // Downlink recibido: state = número de bytes
    } else if (state == RADIOLIB_ERR_NONE) {
        // TX exitosa, sin downlink
    } else if (state == RADIOLIB_ERR_RX_TIMEOUT) {
        // TX exitosa, ventana RX expirada (normal en Clase A)
    } else if (state == RADIOLIB_ERR_NETWORK_NOT_JOINED) {
        joined = false;  // Forzar rejoin
    }
}
```

**Convención de RadioLib para `sendReceive()`**:
- **Retorno > 0**: Éxito + downlink → el valor es el número de bytes del downlink
- **Retorno = 0** (`RADIOLIB_ERR_NONE`): Éxito sin downlink
- **Retorno = `RADIOLIB_ERR_RX_TIMEOUT`**: TX exitosa pero sin downlink (comportamiento normal Clase A)
- **Retorno < 0**: Error

#### Loop de Reconexión (líneas 280-293)

```cpp
if (!joined && (millis() - lastJoinAttempt >= JOIN_RETRY_DELAY_MS)) {
    lastJoinAttempt = millis();
    node.beginOTAA(joinEUI, devEUI, nwkKey, appKey);
    int16_t j = node.activateOTAA();
    if (j == RADIOLIB_LORAWAN_NEW_SESSION || j == RADIOLIB_LORAWAN_SESSION_RESTORED) {
        joined = true;
    }
}
```

**Timer independiente de 10 segundos**: Usa su propia variable `lastJoinAttempt`, independiente de `lastTx` (60s). Esto permite reintentar el join 6 veces entre transmisiones de datos.

**`beginOTAA()` antes de cada `activateOTAA()`**: Es crucial reinicializar el contexto OTAA (DevNonce, buffers de sesión) antes de cada intento de join. Si no se hace, el SX1262 reutilizaría el mismo DevNonce, causando rechazo del join por parte del NS.

---

## API de Funciones

### Funciones de Inicialización

| Función | Retorno | Propósito |
|---------|---------|-----------|
| `bool axp192_init()` | `true` si OK | Inicializar PMU, habilitar LDO2 y LDO3 |
| `bool lora_init()` | `true` si OK | Inicializar SX1262 SPI y configurar DIO2 como RF switch |
| `void gps_init()` | void | Iniciar Serial2 para GPS NEO-M8N a 9600 baud |
| `bool lorawan_init()` | `true` si join OK | Configurar y ejecutar OTAA join |

### Funciones de Operación

| Función | Parámetros | Retorno | Propósito |
|---------|-----------|---------|-----------|
| `gps_update()` | — | void | Alimentar parser TinyGPSPlus con datos de Serial2 |
| `gps_check_fix()` | — | `bool` | Verificar si hay fix GPS válido (>=4 sats, age<15s) |
| `build_payload(buffer, hasFix)` | `uint8_t* payload`, `bool hasFix` | void | Construir payload de 10 bytes |
| `handle_tx_result(state)` | `int16_t state` | void | Procesar resultado de `sendReceive()` |

### Funciones de Diagnóstico

| Función | Parámetros | Retorno | Propósito |
|---------|-----------|---------|-----------|
| `printHex(label, buf, len)` | `const char*`, `const uint8_t*`, `size_t` | void | Imprimir buffer en hexadecimal |
| `lorawanErrorStr(code)` | `int16_t code` | `const char*` | Convertir código de error RadioLib a string legible |

---

## El RF Switch DIO2 — Análisis Técnico Profundo

### Contexto del Hardware

El TTGO T-Beam V1.1 utiliza un **RF switch PE4259** (o equivalente) de pSemi/Peregrine Semiconductor entre el pin de RF del SX1262 y el conector SMA de la antena.

```
                      PE4259 RF Switch
    ┌─────────┐                          ┌──────────┐
    │ SX1262  │ RF_OUT ────▶  RFC (pin común)     │
    │         │                          │          ├────▶ SMA Antenna
    │         │        ┌────▶ RF1 (TX)  │          │
    │         │        │                └──────────┘
    │  DIO2   │────────┘
    └─────────┘     Control
                    (VCTL)
```

El PE4259 es un switch SPDT (Single-Pole Double-Throw) controlado por un solo pin CMOS:
- **VCTL = HIGH (>1.4V)**: RFC conectado a RF1 (TX)
- **VCTL = LOW (<0.8V)** o flotante: RFC conectado a RF2 (RX)

### Problema Original

Sin `setDio2AsRfSwitch(true)`:
- DIO2 del SX1262 está en **estado indeterminado** (puede ser LOW, floating, o configurado para otra función)
- El RF switch PE4259 no conmuta a la posición correcta durante TX
- La señal de RF del SX1262 se disipa internamente sin llegar al SMA
- **El resultado**: el gateway Milesight UG56 no detecta absolutamente ningún paquete

### Solución Técnica

El SX1262 tiene un modo interno de control automático de RF switch mediante su pin DIO2. Este modo se activa enviando el comando SPI `SetDio2AsRfSwitchCtrl` (opcode `0x9D`) con parámetro `0x01` (enable).

Cuando este modo está activo, el SX1262 maneja DIO2 automáticamente:

| Estado del SX1262 | Nivel de DIO2 | RF Switch | Señal enrutada |
|-------------------|--------------|-----------|----------------|
| **TX activo** | HIGH | RFC→RF1 | RF del SX1262 → Antena SMA |
| **RX activo** (ventanas RX1/RX2) | HIGH | RFC→RF1 | Antena SMA → Receptor SX1262 |
| **Idle / Sleep / Standby** | LOW | RFC→RF2 | Desconectado (aislamiento ~30 dB) |

### Implementación en RadioLib

```cpp
int16_t SX1262::setDio2AsRfSwitch(bool enable) {
    uint8_t data = enable ? 0x01 : 0x00;
    return SPIwriteCommand(0x9D, &data, 1);
}
```

El método `SPIwriteCommand()` envía el opcode `0x9D` seguido del parámetro `0x01` por SPI:

```
SPI Transaction:
  CS LOW
  MOSI: 0x9D  (opcode SetDio2AsRfSwitchCtrl)
  MOSI: 0x01  (enable = true)
  CS HIGH
```

### Verificación

Para verificar que el comando se aplicó correctamente:
1. RadioLib retorna `RADIOLIB_ERR_NONE` (0)
2. Al hacer join, los Join Requests son visibles en el analizador de paquetes del gateway
3. Con un analizador de espectro en el SMA, se detecta la portadora LoRa durante la transmisión

### Implementación Alternativa (SPI Directo)

Si `setDio2AsRfSwitch()` no estuviera disponible, se podría implementar manualmente:

```cpp
// Método alternativo usando SPI directo
Module* mod = radio.getMod();
mod->SPIbeginTransaction();
digitalWrite(mod->getCs(), LOW);
SPI.transfer(0x9D);   // opcode SetDio2AsRfSwitchCtrl
SPI.transfer(0x01);   // enable
digitalWrite(mod->getCs(), HIGH);
mod->SPIendTransaction();
```

### Impacto en el Consumo

El RF switch PE4259 consume ~10 µA en estado activo y ~1 µA en reposo. El control automático por DIO2 asegura que el switch solo se active durante TX/RX, minimizando el consumo total.

---

## Guía de Modificación y Extensión

### Cómo Agregar un Nuevo Sensor (ej. Temperatura/Humedad)

**Paso 1: Definir constantes y pines**

```cpp
// En la sección de pines:
#define PIN_DHT_DATA    13    // Pin de datos del sensor DHT22

// En la sección de payload:
#define PAYLOAD_TYPE_TEMP_HUMID  0x02
#define PAYLOAD_LEN_EXTENDED     15  // tipo(1) + lat(4) + lng(4) + sats(1) + temp(2) + hum(2) + checksum(1)
```

**Paso 2: Agregar librería y objeto**

```cpp
#include <DHT.h>
DHT dht(PIN_DHT_DATA, DHT22);
```

**Paso 3: Inicializar en setup()**

```cpp
void setup() {
    // ... código existente ...
    dht.begin();
    Serial.println(F("BOOT: DHT22 iniciado"));
}
```

**Paso 4: Modificar build_payload()**

```cpp
void build_payload(uint8_t* payload, bool hasFix) {
    // Campos GPS (calculados por separado para reutilizar)
    int32_t lat, lng;
    uint8_t sats;

    if (hasFix) {
        lat  = (int32_t)(gps.location.lat() * 1e7);
        lng  = (int32_t)(gps.location.lng() * 1e7);
        sats = (uint8_t)gps.satellites.value();
    } else {
        lat  = lastLat;
        lng  = lastLng;
        sats = lastSats;
    }

    // Leer sensor DHT22
    float temp = dht.readTemperature();   // °C
    float hum  = dht.readHumidity();      // %
    int16_t t  = (int16_t)(temp * 100);   // °C × 100
    uint16_t h = (uint16_t)(hum * 100);   // % × 100

    // Si el sensor falla, usar valores centinela
    if (isnan(temp)) t = 0x7FFF;  // 32767 = error
    if (isnan(hum))  h = 0xFFFF;  // 65535 = error

    // Construir payload
    payload[0]  = hasFix ? PAYLOAD_TYPE_TEMP_HUMID : 0x00;
    memcpy(payload + 1,  &lat,  4);
    memcpy(payload + 5,  &lng,  4);
    payload[9]  = sats;
    memcpy(payload + 10, &t,    2);  // Temp: int16 LE
    memcpy(payload + 12, &h,    2);  // Hum:  uint16 LE
    payload[14] = 0x00;  // Futuro checksum
}
```

**Paso 5: Actualizar la constante de tamaño**

```cpp
#define PAYLOAD_LEN           15   // actualizado a 15 bytes
```

### Cómo Cambiar el Intervalo de Transmisión

Modificar la constante `TX_INTERVAL_MS`:

```cpp
// Para intervalos más frecuentes:
#define TX_INTERVAL_MS        30000   // 30 segundos

// Para intervalos más espaciados:
#define TX_INTERVAL_MS        300000  // 5 minutos
```

**Precaución**: En US915 no hay restricción estricta de duty cycle como en EU868 (1%), pero se recomienda mantener intervals ≥ 30s para no saturar los canales y respetar las políticas de fair use.

### Cómo Cambiar la Región LoRaWAN

```cpp
// Cambiar a EU868:
LoRaWANNode node(&radio, &EU868, 1);
// Nota: también se debe cambiar la potencia máxima a +14 dBm:
// radio.setOutputPower(14);

// Cambiar a AU915:
LoRaWANNode node(&radio, &AU915, 2);

// Cambiar a AS923:
LoRaWANNode node(&radio, &AS923, 1);
```

### Cómo Agregar Confirmación de Mensajes (Confirmed Uplink)

Actualmente los uplinks son **unconfirmed** (no requieren ACK del servidor). Para usar mensajes confirmados:

```cpp
// En el loop, cambiar sendReceive() para usar confirmed:
int16_t state = node.sendReceive(payload, PAYLOAD_LEN, LORAWAN_PORT, true);
//                                                                    ^^^^
//                                                    confirmed = true

// En handle_tx_result(), los códigos de retorno para confirmed son:
// - state > 0: ACK recibido (bytes de downlink)
// - RADIOLIB_ERR_TX_TIMEOUT: No se recibió ACK → reintentar
```

**Precaución**: Los mensajes confirmados consumen más batería (el nodo debe mantener el receptor abierto hasta recibir el ACK) y reducen la capacidad de la red. Solo usar para datos críticos.

### Cómo Procesar Downlinks

```cpp
// En handle_tx_result():
if (state > 0) {
    // state = número de bytes del downlink
    uint8_t downlink[64];
    size_t len = node.getDownlink(downlink, sizeof(downlink));
    uint8_t port = node.getDownlinkPort();

    Serial.printf("[RX] Puerto %d, payload=", port);
    printHex("", downlink, len);

    // Procesar comando
    if (port == 10 && len >= 1) {
        switch (downlink[0]) {
            case 0x01:  // Cambiar intervalo TX
                if (len >= 3) {
                    uint16_t newInterval;
                    memcpy(&newInterval, downlink + 1, 2);
                    // TX_INTERVAL_MS = newInterval * 1000;
                    Serial.printf("[CMD] Nuevo intervalo: %d s\n", newInterval);
                }
                break;
            case 0x02:  // Solicitar posición inmediata
                // Forzar transmisión inmediata
                lastTx = 0;  // Hace que la próxima iteración transmita
                break;
        }
    }
}
```

---

## Depuración y Logging

### Niveles de Diagnóstico

El firmware actual tiene un solo nivel de diagnóstico (todos los mensajes se imprimen). Para implementar niveles:

```cpp
// Definir niveles
#define LOG_ERROR   0
#define LOG_WARN    1
#define LOG_INFO    2
#define LOG_DEBUG   3

#define LOG_LEVEL   LOG_INFO   // Cambiar según necesidad

// Macros de logging
#define LOG(level, fmt, ...) \
    do { if (level <= LOG_LEVEL) Serial.printf(fmt, ##__VA_ARGS__); } while(0)

// Uso:
LOG(LOG_INFO, "[GPS] Fix: lat=%.7f lng=%.7f\n", gps.location.lat(), gps.location.lng());
LOG(LOG_DEBUG, "[GPS] Raw NMEA: %s\n", nmeaBuffer);
LOG(LOG_ERROR, "[TX] FALLO CRITICO: %d\n", state);
```

### Volcado de Registros del SX1262

Para depuración avanzada de la radio, RadioLib permite habilitar trazas SPI:

```cpp
// Después de radio.begin():
Module* mod = radio.getMod();
mod->setRfSwitchPins(PIN_LORA_RXEN, PIN_LORA_TXEN);  // Si se usan pines separados
// RadioLib > 7.0 soporta callback de debug:
// radio.setPacketReceivedAction(debugCallback);
```

### Monitor de Recursos ESP32

```cpp
// En el loop, cada 60s:
Serial.printf("[SYS] Heap libre: %d bytes\n", ESP.getFreeHeap());
Serial.printf("[SYS] PSRAM libre: %d bytes\n", ESP.getFreePsram());
Serial.printf("[SYS] Uptime: %lu s\n", millis() / 1000);
```

---

## Pruebas Unitarias y Validación

### Casos de Prueba

| ID | Prueba | Procedimiento | Resultado Esperado |
|----|-------|---------------|-------------------|
| **T1** | Detección del chip SX1262 | Boot sin antena LoRa | Serial: `[SX1262] begin() ... OK` |
| **T2** | DIO2 RF switch | Boot normal | Serial: `[SX1262] DIO2 RF switch ... OK` |
| **T3** | Alimentación AXP192 | Medir LDO2 con multímetro | 3.3V ± 5% en pin de alimentación SX1262 |
| **T4** | GPS detectado | Boot con vista al cielo | Serial: `chars` incrementándose |
| **T5** | GPS fix | Esperar ≤ 5 min en exterior | Serial: `[GPS] FIX: lat=... lng=... sats=...` |
| **T6** | Join OTAA exitoso | Con gateway configurado | Serial: `JOINED !!!` + `DevAddr: 0x...` |
| **T7** | Uplink datos GPS | Esperar 60s tras join | Serial: `[TX] Puerto 1, payload=...` |
| **T8** | Payload correcto | Verificar en NS del gateway | Payload decodificado con lat/lng correctos |
| **T9** | Reconexión tras pérdida | Apagar gateway 2 min, encender | Serial: `Rejoin OTAA ... JOINED !!!` |
| **T10** | Timeout GPS | Boot indoor sin vista al cielo | Serial: `[GPS] Timeout 120s - usando placeholder` a los 120s |
| **T11** | Placeholder transmite | Tras timeout GPS sin fix | Payload: `00000000000000000000` |
| **T12** | Error join -1116 | Boot sin dispositivo registrado | Serial: `FAIL (-1116: NO_JOIN_ACCEPT)` + reintento cada 10s |

### Script de Validación Automatizada

```python
#!/usr/bin/env python3
"""Script de validación del nodo T-Beam LoRaWAN"""

import serial
import time
import re

SERIAL_PORT = "/dev/ttyUSB0"
BAUDRATE = 115200
TIMEOUT = 300  # 5 minutos máximo

def validate_firmware():
    ser = serial.Serial(SERIAL_PORT, BAUDRATE, timeout=1)
    start = time.time()
    checks = {
        "axp192_ok": False,
        "sx1262_ok": False,
        "dio2_ok": False,
        "gps_chars": False,
        "gps_fix": False,
        "join_ok": False,
        "tx_ok": False,
        "timeout_gps": False,
    }

    while time.time() - start < TIMEOUT:
        line = ser.readline().decode("utf-8", errors="replace").strip()
        if not line:
            continue

        print(f"[{time.time()-start:.0f}s] {line}")

        if "AXP192" in line and "OK" in line:
            checks["axp192_ok"] = True
        if "SX1262] begin" in line and "OK" in line:
            checks["sx1262_ok"] = True
        if "DIO2 RF switch" in line and "OK" in line:
            checks["dio2_ok"] = True
        if "chars=" in line and "fix=" in line:
            m = re.search(r"chars=(\d+)", line)
            if m and int(m.group(1)) > 0:
                checks["gps_chars"] = True
        if "FIX:" in line:
            checks["gps_fix"] = True
        if "JOINED" in line:
            checks["join_ok"] = True
        if "TX] Puerto" in line and "payload=" in line:
            checks["tx_ok"] = True
        if "Timeout 120s" in line:
            checks["timeout_gps"] = True

        # Si ya pasaron todas las pruebas críticas, terminar
        if checks["join_ok"] and checks["tx_ok"] and checks["gps_fix"]:
            break

    ser.close()
    return checks

if __name__ == "__main__":
    results = validate_firmware()
    print("\n=== RESULTADOS ===")
    for check, passed in results.items():
        status = "✅" if passed else "❌"
        print(f"  {status} {check}")

    all_hardware = all([results[k] for k in ["axp192_ok", "sx1262_ok", "dio2_ok", "gps_chars"]])
    all_lorawan = all([results[k] for k in ["join_ok", "tx_ok"]])
    print(f"\nHardware: {'OK' if all_hardware else 'FALLÓ'}")
    print(f"LoRaWAN:  {'OK' if all_lorawan else 'FALLÓ'}")
```

---

## Glosario de Códigos de Error RadioLib

Códigos de error relevantes para el SX1262 y la pila LoRaWAN:

| Código | Constante | Significado | Causa Típica |
|--------|-----------|-------------|--------------|
| `0` | `RADIOLIB_ERR_NONE` | Sin error | — |
| `> 0` | (bytes) | Downlink recibido | Número de bytes del downlink |
| `-2` | `RADIOLIB_ERR_CHIP_NOT_FOUND` | SX1262 no responde | Sin alimentación (LDO2), pines SPI incorrectos, chip dañado |
| `-8` | `RADIOLIB_ERR_SPI_CMD_FAILED` | Comando SPI falló | BUSY pin en estado incorrecto, interferencia SPI, frecuencia SPI demasiado alta |
| `-9` | `RADIOLIB_ERR_TX_TIMEOUT` | Timeout de transmisión | Antena no conectada, RF switch no activo, canal ocupado |
| `-10` | `RADIOLIB_ERR_RX_TIMEOUT` | Timeout de recepción | Normal en Clase A sin downlink pendiente |
| `-13` | `RADIOLIB_ERR_INVALID_FREQUENCY` | Frecuencia fuera de rango | Banda/región incorrecta |
| `-1106` | `RADIOLIB_ERR_NETWORK_NOT_JOINED` | No hay sesión LoRaWAN | Join no realizado o sesión expirada |
| `-1115` | `RADIOLIB_LORAWAN_JOIN_NONCE_ERROR` | Error de nonce en join | Conflicto de DevNonce (dispositivo ya hizo join con ese nonce) |
| `-1116` | `RADIOLIB_LORAWAN_NO_JOIN_ACCEPT` | Gateway/NS rechazó el join | Credenciales incorrectas, dispositivo no registrado, subbanda incorrecta |
| `-1119` | `RADIOLIB_LORAWAN_SESSION_RESTORED` | Sesión restaurada (info) | Join exitoso con sesión previa (no es error) |
| `-1121` | `RADIOLIB_LORAWAN_NEW_SESSION` | Nueva sesión creada (info) | Join exitoso (no es error, es código informativo) |

---

## Recursos y Referencias

- **Datasheet SX1262**: https://semtech.my.salesforce.com/sfc/p/#E0000000JelG/a/2R000000HXZa/Gk53fkpGWVoGtHMCG02hk3oPuV7VPORhqGkPQsGlypo
- **RadioLib Documentation**: https://jgromes.github.io/RadioLib/
- **RadioLib LoRaWAN Examples**: https://github.com/jgromes/RadioLib/tree/master/examples/LoRaWAN
- **TTGO T-Beam Schematic**: https://github.com/LilyGO/TTGO-T-Beam
- **XPowersLib Documentation**: https://github.com/lewisxhe/XPowersLib
- **TinyGPSPlus Documentation**: https://github.com/mikalhart/TinyGPSPlus
- **LoRaWAN Specification 1.0.4**: https://lora-alliance.org/resource_hub/lorawan-104-specification-package/
- **US915 Regional Parameters**: https://lora-alliance.org/resource_hub/rp2-1-0-4-lorawan-regional-parameters/
