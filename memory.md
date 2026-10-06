# Arbeidsminne

Kort oppsummering av det som er viktig å huske mellom økter. Detaljer står i `log.md` og under `docs/`.

## Mål

Hele WET.EXE (Lula - The Sexy Empire, 1997) skal rekompileres og kjøre native på Linux rett fra repoet, med mulighet for grafikkutskifting og gradvis rekonstruksjon til lesbar C.

## Valgt arkitektur

- **Statisk rekompilering:** `tools/recomp` (Python og capstone) oversetter hver x86-instruksjon i WET.EXE til C. Registre, flagg, stakk og minne bevares nøyaktig, så Watcom-kallkonvensjonen trenger ikke rekonstrueres først.
- **Runtime:** `src/runtime` (C11, SDL2, POSIX) erstatter KERNEL32, USER32, GDI32, WINMM, DDRAW, DSOUND, OLE32 og COMDLG32.
- **Bygg:** `cmake -S . -B build/game && cmake --build build/game -j`. CMake kjører rekompilatoren ved konfigurering. Generert C havner i `build/game/gen/` og ligger ikke i Git.
- **Kjøring:** `build/game/lula -- -novideo`. Spilldata leses fra `original/app`, som aldri skrives til. Lagring og INI-filer går til `local/save/` (overstyres med `--save`).

## Fakta om binæren

- PE32, Watcom C/C++, image base 0x400000, entry 0x442cfc. Koden ligger i 0x401000-0x44d000.
- Kodeoppdagelsen finner 1415 funksjoner og ca. 78 100 unike instruksjoner. Ghidra fant 945 funksjoner, resten er callbacks som bare nås via pekere.
- Det er 85 hopptabeller. Alle indirekte hopp er løst.
- Watcom-runtimen sender CF på tvers av `ret` (x87-hjelper ved 0x444050). Flagg-liveness er derfor interprosedural.
- Capstone mister F2-prefikset på `f2 a5` (`repnz movsd` i Watcoms memcpy ved 0x442be4). Prefikser leses nå fra rå bytes i `cfg.py`.
- Funksjoner returnerer adressen de faktisk returnerte til. Kallstedet sammenligner med forventet adresse og sender avvik videre opp. Slik håndteres longjmp-lignende kode.

## Status (2026-10-06)

- Den rekompilerte versjonen kjører hele spillet på Linux. Skriptede gjennomspillinger har vært gjennom alle tre trinnene uten feilfeller; 955 av 1261 levende funksjoner har kjørt (`docs/recomp/exploration.md`).
- Hovedmenyen: alle piksler utenfor bokstavene i menyknappene er identiske med originalen under Wine (RGB565).
- Teksten tegnes med Liberation Sans Bold (fontnavnet spillet ber om er "System Small", høyde -14, vekt 700). Noen DDF-etiketter brytes eller klippes. Wine viser originalen med en font av samme størrelse, så fonten er ikke endret uten et skjermbilde fra ekte Windows.
- Rekonstruert som lesbar C: memcpy og get-PC-stubben (Claude), fem ressursfunksjoner (Codex, PR #3, `docs/reconstruction/resource-readers.md`).
- Spillet treffer klikk der siste WM_MOUSEMOVE var (ingen GetCursorPos). Runtimen poster derfor en musebevegelse når en dialog, meldingsboks eller meny lukkes, slik Windows gjør.
- Scenariene i `tests/scenarios/` laster lagrede spill. Lagrede spill skal ikke i Git, så de bygges fra oppskrifter (arbeid pågår, se todo.md).
- Wine-referansen ligger i `build/wine-ref/` (lokal, ikke i Git): skjermbilder, API-sekvens og relay-trace.

## Verktøy og arbeidsflyt

- Ende-til-ende-tester: `python3 -m unittest tests.test_recompiled_game -v` (krever bygget `build/game/lula`).
- `build/game/lula-fncheck` sammenligner håndskrevne funksjoner med de genererte.
- Testkroker: `LULA_HEADLESS`, `LULA_INPUT`, `LULA_FRAMEDUMP`, `LULA_AUDIODUMP`, `LULA_COVERAGE`, `LULA_MSGBOX_AUTO`, `LULA_LOG=3`.
- Når en agent har halvferdige filer i hovedtreet, kan jeg teste i et eget worktree (`/home/user/lula-wt`, med symlenker til `original/app` og `analysis/decompiled`).

## Ting å passe på

- Ikke rediger `original/app/`. Kjør `python3 tools/project.py verify` før og etter arbeid.
- Video og MCI er satt på vent. En kommando som skulle lese AVI-kodek og videostrenger ble avvist av sikkerhetsklassifisereren. Spør brukeren før videoarbeid.
- ChatGPT-lenken brukeren delte kunne ikke åpnes (Cloudflare 403). Codex får oppgaver via `docs/samarbeid-codex.md` og svarer i `docs/samarbeid-codex-svar.md`. Oppgave 1 (ressursleserne) er levert og flettet inn; TAF-redigering pågår hos Codex.
- Diskkvoten er fast per økt. Bildedumper (PPM, 900 KB hver) fylte disken og drepte to agenter 2026-10-06. Gi agenter beskjed om å unngå `LULA_FRAMEDUMP` og rydde etter seg.
- Når et bygg må testes mens en agent bruker `build/game`, bygg i en egen katalog (for eksempel `build/merge`) og kjør testene med `LULA_BINARY=build/merge/lula`.
