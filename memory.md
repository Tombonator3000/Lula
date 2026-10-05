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

## Status (2026-10-05)

- Den rekompilerte versjonen når hovedmenyen hodeløst på Linux. Alle piksler utenfor bokstavene i menyknappene er identiske med originalen under Wine (RGB565).
- Teksten tegnes med Liberation Sans Bold (fontnavnet spillet ber om er "System Small", høyde -14, vekt 700).
- Wine-referansen ligger i `build/wine-ref/` (lokal, ikke i Git): skjermbilder, API-sekvens og relay-trace.

## Ting å passe på

- Ikke rediger `original/app/`. Kjør `python3 tools/project.py verify` før og etter arbeid.
- Video og MCI er satt på vent. En kommando som skulle lese AVI-kodek og videostrenger ble avvist av sikkerhetsklassifisereren. Spør brukeren før videoarbeid.
- ChatGPT-lenken brukeren delte kunne ikke åpnes (Cloudflare 403). Codex jobber parallelt på grenen `codex/resource-reconstruction` (PR #1, NGS/TBF-leser i C).
