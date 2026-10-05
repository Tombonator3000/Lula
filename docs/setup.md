# Oppsett og gjenoppretting

Den lokale arbeidskopien ble opprettet i `/home/tombonator3000t/Documents/Codex/Lula`.
Verktøyene nedenfor er installert lokalt under `local/tools/`; systemets pakker ble ikke endret.

## Fra GitHub

```sh
git clone https://github.com/Tombonator3000/Lula.git
cd Lula
git lfs pull
python3 tools/project.py verify
```

Originalfilene bruker Git LFS (731 560 960 byte totalt). En klone med bare LFS-pekere vil avvises av hashkontrollen.
Den store installasjonsfilen fra RAR-en er ikke duplisert i Git: repoet inneholder selve de utpakkede spillfilene.
RAR-en på skrivebordet er bevart. Den lokale ekstraherte installasjonspakken ligger i `local/repack/`.

## Utpakking fra den registrerte RAR-filen

Bruk en fersk katalog uten `original/`, med repoets manifest og verktøy. `tools/unpack.sh` validerer SHA-256 før utpakking og kjører verken Setup.exe eller WET.EXE.

```sh
tools/unpack.sh "/path/to/Lula - The Sexy Empire (Repack) v2.rar"
```

På denne Ubuntu-maskinen ble innoextract 1.9-3 hentet som Ubuntu-pakke og ekstrahert med `dpkg-deb -x` til `local/tools/innoextract/`. På en annen maskin kan vanlig `innoextract` på PATH brukes, eller `LULA_INNOEXTRACT` settes til verktøyfilen.

## Windows x86-byggemiljø

`tools/build_native.sh` bruker en installert `i686-w64-mingw32-gcc`, `LULA_CC`, eller det lokale MinGW-oppsettet. Verifisert på denne maskinen med GCC 13-posix / MinGW-w64 13.0.0.

Lokalt oppsett uten systeminstallasjon på Ubuntu:

```sh
mkdir -p local/tools/mingw-packages local/tools/mingw
cd local/tools/mingw-packages
apt download gcc-mingw-w64-i686-posix gcc-mingw-w64-i686-posix-runtime gcc-mingw-w64-base binutils-mingw-w64-i686 mingw-w64-i686-dev mingw-w64-common
for package in *.deb; do dpkg-deb -x "$package" ../mingw; done
cd ../../..
tools/build_native.sh
```

Verktøybyggingen lager `build/native/lula-toolchain-probe.exe`, en 32-bits PE-fil som lenker `DirectDrawCreate`. Denne kontrollerer kompilator og Windows/DirectDraw-bibliotekene. Den er ikke en gjenoppbygd WET.EXE. Det finnes foreløpig ingen spillkode som kan kompileres til en komplett erstatning.

Se `docs/binary-analysis.md` for Ghidra-oppsett og reell dekompilering.

## Kjøringskopier

```sh
python3 tools/project.py stage --profile original
python3 tools/project.py stage --profile hd1080
```

Dette lager separate kataloger `build/runtime-original/` og `build/runtime-hd1080/`. Et eksisterende mål avvises for å bevare lagringsfiler; velg et nytt mål med `--output build/ny-kopi` ved behov.
Kjøringskopiene får også tomme `DATA/SAVE`- og `DATA/DATABASE`-mapper: disse stiene finnes i WET.EXE, mens installasjonspakken har lagt sine tomme mapper ett nivå dypere. Den utpakkede baselinen beholdes uendret.
Bruk `start-windows.cmd` på Windows eller `start-linux.sh` med Wine. Linux-startfilen velger medfølgende `ddraw.dll` før Wine sin innebygde DLL. Wine er ikke installert eller testet i denne leveransen.

HD-profilen setter cnc-ddraw til 1920×1080 med bevart sideforhold og fjerner en henvisning til en shaderfil som ikke finnes i pakken. Den endrer ikke spillets interne 640×480-oppløsning eller spillkode. Valget `-novideo` finnes i spillets egen README og kan benyttes ved senere feilsøking.
