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
build/game/lula [--data DIR] [--save DIR] [--mods DIR] [--trace] [--verbose] -- [spillets argumenter]
```

`--mods DIR` (eller `LULA_MODS`) peker på en katalog med samme oppbygning som spillmappen. Filer der leses før `original/app`.

| Miljøvariabel | Virkning |
|---|---|
| `LULA_HEADLESS=1` | Ingen vindu eller lydenhet (SDL dummy-drivere). |
| `LULA_FRAMEDUMP=DIR` | Skriver viste bilder som PPM, ett per `LULA_FRAMEDUMP_MS` (standard 1000). Et bilde som vises én gang og blir stående (hjelpesider, meldingsbokser) skrives også. Bildene er 900 KB hver, så bruk kroken sparsomt. |
| `LULA_INPUT=FIL` | Skriptet input, én linje per hendelse: `<ms> move X Y`, `click X Y`, `rclick X Y`, `down X Y` / `up X Y` (venstre knapp holdes nede, så `move` imellom drar), `key NAVN`, `type TEKST` (resten av linja, med mellomrom og tegnsetting), `dump` (skriv hvert bilde fra nå), `quit`. Linjer som begynner med `#` er kommentarer. |
| `LULA_COVERAGE=FIL` | Skriver hvilke rekompilerte funksjoner som har kjørt (adresse og antall kall), hvert annet sekund og ved avslutning. Slå sammen med `tools/coverage_report.py`. |
| `LULA_MSGBOX_AUTO=1` | Meldingsbokser svarer med standardknappen med en gang (for hodeløse tester). |
| `LULA_AUDIODUMP=FIL` | Skriver den miksede lyden til en WAV-fil. |
| `LULA_SCALE=N` | Startstørrelse på vinduet (standard 2, altså 1280x960). |
| `LULA_SMOOTH=1` | Lineær skalering i stedet for skarpe piksler. |
| `LULA_TEXT_AA=0` | Tekst uten kantutjevning, slik Windows 95 tegnet den. |
| `LULA_LOG=0..3` | Loggnivå. |
| `LULA_CLOCK=1997-01-01T08:00:00` | Veggklokka (`GetLocalTime`) starter på dette lokale tidspunktet og går deretter i sanntid. Spillet seeder `rand()` fra den én gang ved oppstart, så en fast verdi gjør de tilfeldige hendelsene like fra kjøring til kjøring. `tools/scenarios.py` setter den for hver kjøring. |

## Grafikkutskifting

Endrede ressursfiler legges i en mod-katalog og brukes uten å røre originalen. Eksempel med hovedmenyens bilde, som er post 71 (640x480) i `DATA/DIALOG/DIA_BACK.TGP`:

```sh
mkdir -p build/mod-work local/mods/DATA/DIALOG
python3 tools/assets.py unpack original/app/DATA/DIALOG/DIA_BACK.TGP build/mod-work/dia_back
python3 tools/assets.py export-tbf build/mod-work/dia_back/0071.tbf build/mod-work/menu.ppm
# rediger menu.ppm i et bildeprogram (behold 640x480), lagre som menu-ny.ppm
python3 tools/assets.py import-tbf build/mod-work/menu-ny.ppm build/mod-work/dia_back/0071.tbf build/mod-work/0071-ny.tbf
cp build/mod-work/0071-ny.tbf build/mod-work/dia_back/0071.tbf
python3 tools/assets.py pack build/mod-work/dia_back local/mods/DATA/DIALOG/DIA_BACK.TGP
build/game/lula --mods local/mods -- -novideo
```

Testen `tests/test_recompiled_game.py` gjør det samme automatisk med en grønn firkant og kontrollerer at nøyaktig de 4000 pikslene vises i menyen. Bildene må fortsatt ha samme størrelse som originalen; større grafikk krever endringer i spillkoden.

## Verifisert

- Bygg med CMake, GCC 13 og SDL 2.30 på Ubuntu 24.04.
- Hodeløs kjøring med `-novideo` følger samme rekkefølge av Win32- og DirectX-kall som originalen under Wine fram til hovedmenyen.
- Hovedmenyen: alle 307 200 piksler sammenlignet i RGB565 mot Wine-referansen. Avvik finnes bare i bokstavene på de fem knappene. Tekstens venstre- og høyrekant er identisk med Wine på alle fem knappene, siden runtimen plasserer tegn slik GDI gjør (hele piksler per tegn, ingen kerning). De 2728 pikslene som fortsatt avviker, skyldes at Wine tegner med fargede ClearType-kanter fra vertsmaskinen. Spillet ber om fonten "System Small", som ikke finnes; runtimen bruker Liberation Sans Bold, som har samme tegnbredder som Arial.
- Første spillskjerm etter "New game": bare tekst med vertsfont og klokkeslettet avviker fra Wine-referansen. Spillklokken går like fort som i originalen (målt 4,1 mot 4,3 spillminutter per sekund over samme tidsrom, innenfor måleusikkerheten).
- Grafikkutskifting via `--mods` (se over).
- Automatiske ende-til-ende-tester: `python3 -m unittest tests.test_recompiled_game -v`.
- Originalfilene er uendret (`python3 tools/project.py verify`).

- Rekompilatoren: hver instruksjonsform i WET.EXE er sammenlignet med emulatoren unicorn, til sammen 247 380 tilstander uten avvik (`docs/recomp/lifter-verification.md`, `python3 tests/recomp/unicorn_diff.py`).
- Dialogbokser fra ressursene (13 maler, 18 dialogprosedyrer), popupmenyen "Set Digital Output" og MessageBox. Lagring fra F2-menyen og lasting fra hovedmenyen er dekket av en ende-til-ende-test.
- Utforskende kjøring med tilfeldige klikk og taster i 2 x 4 minutter uten feilfeller eller advarsler.
- Skriptet gjennomspilling av alle tre trinn (kontor, bank, eiendomsmeglere, byrå, bydeler, dag-, måneds- og årsskifte, flyplass og sluttscenen) i rundt 400 kjøringer uten feilfeller. 955 av 1261 levende funksjoner har kjørt (75,7 %). Se [utforskningen](recomp/exploration.md).

## Ikke verifisert ennå

- Deler av spillflyten: salg av ferdige filmer og noen sene hendelser er ikke nådd (se [utforskningen](recomp/exploration.md)).
- Fonten: spillet ber om "System Small", som ikke finnes. Med Arial-bredder og GDI-plassering får 37 av 219 enlinjes DDF-etiketter tekst som er bredere enn rektangelet, slik at teksten brytes eller klippes, akkurat som under Wine. Om ekte Windows 95 ga en smalere font, vet vi ikke uten et skjermbilde derfra.
- Lyd på en ekte lydenhet. Miksen er sammenlignet med originalens samples, men ikke lyttet på.
- Video (`.CUT`-filer) og MCI er bevisst satt til side. Med `-novideo` hopper spillet over videoene.
- Windows-bygg av den samme koden.

## Videre rekonstruksjon

Den genererte koden er korrekt, men ikke lesbar som originalkilde. Funksjoner kan erstattes én og én med håndskrevet C i `src/reconstructed/`:

```c
#include "reconstructed.h"

RT_RECONSTRUCTED(0x00442be4)
uint32_t f_00442be4(Cpu *c)
{
    ...                      /* argumenter i EAX, EDX, EBX, ECX (Watcom) */
    c->eax = resultat;
    return rt_return(c);     /* som 'ret' */
}
```

Rekompilatoren ser markeringen, slutter å generere sin egen `f_00442be4` og beholder den som `lifted_00442be4` for sammenligning. CMake regenererer automatisk når filene endres. En erstatning må etterlate registre, flagg som leses senere, stakk og minne slik originalen gjør, fordi kallerne er generert kode. Rekompilatoren advarer hvis funksjonen inngår i flaggflyt på tvers av kall.

Første eksempel er Watcoms `memcpy` (0x442be4) i `src/reconstructed/watcom_crt.c`. Alle ende-til-ende-testene går grønt med den. Codex har rekonstruert fem ressursfunksjoner (NGS-oppslag og -lesing, TBF-lesing, RLE-dekoderen og signaturkontrollen) i `src/reconstructed/resource_*.c`, kontrollert med `lula-fncheck` og 6395 filtilfeller fra spilldataene ([beskrivelse](reconstruction/resource-readers.md)).

`build/game/lula-fncheck` kontrollerer hver erstatning mot den genererte versjonen. Den bygger tilfeldige maskintilstander ut fra en profil i kildekoden, for eksempel `RT_CHECK(0x00442be4, "eax:ptr edx:ptr ebx:size(0,600)")`, kjører begge versjonene og sammenligner registre, flaggene kallerne leser, x87-tilstand og minne (skrapebuffere, stakk over stakkpekeren og hele programbildet). memcpy består 3000 av 3000 tilstander. Samme mekanisme er veien til lesbar kildekode for spillogikken, ny renderer og høyere intern oppløsning.
