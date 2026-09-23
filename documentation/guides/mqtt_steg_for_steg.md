# MQTT och webbpanelen – från installation till körning

**Första gången:** följ steg 1–5.
**När allt är konfigurerat:** gå direkt till steg 5.

Byt exempelvägen `C:\projekt\elektronik_projektet` mot din projektrot, där `firmware` och `tools` finns.

Guiden gäller MQTT-firmwaren och webbpanelen tillsammans. Merga MQTT-PR:n först och webbpanelens PR därefter. Innan webbpanelen finns kan du använda terminalskripten längre ned.

## 1. Installera verktygen

Installera på Windows:

- **ESP-IDF 6.0** – för att bygga och flasha ESP32.
- **Mosquitto** – MQTT-broker och klientverktyg. Exemplen använder `C:\Program Files\mosquitto`.
- **Node.js 20 eller senare** – för webbpanelen.

Anslut datorn och bilen till samma WiFi, exempelvis din mobilhotspot.

Öppna PowerShell och gå till projektroten:

```powershell
cd 'C:\projekt\elektronik_projektet'
```

**Alla `tools`-kommandon i guiden körs från denna mapp.**

## 2. Konfigurera MQTT-brokern – en gång

Brokern körs på datorn och förmedlar meddelanden mellan bilen och webbpanelen.

Skapa mappar och kopiera behörighetsfilen:

```powershell
New-Item -ItemType Directory -Force "$env:USERPROFILE\cnb-mqtt\data"
Copy-Item .\tools\mqtt\mosquitto-acl.example "$env:USERPROFILE\cnb-mqtt\acl"
```

Skapa två konton. Kommandona frågar efter lösenord:

```powershell
& 'C:\Program Files\mosquitto\mosquitto_passwd.exe' -c "$env:USERPROFILE\cnb-mqtt\passwords" cnb-vagrant
& 'C:\Program Files\mosquitto\mosquitto_passwd.exe' "$env:USERPROFILE\cnb-mqtt\passwords" cnb-dashboard
```

- `cnb-vagrant` används av bilen.
- `cnb-dashboard` används av webbpanelen och terminalskripten.

**Använd `-c` bara när lösenordsfilen skapas första gången. Det skriver över en befintlig fil.**

Skapa brokerkonfigurationen:

```powershell
$brokerPath = "$env:USERPROFILE/cnb-mqtt".Replace('\', '/')
@"
listener 1883
allow_anonymous false
password_file $brokerPath/passwords
acl_file $brokerPath/acl
persistence true
persistence_location $brokerPath/data/
log_type error
log_type warning
log_type notice
"@ | Set-Content "$env:USERPROFILE\cnb-mqtt\mosquitto.conf" -Encoding ascii
```

Tillåt inkommande **TCP-port 1883** i Windows-brandväggen för det lokala nätverket där bilen ansluter.

## 3. Konfigurera webbpanelen – en gång

Öppna konfigurationsfilen:

```powershell
notepad .\tools\mqtt\.env
```

Skapa filen om den saknas. Skriv följande och ersätt lösenordet med det du valde för `cnb-dashboard`:

```dotenv
CNB_MQTT_HOST=127.0.0.1
CNB_MQTT_PORT=1883
CNB_MQTT_USERNAME=cnb-dashboard
CNB_MQTT_PASSWORD=DITT_DASHBOARD_LÖSENORD
```

Spara filen. `127.0.0.1` fungerar eftersom webbpanelen och brokern körs på samma dator.

## 4. Konfigurera bilen – en gång

Kör i PowerShell:

```powershell
ipconfig
```

Notera **IPv4-adressen för datorns WiFi-adapter**. Bilen behöver denna adress för att nå brokern.

Öppna sedan **ESP-IDF-terminalen**:

```powershell
cd 'C:\projekt\elektronik_projektet\firmware'
idf.py menuconfig
```

Under **Autonomous car network configuration**, fyll i:

| Inställning | Värde |
|---|---|
| Enable WiFi | Aktiverat |
| WiFi SSID / password | WiFi-namn och WiFi-lösenord |
| Enable MQTT telemetry and runtime control | Aktiverat |
| MQTT broker URI | `mqtt://DATORNS_IPV4:1883` |
| MQTT client ID | `cnb-vagrant` |
| MQTT username | `cnb-vagrant` |
| MQTT password | Lösenordet du skapade för bilen |

Använd **datorns WiFi-adress**, inte `127.0.0.1`, i bilen.

Kontrollera också:
**Component config → ESP System Settings → Main task stack size = 8192**.

Spara och avsluta. Inställningarna sparas i `firmware/sdkconfig` och behålls vid vanlig byggning och flashning.

## 5. Starta systemet – varje gång

Använd tre terminaler och låt dem vara öppna.

### Terminal 1: broker – PowerShell

```powershell
& 'C:\Program Files\mosquitto\mosquitto.exe' -c "$env:USERPROFILE\cnb-mqtt\mosquitto.conf" -v
```

Hoppa över detta om rätt broker redan körs.

### Terminal 2: bilen – ESP-IDF-terminal

Anslut ESP32 med USB:

```powershell
cd 'C:\projekt\elektronik_projektet\firmware'
idf.py build flash monitor
```

Om rätt firmware redan är flashad räcker `idf.py monitor`.

### Terminal 3: webbpanelen – PowerShell

```powershell
cd 'C:\projekt\elektronik_projektet'
.\tools\mqtt-ui\start-ui.ps1
```

Öppna **http://127.0.0.1:8765**.

När panelen visar **LIVE MQTT**, **BROKER CONNECTED** och **CAR RECEIVING** är systemet anslutet.

## Använd webbpanelen

| Du vill… | Gör så här |
|---|---|
| Välja körläge | Stoppa bilen, välj **Driver style**, tryck **Apply settings** |
| Ändra motorpådrag | Ändra **Drive duty**, 0–1 |
| Ändra stoppgräns | Ändra **Stop distance**, 30–70 cm |
| Ändra hur ofta telemetri skickas | Ändra **Telemetry**, i ms |
| Skicka inställningarna till bilen | Tryck **Apply settings** och invänta kvittens |
| Köra automatiskt | Tryck **Start car** |
| Stoppa bilen | Tryck **Stop car** |

Körläget väljer en av körfunktionerna i `logic.cpp`. Duty 0 är inte samma sak som stopp: bilen kan fortfarande vara armerad och styra. Använd **Stop car** för att avarmera. Manuellt servotest, reaktionsavstånd och justerbart loopintervall ingår inte i denna version.

Kör inte `run-car.ps1` samtidigt som webbpanelen styr bilen.

## Köra utan webbpanel – valfritt

Från projektroten kan du använda terminalskripten i stället:

```powershell
.\tools\mqtt\run-car.ps1
```

Låt terminalen vara öppen för heartbeat. Stoppa från en annan terminal i projektroten:

```powershell
.\tools\mqtt\stop-car.ps1
```

## Spara sensordata – valfritt

Öppna ytterligare en PowerShell i projektroten:

```powershell
.\tools\mqtt\log-telemetry.ps1 -OutputPath .\tools\mqtt\logs\mitt-test.ndjson
```

För att bara visa telemetri i terminalen:

```powershell
.\tools\mqtt\watch-telemetry.ps1
```

Avsluta med **Ctrl+C**.

## Avsluta eller felsök

Tryck först **Stop car**. Avsluta webbservern, loggningen och brokern med **Ctrl+C**.
Avsluta ESP-IDF-monitorn med **Ctrl+]**.

| Problem | Kontrollera |
|---|---|
| Connection refused | Brokern måste köra på rätt adress och port. |
| Not authorized | Kontrollera kontots lösenord och brokerfilens ACL. |
| Panelen ansluter men bilen saknas | Kontrollera WiFi, datorns IPv4, bilens brokeradress och brandväggen. |
| Skriptet hittas inte | Kör från projektroten, inte `firmware`. |
| Nytt WiFi eller hotspot | Kontrollera datorns IPv4. Uppdatera bilens `menuconfig` och flasha om vid ändring. |
| Bilen står still | Kontrollera Start, duty och stopporsaken i panelen. |

Lösenord finns lokalt i `sdkconfig` och `tools/mqtt/.env`. Lägg inte dessa filer i Git.
