# Statisk rekompilering av WET.EXE

Denne siden beskriver hvordan spillet bygges som et native program, hva som er verifisert og hva som gjenstår.

## Prinsipp

`tools/recomp` leser `original/app/WET.EXE`, finner all kode og oversetter hver x86-instruksjon til C. Den genererte koden arbeider på en emulert registerfil (`Cpu` i `src/runtime/rt_cpu.h`) og et flatt 4 GiB gjesteminne der gjesteadresse A ligger på `g_mem + A`. PE-filen lastes inn på sin opprinnelige adresse (0x400000), så alle konstanter og pekere i koden stemmer uten omskriving.

Fordi registre, flagg og stakk bevares nøyaktig, trenger ikke Watcom-kallkonvensjonen eller typene å være kjent på forhånd. Det er hovedgrunnen til at hele spillet kan bygges nå, mens den lesbare rekonstruksjonen kan skje gradvis.

Hver gjestefunksjon blir

```c
uint32_t f_XXXXXXXX(Cpu *c)
```

og returnerer returadressen som `ret` faktisk tok av stakken. Kallstedet sammenligner med sin egen returadresse. Et avvik betyr at koden har returnert et annet sted (en pushet fortsettelse eller longjmp-lignende kode), og adressen sendes da oppover til kallstedet som eier den. Det tilsvarer det prosessoren gjør.

## Rekompilatoren

| Fil | Oppgave |
|---|---|
| `tools/recomp/pe.py` | Laster PE-filen slik Windows-lasteren gjør, leser relokeringer og importer. |
| `tools/recomp/cfg.py` | Kodeoppdagelse. Startpunkter er PE-entry, Ghidras funksjonsliste, direkte kallmål og alle relokerte pekere inn i koden som ikke tilhører en hopptabell (vtabeller, callbacks). Hopptabeller avgrenses med `cmp`-grensen og sammenhengende relokeringer. |
| `tools/recomp/flags.py` | Interprosedural liveness for CF, PF, AF, ZF, SF og OF. Bare flagg som faktisk leses senere, regnes ut. |
| `tools/recomp/lift.py` | Oversetter instruksjonene til C. `cmp`/`test` etterfulgt av betingede hopp blir vanlige C-sammenligninger. |
| `tools/recomp/config.json` | Manuelle tillegg: ekstra startpunkter og adresser som ser ut som kode, men er data. |

Kjør den alene med `python3 -m tools.recomp --out build/recomp/gen`. `report.json` i utkatalogen viser antall funksjoner, ikke-støttede instruksjoner og advarsler.

Tall for den nåværende versjonen: 1415 funksjoner, ca. 78 100 unike instruksjoner, 85 hopptabeller, ingen uløste indirekte hopp. To instruksjonstyper (`aam`, `les`) finnes bare i død DOS-kode og blir til feilfeller (`rt_trap`) hvis de noen gang nås.

## Runtimen

| Fil | Oppgave |
|---|---|
| `rt_mem.c` | Gjesteminne, VirtualAlloc-arena og en liten heap for objekter runtimen lager. |
| `rt_loader.c` | Kontrollerer SHA-256 mot filen koden ble generert fra, kartlegger seksjonene og kobler importtabellen til vertsfunksjoner. |
| `rt_core.c` | Dispatch for indirekte kall og hopp, kall fra vert til gjest (WndProc, timere, tråder), feilutskrift og håndtak. |
| `rt_thread.c` | Gjestetråder. All gjestekode kjører under én global lås, som slippes i blokkerende kall. |
| `rt_vfs.c` | Spillet tror det ligger i `C:\LULA`. Lesing går først mot lagringskatalogen og så mot `original/app`. Skriving går alltid til lagringskatalogen. Oppslag skiller ikke mellom store og små bokstaver. |
| `win32/*.c` | KERNEL32, USER32, GDI32, WINMM, DDRAW, DSOUND, OLE32 og COMDLG32. |
| `platform_sdl.c` | Vindu, skalering, tastatur og mus, lyd og testkroker. |

DirectDraw-flatene er 16-bit RGB565 i gjesteminnet, så spillets egne blittere virker uendret. Det som havner på primærflaten, vises i SDL-vinduet med bevart sideforhold.

## Kommandolinje og testkroker

```sh
build/game/lula [--data DIR] [--save DIR] [--trace] [--verbose] -- [spillets argumenter]
```

| Miljøvariabel | Virkning |
|---|---|
| `LULA_HEADLESS=1` | Ingen vindu eller lydenhet (SDL dummy-drivere). |
| `LULA_FRAMEDUMP=DIR` | Skriver viste bilder som PPM, ett per `LULA_FRAMEDUMP_MS` (standard 1000). |
| `LULA_INPUT=FIL` | Skriptet input, én linje per hendelse: `<ms> move X Y`, `click X Y`, `rclick X Y`, `key NAVN`, `quit`. |
| `LULA_SCALE=N` | Startstørrelse på vinduet (standard 2, altså 1280x960). |
| `LULA_SMOOTH=1` | Lineær skalering i stedet for skarpe piksler. |
| `LULA_TEXT_AA=0` | Tekst uten kantutjevning, slik Windows 95 tegnet den. |
| `LULA_LOG=0..3` | Loggnivå. |

## Verifisert

- Bygg med CMake, GCC 13 og SDL 2.30 på Ubuntu 24.04.
- Hodeløs kjøring med `-novideo` følger samme rekkefølge av Win32- og DirectX-kall som originalen under Wine fram til hovedmenyen.
- Hovedmenyen: alle 307 200 piksler sammenlignet i RGB565 mot Wine-referansen. Avvikene (3344 piksler) ligger bare i bokstavene på de fem knappene. Spillet ber om fonten "System Small", som ikke finnes. Runtimen bruker Liberation Sans Bold, Wine valgte en annen erstatning.
- Originalfilene er uendret (`python3 tools/project.py verify`).

## Ikke verifisert ennå

- Spillflyt etter hovedmenyen, lagring og lasting.
- Lyd på en ekte lydenhet. DirectSound-kallene følger originalen, men lyden er ikke lyttet på.
- Video (`.CUT`-filer) og MCI er bevisst satt til side. Med `-novideo` hopper spillet over videoene.
- Dialogbokser og menyer fra ressursene.
- Windows-bygg av den samme koden.

## Videre rekonstruksjon

Den genererte koden er korrekt, men ikke lesbar som originalkilde. Målet er å erstatte funksjoner én og én med håndskrevet C i `src/reconstructed/`, med den rekompilerte versjonen som fasit. Samme mekanisme åpner for ny renderer og høyere intern oppløsning senere.
