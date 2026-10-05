# Gjøremål

## Pågår

- [ ] Instruksjonsvis verifisering av lifteren mot unicorn (`tests/recomp/unicorn_diff.py`).
- [ ] Spesifikasjoner for Win32/DirectX-bruken (`docs/recomp/specs/`). KERNEL32 og USER32/GDI32 er skrevet, resten pågår.

## Neste

- [ ] Dialogbehandler (lagring, lasting, bank, lister, tekstfelt) og popupmenyen "Set Digital Output". En agent jobber med det.
- [ ] Test lagring og lasting (`DATA\SAVE`) når dialogene virker, og at alt havner i lagringskatalogen.
- [ ] Utforskende kjøring (mange skriptede klikk) for å finne kodeveier som treffer `rt_trap` eller manglende funksjonsinnganger.
- [ ] Logg direkte importkall i `--trace` (i dag logges bare indirekte kall og COM-metoder).
- [ ] Lineær interpolasjon i lydmikseren (i dag nærmeste sample; påvirker bare lydkvalitet).
- [ ] Rekonstruksjon i større skala: Watcom-runtime og ressursdekodere først, kontrollert med `lula-fncheck`.
- [ ] Windows-bygg av samme kildekode (MinGW 13 og SDL2 2.30.8 for MinGW finnes). Lav prioritet, originalen kjører allerede på Windows.

## Senere

- [ ] Video (AVI/CUT-filer) og MCI. Avventer avklaring med brukeren.
- [ ] Grafikkutskifting via overlay: erstatningsfiler i en egen katalog som leses før `original/app`.
- [ ] Høyere intern oppløsning. Krever rekonstruksjon av renderer, koordinater og treffområder.
- [ ] Gradvis erstatning av genererte funksjoner med lesbar, håndskrevet C i `src/reconstructed/`.

## Ferdig

- [x] Kodeoppdagelse, flagg-liveness og x86-til-C-oversetter (`tools/recomp`).
- [x] Runtime med minne, PE-laster, dispatch, tråder, filsystem-overlay og Win32/DirectX på SDL2.
- [x] CMake-bygg som rekompilerer og bygger alt fra repoet.
- [x] Referansekjøring av originalen under Wine med skjermbilder og API-sekvens.
- [x] Rekompilert spill når hovedmenyen. Grafikken er piksel-identisk med originalen.
- [x] "New game" og første spillskjerm, spillklokken går like fort som i originalen.
- [x] WM_TIMER-semantikk, dobbeltklikk og aktivering etter spesifikasjonen.
- [x] Låsoverlevering ved bakoverhopp (RT_POLL) og ved retur fra Win32-kall.
- [x] Grafikkutskifting via `--mods`.
- [x] Lydmikser verifisert bit-eksakt mot originalens samples i menyen.
- [x] Mekanisme for håndskrevet rekonstruksjon (`RT_RECONSTRUCTED`) og kontrollverktøyet `lula-fncheck`.
- [x] Ende-til-ende-tester i `tests/test_recompiled_game.py`.
