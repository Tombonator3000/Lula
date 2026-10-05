# Rekonstruerte ressursverktøy i C11

`reconstruction/resources/` inneholder en selvstendig byggbar ressursmodul og et kommandolinjeverktøy for NGS, TBF og TAF. Koden er rekonstruert fra binærformatene som er kontrollert mot originalfiler og spillets lesere, med `tools/assets.py` som Python-referanse. Modulen brukes til ressursredigering. Selve spillet bygges separat med CMake, fra Claudes statiske rekompilering i `src/runtime/` og `tools/recomp/`; se [recompilation.md](../recompilation.md).

## Bygging og bruk

```sh
tools/build_reconstruction.sh
build/reconstruction/lula-resource-cli inspect-ngs original/app/DATA/BACK/back.tgp
build/reconstruction/lula-resource-cli decode-tbf original/app/DATA/BACK/START.TBF build/START-native.ppm
build/reconstruction/lula-resource-cli extract-ngs original/app/WET.DDF 0 build/WET-entry-0.bin
build/reconstruction/lula-resource-cli inspect-taf original/app/DATA/CURSOR/CURSOR.TAF
build/reconstruction/lula-resource-cli decode-taf original/app/DATA/CURSOR/CURSOR.TAF 0 build/cursor.ppm
# Rediger P6-bildet med samme dimensjoner; importer til en ny fil.
build/reconstruction/lula-resource-cli import-taf original/app/DATA/CURSOR/CURSOR.TAF 0 build/cursor.ppm build/CURSOR.TAF
```

Byggeskriptet lager en kjørbar hostversjon og, når MinGW er tilgjengelig, `build/reconstruction/lula-resource-cli.exe` for Windows x86. Windows-bygging alene bekrefter ikke at verktøyet er kjørt på Windows/Wine.

På denne maskinen er host-GCC 15.2 og MinGW/GCC 13 plassert lokalt under `local/tools/`; ingen systempakker er installert. Skriptet finner `cc`, dette lokale GCC-oppsettet eller `LULA_HOST_CC`. For et tilsvarende lokalt Ubuntu-oppsett kan pakkene `gcc-15-x86-64-linux-gnu` og `libgcc-15-dev` hentes med `apt download` og pakkes ut med `dpkg-deb -x` til `local/tools/host/`; systemets eksisterende CPP, binutils og C-headere brukes. Sanitizer-byggingen trenger også `libasan8` og `libubsan1` under samme lokale prefix.

```sh
tools/build_reconstruction.sh --sanitize
LULA_RESOURCE_CLI="$PWD/build/reconstruction/lula-resource-cli-sanitize" python3 -m unittest discover -s tests -p test_native_resources.py -v
```

Den tidligere NGS/TBF-kontrollen bestod alle sju tester både vanlig og med AddressSanitizer/UBSan; den historiske rapporten finnes i `analysis/reconstruction/resource-validation.json`. TAF-utvidelsen har egne korpus-, minne- og runtime-kontroller beskrevet nedenfor og i [taf-editing.md](taf-editing.md). Windows x86-bygging av ressursverktøyet er kontrollert; Windows/Wine-kjøring og Windows-bygg av hele spillet er ikke kontrollert her.

`decode-ngs INPUT INDEX OUTPUT.ppm` dekoder en valgt TBF-payload fra en NGS-container. `INDEX` er et nullbasert desimaltall. `inspect-ngs` skriver JSON med formatord, postantall, filstørrelse og samtlige postoffsets, størrelser og typeord. `extract-ngs` kopierer kun den valgte payloaden, uten postheaderen. Ukjente typeord beholdes og tolkes ikke som spilldata.

Alle filutdata må være nye. Oppretting skjer eksklusivt, med C11s `fopen(..., "wbx")` på host og `_open(..., _O_EXCL)` på Windows for kompatibilitet med eldre CRT-er. Dermed bevares eksisterende filer og symlinkmål også når outputbanen er lik inputbanen. Ved skrivefeil forsøker verktøyet å fjerne den nye, ufullstendige filen. Verktøyet lager ikke manglende overordnede kataloger.

Kontroll av originalbildene er en teknisk pikselkontroll. Runtime-kontrollen bruker en ensfarget markør i original størrelse; den er et bevis på redigeringsruten, ikke ny produksjonsgrafikk.

## Modulens API og levetider

- `lula_blob_read/free` leser en fil til en eid bytebuffer og frigjør den.
- `lula_ngs_parse/close` validerer hele containeren og bygger en eid indeks. Selve inputbufferen lånes: behold den uendret og levende til containeren lukkes.
- `lula_ngs_payload` returnerer en lånt peker, lengde og valgfritt typeord til valgt post.
- `lula_tbf_decode/lula_image_free` lager/frigir et bilde med eide RGB565-pikselord i hostens byteorden.
- `lula_taf_parse/close` bygger/frigjør en eid rammeindeks og låner den uendrede inputbufferen.
- `lula_taf_decode_frame` dekoder valgt ramme til et eid RGB565-bilde.
- `lula_taf_replace_frame` lager en eid `LulaBlob` med et bilde av samme dimensjoner og oppdaterte absolutte offsets. Uendrede piksler gir en kopi av hele originalfilen byteidentisk.
- `lula_ppm_decode` leser P6 med maxval 255 til et eid RGB565-bilde, ved å beholde de høyeste 5/6/5 bitene.
- `lula_ppm_write_new` utvider pikslene til RGB888 ved bitreplikasjon og skriver binær P6 PPM. `lula_blob_write_new` skriver en valgt payload til en ny fil.

Funksjonene returnerer 1 ved suksess og 0 ved feil. En valgfri `LulaError` mottar en konkret melding. Initialiser outputstrukturer til null, og frigjør/lukk dem før gjenbruk. Frigjør bilder og eksporterte bufferkopier, deretter NGS-/TAF-indeks, og til slutt inputbufferen. Grensesnittet finnes i `lula_resources.h` og krever bare standard C-typer; Win32-avhengigheten for eksklusiv filoppretting ligger bak `_WIN32` i implementasjonen.

## Validering og grenser

NGS-leseren kontrollerer signaturen, at slutttabellen ligger etter headeren, at alle tabelloffsets peker nøyaktig seks bytes etter neste postheader, og at payloaden slutter før indeksen. Den krever at postsekvensen treffer indeksstart uten restbytes. NGS-formatord og posttypeord bevares uten å innføre udokumenterte semantiske regler. Tomme arkiver og payloads støttes.

TBF-leseren støtter versjon 16 og modus 0/2. Dimensjonene må være positive, og oppgitt råstørrelse må være nøyaktig `bredde * høyde * 2`. Modus 0 krever eksakt rasterlengde. Modus 2 validerer hver RLE-kommando og all konsumert input, inklusive nullkommandoer, bokstavelige pikselord, gjentatte pikselord og eksakt produsert pikselantall. Ukjente modi, avkuttet input, restbytes i råbilder og ekspansjon utenfor bildet avvises.

TAF-leseren støtter versjon 16, rammemodus 2 og én eller to sluttabeller. Header, rammer, indekser og deklarert dekodet totalstørrelse valideres. Metadata og ukjente ekstrabytes bevares. Import beholder dimensjoner og rammetall. Den originale terminale 14-byte 0x0-rammen i `BUTCH.TAF` bevares som et tomt API-bilde; PPM-eksport/import av tomme bilder avvises. Feltene og signed RLE-kommandoene er dokumentert i [taf-editing.md](taf-editing.md).

For å unngå ubegrensede allokeringer er filgrensen 256 MiB og bildegrensen 16 777 216 piksler. Den største originale NGS-filen er `MUSIC.TAP` på 200 432 490 bytes. Bildegrensen er samme som i Python-referansen. u16-dimensjoner betyr ikke at en 65535×65535-allokering tillates. Alle størrelseskontroller gjøres før allokering eller pekertilgang, og bytefelt leses eksplisitt som little-endian uten alignmentkrav.

## Verifikasjon

```sh
python3 -m unittest discover -s tests -p test_native_resources.py -v
python3 tools/reconstruction/verify_taf.py
python3 tools/reconstruction/verify_taf.py --cli build/reconstruction/lula-resource-cli-sanitize --api-check build/reconstruction/lula-taf-check-sanitize
# Krever separat CMake-bygging av spillet.
python3 tools/reconstruction/verify_taf_runtime.py
```

De sju testene i `tests/test_native_resources.py` kontrollerer følgende mot den bygde host-CLI-en:

- Alle ti originale NGS-containere: samtlige indeksfelt og byteidentisk uttrekk av første, midterste og siste post, uten duplikatuttak i enpostarkiver.
- Alle 339 originale TBF-bilder: C-produsert PPM, inklusive alle RGB888-piksler, må være byteidentisk med Python-referansen. Begge observerte modi dekkes: 122 rå og 217 RLE-bilder.
- Alle 65 536 RGB565-verdier, store repeat-/literal-sekvenser, nullkommandoer og gyldig ikke-kanonisk RLE.
- Feil i signatur, indeks, postlengde, versjon, modus, dimensjoner, råstørrelse og RLE; avkuttede og for store filer; ugyldige indeksargumenter.
- Bevaring av eksisterende outputfiler, inputfiler og symlinkmål.

Mangler host-CLI-en, hoppes disse testene eksplisitt over. Mangler originalkorpuset, hoppes de to korpustestene over. `LULA_RESOURCE_CLI` kan peke på en annen hostbygging for samme testsett, eksempelvis en bygging med minnekontroller. At tester hopper over manglende avhengigheter er ikke dokumentasjon på at dekoderen er validert.

TAF-kontrollen dekker alle 78 filer og 989 rammer, inkludert 988 byteidentiske PPM-rundturer, én tom sluttramme, alle 65 536 RGB565-verdier, åtte byteidentiske endringsresultater mellom C og Python og 37 avvisningskontroller. Runtime-kontrollen måler at den redigerte markøren når et presentert spillbilde gjennom `--mods`.

Verktøyene klargjør samme-størrelse grafikkredigering. HD-illustrasjoner, endret intern oppløsning og Windows-bygg av hele spillet inngår ikke i denne leveransen. Modulen dekoder ikke DDF-payloadfelter, lyd eller video.
