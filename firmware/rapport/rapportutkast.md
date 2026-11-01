# Utveckling av autonoma tävlingsbilar

Teknisk konstruktionsrapport

Författare: Maher [efternamn], Martin [efternamn] och Daniel [efternamn]

Utbildning och kurs: [kompletteras]

Handledare: [kompletteras]

Projektperiod och inlämningsdatum: [kompletteras]

## Sammanfattning

Projektets mål var att utveckla bilar som självständigt kunde ta sig runt en tävlingsbana. För deltagande i tävlingen krävdes ett genomfört autonomt kvalificeringsvarv. Arbetet omfattade elektronik, inbyggd mjukvara och utvecklingsverktyg för att undersöka bilarnas beteende.

Mjukvaran vidareutvecklades från ett befintligt kursbibliotek med drivrutiner, abstrakta gränssnitt och en fabrik. Denna struktur behölls och utökades för att skilja bilspecifik logik från hårdvaruimplementationer och underlätta återanvändning mellan bilar. För Vagranten utvecklades ett webbaserat testgränssnitt med MQTT-kommunikation, där sensorvärden kunde följas och motor, styrning och körparametrar provas. Ett separat tävlingsläge använde en fysisk startmodul utan att aktivera WiFi och MQTT.

Arkitekturen och testgränssnittet var användbara under utveckling och felsökning. Vagranten körde emellertid med felaktig navigation under tävlingsdagen och klarade inte det obligatoriska kvalificeringsvarvet, trots att den enligt gruppens observationer fungerade dagen före. Ingen av gruppens bilar deltog i själva tävlingen. Det övergripande tävlingsmålet uppnåddes därmed inte. Orsaken till Vagrantens felaktiga navigation fastställdes inte. De genomförda livetesterna gav inte tillräckligt underlag för att säkerställa ett tillförlitligt autonomt beteende under tävlingsförhållanden.

[Daniel och Martin kompletterar sammanfattningen med hårdvarans respektive SLAM-arbetets viktigaste resultat. Sammanfattningen färdigställs när dessa avsnitt är klara.]

## Innehållsförteckning

[Genereras i ordbehandlaren när rapportens rubriker och layout är fastställda.]

## Terminologi

- ADC: analog-till-digital-omvandlare.
- PWM: pulsbreddsmodulering. Duty cycle anger signalens relativa aktiva tid.
- Firmware: mjukvara som körs i bilens inbyggda styrsystem.
- Interface: ett programmeringsgränssnitt som anger vilka operationer en komponent erbjuder.
- Factory: ett objekt som skapar konkreta implementationer bakom gemensamma gränssnitt.
- Telemetri: överföring av mätvärden och systemtillstånd för övervakning.
- MQTT: meddelandeprotokoll som i projektet används för telemetri, konfiguration och kommandon.
- SLAM: samtidig lokalisering och kartläggning, från engelskans Simultaneous Localization and Mapping.

## 1 Introduktion

### 1.1 Bakgrund och syfte

En autonom tävlingsbil behöver samordna avståndsmätning, framdrivning och styrning utan fortlöpande manuella körkommandon. Samtidigt kräver utvecklingen möjlighet att undersöka delsystem var för sig. Om bilen exempelvis svänger fel behöver det gå att skilja ett felaktigt sensorvärde från ett fel i vägvalet eller i överföringen av styrkommandot till hårdvaran.

Projektets övergripande syfte var att konstruera bilar som kunde genomföra en tävlingsbana helt autonomt. Ett autonomt varv krävdes för att kvalificera sig till tävlingen. För mjukvaran var en viktig utgångspunkt att separera hårdvaruhantering från bilspecifika beslut och att tillhandahålla stöd för provning och felsökning.

### 1.2 Mål och avgränsningar

Det övergripande tävlingsmålet kompletterades i utvecklingen av funktioner för sensoravläsning, motorstyrning, styrning och testning. I denna rapport skiljs förmågan att prova enskilda funktioner från förmågan att genomföra ett autonomt varv. Det senare är ett systemresultat som inte kan härledas enbart från att delsystemen reagerar på manuella kommandon.

Rapportens mjukvarudel behandlar främst den gemensamma firmwarestrukturen, Vagrantens hårdvaruanknytning, MQTT och webgränssnittet samt skillnaden mellan test- och tävlingsläge. Körstrategin beskrivs översiktligt. Någon kvantitativ utvärdering av varvtider, styrnoggrannhet eller sensorernas mätfel redovisas inte, eftersom sådana mätserier inte finns tillgängliga.

[Gruppen kompletterar med tävlingsregler och övriga ursprungliga krav. Daniel anger hårdvarudelens avgränsningar. Martin anger vad SLAM-delen faktiskt omfattade.]

## 2 Konstruktion

### 2.1 Systemöversikt

Vagrantens styrsystem läser tre IR-baserade avståndssensorer och använder mätningarna för att bestämma motorpådrag och styrkommando. Den granskade implementationen använder ESP32-S3. Applikationen bygger på ESP-IDF och är indelad i hårdvarunära drivrutiner, kommunikationshantering, tillståndshantering och billogik [1].

I testläget tillkommer en dator med webbpanel och anslutning till en MQTT-broker. Webbpanelen är ett utvecklingsverktyg och ska inte förväxlas med bilens autonoma beslutsfunktion. I tävlingsläget kommer startsignalen i stället från en fysisk startmodul.

[Figur 1 infogas här: en gemensam systembild med bilens sensorer, styrenhet, motor och styrdon samt dator och broker i testläget. Markera vad som endast används under test. Daniel och Martin kompletterar med sina delsystem.]

### 2.2 Hårdvarukonstruktion

[Daniel kompletterar med matningens slutliga uppkoppling, styrdon, mekanik, dimensionering och verifiering. Följande komponentöversikt avgränsas till gränsen mellan hårdvara och mjukvara.]

#### 2.2.1 Komponentidentifiering och datablad

Inköpslistan för sprint 1 anger ett MP6550-baserat motordrivkort, artikel 41020343, och listan för sprint 3 anger IR-sensorn GP2Y0A21YK, artikel 41004830. Listorna omfattar inköp för projektet och ska inte tolkas som antal monterade komponenter på en viss bil. Vagrantens aktuella kod använder tre IR-sensorer. Schemat anger L7805, och äldre kopplingsdokumentation anger L7805CV [1].

Tabell 1 sammanfattar komponenterna och vilka uppgifter som används i mjukvarubeskrivningen. Databladsgränser är inte uppmätta resultat från den färdiga bilen.

Tabell 1. Komponenter i Vagrantens mät- och styrkedja.

| Komponent | Identifieringsunderlag | Tekniskt underlag |
| --- | --- | --- |
| Sharp GP2Y0A21YK, datablad GP2Y0A21YK0F | Inköpslista, schema och sensordriver | Analog avståndsutgång, specificerat mätområde 10–80 cm och matning 4,5–5,5 V [2] |
| L7805/L7805CV | Schema, äldre kopplingsdokumentation och gruppens bekräftelse av modellen på den slutliga bilen | Linjär 5 V-regulator [3] |
| MP6550-baserat motordrivkort | Inköpslista och aktuell MP6550-driver | H-brygga för en borstad DC-motor, matningsområde 1,8–22 V [4] |
| Pololu MP6550 carrier, artikel 4733 | Den inköpta artikelns produktlänk identifierar detta kort | Korttillverkaren anger cirka 1,7 A kontinuerligt; detta skiljs från kretsdatabladets 2 A [4], [5] |

Sharps timingdiagram anger en mätperiod på 38,3 ± 9,6 ms. En programloop på 20 ms innebär därför inte en ny oberoende sensormätning vid varje avläsning. Tillverkaren rekommenderar också minst 10 µF avkoppling nära sensorns matningsanslutning [2, s. 4 och 6]. Mjukvarans avståndskapning ersätter varken korrekt matning eller kontroll av mätvärden utanför det specificerade området.

För L7805C anger ST typiskt 2 V dropout vid 1 A; utspänningen kan alltså inte förutsättas vara reglerad enbart för att inspänningen överstiger 5 V. Linjär reglering innebär dessutom effektförlust som värme [3, tabell 10]. Matningsmarginal och temperatur är därför relevanta kontrollpunkter, inte fastställda förklaringar till projektets regulatorfel.

MP6550 stöder PWM upp till 100 kHz [4]. Den aktuella motor-PWM:n är 20 kHz [1]. Pololus dokumentation skiljer driftlägena framåt, bakåt, frirullning och bromsning samt beskriver SLEEP som en aktiveringssignal [5]. Hög nivå på båda styringångarna betyder bromsning, inte samtidig framåt- och bakåtkörning. Varken databladsuppgifter eller programmets duty-värde bevisar vilken ström eller rörelse den belastade bilen hade.

### 2.3 Mjukvaruarkitektur

Utgångspunkten var drivrutinsbiblioteket `cpp-driver-library-p02`, som kom från en annan kurs. Projektets README och `THIRD_PARTY_NOTICES.md` anger ursprunget, MIT-licensen och den upstream-commit som användes som startpunkt [1], [6]. Biblioteket innehöll redan bland annat ADC-, GPIO-, timer-, WiFi- och seriella drivrutiner, abstrakta gränssnitt, teststubbar och en fabriksstruktur. Dessa delar ska därför inte tillskrivas bilprojektet som helt nyutvecklade.

Projektarbetet bestod i att anpassa och utöka grunden för bilarnas komponenter och integrera den med billogik och utvecklingsverktyg. Bland utökningarna finns avståndssensorer, PWM och motorstyrning samt MQTT-baserad kommunikation och webbpanelen. Befintliga drivrutiner behövde också anpassas, exempelvis för flera avståndssensorer som använder samma ADC-enhet. Utvecklingen genomfördes i nära samarbete inom gruppen. Beskrivningen avser därför gruppens gemensamma lösning, inte en uppdelning av upphovsmannaskap utifrån vem som står som författare till en commit.

#### 2.3.1 Ansvarsfördelning och uppstart

Firmwarestrukturen skiljer mellan vad bilen ska göra och hur hårdvaran ska styras. Billogiken fattar beslut utifrån tillgängliga mätvärden. Drivrutinerna översätter anropen till den valda hårdvarans API. Kommunikationslagret hanterar anslutningar och meddelanden, medan körbehörighet och kommandotillstånd ligger i en separat tillståndskomponent, `Control` [1].

Startpunkten `app_main()` i `main.cpp` är avsiktligt liten. I normal körning skapas en konkret fabrik och den valda billogiken, varefter logikens `run()` anropas. Valet av bil görs vid kompilering genom projektets Kconfig-inställningar och typaliaset `TargetLogic` i `target.h`. Därmed behöver uppstarten inte innehålla flera fullständiga bilprogram.

Den aktuella strukturen har val för Vagrant, Ford och en tredje bil. Vagrantens körlogik är implementerad. Ford och den tredje bilen har i detta firmwareträd endast platshållare som rapporterar att körlogik saknas. Strukturen möjliggör därför flera implementationer, men utgör inte i sig bevis för att tre autonoma bilar har färdigställts.

#### 2.3.2 Abstrakta gränssnitt och fabrik

Drivrutinernas gränssnitt är separerade från deras konkreta implementationer. Exempelvis erbjuder ADC-gränssnittet operationer för initiering, råavläsning och spänningsavläsning. En användare av gränssnittet behöver inte känna till vilka ESP-IDF-anrop som krävs för att utföra dessa operationer.

Valet att behålla och vidareutveckla interface- och fabriksstrukturen motiverades av två behov: att kunna byta hårdvaruplattform vid behov och att kunna återanvända kod mellan olika bilar. Fabriken samlar skapandet av konkreta drivrutiner. ESP32-S3-fabriken returnerar exempelvis en `driver::adc::Esp32s3` genom typen `std::unique_ptr<driver::adc::Interface>`. Anropande kod arbetar därefter mot gränssnittet. `std::unique_ptr` uttrycker vilket objekt som äger instansen. Sensor- och aktuatordrivrutiner kan samtidigt hålla icke-ägande referenser till de underliggande komponenter de använder. Dessa komponenter måste därför leva längre än objekten som refererar till dem.

Fördelen är att skapande, ägande och användning inte behöver blandas med hårdvaruspecifika detaljer på varje anropsställe. Lösningen medför dock fler typer och ett behov av tydliga kontrakt för enheter, felvärden och livslängder. Gränssnitt är endast användbara om olika implementationer följer samma kontrakt.

#### 2.3.3 IR-sensorn som exempel på hårdvaruabstraktion

IR-drivern `GP2Y0A21YK` tar emot en referens till `driver::adc::Interface`. Drivern efterfrågar en spänning och omvandlar den till ett avstånd genom den spänning-till-avstånd-modell som finns i implementationen. Den gör inga direkta anrop till ESP-IDF [1].

Det avgörande är att IR-drivern inte behöver veta om spänningen kommer från ADC:n i en ESP32-S3 eller exempelvis en ATmega328P. För en annan plattform behöver en kompatibel ADC-implementation tillhandahållas. Den ska returnera spänningen i samma enhet och följa samma felkontrakt. Själva sensorkoden behöver då inte ändras på grund av ADC:ns plattformstillhörighet.

Detta innebär inte att hela firmwaren automatiskt kan flyttas till ATmega328P. Byggsystem, resurser, realtidsfunktioner och övriga plattformsberoenden behöver också hanteras. Någon sådan portning har inte verifierats. Exemplet visar var beroendegränsen ligger, inte ett genomfört plattformsbyte.

Kort kodexempel som visar det befintliga beroendet:

```cpp
GP2Y0A21YK::GP2Y0A21YK(driver::adc::Interface& adc) noexcept
    : myAdc{adc}
{}

// ADC-implementationen ansvarar för att leverera spänningen i volt.
// Sensormodellen behöver inte känna till mikrokontrollerns ADC-register.
const float voltage = myAdc.readVoltage();
```

Den sista raden illustrerar spänningsavläsningen inne i `readDistance()` och är inte ett fristående program.

[Figur 2 infogas här: IR-driver beroende av ADC-interface, med ESP32-S3 och en hypotetisk ATmega328P-implementation under gränssnittet. Markera den senare som ej implementerad.]

### 2.4 Hårdvarunära drivrutiner

#### 2.4.1 ADC och spänningsavläsning

ESP32-S3-drivern använder ADC-avläsning i oneshot-läge och en separat kalibreringshantering. Råvärdet omvandlas till millivolt och därefter till volt innan det lämnas till sensordrivern. Espressif beskriver kalibreringen som ett sätt att minska effekten av variationer i ADC:ns referensspänning och tillhandahåller stöd för curve fitting på ESP32-S3 [7].

Flera ADC-objekt delar samma underliggande ADC-enhet. En referensräknare håller reda på användarna, och en mutex skyddar skapande och frigöring av den delade resursen. I den granskade implementationen omsluter denna mutex inte varje sensoravläsning. Vagrantens tre sensorer läses sekventiellt i körloopen, inte genom tre samtidiga analoga samplingar [1].

ADC-kalibreringen ska skiljas från kalibreringen av avståndssensorn. En mer korrekt spänningsavläsning säkerställer inte att avståndsmodellen är korrekt för varje sensor eller mätsituation. Den aktuella IR-drivern använder gemensamma modellkonstanter och saknar individuell sensorkalibrering.

Vid misslyckad spänningsavläsning kan drivern returnera `NaN`. Det gör det möjligt för överordnad logik att skilja en felaktig avläsning från ett vanligt numeriskt avstånd. Ett ändligt men orimligt värde behöver däremot ytterligare rimlighetskontroll för att kunna identifieras som fel.

#### 2.4.2 PWM, motor och styrning

PWM-drivern bygger på ESP32-S3:s MCPWM-periferi. Implementationens timer anger perioden, komparatorn anger när signalen ska ändras och generatorn bestämmer utgångens nivå vid dessa händelser. Den uppdelningen motsvarar Espressifs beskrivning av MCPWM:s delmoduler [8].

Motorgränssnittet uttrycker riktning, normaliserad duty cycle och stoppläge. För Vagranten används en MP6550-implementation som översätter dessa operationer till två PWM-utgångar. Billogiken behöver därmed inte vid varje körbeslut formulera drivkretsens signalnivåer direkt.

Vagrantens styrdrivrutin omvandlar en logisk styrvinkel till en frekvens med mittläge och ändlägen definierade i koden. Den ska därför inte beskrivas som identisk med en vanlig RC-servostyrning där enbart pulsbredden ändras vid fast frekvens. Den logiska vinkeln är dessutom ett styrkommando, inte en uppmätt hjulvinkel. Riktning, mittläge och användbara ändlägen behöver verifieras på den monterade bilen [1].

### 2.5 MQTT och webbaserat testgränssnitt

#### 2.5.1 Kommunikationsväg

MQTT använder publicering och prenumeration via en server, här en broker. Meddelanden förmedlas genom ämnen, så kallade topics, vilket skiljer sändaren från de klienter som tar emot meddelandena [9]. I projektet används separata topics för telemetri, konfiguration, kommandon och återrapporterat tillstånd [1].

Webbläsaren kommunicerar med en lokal webbserver. Servern hanterar kopplingen till MQTT-systemet. Användarens kommandon går via denna väg till bilen, och bilens telemetri går tillbaka till panelen. Webbgränssnittet kommunicerar alltså inte direkt med motor- eller sensordrivrutinerna.

[Figur 3 infogas här: webbläsare, lokal webbserver, MQTT-broker och bil. Visa kommandovägen och den återgående telemetrin med separata pilar.]

#### 2.5.2 Telemetri och manuella tester

Panelen visar tre avståndsmätningar, styrkommando och motorpådrag med en kort historik. Råa avstånd och avstånd som begränsats för beslutsfattandet hålls isär. Utvecklaren kan därmed undersöka om en avvikelse redan finns i sensordata eller uppstår senare i besluts- och utgångshanteringen.

Panelen ger också möjlighet att prova motor framåt och bakåt under begränsad tid, ställa styrningen med motorn stoppad, starta och stoppa autonom körning samt ändra körparametrar. Dessa funktioner användes vid praktiska livetester. Parameterändring via MQTT gjorde det möjligt att prova nya inställningar utan att bygga och flasha om programmet för varje sådan ändring.

Telemetrin har dock en viktig begränsning: ett begärt motorpådrag är inte en uppmätt hastighet, och ett styrkommando är inte en verifierad fysisk styrvinkel. Panelen ger insyn i mjukvarans mätvärden och kommandon, men ersätter inte observation av bilens rörelse eller separat mätutrustning.

#### 2.5.3 Tillstånd och kommunikationsfel

Körbehörigheten hanteras separat från MQTT-meddelandenas JSON-format. `Control` skiljer mellan armerat och avarmerat tillstånd och behandlar bland annat start, stopp, sessionsidentitet och heartbeat. Testlägets heartbeat har en tidsgräns på tre sekunder. Förlorad anslutning eller utebliven heartbeat kan därmed avbryta körbehörigheten. Detta är en mjukvarumekanism, inte ett separat hårdvarunödstopp [1].

Konfigurationsmeddelanden kontrolleras innan de tillämpas, och revisionsnummer används för att hantera deras ordning. Beständigt lagrade MQTT-kommandon avvisas, så att ett tidigare startkommando inte behandlas som en ny användaravsikt vid återanslutning. Att MQTT kan lagra retained-meddelanden är en protokollfunktion [9]; beslutet att avvisa dem för körkommandon ligger i projektets applikation [1].

Utgående MQTT-trafik överlämnas genom köer till en separat publiceringsuppgift. Därmed undviker körloopen att själv utföra de nätverksanrop som publiceringen kräver. Detta minskar ett direkt beroende mellan körloop och nätverkstrafik, men är inte ett uppmätt bevis för garanterade svarstider.

### 2.6 Testläge och tävlingsläge

Vagrantens testläge och tävlingsläge använder samma navigationskod, men olika startvillkor och konfigurationsvägar. I testläget används WiFi, MQTT och webbpanel. Tävlingsläget aktiverar i stället en GPIO-baserad startmodul på D10, vilket motsvarar GPIO21 i den aktuella kopplingen. Någon WiFi- eller MQTT-kommunikation startas inte i Vagrantens tävlingsgren [1].

Startmodulen kräver först en observerad låg signal och därefter en hög signal under minst 20 ms. När bilen har startat medför låg signal ett stopp som är låst tills modulen återställs genom omstart. Detta förhindrar att en redan hög signal vid uppstart omedelbart tolkas som en ny startsekvens.

Testlägets körparametrar kan ändras under körning genom MQTT, medan tävlingsparametrarna väljs i byggkonfigurationen. Samma algoritm innebär därför inte automatiskt samma beteende: parameteruppsättningar och startvillkor kan skilja sig. Dessa behöver dokumenteras tillsammans med kodversionen vid jämförande tester.

### 2.7 Vagrantens navigationsprincip

Varje giltigt avstånd begränsas uppåt till `reaction_distance_cm` innan det används i vägvalet. Bilen väljer sedan den riktning som har störst begränsat avstånd. Vid lika fri väg framåt prioriteras framåtkörning, kompletterad med proportionella väggkorrigeringar. Syftet med kapningen är att avstånd bortom den valda beslutshorisonten inte ska påverka vägvalet [1].

Backning används när alla riktningar bedöms blockerade. Manövern omfattar motorvila och förberedelse av styrningen innan backpulsen. Därefter väljs väg utifrån nya mätvärden. Motorpådraget under normal körning ska hållas skilt från styrutslaget. Ingen fullständig PID-regulator med integral- och derivatadel används för väggkorrigeringen.

Att välja det största sensoravståndet säkerställer inte att bilens hela rörelsebana är fri. Sensorernas riktningar, fordonets geometri, hastighet och styrningens respons påverkar om bilen faktiskt kan genomföra en sväng. Kapningen kan inte heller skilja fri väg från ett ändligt men felaktigt stort sensorvärde. Dessa begränsningar är viktiga när körbeteendet utvärderas.

### 2.8 Lokalisering och SLAM

[Martin skriver detta avsnitt. Beskriv den faktiska arkitekturen och gränssnittet mot bilen, vilka delar som körs på respektive dator samt vilka funktioner som har verifierats. Skilj mellan kartläggning, lokalisering och autonom banföljning. Ange vilket separat kodförråd och vilken version som avses.]

## 3 Projektets genomförande

### 3.1 Planering och samordning

Under projektets första månad användes Jira i stor utsträckning. Därefter upphörde användningen, och den löpande samordningen skedde genom dagliga avstämningar, så kallade daily stand-ups. Arbetssättet förändrades därmed från uppföljning i ett gemensamt verktyg till främst muntlig samordning.

Utvecklingen var i stor utsträckning gemensam. Gruppen använde också Git och pull requests för att föra samman ändringar. Kodhistoriken visar en stegvis utveckling från det importerade drivrutinsbiblioteket till sensorstöd, motorstyrning, MQTT, webbpanel och en integrerad körloop. Pull requests gav en möjlighet att granska avgränsade ändringar, men att en ändring integrerades innebar inte automatiskt att den färdiga bilen hade provats med hela kombinationen av ändringar.

Retrospektivet från sprint 2 tog upp behovet av flera testmiljöer, effektivare digital dokumentation av testresultat och mer meningsfulla dagliga avstämningar. Ett förslag var att först formulera ett gemensamt prioriterat mål för dagen och därefter individuella uppgifter. Dokumentet angav också ett helt autonomt varv med Vagranten som färdigkriterium innan arbetet gick vidare till nästa bil [10]. Detta är dokumenterade mål och förbättringsförslag, inte belägg för att samtliga följdes i praktiken.

### 3.2 Färdigkriterier och integration

Gruppens Definition of Done omfattade bland annat kompilering, felhantering, kodkommentarer, granskning av en annan gruppmedlem och integration till `main`. På user story-nivå ingick även sammanhängande funktion, integrationstestning och uppdaterad dokumentation. Dokumentet skilde samtidigt mellan antagna kriterier och förslag som ännu inte ingick. Enhetstester per koduppgift var exempelvis identifierade men ännu inte införda som krav på task-nivå [11].

Det fanns alltså en dokumenterad ambition att bedöma mer än om en enskild funktion hade implementerats. Tävlingsutfallet och gruppens egen bedömning av testningen visar däremot att arbetet inte gav tillräcklig säkerhet om bilarnas sammanlagda funktion. För fortsatt projektarbete behöver färdigkriterier kopplas till sparat verifieringsunderlag, inte enbart till att en uppgift eller pull request har avslutats.

### 3.3 Testning som del av utvecklingsarbetet

Livetester genom webgränssnittet användes för att undersöka sensorvärden, motor, styrning, start och stopp samt ändrade körparametrar. Testverktyget gav direkt återkoppling och var enligt gruppens erfarenhet värdefullt vid felsökningen. Gruppen bedömer samtidigt att testningen inte genomfördes tillräckligt noggrant.

Det saknas sparade testprotokoll och mätserier från dessa körprov. Därför går det inte i efterhand att jämföra alla provförhållanden eller ange hur många upprepningar som utfördes med en viss konfiguration. Denna begränsning påverkar både verifieringen av tävlingsförmågan och möjligheten att utreda avvikelserna under tävlingsdagen.

## 4 Verifiering

### 4.1 Genomförda praktiska tester

Provningen av Vagranten omfattade en fysisk bil, en MQTT-broker och det lokala webgränssnittet. Genom panelen provades motor i båda riktningarna, styrning, avläsning av sensorvärden, start och stopp samt ändring av körparametrar. Återkopplingen bestod av panelens visning och direkt observation av bilen. Dessa moment var funktionella livetester, inte en dokumenterad mätserie.

För varje testområde anger Tabell 2 vad som provades och vad underlaget inte räcker till för att fastställa. Tabellen är en sammanställning av genomförda testområden, inte ett protokoll med formellt godkända testfall.

Tabell 2. Praktiska testområden och begränsningar i underlaget.

| Testområde | Genomfört moment | Begränsning |
| --- | --- | --- |
| Motor | Manuella kommandon framåt och bakåt | Ingen samlad dokumentation av belastning, ström eller rörelse per prov |
| Styrning | Manuella styrkommandon | Ingen redovisad kalibreringsserie för fysisk hjulvinkel |
| Sensorer | Livevisning av avstånd | Ingen sparad serie mot kända referensavstånd |
| Start och stopp | Kommandon genom webbpanelen | Ingen dokumenterad fördelning av svarstider |
| Körparametrar | Ändring genom MQTT och observation av beteendet | Ingen fullständig versionskopplad jämförelse mellan inställningar |
| Autonom körning | Praktiska körprov och körning under tävlingsdagen | Ingen sparad serie med varvresultat och provförhållanden |

Exakta provdatum, konfigurationer och förhållanden kan inte rekonstrueras från någon samlad testlogg. Därför uppfyller dessa livetester inte ensamma kravet på full reproducerbarhet. För framtida prov bör kodversion, byggkonfiguration, parameteruppsättning, batteritillstånd, bana, testmoment och utfall sparas tillsammans.

### 4.2 Felsökning av felaktiga sensorvärden

Ett konkret felsökningstillfälle började med att webgränssnittet visade felaktiga sensorvärden. Observationen ledde till fortsatt felsökning och praktisk provning. Enligt gruppens redogörelse kunde felet därefter lokaliseras till en trasig spänningsregulator.

Panelens roll var att synliggöra avvikelsen i mätkedjan, inte att direkt diagnostisera regulatorn. Felsökningen behövde därför gå vidare från den visade avståndsinformationen till den fysiska uppkopplingen och matningen. Det finns inte något sparat mätprotokoll som anger regulatorns spänningar, testinstrument eller de enskilda felsökningsstegen. Dessa detaljer kan därför inte redovisas som uppmätta resultat.

Händelsen ska hållas åtskild från den felaktiga navigationen under tävlingsdagen. Underlaget visar inte att regulatorfelet orsakade det senare tävlingsutfallet.

### 4.3 Automatiserade tester i kodbasen

Kodbasen innehåller hostbaserade tester med ersättningsimplementationer för hårdvara och tid. Det finns även navigationsregressionstester som anropar Vagrantens körloop med simulerade indata. Konstruktionen gör det möjligt att undersöka exempelvis avståndskapning, vägval, kommandon och backsekvenser utan att varje test behöver utföras på den fysiska bilen [1].

Dessa tester ska hållas åtskilda från de praktiska livetesterna. Ett test med simulerade sensorer verifierar hur koden reagerar på givna värden, men inte om verkliga sensorer mäter korrekt eller om bilen kan genomföra den begärda svängen. Kodbasens äldre tester kan dessutom representera tidigare körstrategier; testresultat behöver kopplas till rätt testmål och kodversion.

Historiska uppgifter om utfallet av automatiserade tester har även hämtats från projektets PR-beskrivningar. Dessa resultat och deras avgränsningar redovisas separat i avsnitt 5.2. De har inte körts om som del av rapportarbetet.

### 4.4 Verifiering av hårdvara och SLAM

[Daniel beskriver mätuppkopplingar, instrument och hårdvaruprov. Martin beskriver datainsamling och verifiering av SLAM. Metoderna ska kunna följas och upprepas av en utomstående.]

## 5 Verifieringsresultat

### 5.1 Mjukvara och utvecklingsverktyg

Den implementerade mjukvaran separerar billogik, kommunikation och drivrutiner genom tydliga typer och gränssnitt. IR-driverns beroende av ADC-gränssnittet är ett konkret exempel på denna separation. Någon jämförande mätning av utvecklingstid eller genomförd portning till en annan mikrokontroller finns däremot inte.

Webgränssnittet och MQTT användes vid praktiska funktionstester och var enligt gruppens erfarenhet värdefulla för felsökningen. De gav möjlighet att följa sensorvärden och prova kommandon utan att varje testmoment krävde en separat firmwareversion. Någon kvantifiering av tidsbesparing eller ökad testtäckning har inte genomförts.

Felsökningen av sensorvärdena gav också ett konkret delresultat: gruppen identifierade en trasig spänningsregulator efter att avvikelsen hade blivit synlig i webbpanelen. Slutsatsen bygger på gruppens redogörelse; någon sparad serie med elektriska mätvärden finns inte tillgänglig. Exemplet visar att information från mjukvaran kunde användas för att rikta fortsatt felsökning mot hårdvaran.

### 5.2 Historiskt redovisade automatiserade tester

GitHub-historiken innehåller uppgifter om genomförda automatiserade tester för flera utvecklingsversioner. Tabell 3 sammanfattar de resultat som redovisas i PR-beskrivningarna, tillsammans med respektive källas avgränsningar [12]–[14].

Tabell 3. Testresultat enligt historiska PR-beskrivningar, inte nya testkörningar av tävlingsversionen.

| Version | Redovisat utfall | Avgränsning |
| --- | --- | --- |
| PR #22, MQTT-firmware | 11 hostbaserade integrationsscenarier uppges ha passerat | Fullständigt ESP-IDF-bygge och fysisk fordonsverifiering återstod enligt beskrivningen |
| PR #23, webbpanel | 14 Node-tester uppges ha passerat, inklusive isolerad Mosquitto-integration och HTTP-åtkomstkontroll | Ingen fysisk bil kommenderades under verifieringen |
| PR #26, integrerad körloop | 24 navigationsfall och 18 webbpanelstester uppges ha passerat; även andra integrationskontroller redovisas | Ett fullständigt nytt bygge hade inte körts där; den kombinerade grenen behövde fortfarande bygg-, flash- och hårdvarukontroll |

Resultaten visar vilket testunderlag som redovisades för dessa ändringar. De innebär inte att den slutliga tävlingsversionen verifierats genom samma testmängd. Antalen avser olika versioner och testområden och ska inte summeras till ett mått på oberoende testtäckning.

### 5.3 Kvalificering och tävlingsutfall

Enligt gruppens observation fungerade Vagranten dagen före tävlingen. Under tävlingsdagens försök uppvisade bilen flera olika symptom: först körde den inte alls, därefter körde den in i väggen och senare fastnade den. Vagranten klarade inte det obligatoriska autonoma kvalificeringsvarvet. Vagranten kvalificerade sig därför inte till tävlingen. Ingen av gruppens bilar deltog i själva tävlingen. Enligt gruppens uppgift var gruppen det enda laget utan någon bil i tävlingen.

Gruppen uppger att den kod som nu ligger till grund för beskrivningen också användes vid kvalificeringen. Någon fullständig sparad kombination av flashad binär, konfiguration och telemetri från tillfället finns inte i det redovisade underlaget.

Orsaken till den felaktiga navigationen fastställdes inte. Underlaget medger därför inte ett säkert orsakssamband mellan tävlingsutfallet och en viss komponent eller kodändring.

[Daniel och Martin infogar sina verifieringsresultat i egna underavsnitt.]

## 6 Diskussion

### 6.1 Arkitekturens styrkor och begränsningar

Den ärvda och vidareutvecklade gränssnittsstrukturen gav en tydlig ansvarsfördelning mellan avståndsmätning, hårdvaruåtkomst och körbeslut. IR-drivern kan till exempel beskrivas och provas utifrån sin spänningsindata utan att hela ADC-implementationen ingår i resonemanget. På motsvarande sätt centraliserar fabriken valet av konkreta drivrutiner. Detta gör beroendena lättare att följa och skapar förutsättningar för återanvändning och isolerad testning.

Förutsättningarna ska dock inte förväxlas med verifierade egenskaper. En välindelad kodbas säkerställer inte korrekt elektrisk inkoppling, korrekta sensormodeller eller ett stabilt autonomt körbeteende. En felaktig indata kan följa alla programmeringsgränssnitt och ändå leda till ett olämpligt beslut. Detsamma gäller en korrekt beräknad styrvinkel som inte motsvarar bilens faktiska svängförmåga.

### 6.2 Värdet av MQTT och webgränssnittet

Testgränssnittets viktigaste bidrag var att göra mjukvarans indata och kommandon synliga under provningen. Det underlättade undersökning av enskilda funktioner och gav möjlighet till snabb parameterändring. Verktyget hade därför ett värde även när bilens autonoma beteende inte fungerade tillfredsställande. Fallet med den trasiga spänningsregulatorn illustrerar detta: ett fel som först syntes som orimliga sensorvärden behövde undersökas även utanför programkoden. Ett mätvärdes väg genom programmet kan vara korrekt implementerad samtidigt som den elektriska indata som modellen använder är felaktig.

Samtidigt är visning inte detsamma som dokumentation. Utan lagrad telemetri och en anteckning om provets konfiguration försvinner möjligheten att i efterhand granska ett händelseförlopp. Projektet innehåller redan ett separat verktyg för att spara MQTT-telemetri till fil [1]. Bristen var därför inte enbart avsaknad av tekniskt stöd för loggning, utan att någon sammanhängande sparad provserie inte finns för de aktuella körförsöken. Fortsatt arbete bör göra loggning och testprotokoll till en normal del av varje jämförande körprov.

### 6.3 Avvikelsen under tävlingsdagen

Det övergripande målet att få bilarna att delta i den autonoma tävlingen uppnåddes inte. De tekniska delresultaten, exempelvis drivrutinsstrukturen och testgränssnittet, ska därför inte likställas med en färdig och verifierad tävlingsbil.

Skillnaden mellan observationerna dagen före tävlingen och utfallet vid kvalificeringen visar att de tidigare körproven inte gav ett tillräckligt underlag för att bedöma tillförlitligheten. Systemets funktion behövde prövas upprepade gånger med dokumenterade förhållanden och i det driftläge som skulle användas vid tävlingen.

Sensorernas mätvärden, styrningens fysiska respons och skillnader mellan test- och tävlingskonfiguration är relevanta områden att undersöka. De är dock möjliga undersökningsområden, inte fastställda orsaker. Den uteblivna dokumentationen gör det svårt att avgöra vilka skillnader som faktiskt förekom mellan proven.

### 6.4 Reflektion över projektdriften

De dagliga avstämningarna gav en form för löpande samordning efter att Jira slutade användas. Att gruppen arbetade nära tillsammans underlättade gemensam utveckling, men behövde kompletteras med dokumentation av vad som återstod att integrera och verifiera. Muntliga avstämningar ersätter inte automatiskt en spårbar lista över uppgifter, kvarstående fel och verifieringsstatus. Att behålla någon form av skriftlig uppföljning hade gjort det lättare att skilja mellan implementerad, provad och systemverifierad funktionalitet.

Gruppens tydligaste identifierade brist var otillräckligt noggrann testning i kombination med en alltför optimistisk bedömning av den tid som återstod för färdigställande och provkörning. För ett autonomt system behövs avsatt tid för integration och upprepade körprov, inte bara utveckling av nya funktioner. Ett lämpligt förbättrat arbetssätt är att koppla varje viktig uppgift till ett verifieringskriterium och dokumentera vilket underlag som krävs innan den betraktas som färdig. Retrospektivet och Definition of Done visar att behovet av testning och dokumentation redan var identifierat [10], [11]. Lärdomen är därför inte bara att formulera sådana krav, utan att reservera tid för dem och följa upp att de genomförs.

Inför tävling bör en kandidatversion låsas och provas med samma konfiguration och startmodul som ska användas på banan. Senare ändringar behöver följas av omprovning. Detta är ett förslag till fortsatt arbetssätt, inte en beskrivning av ett förfarande som gruppen kan visa att den genomförde.

### 6.5 Fortsatt arbete

Nästa steg bör vara att kalibrera och verifiera sambandet mellan sensormätning, styrkommando och fysisk rörelse innan ytterligare körstrategier införs. Därefter behövs upprepade banprov med sparad telemetri, tydlig versionshantering och dokumenterade parameteruppsättningar. Hosttester bör komplettera dessa prov, inte användas som ersättning för dem.

[Daniel och Martin kompletterar diskussionen med sina tekniska begränsningar, avvägningar och erfarenheter av projektarbetet.]

## 7 Slutsatser

Projektet vidareutvecklade ett befintligt drivrutinsbibliotek till en firmware för autonoma bilar. Den ärvda interface- och fabriksstrukturen behölls och utökades för återanvändning mellan bilar och för att minska beroendet av en viss hårdvaruplattform. Hårdvarunära implementationer, kommunikation och billogik har separata ansvar. IR-driverns användning av ett ADC-gränssnitt visar hur sensorns modell kan frikopplas från en specifik mikrokontroller. MQTT och webgränssnittet var användbara verktyg för insyn och praktiska funktionstester. De bidrog bland annat till felsökning som identifierade en trasig spänningsregulator.

Det övergripande tävlingsmålet uppnåddes inte. Vagranten klarade inte kvalificeringsvarvet på grund av felaktig navigation, och ingen av gruppens bilar deltog i själva tävlingen. Den tekniska orsaken till Vagrantens felnavigering fastställdes inte. Avsaknaden av dokumenterade testserier begränsar möjligheten till efterhandsanalys. En central slutsats är därför att en tydlig mjukvaruarkitektur behöver kompletteras med systematisk systemverifiering och spårbara provresultat. Planeringen behöver också ge tillräckligt utrymme för integration och upprepade provkörningar.

[Slutsatser om hårdvara och SLAM kompletteras när dessa resultat har redovisats.]

## Referenser

[1] Projektgruppen, "elektronik_projektet", källkod för rapportunderlaget, commit `e9f559a9b159666df9a87a6acd7558b417917950`. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/tree/e9f559a9b159666df9a87a6acd7558b417917950. Versionen identifierar det redovisade kodunderlaget; den är inte verifierad mot tävlingsdagens flashade binär.

[2] Sharp Corporation, "GP2Y0A21YK0F Distance Measuring Sensor Unit", datablad E4-A00201EN, särskilt s. 1, 3–6. [Online]. Tillgänglig: https://global.sharp/products/device/lineup/data/pdf/datasheet/gp2y0a21yk_e.pdf.

[3] STMicroelectronics, "L78 Positive voltage regulator ICs", datablad DS0422, rev. 38, feb. 2025, särskilt tabell 10 för L7805C. [Online]. Tillgänglig: https://www.st.com/resource/en/datasheet/l78.pdf.

[4] Monolithic Power Systems, "MP6550 1.8V to 22V, 2A, H-Bridge Motor Driver", datablad, rev. 1.0, 16 juli 2019. [Online]. Tillgänglig: https://www.monolithicpower.com/en/documentview/productdocument/index/version/2/document_type/Datasheet/lang/en/sku/MP6550GG-Z/document_id/6810/.

[5] Pololu Corporation, "MP6550 Single Brushed DC Motor Driver Carrier", produktdokumentation, artikel 4733. [Online]. Tillgänglig: https://www.pololu.com/product/4733.

[6] O. Edman Chrantz, "cpp-driver-library-p02", drivrutinsbibliotek, commit `4aa1553964dc9f0e07de63856d025199d88307a2`, MIT-licens. [Online]. Tillgänglig: https://github.com/OliverEdman/cpp-driver-library-p02. Ursprung och licens dokumenteras även i projektets `THIRD_PARTY_NOTICES.md`.

[7] Espressif Systems, "Analog to Digital Converter (ADC) Calibration Driver", ESP-IDF Programming Guide, v6.1, ESP32-S3. [Online]. Tillgänglig: https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/api-reference/peripherals/adc/adc_calibration.html. [Hämtad-datum kompletteras.]

[8] Espressif Systems, "Motor Control Pulse Width Modulator (MCPWM)", ESP-IDF Programming Guide, v6.0, ESP32-S3. [Online]. Tillgänglig: https://docs.espressif.com/projects/esp-idf/en/v6.0/esp32s3/api-reference/peripherals/mcpwm.html. [Hämtad-datum kompletteras.]

[9] A. Banks och R. Gupta, red., "MQTT Version 3.1.1", OASIS Standard, 29 okt. 2014. [Online]. Tillgänglig: https://docs.oasis-open.org/mqtt/mqtt/v3.1.1/os/mqtt-v3.1.1-os.html. [Hämtad-datum kompletteras.]

[10] Projektgruppen, "Sprint 2 retro", interna projektanteckningar, `documentation/sprint2_retro.md`, införda genom commit `4371ddb`. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/blob/1c9772aa25ae24b0520c297d74982f0b6dcc69ad/documentation/sprint2_retro.md.

[11] Projektgruppen, "Definition of Done", intern projektöverenskommelse, `documentation/collaboration_rules/definition_of_done.md`. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/blob/1c9772aa25ae24b0520c297d74982f0b6dcc69ad/documentation/collaboration_rules/definition_of_done.md.

[12] Projektgruppen, "Scrum 50 mqtt firmware clean", pull request #22, integrerad 23 sep. 2026. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/pull/22.

[13] Projektgruppen, "Scrum 50 web UI", pull request #23, integrerad 23 sep. 2026. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/pull/23.

[14] Projektgruppen, "Run integrated car control with proportional wall correction", pull request #26, integrerad 29 sep. 2026. [Online]. Tillgänglig: https://github.com/Maher380/elektronik_projektet/pull/26.

[Daniel och Martin lägger till sina primärkällor. Den slutliga numreringen anpassas efter första förekomst i den färdiga texten.]

## Bilaga A Källkod och reproducerbarhet

Följande filer är centrala för rapportens mjukvarubeskrivning:

| Område | Filer relativt projektroten |
| --- | --- |
| Uppstart och val av billogik | `firmware/main/source/main.cpp`, `firmware/main/include/system/logic/target.h`, `firmware/main/Kconfig.projbuild` |
| Fabrik | `firmware/main/include/driver/factory/interface.h`, `firmware/main/source/driver/factory/esp32s3.cpp` |
| ADC och IR | `firmware/main/include/driver/adc/interface.h`, `firmware/main/source/driver/adc/esp32s3.cpp`, `firmware/main/source/driver/distance_sensor/gp2y0a21yk.cpp` |
| PWM och styrning | `firmware/main/source/driver/pwm/esp32s3.cpp`, `firmware/main/source/driver/servo/vagrant.cpp` |
| Körning och startmodul | `firmware/main/source/system/logic/vagrantLogic.cpp`, `firmware/main/include/driver/start_module/gpio.h` |
| Kommunikation och tillstånd | `firmware/main/source/system/communication/manager.cpp`, `firmware/main/source/system/runtime/control.cpp`, `firmware/main/source/driver/mqtt/esp32s3.cpp` |
| Webbpanel | `tools/mqtt-ui/server.mjs`, `tools/mqtt-ui/core.mjs`, `tools/mqtt-ui/transport.mjs`, `tools/mqtt-ui/public/` |
| Hosttester | `firmware/test/Makefile`, `firmware/test/navigation_regression.cpp` |

Den fastställda kodversionen anges i referens [1]. Byggmiljö, icke-hemliga konfigurationsuppgifter och kontrollernas avgränsningar dokumenteras även i det tillhörande `firmware/rapport/kodversion.md`. Källkoden kan hämtas från commitlänken även om projektets grenar senare ändras. Granskning av kommentarernas aktualitet och komplettering med gruppens övriga kodunderlag återstår inför slutlig inlämning.

## Bilaga B Testprotokoll

[Lägg endast till faktiska, daterade testprotokoll eller mätningar. Använd inte rekonstruerade resultat som om de vore sparade vid de ursprungliga proven. Eventuella nya tester märks med nytt datum och aktuell version.]
