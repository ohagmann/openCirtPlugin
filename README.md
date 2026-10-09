# openCirt – GA-Planungssoftware für BricsCAD

Ein BRX-Plugin (C++/Qt6) zur Erstellung von Planungsunterlagen für die Gebäudeautomation (nach VDI 3814) in BricsCAD V26 unter Windows und Linux. Aus Schema-Zeichnungen mit standardisierten Symbolen entstehen Betriebsmittelkennzeichen, BAS-Adressen, GA-Funktionslisten, Summenblätter, Deckblätter, Inhaltsverzeichnis, Datenpunkt- und Sensorlisten und das PDF-Gesamtdokument – aus einem Fenster heraus, ohne die Zeichnungen einzeln zu öffnen. openCirt ist DIE freie GA-Planungssoftware für alle - kostenlos, hocheffizient und einfach zu bedienen.

Bis Version 1.7.3 war openCirt ein Tab im Plugin batchTool/openCirt. Seit 2.0.0 sind es zwei Plugins: **openCirt** (dieses Repository) und **[batchTool](https://github.com/ohagmann/batchToolPlugin)** für die Stapelverarbeitung beliebiger DWG-Dateien (Text, Attribute, Layer, LISP). Beide lassen sich gleichzeitig in BricsCAD laden.

> ## ⚠️ Hinweis: Bildschirmflackern (Photosensitivität)
>
> Beim PDF-Publish öffnet eine zweite BricsCAD-Instanz die Zeichnungen in schneller Folge im sichtbaren Fenster, plottet sie und schließt sie wieder. Dabei entsteht ein **rasches, großflächiges Flackern** des Bildschirms.
>
> Solche schnellen Hell-Dunkel-Wechsel können bei Menschen mit **photosensitiver Epilepsie** Anfälle auslösen und auch bei nicht betroffenen Personen Unwohlsein, Kopfschmerzen oder Augenbelastung verursachen. Viele Betroffene wissen nichts von ihrer Empfindlichkeit, bis ein Anfall auftritt.
>
> **Empfehlung:** Während eines laufenden Publish-Vorgangs nicht dauerhaft auf den Bildschirm schauen, das Fenster minimieren oder den Arbeitsplatz verlassen. Personen mit bekannter Photosensitivität sollten den Lauf nicht beobachten.
>
> Alle übrigen Funktionen bearbeiten die Zeichnungen als Side-Database ohne Bildaufbau.

## Features

Ein Fenster, oben der Projektordner, darunter die Funktionen:

- **Projekt aufbauen** – Quellzeichnungen aus der Erstellliste (CSV) erzeugen und nach Los / ASP / Gewerk / Anlage einsortieren
- **Projekt erstellen** – der Gesamtlauf: Plankopf, Deckblätter, BMK-Nummerierung, BAS-Generierung, Datenextraktion, GA-Funktionslisten, Summenblätter, Textbreiten
- **BAS konfigurieren** – Aufbau des Benutzeradressierungssystems als Tabelle bearbeiten
- **Projekt bereinigen** – temporäre Dateien und Sicherungskopien im Zeichnungsordner löschen
- **PDF publizieren** – Multi-Sheet-PDF mit Inhaltsverzeichnis
- **IO-Liste erstellen** – Datenpunktliste bzw. IO-Belegungsliste als CSV
- **Sensorliste erstellen** – Fühler und Sensoren aus den GA-FL-Daten als CSV

Das Protokoll im unteren Teil des Fensters ist die einzige Ausgabe; Dialoge erscheinen nur für Rückfragen, Fehler und die Zusammenfassung eines Laufs.

## Voraussetzungen

Windows:

- **BricsCAD V26** (Windows, 64-Bit)
- **BRX SDK V26** (separat von Bricsys zu beziehen, siehe unten)
- **Qt 6.8+** (MSVC 2022, 64-Bit)
- **CMake 3.20+**
- **Visual Studio 2022** (MSVC v143 Toolset)

Linux:

- **BricsCAD V26** (64-Bit, getestet mit V26.2.07 unter Ubuntu)
- **BRX SDK V26** – dasselbe SDK wie unter Windows, die Header sind plattformneutral
- **Qt 6.8.2** (gcc_64) – genau die Version, die BricsCAD mitbringt. Das Plugin läuft im BricsCAD-Prozess und benutzt dessen Qt-Bibliotheken; das SDK wird nur zum Bauen gebraucht
- **CMake 3.20+**, **Ninja**, **g++** mit C++17
- OpenGL-Entwicklerdateien (`libgl-dev` oder gleichwertig), die Qt beim Konfigurieren verlangt

Zur Laufzeit außerdem **LibreOffice** (oder unter Windows Excel) zum Umwandeln der GA-FL-Referenz `GA_FL_VORLAGE.ods`.

### BRX SDK

Das BRX SDK ist proprietär und wird von Bricsys bereitgestellt. Es ist nicht Teil dieses Repositories. Nach dem Bezug muss das SDK unter `external/brx_sdk/` abgelegt werden, sodass die Struktur wie folgt aussieht:

```
external/
  brx_sdk/
    inc/         ← Header-Dateien
    inc64/
    lib64/       ← brx26.lib etc.
    docs/
```

Das SDK kann über das Bricsys Developer Network bezogen werden: https://www.bricsys.com/en-eu/developers

Liegt das SDK an anderer Stelle (etwa ein gemeinsames für openCirt und batchTool), zeigt `BRX_SDK_DIR` darauf: als CMake-Variable (`cmake -DBRX_SDK_DIR=…`) oder als Umgebungsvariable. Unter Linux genügt auch eine Verknüpfung `external/brx_sdk`; unter Windows muss der Ordner kopiert sein oder `BRX_SDK_DIR` gesetzt werden.

## Build

```cmd
CLEAN_BUILD.bat
```

Das Skript führt folgende Schritte aus:
1. Beendet laufende BricsCAD-Instanzen – solange BricsCAD läuft, hält es das geladene Plugin geöffnet und der Build scheitert am Linker. Zuerst wird BricsCAD regulär zum Beenden aufgefordert (Speichern-Rückfragen erscheinen wie gewohnt), erst nach 30 Sekunden folgt eine Rückfrage zum harten Beenden. `CLEAN_BUILD.bat /force` überspringt diese Rückfrage
2. Löscht alte Build-Artefakte
3. CMake-Konfiguration (Visual Studio 17 2022, x64, Release)
4. MSBuild-Kompilierung

Das fertige Plugin liegt anschließend unter `build_windows\Release\opencirt-<Version>.brx` (die Version stammt aus dem obersten Abschnitt von `CHANGELOG.md`, z.B. `opencirt-2.0.0.brx`) und wird zusätzlich nach `sample_project/00- BricsCAD Plugin/00- Windows Version/` gelegt; ältere Stände dort werden entfernt.

### Linux

```sh
./CLEAN_BUILD.sh
```

Das Skript löscht `build_linux`, konfiguriert mit CMake (Generator Ninja, Release) und baut. Das fertige Plugin liegt unter `build_linux/Release/opencirt-<Version>.lrx` und wird zusätzlich nach `sample_project/00- BricsCAD Plugin/01- Linux Version/` gelegt; ältere Stände dort werden entfernt. Ein laufendes BricsCAD wird nicht beendet; es behält die geladene Fassung, die neue gilt nach dem nächsten Start.

Alles Nötige lässt sich ohne Systemrechte im Benutzerverzeichnis einrichten. Vorgabe ist `~/.local/opt/opencirt-toolchain`:

```sh
TC=~/.local/opt/opencirt-toolchain
python3 -m venv $TC/venv
$TC/venv/bin/pip install cmake ninja aqtinstall
$TC/venv/bin/aqt install-qt linux desktop 6.8.2 linux_gcc_64 -O $TC/Qt
```

Fehlen die OpenGL-Entwicklerdateien im System, genügt es, die Pakete herunterzuladen und nach `$TC/sysroot` zu entpacken (`apt-get download libgl-dev libglx-dev libopengl-dev libegl-dev libgles-dev libglvnd-dev libvulkan-dev libxkbcommon-dev libx11-dev x11proto-dev`, dann je Paket `dpkg -x <paket>.deb $TC/sysroot`).

### Manuelle Build-Schritte

```cmd
mkdir build_windows
cd build_windows
cmake -G "Visual Studio 17 2022" -A x64 -DCMAKE_BUILD_TYPE=Release ..
cmake --build . --config Release
```

### Qt- und BricsCAD-Pfade anpassen

Die Vorgaben stehen in der Root-`CMakeLists.txt`:

| Variable | Windows | Linux |
|---|---|---|
| `QT6_DIR` | `C:/Qt/6.8.3/msvc2022_64` | `~/.local/opt/opencirt-toolchain/Qt/6.8.2/gcc_64` |
| `BRICSCAD_DIR` | `C:/Program Files/Bricsys/BricsCAD V26 de_DE` | `/opt/bricsys/bricscad/v26` |
| `OC_SYSROOT` | – | `~/.local/opt/opencirt-toolchain/sysroot` (optional) |

Abweichende Pfade lassen sich ohne Änderung der Datei setzen: beim Aufruf mit `cmake -DQT6_DIR=… -DBRICSCAD_DIR=…` oder über gleichnamige Umgebungsvariablen.

## Installation in BricsCAD

### Einmalig (zum Testen)

1. BricsCAD starten
2. Befehl: `APPLOAD`
3. Zur Datei `opencirt-<Version>.brx` (Linux: `opencirt-<Version>.lrx`) navigieren und laden
4. In der Kommandozeile erscheint: *"openCirt <Version> geladen. Befehl: OPENCIRT (Kurzform OC)"*

### Automatisch bei jedem Start

1. `APPLOAD` aufrufen
2. Unten auf *"Inhalt..."* (Startup Suite) klicken
3. `opencirt-<Version>.brx` (Linux: `opencirt-<Version>.lrx`) zur Startup Suite hinzufügen. Nach einem Versionswechsel den Eintrag auf die neue Datei umstellen – der Dateiname trägt die Version.

## Befehle

| Befehl | Beschreibung |
|---|---|
| `OPENCIRT` | Öffnet das openCirt-Fenster |
| `OC` | Kurzform von `OPENCIRT` |

## Bedienung

### Grundsätzlicher Ablauf

1. Oben im Fenster den **Projektordner** wählen (Feld oder „Durchsuchen…"). Der zuletzt benutzte Ordner wird gemerkt.
2. Die gewünschte Funktion anklicken. Alle Funktionen außer dem PDF-Publish bearbeiten die Zeichnungsdateien direkt als Side-Database – ohne sie im Editor zu öffnen, ohne LISP, unter Windows und Linux gleich.
3. Der Fortschrittsbalken zeigt den laufenden Schritt; das **Protokoll** darunter sammelt Ergebnisse, Zahlen und Fehler. Ein Fehler steht als rote Zeile im Protokoll und mit derselben Zeile als Dialog.

Das Fenster übernimmt BricsCADs Hell-/Dunkeleinstellung. Maßgeblich ist die Systemvariable `COLORTHEME`; sie wird bei jedem Aufruf von `OPENCIRT` neu gelesen. Nach einem Themenwechsel genügt es also, das Fenster zu schließen und den Befehl erneut aufzurufen.

### Funktionen

Die eigentliche Projekterstellung läuft über einen einzigen Knopf – Plankopf, Deckblätter, BMK, BAS, GA-FL, Summen und Textbreiten sind Schritte darin und werden nicht einzeln bedient.

- **Projekt aufbauen** – erzeugt die Quellzeichnungen aus der Erstellliste (CSV): Vorlage kopieren, nach Los / ASP / Gewerk / Anlage einsortieren, Attribute setzen, Stempel und Meldungsblöcke füllen. Wahlweise als Vorschau, die nur das Log schreibt. Beschreibung der Liste: Bedienungsanleitung Abschnitt 3.3
- **Projekt erstellen** – der Gesamtlauf in korrekter Reihenfolge:
  - *Plankopf* – CSV-basierte Plankopf-Attribute setzen (AG, AN, PR etc.)
  - *Deckblätter* – für die Los/ASP/Gewerk/Anlage-Hierarchie, inkl. ASP, Gewerk und Anlage aus der Ordnerstruktur
  - *BMK-Nummerierung* – Betriebsmittelkennzeichen automatisch vergeben
  - *BAS-Generierung* – Benutzeradressierungssystem aus BAS.csv erzeugen
  - *GA-FL* – Funktionslisten zweiphasig: Datenextraktion aus Quell-DWGs, dann GA-FL-Blätter erzeugen und füllen
  - *Summenblätter* – Gewerk-Summe, ASP-Summe, Los-Summe, Projekt-Summe sowie eine Gewerke-Auswertung je Los über alle ASPs
  - *Textbreiten* – Breitenfaktor in GA-FL- und Summenblättern korrigieren, auch für Werte innerhalb der GA-FL-Blockdefinition
- **BAS konfigurieren** – zeigt die Segmente der `BAS.csv` als Tabelle mit Art (Text / Attribut) und Wert; Zeilen lassen sich anfügen, entfernen und verschieben. „Speichern" schreibt die `BAS.csv` im Format des Plugins und sichert die bisherige Datei als `BAS.csv.bak`
- **Projekt bereinigen** – temporäre Dateien und Backups im Zeichnungsordner löschen (`*.bak`, `*.dwl`, `*.dwl2`, `*.sv$`, `*.ac$`, `*.tmp`, `*.log`)
- **PDF publizieren** – DSD-basierter Multi-Sheet-PDF-Export inkl. Inhaltsverzeichnis (22 Einträge pro Seite, Plankopf aus `plankopfdaten.csv`). Das Plotten läuft in einer eigenen Batch-Instanz von BricsCAD
- **IO-Liste erstellen** – Export als CSV-Datei nach `06- Plot` (Referenz: `iomodule.csv`). Im Dialog wird nach Integrationsart gefiltert (Attribut `OC_INTEGRATIONSART_DP_n`): leer = alle Datenpunkte (`Datenpunktliste.csv`), `HW` = SPS-/DDC-Belegungsliste mit Modul- und Kanalzuordnung (`IO-Belegungsliste.csv`), `BUS;SMI` = mehrere Arten (`Datenpunktliste_BUS-SMI.csv`). Die Integrationsart steht als eigene Spalte in der Liste; *Modul-Typ* wird nur für HW-Zeilen gefüllt
- **Sensorliste erstellen** – Keyword-Matching gegen Blockattribute (Referenz: `sensor.csv`), Ausgabe als `Sensorliste.csv` nach `06- Plot`

Die Listen sind CSV-Dateien in UTF-8 mit BOM, Trennzeichen Semikolon, Zeilenende CRLF. Zeile 1 ist die Kopfzeile. LibreOffice und Excel öffnen sie direkt; eine Vorlage ist nicht nötig.

Die Bedienungsanleitung mit Projektstruktur, Erstellliste, Symbolen und allen Schritten: [sample_project/BEDIENUNGSANLEITUNG.md](sample_project/BEDIENUNGSANLEITUNG.md).

## Projektstruktur

```
├── CMakeLists.txt              Root-Build-Konfiguration
├── CLEAN_BUILD.bat             Build-Skript Windows
├── CLEAN_BUILD.sh              Build-Skript Linux
├── LICENSE                     BSL 1.1 Lizenz
├── .gitignore
├── docs/
│   └── DEVELOPMENT.md          Entwickler-Hinweise
├── tools/
│   ├── Close-BricsCAD.ps1      Beendet BricsCAD vor dem Build (Windows)
│   ├── copy_plugin.cmake       Legt das fertige Plugin ins Beispielprojekt
│   └── oc_summen_vergleich.py  Prüfwerkzeug für Summenblätter
├── external/
│   └── brx_sdk/                BRX SDK (nicht im Repository)
├── sample_project/
│   ├── 00- BricsCAD Plugin/    Kompiliertes Plugin (opencirt-<Version>.brx für Windows, opencirt-<Version>.lrx für Linux)
│   ├── 01- Referenzen/
│   │   ├── BAS.csv             BAS-Konfiguration
│   │   ├── GA_FL_VORLAGE.ods   GA-FL Vorlage
│   │   ├── Erstellliste_VORLAGE.csv Erstellliste mit allen Spalten und Beispielzeilen
│   │   ├── iomodule.csv        IO-Modul-Referenzdaten
│   │   ├── opencirt_config.json Projektkonfiguration
│   │   ├── plankopfdaten.csv   Plankopf-Attribute
│   │   └── sensor.csv          Sensor-Referenzdaten
│   ├── 02- Skripte/            LISP-Skripte der Schritte des Gesamtlaufs bis Version 1.6 – heute Referenz für die Übertragung nach C++ (core/OpenCirtEngine)
│   ├── 03- Blockbibliothek/    DWG-Blockvorlagen
│   ├── 04- Vorlagen/
│   │   ├── OC_RSH_Plankopf_quer_V22.dwg
│   │   ├── OC_VORLAGE_DIN_A0.dwg
│   │   ├── OC_VORLAGE_DIN_A2_V14.dwg
│   │   ├── OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg
│   │   ├── OC_VORLAGE_DIN_A2_HISTORIE_V1.dwg
│   │   ├── OC_VORLAGE_GA_FL.dwg
│   │   └── VDI3814_GA_FL_V_1_0.dwg
│   ├── 05- Projekt Zeichnungen/
│   ├── 06- Plot/
│   └── BEDIENUNGSANLEITUNG.md  Bedienungsanleitung (12 Kapitel)
└── src/
    ├── windows_fix.h           Qt 6.8+ / Windows SDK Kompatibilität
    ├── brx_force_include.h     BRX Platform-Header
    ├── core/
    │   ├── OpenCirtEngine.cpp/h    Schritte des Gesamtlaufs auf der Side-Database (BMK, BAS, Extraktion, GA-FL, Textbreiten)
    │   ├── ProjectBuilder.cpp/h    Projektaufbau aus der Erstellliste
    │   └── TextAlignment.h         Lage ausgerichteter Texte (Unterschied BricsCAD Windows/Linux)
    ├── mfc_stubs/              Leere MFC/ATL-Stubs (Qt-basiert, kein MFC; nur Windows)
    ├── plugin/
    │   └── OpenCirtPlugin.cpp/h    BRX Entry Point, Befehle OPENCIRT und OC
    ├── ui/
    │   ├── OpenCirtWindow.cpp/h    Fenster: Bedienflaeche oben, Protokoll unten, Menü, Thema
    │   ├── OpenCirtTab.cpp/h       Bedienflaeche: Projektordner, Funktionen, Fortschritt; steuert die Läufe
    │   ├── BasConfigDialog.cpp/h   Dialog „BAS konfigurieren"
    │   └── Theming.cpp/h           Hell-/Dunkelthema aus BricsCAD COLORTHEME
    └── utils/
        ├── SensorKeywordLoader.cpp/h   Keyword-Abgleich für die Sensorliste
        └── CsvListWriter.cpp/h         Listen als CSV schreiben
```

## Hinweise

- Vor „Projekt erstellen" den Zeichnungsordner sichern (z.B. als ZIP). Der Gesamtlauf legt je Zeichnung eine `*.bak` an, die den Stand vor dem Lauf zeigt.
- Für „Projekt aufbauen" und „Projekt erstellen" darf keine Zeichnung des Projekts in BricsCAD geöffnet sein.
- Die GA-FL-Referenz `GA_FL_VORLAGE.ods` wird bei jedem Gesamtlauf mit LibreOffice (oder Excel) nach CSV umgewandelt. Unter Linux findet das Plugin LibreOffice als Paket, unter `/opt`, als Flatpak und im Suchpfad; `OPENCIRT_LIBREOFFICE` übersteuert die Suche.
- Alle Funktionen laufen unter Windows und Linux gleich; die Ergebnisse wurden an Projekten mit bis zu 867 Blättern verglichen.

## Lizenz

Business Source License 1.1 (BSL 1.1) – siehe [LICENSE](LICENSE) und [ADDITIONAL_TERMS](ADDITIONAL_TERMS).

Kurzfassung: Nutzung für interne Zwecke, kommerzielle Projekte und Dienstleistungen ist erlaubt. Verkauf als eigenständiges Produkt, SaaS-Angebote und proprietäre Forks sind untersagt. Ab dem Change Date (2030-03-02) wird die Software unter AGPLv3 verfügbar. Nutzung auf eigenes Risiko – vor jedem Lauf Backups erstellen!

## Technologie

Entwickelt mit BRX SDK V26, Qt 6, C++17, CMake.
