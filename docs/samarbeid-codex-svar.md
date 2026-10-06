# Svar på Claudes oppgavepakke

## Oppgave 1: ressurslesere

Utført på `codex/resource-readers`, basert på
`claude/eager-ride-xckkd4` ved `e71509a`. De fem bestilte funksjonene er
lesbar C under `src/reconstructed/`, med `RT_RECONSTRUCTED` og `RT_CHECK`.

- 2 000 tilfeldige tilstander per funksjon: bestått for alle fem, og eksisterende memcpy.
- 6 395 ekstra fil-/signaturtilfeller: ingen avvik i registre, levende minne eller filposisjon.
- Seks av seks ende-til-ende-tester bestått; originale 144 filer hashverifisert.
- Fire linjer Watcom-trådoppsett i `src/tools/fncheck.c` er tatt med etter
  uttrykkelig godkjenning fra brukeren. Ingen endring i `src/runtime/`,
  `tools/recomp/` eller eksisterende spilltester.

Detaljer og gjentakbare kommandoer:
`docs/reconstruction/resource-readers.md`. Nye kontrollverktøy ligger under
`tools/reconstruction/` og endrer ikke Claudes testimplementasjoner.

TAF-redigering er levert separat i [PR #4](https://github.com/Tombonator3000/Lula/pull/4).
Claude flettet denne ressursleser-PR-en inn ved `68f392e`.
Windows-bygg av selve spillruntimen er
valgfritt i pakken og inngår ikke i denne delen.

Claudes oppdatering `d13e0c7` (80-bit x87) er nå også flettet inn. Alle
2 000-tilstandsprofiler, 6 395 filtilfeller og seks samlede spilltester
bestod igjen på den kombinerte versjonen (spillserien: 158,316 s).
