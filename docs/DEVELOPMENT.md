# Entwickler-Hinweise

## Architektur

openCirt ist als BRX-Modul aufgebaut (Windows: `.brx`, Linux: `.lrx`), das als Shared Library in BricsCAD geladen wird. Bis 1.7.3 teilte es sich das Modul mit batchTool (Text, Attribute, Layer, LISP); seit 2.0 sind das zwei Plugins in zwei Repositories, siehe unten „Zwei Plugins, gemeinsame Dateien". Beide entstehen aus demselben Quelltext; was nur für eine Plattform gilt, steht hinter `#ifdef _WIN32` beziehungsweise `#ifndef _WIN32`. Die GUI basiert auf Qt6 (nicht MFC), was eine moderne Widget-Bibliothek und Signal/Slot-Kommunikation ermöglicht.

### Schichtenmodell

```
┌─────────────────────────────────────────┐
│  BricsCAD V26 (Host-Applikation)        │
├─────────────────────────────────────────┤
│  plugin/   BRX Entry Point              │
│            acrxEntryPoint, OPENCIRT/OC   │
├─────────────────────────────────────────┤
│  ui/       Qt6 GUI                      │
│            OpenCirtWindow (Protokoll),   │
│            OpenCirtTab (Bedienflaeche),  │
│            BasConfigDialog,              │
│            Theming (COLORTHEME)          │
├─────────────────────────────────────────┤
│  core/     Verarbeitungslogik           │
│            OpenCirtEngine (Gesamtlauf),  │
│            ProjectBuilder (Erstellliste),│
│            TextAlignment                 │
├─────────────────────────────────────────┤
│  utils/    CsvListWriter,                │
│            SensorKeywordLoader           │
└─────────────────────────────────────────┘
```

### Kernkomponenten

**ProjectBuilder** (`core/ProjectBuilder.cpp`) – Projektaufbau aus der Erstellliste. Liest die CSV, baut daraus den Plan (ein Eintrag je Blatt mit Zielpfad, Attributen und Stempeln), prüft auf offene Zeichnungen und führt den Plan aus: Vorlage kopieren, als Side-Database lesen, Attribute setzen, speichern. Die Klasse kennt keine Oberfläche; `OpenCirtTab::onProjektAufbauen()` stellt die Fragen und zeigt das Ergebnis. Regeln, Warnungen und Logzeilen folgen dem LISP-Skript `OC_PROJECT_BUILD_v3_5.lsp`, damit sich beide über ihre Logs vergleichen lassen. Drei Punkte sind nicht offensichtlich:

*Textlage.* Im Editor rechnet BricsCAD die Lage zentrierter und rechtsbündiger Attribute nach jeder Textänderung selbst nach. In einer Side-Database ist es ausdrücklich anzustoßen; deshalb folgt auf jedes `setTextString` ein `adjustTextAlignment()` (`core/TextAlignment.h`, siehe „Schrift" bei den Besonderheiten unter Linux).

*Blockname.* Stempel sind parametrische Blöcke. Ihre Referenz zeigt auf einen anonymen Block (`*U…`), und `AcDbDynBlockReference` erkennt nur dynamische Blöcke im AutoCAD-Sinn. Den Namen, den das Eigenschaftenfenster zeigt, liefert `BrxDbProperties::getValue(id, "EffectiveName~Native")`.

*Speichern unter demselben Namen.* Nach `readDwgFile` hält die Datenbank die Datei offen. `closeInput(true)` liest sie vollständig ein und gibt sie frei; erst dann gelingt `saveAs` auf denselben Pfad zuverlässig.

**OpenCirtWindow** (`ui/OpenCirtWindow.cpp`) – das Fenster: oben die Bedienfläche (`OpenCirtTab`), unten das Protokoll als einzige Ausgabe, dazu Menü (Protokoll leeren/exportieren, Über) und Thema. Der Tab schreibt über sein Signal `logMessage` ins Protokoll; Fenstergeometrie und Projektordner liegen in `QSettings("openCirt", "openCirt")`.

**OpenCirtTab** (`ui/OpenCirtTab.cpp`) – die Bedienfläche: Projektordner, Schaltflächen, Fortschrittsbalken. Der Name stammt aus der Zeit, als dies ein Tab neben den batchTool-Tabs war. Der Tab steuert den Ablauf und stellt die Fragen: welche Zeichnungen, welche Blätter aus welcher Vorlage, welche Summenebenen. Die Arbeit an den Zeichnungen selbst übergibt er an `OpenCirtEngine` und `ProjectBuilder`. Ein Lauf ist ein gewöhnlicher Funktionsaufruf mit Fortschrittsanzeige (`beginRun`, `beginPhase`, `stepDone`, `endRun`); während er läuft, verarbeitet das Fenster Ereignisse ohne Eingaben (`QEventLoop::ExcludeUserInputEvents`). Zwischenschritte stehen als Text im Fortschrittsbalken (`showProgress`), nicht im Protokoll – dort stehen Ergebnisse, Zahlen und Fehler. Fehler gehen über `reportError()`: eine rote Protokollzeile, und dieselbe Zeile als erste Zeile des Dialogs. Nur der PDF-Publish braucht BricsCAD selbst: Er startet eine Batch-Instanz und wartet über eine Markerdatei auf ihr Ende.

**OpenCirtEngine** (`core/OpenCirtEngine.cpp`) – die Schritte des Gesamtlaufs auf der Side-Database. `OcDrawing` liest eine Zeichnung, hält ihre Blöcke und Attribute im Speicher und bietet die Operationen an: Attribute nach Tag setzen, Layer frieren, Text ersetzen, BMK-Nummerierung, BAS-Generierung, Extraktion, GA-FL füllen, Textbreiten anpassen. Bis Version 1.6 liefen diese Schritte als LISP im Editor; die Operationen sind Übertragungen der Skripte aus `02- Skripte` und verweisen in ihren Kommentaren auf die Funktionen dort (`(oc-...)`). Wer hier etwas ändert, sollte vier Dinge wissen:

*Reihenfolgen sind Teil des Ergebnisses.* Die Skripte liefen über `(ssget "X")`, das die Objekte in umgekehrter Datenbankreihenfolge liefert, bauten Listen mit `(cons)` und sortierten mit `(vl-sort)`. Welcher Block bei der BMK-Nummerierung die 01 bekommt und in welcher Reihenfolge die Datenpunkte im GA-FL-Blatt stehen, hängt daran. `OcDrawing` hält die Blöcke deshalb in genau dieser Reihenfolge, und die Operationen bilden die Listenoperationen der Skripte nach – auch wo es umständlich aussieht.

*Die Sortierung der BMK-Nummerierung ist nicht widerspruchsfrei.* Ihr Vergleich behandelt Blöcke mit weniger als 10 Einheiten Abstand in X als eine Spalte. Drei Blöcke im Abstand von je 8 bilden damit keine eindeutige Ordnung, und das Ergebnis hängt am Sortierverfahren. BricsCAD benutzt für `(vl-sort)` die stabile Sortierung der C++-Bibliothek; `vlSort` bildet die der Windows-Fassung nach (bis 32 Elemente Einfügesortierung). Nicht durch `std::stable_sort` ersetzen – unter Linux wählt die Bibliothek ein anderes Verfahren.

*Lage nach jeder Textänderung nachrechnen.* Im Editor rechnet BricsCAD die Lage ausgerichteter Texte selbst nach, in der Side-Database ist es ausdrücklich anzustoßen. Auf `setTextString` und `setWidthFactor` folgt deshalb `adjustTextAlignment()` aus `core/TextAlignment.h` – nicht `adjustAlignment()`, sonst rutscht unten ausgerichteter Text unter Linux nach unten (siehe „Schrift" bei den Besonderheiten unter Linux).

*Sicherungskopien.* `OcDrawing::save()` legt vor dem Überschreiben eine `*.bak` an, je Zeichnung und Lauf einmal; `OcEngine::beginBackupRun()` beginnt einen neuen Lauf (`OpenCirtTab::beginRun()` ruft es auf). Die Sicherung zeigt damit den Stand vor dem Lauf, auch wenn der Lauf eine Zeichnung mehrmals speichert. Blätter, die der Lauf eben erst aus einer Vorlage kopiert hat, gehen über `processNewDrawing()` und bekommen keine.

*Extraktionsdaten gehören einem Lauf.* Die extrahierten CSV-Dateien liegen in einem Tempordner je Projekt und Zweck (`projectExtractDir()`). `resetExtractDir()` leert ihn zu Beginn eines Laufs, `endRun()` erklärt ihn für verbraucht, und `readExtractedData()` liest nur, solange `extractDirUsable()` gilt – also nie etwas, das ein früherer Lauf oder ein anderes Projekt hinterlassen hat. Wer die Daten braucht, extrahiert selbst: `extractDrawings()` liest die Zeichnungen nur zum Auswerten (`OcDrawing::open(path, true)`) und ändert sie nicht. So arbeitet die Sensorliste.

*Dateien im Format der Skripte.* Die extrahierten CSV-Dateien werden so geschrieben, wie LISP sie schrieb (Windows-1252, alles andere als `\U+XXXX`). Für das Füllen der GA-FL-Blätter reicht der Gesamtlauf die Zeilen im Speicher weiter; der Umweg über die Datei beschädigte früher Zeichen wie `’`.

Geprüft wird eine Änderung am besten so wie die Übertragung selbst: ein Projekt vor und nach der Änderung erzeugen und die Attribute aller Blätter vergleichen.

**Theming** (`ui/Theming.cpp`) – Gleicht die Oberfläche an BricsCADs Hell-/Dunkeleinstellung an. Zwei Punkte sind dabei nicht offensichtlich:

*BricsCADs Oberfläche ist MFC-basiert, nicht Qt.* Es gibt also keine Qt-Palette des Hosts zu erben. Maßgeblich ist stattdessen die Systemvariable `COLORTHEME` (0 = dunkel, 1 = hell), gelesen über `acedGetVar`.

*Der Stil muss Fusion sein.* Der ab Qt 6.8 auf Windows 11 voreingestellte `windows11`-Stil zeichnet Flächen, Rahmen und abgerundete Ecken selbst und ignoriert die Palette weitgehend – eine dunkle Palette bliebe dort wirkungslos. Fusion respektiert die Palette vollständig und zeichnet eckig, was zugleich BricsCADs Erscheinungsbild entspricht.

Farben werden nirgends fest verdrahtet. Beschriftungen bekommen über `Theming::setRole()` eine Rolle (`Muted`, `Success`, `Warning`, `ErrorBold` …), die als Qt-Property am Widget hängt. `Theming::apply()` rechnet alle Rollen neu durch – deshalb folgt auch ein bereits gebautes Fenster einem Themenwechsel. `OpenCirtWindow::applyTheme()` ergänzt das um ein Neuzeichnen des Protokolls, dessen Farben als HTML im Text stecken und sich sonst nicht mehr ändern ließen.

Aufgerufen wird `applyTheme()` bei jedem `OPENCIRT` – ein Themenwechsel greift also nach Schließen und erneutem Öffnen des Fensters.

### Qt 6.8+ Windows-Kompatibilität

Die Datei `src/windows_fix.h` wird über `/FI` (Force-Include) in alle Kompilierungseinheiten eingebunden, einschließlich MOC-generierter Dateien. Sie löst Konflikte zwischen Qt 6.8+ internen Windows-SDK-Includes und den BRX-SDK-Headern.

### MFC-Stubs

Das Verzeichnis `src/mfc_stubs/` enthält leere Header-Dateien (`afxwin.h`, `afxext.h` etc.), die BRX-SDK-Includes befriedigen, ohne MFC-Abhängigkeiten einzuführen. Das Plugin verwendet Qt6 statt MFC.

### Linux

BricsCAD für Linux unterscheidet sich an einigen Stellen so, dass der Quelltext darauf Rücksicht nehmen muss:

**Qt ist schon da.** BricsCAD V26 für Linux bringt Qt 6.8.2 mit und hat beim Laden des Plugins bereits eine `QApplication`. Das Plugin benutzt sie mit (`OpenCirtPlugin.cpp`); unter Windows erzeugt es seine eigene. Gelöscht wird sie beim Entladen in keinem Fall: ein zweites Qt-Plugin im selben Prozess (batchTool) kann sie weiter benutzen, und BricsCAD räumt beim Beenden auf. Daraus folgt: Das Plugin muss gegen genau diese Qt-Version gebaut werden, es wird ohne RPATH gelinkt (`CMAKE_SKIP_RPATH`), und außer dem BRX-Einstiegspunkt sind alle Symbole verborgen (`-fvisibility=hidden`), damit sie nicht mit gleichnamigen des Hosts kollidieren.

**Dateidialoge.** BricsCAD liefert keine Plattform-Themen für Qt mit; `QFileDialog` erscheint unter Linux deshalb in der Qt-eigenen Form, nicht als GTK-Dialog. BricsCADs eigene Dialoge (z. B. der Dateiname beim PDF-Publish) sind GTK-Dialoge.

**LISP.** `(getenv "TEMP")` liefert `nil`, Dateimuster in `vl-directory-files` unterscheiden Groß- und Kleinschreibung, und LISP schreibt Textdateien in Windows-1252. Schwerer wiegen zwei Fehler in BricsCAD selbst, die LISP über viele Dokumente unzuverlässig machen (beschrieben in `KNOWN_ISSUES.md` des batchTool-Repositories). Deshalb arbeitet openCirt seit 1.7 ohne LISP. Wer eine Funktion neu baut, die über viele Zeichnungen läuft, schreibt sie als Operation auf der Side-Database.

**Schrift.** BricsCAD für Linux rechnet die Unterlänge einer TrueType-Schrift kleiner als BricsCAD für Windows (Arial: 0,202 statt 0,296 der Texthöhe). Unten ausgerichteter Text rutscht deshalb nach jeder Änderung unter Linux rund ein Zehntel der Texthöhe nach unten – im Editor wie in der Side-Database. Das Plugin gleicht das aus: Wer Text oder Breitenfaktor eines `AcDbText` oder Attributs ändert, ruft danach `adjustTextAlignment()` aus `core/TextAlignment.h` statt `adjustAlignment()`. Eine von Hand gesetzte Lage (`setPosition`) hilft nicht, BricsCAD berechnet sie beim Schließen des Objekts aus dem Ausrichtungspunkt neu; die Funktion hebt deshalb den Ausrichtungspunkt an.

**Verzeichnisse.** `QDirIterator` liefert unter Linux in beliebiger Reihenfolge, unter Windows (NTFS) nach Namen geordnet. Wo die Reihenfolge sichtbar wird oder Nummern bestimmt, ist ausdrücklich zu sortieren.

**Fremde Programme.** BricsCAD stellt sein Programmverzeichnis vor `LD_LIBRARY_PATH`; ein Kindprozess erbt das und fände dort Bibliotheken, die nicht zu ihm passen. Fremde Programme (LibreOffice) deshalb mit `OpenCirtTab::externalToolEnvironment()` starten. Programme nicht an festen Orten allein suchen: je nach Distribution und Paketart (Paket, `/opt`, Flatpak, Snap) liegen sie woanders – `findLibreOffice()` zeigt die Reihenfolge, `OPENCIRT_LIBREOFFICE` übersteuert sie. Ein Flatpak sieht `/tmp` des Rechners nicht; was es lesen oder schreiben soll, gehört in den Projektordner. Und: erst prüfen, ob das Programm seine Arbeit getan hat, dann löschen.

**Header.** `inc/Platform/substitutes` des BRX SDK enthält leere Ersatz-Header für `windows.h` und andere. Sie gehören nur unter Linux in den Include-Pfad; unter Windows verdecken sie die echten Header. `src/mfc_stubs` wird umgekehrt nur unter Windows gebraucht.

## Build-Konfiguration

Die Root-`CMakeLists.txt` ist die einzige Build-Datei. Sie definiert:
- Qt6-Pfad, BRX-SDK-Pfad, BricsCAD-Installationspfad
- Compiler-Flags inkl. Force-Include von windows_fix.h
- Alle Source-/Header-Dateien explizit (kein GLOB) – neue Dateien müssen in `PLUGIN_SOURCES` bzw. `PLUGIN_HEADERS` eingetragen werden
- Linker-Konfiguration gegen brx26.lib und Qt6; unter Linux gegen `libbrx26.so`, `libTD_Alloc.so`, `libTD_Root.so` der Installation und mit `--whole-archive` gegen `libdrx_entrypoint.a`

### Lokale Pfade anpassen

`CMakeLists.txt` trägt Vorgaben für beide Plattformen:

| Variable | Windows | Linux |
|---|---|---|
| `QT6_DIR` | `C:/Qt/6.8.3/msvc2022_64` | `~/.local/opt/opencirt-toolchain/Qt/6.8.2/gcc_64` |
| `BRICSCAD_DIR` | `C:/Program Files/Bricsys/BricsCAD V26 de_DE` | `/opt/bricsys/bricscad/v26` |
| `OC_SYSROOT` | – | `~/.local/opt/opencirt-toolchain/sysroot` (lokal entpackte Entwicklerpakete, optional) |

Sie lassen sich überschreiben, ohne die Datei zu ändern: `cmake -DQT6_DIR=…` oder eine gleichnamige Umgebungsvariable. Das BRX SDK wird immer unter `external/brx_sdk` erwartet.

### Build und laufendes BricsCAD

Solange BricsCAD läuft, hält es das geladene Plugin geöffnet; MSBuild kann die Datei dann nicht ersetzen und der Build bricht mit einem Linker-Fehler ab. `CLEAN_BUILD.bat` ruft deshalb in Schritt 0 `tools/Close-BricsCAD.ps1` auf. Das Skript fordert BricsCAD zunächst regulär zum Beenden auf, sodass Speichern-Rückfragen wie gewohnt erscheinen, und fragt erst nach 30 Sekunden nach hartem Beenden. Mit `-Force` beziehungsweise `CLEAN_BUILD.bat /force` entfällt die Rückfrage.

Zu beachten: zeigt der Startup-Suite-Eintrag auf `build_windows/Release/opencirt-<Version>.brx`, löscht Schritt 1 genau diesen Ordner. Zwischen Build-Start und Build-Ende sollte BricsCAD daher nicht gestartet werden.

Unter Linux gibt es diese Sperre nicht: Der Linker ersetzt die Datei, ein laufendes BricsCAD behält die geladene Fassung bis zum nächsten Start. `CLEAN_BUILD.sh` beendet BricsCAD deshalb nicht.

## Konventionen

- C++17 Standard
- Qt-Coding-Conventions (camelCase für Methoden, m_-Prefix für Member)
- Deutsche Kommentare in domänenspezifischem Code (GA-Planung)
- SCR-Dateien (PDF-Publish): keine Leerzeilen (Leerzeile = ENTER = stört Kommandos)
- Plattformabhängiges hinter `#ifdef _WIN32` / `#ifndef _WIN32`; der Windows-Zweig bleibt dabei unverändert
- Pfade für Dateien und Anzeige über `QDir::toNativeSeparators()`, nicht über festes Ersetzen von Schrägstrich durch Backslash
- Keine festen Farbwerte in der Oberfläche – Farben über `Theming::setRole()` oder die abgeleiteten Funktionen aus `Theming.h` beziehen, sonst bricht die Themenumschaltung

## Zwei Plugins, gemeinsame Dateien

openCirt (`openCirtPlugin`) und batchTool (`batchToolPlugin`) sind seit 2.0 getrennte Repositories mit getrennten Fenstern und getrennter Verarbeitung. Gleich sind in beiden:

- `src/ui/Theming.*` – Hell-/Dunkelthema
- `src/windows_fix.h`, `src/brx_force_include.h`, `src/mfc_stubs/` – Header-Fixes für Qt 6.8+ und das BRX SDK
- der Rumpf von `CMakeLists.txt` (Pfade, Compiler- und Linker-Einstellungen), `CLEAN_BUILD.sh`, `CLEAN_BUILD.bat`, `tools/copy_plugin.cmake`, `tools/Close-BricsCAD.ps1`
- der Aufbau des Einstiegspunkts (`src/plugin/*Plugin.cpp`): Befehlsgruppe, QApplication-Behandlung

Eine Korrektur an einer dieser Dateien gehört in beide Repositories. Beide Plugins lassen sich gleichzeitig laden; sie benutzen verschiedene Befehlsgruppen (`OPENCIRT_CMDS`, `BATCHTOOL_CMDS`) und verschiedene `QSettings`.
