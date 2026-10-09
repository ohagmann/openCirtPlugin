/**
 * @file ProjectBuilder.h
 * @brief Projektaufbau aus der Erstellliste (CSV)
 *
 * Erzeugt die Quellzeichnungen eines Projekts aus der Erstellliste: Vorlagen
 * kopieren, in die Ordnerhierarchie Los / ASP / Gewerk / Anlage einsortieren
 * und die Blockattribute aus den Spalten der Liste setzen.
 *
 * Die Klasse ersetzt das LISP-Skript OC_PROJECT_BUILD (Stand v3.5) und
 * verhaelt sich wie dieses: gleiche Zeilenarten, gleiche Warnungen, gleiches
 * Log. Der Unterschied liegt im Weg: die Zeichnungen werden nicht im Editor
 * geoeffnet, sondern als Side-Database gelesen und geschrieben. Das laeuft
 * unter Windows und Linux gleich und braucht weder LISP noch COM.
 *
 * Ablauf:
 *   prepare()  liest die Liste, baut den Plan, prueft auf offene Zeichnungen
 *              und schreibt die Loesch-Vorschau ins Log
 *   runDry()   schreibt den Plan ins Log, aendert nichts
 *   runReal()  loescht '05- Projekt Zeichnungen', baut neu auf, raeumt auf
 */

#ifndef PROJECTBUILDER_H
#define PROJECTBUILDER_H

#include <QObject>
#include <QString>
#include <QStringList>
#include <QVector>
#include <QPair>
#include <QMap>
#include <QFile>

namespace BatchProcessing {

class ProjectBuilder : public QObject {
    Q_OBJECT

public:
    /// Attributliste in der Reihenfolge der Spalten: (TAG in Grossschreibung, Wert)
    using AttrList = QVector<QPair<QString, QString>>;

    /// Ein Blatt, das erzeugt wird
    struct PlanEntry {
        QString src;                 ///< Vorlage (voller Pfad)
        QString dstDir;              ///< Zielordner
        QString dstBase;             ///< Dateiname ohne Praefix, ohne .dwg
        QString dstPath;             ///< Zieldatei (nach assignIndices)
        AttrList attrs;              ///< Anlagen-Header-Attribute
        QVector<AttrList> stempel;   ///< Stempel-Eintraege, je mit "_CSV-LINE_"
        int msgCount = 0;            ///< befuellte Meldungs-Slots
        bool meldungsblock = false;  ///< OC_AKS der Header-Zeile gesetzt
        bool topLevel = false;       ///< Projektzeile (oberste Ebene)
        int csvLine = 0;             ///< Zeile der Liste
    };

    /// Ergebnis von prepare()
    struct Preview {
        int deleteCount = 0;         ///< Dateien, die geloescht wuerden
        int protectedCount = 0;      ///< geschuetzte *Deckblatt_A.dwg
        int planCount = 0;           ///< zu erzeugende Zeichnungen
        int warnings = 0;            ///< Warnungen bis hierhin
        bool protectDeckblatt = true;
        QStringList openDocs;        ///< in dieser Sitzung offene Zeichnungen
        QStringList lockFiles;       ///< *.dwl / *.dwl2 im Zeichnungsordner
    };

    /// Ergebnis von runReal()
    struct Result {
        int created = 0;             ///< erzeugt und mit Attributen versehen
        int copiedOnly = 0;          ///< nur kopiert
        int errors = 0;              ///< Fehler beim Aufbau
        int swept = 0;               ///< vom Sweep entfernt
        int warnings = 0;            ///< Fehler/Warnungen insgesamt
        bool aborted = false;        ///< Abbruch wegen fehlendem Stempel
        QString abortMessage;        ///< Text fuer den Dialog
    };

    explicit ProjectBuilder(QObject* parent = nullptr);
    ~ProjectBuilder() override;

    /// Liste lesen, Plan bauen, Pre-Flight und Loesch-Vorschau.
    /// false: nicht fortsetzen, lastError() nennt den Grund.
    bool prepare(const QString& csvPath, const QString& projectRoot, bool dryRun);

    /// true, wenn prepare() im echten Lauf an offenen Zeichnungen oder
    /// Sperrdateien gescheitert ist (Preview::openDocs / lockFiles)
    bool blockedByOpenDrawings() const { return m_blocked; }

    QString lastError() const { return m_lastError; }
    QString logPath() const { return m_logPath; }
    const Preview& preview() const { return m_preview; }
    const QVector<PlanEntry>& plan() const { return m_plan; }

    /// Vorschau: Plan ins Log, keine Aenderung am Projekt
    void runDry();

    /// Echter Lauf: Saeuberung, Aufbau, Sweep, Zusammenfassung
    Result runReal();

    /// Anwender hat die Sicherheitsabfrage verneint
    void cancelByUser();

signals:
    /// Fortschritt des Aufbaus, current zaehlt ab 1
    void progress(int current, int total, const QString& fileName);

private:
    // --- Log ---
    bool openLog(const QString& path);
    void closeLog();
    void log(const QString& line);
    void logHeader(const QString& csvPath, const QString& vorlagenPath);
    static QString native(const QString& path);

    // --- Liste ---
    QStringList readCsv(const QString& csvPath, QString& encodingName);
    static QString trimField(const QString& s);
    static QString cleanHeader(const QString& s);
    static QString rowValue(const QStringList& fields, int col);
    static int headerIndex(const QStringList& header, const QString& key);
    static QString stempelPrefix(const QString& value);
    static QString padNumber(int n, int width);

    // --- Plan ---
    using ColumnList = QVector<QPair<int, QString>>;   ///< (Spaltenindex, TAG)
    bool buildVorlagenMap(const QString& vorlagenPath);
    bool buildPlan(const QStringList& lines, const QStringList& header,
                   const ColumnList& attrCols);
    void assignIndices();
    static bool rowHasData(const QStringList& fields, const ColumnList& cols);
    static AttrList rowHashAttrs(const QStringList& fields, const ColumnList& hashCols, int n);
    static int nextIndex(QMap<QString, QStringList>& map, const QString& parentKey,
                         const QString& childName);

    // --- Dateien ---
    static QStringList listFiles(const QString& path, const QStringList& patterns);
    static QStringList listDirs(const QString& path);
    static void sortLikeWindows(QStringList& names);
    static QStringList findFilesRecursive(const QString& basePath, const QStringList& patterns);
    bool isProtectedDeckblatt(const QString& fileName) const;
    bool preflight();
    void previewDeletions();
    void cleanProjectFolder();
    int sweep();

    // --- Zeichnungen ---
    bool executeEntry(const PlanEntry& entry, Result& result);
    void executeDry();
    void abortStempel(const QString& csvLine, const QString& dwgPath,
                      const AttrList& stempelAttrs, Result& result);

    QString m_csvPath;
    QString m_projectRoot;
    QString m_vorlagenPath;
    QString m_zeichnungsRoot;
    bool m_dryRun = false;
    bool m_blocked = false;
    int m_errCount = 0;

    QString m_lastError;
    QString m_logPath;
    QFile m_logFile;

    QMap<QString, QString> m_vorlagen;   ///< Name in Grossschreibung -> Pfad
    QVector<PlanEntry> m_plan;
    Preview m_preview;
};

} // namespace BatchProcessing

#endif // PROJECTBUILDER_H
