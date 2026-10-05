# Lula – analyse og modernisering

Repoet inneholder **144 utpakkede spillfiler** fra den oppgitte «Lula - The Sexy Empire (Repack) v2.rar», SHA-256-manifest, ressursverktøy, Ghidra-analyse og et testet Windows x86-byggemiljø for nye moduler.

**Status:** WET.EXE er statisk rekompilert til C og bygges som et native Linux-program fra dette repoet. Rekompilatoren er kontrollert instruksjon for instruksjon mot emulatoren unicorn (247 380 tilstander, ingen avvik). Hovedmenyen og første spillskjerm er identiske med originalen under Wine utenom tekst som tegnes med en annen font. Lagring og lasting virker, lyden i menyen er sample for sample lik originalens, og grafikk kan byttes ut via `--mods`. Video er satt til side, og spillet kjøres med `-novideo`. Se [rekompileringen](docs/recompilation.md).

## Bygg og kjør på Linux

```sh
git lfs pull
sudo apt install cmake libsdl2-dev python3-pip
pip install -r tools/recomp/requirements.txt
cmake -S . -B build/game
cmake --build build/game -j
build/game/lula -- -novideo
```

Konfigureringen kjører rekompilatoren (`tools/recomp`) på `original/app/WET.EXE` og bygger den genererte C-koden sammen med runtimen i `src/runtime`. Spilldata leses fra `original/app`, som aldri skrives til. Lagring og innstillinger havner i `local/save/` (endres med `--save DIR`). `--trace` logger Win32-kall. Alt+Enter veksler fullskjerm.

## Kom i gang

```sh
git lfs pull
python3 tools/project.py verify
python3 -m unittest discover -s tests -v
```

Originalfilene ligger i `original/app/` og bruker Git LFS. Størrelsen er 731 560 960 byte. Hold disse uendret; gjør grafikk- og kjøreeksperimenter i separate kataloger under `build/`.

## Grafikk: ut og inn igjen

Verktøyet bruker bare standard Python. Denne runden er testet og gir en byte-identisk kopi av den opprinnelige START-knappen:

```sh
mkdir -p build/graphics
python3 tools/assets.py export-tbf original/app/DATA/BACK/START.TBF build/graphics/START.ppm
python3 tools/assets.py import-tbf build/graphics/START.ppm original/app/DATA/BACK/START.TBF build/graphics/START.TBF
```

PPM-filen kan redigeres i et bildeprogram. Reimport bevarer bildets dimensjoner og kvantiserer til originalens RGB565-format. NGS-arkiver kan pakkes ut, få payloads byttet og bygges på nytt med oppdatert indeks:

```sh
python3 tools/assets.py unpack original/app/DATA/BACK/back.tgp build/graphics/back
python3 tools/assets.py pack build/graphics/back build/graphics/back-modified.tgp
```

Se [ressursformatene](docs/asset-formats.md) for begrensninger, animasjoner og merking av payloads.

## Dekompilering og bygging

Reell Ghidra-analyse og eksport av WET.EXE finnes under `analysis/`. Det lokale, vedvarende Ghidra-prosjektet ligger under `local/ghidra/` og kan gjenskapes med repoets eksportskript. Se [binæranalysen](docs/binary-analysis.md) for oppsett, eksakte kommandoer, funksjoner og kodehenvisninger.

```sh
tools/build_native.sh
```

Dette bygger en 32-bits Windows/DirectDraw-verktøyprobe fra `native/toolchain_probe.c`. Dekompilert C i `analysis/decompiled/` er analysemateriale som må rekonstrueres og valideres før det kan erstatte spillets kode.

## Oppløsning

```sh
python3 tools/project.py stage --profile original
python3 tools/project.py stage --profile hd1080
```

Startfilene ligger i `build/runtime-original/` og `build/runtime-hd1080/`. HD-profilen skalerer presentasjonen til 1920×1080 med bevart sideforhold. Den beholder den interne 640×480-oppløsningen. Ekte større spilleflate krever videre arbeid i renderer, koordinater og UI; det er ikke implementert her.

## Dokumentasjon

- [Oppsett, kloning og utpakking](docs/setup.md)
- [Binæranalyse og Ghidra](docs/binary-analysis.md)
- [Ressursformater og grafikkflyt](docs/asset-formats.md)
- [Videre modernisering](docs/modernization.md)
- [Utførte kontroller og grenser](docs/verification.md)

Spillet og medfølgende repack-komponenter er tredjepartsdata. Nye verktøy og rapporter er holdt separat fra disse. Den opprinnelige RAR-filen er bevart; installasjonsprogrammet er ikke kjørt.
