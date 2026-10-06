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
- Teksten tegnes med Liberation Sans Bold (fontnavnet spillet ber om er "System Small", høyde -14, vekt 700), med hele piksler per tegn og uten kerning som i GDI. Menyteksten har da nøyaktig samme kanter som under Wine. 37 av 219 enlinjes DDF-etiketter blir for brede for rektangelet, også under Wine.
- Spilltilstand for produksjonen (bygg, ansatte, filmer) er kartlagt av basisagenten i runde 2: 15 byggposter à 40 bytes fra 0x455849 (+0x10 leid/eid), ansattabell via [0x45555c] med 553 poster à 40 bytes. Detaljer kommer i dokumentasjonen etter runden.
- Rekonstruert som lesbar C: memcpy og get-PC-stubben (Claude), fem ressursfunksjoner (Codex, PR #3, `docs/reconstruction/resource-readers.md`). Codex har også en portabel ressursmodul i `reconstruction/resources/` med TAF-redigering (PR #1 og #4, `docs/reconstruction/taf-editing.md`).
- PR #2 ble slått inn i `main` på `7234f07`; alt senere ligger bare på grenen og trenger en ny PR. Codex' grener bygger på denne grenen, så historikken skal ikke skrives om (flett, ikke rebase).
- Spillet treffer klikk der siste WM_MOUSEMOVE var (ingen GetCursorPos). Runtimen poster derfor en musebevegelse når en dialog, meldingsboks eller meny lukkes, slik Windows gjør.
- Scenariene i `tests/scenarios/` laster lagrede spill. Lagrede spill skal ikke i Git, så `tools/scenarios.py saves` bygger dem fra `tests/scenarios/saves.json` (ca. 5 min). `tools/scenarios.py run` kjører alle 64 (ca. 29 min med `-j 3`). Runneren setter `LULA_CLOCK=1997-01-01T08:00:00`; flere scenarier er tilpasset de tilfeldige bydataene denne klokka gir.
- Avslutning: hovedtråden tar den globale låsen før lyd og SDL stenges, med mindre en spilltråd ba om avslutningen (da holder den låsen allerede).
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
