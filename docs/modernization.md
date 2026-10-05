# Veien til endret grafikk og intern oppløsning

Klargjøringen gir tre konkrete spor, med forskjellige krav til verifikasjon.

## 1. Grafikkbytte i dagens spill

TBF-bilder kan eksporteres til redigerbar PPM og importeres tilbake med original dimensjon. NGS-arkivene (TGP/TAP/DDF) har en dokumentert indeks og kan gjenoppbygges etter payload-endringer. Dette er den første mulige veien fra grafikkredigering til dagens spillformat.

Neste steg er én redigert, nøytral START-knapp: legg den gjenoppbygde TBF-filen i en separat kjøringskopi, start spillet og kontroller visning, plassering og museklikk. Container-/pikseltester alene bekrefter ikke at spillet godtar en endring. Originalene og eksisterende lagringsfiler beholdes.

TAF-animasjoner kan deles og settes byte-identisk sammen. Redigering av animasjonsrammer er foreløpig avvist av verktøyet; transparens, rammeankere og timing må kartlegges før det gjøres.

## 2. Større skjermpresentasjon

Pakken har allerede cnc-ddraw. `hd1080` konfigurerer en separat kopi til 1920×1080, vindusmodus og bevart sideforhold. Dette er visningsskalering av dagens 640×480-spilleflate. Ingen ny grafikk eller større logisk koordinatflate er produsert.

Før profilen kalles spillbar, kontroller oppstart, meny, museposisjon i alle hjørner, video, lyd, Alt+Enter, lagring og lasting. Wine var ikke tilgjengelig ved klargjøringen. Spillets egen README dokumenterer `-novideo` som et feilsøkingsvalg.

## 3. Ny renderer og byggbar spillkode

WET.EXE er 32-bits x86/Windows og importerer DirectDraw/DirectSound. Ghidra har gjenfunnet Watcom-runtime-spor og konkrete startpunkter for vindu, rendering og ressursinnlasting. Se binærrapporten og C-eksporten.

Det finnes flere 640-breddegrenser i renderer-/blittrutiner. Å endre bare oppstartsoppløsningen er derfor utilstrekkelig. Rekkefølgen for rekonstruksjon er:

1. Avgrens innlasting av en TBF/NGS-ressurs og gjengi den i en ny, testbar modul.
2. Dokumenter 640×480-koordinater, stride, klipping, palett/RGB565, museoversetting og videooverlegg.
3. Bruk gjenoppbygde moduler til skalering eller en ny renderer mens spilldata/lagring bevares.
4. Rekonstruer mer spillkode og sammenlign observerbar atferd med dagens spill før hele WET.EXE erstattes.

MinGW-byggingen beviser at nye x86 Windows-moduler kan kompileres og lenkes mot DirectDraw. Den beviser ikke intern ABI-kompatibilitet med Watcom-koden. De interne kallkonvensjonene må bekreftes før hooks eller delvis binærutskifting.

## Primærkilder og kandidater

- [Resources Game Viewer sin Lula-plugin](https://game-viewer.org/plugin.php?id=45) og [formatkode](https://game-viewer.org/plugins_files/g_lula.py): nyttige referanser for Lula-ressursformatene; repoets verktøy validerer dem mot de faktiske filene.
- [ScummVMs Chewy-ressurskode](https://github.com/scummvm/scummvm/blob/master/engines/chewy/resource.cpp): bekrefter beslektet NGS-struktur. Dette betyr ikke at ScummVM støtter Lula.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra): binæranalyse og dekompilering.
- [MinGW-w64](https://www.mingw-w64.org/): nye Windows x86-moduler.
- [cnc-ddraw](https://github.com/FunkyFr3sh/cnc-ddraw): wrapperen i pakken; faktisk Lula-runtime må testes.
- [recomp-kit](https://github.com/veritr1x/recomp-kit): mulig framtidig statisk x86-rekompilering. Lula-støtte er ikke verifisert eller integrert.
- [Milan Kovacs Atari-omskriving](https://milan.kovac.cc/atari/wet/): historisk referanse som ikke gir PC-spillets originale kildekode.
