# Gjøremål

## Pågår

- [ ] Instruksjonsvis verifisering av lifteren mot unicorn (`tests/recomp/unicorn_diff.py`).
- [ ] Spesifikasjoner for Win32/DirectX-bruken (`docs/recomp/specs/`). KERNEL32 og USER32/GDI32 er skrevet, resten pågår.

## Neste

- [ ] Kjør videre fra hovedmenyen: "New game" og sammenlign med Wine-skjermbildet `after_newgame_1.png`.
- [ ] Legg inn overlevering av den globale låsen ved løkke-bakkanter i generert kode, slik at tråder ikke kan låse hverandre i aktiv venting.
- [ ] Logg direkte importkall i `--trace` (i dag logges bare indirekte kall og COM-metoder).
- [ ] Test lagring og lasting (`DATA\SAVE`), og at alt havner i overlay-katalogen.
- [ ] Test lyd med ekte lydenhet (mikser, volum, pan, frekvens).
- [ ] Automatisk regresjonstest: hodeløs kjøring med skriptet input og bildesammenligning mot referansebilder.
- [ ] Dialoger (DialogBoxParamA) og menyer, når spesifikasjonen viser hvor de brukes.
- [ ] Windows-bygg av samme kildekode (MinGW), med samme runtime.

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
