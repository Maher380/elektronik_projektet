# Kodversion för rapportunderlaget

## Fast kodreferens

- Kodförråd: https://github.com/Maher380/elektronik_projektet
- Kodcommit: `e9f559a9b159666df9a87a6acd7558b417917950`
- Stabil länk: https://github.com/Maher380/elektronik_projektet/tree/e9f559a9b159666df9a87a6acd7558b417917950
- Gren vid publicering: `codex/vagrant-logic`.

Committen omfattar aktuell Vagrant-logik, test-/tävlingsval, D10-startmodul,
kickstart och regressionstester. Dess förfäder innehåller MQTT och den manuella
motorprovningen i webbpanelen. Rapporttexten versioneras i en efterföljande
dokumentationscommit så att den kan referera till kodens redan fastställda hash.

Detta är rapportens kodunderlag, inte en verifierad kopia av den firmware som
flashades inför tävlingen. Den senare jämförelsen har inte genomförts.

## Byggmiljö och offentliga inställningar

- ESP-IDF-trädets versionsbeteckning vid kontrollen: `v6.1`.
- Upplösta beroenden i den lokala miljön: ESP-IDF 6.1.0, ESP-MQTT 1.1.0 och
  cJSON 1.7.19~2.
- Mål: ESP32-S3 och Vagrant.
- Kontrollerat driftläge: competition.
- Huvuduppgiftens stack: 8192 byte.
- Kontrollerad competition-profil: duty 44 procent, stop 45 cm och reaction 52 cm.
- Startsignal: D10/GPIO21 enligt `firmware/competition.md`.

Uppgifterna gäller den lokala kontrollmiljön. De fastställer inte automatiskt
vilka inställningar som användes vid varje tidigare körprov. Inga verkliga
nätverksnamn, lösenord eller privata brokeradresser ingår i detta dokument.

## Byggning

Öppna en ESP-IDF-miljö för rätt version och kör från `firmware`:

```bash
idf.py menuconfig
idf.py build
```

Välj Vagrant under Target car och önskat driftläge under Run mode. Vid testläge
anges nätverksuppgifter endast lokalt. Publicera inte den genererade
`sdkconfig`-filen. Versionerade `sdkconfig.defaults` innehåller grundinställningar,
inte en fullständig kopia av kontrollmiljön. Ett nytt bygge från en ren klon kan
behöva hämta komponenter enligt `main/idf_component.yml`.

## Utförda kontroller

Kontrollerna avser den källkod som låstes i kodcommitten, inte bilens tidigare
fysiska beteende.

| Kontroll | Resultat och avgränsning |
| --- | --- |
| `make -C test` från firmware | Godkänd |
| `make -C test navigation` från firmware | Godkänd med lokala cJSON-beroenden |
| `idf.py build` | Godkänt inkrementellt bygge i befintlig competition-miljö före kodcommitten; inte ett fullständigt nytt bygge från ren klon |
| `npm test` från `tools/mqtt-ui` | 18 godkända, 0 fel och 1 överhoppat test; Mosquitto-integrationstestet hittade inte sin förväntade körfil |
| `git diff --cached --check` | Inga rapporterade whitespacefel före kodcommitten |

Navigationstesterna använder fakes från `tests/system_test.cpp`, men kör inte
den äldre filens huvudtestfunktion. De bekräftar därför inte alla äldre
assertions eller fysisk svängförmåga. Navigationens Makefile-mål förutsätter att
cJSON finns under `firmware/managed_components/espressif__cjson/cJSON`, vilket
normalt tillhandahålls av ESP-IDF:s komponenthantering.

Ingen flashning, fysisk provkörning eller verifiering av tävlingsdagens binär
har gjorts som del av publiceringen. Säkerhetsskanning är en separat kontroll
och ersätter inte funktionstestning.

## Delar som inte publiceras

Privata konfigurationsfiler, byggprodukter, lokala loggar, maskinspecifika
editorinställningar, presentationens arbetsfiler och interna rapportanteckningar
ingår inte i publiceringen. Bibliotekets ursprung och MIT-licens redovisas i
projektets `THIRD_PARTY_NOTICES.md`.
