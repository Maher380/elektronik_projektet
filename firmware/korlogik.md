# Enkel körlogik för Vagrant

Koden finns i `main/source/system/logic/vagrantLogic.cpp`.
Test mode och competition mode använder samma körlogik. Test mode startas via
MQTT och kräver anslutning/heartbeat; competition mode startas på D10/GPIO21
utan WiFi/MQTT. Befintliga körparametrar och credentials är oförändrade.

## Avstånd och vägval

Varje giltigt sensorvärde begränsas till intervallet 0 till
`reaction_distance_cm` innan det används för vägval, styrning eller stopp.
Alla värden över gränsen behandlas alltså exakt som gränsen. Råvärden visas
bara i telemetri och får inte avgöra vilken väg som är friast.

1. Är framåtavståndet störst eller lika stort som båda sidoavstånden kör bilen
   framåt med små väggkorrigeringar. Vid tre lika kapade avstånd föredras rakt fram.
2. Annars svänger bilen mot sidan med störst kapat avstånd.
3. Vid lika sidoavstånd behålls senaste sidovalet; saknas ett sådant väljs vänster.
4. Sidostyrningen är proportionell mot sidans avståndsfördel över fronten:
   `90 * clamp((max(vänster, höger) - front) / max(1, reaction - stop), 0, 1)` grader.
   Små skillnader ger små utslag; en fördel på hela reaction-minus-stop ger 90 grader.
   Vanliga styrändringar bromsar inte bilen och ändras mjukt, högst 480 grader/s.

## Små proportionella väggkorrigeringar

När framåt är en minst lika fri väg används sidornas närhet till väggar inom
30 cm. En närmare vänstervägg korrigerar åt höger, och omvänt.
Felet filtreras med 60 ms tidskonstant, skillnader inom 3 cm ignoreras,
P-faktorn är 2,4 och utslaget begränsas till 45 grader.
När närmaste väggen byter sida återställs filtret till det nya felet direkt.
När båda väggarna ligger utanför korrigeringsområdet försvinner korrigeringen.
Ingen integral eller derivata används. Målet är att undvika väggen, inte krama den.

## Fart och kickstart

Första automatiska framåtstarten efter Start använder duty 1,0 i 100 ms,
sedan inställd `drive_duty`. Kort paus, backning och vanliga styrändringar
startar inte en ny kick; det krävs ett nytt Start-tillstånd för att återaktivera den.
Backpulsen använder duty 1,0. Manuella tester behåller vald duty.
Kickstarten får aldrig överstyra Stop, ogiltiga sensorer eller startvillkor.

## Kort backning när alla riktningar är blockerade

När samtliga kapade avstånd är högst `stop_distance_cm` bromsar bilen 80 ms och förbereder
backstyrningen med minst 100 ms motorvila. Backningen startar när styrkommandot
är färdigpositionerat och stabiliserat 80 ms. Den valda sidan låses under pulsen,
och 60 graders motsatt styrutslag används vid första backningen.
Om alla vägar fortfarande är blockerade efter backmanövern används fullt
90 graders motsatt styrutslag vid nästa backpuls, efter samma motorvila och
styrförberedelse. Fri väg eller ett nytt Start återställer till 60 grader.
Detta är logiska kommandovinklar, inte hjulens faktiska vinkel.

Bilen backar i 300 ms. Sedan pausar motorn minst 100 ms och styrningen
positioneras för det nya vägvalet från aktuella, kapade sensormätningar.
Efter motorvilan och stabiliserad styrning återupptas vanlig körning direkt
enligt det senaste vägvalet. Ingen tidsstyrd framåtsväng låser bilen åt ett
gammalt håll. Det finns inget 55 cm-krav och ingen permanent tvåförsökslåsning.

Stoppet släpps när den friaste riktningen är minst stop plus det minsta av 3 cm och halva skillnaden
mellan reaction och stop. Med stop=45 och reaction=52 blir gränsen 48 cm.
Är samtliga riktningar fortfarande blockerade efter pulsen görs en ny kort backmanöver.
Det finns ingen gräns för antalet pulser; Stop måste kunna användas direkt.

Enligt vald policy stoppar ett kort frontavstånd alltså inte ensamt bilen om
en sida är friare. Med stop=40, reaction=52 och mätningar 94302,8 / 38,3 / 15,7
används 52 / 38,3 / 15,7 för vägval: vänster väljs när bilen är armerad.
Detta är inte en kollisionsgaranti; en sidoöppning garanterar inte fri svängradie.
Extremt höga råavstånd med mycket låg ADC måste också kontrolleras på hårdvaran:
kapningen bekräftar inte att sensorn verkligen fungerar eller att vägen är fri.

## Säkerhet och verifiering

Ogiltiga sensorvärden, Stop, förlorad MQTT-auktorisering eller startmodulens
stopptillstånd avbryter automatisk körning. Fel vid motor/PWM/servo-skrivning
stänger motorn och kräver omstart. Frekventa IR- och NAV-loggar är borttagna;
viktiga felmeddelanden finns kvar och testläget har fortsatt MQTT-telemetri.

Ingen bakåtsensor eller rörelseåterkoppling används. Backpulser kan därför inte
bekräfta fri väg bakom bilen och kan upprepas om fronten förblir blockerad.
Prova under uppsikt med möjlighet att stoppa bilen. Kontrollera att logisk
vänster/höger verkligen motsvarar fysisk riktning innan körning på banan.

`make -C test` och `make -C test navigation` kör enhets- respektive
navigationssimuleringar. Simulering är inte ett fysiskt körprov.
