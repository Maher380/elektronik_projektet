# Definition of Done

Det här dokumentet beskriver när en task eller user story räknas som färdig.

## Statusmarkeringar

- `[ ]` Kravet ingår i Definition of Done.
- `[x]` Kravet är identifierat, men ingår inte i Definition of Done ännu.

## Task-nivå: kod

Gäller kodtasks, till exempel features, buggar och refaktoreringar.

- [ ] Koden är implementerad och uppfyller taskens beskrivning.
- [ ] Koden kompilerar och kör utan fel.
- [x] Enhetstester finns och passerar för tasken.
- [ ] Det finns rimlig felhantering för uppenbara fel och specialfall.
- [ ] Koden är kommenterad där logiken inte är självförklarande.
- [x] Koden följer gruppens namn- och formateringsstandard.
- [ ] Ändringen är pushad via en pull request med ett begripligt commit-meddelande.
- [ ] Koden är granskad av minst en annan gruppmedlem.
- [ ] Pull requesten är godkänd och mergad till `main` av ticketansvarig.
- [ ] Tasken är markerad som klar på boarden.

## Task-nivå: hårdvarudesign

- [ ] Designen är dokumenterad i Git-repot.
- [x] Dokumentationen följer gruppens dokumentationsstruktur.
- [ ] Ändringen är pushad via en pull request med ett begripligt commit-meddelande.
- [ ] Designen är granskad och förstådd av minst en annan gruppmedlem.
- [ ] Pull requesten är godkänd och mergad till `main` av ticketansvarig.
- [ ] Tasken är markerad som klar på boarden.

## User story-nivå

- [ ] Alla tasks kopplade till storyn är klara, granskade, testade och mergade.
- [x] Funktionen uppfyller storyns acceptanskriterier.
- [ ] Alla enhetstester passerar tillsammans efter integrering.
- [ ] Hela programmet bygger och kör med all storyfunktionalitet på plats.
- [ ] Funktionen är testad som helhet, inklusive integration och användarflöde.
- [ ] Nödvändig dokumentation är uppdaterad, till exempel README och användarinstruktioner.
- [ ] Inga kända allvarliga buggar kvarstår.
- [ ] Gruppen är överens om att storyn är färdig.