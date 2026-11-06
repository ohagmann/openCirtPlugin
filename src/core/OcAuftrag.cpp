/**
 * @file OcAuftrag.cpp
 * @brief Auftraege von aussen und Laufstatus als Datei (siehe OcAuftrag.h)
 */

#include "OcAuftrag.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QSaveFile>
#include <QSet>
#include <QStandardPaths>
#include <QTextStream>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "unbekannt"
#endif

namespace BatchProcessing {

namespace {

QString tempDir() {
    return QStandardPaths::writableLocation(QStandardPaths::TempLocation);
}

QString zeit(const QDateTime& t) {
    return t.isValid() ? t.toString("yyyy-MM-ddTHH:mm:ss") : QString();
}

/// Pflichtfeld vom Typ bool
bool readBool(const QJsonObject& o, const QString& key, bool& out, QString& error) {
    if (!o.contains(key)) {
        error = QString("\"%1\" fehlt (true oder false angeben)").arg(key);
        return false;
    }
    if (!o.value(key).isBool()) {
        error = QString("\"%1\" muss true oder false sein").arg(key);
        return false;
    }
    out = o.value(key).toBool();
    return true;
}

/// Antwort auf eine Rueckfrage: "weiter" oder "abbrechen", fehlt = abbrechen
bool readAntwort(const QJsonObject& o, const QString& key, bool& weiter, QString& error) {
    weiter = false;
    if (!o.contains(key)) return true;
    const QString v = o.value(key).toString().trimmed().toLower();
    if (v == "weiter") { weiter = true; return true; }
    if (v == "abbrechen") return true;
    error = QString("\"%1\" muss \"weiter\" oder \"abbrechen\" sein").arg(key);
    return false;
}

}  // namespace

// ============================================================================
// OcAuftrag
// ============================================================================

bool OcAuftrag::fromFile(const QString& path, OcAuftrag& out, QString& error) {
    out = OcAuftrag();
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        error = QString("Auftragsdatei nicht lesbar: %1").arg(QDir::toNativeSeparators(path));
        return false;
    }
    QJsonParseError pe;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &pe);
    if (pe.error != QJsonParseError::NoError || !doc.isObject()) {
        error = QString("Auftragsdatei ist kein JSON-Objekt (%1)").arg(pe.errorString());
        return false;
    }
    const QJsonObject o = doc.object();

    out.aktion = o.value("aktion").toString().trimmed().toLower();
    QSet<QString> erlaubt = { "id", "aktion", "projekt" };
    if (out.aktion == "erstellen") {
        erlaubt += { "bmk", "bas", "leistungslisten",
                     "fehlende_referenzen", "stempel_ohne_geraet" };
    } else if (out.aktion == "pdf") {
        erlaubt += { "ziel", "ersetzen" };
    } else {
        error = out.aktion.isEmpty()
            ? QString("\"aktion\" fehlt (\"erstellen\" oder \"pdf\")")
            : QString("Unbekannte aktion \"%1\" (\"erstellen\" oder \"pdf\")").arg(out.aktion);
        return false;
    }
    for (auto it = o.constBegin(); it != o.constEnd(); ++it) {
        if (!erlaubt.contains(it.key())) {
            error = QString("Unbekannter Schluessel \"%1\" fuer aktion \"%2\"")
                    .arg(it.key(), out.aktion);
            return false;
        }
    }

    out.id = o.value("id").toString().trimmed();
    if (out.id.isEmpty()) {
        out.id = QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss");
    }

    const QString projekt = o.value("projekt").toString().trimmed();
    if (projekt.isEmpty()) {
        error = "\"projekt\" fehlt (Projektordner)";
        return false;
    }
    out.projekt = QDir::cleanPath(QDir::fromNativeSeparators(projekt));
    if (!QFileInfo(out.projekt).isDir()) {
        error = QString("Projektordner nicht gefunden: %1")
                .arg(QDir::toNativeSeparators(out.projekt));
        return false;
    }

    if (out.aktion == "erstellen") {
        if (!readBool(o, "bmk", out.bmk, error)) return false;
        if (!readBool(o, "bas", out.bas, error)) return false;
        if (!readBool(o, "leistungslisten", out.leistungslisten, error)) return false;
        if (!readAntwort(o, "fehlende_referenzen", out.fehlendeReferenzenWeiter, error)) return false;
        if (!readAntwort(o, "stempel_ohne_geraet", out.stempelOhneGeraetWeiter, error)) return false;
    } else {
        const QString ziel = o.value("ziel").toString().trimmed();
        if (ziel.isEmpty()) {
            error = "\"ziel\" fehlt (vollstaendiger Pfad der PDF)";
            return false;
        }
        out.ziel = QDir::cleanPath(QDir::fromNativeSeparators(ziel));
        if (!QDir::isAbsolutePath(out.ziel)) {
            error = QString("\"ziel\" muss ein vollstaendiger Pfad sein: %1").arg(ziel);
            return false;
        }
        if (!out.ziel.endsWith(".pdf", Qt::CaseInsensitive)
            || QFileInfo(out.ziel).completeBaseName().isEmpty()) {
            error = QString("\"ziel\" muss auf .pdf enden: %1").arg(ziel);
            return false;
        }
        if (o.contains("ersetzen")) {
            if (!o.value("ersetzen").isBool()) {
                error = "\"ersetzen\" muss true oder false sein";
                return false;
            }
            out.ersetzen = o.value("ersetzen").toBool();
        }
    }
    return true;
}

QString OcAuftrag::beschreibung() const {
    if (aktion == "erstellen") {
        QStringList teile;
        if (bmk) teile << "BMK";
        if (bas) teile << "BAS";
        if (leistungslisten) teile << "Leistungslisten";
        return QString("Projekt erstellen (%1)")
               .arg(teile.isEmpty() ? QString("ohne BMK/BAS/Leistungslisten") : teile.join(", "));
    }
    return QString("PDF publizieren nach %1%2")
           .arg(QDir::toNativeSeparators(ziel), ersetzen ? " (ersetzen)" : "");
}

// ============================================================================
// OcRunStatus
// ============================================================================

OcRunStatus::~OcRunStatus() {
    if (m_protokoll.isOpen()) m_protokoll.close();
}

QString OcRunStatus::statusPath() { return tempDir() + "/OpenCirt_status.json"; }
QString OcRunStatus::protokollPath() { return tempDir() + "/OpenCirt_protokoll.txt"; }
QString OcRunStatus::abbruchPath() { return tempDir() + "/OpenCirt_abbrechen"; }

bool OcRunStatus::abbruchAngefordert() {
    return QFileInfo::exists(abbruchPath());
}

void OcRunStatus::abbruchEntfernen() {
    QFile::remove(abbruchPath());
}

void OcRunStatus::begin(const QString& aktion, const QString& quelle,
                        const QString& projekt, const QString& id) {
    m_active = true;
    m_id = id;
    m_quelle = quelle;
    m_aktion = aktion;
    m_projekt = QDir::toNativeSeparators(projekt);
    m_zustand = "laeuft";
    m_beginn = QDateTime::currentDateTime();
    m_ende = QDateTime();
    m_phase.clear();
    m_schritt = 0;
    m_von = 0;
    m_datei.clear();
    m_bricscadFrei = false;
    m_abbruch = false;
    m_zaehler = QJsonObject();
    m_pdf.clear();
    m_ergebnis.clear();
    m_fehlerzeilen = 0;
    m_letzteMeldungen.clear();
    m_letzteFehler.clear();
    m_lastResult.clear();
    m_lastResultIsError = false;

    if (m_protokoll.isOpen()) m_protokoll.close();
    m_protokoll.setFileName(protokollPath());
    if (m_protokoll.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text)) {
        QTextStream out(&m_protokoll);
        out.setEncoding(QStringConverter::Utf8);
        out << "openCirt " << PLUGIN_VERSION << " - " << aktion << " (" << quelle << ")"
            << (id.isEmpty() ? QString() : " Auftrag " + id) << "\n"
            << "Projekt: " << m_projekt << "\n"
            << "Beginn: " << zeit(m_beginn) << "\n\n";
        out.flush();
    }
    write(true);
}

void OcRunStatus::setPhase(const QString& phase, int schritt, int von, const QString& datei) {
    if (!m_active) return;
    m_phase = phase;
    m_schritt = schritt;
    m_von = von;
    m_datei = datei;
    write(false);
}

void OcRunStatus::setText(const QString& text) {
    if (!m_active) return;
    m_phase = text;
    m_schritt = 0;
    m_von = 0;
    m_datei.clear();
    write(false);
}

void OcRunStatus::log(const QString& type, const QString& message) {
    // Kurz nach finish() noch: die Zusammenfassung eines Laufs steht oft
    // erst danach im Fenster und gehoert noch ins Protokoll dieses Laufs.
    // Spaetere Zeilen (anderes Projekt gewaehlt ...) nicht mehr.
    if (m_zustand.isEmpty()) return;   // noch nie ein Lauf
    if (!m_active && m_ende.isValid()
        && m_ende.msecsTo(QDateTime::currentDateTime()) > 3000) {
        return;
    }

    const QString t = QDateTime::currentDateTime().toString("HH:mm:ss");
    const QString typ = type.isEmpty() ? QString("INFO") : type.toUpper();
    if (m_protokoll.isOpen()) {
        QTextStream out(&m_protokoll);
        out.setEncoding(QStringConverter::Utf8);
        out << t << " " << typ.leftJustified(7) << " " << message << "\n";
        out.flush();
    }
    if (!m_active) return;

    const QString line = QString("%1 [%2] %3").arg(t, typ, message);
    m_letzteMeldungen << line;
    while (m_letzteMeldungen.size() > 15) m_letzteMeldungen.removeFirst();
    const bool fehler = (typ == "ERROR" || typ == "ERR");
    if (fehler) {
        ++m_fehlerzeilen;
        m_letzteFehler << line;
        while (m_letzteFehler.size() > 10) m_letzteFehler.removeFirst();
    }
    if (fehler || typ == "SUCCESS") {
        m_lastResult = message;
        m_lastResultIsError = fehler;
    }
    write(false);
}

void OcRunStatus::setBricscadFrei(bool frei) {
    if (!m_active) return;
    m_bricscadFrei = frei;
    write(true);
}

void OcRunStatus::setAbbruchAngefordert() {
    if (!m_active) return;
    m_abbruch = true;
    write(true);
}

void OcRunStatus::setZaehler(const QString& name, int wert) {
    if (!m_active) return;
    m_zaehler.insert(name, wert);
    write(false);
}

void OcRunStatus::setPdf(const QString& path) {
    if (!m_active) return;
    m_pdf = QDir::toNativeSeparators(path);
    write(false);
}

void OcRunStatus::finish(const QString& zustand, const QString& ergebnis) {
    if (!m_active) return;
    m_zustand = zustand;
    m_ergebnis = ergebnis.isEmpty() ? m_lastResult : ergebnis;
    m_ende = QDateTime::currentDateTime();
    m_bricscadFrei = true;
    m_active = false;
    if (m_protokoll.isOpen()) {
        QTextStream out(&m_protokoll);
        out.setEncoding(QStringConverter::Utf8);
        out << "\nEnde: " << zeit(m_ende) << " - " << zustand
            << (m_ergebnis.isEmpty() ? QString() : ": " + m_ergebnis) << "\n";
        out.flush();
    }
    abbruchEntfernen();
    write(true);
}

void OcRunStatus::write(bool force) {
    // Hoechstens alle 250 ms schreiben; Zustandswechsel immer
    if (!force && m_sinceWrite.isValid() && m_sinceWrite.elapsed() < 250) {
        m_dirty = true;
        return;
    }
    m_dirty = false;
    m_sinceWrite.start();

    QJsonObject o;
    o["programm"] = "openCirt";
    o["version"] = PLUGIN_VERSION;
    o["pid"] = QCoreApplication::applicationPid();
    o["id"] = m_id;
    o["quelle"] = m_quelle;
    o["aktion"] = m_aktion;
    o["projekt"] = m_projekt;
    o["zustand"] = m_zustand;
    o["bricscad_frei"] = m_bricscadFrei;
    o["abbruch_angefordert"] = m_abbruch;
    o["beginn"] = zeit(m_beginn);
    o["aktualisiert"] = zeit(QDateTime::currentDateTime());
    o["ende"] = zeit(m_ende);
    o["dauer_s"] = static_cast<qint64>(
        m_beginn.secsTo(m_ende.isValid() ? m_ende : QDateTime::currentDateTime()));
    o["phase"] = m_phase;
    o["schritt"] = m_schritt;
    o["von"] = m_von;
    o["datei"] = m_datei;
    o["zaehler"] = m_zaehler;
    o["pdf"] = m_pdf;
    o["ergebnis"] = m_ergebnis;
    o["fehlerzeilen"] = m_fehlerzeilen;
    o["letzte_fehler"] = QJsonArray::fromStringList(m_letzteFehler);
    o["letzte_meldungen"] = QJsonArray::fromStringList(m_letzteMeldungen);
    o["protokoll"] = QDir::toNativeSeparators(protokollPath());

    QSaveFile f(statusPath());
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
        f.commit();
    }
}

} // namespace BatchProcessing
