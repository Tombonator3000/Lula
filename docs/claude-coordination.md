# Samordning av Lula-arbeidet

Brukeren formidlet Claudes oppgavepakke i `docs/samarbeid-codex.md` og ba
Codex gjennomføre den på egne grener med PR-er mot
`claude/eager-ride-xckkd4`. Samordningen skjer gjennom repoet. Direkte
lesing eller melding i den oppgitte Claude-økten er ikke bekreftet.

## Arbeidsfordeling

Claude eier `tools/recomp/`, `src/runtime/`, CMake-oppsettet, eksisterende
spilltester og de løpende logg-/planfilene. Codex har rekonstruert de fem
ressursleserne i `src/reconstructed/resource_*.c` på
`codex/resource-readers`, levert i [PR #3](https://github.com/Tombonator3000/Lula/pull/3).
Resultat og kontrollkommandoer står i `docs/reconstruction/resource-readers.md`
og `docs/samarbeid-codex-svar.md` på den grenen. Brukeren godkjente særskilt
fire linjer Watcom-trådoppsett i `src/tools/fncheck.c` for funksjonstesten.

`codex/taf-frames` leverer oppgave 2: samme-størrelse eksport/import av
TAF-rammer i Python og den portable C-modulen. Den bygger på Claudes gren
og henter også inn historikken til den tidligere NGS/TBF-modulen fra
`main`. TAF-endringer krever ikke PR #3; det originale genererte
ressursleserbygget er også kontrollert med en endret TAF via `--mods`.

Codex eier i TAF-delen:

- `tools/assets.py`, `tools/build_reconstruction.sh` og kontrollverktøyene i `tools/reconstruction/`.
- `reconstruction/resources/` og den allerede leverte `tests/test_native_resources.py` fra PR #1.
- Dokumentasjon og kontrollrapporter for egne ressursendringer.

## Kontroller og videre arbeid

TAF-felt, bruksanvisning og grenser står i `docs/reconstruction/taf-editing.md`.
`analysis/reconstruction/taf-validation.json` registrerer de utførte
kontrollene og filhashene. Den eldre `resource-validation.json` er en
historisk rapport for PR #1; påstandene om manglende spillbygg der gjelder
bare det tidligere tidspunktet og den portable modulens omfang.

Originalfiler og tidligere lagringer bevares. Grafikkendringer som endrer
intern oppløsning, koordinater, treffområder eller timing krever eget
arbeid og spillkontroll. Ny HD-grafikk og Windows-bygg av selve spillet er
ikke gjennomført i denne oppgavepakken. Ressursverktøyet er krysskompilert
for Windows x86, uten bekreftet Windows/Wine-kjøring.
