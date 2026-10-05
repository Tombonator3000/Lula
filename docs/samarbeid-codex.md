# Oppgaver til Codex (ChatGPT)

Skrevet av Claude 2026-10-05. Brukeren ba om at ChatGPT/Codex hjelper til. Claude kan ikke snakke med ChatGPT direkte, så oppgavene står her og i PR [Tombonator3000/Lula#2](https://github.com/Tombonator3000/Lula/pull/2).

## Utgangspunkt

Grenen `claude/eager-ride-xckkd4` (PR #2) inneholder en statisk rekompilering av hele WET.EXE til C, en runtime på SDL2 og tester. Les `docs/recompilation.md` først, deretter `docs/recomp/specs/` (spesifikasjoner for alle Win32/DirectX-kall og et kart over spillkoden).

```sh
git fetch origin claude/eager-ride-xckkd4
git checkout -b codex/<oppgave> origin/claude/eager-ride-xckkd4
git lfs pull
sudo apt install cmake libsdl2-dev && pip install -r tools/recomp/requirements.txt
cmake -S . -B build/game && cmake --build build/game -j
python3 -m unittest tests.test_recompiled_game -v      # 6 ende-til-ende-tester
build/game/lula-fncheck                                  # kontroll av rekonstruerte funksjoner
```

Regler: følg `AGENTS.md`. Ikke rediger `original/app/`. Arbeid på en egen `codex/...`-gren basert på grenen over og lag en PR mot den. Kjør testene og `python3 tools/project.py verify` før PR.

## Filer Claude jobber i (unngå å endre dem uten avtale)

`tools/recomp/`, `src/runtime/` (inkludert `win32/`), `CMakeLists.txt`, `tests/test_recompiled_game.py`, `tests/recomp/`, `log.md`, `memory.md`, `todo.md`.

## Oppgave 1: rekonstruer ressurs- og dekoderfunksjonene (anbefalt først)

Dette passer rett inn i NGS/TBF-arbeidet fra PR #1. Erstatt de genererte versjonene med lesbar C i `src/reconstructed/` (én fil per område, for eksempel `src/reconstructed/resources.c`). Mekanismen er beskrevet i `docs/recompilation.md` under "Videre rekonstruksjon", med `src/reconstructed/watcom_crt.c` som eksempel.

Adressene og oppførselen står i `docs/recomp/specs/resources-and-game-map.md`, del B.6:

| Adresse | Funksjon |
|---|---|
| 0x43f7c1 | RGB565 RLE-rammedekoder (modus 2), 4 kallere |
| 0x442431 | Signaturkontroll "TBF"/"TPF"/"TAF"/"TFF" til typekode |
| 0x43ebd3 | NGS: finn post N (kaller filrutinene) |
| 0x440041 | NGS: les gjeldende post |
| 0x43e8eb | TBF fra gjeldende filposisjon |

Krav:

- Marker hver funksjon med `RT_RECONSTRUCTED(0x...)` og legg til `RT_CHECK(0x..., "profil")` slik at `lula-fncheck` kan prøve den på tilfeldige tilstander. Profilspråket står i `src/reconstructed/reconstructed.h`.
- Watcom sender de første argumentene i EAX, EDX, EBX og ECX og returnerer i EAX. Registre, flagg som kallere leser, stakk og minne må bli nøyaktig som i originalen. Funksjoner som kaller andre spillfunksjoner bruker `rt_call_guest(c, f_XXXXXXXX, returadresse)`.
- Ferdigkriterium: `build/game/lula-fncheck` består for alle nye funksjoner (minst 2000 tilstander hver), og alle 6 ende-til-ende-tester er grønne. Skriv kort i hver funksjon hva den gjør, med referanse til spesifikasjonen.

## Oppgave 2: TAF-redigering (animasjonsgrafikk)

Uendret fra forrige forslag: kartlegg de uidentifiserte TAF-feltene og støtt eksport og import av rammer i `tools/assets.py` (gjerne PNG), med samme API i `reconstruction/resources/`. Ferdigkriterium: alle 78 TAF-filer og 989 rammer gir byte-identiske filer etter uendret eksport og import, og en endret ramme med samme mål validerer. Den rekompilerte versjonen kan da teste endringen: legg filen i en katalog og start med `build/game/lula --mods katalog -- -novideo`.

## Oppgave 3 (valgfri): Windows-bygg av den rekompilerte versjonen

MinGW-w64 13 og `SDL2-devel-2.30.8-mingw` er nok. Runtimen bruker i dag POSIX (mmap, realpath, `/proc/self/exe`, sigaction, localtime_r, open uten O_BINARY). Legg Windows-varianter i en ny fil `src/runtime/os_win32.c` eller i `#ifdef _WIN32`-blokker, og en CMake-toolchainfil for krysskompilering. Test `lula.exe` hodeløst under Wine med `LULA_HEADLESS=1`. Si fra i PR-en hvilke runtime-filer som er endret, siden Claude jobber i dem.

## Rapportering

Svar i PR-en din eller i en ny fil `docs/samarbeid-codex-svar.md`: hva som er gjort, testresultater med tall, og hva som gjenstår.
