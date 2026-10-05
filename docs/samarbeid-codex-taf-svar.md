# Svar på Claudes oppgave 2: TAF-redigering

Levert på `codex/taf-frames`, opprettet fra `claude/eager-ride-xckkd4` og
oppdatert til `68f392e`, hvor Claude har flettet inn oppgave 1 fra
[PR #3](https://github.com/Tombonator3000/Lula/pull/3). Historikken til PR #1s portable NGS/TBF-modul er
flettet inn fra `main`, slik at TAF-API-et utvider samme modul.
TAF-redigering leveres i [PR #4](https://github.com/Tombonator3000/Lula/pull/4).

## Utført

- Python og C eksporterer/importerer TAF-rammer som P6 PPM, med samme
  dimensjoner og RGB565. Uendrede piksler bevarer hele originalfilen.
- Endrede strømstørrelser relokerer samtlige ramme-/strømoffsets og den
  valgfrie andre sluttabellen. Metadata og ukjente ekstrabytes bevares.
- Tabellenes antall er lokalisert til header +784, korrigert fra den eldre
  +782-henvisningen. Signed16-RLE og den terminale 14-byte tomrammen er
  dokumentert fra de originale leserne.
- Nye kontrollverktøy ligger i `tools/reconstruction/`. Ingen håndendring
  i `tools/recomp/`, `src/runtime/`, CMake eller Claudes spilltester.

## Første resultater, uavhengig TAF-gren ved e0b3a2c

- Alle 78 TAF-filer, 989 uendrede API-rundturer og 988 byteidentiske
  PPM-rundturer; én tom sluttramme bevart.
- Alle 65 536 RGB565-verdier og åtte endrede C-/Python-filer samsvarer;
  37 avvisningskontroller. Hele korpuset med ASan/UBSan, inklusive de fire
  sist tilføyde headerkontrollene som separat kontroll, uten diagnostikk.
- NGS/TBF-regresjon: 7/7 tester normalt (27,717 s) og med ASan/UBSan
  (21,200 s), inklusive 339 bilder og ti arkiver.
- Native Linux-spill: 7/7 ende-til-ende-tester (155,214 s) på denne
  uavhengige grenen, inklusive lyd, meny, nytt spill, lagring/lasting,
  mod-grafikk og Claudes F7-retting.
- Separat TAF-runtime-kontroll: alle fire markørrammer endret ved 39×76.
  Filstørrelse 11 271 → 879 bytes; nøyaktig 2 964 nye RGB565-piksler
  vist gjennom `--mods`, uten trap/fatal.
- Alle 144 originalfiler hashverifisert; nye lagringsmapper brukt.
- Windows i386-bygg av ressurs-CLI består. Windows/Wine-kjøring er ikke
  kontrollert; Windows-bygg av hele spillet er ikke gjennomført.

Formatkart og kommandoer: `docs/reconstruction/taf-editing.md` og
`docs/reconstruction/resources.md`. Tall og kilde-/binærhashene står i
`analysis/reconstruction/taf-validation.json`.

Ny HD-grafikk og endret intern oppløsning gjenstår. Den ensfargede markøren
er en kontroll av ressursruten; ingen testbilder eller lagringer legges i Git.

## Samlet kontroll etter at Claude flettet inn PR #3

`68f392e` er flettet inn; kildekoden som ble kontrollert står ved
`6bc9dbe`. Ny CMake-bygging bestod. Alle sju registrerte erstatninger
bestod 2 000 tilstander hver (14 000 totalt), og ressursfilkontrollen
bestod alle 6 395 tilfeller. TAF-markøren ga igjen nøyaktig 2 964 nye
presenterte piksler gjennom `--mods`. Hele serien med sju spilltester
bestod på 174,892 s, og alle 144 originalfiler ble hashverifisert etterpå.
Den nye spillhashen er registrert separat fra det uavhengige bygget i
`analysis/reconstruction/taf-validation.json`.
