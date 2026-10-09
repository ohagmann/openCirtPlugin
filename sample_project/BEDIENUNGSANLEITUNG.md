# openCirt – Bedienungsanleitung

**Version:** 2.0  
**Stand:** Oktober 2026  
**Für:** BricsCAD V26 mit dem openCirt-Plugin ab Version 2.0

---

## Inhaltsverzeichnis

1. [Überblick](#1-überblick)
2. [Installation](#2-installation)
3. [Projektstruktur anlegen](#3-projektstruktur-anlegen)
4. [Symbole einfügen und Zeichnungen erstellen](#4-symbole-einfügen-und-zeichnungen-erstellen)
5. [Plankopf-Daten setzen](#5-plankopf-daten-setzen)
6. [BMK-Nummerierung](#6-bmk-nummerierung)
7. [BAS-Generierung](#7-bas-generierung)
8. [GA-Funktionslisten erstellen](#8-ga-funktionslisten-erstellen)
9. [Deckblätter generieren](#9-deckblätter-generieren)
10. [Inhaltsverzeichnis und PDF-Publish](#10-inhaltsverzeichnis-und-pdf-publish)
11. [Gesamtprojekt erstellen](#11-gesamtprojekt-erstellen)
12. [Tipps und Fehlerbehebung](#12-tipps-und-fehlerbehebung)

---

## 1. Überblick

openCirt automatisiert die Erstellung von GA-Planungsunterlagen in BricsCAD. Aus Schema-Zeichnungen mit standardisierten Symbolen werden automatisch erzeugt:

- **BMK-Nummern** (Betriebsmittelkennzeichen) für alle Datenpunkte
- **BAS-Bezeichner** (Benutzeradressierungssystem) nach frei konfigurierbarer Vorlage
- **GA-Funktionslisten** mit allen Datenpunkten, GA-Funktionen und BAS-Adressen
- **Summenblätter** (Projekt-, Los-, ASP, Gewerke- und Anlagensummen)
- **Deckblätter** für jede Hierarchie-Ebene (Projekt, Los, ASP, Gewerk, Anlage)
- **Inhaltsverzeichnis** mit automatischer Seitennummerierung
- **PDF-Gesamtdokument** als Multi-Sheet-PDF

Der gesamte Ablauf von der Schema-Zeichnung bis zum fertigen PDF-Planungspaket ist automatisiert.

---

## 2. Installation

### 2.1 Plugin laden

1. Die Datei `opencirt-<Version>.brx` (z.B. `opencirt-2.0.0.brx`) aus dem Ordner `00- BricsCAD Plugin/00- Windows Version/` in einen festen Speicherort kopieren (z.B. `C:\BricsCAD Plugins\`).
2. BricsCAD V26 starten.
3. Befehl `APPLOAD` eingeben.
4. Zur Datei `opencirt-<Version>.brx` navigieren und „Laden" klicken.
5. In der Kommandozeile erscheint: *„openCirt <Version> geladen. Befehl: OPENCIRT (Kurzform OC)"*

**Linux:** Die Datei heißt dort `opencirt-<Version>.lrx` und liegt in `00- BricsCAD Plugin/01- Linux Version/`. Sie wird genauso über `APPLOAD` geladen; als Speicherort eignet sich zum Beispiel `~/BricsCAD Plugins/`. Weitere Dateien sind nicht nötig, das Plugin benutzt die Qt-Bibliotheken, die BricsCAD mitbringt. Zum Umwandeln der GA-FL-Referenz (ODS) braucht das Plugin LibreOffice (Abschnitt 8.3).

### 2.2 Automatisch bei jedem Start laden

1. `APPLOAD` aufrufen.
2. Unten auf „Inhalt..." (Startup Suite) klicken.
3. `opencirt-<Version>.brx` (Linux: `opencirt-<Version>.lrx`) zur Startup Suite hinzufügen.

Der Dateiname trägt die Version. Nach einem Versionswechsel den Eintrag in der Startup Suite auf die neue Datei umstellen; die alte Datei kann weg.

### 2.3 Plugin öffnen

Befehl in der Kommandozeile: `OPENCIRT` (Kurzform `OC`)

Das Fenster zeigt oben den **Projektordner** (Feld mit „Durchsuchen…"), darunter die Funktionen mit dem Fortschrittsbalken und unten das **Protokoll**. Der zuletzt benutzte Projektordner wird gemerkt.

Bis Version 1.7 war openCirt ein Tab im Plugin batchTool/openCirt (Befehl `BATCHTOOL`). Die Stapelverarbeitung beliebiger DWG-Dateien (Text, Attribute, Layer, LISP) gibt es seit 2.0 als eigenes Plugin **batchTool**; beide lassen sich gleichzeitig laden.

---

## 3. Projektstruktur anlegen

Jedes openCirt-Projekt folgt einer festen Ordnerstruktur. Dieses Sample-Projekt dient als Vorlage – kopieren Sie den gesamten `04- OC SAMPLE`-Ordner und benennen Sie ihn um.

### 3.1 Ordnerstruktur

```
Projektname/
├── 00- BricsCAD Plugin/        Plugin-Binary (nur Referenz)
├── 01- Referenzen/             Konfigurationsdateien
│   ├── BAS.csv                 BAS-Aufbau (Segmente)
│   ├── GA_FL_VORLAGE.ods       Datenpunkt-Referenztabelle
│   ├── Erstellliste_VORLAGE.csv Erstellliste mit allen Spalten und Beispielzeilen (Abschnitt 3.3)
│   ├── plankopfdaten.csv       Plankopf-Stammdaten
│   └── opencirt_config.json    Plugin-Konfiguration (automatisch)
├── 02- Skripte/                LISP-Skripte der Schritte bis Version 1.6 – nur noch Referenz, werden nicht ausgeführt
│   ├── BmkNummerierung.lsp
│   ├── ExtractDP.lsp
│   ├── FillGaFl.lsp
│   ├── GenBas.lsp
│   └── TextBreitenAnpassenBloecke.lsp
├── 03- Blockbibliothek/        Symbole für Schema-Zeichnungen
│   ├── 00- Vorlagensymbol/     Basis-Vorlage zum Erstellen eigener Symbole
│   ├── 01- Pfeile/             Datenpunkt-Pfeile (AI, AO, BI, BO)
│   └── ...                     Weitere Symbole (Ventilator, Meldung etc.)
├── 04- Vorlagen/               DWG-Vorlagen (projektneutral, Plankopfdaten kommen aus plankopfdaten.csv)
│   ├── OC_RSH_Plankopf_quer_V21.dwg        Plankopf-Block
│   ├── OC_VORLAGE_DIN_A2_V14.dwg           Rahmen/Plankopf DIN A2 – Schema-Zeichnungen und Deckblätter (höchste V<n> gilt)
│   ├── OC_VORLAGE_DIN_A0.dwg               Rahmen/Plankopf DIN A0
│   ├── OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg  Inhaltsverzeichnis-Blatt inkl. Eintragsblock
│   ├── OC_VORLAGE_DIN_A2_HISTORIE_V1.dwg   Blatt Änderungshistorie
│   ├── OC_VORLAGE_GA_FL.dwg                GA-FL Blattvorlage
│   └── VDI3814_GA_FL_V_1_0.dwg             GA-FL Block (Quelle der Funktionsliste)
├── 05- Projekt Zeichnungen/    Hier entstehen die Zeichnungen
│   └── 01 Los 1/
│       └── 01 ASP01/
│           ├── 01 GA/          Gewerk: Gebäudeautomation
│           ├── 02 RLT/         Gewerk: Raumlufttechnik
│           ├── 03 HZG/         Gewerk: Heizung
│           └── ...
└── 06- Plot/                   Ausgabe (PDF, Listen als CSV)
```

### 3.2 Ordnerhierarchie der Zeichnungen

Die Ordnerstruktur unter `05- Projekt Zeichnungen/` folgt der GA-Hierarchie: Welche Ebene ein Ordner ist, ergibt sich aus seiner Lage; die Namen vergibt die Erstellliste, „ASP01" ist nur ein Beispiel.

```
Los → ASP → Gewerk → Anlage
```

**Beispiel:**
```
05- Projekt Zeichnungen/
└── 01 Los 1/
    └── 01 ASP01/
        ├── 01 GA/
        │   └── 01 SSK-1000/     ← Anlage
        │       └── 00 SSK interne Datenpunkte.dwg
        ├── 02 RLT/
        │   └── 01 TKA-1000/     ← Anlage
        │       └── 00 Hauptanlage.dwg
        └── 03 HZG/
            └── 01 WPU-1000/     ← Anlage
                └── 00 Wärmepumpe.dwg
```

**Wichtig:** Die Ordnernamen werden automatisch ausgewertet:
Die Nummerierung am Anfang (01, 02, ...) bestimmt die Sortierung, in allen Ebenen.

**Oberste Ebene = Projektblätter.** DWGs, die direkt in `05- Projekt Zeichnungen/` liegen (Projekt-Deckblatt, Revisionshistorie), gelten als Projektblätter, nicht als Quellzeichnungen: Sie erhalten im Gesamtlauf die Plankopf-Stammdaten aus `plankopfdaten.csv`, werden aber von BMK-Nummerierung, BAS-Generierung, Extraktion und GA-FL übersprungen. Der Dateiname ist dabei frei; ein Präfix `0000 ` sorgt dafür, dass sie im PDF vor Inhaltsverzeichnis und Summenblättern liegen.

### 3.3 Projekt aus der Erstellliste aufbauen

Statt die Zeichnungen von Hand anzulegen, lässt sich der ganze Ordner `05- Projekt Zeichnungen/` aus einer Liste erzeugen: **„Projekt aufbauen"** kopiert je Zeile eine Vorlage aus `04- Vorlagen/`, legt sie in die Ordnerhierarchie Los / ASP / Gewerk / Anlage und trägt die Attributwerte der Zeile ein. Die Funktion ersetzt das frühere LISP-Skript `OC_PROJECT_BUILD`; Listen, die damit liefen, laufen unverändert.

**ACHTUNG:** Der echte Lauf löscht vorher den gesamten Inhalt von `05- Projekt Zeichnungen/`. Vor dem Lauf den Projektordner sichern.

#### Die Erstellliste

Eine Vorlage mit allen Spalten liegt im Beispielprojekt: `01- Referenzen/Erstellliste_VORLAGE.csv`. Sie lässt sich in LibreOffice oder Excel öffnen, um eine eigene Liste zu beginnen (beim Speichern das Semikolon als Trennzeichen beibehalten). Ihre fünf Beispielzeilen zeigen die Zeilenarten: eine Projektzeile (Projekt-Deckblatt), eine Anlagenzeile mit Meldungsgruppe (Revisionshistorie mit Eintrag 1), eine Meldungszeile (Eintrag 2) und zwei Anlagenzeilen mit Blättern aus `OC_VORLAGE_DIN_A2_V14`. Mit „Projekt aufbauen" entstehen daraus vier Blätter in `00 Allgemein/` und `01 Los 1/`. Dabei wird auch das mitgelieferte Beispielblatt mit seinen Datenpunkten gelöscht; zum Ausprobieren also die Vorschau nehmen oder auf einer Kopie des Beispielprojekts arbeiten.

Eine CSV-Datei mit Semikolon als Trennzeichen, gespeichert als UTF-8 (mit oder ohne BOM) oder Windows-1252 – die Kodierung wird erkannt und steht im Log. Zeile 1 ist die Kopfzeile. Die Spalten werden am Namen erkannt, nicht an der Position; Groß-/Kleinschreibung und ein Doppelpunkt am Ende spielen keine Rolle (`Dateiname:` = `DATEINAME`).

| Spalte | Bedeutung |
|---|---|
| `Pos.`, `Index` | optional, werden ignoriert |
| `Vorlage` | Name der Vorlage ohne `.dwg`; gesucht wird in `04- Vorlagen/` samt Unterordnern |
| `Los`, `ASP`, `Gewerk`, `Anlage` | die vier Ordnerebenen |
| `Dateiname` | Name der Zeichnung ohne `.dwg`; die laufende Nummer davor vergibt das Plugin |
| alle übrigen | Attribut-Tags. Der Wert der Zelle wird in jedes Attribut mit diesem Tag geschrieben – auch ein leerer Wert |

Ordner und Dateien erhalten ihre Nummer in der Reihenfolge, in der sie in der Liste zum ersten Mal vorkommen: Ordner zweistellig ab `00`, Dateien zweistellig, ab 100 Blättern in einem Ordner dreistellig.

#### Zeilenarten

| Zeile | Erkennungsmerkmal | Wirkung |
|---|---|---|
| Anlagenzeile | Vorlage, Dateiname und alle vier Ebenen gefüllt | erzeugt ein Blatt in `NN Los/NN ASP/NN Gewerk/NN Anlage/` |
| Projektzeile | Vorlage und Dateiname gefüllt, alle vier Ebenen leer | erzeugt ein Projektblatt auf der obersten Ebene, Dateiname wörtlich ohne Nummer |
| Stempelzeile | Vorlage und Dateiname leer, `OC_ANLAGE` gefüllt | füllt einen weiteren Stempel des zuletzt erzeugten Blatts |
| Meldungszeile | Vorlage, Dateiname und `OC_ANLAGE` leer, mindestens eine `#`-Spalte gefüllt | hängt eine weitere Meldung an das zuletzt erzeugte Blatt |
| Trennzeile | Dateiname und `OC_ANLAGE` leer | wird übergangen, dient der Gliederung |

Eine Zeile mit Dateiname, in der nur ein Teil der vier Ebenen gefüllt ist, gilt als fehlerhaft: Sie wird mit Warnung übersprungen.

**Stempel.** Blöcke, deren Name `STEMPEL` enthält, werden nicht über die Anlagenzeile gefüllt, sondern gezielt angesprochen: Der Wert in `OC_ANLAGE` beginnt mit `[1]`, `[2]` … und trifft den Stempel, dessen Attribut `OC_ANLAGE` in der Vorlage mit derselben Kennung beginnt. Die Kennung ist Pflicht, auch bei nur einem Stempel. Findet sich zu einer Kennung kein Stempel, bricht der Lauf an dieser Stelle ab und nennt Liste, Zeile, Zeichnung und gesuchte Kennung; die bis dahin erzeugten Blätter bleiben erhalten.

**Meldungen.** Eine Spalte mit `#` im Namen (z. B. `OC_FCODE_DP_#`) gilt für alle Meldungen eines Blocks: Das `#` wird durch die laufende Nummer ersetzt. Die Anlagenzeile ist Meldung 1, jede folgende Meldungszeile die nächste. Die Meldungsgruppe (`OC_AKS`, `OC_BEZEICHNUNG` und alle `#`-Spalten) wird nur geschrieben, wenn `OC_AKS` in der Anlagenzeile gefüllt ist. Auf demselben Weg lässt sich die Revisionshistorie zeilenweise füllen (`OC_INDEX_AENDERUNG_#`, `OC_DATUM_AENDERUNG_#` …).

**Deckblatt-Schutz.** Enthält die Liste keine Projektzeile, bleibt eine Datei `*Deckblatt_A.dwg` auf der obersten Ebene beim Löschen stehen. Enthält sie mindestens eine Projektzeile, gehört die oberste Ebene der Liste und wird vollständig neu erzeugt.

#### Ausführung

1. Oben im Fenster den Projektordner wählen, dann auf **„Projekt aufbauen"** klicken.
2. Die Erstellliste auswählen.
3. **„Vorschau"** oder **„Aufbauen"** wählen.
   - *Vorschau* ändert nichts. Sie schreibt in das Log, was gelöscht und was erzeugt würde, samt aller Attributwerte und Warnungen.
   - *Aufbauen* zeigt zuerst die Zahl der zu löschenden Dateien und der zu erzeugenden Zeichnungen. Erst nach der Bestätigung wird gelöscht und aufgebaut.
4. Am Ende nennt eine Meldung die Zahl der erzeugten Blätter, Fehler und Warnungen.

Sind Zeichnungen aus `05- Projekt Zeichnungen/` noch geöffnet oder liegen dort Sperrdateien (`*.dwl`, `*.dwl2`), bricht der echte Lauf ab, bevor etwas gelöscht wird, und listet die betroffenen Dateien auf.

Das Log liegt im Projektordner: `OC_Log_<Datum>_<Uhrzeit>.txt`, bei der Vorschau mit der Endung `_DRY.txt`. Es empfiehlt sich, vor jedem echten Lauf die Vorschau laufen zu lassen und die Zeilen mit `WARN` zu prüfen.

Die Zeichnungen werden dabei nicht im Editor geöffnet, sondern direkt bearbeitet. Der Bildschirm flackert nicht, und ein Projekt mit 320 Blättern ist in rund einer halben Minute aufgebaut.

---

## 4. Symbole einfügen und Zeichnungen erstellen

### 4.1 Neue Zeichnung anlegen

1. Öffnen Sie die Vorlage `OC_VORLAGE_DIN_A2_V14.dwg` aus dem Ordner `04- Vorlagen/`.
2. Speichern Sie die Datei im passenden Ordner unter `05- Projekt Zeichnungen/`, z.B.:
   ```
   05- Projekt Zeichnungen/01 Los 1/01 ASP01/02 RLT/01 TKA-1000/00 Hauptanlage.dwg
   ```

### 4.2 Symbole aus der Blockbibliothek einfügen

Die Blockbibliothek (`03- Blockbibliothek/`) enthält vorgefertigte Symbole mit allen openCirt-Attributen.

**Symbol einfügen:**

1. In BricsCAD den Befehl `INSERT` (oder `EINFÜGE`) oder über die GUI "Block einfügen"verwenden.
2. Auf „Durchsuchen" klicken und zum Ordner `03- Blockbibliothek/` navigieren.
3. Das gewünschte Symbol auswählen, z.B. `OC_VORLAGE_VENTILATOR_OBEN_UNTEN_RLT_V3.dwg`.
4. Einfügepunkt in der Zeichnung bestimmen.

**Verfügbare Symboltypen:**

| Symbol | Beschreibung |
|---|---|
| `Symbolvorlage_20_DP_V_1_0.dwg` | Basis-Vorlage mit 20 Datenpunkten (zum Erstellen eigener Symbole) |
| `OC_VORLAGE_DP_PFEIL_AI_V1.dwg` | Datenpunkt-Pfeil: Analogeingang (AI) |
| `OC_VORLAGE_DP_PFEIL_AO_V1.dwg` | Datenpunkt-Pfeil: Analogausgang (AO) |
| `OC_VORLAGE_DP_PFEIL_BI_V1.dwg` | Datenpunkt-Pfeil: Binäreingang (BI) |
| `OC_VORLAGE_DP_PFEIL_BO_V1.dwg` | Datenpunkt-Pfeil: Binärausgang (BO) |
| `OC_VORLAGE_PFEIL_V1.dwg` | Allgemeiner Pfeil |
| `OC_VORLAGE_VENTILATOR_OBEN_UNTEN_RLT_V3.dwg` | Ventilator-Symbol (RLT) |
| `OC_VORLAGE_1_HW_MELDUNG_GA_V1.dwg` | Hardware-Meldung |

### 4.3 Eigene Symbole erstellen

1. Kopieren Sie `Symbolvorlage_20_DP_V_1_0.dwg` aus `03- Blockbibliothek/00- Vorlagensymbol/`.
2. Öffnen Sie die Kopie in BricsCAD.
3. Zeichnen Sie Ihre Grafik.
4. Die vorhandenen OC-Attribute bleiben erhalten – sie werden automatisch von openCirt befüllt.
5. Nicht benötigte Datenpunkte deaktivieren: Attribut `OC_FL_AKTIV_n` auf leer setzen (nur Datenpunkte mit Aktiv-Kennzeichen werden verarbeitet).

**Tipp:** Falls nach dem Bearbeiten eines Blocks die Attribut-Reihenfolge im Eigenschaftenfenster durcheinander ist, können Sie die ATTDEFs im Block-Editor (BEDIT) manuell löschen und in der gewünschten Reihenfolge neu anlegen.

### 4.4 Wichtige OC-Attribute in den Symbolen

Jedes openCirt-Symbol enthält folgende Attribute pro Datenpunkt (n = 1..20):

| Attribut | Beschreibung | Beispiel |
|---|---|---|
| `OC_BEZEICHNUNG` | Bezeichnung des Geräts/Symbols | „ZUL-Ventilator" |
| `OC_AKS` | Anlagenkennzeichen (wird von BMK befüllt) | „ZUV-" |
| `OC_FL_AKTIV_n` | Datenpunkt n aktiv? | „ja" / „" (leer = inaktiv) |
| `OC_REF_DP_n` | Referenzname für GA-FL-Vorlage (ODS-Lookup) | „MW_HW" |
| `OC_FCODE_DP_n` | Funktionscode | „MW_01" |
| `OC_INTEG_DP_n` | Integrationsart. Leer = Wert aus der GA-FL-Vorlage (Spalte C der Referenz); ein im Symbol gesetzter Wert hat Vorrang (Symbol > Referenz > leer) | „BACnet" / „virtuell" |
| `OC_KOMMENTAR_DP_n` | Kommentar | „Zulufttemperatur" |
| `OC_BAS_DP_n` | BAS-Adresse (wird automatisch generiert) | „BSP-ASP01-RLT-TKA-1000-TZU-01-MW_01" |

### 4.5 Datenpunkt-Pfeile

Die Datenpunkt-Pfeile (`01- Pfeile/`) werden an die Symbole angehängt und zeigen die Signalrichtung:

- **AI** (Analog Input): Messwerte lesen (Temperatur, Druck, ...)
- **AO** (Analog Output): Stellsignale ausgeben (Ventilstellung, Drehzahl, ...)
- **BI** (Binary Input): Meldungen lesen (Ein/Aus, Störung, ...)
- **BO** (Binary Output): Schaltbefehle ausgeben (Ein/Aus, ...)

---

## 5. Plankopf-Daten setzen

Die Funktion „Plankopf-Daten setzen" befüllt die Plankopf-Blöcke aller Zeichnungen mit den Stammdaten aus `plankopfdaten.csv`.

### 5.1 plankopfdaten.csv anpassen

Öffnen Sie `01- Referenzen/plankopfdaten.csv` in einem Texteditor und passen Sie die Werte an:

```csv
Attributname;Wert;Erläuterung
AN1;Ihre Firma GmbH;Auftragnehmer Zeile 1
AN2;Musterstraße 1;Auftragnehmer Zeile 2
...
AG1;Bauherr GmbH;Auftraggeber Zeile 1
...
PR1;Projektname;Projekt Zeile 1
PR2;Projektadresse;Projekt Zeile 2
...
ERSTELLER;Max Mustermann;Ersteller
ERSTELLDATUM;01.04.2026;Erstelldatum
```

**Wichtig:** `PR1` wird auf dem Projekt-Deckblatt als Projektname angezeigt.

### 5.2 Ausführung

Der Schritt ist Teil von **„Projekt erstellen"** (Abschnitt 11): Die Werte der CSV werden in die Plankopf-Attribute aller Zeichnungen geschrieben.

Zusätzlich werden automatisch aus dem Ordnerpfad die Attribute **ASP**, **GEWERK** und **ANLAGE** im Plankopf gesetzt. Maßgeblich ist die Lage im Pfad (`Los / ASP / Gewerk / Anlage`), nicht der Name: Die ASP-Kennung darf beliebig heißen und wird so übernommen, wie sie in der Erstellliste steht (seit 1.7.2; bis 1.7.1 musste der Ordnername „ASP" oder „ISP" enthalten).

---

## 6. BMK-Nummerierung

Die BMK-Nummerierung vergibt automatisch fortlaufende Betriebsmittelkennzeichen für alle Symbole mit dem Attribut `OC_AKS`.

### 6.1 Funktionsweise

- Jeder Block mit Attribut `OC_AKS` wird erkannt.
- Das Präfix (z.B. „BSK-") wird beibehalten, die Nummer wird automatisch angehängt: `BSK-01`, `BSK-02`, ...
- Die Sortierung erfolgt spaltenweise: links → rechts, innerhalb einer Spalte.
- Zähler werden zwischen Zeichnungen weitergegeben (Datei `bmk_counters.tmp` im Ordner der Zeichnungen). „Projekt erstellen" löscht die Datei wieder, sobald alle Zeichnungen nummeriert sind. „Projekt bereinigen" entfernt sie.

### 6.2 Steuerung pro Zeichnung

Das Plankopf-Attribut **BMK_NUMMERIERUNG** steuert den Modus. Ist es nicht vorhanden oder leer, wird ersatzweise **FREITEXT_05** ausgewertet (ältere Plankopf-Vorlagen). Ist `BMK_NUMMERIERUNG` befüllt, wird `FREITEXT_05` nicht mehr angesehen.

| Wert | Verhalten |
|---|---|
| `NEUSTARTEN` (oder beide Attribute leer) | Zähler beginnen bei 01 |
| `FORTSETZEN` | Zähler aus vorheriger Zeichnung übernehmen |

Die BricsCAD-Konsole zeigt je Zeichnung, aus welchem Attribut der Modus stammt, z.B. `Modus (BMK_NUMMERIERUNG): FORTSETZEN` oder `Modus (FREITEXT_05): NEUSTARTEN`.

### 6.3 Sperren einzelner Blöcke

Wenn ein Block ein Attribut `OC_AKS_LOCK` hat und dieses auf „JA", „TRUE" oder „X" gesetzt ist, wird der Block von der Nummerierung übersprungen.

### 6.4 Ausführung

Der Schritt ist Teil von **„Projekt erstellen"** (Abschnitt 11) und lässt sich dort über den Haken „BMK-Nummerierung einschliessen" abschalten. Die Zeichnungen werden in der Reihenfolge ihrer Namen verarbeitet.

---

## 7. BAS-Generierung

Die BAS-Generierung baut für jeden aktiven Datenpunkt eine Benutzeradresse (BAS-String) zusammen und schreibt sie in das Attribut `OC_BAS_DP_n`.

### 7.1 BAS.csv konfigurieren

Die Datei `01- Referenzen/BAS.csv` definiert den Aufbau des BAS-Strings. Jede Zeile ist ein Segment:

```csv
"Testprojekt"     ← Statischer Text (in Anführungszeichen)
-                  ← Trennzeichen (Bindestrich)
ASP                ← Attributwert aus dem Plankopf
-
GEWERK
-
ANLAGE
-
ORTSKENNZEICHEN
-
OC_AKS             ← Attributwert aus dem Block
-
OC_FCODE_DP        ← Endet mit _DP → wird pro Datenpunkt zu OC_FCODE_DP_1, _2, ...
```

**Ergebnis-Beispiel:** `BSP-ASP01-RLT-TKA-1000-ZUV-01-FR_01`

**Kommentare in der BAS.csv**: Ausgewertet wird nur die erste Spalte einer Zeile, also alles bis zum ersten Semikolon. Was dahinter steht, ist Kommentar. Zeilen, die mit `#` beginnen, werden komplett ignoriert. Die Datei kann damit auch aus Excel/LibreOffice (deutsche Locale, Trennzeichen `;`) gespeichert werden, ohne den Aufbau zu stören.

```csv
# BAS-Aufbau Testprojekt
"Testprojekt";Standortkennung (statisch)
-;Trenner
OC_AKS;AKS des Betriebsmittels
-
OC_FCODE_DP;Funktionscode je Datenpunkt
```

Einschränkung: Ein statischer Text darf selbst kein Semikolon enthalten, weil die Zeile dort abgeschnitten würde.

**Anführungszeichen:** Nur statischer Text steht in Anführungszeichen. Speichert Calc oder Excel die Datei mit „alle Textzellen in Anführungszeichen", stehen auch die Attributnamen darin (`"ASP"`), und das Plugin übernimmt sie wörtlich: Der BAS lautet dann `…-ORTSKENNZEICHEN-GEWERK-…`. In der Tabellenkalkulation ist das nicht zu sehen, nur in einem Texteditor. Seit 1.7.2 steht der gelesene Aufbau im Protokoll.

**Knopf „BAS konfigurieren"** (seit 1.7.3, neben „BAS-Generierung einschliessen"): zeigt die Segmente als Tabelle mit Art (Text / Attribut) und Wert. Zeilen lassen sich als Attribut, Text oder Trennzeichen anfügen, entfernen und verschieben; die Vorschau zeigt den Aufbau mit `<NAME>` als Platzhalter für Attribute. „Speichern" schreibt die BAS.csv im richtigen Format und sichert die bisherige Datei als `BAS.csv.bak`. Oben im Dialog steht, welche Datei gelesen wurde; fehlt sie, wird sie mit dem üblichen Aufbau angelegt und das gemeldet. Wer die Datei lieber von Hand pflegt, kann das weiterhin tun.

### 7.2 Segment-Typen

| Typ | Format | Beschreibung |
|---|---|---|
| Statischer Text | `"Text"` | Wird 1:1 übernommen |
| Trennzeichen | `-` | Bindestrich als Separator |
| Plankopf-Attribut | `ASP`, `GEWERK`, etc. | Wert wird aus dem Plankopf gelesen |
| Block-Attribut | `OC_AKS`, etc. | Wert wird aus dem Symbol-Block gelesen |
| Datenpunkt-Attribut | `OC_FCODE_DP` | Endet mit `_DP` → wird zu `_DP_1`, `_DP_2`, ... pro Datenpunkt |
| Kommentarzeile | `# Text` | Zeile wird komplett ignoriert |
| Kommentar in Spalte 2 | `OC_AKS;Text` | Nur die erste Spalte (bis zum ersten `;`) wird ausgewertet |

### 7.3 Ausführung

Der Schritt ist Teil von **„Projekt erstellen"** (Abschnitt 11) und lässt sich dort über den Haken „BAS-Generierung einschliessen" abschalten. Die BAS.csv wird eingelesen, alle aktiven Datenpunkte werden verarbeitet, das Ergebnis steht in `OC_BAS_DP_n`.

---

## 8. GA-Funktionslisten erstellen

Die GA-FL-Erstellung ist der Kern von openCirt. Sie läuft in drei Phasen:

### 8.1 Phase 1: Datenextraktion

- Aus allen Quell-DWGs werden die Datenpunkte gelesen (OC_BMK, OC_AKS, OC_REF_DP, OC_BAS_DP, Funktionswerte, ...).
- Die Daten werden als CSV-Dateien im temp-Verzeichnis gespeichert, je Projekt in einem eigenen Ordner (`OpenCirt_extract/<Projektname>_<Kennung>`). Dort liegen auch die Protokolle `extractdp_log.txt` und `fillgafl_log.txt`.
- Die Sensorliste greift nicht auf diese Dateien zurück: „Sensorliste erstellen" liest die Zeichnungen des geladenen Projekts selbst neu ein. BMK und BAS stehen in der Liste so, wie der letzte Lauf von „Projekt erstellen" sie in die Zeichnungen geschrieben hat.

### 8.2 Phase 2: GA-FL-Blätter erzeugen

- Für jede Quell-DWG wird eine GA-FL-DWG erzeugt (Kopie von `OC_VORLAGE_GA_FL.dwg`), bei mehr als 25 Datenpunkten mehrere Blätter mit Übertrag.
- Die Blätter werden mit den extrahierten Daten und den Werten der Referenztabelle gefüllt.

### 8.2a Phase 3: Summenblätter

- Läuft im Anschluss an die GA-FL-Blätter.
- Die Summenblätter (Gewerk-, ASP-, Los-, Projekt-Summe, Gewerke je Los) werden aus den **fertigen GA-FL-Blättern** aggregiert, nicht aus der Referenztabelle neu berechnet. Eine Handkorrektur in einem GA-FL-Blatt schlägt damit in den Summen durch.
- Die Referenztabelle wird für die Summen nicht benötigt.

### 8.3 GA_FL_VORLAGE.ods

Die Datei `01- Referenzen/GA_FL_VORLAGE.ods` ist die Referenztabelle. Sie definiert, welche Spalten und Werte für jeden Datenpunkttyp (Referenz-DP) in der GA-FL erscheinen. Das Attribut `OC_REF_DP_n` im Symbol verweist auf eine Zeile in dieser Tabelle.

Fehlt die Datei oder ist sie leer, bricht „Projekt erstellen" ab, bevor bestehende Blätter gelöscht werden.

**LibreOffice wird gebraucht.** Das Plugin lässt die Tabelle bei jedem Lauf von LibreOffice in `GA_FL_VORLAGE.csv` umwandeln (unter Windows ersatzweise von Excel). Die Tabelle darf dabei in LibreOffice geöffnet sein; gelesen wird der gespeicherte Stand. Scheitert die Umwandlung, erscheint eine Meldung mit dem Grund, und an den Zeichnungen ist nichts geändert.

Unter Linux findet das Plugin LibreOffice aus dem Paket der Distribution, aus den Paketen von libreoffice.org (`/opt/libreoffice…`), als Flatpak (`org.libreoffice.LibreOffice`) und als Snap. Liegt es woanders, nennt die Umgebungsvariable `OPENCIRT_LIBREOFFICE` das Programm, zum Beispiel in `~/.profile`:

```
export OPENCIRT_LIBREOFFICE="/pfad/zu/soffice"
```

Ein Flatpak läuft in einer Sandbox und braucht Zugriff auf den Projektordner. LibreOffice von Flathub hat ihn von Haus aus; wurde die Berechtigung für das Dateisystem eingeschränkt (etwa mit Flatseal), muss der Projektordner freigegeben sein.

### 8.4 Ausführung

Die drei Phasen sind Teil von **„Projekt erstellen"** (Abschnitt 11) und laufen dort ohne weiteres Zutun nacheinander.

Die erzeugten GA-FL-Dateien werden im jeweiligen Anlage-Ordner gespeichert:
```
01 TKA-1000/
├── 00 Hauptanlage.dwg            ← Quell-Zeichnung
└── 00 Hauptanlage_GA_FL_1.dwg    ← Erzeugte GA-FL
```

### 8.5 Textbreitenanpassung

Zum Schluss des Gesamtlaufs werden die Textbreiten aller GA-FL- und Summenblätter angepasst: Zu breite Texte werden gestaucht, damit alle Einträge in ihre Spalten passen. Gestreckt wird nie.

---

## 9. Deckblätter generieren

Die Funktion „Deckblätter erstellen" erzeugt automatisch Deckblätter für jede Hierarchie-Ebene.

### 9.1 Erzeugte Deckblätter

| Ebene | Dateiname | Inhalt |
|---|---|---|
| Projekt | `0000 Projekt_Deckblatt_B.dwg` | Projektname (aus PR1) |
| Los | `0000 Los 1_Deckblatt.dwg` | Los-Bezeichnung |
| ASP | `0000 ASP01_Deckblatt.dwg` | ASP-Kennung |
| Gewerk | `0000 RLT_Deckblatt.dwg` | Gewerk-Bezeichnung |
| Anlage | `0000 TKA-1000_Deckblatt.dwg` | Anlagen-Kennung |

Die Deckblätter werden aus der Vorlage `OC_VORLAGE_DIN_A2_V<n>.dwg` mit der höchsten Versionsnummer erzeugt (derzeit `OC_VORLAGE_DIN_A2_V14.dwg`). Der Layer „GA-Deckblatt" wird aufgetaut, Trennlinien-Layer werden eingefroren.

### 9.2 Ausführung

Der Schritt ist Teil von **„Projekt erstellen"** (Abschnitt 11).

---

## 10. Inhaltsverzeichnis und PDF-Publish

### 10.1 Inhaltsverzeichnis

Das Inhaltsverzeichnis wird automatisch aus allen Zeichnungen im Projekt generiert. Jeder Eintrag enthält:

- Los, ASP, Gewerk, Anlage
- Zeichnungsnummer
- Seitenzahl

Grundlage ist die Blattvorlage `OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS_V1.dwg`, die den Eintragsblock mit allen Zeilen bereits enthält – 22 Einträge pro Seite, bei Bedarf werden automatisch weitere Seiten erzeugt.

### 10.2 PDF-Publish

Nach dem Inhaltsverzeichnis wird automatisch ein Multi-Sheet-PDF im Ordner `06- Plot/` erzeugt. Das PDF enthält alle Zeichnungen in der korrekten Reihenfolge:

1. Projekt-Deckblatt
2. Inhaltsverzeichnis
3. ASP-Deckblätter → Gewerk-Deckblätter → Anlage-Deckblätter → Schema-Zeichnungen → GA-FLs → Summenblätter

### 10.3 Ausführung

Auf **„PDF publizieren"** klicken. Inhaltsverzeichnis und PDF werden nacheinander erzeugt. Das Inhaltsverzeichnis entsteht ohne Bildaufbau; für das PDF startet eine zweite BricsCAD-Instanz, die Dateiname und Ordner abfragt und die Blätter plottet.

---

## 11. Gesamtprojekt erstellen

Die Funktion **„Projekt erstellen"** führt alle Schritte in der korrekten Reihenfolge automatisch aus:

1. Projektstruktur bereinigen (alte GA-FLs, Summenblätter, Deckblätter, Inhaltsverzeichnisse löschen)
2. Plankopf-Daten setzen (aus CSV, einschließlich der Projektblätter auf der obersten Ebene)
3. Deckblätter erstellen
4. ASP, Gewerk und Anlage aus dem Ordnerpfad in den Plankopf schreiben
5. BMK-Nummerierung (optional, per Checkbox)
6. BAS-Generierung (optional, per Checkbox)
7. GA-FL Phase 1: Datenextraktion
8. GA-FL Phase 2: Erzeugung und Befüllung
9. GA-FL Phase 3: Summenblätter aus den fertigen GA-FL-Blättern
10. Textbreitenanpassung

Das Inhaltsverzeichnis entsteht zusammen mit dem PDF über **„PDF publizieren"** (Abschnitt 10).

**Seit Version 1.7 läuft der Gesamtlauf ohne Editor.** Das Plugin bearbeitet die Zeichnungsdateien direkt, statt sie nacheinander in BricsCAD zu öffnen. Der Bildschirm flackert nicht, der Lauf dauert bei einem Projekt mit rund 850 Blättern wenige Minuten, und er läuft unter Windows und Linux gleich. Die LISP-Skripte in `02- Skripte/` werden dafür nicht mehr gebraucht; der Ordner muss weiterhin vorhanden sein.

Zeichnungen des Projekts dürfen während des Laufs nicht in BricsCAD geöffnet sein. Ist eine geöffnet, startet der Lauf nicht und nennt sie.

### Optionen des Gesamtlaufs

| Option | Beschreibung |
|---|---|
| BMK-Nummerierung einschliessen | BMK-Vergabe vor GA-FL-Generierung |
| BAS-Generierung einschliessen | BAS-Adressen vor GA-FL-Generierung erzeugen |

### Ausführung

Auf **„Projekt erstellen"** klicken und die Warnung bestätigen. Der Prozess läuft vollautomatisch; solange er läuft, nimmt das Plugin-Fenster keine Eingaben an. Am Ende nennt eine Meldung Stückzahlen, Fehler und Dauer.

---

## 12. Tipps und Fehlerbehebung

### Allgemeine Hinweise

- **Backups:** Vor „Projekt erstellen" den Zeichnungsordner sichern (z.B. als ZIP). Das Plugin legt Sicherungskopien an (nächster Punkt), eine eigene Sicherung ersetzt das nicht.
- **Sicherungskopien bei „Projekt erstellen":** Jede Zeichnung, die es vor dem Lauf schon gab, bekommt eine Sicherungskopie `*.bak` mit ihrem Stand vor dem Lauf (Quellzeichnungen und Projektblätter; nicht die Blätter, die der Lauf selbst erzeugt). Zum Zurückholen die `.bak` in `.dwg` umbenennen. Ein weiterer Lauf ersetzt die Sicherung durch den dann aktuellen Stand. „Projekt bereinigen" löscht die Sicherungen, „Projekt aufbauen" ebenfalls.
- **Nicht bedienen:** Während des PDF-Publish die zweite BricsCAD-Instanz nicht bedienen – sie arbeitet ein Skript ab. Das openCirt-Fenster nimmt während eines Laufs keine Eingaben an.
- **Geöffnete Zeichnungen:** Für „Projekt erstellen" und „Projekt aufbauen" darf keine Zeichnung des Projekts in BricsCAD geöffnet sein; der Lauf startet sonst nicht und nennt sie.
- **Neustart:** Vor dem Gesamtlauf ist kein Neustart von BricsCAD nötig, er öffnet keine Dokumente.

### Häufige Fehler

| Problem | Ursache | Lösung |
|---|---|---|
| „Projektstruktur unvollstaendig" | Ordner fehlen | Die Ordner 01- Referenzen, 04- Vorlagen und 05- Projekt Zeichnungen müssen existieren |
| „Projekt erstellen" startet nicht: „Zeichnungen … sind in BricsCAD geöffnet" | Eine Zeichnung des Projekts ist im Editor geöffnet | Zeichnung schließen, Lauf neu starten |
| „BAS.csv nicht gefunden" | BAS.csv fehlt oder falsch benannt | Datei muss exakt `BAS.csv` heißen und in `01- Referenzen/` liegen |
| „Referenz fehlt" / „GA-FL-Referenz nicht gefunden" | ODS-Datei fehlt oder leer | `GA_FL_VORLAGE.ods` in `01- Referenzen/` ablegen; der Lauf startet erst dann |
| „Referenz umwandeln": LibreOffice wurde nicht gefunden | LibreOffice fehlt oder liegt an einem Ort, an dem das Plugin nicht sucht | LibreOffice installieren oder das Programm in `OPENCIRT_LIBREOFFICE` nennen (siehe 8.3). An den Zeichnungen ist nichts geändert |
| „Referenz umwandeln": LibreOffice hat keine CSV-Datei geschrieben | LibreOffice kann die Tabelle nicht lesen oder nicht in den Ordner schreiben; bei einem Flatpak fehlt meist der Zugriff auf den Projektordner | Die Meldung zeigt die Ausgabe von LibreOffice. Zugriff auf den Projektordner freigeben, Lauf neu starten |
| Keine Datenpunkte erkannt | OC_FL_AKTIV_n nicht gesetzt | Mindestens ein Datenpunkt muss aktiv sein (OC_FL_AKTIV_n = „ja") |
| BMK-Nummern beginnen nicht bei 01 | BMK_NUMMERIERUNG (bzw. FREITEXT_05) = FORTSETZEN | Auf „NEUSTARTEN" setzen. „Projekt erstellen" löscht die Zählerdatei `bmk_counters.tmp` selbst |
| Deckblatt zeigt „Projekt" statt Projektname | PR1 leer | In plankopfdaten.csv den Wert für PR1 eintragen |
| „Projekt aufbauen" bricht ab: „Zeichnungen sind offen" | Eine Zeichnung aus `05- Projekt Zeichnungen` ist geöffnet, oder dort liegt eine Sperrdatei `*.dwl`/`*.dwl2` aus einem Absturz | Zeichnungen schließen, verwaiste Sperrdateien löschen („Projekt bereinigen"), Aufbau neu starten |
| „Projekt aufbauen" bricht ab: „Stempel nicht gefunden" | Die Kennung `[n]` aus `OC_ANLAGE` kommt in keinem Stempel der Vorlage vor | Erstellliste und Vorlage vergleichen; die Meldung nennt Zeile, Zeichnung und Kennung |

### Bekannte Probleme

Die bis Version 1.7 hier beschriebenen Probleme in BricsCAD (GDI-Objekt-Leck unter Windows, LISP-Umgebung unter Linux) betrafen nur den LISP-Tab, der seit 2.0 zum Plugin batchTool gehört (dort in `KNOWN_ISSUES.md`). Für openCirt sind derzeit keine bekannt. Unterschiede zwischen BricsCAD für Windows und für Linux, die das Plugin ausgleicht (Lage ausgerichteter Texte) oder die aus BricsCAD stammen (OLE-Objekte fehlen im Linux-PDF), beschreibt `docs/DEVELOPMENT.md`.

### Attribut-Reihenfolge korrigieren

Wenn die Reihenfolge der Attribute im Eigenschaftenfenster durcheinander ist (z.B. nach manuellem Bearbeiten eines Blocks):

1. Block im Block-Editor öffnen (BEDIT).
2. `ATTDISP` auf `EIN` setzen, damit alle Attribute sichtbar sind.
3. ATTDEFs in der gewünschten Reihenfolge neu anlegen (die Erstellungsreihenfolge bestimmt die Anzeigereihenfolge).
4. Block-Editor schließen und speichern.

---

## Lizenz

openCirt steht unter der Business Source License 1.1 (BSL 1.1). Nutzung für interne Zwecke und kommerzielle Projekte ist erlaubt. Verkauf als eigenständiges Produkt und SaaS-Angebote sind untersagt. Ab 2030-03-02 wird die Software unter AGPLv3 verfügbar.

Siehe [LICENSE](../LICENSE) und [ADDITIONAL_TERMS](../ADDITIONAL_TERMS) im Repository.

---

*openCirt – Open Source GA-Planungsautomatisierung für BricsCAD*  
*© 2026 Oliver Hagmann*
