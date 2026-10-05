# Verifiserte ressursformater og grafikkrute

Dette er kartlagt mot de utpakkede originalfilene i `original/app` den 5. oktober 2026. Originalfilene skal bevares. Verktøyet `tools/assets.py` bruker bare Python-standardbiblioteket og skriver til nye filer eller en ny utpakkingskatalog. Eksisterende utdata blir ikke overskrevet.

Formatobservasjonene er egne kontroller av binærdata. For sammenligning ble forfatterens [Lula-plugin](https://game-viewer.org/plugin.php?id=45) og [formatleserens kildekode](https://game-viewer.org/plugins_files/g_lula.py) lest; implementasjonen her er skrevet selvstendig. Det følger ingen tredjepartsdekoder eller spillressurser med verktøyet.

## Hva som fungerer nå

| Format | Originalfiler | Verifisert funksjon | Begrensning |
|---|---:|---|---|
| TGP | 4 / 334 TBF-payloads | Liste, utpakking, utskifting av payloads, repakking med nye størrelser/offsets | Ingen runtime-test av endrede bilder |
| TAP | 5 / 985 payloads | Samme NGS-container; lyd kan trekkes ut som WAV | Person-/utstyrsdata er fortsatt ugjennomsiktige binærposter |
| DDF | 1 / 90 payloads | Samme NGS-container; bytebevarende deling og repakking | Spilldata/layout-feltenes semantikk er ikke ferdig kartlagt |
| TBF | 5 separate + 334 i TGP | Dekoding av modus 0/2; eksport/import via PPM, RGB565 og samme dimensjon | Andre modi og større dimensjoner er ikke autorisert av formatbevisene |
| TAF | 78 / 989 rammer | Verifisert rammegrense, RGB565-RLE, metadata; byteidentisk deling/repakking | Repakking av endrede TAF-rammer er bevisst avvist |
| CUT | 43 | Alle er vanlige RIFF/AVI med riktig totalstørrelse | Ingen videoavspilling, bildeeksport eller rekomprimering utført av dette verktøyet |

TAP-lyd består av 107 WAV-payloads med typeord 27. Totalt inneholder NGS-filene 334 poster med type 1, 880 med type 24, 88 med type 21 og 107 med type 27. Typeord og rekkefølge bevares; filendelsen alene avgjør ikke hva payloaden er.

## NGS: TGP, TAP og DDF

Alle 10 filer følger den samme observerte strukturen. Tallene er little-endian.

| Felt | Posisjon / størrelse | Bevis |
|---|---|---|
| Signatur | 0 / 4 bytes | `NGS\0` |
| Versjons-/formatord | 4 / u16 | 21 i TGP, 24 i TAP/DDF |
| Antall poster | 6 / u16 | Samsvarer med postsekvens og indeks |
| Postheader | Fra 8 / 6 bytes per post | u32 payloadlengde, u16 typeord |
| Payload | Umiddelbart etter hver postheader | Lengde tilsvarer headerfeltet |
| Indeks | Siste `antall * 4` bytes | u32 absolutte offsets til payloadene, altså 6 bytes etter postheaderne |

Det finnes ingen uidentifiserte mellomrom eller restbytes i disse originalene. Repakking beregner lengder og absolutte offsets på nytt. En WAV/TBF-payload slutter før neste 6-byte postheader; denne headeren er ikke en del av eksportfilen.

`WET.DDF` har to poster av type 24 og 88 av type 21. Første payload inneholder blant annet ressursstier, men feltstrukturen for spillogikk/layout er ikke gjort endringsbar. Å kunne repakke containeren betyr ikke at alle spilldata er dekompilert.

## TBF: RGB565 og to verifiserte modi

| Felt | Posisjon / størrelse |
|---|---|
| Signatur | 0 / `TBF\0` |
| Versjonsord | 4 / u16 = 16 |
| Kompresjonsmodus | 6 / u16 = 0 eller 2 |
| Dekodet byteantall | 8 / u32 = `bredde * høyde * 2` |
| Bredde, høyde | 12, 14 / u16 |
| Pikselstrøm | Fra 16 |

RGB565 er en little-endian 16-bit piksel med R i bit 11–15, G i bit 5–10 og B i bit 0–4. PPM-eksporten utvider 5/6-bit verdier ved bitreplikasjon. Importen kvantiserer tilbake til RGB565; alle 65 536 mulige pikselverdier er testet med eksakt tilbakeføring.

Modus 0 består av rå pikselord. Modus 2 består av kommandoord: `0x0000..0xefff` gjentar neste pikselord det oppgitte antallet ganger; `0xf000..0xffff` etterfølges av `65536 - kommando` bokstavelige pikselord. En nullkommando konsumerer det neste pikselordet uten å produsere piksler. Dekoderen krever nøyaktig oppgitt pikselantall og fullstendig konsumert strøm.

Alle 339 originalbilder er validert: 122 rå og 217 komprimerte. Uendret PPM-import returnerer original-TBF-en byteidentisk, også når kompresjonen bruker andre gyldige kommandoer enn vår encoder. Et endret bilde beholder originalmodus og header og får en ny validert pikselstrøm.

Verktøyet tillater foreløpig bare samme dimensjon som originalmalen. Å øke vindus-/skjermoppløsningen er en egen presentasjonsrute. Å øke selve ressursdimensjonene krever kontroll av koordinater, treffområder, DDF-referanser og renderer før det kan kalles kompatibelt.

## TAF: rammer, metadata og tom sluttramme

Headeren inneholder `TAF\0`, u16 versjon 16, u16 rammetall og u32 summen av dekodede RGB565-bytes. Bytes 12–779 er et bevart 768-byte område; verdiene er ikke brukt som palett for RGB565-dekodingen. Ved offset 780 ligger u32 absolutt start på første ramme. Originalenes første ramme begynner ved 787; bytes 784–786 bevares uten semantisk tolkning.

Hver normalramme inneholder u16 modus 2, u16 bredde, u16 høyde, u32 absolutt rammeslutt, u32 absolutt start på pikselstrømmen og ett uidentifisert flaggbyte. Strømstart er `rammestart + 15`. Pikselstrømmen følger samme verifiserte RGB565-RLE som TBF modus 2.

`BUTCH.TAF` har én siste ramme med 0x0 dimensjoner. Den har bare 14 bytes: begge offsetfeltene peker én byte forbi faktisk rammeslutt. Dette særtilfellet er kontrollert eksplisitt og beholdes byteidentisk; ingen piksler produseres.

Etter rammene følger `rammetall * 4` uidentifiserte metadatabytes. 53 filer har i tillegg en like stor tabell med u32 absolutte rammestarts som er validert mot de leste rammene; 25 filer mangler denne tilleggstabellen. Samlet dekodet størrelse er 38 536 884 bytes. Uidentifiserte metadata og flagg gjør at TAF-redigering fortsatt krever mer spillanalyse. Verktøyet avviser endrede rammer og gir en presis feil.

## Konkret, nøytral grafikkrute

`original/app/DATA/BACK/START.TBF` er visuelt kontrollert som en 80x55 filmklapper med teksten START. Denne inneholder ingen person-/erotikkgrafikk. Den egner seg som første endringsforsøk i en separat spillkopi.

```sh
python3 tools/assets.py export-tbf original/app/DATA/BACK/START.TBF build/start-edit.ppm
# Rediger PPM i en bildeeditor, behold 80x55 og eksporter binær P6 med maxval 255.
python3 tools/assets.py import-tbf build/start-edit.ppm original/app/DATA/BACK/START.TBF build/START.TBF
python3 tools/assets.py list build/START.TBF
```

En uendret eksport/import skal gi samme SHA-256 som originalen. Etter en faktisk bildeendring skal den nye TBF-en validere og ha samme dimensjoner og modus. Den skal settes inn i en separat, klargjort spillkopi. Originalfilen beholdes. Det er ennå ikke dokumentert at spillet tegner en endret knapp i runtime.

For bilder som ligger i TGP:

```sh
python3 tools/assets.py unpack original/app/DATA/DIALOG/DIA_BACK.TGP build/dia-back
python3 tools/assets.py pack build/dia-back build/DIA_BACK.TGP
```

Uten endringer blir containeren byteidentisk. Erstatt en valgt utpakket TBF via samme eksport/import-rute for å repakke en endret container. Velg og kontroller innholdet i bildet før visuell redigering. Ikke erstatt vilkårlige personer/videoer som del av en teknisk test.

## Kontroller og gjenværende arbeid

```sh
python3 -m unittest discover -s tests -p test_assets.py -v
```

17 tester bestod mot denne utpakkingen: alle 10 NGS-containere, alle 339 TBF-bilder, 78 TAF-filer med 989 rammer og 43 CUT/AVI-filer, samt syntetiske kontroller av endrede payloadstørrelser, uendret kompresjon, alle RGB565-farger, ugyldige offsetfelt, over-/underløp, usikre manifeststier og beskyttelse av eksisterende filer. Original-corpus-testene hoppes eksplisitt over når proprietære originaler ikke finnes lokalt; de øvrige testene kjører fortsatt.

Dette er en verifisert ressursverktøykjede. Det gjenstår å verifisere endret grafikk i spillet, kartlegge DDF/layout, støtte trygg TAF-redigering og gjenoppbygge spillogikken fra reversert kode før en faktisk modernisert spillrekompilering kan rapporteres som ferdig.
