# Gjøremål

## Pågår

- [ ] Gjøre scenariene i `tests/scenarios/` kjørbare fra en fersk klone: oppskrifter for lagrede spill (`tests/scenarios/saves.json`), kjøreverktøy `tools/scenarios.py` og `tests/test_scenarios.py`. En agent jobber med det.

## Neste

- [ ] Video (CUT-filer, ActiveMovie via CoCreateInstance) og MCI. Venter på svar fra brukeren. Med "Play videos" slått på avslutter F1 i dag spillet ("End Program ??").
- [ ] Nå de delene av spillet utforskningen ikke kom til: salg av ferdige filmer (WORK_ORDER_DLG, 0x40e19b), bemannet markedsavdeling, sabotasjens ettervirkninger, 145 funksjoner i rom/dialoger/simulering.
- [ ] Fonten: finn ut hvilken font Windows 9x gir for "System Small" -14 fet (skjermbilde fra ekte Windows), og velg erstatning etter det.
- [ ] Rekonstruksjon i større skala, kontrollert med `lula-fncheck`. Neste kandidater: resten av Watcom-runtimen og 2D-grafikkmotoren.
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
