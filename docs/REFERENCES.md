# Referencias · TTGO LoRaWAN

Documentación externa que se usó para entender el hardware, los protocolos y las
libertades del proyecto. No es una bibliografía decorativa: cada enlace apunta a
la fuente que explica una decisión concreta.

## Fuentes técnicas

| Fuente | Para qué se consultó |
|---|---|
| [LilyGO LoRa Series](https://github.com/Xinyuan-LilyGO/LilyGo-LoRa-Series) | Placas T-Beam y ejemplos del fabricante. |
| [Semtech SX1262](https://www.semtech.com/products/wireless-rf/lora-connect/sx1262) | Transceptor LoRa y su integración. |
| [LoRaWAN 1.0.4](https://lora-alliance.org/resource_hub/lorawan-specification-v1-0-4/) | Especificación del protocolo LoRaWAN. |
| [Milesight UG56](https://www.milesight.com/iot/product/lorawan-gateway/ug56) | Gateway LoRaWAN usado como receptor. |

## Versiones de este repositorio

`TTGO_LoRaWAN_Milesight`

Cada carpeta puede tener dependencias y versiones distintas. Cuando el código
depende de una versión específica, esa versión está declarada en el README o en
el archivo de configuración de la carpeta, no en este documento.

## Criterio de preservación

- El código y los documentos se copiaron desde la carpeta original de trabajo.
- Se excluyeron `.git`, dependencias instaladas, compilados, registros y bases
  de datos locales.
- Se redactaron tokens, claves privadas y contraseñas detectadas en texto.
- Las plantillas `.example`, `.sample` y los valores de demostración se
  conservan porque documentan la configuración esperada.

## Limitaciones conocidas

El T-Beam puede parecer que transmite y no hacerlo: el switch RF PE4259 necesita la señal DIO2 configurada o el SX1262 queda sordo.

La documentación de cada versión describe el alcance real. Si una función no
está implementada, se dice; si un hardware no fue probado, también.
