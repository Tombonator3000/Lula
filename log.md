# Arbeidslogg

Kronologisk logg over alt som er gjort i repoet. Nyeste oppføring nederst. Tider i UTC.

## 2026-10-05 16:04 UTC: ny økt (Claude Code, gren `claude/eager-ride-xckkd4`)

- Oppdrag fra brukeren: reverse engineering, grafikkutskifting og videre rekonstruksjon. Hele spillet skal rekompileres og kunne kjøres på Linux, helst rett fra repoet.
- Leste `AGENTS.md`, `README.md` og alle filer under `docs/`.
- Prøvde å åpne ChatGPT-lenken brukeren delte (`chatgpt.com/s/cx_...`). Siden svarer med 403 fra Cloudflare både via WebFetch, curl og headless Chromium, så innholdet er ikke lest. Commit `70aae36` ser ut til å være resultatet av den økten, så jeg bygger videre på det som ligger i repoet.
- `git lfs pull`: 698 MB originaldata hentet.
- `python3 tools/project.py verify`: PASS, 144 filer stemmer med SHA-256-manifestet.
- `python3 -m unittest discover -s tests`: 20 av 20 tester OK.
- Fant at containeren har gcc 13, clang, cmake, ffmpeg og Xvfb. SDL2, Wine, MinGW og capstone mangler og blir installert.

## 2026-10-05 16:20-16:55 UTC: statisk rekompilering og runtime

- Instruksjonstelling: koden bruker 118 ulike x86-mnemonikker og 249 operandformer. Det gjør full statisk rekompilering realistisk.
- Skrev `tools/recomp/`: PE-laster (`pe.py`), kodeoppdagelse med hopptabeller og pekere fra relokeringstabellen (`cfg.py`), interprosedural flagg-liveness (`flags.py`) og x86-til-C-oversetter (`lift.py`). Resultat: 1415 funksjoner, ca. 78 100 instruksjoner, 85 hopptabeller, ingen uløste indirekte hopp.
- Startet en bakgrunnsanalyse (6 agenter og en kritiker) som skriver spesifikasjoner for alle Win32/DirectX-kall til `docs/recomp/specs/`. Video, AVI, OLE32 og MCI holdes utenfor inntil brukeren har avklart det.
- Startet en agent som verifiserer lifteren instruksjon for instruksjon mot unicorn (`tests/recomp/`). Den har også skrevet `src/runtime/rt_fpu.c`.
- Referansekjøring under Wine (egen agent): originalen når hovedmenyen etter ca. 8,6 s med `-novideo` når cnc-ddraw bruker `renderer=gdi`. Skjermbilder, API-sekvens og trace ligger lokalt i `build/wine-ref/`.
- Skrev runtime i `src/runtime/`: 4 GiB gjesteminne, PE-laster med SHA-256-kontroll, dispatch mellom gjest og vert, tråder under en global lås, filsystem-overlay (originalen er skrivebeskyttet, skriving går til `local/save/`) og KERNEL32, USER32, GDI32 (tekst via stb_truetype og Liberation Sans), WINMM, DDRAW, DSOUND, OLE32 og COMDLG32 på SDL2.
- Svarte Codex på koordineringsspørsmålet og foreslo at Codex tar TAF-redigering (animasjonsgrafikk).
- La til `CMakeLists.txt`. Konfigureringen kjører rekompilatoren og bygger `build/game/lula`.
- Første kjøring: spillet når hovedmenyen, men med forskjøvet bilde, svarte prikker og uten knapper. Årsak: capstone dropper F2-prefikset på `f2 a5` (`repnz movsd` i Watcoms memcpy ved 0x442be4). Rettet i `cfg.py` ved å lese prefiksbytene direkte. Fem steder var berørt.
- Etter rettingen: hovedmenyen er identisk med Wine-referansen i alle piksler utenfor bokstavene på de fem knappene (3344 av 307 200 piksler avviker, alle i teksten).
- `python3 tools/project.py verify`: PASS etter arbeidet.

## 2026-10-05 17:00 UTC: første spillskjerm

- Committet og pushet milepælen (`643b147`) til `claude/eager-ride-xckkd4`.
- Skriptet klikk på "New game" (505,88) i den rekompilerte versjonen. Spillet går til byoversikten. Mot Wine-bildet `after_newgame_1.png` avviker 423 av 307 200 piksler, alle i teksten "F1-Help" (font) og i klokkeslettet på statuslinjen (spilltiden går i sanntid).

## 2026-10-05 17:10 UTC: timer, tester og grafikkutskifting

- Spesifikasjonene for KERNEL32 og USER32/GDI32 er ferdige (`docs/recomp/specs/`). Viktigste funn: spillogikken går på WM_TIMER (60 ms), lagring og lasting skjer i ekte Win32-dialoger, og museklikk må komme som WM_LBUTTONDBLCLK ved dobbeltklikk.
- USER32: WM_TIMER følger nå Windows-semantikken (klar ved hver periode, forankret til SetTimer, tapte perioder slås sammen), dobbeltklikk syntetiseres, WM_ACTIVATEAPP sendes synkront, lukking av vinduet avslutter programmet. Spillklokken går nå like fort som i originalen.
- KERNEL32: pseudohåndtak for stdout/stderr slik at Watcom-runtimens feilmeldinger kommer ut.
- Startet en agent som lager dialogbehandleren (lagring, lasting, bank, lister, tekstfelt) og popupmenyen. Den eier `user32.c`, `gdi32.c`, `res.c` og nye `dialog.c` til den er ferdig.
- La til `--mods DIR`: en katalog som leses før `original/app`. Testet med endret hovedmenybilde (post 71 i `DIA_BACK.TGP`): de 4000 grønne pikslene vises nøyaktig der de skal.
- Ny test `tests/test_recompiled_game.py` (3 tester: hovedmeny mot originalens hash, New game, grafikk via mods). Alle går grønt.
- Lyd: `LULA_AUDIODUMP=fil.wav` skriver den miksede lyden. I menyen er utgangen nøyaktig `sound.tap` post 9 (22050 Hz, 8 bit) spilt med -10 dB, sample for sample. Lagt inn som fjerde ende-til-ende-test.
- Rekonstruksjon: funksjoner merket `RT_RECONSTRUCTED(0x...)` i `src/reconstructed/` erstatter de genererte (som beholdes som `lifted_...`). Første eksempel er Watcoms memcpy (0x442be4). Alle fire ende-til-ende-tester går grønt med den.
- Nytt verktøy `lula-fncheck` (CMake-mål): kjører hver rekonstruerte funksjon og den genererte originalen på tilfeldige tilstander og sammenligner registre, levende flagg, x87 og minne. memcpy består 3000 av 3000. CMake bruker nå et objektbibliotek (`lula_core`) som deles av spillet og verktøyene.
- Den globale låsen kan nå overleveres ved retur fra hvert Win32-kall når en annen tråd venter, slik at 2 ms-timeren for lyd ikke sultes i travle PeekMessage-løkker. Alle 5 ende-til-ende-tester går grønt.
- DirectX-spesifikasjonen er ferdig. Viktigst: hovedtråden spinner på flagg som lydtimeren (2 ms, egen tråd) nullstiller (0x437451, 0x4376b8, 0x438cbb). Generert kode kaller nå `RT_POLL()` ved hvert bakoverhopp (3016 steder), så en ventende tråd slipper til. FlipToGDISurface bytter tilbake til den opprinnelige primærbufferen og viser den, som spesifikasjonen krever.
- Verifiseringsagenten fant og rettet en feil i flaggfusjonen: flagg som leses ved hoppmål inne i en sammensmeltet `cmp`/`jcc`-kjede ble ikke alltid materialisert. `rt_fprem` gir nå også kvotientbitene i C0/C3/C1.
- Testet i et eget git-worktree (dialogagentens halvferdige `user32.c` lenket ikke i hovedtreet): alle 5 ende-til-ende-tester grønne.
- Rekompilatoren gir nå en tydelig melding hvis `analysis/decompiled/functions.tsv` bare er en Git LFS-peker.

## 2026-10-05 17:40 UTC: lifteren verifisert mot unicorn

- Verifiseringsagenten er ferdig: 65 446 instruksjoner i 245 grupper (185 fullt testet, 60 med 60 utvalgte forekomster inkludert alle kodinger), alle 6 709 sammensmeltede flaggkjeder og 43 x87-sekvenser. 247 380 tilstander sammenlignet med unicorn, 0 feil. 49 bevisst innførte feil ble alle fanget.
- Rettet: `aam` og `les` var feilfeller (finnes i Watcoms tall-til-tekst og printf), `fprem` ga ikke kvotientbitene, og to latente feil i flaggfusjonen. Rekompilatoren melder nå 0 ikke-støttede instruksjonstyper.
- Kjent avvik: x87 holdes i `double`, men Watcom setter 64-bits presisjon. Agenten har fått i oppgave å gjøre x87 80-bit-eksakt med `long double` og sammenligne eksakt mot unicorn.
- Kjør på nytt: `python3 tests/recomp/unicorn_diff.py` (ca. 3 min) eller `--quick`.
- WINMM/timing-spesifikasjonen er ferdig. Etter den: timertråden (lydmotoren, 2 ms) kjører nå hver tick ferdig uten å gi fra seg den globale låsen, slik en tidskritisk timertråd på en Win9x-maskin med én CPU gjorde. Spillets volumglidebryter (`waveOutSetVolume`) virker nå som master-volum på vår egen miks, aldri på systemmikseren. Timertrådens stakk ligger over hovedtrådens stakkgrense, som Watcoms stakksjekk krever (gitt av rekkefølgen trådene lages i).

## 2026-10-05 18:00 UTC: dialoger, lagring og lasting

- Dialogagenten er ferdig (`src/runtime/win32/dialog.c`, ca. 2800 linjer): modale Win32-dialoger fra EXE-ressursene med STATIC, EDIT, LISTBOX og BUTTON, hele WM_CTLCOLOR-protokollen, nestede dialoger, popupmenyen "Set Digital Output" og ekte MessageBox. Sammenlignet med Wine-skjermbilder av originalens dialoger.
- Lagring og lasting virker: lagret fra F2-menyen til `DATA/SAVE/SAVEGAME.  4` i lagringskatalogen og lastet inn igjen etter omstart.
- Rettet krasj ved `ExitProcess`: SDL ble avsluttet fra spilltråden. Nå ber spilltråden hovedtråden om å avslutte.
- Inputskript kan nå bruke F1-F12 og `type ORD`. Ny ende-til-ende-test lagrer via F2 og laster fra hovedmenyen. Alle 6 testene grønne.
- Ressurs- og spillkartspesifikasjonen (`docs/recomp/specs/resources-and-game-map.md`) er ferdig.
