# CnB RC Control – lokal MQTT-panel

Webbpanel efter `mqtt-monitoring-mockup.svg`, för **SCRUM-16-experimentet**.
Fem grafer visar de senaste tio sekunderna: vänster, center, höger,
begärd styrvinkel och begärd motor-duty. Panelen kan starta/stoppa bilen och
ändra experimentets befintliga MQTT-inställningar. Firmware ändras inte.

[Förhandsbild av graferna med simulerad data](preview.png).

## 1. Förbered datorn

Du behöver **Node.js 20 eller senare** och de befintliga Mosquitto-klienterna.
Inga npm-paket, CDN-resurser eller nya brokerinställningar behövs.
Startskriptet letar efter Node i PATH, standardinstallationen och den lokala
Codex-installationen. Node kan annars installeras från [nodejs.org](https://nodejs.org/).

Öppna PowerShell i experimentets projektrot:

```powershell
Set-Location 'C:\Users\Maher\Documents\Codex\2026-09-07\new-chat\outputs\elektronik_projektet-SCRUM16-mqtt-experiment'
```

## 2. Prova utseendet utan bilen

```powershell
.\tools\mqtt-ui\start-ui.ps1 -Demo
```

Öppna **[http://127.0.0.1:8765](http://127.0.0.1:8765)** i webbläsaren.
Behåll terminalen öppen. Den orange **DEMO ENVIRONMENT**-raden betyder att
alla värden är simulerade. Start, stopp och inställningar påverkar bara demon.
Demo läser inte `.env` och ansluter inte till någon MQTT-broker.

Avsluta servern med **Ctrl+C** innan du byter till verklig MQTT.

## 3. Anslut till bilen

1. Starta Mosquitto och bilen enligt [huvudguiden](../../documentation/guides/mqtt_steg_for_steg.md).
2. Kontrollera att experimentets **`tools/mqtt/.env`** innehåller rätt
   `CNB_MQTT_HOST`, `CNB_MQTT_PORT`, `CNB_MQTT_USERNAME` och `CNB_MQTT_PASSWORD`.
   Det är samma fil som `watch-telemetry.ps1` använder.
3. Avsluta `run-car.ps1` och dess heartbeat innan panelen tar över körningen.
   `watch-telemetry.ps1` och `log-telemetry.ps1` kan fortsätta läsa samtidigt.
4. Starta panelen utan `-Demo`:

```powershell
.\tools\mqtt-ui\start-ui.ps1
```

5. Öppna **[http://127.0.0.1:8765](http://127.0.0.1:8765)**.
   Märkningen ska vara **LIVE MQTT**, **BROKER CONNECTED** och **CAR RECEIVING**.
   Att öppna sidan skickar inget startkommando.
6. Välj önskat körläge, duty, stoppavstånd och telemetriintervall. Klicka
   **Apply settings**. Vänta på `Configuration #… acknowledged by car`.
7. Klicka **Start car**. Först när bilen bekräftat rätt start-ID och session
   aktiverar panelen heartbeat. **ARMED** betyder att körningen är aktiverad;
   `motion_state` visar om bilens logik begär rörelse eller bromsar.
8. Klicka **Stop car** för att avarmera. Vänta på `Stop acknowledged by car`.

Panelen fungerar med det befintliga experimentets firmware. Den här UI-ändringen
kräver i sig ingen ny build eller flashning. Wi-Fi och MQTT-uppgifter i
`firmware/sdkconfig` påverkas inte.

## 4. Vad reglagen gör

| Reglage | Värden | Betydelse |
| --- | --- | --- |
| Driver style | DecideAction, GradualSweep, SlowLeft, SlowRight | Väljer den befintliga körfunktionen i `logic.cpp`. Kräver stopp före byte. |
| Drive duty | 0.00–1.00 | Begärd duty när bilens körlogik tillåter rörelse, i alla körlägen. |
| Stop distance | 30–70 cm | Stoppgränsen som den valda körfunktionen använder. |
| Telemetry | 200, 500, 1000, 2000 eller 5000 ms | Publiceringsintervall; välj 200 ms för upp till fem punkter per sekund. Andra giltiga intervall från bilen visas också. |
| Apply settings | Hela formuläret | Publicerar alla fyra värden tillsammans, med en ny revision. |
| Start car | Ny session | Aktiverar körläget och panelens heartbeat efter bilens bekräftelse. |
| Stop car | Stopkommando | Avarmerar bilen och avslutar panelens heartbeat. |

**Duty 0 är inte samma sak som Stop car.** Med duty 0 kan bilen fortfarande
vara armerad och styra. Använd stoppknappen för att avarmera.

Panelen använder experimentets befintliga protokoll. Direkt manuell styrning
till en valfri vinkel, exempelvis en ratt/slider på +25°, stöds inte av denna
firmware. Grafen visar vinkeln som bilens valda körfunktion begär.

## 5. Läs graferna

- **Tio sekunder:** tidsaxeln flyttar sig kontinuerligt, även om nya paket uteblir.
  Punkten vid `now` är den senast mottagna tidspositionen, inte en prognos.
  Håll muspekaren över grafen för ett provs värde och ålder.
- **Avstånd:** cm från `distance_cm.left/center/right`. Samma färger som skissen.
  Y-axeln anpassas till synliga värden; stora värden klipps inte bort.
  Den streckade linjen är det inställda stoppavståndet.
- **ADC:** senaste `adc_raw` visas intill respektive avstånd. `—` betyder
  saknat/ogiltigt råvärde, inte noll.
- **Sensorstatus:** Sharp GP2Y0A21 har nominellt mätområde 10–80 cm.
  `OUT OF RANGE` markerar värden utanför det området. `BELOW LIMIT` jämför
  just den sensorn med stoppgränsen. Detta är inte ett besked om att bilen
  måste stanna; DecideAction kan välja en annan riktning.
- **Styrvinkel:** `steering_deg`, −90° vänster till +90° höger.
- **Speed command:** `motor.speed_command`, normalt 0–1. Detta är duty,
  inte km/h. PWM F/B visar `forward_duty` och `backward_duty`. Vid bromsning
  kan båda vara 1 samtidigt som speed command är 0.
- **Punkter och luckor:** punkterna är mottagna prover. Styrning/duty ritas
  som steg mellan prover. Saknade eller ogiltiga värden och längre avbrott
  blir luckor. Händelser mellan MQTT-prover kan inte återskapas.
- **För gammal data:** aktuella siffror blir `—`, historiken tonas ned och
  fortsätter glida ur fönstret. Färskhetsgränsen är det största av 2,5 sekunder
  och 2,5 gånger telemetriintervallet. Broker- och bilstatus visas separat.
- **Lagring:** bara de senaste tio sekunderna finns i serverns arbetsminne.
  En uppdaterad webbsida får den historiken. Brokeråteranslutning, detekterad
  bilomstart eller omstart av servern rensar den. Använd `log-telemetry.ps1`
  om du behöver spara ett helt körpass.

**Moving, styrvinkel och PWM är rapporterade kommandon/tillstånd.** Bilen har
ingen återkoppling här som bevisar fysisk hastighet eller hjulvinkel.

## 6. Heartbeat och avbrott

Endast den flik som startade sessionen förnyar panelens körbehörighet.
Panelen skickar MQTT-heartbeat ungefär en gång i sekunden. Stoppa innan
du lämnar fliken: byte till en annan flik, minimering, omladdning och stängning
begär stopp. Om stängningssignalen inte når servern upphör heartbeat när
flikens livstecken har saknats i cirka 1,6 sekunder. Firmwarens egna
heartbeat-timeout är tre sekunder efter senaste mottagna heartbeat.

Förlorad brokeranslutning, gamla telemetripaket eller saknad startbekräftelse
avslutar panelens session. Start återförsöks inte automatiskt efter avbrott.
Vid ett fel på stopp-publicering kan panelen inte bekräfta fysisk inbromsning;
firmwarens heartbeat-timeout kvarstår som reserv. Håll därför bilens fysiska
strömbrytare tillgänglig under körprov.

Flera flikar kan övervaka, men använd **en aktiv operatör**. Andra flikar
kan stoppa, men inte överta panelens heartbeat. Ett separat `run-car.ps1`
eller en klasskamrats MQTT-klient kan fortfarande styra via brokern;
panelens ägandeskap är lokalt för denna server.

## 7. Anslutning och integritet

```text
Webbläsare → lokal Node-server på 127.0.0.1:8765 → Mosquitto TCP → broker → ESP32
```

Webbläsaren behöver ingen MQTT-WebSocket-port. Brokerns befintliga port används
av Mosquitto-klienterna. Servern binder bara till `127.0.0.1`; du behöver ingen
ny brandväggsregel. Öppna den exakta adressen `127.0.0.1`, inte datorns LAN-adress.
Gränssnittet exponeras inte för klasskamrater via nätverket.

Lösenord läses endast av servern från den befintliga lokala `.env`-filen.
Servern serverar endast fyra uttryckligt valda UI-filer. MQTT-lösenord skickas
inte till webbläsaren, HTML, API-svar, bilder eller loggar. Liksom befintliga
PowerShell-verktyg lämnar bryggan credentials som argument till Mosquitto;
en lokal användare med rätt att läsa processargument kan därför se dem.
MQTT-transportens kryptering är samma som din befintliga brokerkonfiguration.

HTTP-kommandon kräver lokal origin och en token för aktuell serverstart.
Start, stopp och heartbeat publiceras utan retain. Konfiguration publiceras
med retain och QoS 1. Bilens bekräftelse kontrolleras separat från brokerns.
MQTT-format och klientflaggor finns i
[Mosquittos klientdokumentation](https://mosquitto.org/man/mosquitto_sub-1.html).

## 8. Felsökning

| Problem | Kontroll |
| --- | --- |
| PowerShell hittar inte skriptet | Gå till experimentets projektrot, inte `firmware`. |
| Node saknas | Installera Node.js 20+ och öppna en ny terminal. Inget `npm install` behövs. |
| Port already in use | Avsluta den tidigare panelservern, eller kör `start-ui.ps1 -Port 8766` och öppna `http://127.0.0.1:8766`. |
| DEMO trots att bilen är på | Avsluta servern med Ctrl+C och starta utan `-Demo`. Ladda om sidan. |
| BROKER OFFLINE | Starta Mosquitto. Kontrollera `.env`, brokeradress, port, användare och lösenord. |
| CAR OFFLINE/STALE | Kontrollera ESP32:s Wi-Fi/MQTT-logg och topics. En retained online-status räcker inte: färsk telemetri krävs. |
| Saknad startbekräftelse | Kontrollera att användaren får skriva `/command` och läsa `/command/state`. Panelen avbryter startförsöket och begär stopp. |
| Inställning avvisad | Läs feltexten och raden `Confirmed`. Stoppa före byte av körläge. Duty över 0,5 kräver experimentets firmware. |
| Config timeout | Bilen kan ha hunnit tillämpa värdena även om svaret försvann. Läs `Confirmed` när kontakten återkommer. |
| Få punkter i grafen | Välj Telemetry 200 ms och klicka Apply settings. Standard 1000 ms ger cirka en punkt per sekund. |
| Bilen stannar när fliken lämnas | Avsiktligt: kontrollfliken behöver vara synlig för heartbeat. Starta på nytt efter återkomst. |

## 9. Filer och verifiering

| Fil | Ansvar |
| --- | --- |
| `start-ui.ps1` | Startar Node från valfri PowerShell; `-Demo` och `-Port`. |
| `server.mjs` | Lokal HTTP-server, skyddade kontrollanrop och SSE till webbläsaren. |
| `transport.mjs` | Befintliga Mosquitto-klienter, `.env`, anslutningskontroll och separat simulator. |
| `core.mjs` | Tio sekunders historik, inställningsvalidering, bekräftelser och kontrollsession. |
| `public/index.html`, `public/style.css` | Panelens struktur och utseende. |
| `public/app.mjs`, `public/charts.mjs` | Reglage, status och rullande canvasgrafer. |
| `tests/console.test.mjs` | Historik, gränsvärden, ägandeskap, fel och HTTP-skydd. |
| `tests/mqtt.test.mjs` | Integration mot en separat tillfällig lokal Mosquitto-broker och simulerad bil. |

Kör från projektroten med Node i PATH:

```powershell
node --test tools/mqtt-ui/tests/console.test.mjs tools/mqtt-ui/tests/mqtt.test.mjs
```

Integrationstestet använder inte din `.env` eller din riktiga broker/bil.
Om Mosquitto inte finns i standardmappen markeras det testet som överhoppat.
Automatiserade kontroller och UI-prov med simulerad bil ersätter inte
ett körprov med den fysiska bilen.

Verifierat 2026-09-10: **14/14 automatiska tester godkända**, inklusive den
isolerade Mosquitto-integrationen med Mosquitto 2.1.2. PowerShell-skriptets
syntax kontrollerad. Webbläsarprov vid 1440 och 390 px bredd visade fem grafer
utan horisontellt överflöde eller JavaScript-fel. Start/stopp, GradualSweep,
inställningsbekräftelse, duty 0 under armering och återanslutning efter
serveromstart provades med demo. Ingen riktig bil startades av dessa tester.
