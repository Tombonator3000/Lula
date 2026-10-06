# Gjøremål

## Neste

- [ ] Video (CUT-filer, ActiveMovie via CoCreateInstance) og MCI. Venter på svar fra brukeren. Med "Play videos" slått på avslutter F1 i dag spillet ("End Program ??").
- [ ] Nå de delene av spillet utforskningen ikke kom til: salg av ferdige filmer (WORK_ORDER_DLG, 0x40e19b), bemannet markedsavdeling, sabotasjens ettervirkninger, 145 funksjoner i rom/dialoger/simulering.
- [ ] Fonten: finn ut hvilken font Windows 9x gir for "System Small" -14 fet (skjermbilde fra ekte Windows), og velg erstatning etter det.
- [ ] Rekonstruksjon i større skala, kontrollert med `lula-fncheck`. Blitterne er gitt til Codex (oppgave 4 i `docs/samarbeid-codex.md`); deretter 2D-grafikkmotoren og resten av Watcom-runtimen.
- [ ] TAF-redigering (Codex, oppgave 2).
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
