/**
 * @file OpenCirtEngine.h
 * @brief openCirt-Operationen auf Zeichnungen ohne Editor (Side-Database)
 *
 * Bis Version 1.6 liefen die Schritte des Gesamtlaufs als LISP im Editor:
 * das Plugin schrieb ein Skript, BricsCAD oeffnete jede Zeichnung, fuehrte
 * das LISP aus, speicherte und schloss sie wieder. Hier stehen dieselben
 * Schritte als C++. Eine Zeichnung wird als Side-Database gelesen, geaendert
 * und gespeichert - ohne Dokument, ohne Bildaufbau, unter Windows und Linux
 * gleich.
 *
 * Die Operationen sind Uebertragungen der Skripte aus '02- Skripte' und der
 * LISP-Schnipsel, die OpenCirtTab frueher erzeugt hat:
 *
 *   BmkNummerierung.lsp v2.3          -> OcDrawing::bmkNummerierung
 *   GenBas.lsp v1.3                   -> OcDrawing::genBas
 *   ExtractDP.lsp v1.7                -> OcDrawing::extractDp
 *   FillGaFl.lsp v1.5                 -> OcDrawing::fillGaFl
 *   TextBreitenAnpassenBloecke.lsp v3 -> OcDrawing::textBreitenAnpassen
 *
 * Wo das Ergebnis von einer Reihenfolge abhaengt, gilt die der Skripte:
 * Bloecke in der Reihenfolge von (ssget "X"), Attribute in der Reihenfolge
 * der ATTRIB-Kette, Sortierung wie (vl-sort). Das ist Absicht - nur so
 * entsteht dasselbe Ergebnis wie bisher.
 */

#ifndef OPENCIRTENGINE_H
#define OPENCIRTENGINE_H

#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QFile>
#include <memory>

namespace BatchProcessing {

/// Geordnete Liste (Schluessel, Wert); der erste Treffer gilt
using OcPairs = QVector<QPair<QString, QString>>;

// ============================================================================
// Protokolldatei
// ============================================================================

/// Protokoll im Stil der Skripte (oc-log): jede Meldung beginnt mit einem
/// Zeilenumbruch, die Meldungen stehen ohne weiteres Trennzeichen
/// hintereinander. Geschrieben wird UTF-8.
class OcLogFile {
public:
    OcLogFile() = default;
    ~OcLogFile();

    /// Datei oeffnen; truncate = false haengt an
    bool open(const QString& path, bool truncate);
    void close();
    void write(const QString& message);

private:
    QFile m_file;
};

// ============================================================================
// Eingaben und Ergebnisse der Operationen
// ============================================================================

/// Segment der BAS.csv
struct OcBasSegment {
    enum Type { Static, Attr, AttrDp };
    Type type = Static;
    QString value;   ///< Text bzw. Attributname
};

/// Segment der Blattname-Konfiguration (Seitennamen.csv, Zeile 1)
struct OcSeitennamenSegment {
    enum Type { Text, Attr };
    Type type = Text;
    QString value;   ///< woertlicher Text oder Attribut-Tag (in Grossbuchstaben)
};

struct OcBmkResult {
    int blocks = 0;          ///< Bloecke mit OC_AKS
    int numbered = 0;        ///< neu nummerierte Eintraege
    int locked = 0;          ///< wegen OC_AKS_LOCK uebersprungen
    QString mode;            ///< NEUSTARTEN / FORTSETZEN / ...
    QString modeSource;      ///< BMK_NUMMERIERUNG / FREITEXT_05 / Default
};

struct OcExtractResult {
    bool ok = false;
    int dpCount = 0;         ///< aktive Datenpunkte
    QStringList lines;       ///< Zeilen der CSV, wie sie in der Datei stehen
    QString error;
};

/// Eingaben fuer eine Blattfuellung (FillGaFl)
struct OcFillJob {
    QVector<QStringList> rows;      ///< CSV-Zeilen ohne Kommentarzeilen, Zeile 0 = Kopf
    OcPairs plankopf;               ///< Plankopf-Daten in der Reihenfolge der Liste im Skript
    bool useReference = false;      ///< Referenz (GA_FL_VORLAGE.csv) anwenden
    int sheetNum = 1;               ///< Blattnummer, 1 = Erstblatt
    int startRow = 0;               ///< erste Datenzeile (0-basiert, nach dem Kopf)
    int dpCount = 24;               ///< Datenpunkte auf diesem Blatt
};

/// Zustand, der von Blatt zu Blatt weitergereicht wird
struct OcFillState {
    bool hasCarry = false;          ///< Summen des Vorgaengerblatts vorhanden
    QVector<int> carry;             ///< Summen des Vorgaengerblatts (Uebertrag)
    QStringList missingRefs;        ///< fehlende Referenzen, ohne Dubletten
};

struct OcFillResult {
    bool ok = false;
    int filled = 0;                 ///< befuellte Datenpunkte
    int gaFlBlocks = 0;             ///< gefundene GA-FL-Bloecke
    QString error;
};

struct OcTextWidthResult {
    int blockCount = 0;             ///< angepasste Blockattribute
    int modelCount = 0;             ///< angepasste Texte im Modellbereich
    int skipCount = 0;              ///< Attribute ohne Zellzuordnung
};

// ----------------------------------------------------------------------------
// Anlagenleistungsliste
// ----------------------------------------------------------------------------

/// Ein Leistungsstempel (Stempel mit OC_LISTE = LL) einer Quellzeichnung
struct OcLlStamp {
    /// Verbindung zum Geraet ueber die Linie auf GA-Leistungsstempel-Verbindung
    enum Link {
        Linked,          ///< Geraet gefunden, BAS gelesen
        NoLine,          ///< keine Linie am Stempel
        NoDevice,        ///< Linie endet an keinem Block mit OC_BAS_DP_n
        NoBasValue       ///< Geraet gefunden, aber sein BAS ist leer
    };

    int nr = 0;                     ///< [n] aus OC_ANLAGE, 0 ohne Nummer
    QString anlage;                 ///< OC_ANLAGE, z. B. "[2] Zuluftventilator"
    QVector<QPair<QString, QString>> zeilen;   ///< OC_TEXT_Zn_S1 / _S2, n = 1..16
    QString bemerkung;              ///< OC_BEMERKUNG
    QString absicherung;            ///< OC_ABSICHERUNG
    Link link = NoLine;
    QString bas;                    ///< BAS des Geraets bis einschliesslich OC_AKS
    QString deviceAks;              ///< OC_AKS des Geraets
    bool basCut = true;             ///< false: OC_AKS stand nicht im BAS, BAS ungekuerzt
    double x = 0, y = 0;            ///< Einfuegepunkt
};

/// Was eine Quellzeichnung zur Anlagenleistungsliste beitraegt
struct OcLlScan {
    QVector<OcLlStamp> stamps;      ///< nach [n] sortiert
    QString zeichnungsnummer;       ///< Plankopf ZEICHNUNGSNUMMER
    QString ortskennzeichen;        ///< Plankopf ORTSKENNZEICHEN
    bool schrankBlatt = false;      ///< Blatt traegt das Bauteil ASP-* (Schrankblatt)
};

/// Lage der Liste auf dem Listenblatt, aus der Vorlage gelesen
struct OcLlLayout {
    bool ok = false;
    QString error;
    double x0 = 0, y0 = 0;          ///< linke obere Ecke des ASP-Bands
    double hAsp = 5, hKopf = 10, hZeile = 5, hSumme = 10;
    double yMin = 0;                ///< tiefste erlaubte Unterkante (ueber dem Plankopf)

    /// Zeilen unter Band und Kopf bis yMin
    int rowsPerPage() const;
};

/// Inhalt eines Listenblatts
struct OcLlPage {
    OcPairs aspBand;                ///< OC_LL_ASP, OC_LL_ASP_BEREICH, OC_LL_ASP_STANDORT
    QVector<OcPairs> rows;          ///< leere Liste = Leerzeile
    bool withSumme = false;
    OcPairs summe;                  ///< OC_LL_SUM_*, OC_LL_EINSPEISUNG
};

struct OcLlBuildResult {
    bool ok = false;
    int rows = 0;                   ///< eingefuegte Zeilen (mit Leerzeilen)
    QStringList frozenLayers;       ///< ausgeblendete Trennlinien
    QString error;
};

// ============================================================================
// Hilfsfunktionen ohne Zeichnung
// ============================================================================

namespace OcEngine {

/// Grossschreibung wie (strcase): Zeichen fuer Zeichen, ohne Sonderfaelle
QString lispUpper(const QString& s);

/// (vl-string-trim chars s)
QString lispTrim(const QString& s, const QString& chars);

/// (atoi s): fuehrende Ganzzahl, sonst 0
int lispAtoi(const QString& s);

/// (wcmatch text pattern) fuer die Muster, die openCirt benutzt: * ? # @ .
bool wildMatch(const QString& pattern, const QString& text);

/// Wert "aktiv"? ja/true/1/x/high/aktiv/wahr
bool isActiveValue(const QString& value);

/// Textdatei so lesen wie (read-line): UTF-8 (mit oder ohne BOM) oder
/// Windows-1252, Zeilenende LF oder CRLF. ok = false, wenn die Datei fehlt.
QStringList readLines(const QString& path, bool* ok = nullptr);

/// Text so kodieren wie (write-line): Windows-1252, alles andere als \U+XXXX
QByteArray encodeLikeLisp(const QString& text);

/// Zeilen so schreiben wie (write-line)
bool writeLinesLikeLisp(const QString& path, const QStringList& lines);

/// Semikolon-getrennte Zeile zerlegen (oc-fl-parse-csv-line)
QStringList splitSemicolon(const QString& line);

/// Zeilen einer extrahierten CSV in Datenzeilen wandeln (oc-fl-read-csv):
/// getrimmt, ohne Leer- und Kommentarzeilen; Zeile 0 ist der Kopf
QVector<QStringList> rowsFromLines(const QStringList& lines);

/// Plankopf aus der Zeile #PLANKOPF;... (oc-fl-read-plankopf-from-csv)
OcPairs plankopfFromLines(const QStringList& lines);

/// GA_FL_VORLAGE.csv lesen (oc-fl-read-reference-csv), kommagetrennt.
/// ok = false, wenn die Datei fehlt oder nicht lesbar ist.
QVector<QStringList> readReferenceCsv(const QString& path, bool* ok = nullptr);

/// BAS.csv lesen (oc-parse-bas-csv). ok = false, wenn die Datei fehlt.
QVector<OcBasSegment> parseBasCsv(const QString& path, bool* ok = nullptr);

/// BAS-Aufbau als Text fuers Protokoll: Text in Anfuehrungszeichen, Attribute
/// ohne, Attribute je Datenpunkt mit "_n"; Segmente durch " + " getrennt
QString basLayoutText(const QVector<OcBasSegment>& segments);

/// Schreibt die BAS.csv so, wie parseBasCsv sie liest: Text als """Text"""
/// (uebersteht Oeffnen und Speichern in Calc), das Trennzeichen "-" und
/// Attributnamen ohne Anfuehrungszeichen. Eine vorhandene Datei wird vorher
/// als BAS.csv.bak gesichert.
bool writeBasCsv(const QString& path, const QVector<OcBasSegment>& segments,
                 QString* error = nullptr);

/// Liest die Seitennamen-Konfiguration (01- Referenzen/Seitennamen.csv):
/// nur Zeile 1 ist die Konfiguration, was darunter steht, ist die freie
/// Anleitung des Anwenders. Zeile 1 ist eine Folge von Zellen, getrennt
/// durch Semikolon: eine Zelle in Anfuehrungszeichen ist woertlicher Text
/// (Leerzeichen bleiben erhalten), eine ohne ist ein Attribut-Name (gross
/// gespeichert). Es wird nirgends getrimmt. ok = false, wenn die Datei
/// nicht lesbar ist; leerer Vektor bei fehlender oder leerer Datei.
QVector<OcSeitennamenSegment> parseSeitennamenCsv(const QString& path, bool* ok = nullptr);

/// Setzt die Segmente als Konfigurationszeile zusammen, genau so wie
/// parseSeitennamenCsv sie liesst: Text-Zelle """Text""" (drei Ebenen),
/// Attribut-Zelle nackig; die Zellen trennt ein Semikolon.
QString seitennamenZeile(const QVector<OcSeitennamenSegment>& segments);

/// Schreibt die Seitennamen-Konfiguration (UTF-8): ersetzt Zeile 1 durch
/// seitennamenZeile; was darunter stand (die freie Anleitung des Anwenders),
/// bleibt unveraendert erhalten. Ohne .bak.
bool writeSeitennamenCsv(const QString& path, const QVector<OcSeitennamenSegment>& segments,
                         QString* error = nullptr);

/// Titel aus den Segmenten: Text woertlich, Attribut mit seinem Wert aus der
/// Map; fehlt der Wert oder ist leer, faellt das Segment weg. Semikolon und
/// eckige Klammern im Wert (Problemzeichen des INI-artigen DSD) werden zu
/// Leerzeichen, damit der Sektionsname erhalten bleibt.
QString seitennamenTitel(const QVector<OcSeitennamenSegment>& segments,
                         const QMap<QString, QString>& attribute);

/// Neuen Lauf beginnen: jede Zeichnung bekommt beim ersten Speichern wieder
/// eine Sicherungskopie (*.bak). Weitere Speichervorgaenge desselben Laufs
/// lassen sie stehen, sie zeigt also den Stand vor dem Lauf.
void beginBackupRun();

} // namespace OcEngine

// ============================================================================
// Zeichnung als Side-Database
// ============================================================================

class OcDrawing {
public:
    OcDrawing();
    ~OcDrawing();

    OcDrawing(const OcDrawing&) = delete;
    OcDrawing& operator=(const OcDrawing&) = delete;

    /// Zeichnung lesen. false: lastError() nennt den Grund.
    /// readOnly: nur zum Auswerten - die Datei wird so gelesen, dass auch eine
    /// im Editor geoeffnete Zeichnung lesbar ist, und save() ist gesperrt.
    bool open(const QString& path, bool readOnly = false);

    /// Unter demselben Namen speichern. Mit backup bleibt der bisherige Stand
    /// als <Name>.bak neben der Zeichnung liegen - je Lauf einmal, siehe
    /// OcEngine::beginBackupRun(). Misslingt die Sicherung, wird nicht
    /// gespeichert.
    bool save(bool backup = true);

    void close();
    bool isOpen() const;
    QString path() const;
    QString lastError() const;

    /// Anzahl der INSERTs (alle Layouts)
    int insertCount() const;

    // ------------------------------------------------------------------
    // Attribute
    // ------------------------------------------------------------------

    /// Werte in alle Bloecke schreiben, die ein Attribut mit dem Tag fuehren.
    /// Schluessel in Grossschreibung; je Attribut gilt der erste passende
    /// Eintrag. blockPattern schraenkt auf Blocknamen ein (Platzhalter *),
    /// effectiveName vergleicht dabei den Namen, den das Eigenschaftenfenster
    /// zeigt. Rueckgabe: Anzahl der gesetzten Attribute.
    int setAttributes(const OcPairs& upperTagValues,
                      const QString& blockPattern = QString(),
                      bool effectiveName = false);

    /// Wert des ersten Attributs mit diesem Tag in einem Block, dessen Name
    /// auf das Muster passt. found meldet, ob es das Attribut gibt.
    QString attributeValue(const QString& upperTag, const QString& blockPattern,
                           bool* found = nullptr) const;

    // ------------------------------------------------------------------
    // Deckblatt und Layer
    // ------------------------------------------------------------------

    /// Layer auftauen und einschalten. Fehlt der Layer, geschieht nichts.
    void thawLayer(const QString& name);

    /// Layer einfrieren, in der gegebenen Reihenfolge. Wie im Editor laesst
    /// sich der aktuelle Layer nicht einfrieren; die Liste bricht dort ab.
    void freezeLayers(const QStringList& names);

    /// TEXT und MTEXT, deren Inhalt (ohne Beachtung der Schreibweise) dem
    /// Suchtext entspricht, ersetzen. Rueckgabe: Anzahl.
    int replaceText(const QString& upperSearch, const QString& replacement);

    // ------------------------------------------------------------------
    // Uebertragene Skripte
    // ------------------------------------------------------------------

    OcBmkResult bmkNummerierung(QStringList* messages = nullptr);

    /// Rueckgabe: Anzahl der geschriebenen BAS-Strings
    int genBas(const QVector<OcBasSegment>& segments, QStringList* messages = nullptr);

    /// Datenpunkte extrahieren. Schreibt <extractDir>/<Zeichnungsname>.csv.
    OcExtractResult extractDp(const QString& extractDir, OcLogFile* log = nullptr);

    OcFillResult fillGaFl(const OcFillJob& job, const QVector<QStringList>& reference,
                          OcFillState& state, OcLogFile* log = nullptr);

    OcTextWidthResult textBreitenAnpassen();

    // ------------------------------------------------------------------
    // Anlagenleistungsliste
    // ------------------------------------------------------------------

    /// Leistungsstempel (OC_LISTE enthaelt LL) mit ihren Zeilen lesen und je
    /// Stempel das Geraet am anderen Ende der Linie auf dem Layer
    /// GA-Leistungsstempel-Verbindung suchen. Ein Linienende gehoert zu einem
    /// Block, wenn es hoechstens tol von dessen sichtbarer Geometrie entfernt
    /// liegt (ausgeblendete Teile parametrischer Bloecke zaehlen nicht).
    OcLlScan scanLeistungsstempel(double tol = 0.5) const;

    /// Lage der Liste in der Listenvorlage: ASP-Band der Vorlage als Anker,
    /// Hoehen aus den Blockdefinitionen, Unterkante ueber dem Plankopf.
    OcLlLayout leistungslisteLayout() const;

    /// Musterzeilen der Vorlage entfernen und die Liste neu einsetzen:
    /// ASP-Band, Kopf, Zeilen, bei Bedarf Summe. Trennlinien, die die Liste
    /// kreuzen, werden eingefroren.
    OcLlBuildResult buildLeistungsliste(const OcLlLayout& layout, const OcLlPage& page);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace BatchProcessing

#endif // OPENCIRTENGINE_H
