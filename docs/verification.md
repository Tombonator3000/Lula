# Klargjøring kontrollert 5. oktober 2026

| Kontroll | Resultat |
|---|---|
| RAR og Inno-pakke pakket ut uten å kjøre installasjonsprogrammet | PASS |
| 144 spillfiler, 731 560 960 byte, registrert med SHA-256 | PASS |
| Alle originalfiler urørt etter arbeid | PASS |
| Hele NGS-korpuset: 10 arkiver byte-identisk etter utpakking/repakking | PASS |
| Alle 339 TBF-bilder: dekoding, PPM-eksport og byte-identisk reimport | PASS |
| Alle 65 536 RGB565-fargeverdier: eksakt PPM-tilbakeføring | PASS |
| Endret NGS-payloadstørrelse, indeks, TBF-piksler og feilaktige inndata | PASS i syntetiske tester |
| 78 TAF-filer / 989 rammer: strukturkontroll og bytebevaring | PASS |
| 43 CUT-filer: konsistente RIFF/AVI-containere | PASS; videoavspilling ikke testet |
| Ghidra: faktisk analyse, lagret prosjekt og gjenåpning | PASS |
| Ghidra: 944 av 945 funksjoner med fullført eksport | PASS med én dokumentert ufullstendig funksjon |
| PE-importer sammenlignet med uavhengig verktøy | PASS, 143 av 143 |
| MinGW: Windows i386-fil bygget og lenket med DirectDrawCreate | PASS, verktøyprobe |
| Separat original-/hd1080-staging og originalhash etter staging | PASS |
| Beskyttelse av eksisterende lagringsmapper og symlink til original/utenfor prosjekt | PASS |
| Samlet testkjøring | PASS, 20 av 20 tester |
| Kjøring av selve spillet på Windows/Wine | UNVERIFIED |
| Redigert grafikk i faktisk spill | UNVERIFIED |
| Full rekompilering av WET.EXE | Ikke implementert; analysekode er ikke byggbar spillkilde |
| Ny intern HD-oppløsning | Ikke implementert; hd1080 er visningsskalering |

Den frittstående START-knappen og `back.tgp` ble også eksportert/pakket i `build/asset-proof/` og sammenlignet direkte med originalfilene. SHA-256 og verktøybyggets filidentitet er bevart i `analysis/preparation-checks.json`. Ghidra-kontrollene, eksportenes identitet og kjente analysegrenser finnes i `analysis/binary/verification.json` og `docs/binary-analysis.md`.

Feiltilfeller inkluderer endret/manglende original, ekstra originalfil, eksisterende utdata, symlink som flytter staging utenfor `build/`, ugyldige arkivindekser, avkuttede RLE-strømmer, feil bildedimensjon og endringer i foreløpig ikke støttet TAF-innhold.

## Gjenta kontrollene

```sh
git lfs pull
python3 tools/project.py verify
python3 -m unittest discover -s tests -v
git lfs fsck
tools/build_native.sh
```

En ny Ghidra-eksport kan kjøres med `tools/decompile.sh`. Det lokale prosjektet og analyseeksporten er allerede opprettet. Dette kjører ingen spillkode.

Ved senere runtime-test brukes en separat kjøringskopi og faktisk brukerinput. Test meny/start, START-knappens museområde, vindusstørrelse, lyd/video samt lagring/lasting før modifiserte assets eller en ny renderer kalles spillbare.
