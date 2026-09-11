# SCRUM-16-experimentet: bygg, flasha och använd Wi-Fi/MQTT

Den här guiden gäller endast **elektronik_projektet-SCRUM16-mqtt-experiment**.
SCRUM-50-projektets befintliga filer har inte ändrats.

**Ny webbpanel:** [CnB RC Control – steg för steg](../../tools/mqtt-ui/README.md)
visar tre sensorer, ADC, styrvinkel och motor-duty, med rullande tio sekunders
historik. Start/stopp, körläge, duty och stoppavstånd kan hanteras där.
Panelen ersätter `run-car.ps1` när den används för körning och håller då själv
heartbeat igång. `watch-telemetry.ps1` och loggning kan fortsätta samtidigt.

Efter att broker och bil är anslutna: kör från experimentets projektrot:

```powershell
.\tools\mqtt-ui\start-ui.ps1
```

Öppna [http://127.0.0.1:8765](http://127.0.0.1:8765) och behåll terminalen öppen.
Lägg till `-Demo` för att prova panelen utan bilen. UI-ändringen kräver ingen
ny flashning och ändrar inte Wi-Fi-/MQTT-uppgifterna.

## 1. Vad du kör

Experimentet utgår från den mergade SCRUM-16-koden, commit
`83154ef42da40fea1d1b292e45f7474ca9508a86` ([PR #15](https://github.com/Maher380/elektronik_projektet/pull/15)).
Wi-Fi, MQTT-protokoll och operatörsverktyg kommer från SCRUM-50, commit
`4f989cf267f5840389f63154ec4fb04c18a25fb2`.

**Körbesluten finns i `logic.cpp`.** Samma tre namngivna sensorinstanser,
`PlannedAction`, `switch`, fyra körfunktioner, sensorbromsning och
`executeAction()` används. De fasta stoppavstånden och hastigheterna i de fyra
körfunktionerna har ersatts med `myStopDistanceCm` och `myDriveDuty`.

Det finns ingen separat Planner som väljer väg. SCRUM-50:s `Control::evaluate`
och `applyOutput` används inte. MQTT-lagret kontrollerar tillståndet att köra
och överför konfiguration; det väljer inte riktning eller styrvinkel.

## 2. Inställningar och deras betydelse

| Inställning | Tillåtet | Startvärde | Betydelse |
| --- | --- | --- | --- |
| `StopDistanceCm` | 30–70 cm | 30 | Gränsen som körfunktionerna jämför avstånden mot. |
| `DriveDuty` | 0,0–1,0 | 0,5 | Begärd motor-duty när vägen är fri, i **alla fyra körlägen**. |
| `DriverStyle` | `DecideAction`, `GradualSweep`, `SlowLeft`, `SlowRight` | `DecideAction` | Väljer en av originalets körfunktioner. Byte kräver `DISARMED`. |
| `TelemetryIntervalMs` | 200–5000 ms | 1000 | Hur ofta MQTT-telemetri skickas. |

Använd decimalpunkt i PowerShell, till exempel `0.25`.

`DriveDuty 1.0` betyder 100 % PWM. Duty är ett styrkommando, inte uppmätt
hastighet, ström eller vridmoment. Originalets testlägen hade fast duty 0,2;
i experimentet använder de nu det inställda värdet. Ange `DriveDuty 0.2`
om du vill jämföra dessa lägen med SCRUM-16:s ursprungliga hastighet.

`DriveDuty 0.0` ger ingen drivning, men bilen kan fortfarande vara `ARMED`
och styra. Använd **stop-kommandot** för att göra bilen `DISARMED`.
MQTT-start aktiverar inte körning om SCRUM-16:s hinder- eller sensorvillkor bromsar.

## 3. Terminaler och projektmappar

| Terminal | Används till | Mapp |
| --- | --- | --- |
| A: ESP-IDF-terminal | Bygga, flasha, serial monitor, menuconfig | Experimentets `firmware` |
| B: PowerShell | Mosquitto-broker | Valfri mapp; kommandot nedan har absoluta sökvägar |
| C: PowerShell | Visa telemetri | Experimentets projektrot |
| D: PowerShell | Konfigurera, starta och hålla heartbeat igång | Experimentets projektrot |
| E: PowerShell | Stopp eller loggning medan andra terminaler används | Experimentets projektrot |

Projektroten är:

```text
C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM16-mqtt-experiment
```

`tools` ligger i projektroten, inte inne i `firmware`.

## 4. Wi-Fi och MQTT-uppgifter

De befintliga lokala filerna `firmware/sdkconfig`, `tools/mqtt/.env` och
revisionsräknaren har kopierats till experimentet. Wi-Fi och MQTT behöver
därför normalt inte konfigureras om. Kopiorna är Git-ignorerade, liksom i SCRUM-50.
Inga gamla binärer eller byggcachefiler har kopierats.

ESP32 och brokerdatorn måste kunna nå varandra. Vid iPhone-hotspot ansluter du
båda till samma hotspot. Kontrollera datorns adress om anslutningen har ändrats:

```powershell
ipconfig
```

Förväntat: under Wi-Fi-adaptern visas datorns aktuella IPv4-adress.

### Bilens inställningar

Öppna terminal A och gå till firmwaremappen:

```powershell
Set-Location 'C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM16-mqtt-experiment\firmware'
```

Förväntat: prompten slutar med `SCRUM16-mqtt-experiment\firmware`.

Vid behov, öppna konfigurationen:

```powershell
idf.py menuconfig
```

Förväntat: menyn öppnas. Under **Autonomous car network configuration** finns:

| Menyval | Vad det ska innehålla |
| --- | --- |
| Enable WiFi | Aktiverat |
| WiFi SSID / password | Nätverkets namn och lösenord |
| Enable MQTT telemetry and runtime control | Aktiverat |
| MQTT broker URI | `mqtt://DATORNS_IP:1883` |
| MQTT client ID | Bilens unika klient-ID |
| MQTT username / password | Bilens brokerkonto, normalt `cnb-vagrant` |

Main-taskens stack ska vara minst **8192 byte**. Inställningen finns under
**Component config → ESP System Settings → Main task stack size**.
`sdkconfig` innehåller de sparade inställningarna. Kör inte `set-target`
eller radera `sdkconfig` som rutin vid uppdatering av den här kopian.

### Datorns klientinställningar

Öppna vid behov klientkonfigurationen:

```powershell
notepad 'C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM16-mqtt-experiment\tools\mqtt\.env'
```

Förväntat: filen öppnas. `CNB_MQTT_HOST` ska vara samma brokeradress som bilen
använder, men utan `mqtt://`. Övriga nycklar är `CNB_MQTT_PORT`,
`CNB_MQTT_USERNAME` och `CNB_MQTT_PASSWORD`. Datorverktygen använder normalt
kontot `cnb-dashboard`. Spara ändringar lokalt.

## 5. Starta brokern

Använd er befintliga Mosquitto-installation, lösenordsfil och ACL. Du behöver
inte skapa om dem för experimentet. Om rätt broker redan körs, låt den fortsätta.

Annars startar du brokern i terminal B:

```powershell
& 'C:\Program Files\mosquitto\mosquitto.exe' -c 'C:\Users\Maher\cnb-mqtt\mosquitto.conf' -v
```

Förväntat: brokern lyssnar på port 1883. Låt terminalen vara öppen.
Vid fel om upptagen port: kontrollera vilken broker som redan körs.
En Mosquitto-tjänst som bara lyssnar på `127.0.0.1` kan inte ta emot bilen
över Wi-Fi. Windows-brandväggen behöver tillåta anslutningarna från det lokala nätet.

På en ny dator finns mallarna `tools/mqtt/mosquitto.conf.example` och
`tools/mqtt/mosquitto-acl.example`. De kräver lokal lösenordsfil och korrekta
sökvägar; de är inte en färdig ersättning för din befintliga brokerkonfiguration.

## 6. Bygg och flasha experimentet

Avsluta eventuell pågående `run-car.ps1` med Ctrl+C före flashning.
Kör i terminal A, i experimentets `firmware`:

```powershell
idf.py build
```

Förväntat: `build/cnb_scrum16_mqtt_experiment.bin` skapas. Första bygget kan
behöva hämta samma MQTT- och cJSON-beroenden som SCRUM-50.

```powershell
idf.py flash monitor
```

Förväntat: experimentet flashas och serial monitor öppnas. Vid start visas:

```text
SCRUM16 MQTT EXPERIMENT | baseline 83154ef | MQTT stop/duty/style | boot disarmed
```

Detta identifierar experimentet. Om ESP-IDF inte kan välja rätt seriell port,
använd dess `-p`-alternativ med bilens faktiska COM-port. Ingen `erase-flash`
behövs för ett vanligt byte mellan de här firmwareversionerna.

## 7. Prenumerera: visa och logga telemetri

Öppna terminal C och gå till experimentets projektrot:

```powershell
Set-Location 'C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM16-mqtt-experiment'
```

Förväntat: prompten visar experimentets projektrot.

```powershell
.\tools\mqtt\watch-telemetry.ps1
```

Förväntat: formaterade avstånd, ADC-värden, motor-duty och tillstånd visas.
`ADC raw` innehåller tal vid lyckade läsningar, annars `--`.

För råa MQTT-payloads använder du i stället:

```powershell
.\tools\mqtt\watch-telemetry.ps1 -Raw
```

Förväntat: topic och JSON visas utan formatering. Avsluta en watcher med Ctrl+C
innan du startar den andra i samma terminal.

För att spara telemetri, kör i terminal E från samma projektrot:

```powershell
.\tools\mqtt\log-telemetry.ps1
```

Förväntat: sökvägen till en NDJSON-fil under `tools/mqtt/logs` visas.
Varje rad innehåller mottagningstid och telemetri. Avsluta med Ctrl+C.

## 8. Publicera konfiguration och starta

Öppna terminal D i experimentets projektrot. Välj exempelvis:

```powershell
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0.30 -TelemetryIntervalMs 1000 -DriverStyle DecideAction
```

Förväntat: skriptet skriver en publicerad revision. Kontrollera sedan i terminal C
att bilens `config/state` visar **applied** och rätt värden. Publicering till
brokern är inte i sig bevis på att bilen har accepterat inställningarna.

Starta:

```powershell
.\tools\mqtt\run-car.ps1
```

Förväntat: bilen bekräftar start och blir `ARMED`. Skriptet skickar heartbeat
varje sekund. Låt terminalen fortsätta köra. Om sensorerna tillåter rörelse
använder körfunktionen duty 0,30.

Stoppa från terminal E:

```powershell
.\tools\mqtt\stop-car.ps1
```

Förväntat: `Stop sent.` och därefter `DISARMED / STOPPED` i bilens status.
Ctrl+C i `run-car.ps1` försöker också skicka stopp. Förlorad heartbeat i tre
sekunder eller förlorad MQTT-anslutning gör bilen disarmed; återanslutning
startar inte bilen automatiskt.

### Ändra duty

Använd samma körläge och skicka hela den önskade konfigurationen:

```powershell
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0.70 -TelemetryIntervalMs 1000
```

Förväntat: `applied` och duty 0,70. Utelämnat `DriverStyle` behåller aktivt
körläge i bilens RAM. Ändrad duty och stoppgräns kan appliceras medan bilen är armed.
Alla numeriska värden i kommandot uppdateras tillsammans.

Exempel med noll duty:

```powershell
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0.0 -TelemetryIntervalMs 1000
```

Förväntat: ingen drivning. När vägen är fri är tillståndet `ARMED / STOPPED`,
och originalets `Coast` ger PWM 0/0. Vid ett hinder eller sensorfel behåller
SCRUM-16 sin bromsning, vilket ger PWM 1/1. `SLEEP` är fortsatt hög så länge
bilen är armed. `stop-car.ps1` stänger även av drivaren via `SLEEP`.

### Ändra körläge

Stoppa bilen först och välj sedan exempelvis:

```powershell
.\tools\mqtt\set-config.ps1 -StopDistanceCm 35 -DriveDuty 0.20 -TelemetryIntervalMs 200 -DriverStyle GradualSweep
```

Förväntat: `applied` och `gradual_sweep`. Starta sedan med `run-car.ps1` igen.
Körlägesbyte medan bilen är armed avvisas med `driver_style_requires_disarmed`;
hela paketet avvisas då, även dess numeriska inställningar.

En alternativ genväg med standardvärden finns också:

```powershell
.\tools\mqtt\set-experiment-config.ps1 -DriverStyle SlowLeft -StopDistanceCm 35 -DriveDuty 0.20
```

Förväntat: samma konfigurationsschema publiceras, med telemetriintervall 1000 ms
om inget annat anges. Genvägens utelämnade värden är stopp 30 cm, duty 0,5,
`DecideAction` och 1000 ms; den är inte ett kommando för partiella uppdateringar.

## 9. Topics och JSON

| Topic | Riktning | QoS | Retained |
| --- | --- | --- | --- |
| `cnb/vagrant/telemetry` | Bil → prenumeranter | 0 | Nej |
| `cnb/vagrant/status` | Bil/broker → prenumeranter | 1 | Ja |
| `cnb/vagrant/config/set` | Operatör → bil | 1 | Ja |
| `cnb/vagrant/config/state` | Bil → operatör | 1 | Ja |
| `cnb/vagrant/command` | Operatör → bil | 1 | Nej |
| `cnb/vagrant/command/state` | Bil → operatör | 1 | Ja |

Ett konfigurationspaket har till exempel detta format. `revision` är bara
ett schemaexempel; skripten skapar själva ett nytt stigande värde:

```json
{"schema_version":1,"revision":1,"stop_distance_cm":35,"drive_duty":0.3,"telemetry_interval_ms":1000,"driver_style":"decide_action"}
```

Konfigurationen är retained i brokern och lagras i RAM på bilen. Ett gammalt
retained paket kan därför återkomma efter omstart. Skicka en ny komplett
konfiguration om du vill ändra det. Körläge ska vara med i retained paket
om det ska återställas även efter omstart.

Start, stopp och heartbeat använder sessions-ID. `run-car.ps1` skapar sessionen,
väntar på matchande startbekräftelse och skickar heartbeat. Publicera aldrig
start eller heartbeat som retained. Använd operatörsskripten för dessa kommandon.

## 10. Körlogik, koppling och kodplatser

| Körläge | Originalets styrning och hinderkontroll |
| --- | --- |
| `DecideAction` | Rakt om framåt är större än båda sidorna. Annars välj den friare sidan; lika sidor väljer höger. Kontrollera stoppgränsen på vald väg. |
| `GradualSweep` | Svep mellan −90 och +90 i steg om 5 grader. Kontrollera alla tre sensorer mot stoppgränsen. |
| `SlowLeft` | Styr −90 och kontrollera alla sensorer mot stoppgränsen. |
| `SlowRight` | Styr +90 och kontrollera alla sensorer mot stoppgränsen. |

Alla körlägen använder nu inställd `DriveDuty`. Mindre avstånd än gränsen
bromsar; exakt gränsavstånd tillåter körning enligt originalets `<`-jämförelse.
`GradualSweep` fortsätter ändra styrkommandot även under hinderbromsning.

| Funktion | Nano-märkning | GPIO |
| --- | --- | --- |
| IR vänster | A0 | 1 |
| IR framåt/center | A1 | 2 |
| IR höger | A3 | 4 |
| Motor IN1 | D2 | 5 |
| Motor IN2 | D3 | 6 |
| Motor SLEEP | D4 | 7 |
| Styrservo | D6 | 9 |

Servodrivaren är exakt den mergade SCRUM-16-versionen: −90 → 500 Hz,
0 → 300 Hz, +90 → 215 Hz, med 50 % duty. Kontrollera faktisk hjulriktning
på bilen. Värdena är kodens logiska vinklar, inte uppmätta hjulvinklar.

| Fil | Vad du hittar där |
| --- | --- |
| [logic.cpp](../../firmware/main/source/system/logic/logic.cpp) | Sensorläsning, `hasValidEnvironmentPicture()`, `decideAction()`, de fyra körfunktionerna, `executeAction()`, serialloggning och loopen. |
| [logic.h](../../firmware/main/include/system/logic/logic.h) | Namngivna sensorinstanser, pin-konstanter, `PlannedAction`, `myStopDistanceCm`, `myDriveDuty` och topics i `MqttTopics`. |
| [logic_mqtt.cpp](../../firmware/main/source/system/logic/logic_mqtt.cpp) | Överför MQTT-konfiguration till Logic, kontrollerar MQTT-tillstånd och publicerar telemetri inklusive ADC. |
| [control.cpp](../../firmware/main/source/system/runtime/control.cpp) | Validering av stoppgräns, duty 0–1, körläge och revisioner; start/stopp och sessioner. |
| [scrum16_mqtt_control.cpp](../../firmware/main/source/system/runtime/scrum16_mqtt_control.cpp) | MQTT-tillstånd, heartbeat och statusrapportering av Logics beslut. Inga vägval. |
| [manager.cpp](../../firmware/main/source/system/communication/manager.cpp) | Oförändrat SCRUM-50-protokoll: JSON, topics, återanslutning och telemetri. |
| [Kconfig.projbuild](../../firmware/main/Kconfig.projbuild) | Wi-Fi- och MQTT-menyn för `menuconfig`. |
| [set-config.ps1](../../tools/mqtt/set-config.ps1) | PowerShell-parametrar, inklusive accepterad duty 0–1. |
| [logic.diff](../../reference/logic.diff) | Exakt skillnad mellan SCRUM-16-originalet och experimentets `logic.cpp`. |

## 11. Tolka utskrift och felsök

| Utskrift/problem | Betydelse och kontroll |
| --- | --- |
| `DISARMED / STOPPED`, `boot` | Normalt efter omstart; startkommando krävs. |
| `sensor_fault` | Originalets sensorvalidering har misslyckats. SCRUM-16:s bromsning är kvar. |
| Växlar mellan `moving` och `sensor_fault` | Kan ge ryckig körning; jämför avstånd och ADC, kontrollera sensorer och matning. |
| `MOVING` men bilen står stilla | Tillståndet är ett körbeslut. Kontrollera även motor, matning, SLEEP och faktisk PWM. |
| `ADC raw --` | Ogiltigt/saknat råprov. Om alla tre alltid saknas, kontrollera startmarkören så rätt firmware körs. |
| Bilen följer en sidovägg | Originalet har ingen sidocentrering. `DecideAction` kan välja rakt fram trots ett nära sidohinder. |
| `drive_duty_out_of_range` | Experimentet accepterar ändliga tal 0–1. SCRUM-50 accepterar fortfarande högst 0,5. |
| Ingen startbekräftelse | Kontrollera brokeranslutning, online-status, credentials och command/state. |
| Fel om att skript saknas | Kör från experimentets projektrot, inte från `firmware`. |

Originalets serialrad visar den **planerade** åtgärden. Den kan visa `Speed: 0.50`
medan MQTT håller bilen disarmed. MQTT:s `motor.speed_command` tar hänsyn till
start/stopp-tillståndet. Telemetrins PWM och styrvinkel är fortfarande
drivertillstånd, inte fysisk återkoppling.

## 12. Återgå till SCRUM-50

Stoppa experimentet och avsluta dess `run-car.ps1`. Gå till den oförändrade
SCRUM-50-firmwaren i ESP-IDF-terminalen:

```powershell
Set-Location 'C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM-50\firmware'
```

Förväntat: du står i det ursprungliga projektets firmwaremapp.

```powershell
idf.py build flash monitor
```

Förväntat: SCRUM-50 kör igen. Publicera därefter en SCRUM-50-konfiguration med
duty **högst 0,5** innan du startar bilen. Båda versionerna använder samma
topics och broker, så experimentets retained konfiguration kan ligga kvar.

## 13. Verifiering

[Källjämförelsen](../../reference/source-verification.txt) kontrollerar att
11 originalmetoder är orörda och att de fyra körfunktionerna endast har fått
stoppavstånd och duty som parametrar. Motor-, PWM-, IR- och servodrivarna
matchar SCRUM-16. Originalprojektets filer och Git-status kontrolleras separat.

Host-testerna kör den verkliga `Logic::run()` med simulerad hårdvara, MQTT och
klocka. [ESP32-S3-kontrollen](../../reference/esp-syntax-results.txt) använder
projektets befintliga SDK-konfiguration och korskompilator.

ESP-IDF:s Python-start nekades åtkomst i agentmiljön. Fullständig build/länkning,
storlekskontroll, flashning och fysiska körprov måste därför göras i din
ESP-IDF-terminal. Syntaxkontroll och host-tester ersätter inte dessa steg.
