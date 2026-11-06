/**
 * @file OcAuftrag.h
 * @brief Auftraege von aussen und Laufstatus als Datei
 *
 * Ein Auftrag ist eine kleine JSON-Datei, die ein anderes Programm schreibt
 * (z. B. ein MCP-Server, der BricsCAD fernsteuert) und per LISP an openCirt
 * uebergibt:
 *
 *     (opencirt-auftrag "/tmp/oc_mcp/opencirt_auftrag.json")
 *
 * openCirt fuehrt ihn aus wie einen Knopfdruck im Fenster. Rueckfragen, die
 * sonst ein Dialog stellt, beantwortet der Auftrag; fehlt eine Antwort, gilt
 * "abbrechen". Es gibt keine Vorgaben fuer die Optionen eines Laufs und
 * openCirt merkt sich nichts: jeder Auftrag nennt alles selbst.
 *
 *     { "aktion": "erstellen", "projekt": "<Projektordner>",
 *       "bmk": true, "bas": true, "leistungslisten": false,
 *       "fehlende_referenzen": "abbrechen",      (optional, oder "weiter")
 *       "stempel_ohne_geraet": "abbrechen" }     (optional, oder "weiter")
 *
 *     { "aktion": "pdf", "projekt": "<Projektordner>",
 *       "ziel": "<Ordner>/<Name>.pdf", "ersetzen": false }
 *
 * Waehrend eines Laufs - per Auftrag oder per Knopf - steht der Stand in
 * <Temp>/OpenCirt_status.json, das Protokoll in <Temp>/OpenCirt_protokoll.txt.
 * Andere Programme lesen beides, ohne BricsCAD zu stoeren. Liegt die Datei
 * <Temp>/OpenCirt_abbrechen vor, haelt der laufende Lauf an wie nach einem
 * Klick auf "Abbrechen".
 *
 * Nur Qt, kein BRX: die Klasse laesst sich ohne BricsCAD testen.
 */

#ifndef OCAUFTRAG_H
#define OCAUFTRAG_H

#include <QString>
#include <QStringList>
#include <QJsonObject>
#include <QElapsedTimer>
#include <QDateTime>
#include <QFile>

namespace BatchProcessing {

// ============================================================================
// Auftrag
// ============================================================================

struct OcAuftrag {
    QString id;                 ///< Kennung des Aufrufers, sonst Zeitstempel
    QString aktion;             ///< "erstellen" oder "pdf"
    QString projekt;            ///< Projektordner (Schreibweise mit /)

    // aktion "erstellen"
    bool bmk = false;
    bool bas = false;
    bool leistungslisten = false;
    bool fehlendeReferenzenWeiter = false;   ///< sonst Abbruch
    bool stempelOhneGeraetWeiter = false;    ///< sonst Abbruch

    // aktion "pdf"
    QString ziel;               ///< vollstaendiger Pfad der PDF (mit /)
    bool ersetzen = false;      ///< vorhandene PDF ersetzen

    /// Auftragsdatei lesen und pruefen. Unbekannte Schluessel gelten als
    /// Fehler, damit ein Tippfehler nicht still eine Vorgabe ausloest.
    /// false: error nennt den Grund.
    static bool fromFile(const QString& path, OcAuftrag& out, QString& error);

    /// Kurzbeschreibung fuers Protokoll, z. B. "erstellen (BMK, BAS)"
    QString beschreibung() const;
};

// ============================================================================
// Laufstatus
// ============================================================================

class OcRunStatus {
public:
    OcRunStatus() = default;
    ~OcRunStatus();

    OcRunStatus(const OcRunStatus&) = delete;
    OcRunStatus& operator=(const OcRunStatus&) = delete;

    static QString statusPath();      ///< <Temp>/OpenCirt_status.json
    static QString protokollPath();   ///< <Temp>/OpenCirt_protokoll.txt
    static QString abbruchPath();     ///< <Temp>/OpenCirt_abbrechen

    /// Liegt eine Abbruch-Anforderung von aussen vor?
    static bool abbruchAngefordert();
    /// Abbruch-Anforderung entfernen (bei Beginn und Ende eines Laufs)
    static void abbruchEntfernen();

    /// Neuen Lauf beginnen: Protokoll leeren, Zustand "laeuft".
    /// quelle: "knopf" oder "auftrag".
    void begin(const QString& aktion, const QString& quelle,
               const QString& projekt, const QString& id);

    /// Zwischen begin() und finish()
    bool active() const { return m_active; }
    QString aktion() const { return m_aktion; }

    /// Abschnitt mit Zaehlung (schritt von von) und zuletzt bearbeiteter Datei
    void setPhase(const QString& phase, int schritt, int von, const QString& datei);
    /// Zwischenschritt ohne Zaehlung
    void setText(const QString& text);
    /// Protokollzeile; type wie im Fenster (INFO, SUCCESS, WARN, ERROR)
    void log(const QString& type, const QString& message);
    /// false, solange openCirt BricsCAD beschaeftigt haelt (Zeichnungen
    /// bearbeitet); waehrend des PDF-Publish in der Batch-Instanz true
    void setBricscadFrei(bool frei);
    void setAbbruchAngefordert();
    void setZaehler(const QString& name, int wert);
    void setPdf(const QString& path);

    /// Lauf beenden: zustand "fertig", "fehler" oder "abgebrochen".
    /// ergebnis leer: die letzte Ergebniszeile (SUCCESS/ERROR) des Laufs.
    void finish(const QString& zustand, const QString& ergebnis = QString());

    /// Letzte Ergebniszeile (SUCCESS oder ERROR) seit begin(), mit Typ
    QString letzteErgebniszeile() const { return m_lastResult; }
    bool letzteErgebniszeileIstFehler() const { return m_lastResultIsError; }

private:
    void write(bool force);

    bool m_active = false;
    QString m_id;
    QString m_quelle;
    QString m_aktion;
    QString m_projekt;
    QString m_zustand;
    QDateTime m_beginn;
    QDateTime m_ende;
    QString m_phase;
    int m_schritt = 0;
    int m_von = 0;
    QString m_datei;
    bool m_bricscadFrei = false;
    bool m_abbruch = false;
    QJsonObject m_zaehler;
    QString m_pdf;
    QString m_ergebnis;
    int m_fehlerzeilen = 0;
    QStringList m_letzteMeldungen;
    QStringList m_letzteFehler;
    QString m_lastResult;
    bool m_lastResultIsError = false;

    QFile m_protokoll;
    QElapsedTimer m_sinceWrite;
    bool m_dirty = false;
};

} // namespace BatchProcessing

#endif // OCAUFTRAG_H
