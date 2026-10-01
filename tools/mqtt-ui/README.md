# MQTT-webbpanelen

Webbpanelen visar tio sekunders historik för tre IR-sensorer, begärd styrvinkel och motorpådrag. Här kan du starta/stoppa bilen och ändra körläge, duty, stoppavstånd och telemetriintervall.

**Ford:** kör `.\tools\mqtt-ui\start-ui.ps1 -Car ford`, se [Ford-guiden](../../documentation/guides/ford_manual_by_remote.md).

**Första gången:** följ [guiden från installation till körning](../../documentation/guides/mqtt_steg_for_steg.md).
För den integrerade körloopen, se även [systemtest och provade inställningar](../../documentation/guides/main_system_test.md).

**Redan konfigurerat:** starta brokern och bilen enligt steg 5 i guiden. Kör sedan från projektroten:

```powershell
cd 'C:\projekt\elektronik_projektet'
.\tools\mqtt-ui\start-ui.ps1
```

Byt sökvägen mot din egen projektrot. Öppna **http://127.0.0.1:8765** och kontrollera **LIVE MQTT**, **BROKER CONNECTED** och **CAR RECEIVING**.

**Andra enheter (t.ex. en telefon på cnb-net):** klicka på **LAN OFF** överst på sidan
på http://127.0.0.1:8765. Den visar då adressen att öppna på telefonen. Knappen syns och
fungerar bara på 127.0.0.1, och LAN-åtkomsten är alltid avstängd när servern startar.
Tillåt Node.js i Windows-brandväggen första gången. Alla på nätet kan då styra bilen.

Ändra inställningar med **Apply settings** och invänta kvittens. **Start car** aktiverar körning; **Stop car** avarmerar. Kör inte `run-car.ps1` samtidigt som panelen styr bilen.

Med systemtest-firmware visas även **Reaction distance**, **Loop interval** och
**Stop motor & test servo**. Servotestet stoppar motorn och ställer önskad vinkel;
ett nytt Start återgår till navigationen. Sensorernas beslutsavstånd visas separat
från mätvärdena. Systemtestet använder endast körläget `decide_action`.

För att prova panelen utan bil eller broker:

```powershell
.\tools\mqtt-ui\start-ui.ps1 -Demo
```

Demo använder simulerade värden och en förenklad körmodell, inte bilens exakta
väggkorrigering. Avsluta servern med **Ctrl+C** innan du byter läge.
