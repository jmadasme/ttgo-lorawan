# BITÁCORA — TTGO T-Beam LoRaWAN → Milesight UG56

**Fecha:** 11 de agosto de 2026
**Placa:** TTGO T-Beam V1.1 (ESP32 + SX1262 + AXP192 + NEO-M8N)
**Gateway:** Milesight UG56 (NS embebido, AU915)
**Librería:** RadioLib 7.7.1, core ESP32 3.3.11

---

## Problema original

El TTGO T-Beam estaba programado con credenciales OTAA (US915, subbanda 2) pero el gateway Milesight UG56 **no veía ningún paquete** — ni siquiera Join Requests. El dispositivo aparecía como "Never activated".

---

## Diagnóstico y soluciones (en orden)

### 1. RF Switch DIO2 — CAUSA RAÍZ
**Problema:** El TTGO T-Beam V1.1 tiene un RF switch PE4259 entre el SX1262 y el conector SMA de la antena. Este switch se controla mediante el pin DIO2 del SX1262 (conexión interna en PCB, sin GPIO del ESP32). El código original NO configuraba DIO2 como control de RF switch.

**Solución:**
```cpp
radio.setDio2AsRfSwitch(true);  // DIO2 → RF switch control
```

**Resultado:** El gateway empezó a ver paquetes (Join Requests).

---

### 2. Región US915 → AU915
**Problema:** El código usaba `&US915` pero el UG56 opera en frecuencias **AU915** (915.2-927.8 MHz uplink), no US915 (902.3-914.9 MHz). Los Join Requests salían en una banda que el gateway no monitoreaba.

**Descubrimiento:** Un proyecto CubeCell funcional con el mismo gateway usaba `&AU915`. Al revisar los Incoming Packets del UG56, las frecuencias de los JnReq eran 917-918 MHz (AU915).

**Solución:**
```cpp
LoRaWANNode node(&radio, &AU915, LORAWAN_SUBBAND);
```

---

### 3. JoinEUI automático del UG56
**Problema:** El NS embebido del UG56 asigna un JoinEUI automáticamente (`24E124C0002ADD01`). El formulario de registro no muestra campo JoinEUI. El código usaba `70B3D57ECD8089C8` (generado manualmente) que no coincidía.

**Descubrimiento:** En los Incoming Packets, el campo `AppEUI` de los JnReq mostraba `24E124C0002ADD01`.

**Solución:**
```cpp
const uint64_t joinEUI = 0x24E124C0002ADD01;
```

---

### 4. TCXO y scanGuard
**Problema:** Tras resolver el RF switch, el join seguía fallando (-1116: NO_JOIN_ACCEPT). El SX1262 del T-Beam usa un TCXO de 1.8V que requiere configuración explícita para precisión de frecuencia. Sin TCXO, el PLL usa el oscilador RC interno (impreciso).

**Solución:**
```cpp
radio.setTCXO(1.8);        // Precisión de frecuencia
node.scanGuard = 200;      // Tiempo de guarda entre canales (como el CubeCell)
```

---

### 5. Payload size (stack smashing)
**Problema v1:** Payload de 14 bytes → error `-4` (PACKET_TOO_LONG). AU915 DR0-DR2 máximo 11 bytes.
**Problema v2:** Reduje a 12 bytes pero seguía overflow. Stack smashing crash.
**Problema v3:** Reduje a 11 bytes exactos. Sin errores.

**Formato final (11 bytes):**
```
[0-3]   Lat ×10⁷ int32 LE
[4-7]   Lng ×10⁷ int32 LE
[8]     [7:4]=sats, [3]=GPS fix, [2:1]=reserved, [0]=GPIO26
[9]     GPIOs: 2, x, 13, 14, 15, 16, 17, 25 (bit1 libre, GPIO4 es LED)
[10]    ADC GPIO36 8-bit (0-255 → 0-3.3V)
```

---

### 6. GPIO4 conflicto LED vs INPUT_PULLUP
**Problema:** GPIO4 se usaba como LED (OUTPUT) pero también estaba en el array de GPIOs de entrada. El bucle de init lo reconfiguraba como `INPUT_PULLUP`, matando el control del LED.

**Solución:** GPIO4 removido del array `gpioPins[]`. El bit 1 del byte de GPIOs queda reservado.

---

### 7. DevNonce y power cycles (NVS)
**Problema:** Cada power cycle (apagar/encender) hacía que RadioLib reiniciara el DevNonce a 0. El NS del UG56 rechazaba el join con `-1116` por detectar DevNonce repetido (anti-replay). Solución temporal: crear nuevo dispositivo cada vez.

**Solución definitiva:** Persistencia de sesión en NVS (flash del ESP32):
```cpp
// Guardar tras join exitoso
uint8_t* buf = node.getBufferNonces();
prefs.putBytes("session", buf, RADIOLIB_LORAWAN_NONCES_BUF_SIZE);

// Restaurar en el siguiente boot
node.setBufferNonces(savedBuffer);
```

**Resultado:** Power cycles normales ya no requieren nuevo dispositivo. Solo reflash de firmware limpia el NVS.

---

### 8. Decoder para UG56
**Problema:** El UG56 requiere formato específico. `decodeUplink(input)` con `input.bytes`. El formato `function Decode(fPort, bytes)` no funcionó. Tampoco `{data: {...}}`.

**Solución final (ChirpStack format):**
```js
function decodeUplink(input) {
  var b = input.bytes;
  // ... decodificar GPS, GPIOs, ADC
  return { data: { gpsFix, lat, lng, sats, ... } };
}
```

---

## Credenciales finales (v10)

| Parámetro | Valor |
|-----------|-------|
| DevEUI | `70B3D57E5F0040E5` |
| JoinEUI | `24E124C0002ADD01` (automático UG56) |
| AppKey | `CBD4C4992A115BC20118C53A4C1CC0D6` |
| Región | AU915, subbanda 2 |
| Puerto uplink | 1 |
| Intervalo TX | 60s |

---

## Comandos downlink (port 1)

| Hex | Acción |
|-----|--------|
| `01 01` | LED ON (GPIO4, LOW=ON) |
| `01 00` | LED OFF |
| `01 02` | LED TOGGLE |
| `01 03 N` | LED BLINK N veces |
| `02` | STATUS (triple flash 100ms) |

---

## Lecciones aprendidas

1. **DIO2 RF switch** — El error más silencioso. El radio responde por SPI pero no transmite. Siempre verificar en hardware.
2. **AU915 ≠ US915** — Aunque el UG56 diga "US915" en su UI, opera en AU915. Verificar frecuencias reales en Incoming Packets.
3. **JoinEUI del NS** — Los NS embebidos asignan su propio JoinEUI. No usar uno generado manualmente.
4. **Payload size matters** — AU915 DR0-DR2 = 11 bytes máximo. Probar con el mínimo viable.
5. **NVS para DevNonce** — Sin persistencia, cada power cycle = nuevo dispositivo. `getBufferNonces()` + `setBufferNonces()` lo resuelven.
6. **No pisar pines** — Si un GPIO es LED (OUTPUT), no incluirlo en el array de entradas.

---

## Versiones del firmware

| v | Cambio |
|---|--------|
| v1 | RF Switch DIO2 |
| v2 | Región US915→AU915 |
| v3 | JoinEUI `24E124...` |
| v4 | TCXO + scanGuard |
| v5-v8 | Payload 14→12→11 bytes, GPIO4 fix |
| v9 | Decoder UG56 funcional |
| v10 | **NVS persistencia (definitivo)** |
