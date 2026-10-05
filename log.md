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
