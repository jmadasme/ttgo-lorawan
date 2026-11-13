/**
 * Payload Decoder — TTGO T-Beam → Milesight UG56 v3 (11 bytes)
 * ============================================================
 *   [0-3]   Lat ×10^7 int32 LE
 *   [4-7]   Lng ×10^7 int32 LE
 *   [8]     [7:4]=sats(0-15) [3]=GPS fix [2:1]=reserved [0]=GPIO26
 *   [9]     GPIOs: 2,4,13,14,15,16,17,25 (1=HIGH)
 *   [10]    ADC GPIO36 8-bit (0-255 → 0-3.3V)
 *
 * Pegar en: UG56 → Device → Payload Codec → Custom JS
 */
function decodeUplink(input) {
  var b = input.bytes;
  if (b.length < 11) return { data: { error: "short" } };

  // GPS
  var lat = b[0] | b[1]<<8 | b[2]<<16 | b[3]<<24;
  if (lat & 0x80000000) lat -= 0x100000000;
  var lng = b[4] | b[5]<<8 | b[6]<<16 | b[7]<<24;
  if (lng & 0x80000000) lng -= 0x100000000;

  // Byte 8: sats + fix + GPIO26
  var gpsFix = (b[8] & 0x08) ? true : false;

  // GPIOs
  var g = b[9];
  var gpio = {
    GPIO2:  (g & 0x01) ? 1 : 0,
    GPIO4:  (g & 0x02) ? 1 : 0,
    GPIO13: (g & 0x04) ? 1 : 0,
    GPIO14: (g & 0x08) ? 1 : 0,
    GPIO15: (g & 0x10) ? 1 : 0,
    GPIO16: (g & 0x20) ? 1 : 0,
    GPIO17: (g & 0x40) ? 1 : 0,
    GPIO25: (g & 0x80) ? 1 : 0
  };

  // ADC
  var adcRaw = b[10];

  return {
    data: {
      gpsFix: gpsFix,
      latitude: parseFloat((lat / 10000000.0).toFixed(7)),
      longitude: parseFloat((lng / 10000000.0).toFixed(7)),
      satellites: (b[8] >> 4) & 0x0F,
      gpio26: (b[8] & 0x01) ? 1 : 0,
      gpio: gpio,
      adcRaw: adcRaw,
      adcVoltage: parseFloat((adcRaw / 255.0 * 3.3).toFixed(2)),
      summary: (gpsFix ? "GPS:" + (lat/1e7).toFixed(5) : "GPS:noFix")
        + " | Sats:" + ((b[8]>>4)&0xF)
        + " | ADC:" + (adcRaw/255*3.3).toFixed(1) + "V"
    }
  };
}
