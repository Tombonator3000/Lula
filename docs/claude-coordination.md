# Samordning av Lula-arbeidet

Brukeren har bedt Codex samarbeide med Claude om dekompilering, rekonstruksjon og grafikk. Den oppgitte Claude-økten kunne ikke leses eller meldes fra Codex fordi nettleserens sikkerhetskontroll ikke kunne verifiseres. Ingen melding er derfor sendt direkte til Claude, og arbeidsfordelingen nedenfor er et forslag som avventer svar.

## Spørsmål til Claude

Hva trenger du hjelp til i Lula-arbeidet: Watcom/dekompilering, rekonstruert spillkode, ressursformater eller grafikk? Hvilken gren bruker du, og hvilke filer/moduler tar du ansvar for? Oppgi én avgrenset oppgave Codex kan eie med et observerbart ferdigkriterium.

## Tilgjengelig grunnlag

- `main` har 144 uendrede spillfiler via Git LFS, SHA-256-manifest og testet Python-verktøykjede for ressursformatene.
- Ghidra har eksportert 944/945 identifiserte funksjoner; én ufullstendig funksjon og Watcom-ABI-begrensningene er dokumentert i `docs/binary-analysis.md`.
- START.TBF og TGP-arkivene har en kontrollert grafikk-ut/inn-rute. TAF kan foreløpig deles/repakkes uendret.
- Visningsskalering er klargjort separat fra intern oppløsning. De faste 640/480-grensene er kartlagt i `analysis/decompiled/resolution-candidates.tsv`.

## Avgrenset Codex-kandidat

Codex bruker grenen `codex/resource-reconstruction`. Den nye modulen under `reconstruction/resources/` rekonstruerer NGS/TBF-ressursinnlasting i byggbar C11 og sammenlignes med den validerte Python-dekoderen. Den endrer ingen spillregler, originalfiler, eksisterende Ghidra-analyse eller produksjonsgrafikk. Det er en separat utviklingsmodul, ikke et ferdig rekonstruert spill.

Filer som Codex eier i dette avgrensede arbeidet:

- `reconstruction/resources/`
- `tools/build_reconstruction.sh`
- `tests/test_native_resources.py`
- `docs/reconstruction/resources.md`
- Denne samordningsfilen og rapporten for modulens verifikasjon.

Claude kan bruke den eksisterende analysen og foreslå videre oppgavefordeling. Ingen eierskap til Claudes filer eller framtidige oppgaver er avtalt. Send status via repoets vanlige branch/PR-fly eller ved å oppdatere samordningen i en separat commit. Bevar `original/app/`, spillatferd og lagringsfiler.

## Aksept av videre rekonstruksjon

En byggbar modul må ha dokumentert samsvar med faktiske originaldata og feiltester. En visuell modul må også kjøres og inspiseres. Et komplett rekonstruert spill må kunne starte, gjennomføre spillflyten og lagre/laste med samme observerbare atferd som originalen. Analysekode, en testprobe eller en grafikkforhåndsvisning alene oppfyller ikke dette.
