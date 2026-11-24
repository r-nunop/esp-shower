# Política de gestión de consumo

## Objetivo

La ducha pasa casi todo el tiempo en REPOSO, esperando una orden. En ese estado no hay nada encendido salvo la electrónica, así que es donde tiene sentido ahorrar. Durante una ducha el consumo lo marca el calentador (120 W), y lo que gaste el ESP32 es despreciable a su lado.

La política tiene una condición: **la placa tiene que poder responder en todo momento** a un `iniciar` o a un `startOTA` que llegue por RPC desde ThingsBoard, y a la pantalla táctil.

## Medidas

| Medida | REPOSO y ERROR | Fases activas |
|---|---|---|
| Wi-Fi en modem sleep (`WIFI_PS_MAX_MODEM`) | Sí | Sí |
| Telemetría | Cada 60 s, y al momento en cada cambio de estado | Cada `cadencia_ms` (se cambia con `setCadencia`) |
| Periodo de la máquina de estados | 1 s (ERROR: 100 ms), con despertar inmediato al llegar un comando | 100 ms |
| Frecuencia dinámica de la CPU (40–160 MHz) | Sí | Sí |
| Light sleep automático | Solo en REPOSO | No |
| Sensor de turbidez alimentado | No | Solo en FILTRADO |

- **Modem sleep:** la radio duerme entre las balizas del router y se mantiene la conexión con ThingsBoard.
- **Telemetría más lenta en reposo:** cada envío enciende la radio. En REPOSO los datos casi no cambian, así que basta con uno por minuto. Los cambios de estado se envían al momento para que el panel no vaya con retraso.
- **Máquina de estados a 1 s en reposo:** no hay nada que controlar. La espera se hace sobre la cola de comandos, así que una orden se atiende al momento y no al cabo de un segundo.
- **Frecuencia dinámica y light sleep:** con la gestión de energía de ESP-IDF (`CONFIG_PM_ENABLE`), la CPU baja a 40 MHz cuando no tiene trabajo. En REPOSO, además, el chip entra solo en light sleep entre tareas. El light sleep se desactiva en las fases activas para no interferir con el control del calentador ni con los relés. Se puede quitar con `CONFIG_SHOWER_LIGHT_SLEEP` en menuconfig.
- **OTA:** durante la descarga se bloquea el light sleep (`ESP_PM_NO_LIGHT_SLEEP`) para que no vaya más lenta.
- **Sensor de turbidez:** su MOSFET solo se enciende durante FILTRADO, que es cuando se mide.

## Por qué no deep sleep ni "Wi-Fi solo al transmitir"

1. **Hay que responder siempre.** Un RPC de ThingsBoard solo llega si la sesión MQTT está abierta. En deep sleep, o con la Wi-Fi apagada, cada despertar cuesta varios segundos (Wi-Fi, TLS y MQTT), y los RPC enviados mientras dormía se pierden.
2. **Seguridad.** En deep sleep los GPIO se reinician si no se fijan uno a uno. La placa de extensión (IO12) se apagaría y habría que volver a configurar el MCP23017 en cada despertar.
3. **La pantalla táctil** también tiene que poder despertar el sistema.

El light sleep con modem sleep da la mayor parte del ahorro sin perder la conexión.

## Consumo estimado del ESP32

**Son estimaciones del datasheet del ESP32 y de la guía de bajo consumo de Espressif, no medidas.** No incluyen el resto de la placa (reguladores, LEDs, sensores, placa de extensión).

| Modo | Corriente del ESP32 |
|---|---|
| Wi-Fi transmitiendo | 160–260 mA (picos) |
| Modem sleep a 160 MHz | 27–44 mA |
| Modem sleep a 80 MHz | 20–31 mA |
| Light sleep (sin radio) | unos 0,8 mA |
| Light sleep automático con Wi-Fi conectada | pocos mA de media, según el DTIM del router |

Antes, en REPOSO el ESP32 estaba siempre en modem sleep a 160 MHz. Con esta política pasa la mayor parte del tiempo en light sleep, así que el consumo del ESP32 en reposo debería bajar en torno a un orden de magnitud.

## Medidas reales

Pendiente de medir con la placa (issue #5).

| Situación | Corriente de la placa |
|---|---|
| REPOSO, light sleep desactivado | — |
| REPOSO, light sleep activado | — |

## Nota para quien haga la pantalla

La pantalla va por UART (TX IO13, RX IO34). En light sleep, los bytes que llegan por RX mientras el chip duerme se pierden, y un toque en la pantalla en REPOSO no llegaría. Hay que hacer dos cosas:

1. **Bloquear el light sleep mientras se esperan datos:** crear un bloqueo con `esp_pm_lock_create(ESP_PM_NO_LIGHT_SLEEP, 0, "pantalla", &lock)` y tenerlo tomado mientras la pantalla pueda enviar algo (por ejemplo, siempre que esté encendida).
2. **Usar el reloj REF_TICK en la UART** (`.source_clk = UART_SCLK_REF_TICK` en `uart_config_t`). Con la frecuencia dinámica, el reloj APB cambia, y con el reloj por defecto los baudios se descuadrarían. REF_TICK no cambia con la frecuencia de la CPU. Como REF_TICK va a 1 MHz, conviene no pasar de 115200 baudios.
