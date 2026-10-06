# Gjøremål

## Neste

- [ ] Ny PR mot `main` med alt etter `7234f07` (venter på brukeren).
- [ ] Katalog over alle bilder (fil, post, størrelse, rom) og en plan for HD-teksturer i runtimen (spurt brukeren).

- [ ] Video (CUT-filer, ActiveMovie via CoCreateInstance) og MCI. Venter på svar fra brukeren. Med "Play videos" slått på avslutter F1 i dag spillet ("End Program ??").
- [ ] `0x40a15c` (WM_DESTROY-håndtereren) nås bare via feilruten for video; tas med når video er avklart.
- [ ] Fonten: tegnplasseringen stemmer nå med Wine. Om ekte Windows 9x ga en smalere font for "System Small" -14 fet, krever et skjermbilde fra ekte Windows.
- [ ] Rekonstruksjon i større skala, kontrollert med `lula-fncheck`. Blitterne er gitt til Codex (oppgave 4 i `docs/samarbeid-codex.md`); deretter 2D-grafikkmotoren og resten av Watcom-runtimen.
- [ ] Logg direkte importkall i `--trace` (i dag logges bare indirekte kall og COM-metoder).
- [ ] Lineær interpolasjon i lydmikseren (i dag nærmeste sample; påvirker bare lydkvalitet).
- [ ] Windows-bygg av samme kildekode (MinGW 13 og SDL2 2.30.8 for MinGW finnes). Lav prioritet, originalen kjører allerede på Windows.

## Senere

- [ ] Høyere intern oppløsning. Krever rekonstruksjon av renderer, koordinater og treffområder.
- [ ] Gradvis erstatning av genererte funksjoner med lesbar, håndskrevet C i `src/reconstructed/`.

## Ferdig

- [x] Kodeoppdagelse, flagg-liveness og x86-til-C-oversetter (`tools/recomp`), verifisert mot unicorn (247 560 tilstander).
- [x] x87 80-bit-eksakt med `long double`.
- [x] Runtime med minne, PE-laster, dispatch, tråder, filsystem-overlay og Win32/DirectX på SDL2.
- [x] CMake-bygg som rekompilerer og bygger alt fra repoet.
- [x] Referansekjøring av originalen under Wine med skjermbilder og API-sekvens.
- [x] Rekompilert spill når hovedmenyen. Grafikken er piksel-identisk med originalen.
- [x] "New game" og første spillskjerm, spillklokken går like fort som i originalen.
- [x] WM_TIMER-semantikk, dobbeltklikk og aktivering etter spesifikasjonen.
- [x] Låsoverlevering ved bakoverhopp (RT_POLL) og ved retur fra Win32-kall.
- [x] Dialogbehandler, popupmeny og MessageBox. Lagring og lasting testet.
- [x] Grafikkutskifting via `--mods`.
- [x] Lydmikser verifisert bit-eksakt mot originalens samples i menyen.
- [x] Mekanisme for håndskrevet rekonstruksjon (`RT_RECONSTRUCTED`) og kontrollverktøyet `lula-fncheck`.
- [x] Ende-til-ende-tester i `tests/test_recompiled_game.py`.
- [x] Dekningstellere per funksjon (`LULA_COVERAGE`, `tools/coverage_report.py`).
- [x] Skriptet utforskning av alle tre trinn: 955 av 1261 levende funksjoner kjørt, ingen feilfeller (`docs/recomp/exploration.md`).
- [x] Musposisjon etter modale dialoger (ny WM_MOUSEMOVE som i Windows).
- [x] Ressursleserne rekonstruert som lesbar C (Codex, PR #3), kontrollert på nytt etter fletting.
- [x] 64 scenariotester kjørbare fra en fersk klone (`tools/scenarios.py`, 40 oppskrifter for lagrede spill, fast klokke med `LULA_CLOCK`).
- [x] Sjelden krasj ved avslutning (lydtimeren kalte en lukket lydenhet) rettet.
- [x] Codex-gjennomgangen av PR #2: timer, kommandolinje og README.
- [x] TAF-redigering (Codex, PR #4) gjennomgått, rettet og flettet inn.
- [x] GDI-tekst med hele piksler per tegn og uten kerning; menyteksten har samme kanter som under Wine.
- [x] `TextOutA` logges på nivå 3 (tooltips, hjelpesider).
- [x] Runde 2 av utforskningen: hele filmproduksjonen og resten av spillet, 1123 av 1262 levende funksjoner, ingen runtime-feil, 157 scenarier.
- [x] `docs/recomp/game-state.md`: spilltilstand i minne og lagrede spill, faktasjekket.
- [x] Dekningstellere for håndskrevne funksjoner, dra med musen i skript, `# covers:`, `# exit:` og filer i oppskrifter.
