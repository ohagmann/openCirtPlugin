/**
 * @file Feldgeraeteliste.cpp
 * @brief Feldgeraeteliste (siehe Feldgeraeteliste.h)
 */

#include "Feldgeraeteliste.h"

#include <QMap>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>

namespace BatchProcessing {
namespace OcFg {

namespace {

/// Semikolon-getrennte Zeile mit Feldern in Anfuehrungszeichen (so schreibt
/// Calc, wenn ein Feld ein Semikolon enthaelt oder "Alle Textzellen in
/// Anfuehrungszeichen" gewaehlt ist)
QStringList splitCsv(const QString& line)
{
    QStringList cells;
    QString cell;
    bool quoted = false;
    for (int i = 0; i < line.size(); ++i) {
        const QChar c = line.at(i);
        if (quoted) {
            if (c == QLatin1Char('"')) {
                if (i + 1 < line.size() && line.at(i + 1) == QLatin1Char('"')) {
                    cell += c;
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                cell += c;
            }
        } else if (c == QLatin1Char('"')) {
            quoted = true;
        } else if (c == QLatin1Char(';')) {
            cells << cell.trimmed();
            cell.clear();
        } else {
            cell += c;
        }
    }
    cells << cell.trimmed();
    return cells;
}

/// Spaltenueberschrift vergleichbar machen: klein, Umlaute ausgeschrieben,
/// ohne Doppelpunkt/Punkt am Ende, einfache Leerzeichen
QString normKopf(const QString& text)
{
    QString s = text.trimmed().toLower();
    s.replace(QChar(0x00E4), QStringLiteral("ae"));
    s.replace(QChar(0x00F6), QStringLiteral("oe"));
    s.replace(QChar(0x00FC), QStringLiteral("ue"));
    s.replace(QChar(0x00DF), QStringLiteral("ss"));
    while (s.endsWith(QLatin1Char(':')) || s.endsWith(QLatin1Char('.'))) s.chop(1);
    return s.simplified();
}

bool istNein(const QString& value)
{
    static const QSet<QString> nein = {
        QStringLiteral("nein"), QStringLiteral("n"), QStringLiteral("no"),
        QStringLiteral("0"), QStringLiteral("false"), QStringLiteral("falsch")
    };
    return nein.contains(value.trimmed().toLower());
}

QString zelle(const QStringList& cells, int col)
{
    return (col >= 0 && col < cells.size()) ? cells.at(col).trimmed() : QString();
}

} // namespace

QString blockStamm(const QString& block)
{
    static const QRegularExpression version(QStringLiteral("_V_?\\d+$"),
                                            QRegularExpression::CaseInsensitiveOption);
    QString s = block.trimmed();
    s.remove(version);
    return OcEngine::lispUpper(s);
}

QString bmkStamm(const QString& aks)
{
    static const QRegularExpression nummer(QStringLiteral("[-_]*\\d*[-_]*$"));
    QString s = aks.trimmed();
    s.remove(nummer);
    return OcEngine::lispUpper(s);
}

QString funktionStamm(const QString& fcode)
{
    QString s;
    for (const QChar c : fcode.trimmed()) {
        if (!c.isLetter()) break;
        s += c;
    }
    return OcEngine::lispUpper(s);
}

// ============================================================================
// Referenz
// ============================================================================

QString Referenz::key(const QString& block, const QString& bmk, const QString& funktion)
{
    return block + QLatin1Char('|') + bmk + QLatin1Char('|') + funktion;
}

bool Referenz::load(const QString& path, QString* error, QStringList* hinweise)
{
    m_eintraege.clear();
    m_index.clear();

    bool ok = false;
    const QStringList lines = OcEngine::readLines(path, &ok);
    if (!ok) {
        if (error) *error = QStringLiteral("Datei nicht gefunden oder nicht lesbar");
        return false;
    }

    int colBlock = -1, colBmk = -1, colFunktion = -1, colArt = -1, colKat = -1,
        colLief = -1, colListe = -1, colPflicht = -1;
    bool kopfGefunden = false;
    int nr = 0;
    for (const QString& raw : lines) {
        ++nr;
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        const QStringList cells = splitCsv(line);

        if (!kopfGefunden) {
            kopfGefunden = true;
            for (int c = 0; c < cells.size(); ++c) {
                const QString k = normKopf(cells.at(c));
                if (k == QLatin1String("block")) colBlock = c;
                else if (k == QLatin1String("bmk") || k == QLatin1String("kuerzel")
                         || k == QLatin1String("oc_aks") || k == QLatin1String("aks")) colBmk = c;
                else if (k == QLatin1String("funktion")) colFunktion = c;
                else if (k == QLatin1String("geraeteart")) colArt = c;
                else if (k == QLatin1String("kategorie")) colKat = c;
                else if (k == QLatin1String("lieferung")) colLief = c;
                else if (k == QLatin1String("darstellung in liste") || k == QLatin1String("in liste")
                         || k == QLatin1String("liste")) colListe = c;
                else if (k == QLatin1String("ausfuehrung pflicht") || k == QLatin1String("ausfuehrung")
                         || k == QLatin1String("pflicht")) colPflicht = c;
            }
            if (colArt < 0 || (colBlock < 0 && colBmk < 0)) {
                if (error) {
                    *error = QStringLiteral("Kopfzeile ohne Spalte \"Geräteart\" oder ohne "
                                            "\"Block\" und \"BMK\": ") + line;
                }
                return false;
            }
            continue;
        }

        bool leer = true;
        for (const QString& c : cells) {
            if (!c.trimmed().isEmpty()) { leer = false; break; }
        }
        if (leer) continue;

        Eintrag e;
        e.block = blockStamm(zelle(cells, colBlock));
        e.bmk = bmkStamm(zelle(cells, colBmk));
        e.funktion = OcEngine::lispUpper(zelle(cells, colFunktion));
        e.geraeteart = zelle(cells, colArt);
        e.kategorie = zelle(cells, colKat);
        e.lieferung = zelle(cells, colLief);
        e.inListe = !istNein(zelle(cells, colListe));
        {
            const QString p = zelle(cells, colPflicht);
            e.ausfuehrungPflicht = !p.isEmpty() && !istNein(p);
        }
        e.zeile = nr;

        if (e.block.isEmpty() && e.bmk.isEmpty() && e.funktion.isEmpty()) {
            if (hinweise) {
                hinweise->append(QStringLiteral("Zeile %1: weder Block noch BMK noch Funktion - gilt nicht")
                                 .arg(nr));
            }
            continue;
        }
        const QString k = key(e.block, e.bmk, e.funktion);
        if (m_index.contains(k)) {
            if (hinweise) {
                hinweise->append(QStringLiteral("Zeile %1: gleicher Block/BMK/Funktion wie Zeile %2 - "
                                                "es gilt Zeile %2")
                                 .arg(nr).arg(m_eintraege.at(m_index.value(k)).zeile));
            }
            continue;
        }
        m_index.insert(k, m_eintraege.size());
        m_eintraege.append(e);
    }

    if (!kopfGefunden) {
        if (error) *error = QStringLiteral("Datei ist leer");
        return false;
    }
    return true;
}

const Eintrag* Referenz::geraet(const QString& blockStamm, const QString& bmkStamm) const
{
    const QString keys[] = {
        key(blockStamm, bmkStamm, QString()),
        key(blockStamm, QString(), QString()),
        key(QString(), bmkStamm, QString()),
    };
    for (const QString& k : keys) {
        const auto it = m_index.constFind(k);
        if (it != m_index.constEnd()) return &m_eintraege.at(it.value());
    }
    return nullptr;
}

const Eintrag* Referenz::funktion(const QString& blockStamm, const QString& bmkStamm,
                                  const QString& funktionStamm) const
{
    if (funktionStamm.isEmpty()) return nullptr;
    const QString keys[] = {
        key(blockStamm, bmkStamm, funktionStamm),
        key(blockStamm, QString(), funktionStamm),
        key(QString(), bmkStamm, funktionStamm),
        key(QString(), QString(), funktionStamm),
    };
    for (const QString& k : keys) {
        const auto it = m_index.constFind(k);
        if (it != m_index.constEnd()) return &m_eintraege.at(it.value());
    }
    return nullptr;
}

// ============================================================================
// Liste und Summe
// ============================================================================

QStringList kopf()
{
    return {
        QStringLiteral("Pos."), QStringLiteral("Los"), QStringLiteral("ASP"),
        QStringLiteral("Gewerk"), QStringLiteral("Anlage"), QStringLiteral("Ort"),
        QStringLiteral("BMK"), QStringLiteral("BAS"), QStringLiteral("Bezeichnung"),
        QString::fromUtf8("Ger\xC3\xA4teart"), QString::fromUtf8("Ausf\xC3\xBChrung"),
        QStringLiteral("Kategorie"),
        QStringLiteral("Lieferung"), QStringLiteral("Produkt"), QStringLiteral("Blatt"),
        QStringLiteral("Block")
    };
}

QStringList zellen(const Zeile& z, int pos, int stellen)
{
    return {
        QStringLiteral("%1").arg(pos, stellen, 10, QLatin1Char('0')),
        z.los, z.asp, z.gewerk, z.anlage, z.ort,
        z.bmk, z.bas, z.bezeichnung,
        z.geraeteart, z.ausfuehrung, z.kategorie, z.lieferung,
        z.produkt, z.blatt, z.block
    };
}

QString ausfuehrungFehlt()
{
    return QString::fromUtf8("(Ausf\xC3\xBChrung fehlt)");
}

QVector<QStringList> summe(const QVector<Zeile>& zeilen)
{
    // Spalten: Lose und ihre ASP in der Reihenfolge der Liste
    QStringList lose;
    QMap<QString, QStringList> aspJeLos;
    for (const Zeile& z : zeilen) {
        if (!lose.contains(z.los)) lose << z.los;
        QStringList& asps = aspJeLos[z.los];
        if (!asps.contains(z.asp)) asps << z.asp;
    }

    struct Spalte { QString titel; QString los; QString asp; enum { Asp, Los, Gesamt } art; };
    QVector<Spalte> spalten;
    for (const QString& los : lose) {
        for (const QString& asp : aspJeLos.value(los)) {
            const QString aspText = asp.isEmpty() ? QStringLiteral("(ohne ASP)") : asp;
            spalten.append({ los.isEmpty() ? aspText : los + QStringLiteral(" / ") + aspText,
                             los, asp, Spalte::Asp });
        }
        if (!los.isEmpty()) {
            spalten.append({ QStringLiteral("Summe ") + los, los, QString(), Spalte::Los });
        }
    }
    spalten.append({ QStringLiteral("Gesamt"), QString(), QString(), Spalte::Gesamt });

    // Zeilen: Kategorie + Geraeteart + Ausfuehrung + Lieferung
    struct Gruppe {
        QString kategorie, geraeteart, ausfuehrung, lieferung;
        bool unbekannt = false;
        QHash<QString, int> jeAsp;      ///< los|asp -> Anzahl
        QHash<QString, int> jeLos;
        int gesamt = 0;
    };
    QMap<QString, Gruppe> gruppen;      // Schluessel sortiert sie
    for (const Zeile& z : zeilen) {
        const QString kat = z.unbekannt ? QStringLiteral("?") : z.kategorie;
        // Unbekannte zuletzt: Praefix \x7F sortiert hinter alle Buchstaben
        const QString sortKey = (z.unbekannt ? QStringLiteral("\x7F") : QString())
            + kat.toLower() + QLatin1Char('\x1F') + z.geraeteart.toLower()
            // "(Ausfuehrung fehlt)" unter den echten Ausfuehrungen der Geraeteart
            + QLatin1Char('\x1F') + (z.ausfuehrung == ausfuehrungFehlt() ? QStringLiteral("\x7F") : QString())
            + z.ausfuehrung.simplified().toLower()
            + QLatin1Char('\x1F') + z.lieferung.toLower();
        Gruppe& g = gruppen[sortKey];
        g.kategorie = kat;
        g.geraeteart = z.geraeteart;
        g.ausfuehrung = z.ausfuehrung.simplified();
        g.lieferung = z.lieferung;
        g.unbekannt = z.unbekannt;
        g.jeAsp[z.los + QLatin1Char('|') + z.asp] += 1;
        g.jeLos[z.los] += 1;
        g.gesamt += 1;
    }

    QVector<QStringList> tabelle;
    QStringList kopfzeile = { QStringLiteral("Kategorie"), QString::fromUtf8("Ger\xC3\xA4teart"),
                              QString::fromUtf8("Ausf\xC3\xBChrung"), QStringLiteral("Lieferung") };
    for (const Spalte& s : spalten) kopfzeile << s.titel;
    tabelle.append(kopfzeile);

    auto zahl = [](int n) { return n > 0 ? QString::number(n) : QString(); };
    for (const Gruppe& g : gruppen) {
        QStringList row = { g.kategorie, g.geraeteart, g.ausfuehrung, g.lieferung };
        for (const Spalte& s : spalten) {
            switch (s.art) {
            case Spalte::Asp:    row << zahl(g.jeAsp.value(s.los + QLatin1Char('|') + s.asp)); break;
            case Spalte::Los:    row << zahl(g.jeLos.value(s.los)); break;
            case Spalte::Gesamt: row << QString::number(g.gesamt); break;
            }
        }
        tabelle.append(row);
    }
    return tabelle;
}

} // namespace OcFg
} // namespace BatchProcessing
