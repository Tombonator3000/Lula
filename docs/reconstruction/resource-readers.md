# Lesbare ressurslesere i spillbygget

Codex har rekonstruert fem funksjoner fra oppgavepakken i
`docs/samarbeid-codex.md`. De erstatter den genererte koden direkte i
Linux-bygget, via `RT_RECONSTRUCTED`, og den genererte versjonen beholdes som
kontrollgrunnlag. Spillets Watcom-registre, returadresser, gjestestakk,
minneskriving, feilord og filposisjoner er bevart.

| Adresse | Kildefil | Oppgave |
|---|---|---|
| 0x43f7c1 | `resource_rle.c` | Rådata, byte-RLE, RGB565-RLE, 64 000-byte blokker, RGB555-konvertering |
| 0x442431 | `resource_signature.c` | TBF/TPF/TAF/TFF-signatur til type, uten forskjell på store/små ASCII-bokstaver |
| 0x43ebd3 | `resource_ngs.c` | Finn og søk til NGS-post, med originalens indeksavgrensing |
| 0x440041 | `resource_ngs.c` | Les lengde/type og gjeldende NGS-payload |
| 0x43e8eb | `resource_tbf.c` | Header, palett, dekoderdispatch og sekvensielt NGS-oppslag |

Kildegrunnlaget er originalinstruksjonene i WET.EXE og spesifikasjonen B.6 i
`docs/recomp/specs/resources-and-game-map.md`. Dette er lesbare erstatninger
inne i Claudes statisk rekompilerte spill, ikke en påstand om at resten av
spillogikken allerede er rekonstruert som vanlig kildekode.

## Kontrollert 2026-10-05

- CMake/GCC 15.2.0/SDL 2.32.10: native Linux-bygg av `lula` og `lula-fncheck`.
- `lula-fncheck --iterations 2000`: alle seks registrerte erstatninger,
  inkludert eksisterende memcpy, bestod; 12 000 tilstander, ingen avvik.
  De fem nye funksjonene bidrar med 10 000 tilstander. De tilfeldige
  ressursprofilene dekker manglende/ugyldige filhåndtak; de åpner ikke filer.
- Separat filbasert kontroll: 6 395 tilfeller, ingen avvik. 71 signaturer,
  1 454 NGS-oppslag, 1 409 NGS-lesinger, 2 678 dekodinger og 783 TBF-lesinger.
  Alle 10 NGS-pooler, 339 TBF-bilder og de 988 ikke-tomme TAF-rammene inngår;
  den siste av 989 TAF-rammer er en 0x0-sentinel uten billeddata.
- Syntetiske tilfeller prøver store blokker, rådata/byte-RLE/ord-RLE,
  RGB555-konvertering, EOF, feil signatur, palett, versjon 8/19 og gamle feilord.
- Alle seks eksisterende ende-til-ende-tester bestod, fordelt på to kjøringer:
  fem spilltester (121,659 s) og funksjonstesten (27,652 s). Menyens piksler,
  nytt spill, mod-grafikk, lydsamples og lagring/lasting kontrolleres.
- `python3 tools/project.py verify`: alle 144 originalfiler uendret før og
  etter arbeidet. Ingen eksisterende lagringsfiler ble brukt eller endret.

Den filbaserte kontrollen sammenligner alle registre, returadresse, flagg
som kallere leser, x87-tilstand, feilstatus, filposisjon, hele programbildet,
filmanagerens buffer, destinasjon med vaktområde, palett og levende stakk.
Begge versjoner får samme filhåndtak og samme startposisjon. Frigjort stakk
under avsluttende ESP sammenlignes ikke, på samme måte som i `lula-fncheck`.

Etter at Claudes gren fikk 80-bit x87 og nye flyttallsbyggeflagg, ble
`d13e0c7` flettet inn på Codex-grenen. Det nye bygget bestod på nytt alle
12 000 tilfeldige tilstander og 6 395 filtilfeller. Hele serien med seks
ende-til-ende-tester ble kjørt samlet og bestod på 158,316 s. Oppdateringen
av Claudes filer kom fra den publiserte grenen; Codex redigerte dem ikke.

Funksjonskontrollen trengte fire linjer oppsett i `src/tools/fncheck.c`:
Watcoms tråddata initialiseres med stakkgrensen før `__CHK` kalles. Uten dette
krasjet testen før noen ressursfunksjon kunne kontrolleres. Brukeren godkjente
denne avgrensede endringen; runtimen, rekompilatoren og spilltestene er urørt.

## Gjenta kontrollen

Etter vanlig CMake-bygg som beskrevet i `docs/recompilation.md`:

```sh
build/game/lula-fncheck --iterations 2000
python3 tools/reconstruction/build_resource_check.py
python3 tools/reconstruction/resource_fixtures.py
build/game/lula-resource-check . build/resource-check/fixtures.tsv
python3 -m unittest tests.test_recompiled_game -v
python3 tools/project.py verify
```

For et annet CMake-buildområde, bruk `--build-dir` til koblingsverktøyet og
`LULA_BINARY` for spilltestene. Filkontrollen bruker en egen midlertidig
lagringsmappe. Originaldata og genererte grafikkfiler legges aldri i rapporten.

Kontrollene sammenligner med den genererte C-versjonen og eksisterende
Wine-referansehash. Nye tester mot selve prosessorkoden i Unicorn eller
langvarig manuell gjennomspilling inngår ikke i denne leveransen.

## Samlet kontroll med TAF-redigering

Claude flettet PR #3 inn ved `68f392e`; dette er tatt inn i TAF-grenen i
PR #4. Ved kodecommit `6bc9dbe` bestod ny CMake-bygging, alle sju
registrerte erstatninger med 2 000 tilstander hver (14 000 totalt), alle
6 395 filtilfeller og alle sju ende-til-ende-tester (174,892 s). De fem
ressursfunksjonene bidrar fortsatt med 10 000 av tilstandene; øvrige
er memcpy og Claudes get-PC-stub. Endret TAF-markør ble også kontrollert
gjennom `--mods`, med nøyaktig 2 964 nye presenterte piksler. Alle 144
originalfiler var uendret etterpå.
