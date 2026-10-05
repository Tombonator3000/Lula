# WET.EXE: analysegrunnlag for Lula-modernisering

Analysen er kjørt på den utpakkede originalfilen `original/app/WET.EXE` uten å starte eller endre spillet. Ghidra 12.1.4 lagret et faktisk analyseprosjekt i `local/ghidra/Lula.gpr` og eksporterte C-lignende analyse for 944 av 945 identifiserte funksjoner. Dette er et grunnlag for rekonstruksjon og kartlegging; det er ikke original kildekode eller et byggbart spill.

## Etterprøvbare egenskaper

| Felt | Målt verdi |
|---|---|
| SHA-256 | `8c223b528b912dd141dbe8db272b96b15279dab4bb2c31115b86d3c53842cea9` |
| Format | PE32, Intel i386, Windows GUI |
| Image base | `0x00400000` |
| Entry point | `0x00442cfc` |
| PE-tidsstempel | `1997-09-26T16:41:17+00:00` |
| Linker-versjon i PE-header | `2.18` |
| Seksjoner | 6 |
| Imports | 143 |
| PE-ressursblad | 119 |
| Ghidra-funksjoner | 945, hvorav 42 import-/andre thunk-funksjoner |
| Dekompilering | 944 fullførte eksportresultater, 1 ufullstendig |

PE-tidsstempel og linker-versjon er filmetadata, ikke bevis på opphav eller kompilator. En eksplisitt `WATCOM C/C++32 Run-Time system`-streng ved `0x00442d0f` gir et konkret Watcom-spor. Den opprinnelige interne kallkonvensjonen må fortsatt rekonstrueres fra instruksjonene. Ghidras nåværende standard Windows-kompilatorspesifikasjon er ikke en Watcom-ABI-modell. `extraout_ECX`, uavklarte registre og feilaktig utledede signaturer viser denne begrensningen i analyse-C.

Den ufullstendige funksjonen er `FUN_00407860` ved `0x00407860`: `Cannot properly adjust input varnodes`. Loggen har dessuten en pcode-advarsel ved `0x0044c46a` inne i `FUN_0044c3c3`. At en dekompilering er fullført, beviser ikke at funksjonen er korrekt, at alle kodeområder ble identifisert, eller at koden kan kompileres.

## Konkrete startpunkter

Adresser nedenfor er virtuelle adresser i den opprinnelige PE-filen. Funksjonsrollene er analysebetegnelser, ikke gjenfunne kildesymboler.

| Adresse | Observert oppførsel | Videre bruk |
|---|---|---|
| `0x00401010` | Oppstart; sender `0x280` og `0x1e0` til `FUN_00433f2e` | 640 × 480-baseline og startrekkefølge |
| `0x00433f2e` | `CreateWindowExA`, timer, oppstart av DirectDraw | Vindusmodus og presentasjonslag |
| `0x004340b3` | Kaller importert `DirectDrawCreate`, COM-kall og oppslag av `Sleep` | DirectDraw-objekt og kooperativ modus |
| `0x00434247` | Indirekte COM-kall ved vtable-offset `0x54` | Kandidat for skjermmodus; må typebestemmes manuelt |
| `0x00434a1c` | Undersøker pikselformat; feilmelding for 4/8 bit, aksepterer 16/24/32 | Fargeformat og moderne bufferkonvertering |
| `0x0043a9c8` | Initialiserer målbufferbredde/-høyde med 640/480 | Bufferdimensjoner, pitch og klipping |
| `0x00444816` | Sprite-blit med 16-bit-elementer; særgren for bredde 640 og stride `0x500` | Blit-/stride-avhengigheter ved oppløsningsendring |
| `0x00401ab6` | Refererer BACK.TGP, PERSO.TAP, EQUIP.TAP, PORTRAIT.TGP, SOUND.TAP, MUSIC.TAP og WET.DDF | Ressurspoolenes oppstart |
| `0x004425ac` | Wrapper rundt `CreateFileA` | Filåpning og sti-/modushåndtering |
| `0x004426d1` | Wrapper rundt `ReadFile` | Leserutiner og videre formatsporing |
| `0x004092b1` | Refererer `DATA\VIDEO\` og filåpning | Kandidat for videoinnlasting |

`resolution-candidates.tsv` inneholder 59 instruksjoner med konstant 640 og 25 med 480, fordelt på 42 identifiserte funksjoner. Listen er et søkegrunnlag: hver forekomst må knyttes til sin faktiske rolle. Den inneholder også andre vanlige skjermstørrelser; for eksempel kan 768 være en palettstørrelse fremfor en oppløsning.

Å endre vinduets 640 × 480-konstanter alene vil ikke løse bufferpitch, klipping, UI-koordinater, museområder, bakgrunnsbilder eller spriteformater. En praktisk første moderniseringsrute er å beholde de logiske originalkoordinatene og skalere presentasjonen i et eget lag. Større logisk oppløsning krever rekonstruksjon av alle relevante avhengigheter.

## Filer og gjentakbar kjøring

Fra repoets rot:

```sh
python3 tools/analyze_pe.py
./tools/decompile.sh --install
```

`--install` laster ned og hash-kontrollerer den pinnede Ghidra-/Java-verktøykjeden under `local/tools`, og kjører deretter analysen. Verktøykjeden er klargjort på denne maskinen; senere eksport kan kjøres med:

```sh
./tools/decompile.sh
```

Første kjøring importerer originalen og analyserer den. Senere kjøringer åpner samme prosjekt, kontrollerer originalens SHA-256 og eksporterer på nytt uten å gjenta full autoanalyse. Scriptet bevarer prosjektet. En annen originalhash krever en bevisst ny import i et separat prosjekt. Headless-kjøring bruker maksimalt to analysearbeidere og som standard 1 GiB Java-heap; `GHIDRA_HEADLESS_MAXMEM` kan justeres ved behov.

| Resultat | Plassering |
|---|---|
| PE-header, imports, ressursstruktur og hashes | `analysis/binary/pe-report.json` |
| Imports og adresser | `analysis/binary/imports.tsv` |
| Instruksjoner | `analysis/binary/disassembly.asm` |
| Rå strengkandidater | `analysis/binary/strings.tsv` |
| Full Ghidra analyse-C | `analysis/decompiled/WET.analysis.c` |
| Funksjoner og individuelle dekompileringsresultater | `analysis/decompiled/functions.tsv` |
| Symboler, funksjonskall, imports og strengreferanser | `analysis/decompiled/*-xrefs.tsv`, `callgraph.tsv`, `symbols.tsv` |
| Skjermstørrelseskandidater | `analysis/decompiled/resolution-candidates.tsv` |
| Kontrollert eksportstatus og kildehash | `analysis/decompiled/summary.json`, `focus-index.json` |
| Avgrensede analyseutdrag etter adresse | `analysis/decompiled/focus/` |
| Ghidra- og scriptlogger | `analysis/decompiled/ghidra.log`, `export.log` |
| Vedvarende redigerbart Ghidra-prosjekt | `local/ghidra/Lula.gpr` |

Rå strengskanning kan gi tilfeldige treff i maskinkode. `defined-strings.tsv` og `string-xrefs.tsv` gir Ghidras strengdefinisjoner og faktiske statiske referanser. Indirekte COM-kall og kodeområder uten gjenkjente funksjonsgrenser trenger manuell analyse. Windows-system-DLL-ene er ikke importert som komplette bibliotekbinærer; importnavnene og deres kallsteder er eksportert.

## Grensen mot rekompilering

Analyse-C bruker utledede typer, globale adresser, uavklarte registerverdier og indirekte funksjonspointere. Den skal ikke mates direkte til MinGW og omtales som rekompilert Lula. MinGW/DirectDraw-linktesten i `native/toolchain_probe.c` verifiserer at framtidige Windows-moduler kan bygges og linkes; den er ikke bevis på kompatibilitet med spillets Watcom-register-ABI eller interne hook-kall.

For en faktisk rekompilert erstatning må en avgrenset modul rekonstrueres med dokumenterte typer, ressursformater, kallkonvensjoner og observerbar oppførsel. Byggbarheten og samsvar med originalen må deretter testes separat. Denne leveransen klargjør verktøyene og gir sporbare startpunkter for dette arbeidet.

## Verktøykilder

Ghidra 12.1.4 kommer fra [NSA-prosjektets offisielle GitHub-release](https://github.com/NationalSecurityAgency/ghidra/releases/tag/Ghidra_12.1.4_build). ZIP-filens publiserte SHA-256 er kontrollert før utpakking. [Ghidras Getting Started](https://github.com/NationalSecurityAgency/ghidra/blob/Ghidra_12.1.4_build/GhidraDocs/GettingStarted.md) krever Java 21 JDK. Java 21.0.12.1 er hentet som Ubuntu `resolute-updates`-pakker via APT og pakket ut lokalt med hashene fra pakkemetadataene. Eksakte versjoner, kilde-URL-er og hashes finnes i `tools/ghidra/toolchain.json`. Ingen av disse verktøyene er installert systemglobalt.
