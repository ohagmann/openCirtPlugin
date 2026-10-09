# Changelog

Alle wesentlichen Änderungen am openCirt-Plugin werden in dieser Datei dokumentiert. Bis 1.7.3 war openCirt ein Tab im gemeinsamen Plugin batchTool/openCirt; die Einträge bis dahin stammen aus dessen CHANGELOG.

Format basiert auf [Keep a Changelog](https://keepachangelog.com/de/1.1.0/).
Versionierung: Bump bei Änderungen am Plugin-Binary (C++/GUI). Kein Bump bei reinen Änderungen an Vorlagen, LISP-Skripten, Dokumentation oder Repo-Konfiguration.

## [2.0.0] - 2026-10-09

### Changed
- **openCirt ist ein eigenes Plugin.** Bis 1.7.3 war openCirt ein Tab im Plugin batchTool/openCirt (`batchtool-<Version>.brx/.lrx`, Befehl `BATCHTOOL`). Seit 2.0.0 gibt es zwei Plugins: openCirt (`opencirt-<Version>.brx/.lrx`, Befehl `OPENCIRT`, Kurzform `OC`) für die GA-Planung und batchTool (`batchtool-<Version>.brx/.lrx`, `BATCHTOOL`/`BT`, eigenes Repository) mit den Tabs Text, Attribute, Layer und LISP. Beide lassen sich gleichzeitig laden. Die Kurzformen sind echte Befehle und brauchen keinen Eintrag in der `default.pgp`.
- **Fenster ohne Tabs.** Der Projektordner steht oben im Fenster (Feld mit „Durchsuchen…"), darunter die Schaltflächen wie bisher, darunter das Protokoll. Der General-Tab mit Dateifiltern und Backup-Einstellungen entfällt; openCirt hat ihn nie benutzt (die Sicherungskopien `*.bak` legt der Gesamtlauf weiterhin selbst an). Das Häkchen „openCirt-Funktionen aktivieren" entfällt. Der zuletzt benutzte Projektordner wird gemerkt; beim ersten Start übernimmt openCirt den Quellordner aus batchTool/openCirt 1.7.
- **Ein Fortschrittsbalken.** Zwischenschritte („Deckblaetter erzeugen…", „GA-FL lesen: 20 von 867 Blaettern") stehen als Text im Fortschrittsbalken, nicht mehr in der Statuszeile und nicht mehr im Protokoll. Im Protokoll bleiben Ergebnisse, Zahlen und Fehler.
- **Fehlermeldungen auf einem Weg.** Ein Fehler steht als eine rote Zeile im Protokoll, und dieselbe Zeile ist die erste Zeile des Dialogs, darunter die Erläuterung. Bisher hatten Protokoll und Dialog verschiedene Texte. Auch Prüfungen vor dem Lauf (Projektstruktur unvollständig, Datei fehlt) stehen jetzt im Protokoll. Erfolgsmeldungen, Sicherheitsabfragen und Rückfragen bleiben wie bisher.
- Menü und Beschriftungen des Fensters auf Deutsch („Protokoll → Leeren / Exportieren…", „Hilfe → Ueber openCirt…"); Fenstergröße und -lage werden gemerkt. Die Protokollzeile „Projektkonfiguration geladen" entfällt.

### Removed
- Tabs General, Text, Attribute, Layer und LISP (jetzt batchTool). Damit betreffen die in `KNOWN_ISSUES.md` beschriebenen BricsCAD-Probleme (GDI-Objekt-Leck unter Windows, LISP unter Linux) openCirt nicht mehr; sie trafen nur den LISP-Tab. Die Datei ist zu batchTool gewandert.

### Technisches
- Neues Fenster `src/ui/OpenCirtWindow.*` (Protokoll, Menü, Thema), Einstiegspunkt `src/plugin/OpenCirtPlugin.cpp` (Befehle `OPENCIRT`, `OC`, Befehlsgruppe `OPENCIRT_CMDS`). Die unter Windows selbst angelegte `QApplication` bleibt beim Entladen stehen, damit ein zweites Qt-Plugin (batchTool) sie weiter benutzen kann; BricsCAD räumt beim Beenden auf.
- Einstellungen unter `QSettings("openCirt", "openCirt")` (Projektordner, Fenstergeometrie), getrennt von batchTool.
- CMake-Ziel `opencirt_plugin`, Ausgabe `opencirt-<Version>.brx/.lrx`, Ablage im Beispielprojekt über `tools/copy_plugin.cmake`; Versionsmakro `PLUGIN_VERSION`.
- Repositories: `batchTool_openCirt` ist archiviert (Stand 1.7.3). openCirt wird in `openCirtPlugin` weiterentwickelt, batchTool in `batchToolPlugin`. `src/ui/Theming.*`, `src/windows_fix.h` und `src/mfc_stubs/` sind in beiden gleich; eine Korrektur dort gehört in beide.

### Geprüft (Linux)
- Beispielprojekt (Arbeitskopie mit den Gewerke-Ordnern), Gesamtlauf und IO-Liste mit 1.7.3 und 2.0.0 auf zwei frischen Kopien: 17 Blätter, 9.092 Attribute; `compare.py` und `lage.py` ohne Abweichung (Werte, Einfügepunkte, Ausrichtungspunkte, Textmaße), `IO-Belegungsliste.csv` byteidentisch.
- Beispielprojekt mit Erstellliste: „Projekt aufbauen" (4 Zeichnungen) und Gesamtlauf mit 1.7.3 und 2.0.0: 10 Blätter, Attribute gleich.
- „PDF publizieren" auf dem erzeugten Projekt: Inhaltsverzeichnis (1 Seite, 4 Einträge), PDF mit 18 Seiten aus der Batch-Instanz, Abschlussmeldung wie bisher.
- Protokoll des Gesamtlaufs ohne Zwischenschritte: 33 statt 43 Zeilen beim Beispielprojekt, Zahlen und Ergebnisse unverändert.
- openCirt und batchTool 2.0.0 in einer BricsCAD-Sitzung geladen, beide Fenster geöffnet, batchTool entladen, openCirt erneut aufgerufen, batchTool erneut geladen: BricsCAD läuft weiter, beide Fenster bleiben.
- Windows-Build am 2026-10-09 vom Anwender gebaut (MSVC 19.44, Qt 6.8.3): `opencirt-2.0.0.brx` liegt im Beispielprojekt; Plugin unter Windows geladen, Fenster geöffnet und bedient.
- Projekt mit 1.809 Blättern (Linux, Anwender): „Projekt aufbauen" mit Erstellliste (817 Quellzeichnungen), Gesamtlauf und „PDF publizieren" mit 2.0.0; Referenz derselbe Ablauf mit 1.7.3 auf einer frischen Kopie des Ausgangsstands. Aufbau-Protokolle zeilengleich (20.454 Zeilen); alle 1.809 Blätter gleich: 810 Quellzeichnungen, 134 Deckblätter, 792 GA-FL, 26 Summen, 7 Projektblätter, 40 Inhaltsseiten – 1.586.740 Attribute ohne Abweichung in Wert, Einfügepunkt, Ausrichtungspunkt und Textmaß. PDF 1.809 Seiten.

## [1.7.3] - 2026-10-08

### Added
- **„BAS konfigurieren".** Neuer Knopf im openCirt-Tab neben „BAS-Generierung einschliessen". Er zeigt die Segmente der BAS.csv als Tabelle mit Art (Text / Attribut) und Wert. Zeilen lassen sich als Attribut, Text oder Trennzeichen anfügen, entfernen und nach oben oder unten schieben; die Vorschau zeigt den Aufbau mit `<NAME>` für Attribute und `<NAME_n>` für Attribute je Datenpunkt, darunter stehen Beispiele für Text- und Attributzeilen. „Speichern" schreibt die BAS.csv im Format des Plugins (Text als `"""Text"""`, Trennzeichen `-`, Attributnamen ohne Anführungszeichen) und sichert die bisherige Datei als `BAS.csv.bak`; der Aufbau steht danach im Protokoll. Der Dialog zeigt oben, welche Datei er gelesen hat und wie viele Segmente; fehlt die BAS.csv, legt er sie mit dem üblichen Aufbau an und meldet das. Beides steht auch im Protokoll. Die Datei bleibt eine schlichte CSV und lässt sich weiter von Hand oder per Skript pflegen; Kommentare hinter dem Semikolon gehen beim Speichern aus dem Dialog verloren.

### Changed
- Die Rückfrage „BAS.csv pruefen" aus 1.7.2 entfällt, der Dialog macht sie überflüssig. Die Protokollzeile „BAS.csv geladen: N Segmente, Aufbau: …" bleibt.

### Fixed
- **Beschreibungen neben den Knöpfen unter Linux lesbar.** Ein Stylesheet mit Farbe schreibt seine Farbe in die Palette des Widgets zurück; die gedämpfte Farbe wurde bei jedem Anwenden des Themas aus der schon gedämpften Farbe neu gemischt. Unter Linux begann das bei der hellen Standardpalette eines noch elternlosen Labels, im dunklen Thema blieb davon fast nichts übrig. Die Farben werden jetzt aus der Palette des Fensters gerechnet.

### Geprüft (Linux)
- „BAS konfigurieren" mit einer BAS.csv, in der alle Zeilen in Anführungszeichen stehen: der Dialog zeigt 13 Zeilen der Art Text, Vorschau `GEB1-ORTSKENNZEICHEN-GEWERK-…`; nach Umstellen der sechs Namen auf Attribut lautet die Vorschau `GEB1-<ORTSKENNZEICHEN>-<GEWERK>-<ANLAGE>-<OC_AKS>-<OC_FCODE_DP_n>-<ASP>`, die gespeicherte Datei ist byteidentisch mit einer von Hand richtig geschriebenen BAS.csv, die bisherige liegt als `BAS.csv.bak`.
- Beispielprojekt: Laden, Trennzeichen anfügen, nach oben, nach unten, entfernen, speichern ergibt die Datei im Format des Plugins mit unverändertem Aufbau; anschließender Gesamtlauf ohne Rückfrage, Ergebnis attributgleich mit 1.7.2 (17 Blätter, 9.092 Attribute).
- Beschreibungen neben den Knöpfen im dunklen Thema per Bildschirmfoto geprüft: vorher kaum vom Hintergrund zu unterscheiden, jetzt lesbar.
- Projekt ohne BAS.csv: Meldung „BAS.csv fehlte und wurde mit dem ueblichen Aufbau angelegt" mit Pfad, die Datei liegt danach mit dem üblichen Aufbau in `01- Referenzen`. Projekt mit BAS.csv: Statuszeile „BAS.csv gelesen, 13 Segmente" mit Pfad, alle Zeilen in der Tabelle, keine Meldung. Beides auch als Protokollzeile.

## [1.7.2] - 2026-10-07

### Added
- **Das Protokoll zeigt den BAS-Aufbau.** Beim Laden der BAS.csv steht im Protokoll, wie der BAS zusammengesetzt wird, z.B. `"GEB1" + "-" + ORTSKENNZEICHEN + "-" + GEWERK + … + OC_FCODE_DP_n + "-" + ASP`: Text in Anführungszeichen, Attribute ohne, Attribute je Datenpunkt mit `_n`.
- **Rückfrage bei Anführungszeichen um Attributnamen.** Stehen in der BAS.csv Zeilen wie `"ASP"` oder `"OC_AKS"` in Anführungszeichen, nimmt das Plugin sie wörtlich; der BAS lautet dann `GEB1-ORTSKENNZEICHEN-GEWERK-…`. So speichern Calc und Excel die Datei mit „alle Textzellen in Anführungszeichen", und in der Tabellenkalkulation ist das nicht zu sehen. „Projekt erstellen" zeigt die betroffenen Zeilen jetzt vor der Sicherheitsabfrage an, mit „Abbrechen" (Vorgabe) und „Trotzdem fortfahren"; beim Abbruch bleibt das Projekt unverändert. Ein Kürzel wie `"GEB1"` löst keine Rückfrage aus. Anlass: eine so gespeicherte BAS.csv aus einem anderen Projekt (2026-10-07), die zunächst wie ein Fehler der neuen Version aussah.

### Fixed
- **Die ASP-Kennung darf beliebig heißen.** Bis 1.7.1 galt ein Ordner nur dann als ASP-Ebene, wenn sein Name „ASP" oder „ISP" enthielt. Stand in der Erstellliste eine Kennung wie `ZE01`, legte „Projekt aufbauen" zwar den Ordner `00 ZE01` an, der Gesamtlauf fand darunter aber keine ASP-Ebene: ASP, GEWERK und ANLAGE blieben im Plankopf leer, Datenpunktliste und IO-Belegungsliste führten die Blätter unter „(ohne ASP)", die Sensorliste ohne ASP, die BAS-Adressen entstanden ohne die Segmente ASP, Gewerk und Anlage (`Testprojekt-----ZUV-01-FG_01` statt `Testprojekt-ZE01-GA-SSK-1000--ZUV-01-FG_01`), und die Summenblätter warfen die Blätter aller solcher Ordner in einen Topf (keine Los-Summe, keine Gewerke-Summe je Los, ASP-Summe als `_default`). Jetzt bestimmt die Lage im Pfad die Ebene, so wie „Projekt aufbauen" die Ordner anlegt: `05- Projekt Zeichnungen/<Los>/<ASP>/<Gewerk>/<Anlage>`. Die Namen vergibt der Planer in der Erstellliste, das Plugin übernimmt sie, wie sie dort stehen. Das Inhaltsverzeichnis arbeitete schon so.
- Folge für bestehende Projekte: Blätter unter einer ASP-Ebene, deren Name bisher nicht erkannt wurde (z.B. `00 Los 00/00 Übersicht/00 GA/00 Topologie`), bekommen beim nächsten Gesamtlauf ASP, GEWERK und ANLAGE aus ihren Ordnern in den Plankopf (`Übersicht`, `GA`, `Topologie`), ebenso ihre Deckblätter. Bisher blieben diese Felder dort leer.

### Geprüft (Linux)
- Beispielprojekt mit Erstellliste, in der `ASP01` durch `ZE01` ersetzt ist: „Projekt aufbauen" legt `00 Los 1/00 ZE01/…` an, der Gesamtlauf schreibt `ZE01` als ASP in Quellblätter und Deckblätter.
- Beispielprojekt mit umbenanntem Ordner `01 ZE01`, Gesamtlauf und IO-Liste: mit 1.7.1 alle 36 Zeilen „(ohne ASP)", BAS ohne ASP/Gewerk/Anlage, 2 Summenblätter; mit 1.7.2 alle Zeilen `ZE01`, BAS vollständig, 4 Summenblätter. Gegen den Lauf mit `01 ASP01` ist das Ergebnis bis auf den Namen gleich (17 Blätter, 9.092 Attribute; nur die mittig gesetzte Überschrift des ASP-Deckblatts liegt wegen der anderen Textbreite anders).
- Ein Projekt mit 138 Blättern (115.387 Attribute), 1.7.1 gegen 1.7.2: gleich bis auf die drei Quellblätter und drei Deckblätter unter `00 Los 00/00 Übersicht/00 GA/00 Topologie`, die jetzt ASP `Übersicht`, GEWERK `GA` und ANLAGE `Topologie` tragen (Protokoll: „52 von 52" statt „49 von 52 Dateien erhalten ASP/GEWERK/ANLAGE aus Ordnerhierarchie").
- BAS.csv-Rückfrage: mit einer BAS.csv, in der alle Zeilen in Anführungszeichen stehen erscheint vor der Sicherheitsabfrage der Dialog „BAS.csv pruefen" mit den sechs betroffenen Zeilen; „Abbrechen" lässt den Zeichnungsordner unverändert (Prüfsummen gleich), „Trotzdem fortfahren" läuft wie bisher durch (BAS `GEB1-ORTSKENNZEICHEN-GEWERK-ANLAGE-OC_AKS-OC_FCODE_DP-ASP`, Aufbau und Warnung im Protokoll). Beispielprojekt unverändert und mit Kürzel `"GEB1"`: kein Dialog, Protokollzeile „BAS.csv geladen: 13 Segmente, Aufbau: …", Ergebnis attributgleich mit dem Lauf ohne die Prüfung (17 Blätter, 9.092 Attribute).

## [1.7.1] - 2026-09-30

### Added
- **Beispiel-Erstellliste im Beispielprojekt:** `01- Referenzen/Erstellliste_VORLAGE.csv` mit allen 38 Spalten der Erstellliste (UTF-8 mit BOM, Semikolon) und fünf Beispielzeilen, die Projektzeile, Anlagenzeile mit Meldungsgruppe, Meldungszeile und einfache Anlagenzeilen zeigen. „Projekt aufbauen" erzeugt daraus vier Blätter aus den Vorlagen des Beispielprojekts (geprüft: Vorschau und Lauf ohne Warnung, anschließender Gesamtlauf mit 10 Deckblättern). Beschrieben in der Bedienungsanleitung, Abschnitt 3.3.
- **Vorlagen des Beispielprojekts:** `OC_VORLAGE_DIN_A2_HISTORIE_V1.dwg` und `OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg` auf den Vorlagenstand vom 24.09.2026 gebracht (Blöcke, Attribute und Lagen unverändert gegenüber dem Stand vom 23.09.; geprüft mit Aufbau, Gesamtlauf und PDF mit Inhaltsverzeichnis).

### Changed
- **Der Dateiname trägt die Version.** Der Build erzeugt `batchtool-<Version>.brx` bzw. `batchtool-<Version>.lrx` (z.B. `batchtool-1.7.1.lrx`) statt `batchtool.brx`/`batchtool.lrx`, damit sich die Stände auseinanderhalten lassen. Die fertige Datei wird zusätzlich ins Beispielprojekt gelegt (`00- BricsCAD Plugin/00- Windows Version/` bzw. `01- Linux Version/`), ältere Stände dort werden entfernt. Wer das Plugin über die Startup Suite lädt, stellt den Eintrag nach einem Versionswechsel auf die neue Datei um.

### Fixed
- **LibreOffice als Flatpak wird gefunden.** „Projekt erstellen" wandelt `GA_FL_VORLAGE.ods` mit LibreOffice in eine CSV-Datei um. Unter Linux suchte das Plugin nur in `/usr/bin` und `/snap/bin`; war LibreOffice als Flatpak installiert, brach der Lauf mit „Weder LibreOffice noch Excel gefunden" ab. Gesucht wird jetzt in `/usr/bin`, `/usr/local/bin`, `/usr/lib64/libreoffice`, `/usr/lib/libreoffice`, `/opt/libreoffice*` (Pakete von libreoffice.org), bei den Startskripten von Flatpak (`~/.local/share/flatpak/exports/bin` und `/var/lib/flatpak/exports/bin`, Anwendung `org.libreoffice.LibreOffice`), in `/snap/bin` und im Suchpfad. Liegt LibreOffice woanders, nennt die Umgebungsvariable `OPENCIRT_LIBREOFFICE` das Programm.
- **Scheitert die Umwandlung, bleibt das Projekt, wie es war.** Bisher löschte der Lauf zuerst Deckblätter, GA-FL-Blätter, Summenblätter, Inhaltsverzeichnis und die bisherige `GA_FL_VORLAGE.csv` und stellte erst danach fest, dass LibreOffice fehlt. Die Umwandlung steht jetzt vor dem Löschen; die bisherige CSV wird erst ersetzt, wenn die neue geschrieben ist.
- **Der Abbruch wird gemeldet.** Bisher stand er nur rot im Protokoll. Jetzt erscheint eine Meldung mit dem Grund: LibreOffice nicht gefunden, nicht startbar, ohne Antwort (nach 90 s) oder ohne Ergebnis – samt der Ausgabe von LibreOffice. Die Meldung „Weder LibreOffice noch Excel gefunden" erschien bisher auch dann, wenn LibreOffice gefunden war und die Umwandlung scheiterte.
- **LibreOffice startet mit eigener Umgebung.** BricsCAD stellt sein Programmverzeichnis vor den Bibliothekspfad (`LD_LIBRARY_PATH`); ein von dort gestartetes fremdes Programm fände Bibliotheken, die nicht zu ihm passen. Das Plugin nimmt das Verzeichnis für den Aufruf von LibreOffice heraus. Während LibreOffice arbeitet, wird die Anzeige weiter gezeichnet.

### Geprüft (Linux)
- LibreOffice aus dem Paket der Distribution: Ergebnis des Beispielprojekts wie mit 1.7.0 (17 Blätter, 9.087 Attribute, Auszug bytegleich).
- LibreOffice nicht auffindbar, LibreOffice ohne Ergebnis, LibreOffice ohne Antwort: Meldung erscheint, Zeichnungen und Referenzdateien unverändert, das Plugin bleibt bedienbar.
- Flatpak: geprüft mit einem nachgebildeten Startskript an der Stelle, an der Flatpak seines ablegt. **Mit einem echten LibreOffice-Flatpak ist die Umwandlung noch nicht geprüft.**

## [1.7.0] - 2026-09-29

### Changed
- **„Projekt erstellen" läuft ohne Editor.** Bisher schrieb das Plugin für jeden Schritt ein Skript; BricsCAD öffnete jede Zeichnung im Editor, führte LISP aus, speicherte und schloss sie – jede Quellzeichnung fünfmal. Jetzt bearbeitet das Plugin die Zeichnungen selbst als Side-Database (`core/OpenCirtEngine`): Plankopf-Stammdaten, Deckblätter, ASP/Gewerk/Anlage aus der Ordnerhierarchie, BMK-Nummerierung, BAS-Generierung, Extraktion, GA-FL-Blätter, Summenblätter und Textbreiten. Die fünf Skripte aus `02- Skripte` sind dafür Schritt für Schritt nach C++ übertragen, einschließlich der Reihenfolgen, an denen das Ergebnis hängt (Blöcke wie `(ssget "X")`, Attribute wie die ATTRIB-Kette, Sortierung wie `(vl-sort)`). Folgen: Der Lauf flackert nicht mehr und blockiert BricsCAD nicht über Minuten; ein Projekt mit 323 Quellzeichnungen, 3.392 Datenpunkten, 347 GA-FL-Blättern, 32 Summenblättern und 146 Deckblättern brauchte im Test unter Linux 1:49 Minuten, die Sicherungskopien eingeschlossen (zum Vergleich: für ein kleineres Projekt mit 263 Blättern nennt der Eintrag zu 1.4.1 rund 14 Minuten unter Windows); der Lauf öffnet keine Dokumente mehr und ist damit vom GDI-Objekt-Leck (`KNOWN_ISSUES.md` Abschnitt 1) nicht mehr betroffen; und er läuft unter Linux, wo die LISP-Umgebung von BricsCAD ihn bisher scheitern ließ
- **Geprüft gegen ein unter Windows mit 1.5 erzeugtes Projekt** (867 Blätter, 738.180 Attribute): Attributwerte gleich in allen Quellzeichnungen (89.828 Attribute – BMK, BAS, Plankopf), Deckblättern (146), GA-FL-Blättern (584.001), Summenblättern (53.856) und Seiten des Inhaltsverzeichnisses (3.258); Breitenfaktoren der Textbreitenanpassung bei allen 738.180 Attributen gleich, dieselben 65 Texte gestaucht; Layerzustände und ersetzte Texte der Deckblätter gleich. Einfügepunkte und Textmaße aller Attribute gleich, ausgenommen die Blätter, deren Vorlage nach dem Windows-Lauf geändert wurde (Änderungshistorie, Inhaltsverzeichnis). Die Textmaße aus der Side-Database stimmen mit denen des Editors überein (158 Attribute, Abweichung 0). Ein zweiter Lauf auf dem fertigen Projekt liefert dasselbe Ergebnis (734.922 Attribute, kein Unterschied)
- **„PDF publizieren": Das Inhaltsverzeichnis entsteht ohne Editor.** Die Seiten werden wie die übrigen Blätter als Side-Database gefüllt; das Plotten selbst läuft weiter in der eigenen Batch-Instanz von BricsCAD, Dateiname und Ordner fragt BricsCAD wie bisher ab
- **Was sich gegenüber dem Weg über LISP unterscheidet:**
  - Zeichen aus dem Bereich 0x80–0x9F von Windows-1252 (typografisches Apostroph `’`, Gedankenstrich `–`, `€` …) kommen unbeschädigt im GA-FL-Blatt an. Auf dem alten Weg gingen sie über eine CSV-Datei und wurden dabei zu Steuerzeichen (im Prüfprojekt drei Zeichnungsnummern mit `WC’s`)
  - Sind Zeichnungen des Projekts im Editor geöffnet, startet der Lauf nicht und nennt sie. Früher fiel das nicht auf, weil das Skript die Zeichnungen selbst öffnete
  - Am Ende steht eine Meldung mit Stückzahlen, Fehlern und Dauer
  - **Sicherungskopien:** Jede Zeichnung, die es vor dem Lauf schon gab (Quellzeichnungen, Projektblätter), bekommt beim ersten Speichern eine `*.bak` mit ihrem Stand vor dem Lauf. Weitere Speichervorgänge desselben Laufs lassen die Sicherung stehen – im Editor überschrieb jedes Speichern sie, am Ende zeigte sie den Stand vor dem letzten Schritt. Blätter, die der Lauf selbst aus einer Vorlage erzeugt (Deckblätter, GA-FL, Summen, Inhaltsverzeichnis), bekommen keine; ihr Vorzustand wäre die leere Vorlage. Lässt sich die Sicherung nicht anlegen, wird die Zeichnung nicht überschrieben und der Fehler gemeldet. „Projekt bereinigen" entfernt die Sicherungen
  - Die Vorschaubilder der Zeichnungen im Dateimanager werden nicht neu erzeugt
  - `extractdp_log.txt` und `fillgafl_log.txt` im Tempordner sind UTF-8 und liegen im Ordner des Projekts (siehe „Fixed")
  - Die Skripte in `02- Skripte` braucht der Gesamtlauf nicht mehr. Der Ordner wird weiterhin verlangt, die Skripte bleiben für den LISP-Tab benutzbar. Wer an ihnen etwas geändert hat, muss die Änderung künftig im Plugin nachziehen
- **`bmk_counters.tmp` bleibt nicht mehr liegen.** Der Gesamtlauf löscht die Zählerdateien, sobald die letzte Quellzeichnung nummeriert ist, und vorsorglich auch vor der ersten – falls ein abgebrochener Lauf oder ein Lauf im LISP-Tab welche hinterlassen hat. Bisher blieben sie liegen: Begann ein Ordner mit einer Zeichnung im Modus `FORTSETZEN`, zählte ein zweiter Lauf dort weiter, wo der erste aufgehört hatte. Jetzt liefert jeder Lauf dieselben Nummern
- **Unten ausgerichteter Text steht unter Linux an derselben Stelle wie unter Windows.** BricsCAD für Linux rechnet die Unterlänge einer TrueType-Schrift kleiner als BricsCAD für Windows (Arial: 0,202 statt 0,296 der Texthöhe). Ein unter Linux geänderter, unten ausgerichteter Text rutscht deshalb nach unten, bei 2,07 mm Texthöhe um 0,19 mm – im Editor genauso. Betroffen ist die Spalte AKS/BAS der GA-FL. Das Plugin gleicht das unter Linux aus (`core/TextAlignment.h`): Der Text behält den Abstand zur Grundlinie, den er in der Vorlage hat. Weil BricsCAD die Lage beim Schließen des Objekts aus dem Ausrichtungspunkt neu berechnet, hebt das Plugin dafür den Ausrichtungspunkt an. Im Prüfprojekt liegen damit alle Texte der GA-FL- und Summenblätter dort, wo sie im Windows-Stand liegen (Einfügepunkt und Textmaße gleich); bei 3.708 Attributen liegt der Ausrichtungspunkt 0,19 mm höher. Sichtbar ist das nicht. Wird ein solcher Text später unter Windows im Editor geändert, rückt er um diesen Betrag nach oben. Unter Windows ändert sich nichts

### Fixed
- **Extraktionsdaten eines Projekts können nicht mehr in ein anderes geraten.** Der Gesamtlauf legte die extrahierten Datenpunkte in `<Temp>/OpenCirt_extract` ab, einem Ordner für alle Projekte, und die Sensorliste las später, was dort lag. Nach einem Projektwechsel ohne neuen Gesamtlauf stand in der Sensorliste deshalb der Inhalt des zuvor bearbeiteten Projekts, sobald beide dieselben Ordner- und Dateinamen hatten – bei einer Projektkopie immer. Nachgestellt mit zwei Kopien des Beispielprojekts. Jetzt gilt: Extraktionsdaten werden nur in dem Lauf gelesen, der sie geschrieben hat
  - Die **Sensorliste liest die Zeichnungen selbst**, ohne sie zu ändern, statt auf den letzten Gesamtlauf zurückzugreifen. Sie zeigt damit immer den gespeicherten Stand des geladenen Projekts und setzt keinen Gesamtlauf in derselben Sitzung mehr voraus; BMK und BAS stehen in der Liste so, wie der letzte Gesamtlauf sie in die Zeichnungen geschrieben hat. Bei 323 Zeichnungen dauert das rund 8 Sekunden. Lässt sich eine Zeichnung nicht lesen, entsteht keine Liste, und die Meldung nennt die Zeichnung
  - Jedes Projekt hat **seinen eigenen Tempordner**, `<Temp>/OpenCirt_extract/<Projektname>_<Kennung>`, die Sensorliste einen weiteren mit dem Zusatz `_Sensorliste`. Die Kennung entsteht aus dem vollen Pfad des Projekts. Dort liegen auch `extractdp_log.txt` und `fillgafl_log.txt`; `projekt.txt` nennt Projekt und Zeitpunkt. Zwei BricsCAD-Instanzen mit verschiedenen Projekten kommen sich nicht mehr in die Quere
  - Der Ordner wird vor jedem Lauf geleert. Gelingt das nicht, etwa weil eine Datei daraus in einer Tabellenkalkulation geöffnet ist, startet der Lauf nicht – mit alten Daten wird nicht gearbeitet
- **Plankopf-Stammdaten stammen immer aus dem geladenen Projekt.** Für Inhaltsverzeichnis, Deckblätter und Summenblätter las das Plugin `plankopfdaten.csv` nur einmal je Sitzung. Nach einem Projektwechsel oder einer Änderung der Datei schrieb „PDF publizieren" die alten Werte in das Inhaltsverzeichnis – nach einem Projektwechsel die des vorigen Projekts. Jetzt wird neu gelesen, sobald das Projekt gewechselt hat oder die Datei sich geändert hat

### Removed
- Skripterzeugung für den Gesamtlauf und das Inhaltsverzeichnis, die Markerdateien und Timer, die auf das Ende eines Skripts warteten, und der Plugin-Befehl `OC_PHASE3_PREPARE`

## [1.6.0] - 2026-09-29

### Added
- **Das Plugin läuft unter Linux.** Derselbe Quelltext baut jetzt für Windows (`batchtool.brx`) und für BricsCAD V26 unter Linux (`batchtool.lrx`); `CLEAN_BUILD.sh` ist das Gegenstück zu `CLEAN_BUILD.bat`. Alles Linux-Spezifische steht hinter `#ifdef`, das Verhalten unter Windows ist unverändert. Die Unterschiede im Einzelnen: BricsCAD für Linux bringt eine eigene Qt-Anwendung mit, die das Plugin mitbenutzt statt eine zweite zu erzeugen; Skripte startet es dort über `sendStringToExecute`, weil `acedCommand(_.SCRIPT)` aus dem Plugin-Fenster heraus unter Linux nichts auslöst; Pfade in DSD- und Skriptdateien tragen den Trenner des Systems; die Batch-Instanz des PDF-Publish bestätigt unter Linux die Rückfrage beim Beenden; die Dateiliste mit Unterordnern wird sortiert, weil Linux Verzeichnisse ungeordnet liefert. `ExtractDP.lsp` v1.7 legt sein Protokoll auch dann an, wenn die Umgebungsvariable `TEMP` fehlt. Geprüft unter Linux: Tabs Text, Attributes, Layers und LISP, Layer-Analyse, Projekt aufbauen, Projekt bereinigen, PDF-Publish mit Inhaltsverzeichnis (am Beispielprojekt, nicht an einem großen Projekt), IO-Liste (347 Blätter), Sensorliste (mit Extraktionsdaten aus einer frischen BricsCAD-Instanz), Pfade und Werte mit Umlauten. Der Gesamtlauf „Projekt erstellen" lief mit diesem Stand unter Linux noch nicht – Ursache waren zwei Fehler in der LISP-Umgebung von BricsCAD für Linux, siehe `KNOWN_ISSUES.md` Abschnitt 2 und die Fehlerberichte unter `docs/bricscad-linux-bugs`; behoben mit 1.7.0
- **„Projekt aufbauen" – der Projektaufbau aus der Erstellliste sitzt jetzt im Plugin.** Bisher erledigte das LISP-Skript `OC_PROJECT_BUILD` diesen Schritt außerhalb des Plugins: Es öffnete jede Zeichnung im Editor und setzte die Attribute über vla-Funktionen. Die neue Schaltfläche im openCirt-Tab (über „Projekt erstellen") macht dasselbe ohne LISP: Die Zeichnungen werden als Side-Database gelesen, geändert und gespeichert. Regeln und Log sind die des Skripts v3.5 – Anlagen-, Projekt-, Stempel- und Meldungszeilen, `#`-Spalten, Marker `OC_AKS`, Ordner- und Dateinummern, Deckblatt-Schutz, Abbruch bei offenen Zeichnungen und bei fehlendem Stempel, Erstellliste in UTF-8 oder Windows-1252. Nach Auswahl der Liste stehen „Vorschau" (ändert nichts, schreibt das Log) und „Aufbauen" (mit Sicherheitsabfrage) zur Wahl; der Projektordner kommt aus dem General-Tab. Geprüft an einem Projekt mit 323 Blättern: alle 323 `COPY`-, 6.625 `attr`- und 150 Stempel-Zeilen des Logs samt Trefferzahlen gleich dem Windows-Lauf des Skripts; an 80 Blättern, die das Skript im Editor erzeugt hatte, 18.593 Attribute nach Wert und Textlage identisch; Vorschau-Log einer Testliste mit allen Warnfällen inhaltsgleich mit dem des Skripts. Laufzeit 26 Sekunden statt mehrerer Minuten, ohne Bildaufbau. Unterschiede zum Skript: Das Log ist UTF-8 mit BOM; `*.bak`-Dateien entstehen nicht mehr, der abschließende Sweep findet deshalb in der Regel nichts; ein leerer Vorlagen-Ordner wird als solcher gemeldet. Die LISP-Dateien bleiben unverändert benutzbar
- **Parametrische Blöcke werden am Namen erkannt.** Stempel sind in den Vorlagen parametrische Blöcke; ihre Referenzen zeigen auf anonyme Blöcke (`*U…`), und `AcDbDynBlockReference` kennt nur dynamische Blöcke im AutoCAD-Sinn. Der Projektaufbau fragt den Namen deshalb über `BrxDbProperties` ab (`EffectiveName`) – derselbe Name, den das Eigenschaftenfenster zeigt

### Changed
- **Sensorliste, Datenpunktliste und IO-Belegungsliste entstehen als CSV statt als ODS.** Bisher wurde eine ODS-Vorlage kopiert, entpackt, ihre `content.xml` umgeschrieben und das Archiv neu gepackt – viel Aufwand für eine Tabelle mit acht Spalten. Jetzt schreibt das Plugin die Liste direkt: `Sensorliste.csv`, `Datenpunktliste.csv`, `IO-Belegungsliste.csv` beziehungsweise `Datenpunktliste_<Filter>.csv`, weiterhin nach `06- Plot`. Format: UTF-8 mit BOM, Trennzeichen Semikolon, Zeilenende CRLF, Zeile 1 ist die Kopfzeile; Felder mit Semikolon oder Anführungszeichen stehen in Anführungszeichen. Inhalt, Reihenfolge, Filter nach Integrationsart, Modul- und Kanalzuordnung sowie Reservekanäle sind unverändert – geprüft an einem Projekt mit 347 GA-FL-Blättern, alle vier Listen Zelle für Zelle gleich der bisherigen ODS-Ausgabe. Entfallen sind die Titelzeile über der Kopfzeile und bei der Sensorliste die achte, stets leere Spalte ohne Überschrift

### Removed
- **QuaZip, pugixml und zlib.** Die drei Bibliotheken wurden nur für die ODS-Ausgabe gebraucht. Mit ihnen entfallen `external/quazip`, `external/pugixml`, der zlib-Download beim Windows-Build, `OdsTemplateWriter` und die Vorlagen `OC_VORLAGE_IO_BELEGUNG_V_1.ods` und `OC_VORLAGE_SENSORLISTE_V_1.ods`. Das Plugin hängt damit nur noch von Qt und dem BRX SDK ab. `GA_FL_VORLAGE.ods` bleibt; sie wird wie bisher über LibreOffice nach CSV gewandelt

## [1.5.1] - 2026-09-24

### Changed
- **Projektblätter auf der obersten Ebene.** Was direkt in `05- Projekt Zeichnungen` liegt (Projekt-Deckblatt, Revisionshistorie), ist ein Projektblatt und keine Quellzeichnung. Bisher entschied der Dateiname darüber (`0000 Projekt_Deckblatt…`, `_Deckblatt`, `_Inhalt_`); ein anders benanntes Blatt auf der obersten Ebene lief durch BMK, BAS, Extraktion und GA-FL, und das handgemachte Deckblatt bekam umgekehrt nie die Plankopf-Stammdaten. Jetzt gilt die Ebene: Der Gesamtlauf überspringt die oberste Ebene bei BMK, BAS, Extraktion und GA-FL, schreibt aber in Schritt 1.5 die Stammdaten aus `plankopfdaten.csv` (AN, AG, PR, ERSTELLER, ERSTELLDATUM) auch in diese Blätter. Vom Plugin erzeugte Blätter (Inhaltsverzeichnis, Summen) bleiben davon unberührt, sie versorgen sich selbst. Hintergrund: Projekt-Deckblatt und Revisionshistorie entstehen inzwischen aus der Erstellliste über projektneutrale Vorlagen, deren Plankopf leer ist

### Added
- **`KNOWN_ISSUES.md` – GDI-Objekt-Leck in BricsCAD V26.** BricsCAD gibt je Öffnen und Schließen eines Dokuments rund zwei GDI-Objekte nicht wieder frei (reproduzierbar im leeren Profil ohne Add-ons, das Plugin ist unbeteiligt). Ein zweiter Gesamtlauf in derselben Sitzung überschreitet das Windows-Limit von 10.000 GDI-Objekten je Prozess und endet mit APPCRASH („Fehler beim Ausführen von _open"). Die neue Datei beschreibt Symptom, Ursache, die Beobachtung im Task-Manager, die Regel „BricsCAD vor jedem Gesamtlauf neu starten" und das optionale Anheben des Limits über den Registry-Wert `GDIProcessHandleQuota`. Bedienungsanleitung Abschnitt 12 verweist darauf. Kein Plugin-Bump (reine Doku-Änderung)

### Changed
- **Sample-Projekt: Vorlagen auf den aktuellen Stand, alle DWGs bereinigt.** `04- Vorlagen` führt jetzt `OC_VORLAGE_DIN_A2_V14` (V13 entfernt; das Plugin nimmt die höchste Versionsnummer), neu `OC_VORLAGE_DIN_A2_HISTORIE_V1`, dazu die aktuellen Stände von Plankopf, DIN A0, Inhaltsverzeichnis, GA-FL-Blatt und GA-FL-Block. `OC_VORLAGE_EINTRAG_INHALT_DIN_A2_V_5.dwg` ist entfernt – seit 1.4.0 sitzt der Eintragsblock in der Inhaltsverzeichnis-Vorlage. In der Blockbibliothek kommt `Symbolvorlage_20_DP_UNIVERSAL_V1` hinzu. Alle 17 DWGs des Sample-Projekts sind mit `-PURGE` bereinigt (alles außer Layer, drei Durchgänge); aus der GA-FL-Blockvorlage sind ein in einem Papierbereich verbliebener, projektbezogen ausgefüllter Plankopf-Block samt Definition, ein Plantitel und ein Logo-Bild mit seinen Bilddefinitionen entfernt; alle `OC_BAS_DP_n` waren bereits leer. README und Bedienungsanleitung (3.1, 4.1, 9.1, 10.1) nachgezogen, Inhaltsverzeichnis mit 22 Einträgen je Seite wie im Code. Kein Plugin-Bump (Vorlagen/Doku)
- **FillGaFl.lsp v1.5 – Integrationsart aus dem Symbol hat Vorrang.** Ein im Symbol gesetztes `OC_INTEGRATIONSART_DP_n` (z. B. `virtuell` bei einer Referenz, deren Vorgabe `HW` ist) wurde beim Befüllen der GA-FL von der Referenztabelle wieder überschrieben; das GA-FL-Blatt zeigte dann eine andere Integrationsart als das Automationsschema. Jetzt gilt wie beim Kommentar: Symbol > Referenz > leer. Kein Plugin-Bump (reine LISP-/Doku-Änderung)
- **BmkNummerierung.lsp v2.3 – Steuerattribut `BMK_NUMMERIERUNG`.** Der Modus (`NEUSTARTEN` / `FORTSETZEN`) wird jetzt bevorzugt aus dem Plankopf-Attribut `BMK_NUMMERIERUNG` gelesen. Nur wenn es fehlt oder leer ist, greift wie bisher `FREITEXT_05`, damit ältere Plankopf-Vorlagen weiter funktionieren. Die Konsole zeigt je Zeichnung, aus welchem Attribut der Modus stammt. Kein Plugin-Bump (reine LISP-/Doku-Änderung)

## [1.5.0] – 2026-09-09

### Changed
- **Die Summenblätter entstehen aus den fertigen GA-FL-Blättern.** Bisher rechnete der Gesamtlauf die Summen aus den Extraktionsdaten und der Referenztabelle neu – parallel zu den GA-FL-Blättern, nicht aus ihnen. Eine Handkorrektur in einem Blatt kam in den Summen nie an, und die Summen hingen an der `GA_FL_VORLAGE`, obwohl sie nur aggregieren sollen. Jetzt läuft die Summenbildung als eigene Phase 3, nachdem das letzte GA-FL-Blatt gespeichert ist: Die Blätter werden per Side-Database gelesen (derselbe Leser wie beim Datenpunkt-Export), die Funktionswerte kommen aus den Blattzellen, der Übertrag der Folgeblätter wird übersprungen. Für die Summenblätter setzt `FillGaFl` keine Referenz mehr voraus. Ergebnis bei unveränderten Blättern identisch – Σ(Referenzwerte je Datenpunkt) ist Σ(Blattzellen je Datenpunkt); Datenpunkte ohne `OC_REF_DP`, die Funktionswerte im Symbol tragen, zählen jetzt ebenfalls mit, weil das Blatt sie zeigt
- **Phase 3 startet ohne Zeitlimit.** Das Phase-2-Skript endet mit dem Plugin-Befehl `OC_PHASE3_PREPARE`, der die Blätter liest, die Summen-CSVs schreibt und das Phase-3-Skript (Summen, Textbreiten, Zurücksetzen der Systemvariablen) bereitlegt. Das Plugin startet es, sobald das Phase-2-Skript beendet ist (`CMDACTIVE` = 0) – derselbe Weg wie von Phase 1 nach Phase 2, ohne Obergrenze und damit unabhängig von der Projektgröße. Ein verschachtelter Start per `_.SCRIPT` aus dem Skript heraus wurde verworfen: BricsCAD lässt danach in der Ausgangszeichnung einen offenen ÖFFNEN-Prompt stehen, der den nächsten Befehl schluckt und das Cleanup unwirksam macht

### Fixed
- **Fehlende Referenzen als Baum.** Der Dialog aus 1.4.2 zeigte je Referenz eine Zeile mit allen betroffenen Zeichnungen dahinter – bei vielen Fundstellen unlesbar. Jetzt ist jede fehlende Referenz ein Knoten mit Anzahl, die Zeichnungen stehen als Unterzeilen darunter, alles aufgeklappt; Dialog weiterhin skalierbar. Die Meldung zu Referenzwerten > 1 bleibt eine flache Liste
- **Fehlende Referenz bricht den Gesamtlauf ab.** Fehlte `GA_FL_VORLAGE.ods`, übersprang der Lauf die Konvertierung stillschweigend; war die CSV leer oder unlesbar, entfiel die Vorabprüfung ebenso still – alle Datenpunkte landeten auf 0. Jetzt wird die ODS vor der Sicherheitsabfrage und vor dem Cleanup geprüft (nichts wird gelöscht), und eine leere Referenz-CSV beendet den Lauf nach der Extraktion mit Meldung und Zustandsreset

## [1.4.2] – 2026-09-09

### Fixed
- **Sammelmeldungen sind wieder quittierbar.** Die Warnungen über fehlende Datenpunkt-Referenzen und über Referenzwerte größer 1 setzten ihre Einträge zu einem einzigen Text zusammen und zeigten ihn in einer `QMessageBox`. Die wächst mit ihrem Inhalt und lässt sich nicht verkleinern – bei vielen Einträgen rutschten die Schaltflächen unter den Bildschirmrand und der Dialog ließ sich nicht mehr schließen. Beide Meldungen laufen jetzt über einen skalierbaren Dialog mit scrollbarer Liste und Größengriff; die Schaltflächen bleiben unabhängig von der Zahl der Einträge erreichbar. Die fehlenden Referenzen werden zusätzlich sortiert ausgegeben statt in der Reihenfolge des `QSet`

### Changed
- **Der Alert am Ende des Gesamtlaufs entfällt.** Nach dem Durchlauf meldete `oc-fl-show-missing-refs` dieselben fehlenden Referenzen ein zweites Mal per LISP-`alert` – mit demselben Größenproblem und ohne zusätzlichen Inhalt gegenüber der Vorabprüfung, die zu jeder Referenz auch die betroffenen Zeichnungen nennt. Der Aufruf ist aus dem Gesamtlauf-SCR entfernt; die Funktion bleibt in `FillGaFl.lsp` erhalten, der Logeintrag ebenfalls
- **Schreibweise „openCirt" durchgängig.** In Dokumentation, Lizenztexten und Quelltextkommentaren stand überwiegend „OpenCirt". Bezeichner sind unverändert: `OpenCirtTab`, `OpenCirtConfig`, `OPENCIRT_VERSION`, `opencirt_config.json` und der Temp-Ordner `OpenCirt_extract`

## [1.4.1] – 2026-09-02

### Changed
- **Die Optionen des Gesamtlaufs stehen jetzt direkt unter „Projekt erstellen“.** Die beiden Haken „BMK-Nummerierung einschliessen“ und „BAS-Generierung einschliessen“ betreffen nur diesen einen Lauf, standen aber in einer eigenen Box „Optionen (Gesamtlauf)“ unterhalb aller Funktionen – räumlich getrennt von dem Button, den sie steuern. Sie sitzen nun eingerueckt unter dem Button, gefolgt von einem Trenner, „Projekt bereinigen“, einem weiteren Trenner und den drei Ausgabefunktionen. Die separate Optionen-Box entfällt; Verhalten und Voreinstellung der Haken sind unverändert
- **GA-FL-Befüllung ist etwa 14-mal schneller.** `FillGaFl.lsp` v1.4 lief die ATTRIB-Kette des GA-FL-Blocks (rund 1.600 Attribute) bisher bei jedem einzelnen Lese- und Schreibzugriff komplett ab und rief nach jedem geschriebenen Attribut `entupd` – pro Blatt mit 25 Datenpunkten knapp 5 Mio. `entget`-Aufrufe. Jetzt wird die Kette pro Block einmal als `(TAG . ename)`-Liste gecacht, Zugriffe gehen über den Cache, und `entupd` erfolgt einmal pro Block am Ende. Gemessen an einem Projekt mit 263 Blättern und 2.136 Datenpunkten: GA-FL-Phase 29:41 min → 2:09 min, Gesamtlauf 43 → 14 min. Ergebnis funktional identisch – Logvergleich beider Läufe ohne Abweichung, zusätzlich 1.064 Attributwerte an vier Blättern einer RLT-Anlage gegen eine unabhängige Nachrechnung aus CSV und GA_FL_VORLAGE geprüft

### Added
- **Kommentare in der `BAS.csv`.** `GenBas.lsp` v1.3 wertet nur noch die erste Spalte einer Zeile aus (alles bis zum ersten `;`) und ignoriert Zeilen, die mit `#` beginnen. Bisher war die Datei trotz Endung keine CSV – die komplette Zeile galt als Segment, ein Kommentar in Spalte 2 wurde zum ungefundenen Attributnamen und erzeugte ein leeres Segment. Nebeneffekt: Die Datei überlebt jetzt das Speichern aus Excel/LibreOffice mit `;`-Trennung. Einschränkung: Ein statischer Text darf selbst kein `;` enthalten. Doku in BEDIENUNGSANLEITUNG 7.1/7.2 ergänzt

## [1.4.0] – 2026-08-24

### Changed
- **Das Inhaltsverzeichnis entsteht aus einer vollständigen Blattvorlage.** Bisher wurde je Zeile ein einzeiliger Block aus `OC_VORLAGE_EINTRAG_INHALT_DIN_A2*.dwg` eingefügt und über feste Koordinaten positioniert (Start 31/384, Zeilenhöhe 15 mm) – 21 externe `INSERT`-Vorgänge je Blatt, jeder eine Gelegenheit zum Verrutschen. Jetzt trägt `OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg` den Eintragsblock mit allen Zeilen bereits an seiner endgültigen Stelle. Die Erzeugung kopiert das Blatt, füllt die Attribute und speichert. Entfallen sind damit die INSERT-Schleife, der Dummy-Insert zum Vorladen der Blockdefinition, das Umschalten von `ATTREQ`/`ATTDIA` und die Layersteuerung, weil die Vorlage die Layer bereits richtig führt
- Zeilen werden über ihren Tag angesprochen (`OC_INHALT_<FELD>_01` bis `_22`), der Block im Blatt über das Muster `OC_VORLAGE_EINTRAG_INHALT_DIN_A2*` gesucht. Eine künftige Blockversion erfordert daher keine Codeänderung. Findet sich kein passender Block oder tragen die Attribute keinen Zeilenindex, meldet das Skript das in der Konsole, statt still ein leeres Blatt zu erzeugen
- 22 statt 21 Einträge je Blatt, gepflegt in `OpenCirtConfig::INHALT_ROWS_PER_PAGE` statt zweimal als lokale Konstante. **Damit verschieben sich die Seitenzahlen gegenüber älteren PDFs desselben Projekts**
- Der Plankopf der Inhaltsseiten führt jetzt eine Zeichnungsnummer: `Inhaltsverzeichnis Seite 1 von 3`, bei einseitigem Verzeichnis nur `Inhaltsverzeichnis`. Das Feld blieb bisher leer
- Beschriftungen im Plugin auf die Schreibweise `openCirt` vereinheitlicht: Reitername, Checkbox, Schaltflächengruppe, Statuszeile, Tooltip und `setOrganizationName`. Klassennamen bleiben unverändert

### Fixed
- **Vorlagen wurden alphabetisch statt nach Version gewählt.** `findDeckblattVorlage()` und der Eintrags-Finder nahmen jeweils den ersten Treffer ihres Dateimusters. Lagen `V12` und `V13` nebeneinander, gewann `V12` – entgegen der Konvention, dass die höchste Nummer der aktuelle Stand ist. Zusätzlich hätte die neue Inhaltsvorlage das Muster `OC_VORLAGE_DIN_A2*` mit abgedeckt und wäre fälschlich als Deckblattvorlage gezogen worden. Neu wählt `newestVorlage()` nach der Versionsnummer und schließt die Inhaltsvorlage beim Deckblatt aus
- Der Über-Dialog meldete fest verdrahtet `v3.0.0` und wich damit von der hier geführten Zählung ab. Die Version kommt jetzt aus `project(... VERSION ...)` und ist an einer Stelle gepflegt

### Added
- `OPENCIRT_VERSION` als Compilerdefinition aus der CMake-Projektversion

### Removed
- `findInhaltBlockVorlage()` samt der einzeiligen Eintragsvorlage `OC_VORLAGE_EINTRAG_INHALT_DIN_A2_V_4.dwg`, die in keinem Projekt mehr gebraucht wird

### Migration
- Jedes Projekt braucht `OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg` in `04- Vorlagen`. Fehlt sie, bricht die Erzeugung mit einer Meldung ab, statt ein falsches Blatt zu bauen

## [1.3.1] – 2026-08-23

### Changed
- Die Erfolgsmeldung des Datenpunkt-Exports nennt jetzt die tatsächliche Zeilenzahl der Datei und schlüsselt sie auf: `972 Zeilen (869 Datenpunktzeilen + 103 Reservekanäle)`. Bisher wurde nur der Datenpunktanteil gemeldet – die Reservezeilen entstehen erst beim Auffüllen der angefangenen Module und fehlten in der Zählung, die Meldung wich damit von der Datei ab. Gezählt wird nun der Schreibzähler selbst statt einer nebenher geführten Summe, die auseinanderlaufen konnte
- Der Lesefortschritt des Exports läuft in die Statuszeile statt ins Protokoll (neues Signal `OpenCirtTab::statusMessage`, leerer Text setzt zurück). Das Protokoll führt damit nur noch Ergebnisse; die Taktung ist von 50 auf 20 Blätter verkürzt, weil eine Statuszeile das verträgt

### Removed
- Zwei veraltete `batchtool.brx`-Kopien außerhalb des Repos entfernt (Stand 15.03. und 25.03.), damit beim Laden des Moduls keine Verwechslung mehr möglich ist


## [1.3.0] – 2026-08-23

### Changed
- **Oberfläche folgt BricsCADs Hell-/Dunkeleinstellung.** Maßgeblich ist die Systemvariable `COLORTHEME`, gelesen bei jedem Aufruf von `BATCHTOOL` – nach einem Themenwechsel genügt Schließen und erneutes Öffnen. Der Stil ist jetzt Fusion: der auf Windows 11 voreingestellte Qt-Stil zeichnet Flächen und abgerundete Ecken selbst und ignoriert die Palette, weshalb dort weder ein dunkles Thema noch BricsCADs eckige Optik möglich wäre
- Keine fest verdrahteten Farbwerte mehr in der Oberfläche. Beschriftungen tragen eine Rolle (`Muted`, `Success`, `Warning`, `ErrorBold` …), die beim Themenwechsel neu berechnet wird; bisher waren die Farben für ein helles Thema geschrieben und auf dunklem Grund kaum lesbar
- Der openCirt-Tab führt kein eigenes Protokoll mehr. Meldungen liefen bisher doppelt – einmal im Tab, einmal im *Processing Log* –, was rund die halbe Fensterhöhe für denselben Text verbrauchte und unter dem Protokollkasten einen leeren Streifen hinterließ. Das *Processing Log* ist jetzt die einzige Ansicht, ohne Höhenbegrenzung und mit fester Beteiligung an der Fensterhöhe
- Das *Processing Log* färbt Meldungen nach Art ein (Fehler, Warnung, Erfolg). Das konnte bisher nur das entfallene Tab-Protokoll
- Trenner zwischen den Schaltflächengruppen ist ein echtes `QFrame` statt eines leeren `QLabel` mit Rahmen, das auf dunklem Grund unsichtbar blieb

### Added
- `src/ui/Theming.cpp/h` – Palette, Stil und rollenbasierte Farben, abgeleitet aus `COLORTHEME`


## [1.2.0] – 2026-08-22

### Added
- Gewerke-Summenblatt je Los: `0002 Projekt_Summe_Gewerke_<Los>_NN.dwg` im Wurzelordner der Projektzeichnungen, eine Zeile je Gewerk aggregiert über alle ASPs des Loses. Liegt in der Publish-Reihenfolge direkt hinter der Projektsumme
- Datenpunkt-Export mit Filterdialog: Filter nach Integrationsart (semikolongetrennt, Vorbelegung `HW`, leer = alle Datenpunkte). Die Spalte *Modul-Typ* wird nur für `HW`-Zeilen befüllt, alle anderen Integrationsarten belegen keinen Klemmenkanal
- Spalte *Integrationsart* in der IO-/Datenpunktliste. Reservezeilen bleiben dort leer, weil ihnen kein Datenpunkt zugrunde liegt. Setzt die um eine Spalte erweiterte `OC_VORLAGE_IO_BELEGUNG_V_1.ods` voraus (Integrationsart als vorletzte Spalte, vor Modul-Typ)
- Plankopf-Attribut `LPH` (Leistungsphase) wird mit extrahiert und damit in GA-FL- und Summenblätter übernommen (`ExtractDP.lsp`). Plankopfblöcke ohne dieses Attribut bleiben unberührt
- Warnmeldung beim Datenpunkt-Export, wenn `GA_FL_VORLAGE.ods` bei einem Datenpunkt einen IO-Referenzwert > 1 führt (ein Datenpunkt trägt genau ein AKS/BAS und belegt damit genau einen Kanal)
- Protokollausgabe der im Projekt vorhandenen Integrationsarten inklusive Anzahl – zeigt sofort, ob `OC_INTEGRATIONSART_DP_n` in den Zeichnungen gepflegt ist
- Diagnosebefehl `TEXTZELLEN` in `TextBreitenAnpassenBloecke.lsp`: listet für eine gewählte Blockreferenz alle erkannten Zellen mit Breite und Höhe

### Fixed
- **Textbreitenanpassung greift jetzt in Blockdefinitionen hinein.** Die GA-FL-Tabelle liegt als ein Block im Modellbereich; `(ssget "_X" ...)` fand die rund 1.740 Zellrechtecke innerhalb der Blockdefinition nicht, weshalb mehrstellige Zahlen in den 5 mm breiten Zählspalten nie gestaucht wurden. Die Zuordnung Text → Zelle wird nun im Blockdefinitionsraum über den Attributnamen gebildet und mit dem Blockmaßstab auf die Blockreferenz angewandt (`TextBreitenAnpassenBloecke.lsp` v3.0)
- **Integrationsart wurde nie extrahiert.** `ExtractDP.lsp` las `OC_INTEG_DP_n`; in den Datenpunktblöcken heißt das Attribut `OC_INTEGRATIONSART_DP_n` (Erstellliste Spalte 32). Die Spalte `INTEG_DP` kam dadurch in jeder Extraktion leer an. Der alte Name bleibt als Fallback erhalten
- Plankopf-Stammdaten aus `plankopfdaten.csv` werden jetzt auch in die Inhaltsverzeichnis-Seiten geschrieben; bisher blieb dort der Stand der DIN-A2-Vorlage stehen
- `ASP`, `GEWERK` und `ANLAGE` werden in Deckblättern aus der Ordnerhierarchie gefüllt; bisher wurde ausschließlich `ASP` gesetzt
- Textbreitenanpassung im Gesamtlauf erfasst jetzt auch alle Summenblätter, nicht mehr nur die GA-FL-Blätter – gerade dort stehen die aggregierten mehrstelligen Werte

### Changed
- `Deckblatt_B` (`0000 Projekt_Deckblatt_B.dwg`) wird nicht mehr erzeugt. Das Aufräumen bestehender Dateien bleibt erhalten, damit Altbestände beim nächsten Lauf verschwinden. `0000 Projekt_Deckblatt_A.dwg` bleibt wie bisher unangetastet
- openCirt-Tab aufgeräumt: die Schaltflächen *Plankopf-Daten setzen*, *Deckblätter erstellen*, *BMK erstellen*, *BAS erstellen*, *GA-FL erstellen* und *Textbreiten anpassen* entfallen. Alle sechs Schritte laufen als Teil von *Projekt erstellen*
- Reihenfolge der verbliebenen Schaltflächen nach Wichtigkeit im Arbeitsablauf: *Projekt erstellen*, *Projekt bereinigen*, dann getrennt durch eine Linie die Ausgaben *PDF publizieren*, *IO-Liste erstellen*, *Sensorliste erstellen*
- Reservezeilen der IO-Liste führen jetzt den ASP mit — der Klemmenplatz ist physisch vorhanden und einem Automationsschwerpunkt zugeordnet, auch wenn kein Datenpunkt darauf liegt. Anlage, BMK, BAS und Integrationsart bleiben leer
- *IO-Belegung erstellen* heißt jetzt *IO-Liste erstellen* und deckt über den Filter beide Anwendungsfälle ab. Der Ausgabename richtet sich nach dem Filter: `IO-Belegungsliste.ods` bei reinem HW-Filter, sonst `Datenpunktliste[_<Filter>].ods`
- **Der Export liest die fertigen GA-FL-Blätter statt der Extraktion.** Bisher wurden Zwischenstand (Temp-CSVs) und Referenz (`GA_FL_VORLAGE.ods`) getrennt ausgewertet und die Logik von `FillGaFl.lsp` nachgebaut — beides konnte auseinanderlaufen, und Korrekturen von Hand im Blatt kamen in der Liste nicht an. Gelesen wird jetzt über eine Side-Database, also ohne die Zeichnungen im Editor zu öffnen: kein Bildaufbau, kein Flackern, keine Dateisperre. Voraussetzung ist ein vollständiger Lauf von *Projekt erstellen*; ohne GA-FL-Blätter meldet der Export das jetzt klar statt eine unvollständige Liste zu erzeugen

## [1.1.0] – 2026-03-26

### Added
- PDF-Publish: Batch-Instanz via `/b` mit SCR-Steuerung, Marker-basierte Completion und `BACKGROUNDPLOT=0` für synchronen Publish
- IO-Belegung: ODS-Vorlagen-basierte Generierung von IO-Belegungsplänen (`OdsTemplateWriter`)
- Sensorliste: Automatische Sensorlisten-Erstellung mit Keyword-Matching (`SensorKeywordLoader`)
- Neue Vorlagen: `OC_VORLAGE_IO_BELEGUNG_V_1.ods`, `OC_VORLAGE_SENSORLISTE_V_1.ods`
- Neue Referenzdaten: `iomodule.csv`, `sensor.csv` im Sample-Projekt

### Changed
- `.gitignore` aufgeräumt und erweitert (CMake, C++, Qt, OS-Patterns)
- `.editorconfig` hinzugefügt (einheitliche Formatierung)
- `.gitattributes` hinzugefügt (Zeilenende-Normalisierung Windows/Linux)
- Vendor-Libraries pugixml und quazip als Source direkt getrackt (statt nested git repos)
- README.md: Titelzeile angepasst

## [1.0.0] – 2026-03-15

### Added
- **General-Tab**: Quellordner, Dateifilter (Include/Exclude), Unterordner-Rekursion, Backup-Konfiguration
- **Text-Tab**: Suchen/Ersetzen in DBText/MText mit Regex, Groß-/Kleinschreibung, Mehrfachersetzung
- **Attributes-Tab**: Blockattribute ändern (Filter nach Block, Tag, Sichtbarkeit)
- **Layers-Tab**: Layer löschen, umbenennen, einfrieren, Farbe/Linientyp/Transparenz ändern, Layer-Analyse
- **LISP-Tab**: Automatisierte LISP-Skript-Ausführung auf beliebig viele DWG-Dateien via SCR
- **openCirt-Tab**: GA-Planungsautomatisierung (Plankopf, BMK-Nummerierung, BAS-Generierung, GA-FL, Summenblätter, Deckblatt, Inhaltsverzeichnis, PDF-Publish)
- Sample-Projekt mit Vorlagen, Blockbibliothek, LISP-Skripten und Bedienungsanleitung
- Lizenz: BSL 1.1 (Licensor: Oliver Hagmann, Change Date: 2030-03-02, Change License: AGPLv3)
