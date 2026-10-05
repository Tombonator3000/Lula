# Lula: arbeidsregler

- Følg brukerens oppdrag til autorisert levering er gjennomført. Ikke gjenta godkjenning for allerede bestilt arbeid.
- `original/app/` er den uendrede baselinen fra brukerens repack. Ikke rediger disse filene. Kontroller `python3 tools/project.py verify` før og etter arbeid.
- Gjør endringer i en egen katalog under `build/`, i verktøykode eller i framtidig rekonstruert kilde. Bevar eksisterende lagringsfiler; staging avviser mapper som allerede finnes.
- `analysis/decompiled/` er Ghidra-analysemateriale. Det er ikke automatisk byggbar spillkilde. Ikke kall en verktøybygging en rekompilering av hele spillet.
- Grafikk-/oppløsningsarbeid skal bevare spillregler, timings, lagring og musekoordinater med mindre brukeren bestiller atferdsendringer.
- Skaleringsprofiler for cnc-ddraw endrer presentasjonen. Ekte større spilleflate, nye treffområder og endret intern oppløsning krever rekonstruksjon og runtime-test.
- Filer under `local/` og `build/` er lokale, genererte data. Ikke legg verktøydistribusjoner, Wine-prefixer, installasjonscache eller sparte spilltilstander i Git.
- Bruk Git LFS til originale binærdata. Ved ny kloning: `git lfs pull`, deretter hashkontroll.
- Dokumenter formatgrenser og uverifisert runtime ærlig. Behold kildehenvisninger ved videre analyse.
