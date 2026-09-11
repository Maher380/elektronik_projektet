# SCRUM-16 med Wi-Fi/MQTT – separat experiment

Fristående ESP-IDF-projekt för Arduino Nano ESP32 / ESP32-S3.
Det befintliga SCRUM-50-projektet har inte ändrats.

**[Öppna steg-för-steg-guiden](documentation/guides/mqtt_steg_for_steg.md)**
för byggning, flashning, Wi-Fi, broker, MQTT-start/stopp, körläge, duty och loggning.

## MQTT-panel i webbläsaren

**[Guide till CnB RC Control](tools/mqtt-ui/README.md)** – tre sensorgrafer,
styrvinkel och motor-duty med rullande tio sekunders historik, ADC-värden och
reglage för start/stopp, körläge, duty, stoppavstånd och telemetriintervall.

Kör från denna projektrot:

```powershell
.\tools\mqtt-ui\start-ui.ps1
```

Öppna [http://127.0.0.1:8765](http://127.0.0.1:8765). Panelen använder befintliga
`tools/mqtt/.env`. Lägg till `-Demo` för en helt simulerad förhandsvisning.
Node.js 20+ krävs; inga npm-paket eller nya brokerinställningar behövs.
Firmware behöver inte byggas om för denna UI-ändring.

## Körkoden

[logic.cpp](firmware/main/source/system/logic/logic.cpp) bygger direkt på den
mergade SCRUM-16-versionen `83154ef`. Samma struktur, separata sensorinstanser,
`PlannedAction`, körfunktioner och `executeAction()` finns kvar. De fasta
stopp- och hastighetsvärdena har ersatts med `myStopDistanceCm` och `myDriveDuty`.
Ingen annan klass väljer väg eller styrvinkel.

Wi-Fi/MQTT kommer från SCRUM-50 (`4f989cf`).
MQTT kan starta/stoppa, hålla heartbeat, ändra körläge och telemetriintervall,
sätta stoppavstånd 30–70 cm samt duty **0,0–1,0 i alla fyra körlägen**.
Boot är disarmed och ett faktiskt körlägesbyte kräver disarmed.
Alla körlägen använder det inställda duty-värdet; välj 0,2 manuellt för
SCRUM-16:s ursprungliga testhastighet i SlowLeft/SlowRight/GradualSweep.

Originalets sensorbromsning, vägval, servo- och motordrivrutiner behålls.
Experimentet är inte en korrigering av sensorernas mätvärden eller sidocentrering.

## Filer för granskning

- [Originalets logic.cpp](reference/SCRUM16/logic.cpp)
- [Originalets logic.h](reference/SCRUM16/logic.h)
- [Exakt diff för logic.cpp](reference/logic.diff)
- [Källverifiering](reference/source-verification.txt)
- [ESP32-S3-syntaxkontroll](reference/esp-syntax-results.txt)
- [Host-tester](tests/experiment_test.cpp)

De befintliga lokala Wi-Fi/MQTT-inställningarna har kopierats till experimentets
Git-ignorerade `firmware/sdkconfig` och `tools/mqtt/.env`. Inga gamla binärer
eller byggcachefiler har kopierats.

ESP-IDF:s Python-start nekades åtkomst i agentmiljön. Fullständig build,
länkning och fysisk verifiering återstår enligt guiden. Ingen flashning,
MQTT-start av bilen, commit eller push har utförts av agenten.
