# Redigering av TAF-animasjonsrammer

`tools/assets.py` eksporterer og importerer enkeltbilder som binær P6 PPM.
Import beholder rammetall, dimensjoner, RGB565, metadata og rekkefølge. En
endret komprimert strøm kan få annen lengde; verktøyet beregner da alle
absolutte ramme- og strømpekere og den valgfrie sluttindeksen på nytt.
Uendret import returnerer hele originalfilen byteidentisk, også med
ikke-kanoniske RLE-kommandoer.

## Bevis fra de originale leserne

Feltkartet er kontrollert mot `WET.EXE`, særlig `0x43eca6` (én ramme),
`0x43ef7b` (alle rammer), `0x43f477` (første sluttabell), `0x43f5bf`
(rammeindeks) og `0x434ec2` (komplett animasjon). Alle tall er little-endian.

| Felt | Offset | Tolkning og behandling |
|---|---:|---|
| Signatur | 0 | `TAF\0` |
| Pikselformat | 4 / u16 | 16 i originalene, RGB565; andre verdier avvises |
| Rammetall | 6 / u16 | Beholdes |
| Dekodet totalstørrelse | 8 / u32 | Sum av `bredde * høyde * 2`; beholdes ved samme dimensjoner |
| 768-byte blokk | 12–779 | Beholdes byteidentisk; denne redigeringsruten tolker den ikke som RGB565-palett |
| Første ramme | 780 / u32 | Absolutt rammeoffset; 787 i alle 78 originalfiler |
| Antall sluttabeller | **784 / u16** | 1 i 25 filer og 2 i 53 filer |
| Ekstrabyte | 786 | Beholdes; de undersøkte leserne leser 786-byte header og søker deretter til første ramme |
| Rammemodus | ramme + 0 / u16 | 2, RGB565-RLE |
| Bredde/høyde | ramme + 2, +4 / u16 | Beholdes |
| Neste ramme | ramme + 6 / u32 | Absolutt start på neste ramme, eller slutt på siste strøm |
| Strømstart | ramme + 10 / u32 | Absolutt `rammestart + 15` |
| Ekstrabyte | ramme + 14 | Beholdes; leserne leser 14-byte rammeheader og søker til strømstart |
| Første sluttabell | etter siste ramme / `rammetall * 4` | Fire metadatabytes per ramme; kopieres av `0x43f477`, beholdes byteidentisk |
| Andre sluttabell | siste `rammetall * 4` bytes, når tabellantall = 2 | Absolutte rammestarter; bygges på nytt når strømstørrelser endres |

Offset **784** korrigerer den eldre henvisningen til `0x30e`: instruksjonen
ved `0x43ed88` leser en dword ved header + `0x30e` og skifter aritmetisk 16
bit til høyre. Feltet som faktisk brukes er dermed word ved `0x310`.
`0x43f477` søker `-(rammetall * 4 * tabellantall)` fra EOF og leser én
tabell. `0x43eca6` bruker siste tabell ved tabellantall >= 2, og følger
rammelenkene ellers. Derfor må både lenker og sluttindeks oppdateres.

De undersøkte leserne gir ikke et sikkert navn på de fire metadatabytene
per ramme eller ekstrabytene. De bevares uten redigeringsstøtte. Uendret
rå deling/repakking støttes som før; en faktisk bildeendring krever at
headerens tabellantall samsvarer med én eller to observerte sluttabeller.

## RLE og tom sluttramme

Den originale dekoderen sign-utvider kommandoordet ved `0x43fa48`.
`0x0000..0x7fff` gjentar neste pikselord; `0x8001..0xffff` kopierer
`65536 - kommando` bokstavelige ord. Kommando `0x8000` avvises, fordi
originalens 16-bit negasjon av `INT16_MIN` ikke gir et positivt antall.
En nullkommando konsumerer pikselordet og produserer ingen piksler.
Encoder bruker gjentakelser på høyst 32767 og literalblokker på høyst
4096 piksler. Alle strømmer må dekode til nøyaktig rammens pikselantall.

`BUTCH.TAF` har én siste 0x0-ramme med bare 14 bytes. Begge offsetfeltene
peker én byte forbi faktisk slutt, til `rammestart + 15`. Relokering
beholder dette særtilfellet og lager ingen ekstra byte. Den tomme rammen
har ingen bildeeksport og kan ikke importeres som PPM. Rå deling/repakking
og tom RGB565-utskifting bevarer den. Dermed omfatter bildedelen 988
rammer, mens komplett bytebevaring omfatter alle 989.

## Bruk

Rammeindeksen er nullbasert. Originalen brukes som mal; utdata må være en
ny fil. Det gamle `unpack`/`pack`-formatet for rå TAF-deler er fortsatt
bytebevarende og avviser endrede rå deler.

```sh
python3 tools/assets.py export-taf original/app/DATA/CURSOR/CURSOR.TAF 0 build/cursor-frame.ppm
# Rediger P6-bildet i en bildeeditor; behold dimensjonene.
python3 tools/assets.py import-taf build/cursor-frame.ppm original/app/DATA/CURSOR/CURSOR.TAF 0 build/CURSOR.TAF
python3 tools/assets.py list build/CURSOR.TAF
```

Python-API:

```python
ppm = taf_frame_to_ppm(original_taf, frame_index)
edited_taf = ppm_to_taf(ppm, original_taf, frame_index)
edited_taf = replace_taf_frame(original_taf, frame_index, rgb565_bytes)
```

PPM utvider RGB565 med bitreplikasjon; import bruker de høyeste 5/6/5
bitene fra RGB. Dette er samme konvertering som TBF-ruten og gir eksakt
tilbakeføring av alle originale RGB565-ord. Pikselord 0 brukes som
gjennomsiktighet av originalens spriteblitter, for eksempel `0x444906`.
PPM har ingen egen alfakanal.

Den rekompilerte versjonen kan lese en endret fil gjennom `--mods`, med
samme relative filsti som originalen. Formatvalidering bekrefter filens
struktur; en runtime-kontroll må i tillegg bekrefte det valgte bildet og
spillets normale animasjonsflyt. Verktøyet endrer ikke koordinater,
treffområder, timing eller intern spilleflate.

## Utførte kontroller

- Alle 78 originalfiler og 989 rammer er analysert. 988 PPM-eksporter og
  uendrede importer returnerte hele originalfilen byteidentisk; den ene
  0x0-sentinelen ble bevart gjennom rå RGB565-utskifting og avviste
  bildeeksport.
- Ett endret pikselord i første ikke-tomme ramme i hver av de 78 filene
  validerte. 66 filer fikk annen komprimert størrelse. Prefix, første
  sluttabell, dimensjoner, ekstrabytes og andre rammers komprimerte
  strømmer ble kontrollert uendret; alle flyttede lenker og indekser
  validerte.
- En uniform strøm med 70 000 piksler ble kodet som gjentakelser på
  32767, 32767 og 4466, og dekodet eksakt. En enkelt negativ
  literalkommando med 5000 piksler dekodet eksakt; `0x8000` ble avvist,
  og nullkommandoen beholdt sin oppførsel.
- CLI-eksport/import av CURSOR.TAF ga byteidentisk fil. Eksisterende
  utdata, ugyldige rammeindekser og endrede dimensjoner ble avvist.
- Den portable C-modulen bestod 989 uendrede API-rundturer og 988
  PPM-rundturer mot Python, alle 65 536 RGB565-verdier og åtte endringer
  med byteidentisk C-/Python-resultat. 37 avvisningskontroller dekker
  ugyldige headere, rammer, indekser, dimensjoner og eksisterende utdata.
  Hele korpuset ble kjørt med AddressSanitizer/UBSan; de fire sist
  tilføyde headerkontrollene ble også kjørt separat med disse kontrollene.
- Spillet bygget på Claudes `e0b3a2c` viste den endrede TAF-filen via
  `--mods`. Alle fire markørrammer ble endret til RGB565 `0x1234`, med
  original størrelse 39×76. Filen krympet fra 11 271 til 879 bytes.
  Presenterte spillbilder fikk nøyaktig 2 964 nye piksler med denne
  fargen, uten trap/fatal. Kontroll og modifikasjon brukte nye
  lagringsmapper; originalfiler og tidligere lagringer ble bevart.

Gjenta filkontrollene med `tools/build_reconstruction.sh` og
`python3 tools/reconstruction/verify_taf.py`. For minnekontroll, bygg med
`--sanitize` og gi skriptet `--cli
build/reconstruction/lula-resource-cli-sanitize --api-check
build/reconstruction/lula-taf-check-sanitize`. Etter separat CMake-bygg av
spillet gjentas runtime-kontrollen med
`python3 tools/reconstruction/verify_taf_runtime.py`.

Kontrollene bekrefter redigeringsruten og den testede markørvisningen.
Nye illustrasjoner, intern oppløsningsendring og langvarig gjennomspilling
inngår ikke i denne leveransen.
