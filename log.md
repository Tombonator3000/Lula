# Arbeidslogg

Kronologisk logg over alt som er gjort i repoet. Nyeste oppføring nederst. Tider i UTC.

## 2026-10-05 16:04 UTC: ny økt (Claude Code, gren `claude/eager-ride-xckkd4`)

- Oppdrag fra brukeren: reverse engineering, grafikkutskifting og videre rekonstruksjon. Hele spillet skal rekompileres og kunne kjøres på Linux, helst rett fra repoet.
- Leste `AGENTS.md`, `README.md` og alle filer under `docs/`.
- Prøvde å åpne ChatGPT-lenken brukeren delte (`chatgpt.com/s/cx_...`). Siden svarer med 403 fra Cloudflare både via WebFetch, curl og headless Chromium, så innholdet er ikke lest. Commit `70aae36` ser ut til å være resultatet av den økten, så jeg bygger videre på det som ligger i repoet.
- `git lfs pull`: 698 MB originaldata hentet.
- `python3 tools/project.py verify`: PASS, 144 filer stemmer med SHA-256-manifestet.
- `python3 -m unittest discover -s tests`: 20 av 20 tester OK.
- Fant at containeren har gcc 13, clang, cmake, ffmpeg og Xvfb. SDL2, Wine, MinGW og capstone mangler og blir installert.

## 2026-10-05 16:20-16:55 UTC: statisk rekompilering og runtime

- Instruksjonstelling: koden bruker 118 ulike x86-mnemonikker og 249 operandformer. Det gjør full statisk rekompilering realistisk.
- Skrev `tools/recomp/`: PE-laster (`pe.py`), kodeoppdagelse med hopptabeller og pekere fra relokeringstabellen (`cfg.py`), interprosedural flagg-liveness (`flags.py`) og x86-til-C-oversetter (`lift.py`). Resultat: 1415 funksjoner, ca. 78 100 instruksjoner, 85 hopptabeller, ingen uløste indirekte hopp.
- Startet en bakgrunnsanalyse (6 agenter og en kritiker) som skriver spesifikasjoner for alle Win32/DirectX-kall til `docs/recomp/specs/`. Video, AVI, OLE32 og MCI holdes utenfor inntil brukeren har avklart det.
- Startet en agent som verifiserer lifteren instruksjon for instruksjon mot unicorn (`tests/recomp/`). Den har også skrevet `src/runtime/rt_fpu.c`.
- Referansekjøring under Wine (egen agent): originalen når hovedmenyen etter ca. 8,6 s med `-novideo` når cnc-ddraw bruker `renderer=gdi`. Skjermbilder, API-sekvens og trace ligger lokalt i `build/wine-ref/`.
- Skrev runtime i `src/runtime/`: 4 GiB gjesteminne, PE-laster med SHA-256-kontroll, dispatch mellom gjest og vert, tråder under en global lås, filsystem-overlay (originalen er skrivebeskyttet, skriving går til `local/save/`) og KERNEL32, USER32, GDI32 (tekst via stb_truetype og Liberation Sans), WINMM, DDRAW, DSOUND, OLE32 og COMDLG32 på SDL2.
- Svarte Codex på koordineringsspørsmålet og foreslo at Codex tar TAF-redigering (animasjonsgrafikk).
- La til `CMakeLists.txt`. Konfigureringen kjører rekompilatoren og bygger `build/game/lula`.
- Første kjøring: spillet når hovedmenyen, men med forskjøvet bilde, svarte prikker og uten knapper. Årsak: capstone dropper F2-prefikset på `f2 a5` (`repnz movsd` i Watcoms memcpy ved 0x442be4). Rettet i `cfg.py` ved å lese prefiksbytene direkte. Fem steder var berørt.
- Etter rettingen: hovedmenyen er identisk med Wine-referansen i alle piksler utenfor bokstavene på de fem knappene (3344 av 307 200 piksler avviker, alle i teksten).
- `python3 tools/project.py verify`: PASS etter arbeidet.

## 2026-10-05 17:00 UTC: første spillskjerm

- Committet og pushet milepælen (`643b147`) til `claude/eager-ride-xckkd4`.
- Skriptet klikk på "New game" (505,88) i den rekompilerte versjonen. Spillet går til byoversikten. Mot Wine-bildet `after_newgame_1.png` avviker 423 av 307 200 piksler, alle i teksten "F1-Help" (font) og i klokkeslettet på statuslinjen (spilltiden går i sanntid).

## 2026-10-05 17:10 UTC: timer, tester og grafikkutskifting

- Spesifikasjonene for KERNEL32 og USER32/GDI32 er ferdige (`docs/recomp/specs/`). Viktigste funn: spillogikken går på WM_TIMER (60 ms), lagring og lasting skjer i ekte Win32-dialoger, og museklikk må komme som WM_LBUTTONDBLCLK ved dobbeltklikk.
- USER32: WM_TIMER følger nå Windows-semantikken (klar ved hver periode, forankret til SetTimer, tapte perioder slås sammen), dobbeltklikk syntetiseres, WM_ACTIVATEAPP sendes synkront, lukking av vinduet avslutter programmet. Spillklokken går nå like fort som i originalen.
- KERNEL32: pseudohåndtak for stdout/stderr slik at Watcom-runtimens feilmeldinger kommer ut.
- Startet en agent som lager dialogbehandleren (lagring, lasting, bank, lister, tekstfelt) og popupmenyen. Den eier `user32.c`, `gdi32.c`, `res.c` og nye `dialog.c` til den er ferdig.
- La til `--mods DIR`: en katalog som leses før `original/app`. Testet med endret hovedmenybilde (post 71 i `DIA_BACK.TGP`): de 4000 grønne pikslene vises nøyaktig der de skal.
- Ny test `tests/test_recompiled_game.py` (3 tester: hovedmeny mot originalens hash, New game, grafikk via mods). Alle går grønt.
- Lyd: `LULA_AUDIODUMP=fil.wav` skriver den miksede lyden. I menyen er utgangen nøyaktig `sound.tap` post 9 (22050 Hz, 8 bit) spilt med -10 dB, sample for sample. Lagt inn som fjerde ende-til-ende-test.
- Rekonstruksjon: funksjoner merket `RT_RECONSTRUCTED(0x...)` i `src/reconstructed/` erstatter de genererte (som beholdes som `lifted_...`). Første eksempel er Watcoms memcpy (0x442be4). Alle fire ende-til-ende-tester går grønt med den.
- Nytt verktøy `lula-fncheck` (CMake-mål): kjører hver rekonstruerte funksjon og den genererte originalen på tilfeldige tilstander og sammenligner registre, levende flagg, x87 og minne. memcpy består 3000 av 3000. CMake bruker nå et objektbibliotek (`lula_core`) som deles av spillet og verktøyene.
- Den globale låsen kan nå overleveres ved retur fra hvert Win32-kall når en annen tråd venter, slik at 2 ms-timeren for lyd ikke sultes i travle PeekMessage-løkker. Alle 5 ende-til-ende-tester går grønt.
- DirectX-spesifikasjonen er ferdig. Viktigst: hovedtråden spinner på flagg som lydtimeren (2 ms, egen tråd) nullstiller (0x437451, 0x4376b8, 0x438cbb). Generert kode kaller nå `RT_POLL()` ved hvert bakoverhopp (3016 steder), så en ventende tråd slipper til. FlipToGDISurface bytter tilbake til den opprinnelige primærbufferen og viser den, som spesifikasjonen krever.
- Verifiseringsagenten fant og rettet en feil i flaggfusjonen: flagg som leses ved hoppmål inne i en sammensmeltet `cmp`/`jcc`-kjede ble ikke alltid materialisert. `rt_fprem` gir nå også kvotientbitene i C0/C3/C1.
- Testet i et eget git-worktree (dialogagentens halvferdige `user32.c` lenket ikke i hovedtreet): alle 5 ende-til-ende-tester grønne.
- Rekompilatoren gir nå en tydelig melding hvis `analysis/decompiled/functions.tsv` bare er en Git LFS-peker.

## 2026-10-05 17:40 UTC: lifteren verifisert mot unicorn

- Verifiseringsagenten er ferdig: 65 446 instruksjoner i 245 grupper (185 fullt testet, 60 med 60 utvalgte forekomster inkludert alle kodinger), alle 6 709 sammensmeltede flaggkjeder og 43 x87-sekvenser. 247 380 tilstander sammenlignet med unicorn, 0 feil. 49 bevisst innførte feil ble alle fanget.
- Rettet: `aam` og `les` var feilfeller (finnes i Watcoms tall-til-tekst og printf), `fprem` ga ikke kvotientbitene, og to latente feil i flaggfusjonen. Rekompilatoren melder nå 0 ikke-støttede instruksjonstyper.
- Kjent avvik: x87 holdes i `double`, men Watcom setter 64-bits presisjon. Agenten har fått i oppgave å gjøre x87 80-bit-eksakt med `long double` og sammenligne eksakt mot unicorn.
- Kjør på nytt: `python3 tests/recomp/unicorn_diff.py` (ca. 3 min) eller `--quick`.
- WINMM/timing-spesifikasjonen er ferdig. Etter den: timertråden (lydmotoren, 2 ms) kjører nå hver tick ferdig uten å gi fra seg den globale låsen, slik en tidskritisk timertråd på en Win9x-maskin med én CPU gjorde. Spillets volumglidebryter (`waveOutSetVolume`) virker nå som master-volum på vår egen miks, aldri på systemmikseren. Timertrådens stakk ligger over hovedtrådens stakkgrense, som Watcoms stakksjekk krever (gitt av rekkefølgen trådene lages i).

## 2026-10-05 18:00 UTC: dialoger, lagring og lasting

- Dialogagenten er ferdig (`src/runtime/win32/dialog.c`, ca. 2800 linjer): modale Win32-dialoger fra EXE-ressursene med STATIC, EDIT, LISTBOX og BUTTON, hele WM_CTLCOLOR-protokollen, nestede dialoger, popupmenyen "Set Digital Output" og ekte MessageBox. Sammenlignet med Wine-skjermbilder av originalens dialoger.
- Lagring og lasting virker: lagret fra F2-menyen til `DATA/SAVE/SAVEGAME.  4` i lagringskatalogen og lastet inn igjen etter omstart.
- Rettet krasj ved `ExitProcess`: SDL ble avsluttet fra spilltråden. Nå ber spilltråden hovedtråden om å avslutte.
- Inputskript kan nå bruke F1-F12 og `type ORD`. Ny ende-til-ende-test lagrer via F2 og laster fra hovedmenyen. Alle 6 testene grønne.
- Ressurs- og spillkartspesifikasjonen (`docs/recomp/specs/resources-and-game-map.md`) er ferdig.
- Brukeren ba om at ChatGPT hjelper til. Jeg kan ikke nå ChatGPT direkte, så oppgavene står i `docs/samarbeid-codex.md`: rekonstruksjon av ressursleserne (0x43f7c1, 0x442431, 0x43ebd3, 0x440041, 0x43e8eb) kontrollert med `lula-fncheck`, TAF-redigering og et valgfritt Windows-bygg. `reconstructed.h` har fått `rt_call_guest` og `rt_return_pop` for håndskrevet kode som kaller andre spillfunksjoner.

## 2026-10-05 18:30 UTC: x87 bit-eksakt, kodekart

- x87-agenten er ferdig. Funn: spillet regner med 53-bits presisjon (Watcom setter kontrollord 0x127F ved oppstart), ikke 64. Registrene holdes nå som 80-bits `long double`, vertsprosessorens x87-kontrollord følger spillets (presisjon og avrunding), og fsin, fcos, fyl2x, fprem, fist og frndint bruker vertsinstruksjonene på x86. Full sammenligning med unicorn: 247 560 tilstander, 0 feil; 8 882 tilstander som før tapte presisjon er nå bit-eksakte. Bestått også ved 64, 53 og 24 bits presisjon.
- Mine valg etter rapporten: nye tråder starter med kontrollord 0x27F som på Windows, og generert kode bygges med `-frounding-math`. Bygg, `lula-fncheck` og alle 6 ende-til-ende-tester grønne.
- Kodekartet (`docs/recomp/specs/code-discovery.md`) er ferdig.
- Kjent begrensning: transcendentale funksjoner gir det vertsprosessoren gir (kan avvike i siste bit fra en Pentium fra 1997). ARM64 og andre verter mister 80-bits presisjon (byggevarsel).
- Etter kodekartet: 0x44c3c3 er ekte kode (en stubb som henter egen adresse, etterfulgt av en tabell med tierpotenser) og var feilaktig svartelistet. Alle `%f`/`%e`/`%g` i sprintf krasjet derfor, blant annet versjonsvisningen på F7. Stubben er nå håndskrevet i `src/reconstructed/watcom_crt.c`. 0x44cf80 (strengen "Sleep" fra en binærpatch) er svartelistet. Hopptabellgrensen godtar nå 8/16-bits sammenligninger og registerkopier (tabellene 0x441414 og 0x4419e4 har 8 og 6 innganger, ikke 16 og 12).
- Ny test trykker F7 i spillet. Den feiler på det gamle bygget med nøyaktig denne feilfellen og går grønt nå. 8 ende-til-ende-tester og `lula-fncheck` (2 funksjoner) grønne.
- Dekning: generert kode teller kall per funksjon (`RT_COV`), og `LULA_COVERAGE=fil` skriver dem ut. `tools/coverage_report.py` slår sammen kjøringer og rapporterer per modul. Testene og to utforskende kjøringer har kjørt 482 av 1261 levende funksjoner (38,2 %). Svakest dekket: rom, dialoger og simulering (20,7 %).

## 2026-10-06 00:30 UTC: utforskning av hele spillet, musfeil rettet, Codex-arbeid flettet inn

- Fem agenter spilte gjennom kontoret, bank og eiendomsmeglere, byråene, bydelene i trinn 1 og simuleringen (dag, måned, år) med skriptet input, i rundt 400 kjøringer. Alle tre trinnene ble nådd, også flyplassen og sluttscenen. Ingen feilfeller, ingen manglende importer. Dekningen gikk fra 482 til 955 av 1261 levende funksjoner (75,7 %), rom/dialoger/simulering fra 20,7 % til 76,5 %. Oppsummert i `docs/recomp/exploration.md`.
- Disken ble full av bildedumper mot slutten. Rapportene fra bydel- og simuleringsagenten og feilrettingssteget gikk tapt; scenariene og dekningsfilene deres overlevde. Jeg ryddet build/explore (bildedumper og en kopi av spilldataene) og tok feilrettingen selv.
- Rettet (`7116e60`): etter en modal dialog brukte spillet museposisjonen fra før dialogen, så neste klikk uten musebevegelse traff feil sted (åpnet innskuddsdialogen igjen, eller gikk inn i en bygning etter lasting). Spillet treffer klikk der siste WM_MOUSEMOVE var. Windows poster en musebevegelse når vinduet under markøren endres; runtimen gjør nå det samme når en dialog, meldingsboks eller meny lukkes. Bekreftet med utforskerens reproduksjon.
- Rettet i testkrokene: `type` tar resten av linja (mellomrom, tegnsetting, ingen 31-tegnsgrense), lange skriptlinjer deles ikke lenger, og bildedumperen skriver også et bilde som vises én gang og blir stående.
- Ikke endret: noen etiketter i skjemaene brytes eller klippes med erstatningsfonten. Wine-skjermbildet av originalen viser en font av samme størrelse (litt bredere enn vår), så det finnes ikke grunnlag for å bytte font uten et skjermbilde fra ekte Windows.
- Åpent: med "Play videos" slått på avslutter F1 spillet, fordi ActiveMovie (CoCreateInstance) ikke finnes i runtimen. Video venter på brukerens svar.
- Codex leverte oppgave 1 (fem ressursfunksjoner som lesbar C, PR #3, flettet inn i grenen). Flettet lokalt og kontrollert i et eget bygg (`build/merge`): `lula-fncheck` 7 funksjoner x 2000 tilstander bestått, Codex' filkontroll 6395 tilfeller uten avvik, 7 ende-til-ende-tester grønne, 144 originalfiler uendret.
- Full testsuite før fletting: 28 tester grønne.
- Startet en agent som gjør de 64 scenariene kjørbare fra en fersk klone (oppskrifter for lagrede spill, `tools/scenarios.py`, `tests/test_scenarios.py`).

## 2026-10-06 00:50 UTC: Codex-gjennomgang av PR #2 håndtert, ny Codex-oppgave

- Codex' automatiske gjennomgang hadde fire funn på en eldre versjon av PR #2. Svart på og løst alle fire:
  - Timertråden kunne gi fra seg låsen midt i et tikk: allerede rettet i `64982c2` (tråden kjører med `rt_gil_set_no_yield`).
  - Timeren tok igjen tapte tikk i en bølge etter korte stopp: rettet i `fb43f0f`, tapte tikk slås nå alltid sammen (spesifikasjonen R3). Målt: noe færre tikk under tung logging, uten betydning siden hvert tikk jobber ut fra avspillingsposisjonen.
  - Kommandolinjen kunne sprenge spillets 260-bytes buffer og 20 argumentplasser: `lula` avviser nå for lange argumenter med en melding (`fb43f0f`).
  - README-ordlyden: presisert hva den statiske rekompileringen er (maskinelt oversatt C pluss runtime, ikke lesbar kildekode) og hva som er testet.
- Kontrollert i et eget worktree (`/home/user/lula-wt`, gjenskapt fra HEAD) så agentens pågående endringer ikke kom med: 7 ende-til-ende-tester grønne.
- `docs/samarbeid-codex.md`: oppgave 1 markert ferdig, ny oppgave 4 er rekonstruksjon av blitterne (0x4458cc, 0x444d1d, 0x444b00 og flere), rangert etter antall kall i utforskningen.

## 2026-10-06 05:20 UTC: scenariotester fra fersk klone, avslutningskrasj rettet

- Agenten for scenariene er ferdig. Alle 64 scenarier kan nå kjøres fra en fersk klone: `tests/scenarios/saves.json` har 40 oppskrifter som bygger de lagrede spillene ved å spille og lappe (`python3 tools/scenarios.py saves`, ca. 5 min), og `python3 tools/scenarios.py run` kjører og bedømmer scenariene ut fra `# check:`- og `# reject:`-linjer. `tests/test_scenarios.py` kjører seks raske scenarier som del av testsuiten (`LULA_SCENARIOS=all` for alle). Ingen lagrede spill i Git.
- Ny testkrok `LULA_CLOCK`: veggklokka starter på et fast tidspunkt. Spillet seeder `rand()` fra den, så tilfeldige hendelser (bank, distributør, bar, flyreiser) blir like hver gang. Flere scenarier er tilpasset de bydataene klokka 1997-01-01 08:00 gir.
- Agenten fant en sjelden krasj (1 av ca. 250 kjøringer) ved avslutning: hovedtråden lukket SDL-lydenheten mens lydtimeren (2 ms, spillets lydsystem) kunne låse den samme enheten. Rettet: all spillkode stoppes før nedstengingen ved at hovedtråden tar den globale låsen, med mindre en spilltråd ba om avslutningen og allerede holder den. Agentens ekstra forsøk for denne krasjen er fjernet fra runneren, så en ny krasj blir synlig.
- Kontroll: 40 avslutninger på tilfeldige tidspunkt under lyd, alle rene. Testsuiten 29 tester grønne, `lula-fncheck` 7 funksjoner grønne, 144 originalfiler uendret. Alle 64 scenarier bestått i én full kjøring (1717 s med `-j 3`), etter agentens tre fulle kjøringer med 64 av 64. Scenariene alene når 931 av 1261 levende funksjoner (73,8 %).

## 2026-10-06 06:00 UTC: main flettet inn, runde 2 av utforskningen startet

- PR #2 ble slått inn i `main` 2026-10-05 17:52 på commit `7234f07`. Alt etter det (utforskningen, musrettelsen, scenariene, avslutningskrasjen, Codex-gjennomgangen) ligger bare på grenen og trenger en ny PR. `main` (med Codex' PR #1, portabel NGS/TBF-leser i `reconstruction/resources/`) er flettet inn i grenen uten konflikter; `tests.test_native_resources` 7 av 7 grønne etter `tools/build_reconstruction.sh`. Fletting ble valgt framfor rebase fordi Codex' grener bygger på historikken til denne grenen.
- `tools/scenarios.py` leser nå oppskrifter også fra `tests/scenarios/saves/*.json`, én fil per område, så flere agenter kan legge til oppskrifter uten å skrive i samme fil.
- Codex' PR #4 (TAF-redigering) gjennomgås i en workflow på en prøvefletting i `/home/user/lula-pr4`: tre gjennomgangsagenter (formattroskap mot spillets leser, minnesikkerhet i C, Python-verktøy og dokumentasjon), én som kjører Codex' egne kontroller på nytt, og to skeptikere per funn.
- Runde 2 av utforskningen startet som workflow. Ukjørte funksjoner ble kartlagt mot romtabellen i spesifikasjonen: hele filmproduksjonen i trinn 2 (casting, filmplanlegging, studioer, klipping, lyd, kopiering, lager, rekvisitter, fritidsrom, bemannet markedsavdeling) har aldri vært besøkt. En agent bygger et felles lagret spill med alle bygg, ni agenter tar hvert sitt område, hvert område kontrolleres av en uavhengig agent (to kjøringer og dekningsfil), og til slutt lages og selges en film fra start til slutt.
- Fontundersøkelse: Wine-skjermbildet av hovedmenyen viser tekst som er 1 til 4 piksler bredere enn vår, ikke smalere. GDI plasserer hvert tegn på hele piksler og bruker ikke kerning i TextOut/DrawText; runtimen gjorde begge deler med brøkdeler og kerning. Prøver GDI-oppførselen i et eget worktree og måler mot Wine.
