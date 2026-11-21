/**
 * @file OcAuftrag.h
 * @brief Auftraege von aussen und Laufstatus als Datei
 *
 * Ein Auftrag ist eine kleine JSON-Datei, die ein anderes Programm schreibt
 * und per LISP an openCirt uebergibt:
 *
 *     (opencirt-auftrag "/tmp/opencirt_auftrag.json")
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
 * Listen (seit 2.6.0), wie die Knoepfe "IO-Liste erstellen",
 * "Feldgeraeteliste erstellen" (seit 2.9.0, ersetzt "sensorliste") und
 * "Export Testdaten"; sie lesen die Zeichnungen nur:
 *
 *     { "aktion": "io_liste", "projekt": "<Projektordner>",
 *       "filter": "HW" }          ("" = alle Datenpunkte, "HW" = IO-Belegung,
 *                                  "BUS;SMI" = mehrere Integrationsarten)
 *
 *     { "aktion": "feldgeraeteliste", "projekt": "<Projektordner>" }
 *
 *     { "aktion": "testdaten", "projekt": "<Projektordner>",
 *       "geoeffnete_blaetter": "abbrechen" }   (optional, oder "weiter")
 *
 * Projekt aufbauen (seit 2.7.0), wie der Knopf "Projekt aufbauen": leert
 * '05- Projekt Zeichnungen' und erzeugt die Zeichnungen aus der Erstellliste
 * neu; mit "vorschau": true nur das Aufbau-Log, ohne Aenderung:
 *
 *     { "aktion": "aufbauen", "projekt": "<Projektordner>",
 *       "erstellliste": "<Ordner>/<Name>.csv", "vorschau": false }
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
    QString aktion;             ///< "erstellen", "pdf", "io_liste", "feldgeraeteliste", "testdaten", "aufbauen"
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

    // aktion "io_liste": Integrationsart(en) wie im Filterdialog,
    // leer = alle Datenpunkte
    QString filter;

    // aktion "testdaten": GA-FL-Blaetter in BricsCAD geoeffnet -> trotzdem
    // den gespeicherten Stand exportieren? Sonst Abbruch.
    bool geoeffneteBlaetterWeiter = false;

    // aktion "aufbauen": CSV-Erstellliste (vollstaendiger Pfad, mit /) und
    // Vorschau (nur Aufbau-Log) oder echter Lauf - beides Pflicht
    QString erstellliste;
    bool vorschau = false;

    /// Listen-Auftrag (io_liste, feldgeraeteliste, testdaten): liest die
    /// Zeichnungen nur und aendert sie nicht
    bool istListe() const {
        return aktion == "io_liste" || aktion == "feldgeraeteliste" || aktion == "testdaten";
    }

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
    /// Erzeugte Datei bzw. erzeugter Ordner einer Liste (seit 2.6.0)
    void setAusgabe(const QString& path);

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
    QString m_ausgabe;
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
