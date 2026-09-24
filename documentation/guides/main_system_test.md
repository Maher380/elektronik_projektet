# Systemtest i main.cpp

Denna gren kör den lokalt provade testloopen direkt i `firmware/main/source/main.cpp`.
Drivrutiner hämtas via factory. `Control::authorizeAction()` kontrollerar MQTT-start,
anslutning och heartbeat; vägval och motor-/servokommandon bestäms i `main.cpp`.

Bygg, flasha och starta MQTT enligt [MQTT-guiden](mqtt_steg_for_steg.md).
Checka ut hela grenen: testet kräver även runtime- och kommunikationsfilerna.

## Koppling och beteende

| Funktion | Anslutning |
| --- | --- |
| IR vänster / fram / höger | A0 / A1 / A3 (GPIO 1 / 2 / 4) |
| MP6550 IN1 / IN2 / nSLEEP | D2 / D3 / D4 (GPIO 5 / 6 / 7) |
| Servo | D6 (GPIO 9), center 330 Hz |

- Standardvärden: stopp 30 cm, reaktion 40 cm, duty 0,5, sensorloop 250 ms.
- Avstånd över reaktionsgränsen samt NaN/inf behandlas som reaktionsgränsen.
  Negativa avstånd begränsas till noll. Ursprungliga mätvärden och ADC loggas separat.
- Om alla tre beslutsavstånd är högst stoppgränsen bromsar bilen.
- En ensam längsta vänstersida ger −90°, en ensam längsta högersida +90°.
  Framåt som längst eller delad förstaplats ger rakt fram.
- Start krävs efter uppstart. Stopp, förlorad MQTT och utebliven heartbeat stoppar
  motorn. Ett hinder som försvinner tillåter fortsatt körning medan bilen är armed.
- Manuellt servotest stoppar motorn. Ett nytt Start återgår till automatisk körning.

## Extra MQTT-inställningar

Publicera följande JSON till `cnb/vagrant/config/set` (QoS 1, retained).
Använd en högre `revision` för varje ändring under samma uppstart:

```json
{"schema_version":1,"revision":1,"stop_distance_cm":30,"reaction_distance_cm":40,"loop_interval_ms":250,"drive_duty":0.5,"telemetry_interval_ms":1000,"driver_style":"decide_action"}
```

Stoppavstånd: 1–100 cm. Reaktionsavstånd: 1–200 cm och större än stoppavståndet.
Loopintervall: 20–1000 ms. Duty: 0–1. Telemetri: 200–5000 ms.
Utelämnade `reaction_distance_cm` och `loop_interval_ms` behåller aktuella värden.
Testloopen stöder endast `decide_action`; andra körlägen avvisas.

Servotest via `cnb/vagrant/command` (QoS 1, **inte retained**):

```json
{"schema_version":1,"request_id":1,"session_id":"manual-test","command":"servo","angle_deg":-30}
```

Vinkeln får vara −90 till +90°. `request_id` måste vara högre än tidigare start-
och servokommandon under samma uppstart. Ett vanligt stoppkommando avslutar testet.

Den befintliga webbpanelen kan fortfarande starta/stoppa, skicka heartbeat och
visa vanlig telemetri. Nya reglage för reaktionsavstånd, loopintervall och manuellt
servotest ingår inte i denna PR; dessa funktioner nås via MQTT enligt ovan.

## Automatiskt test på datorn

Med Python, Zig och en lokal kopia av cJSON:s `cJSON.c` och `cJSON.h`, kör från
projektroten (ersätt sökvägarna):

```text
python tests/run_system_test.py --zig <sökväg-till-zig> --cjson-dir <cJSON-mapp>
```

Testet kör den riktiga loopen från `main.cpp` med simulerade sensorer, MQTT och PWM.
Det kontrollerar vägval, broms/återstart, servotest, inställningar, telemetri och
heartbeat-timeout. Det ersätter inte provning på bilen.
