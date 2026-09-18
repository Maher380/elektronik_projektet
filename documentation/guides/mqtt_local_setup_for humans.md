# MQTT-experiment: lokal setup-checklista


1. Kör `idf.py menuconfig` i `firmware/` och navigera till `Autonomous car network configuration  --->` sätt sedan riktiga värden för:
   - `WiFi SSID` 
   - `Wifi password` (Kan lämnas tomt om lösenord ej behövs)
   - `MQTT broker URI` (`mqtt://DATORNS_IP:1883`)
   - `MQTT password`
   - `MQTT client ID` bör lämnas med defaultvärde
   - `MQTT username` bör lämnas med defaultvärde
   Spara och avsluta. Resultatet lagras i sdkconfig filen i repo rooten

2. Skapa filen `tools/mqtt/.env` manuellt:
   ```
   CNB_MQTT_HOST=<broker-PC:ns IP, utan mqtt://>
   CNB_MQTT_PORT=1883
   CNB_MQTT_USERNAME=cnb-dashboard
   CNB_MQTT_PASSWORD=<lösenord>
   ```

3. Kontrollera att du har Mosquitto Broker, bland Windows tjänster(/Services) annars installera Mosquitto

4. [Jag har kvar nedan text eftersom det är så här denna koden tvingar en att göra]
       Skapa broker-config utanför repot i `%USERPROFILE%\cnb-mqtt\`:
         Öppna powershell i rooten av repot
         ```powershell
         New-Item -ItemType Directory "$env:USERPROFILE\cnb-mqtt\data" -Force
         Copy-Item tools\mqtt\mosquitto.conf.example "$env:USERPROFILE\cnb-mqtt\mosquitto.conf"
         Copy-Item tools\mqtt\mosquitto-acl.example "$env:USERPROFILE\cnb-mqtt\acl"
         ```
         Öppna `mosquitto.conf` och ersätt `REPLACE_ME` med din användare (sökvägarna
         ska peka på mappen du just skapade). Skapa sedan lösenordsfilen med
         Mosquittos verktyg (kräver att Mosquitto är installerat):
         ```powershell
         & 'C:\Program Files\mosquitto\mosquitto_passwd.exe' -c "$env:USERPROFILE\cnb-mqtt\passwords" cnb-vagrant
         & 'C:\Program Files\mosquitto\mosquitto_passwd.exe' "$env:USERPROFILE\cnb-mqtt\passwords" cnb-dashboard
         ```
         (`-c` skapar filen och används bara för första kontot. Du blir tillfrågad om
         lösenord för varje `mosquitto_passwd`-anrop — använd samma lösenord som du
         satte i `sdkconfig` för `cnb-vagrant` och i `.env` för `cnb-dashboard`.)
   [Men jag hade hellre skrivit]
   gör kopior av tools\mqtt\mosquitto.conf.example och tools\mqtt\mosquitto-acl.example och döp dem till 
   tools\mqtt\mosquitto.conf resp tools\mqtt\acl 
   [här går jag vilse pga  primärt "password_file C:/Users/REPLACE_ME/cnb-mqtt/passwords" vad är det för fil och hur ska den fyllas i?]
   [skulle relativa pather funka? dvs skulle password_file ./tools/mqtt/passwords funka om den ligger i repot?]
   
]


5. Starta brokern:
   ```powershell
   & 'C:\Program Files\mosquitto\mosquitto.exe' -c "$env:USERPROFILE\cnb-mqtt\mosquitto.conf" -v
   ```
   [ jag hade helre velat ha alla konfig filer lokalt i repot men .gitignorade]

6. Bygg och flasha:
   ```powershell
   cd firmware
   idf.py build
   idf.py flash monitor
   ```

7. Kör via skripten i `tools/mqtt/`: `set-config.ps1`, `run-car.ps1`,
   `watch-telemetry.ps1`, `stop-car.ps1`.

   **`tools/mqtt-ui/` finns inte i repot** — ignorera webbpanelsdelen av
   `mqtt_steg_for_steg.md`.
