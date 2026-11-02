/**
 * @file Leistungsliste.cpp
 * @brief Anlagenleistungsliste: Zuordnung, Summen, Einspeisung, Seiten
 */

#include "Leistungsliste.h"

#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace BatchProcessing {
namespace OcLl {

namespace {

struct Alias {
    const char* column;
    QStringList labels;     ///< normalisiert, siehe normLabel
};

/// Bezeichnungen der Stempelzeilen je Spalte. Die Bezeichnungen kommen aus
/// der Erstellliste; erkannt werden die Spaltenkoepfe der Liste und die
/// Bezeichnungen, die die Vorlagen-Stempel schon tragen.
const QVector<Alias>& aliases()
{
    static const QVector<Alias> table = {
        { "OC_LL_ANZAHL",      { "anzahl" } },
        { "OC_LL_P_EINZELN",   { "pzu-einzeln", "pzu einzeln", "leistung", "leistung einzeln",
                                 QString::fromUtf8("leistung je st\xC3\xBC" "ck") } },
        { "OC_LL_P_GES",       { "pzu-ges", "pzu ges", "pzu-gesamt", "pzu gesamt",
                                 "leistung gesamt", "leistung ges" } },
        { "OC_LL_GLZ",         { "g-faktor", "gleichzeitigkeit", "gleichzeitigkeitsfaktor",
                                 "glz", "glz-faktor" } },
        { "OC_LL_ART",         { "art d. antr", "art d antr", "art des antriebes",
                                 "art des antriebs", "antriebsart", "antrieb",
                                 "ventilatortyp", "erhitzertyp" } },
        { "OC_LL_I_L1",        { "strom l1", "i l1", "i-l1", "l1" } },
        { "OC_LL_I_L2",        { "strom l2", "i l2", "i-l2", "l2" } },
        { "OC_LL_I_L3",        { "strom l3", "i l3", "i-l3", "l3" } },
        { "OC_LL_SPANNUNG",    { "spannung" } },
        { "OC_LL_P_AV",        { "pzu av", "pzu-av", "p av", "av" } },
        { "OC_LL_P_SV",        { "pzu sv", "pzu-sv", "p sv", "sv" } },
        { "OC_LL_ABSICHERUNG", { "absicherung" } },
    };
    return table;
}

QString deNumber(double value, int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

} // namespace

QString normLabel(const QString& label)
{
    QString s = label;
    const int bracket = s.indexOf(QLatin1Char('['));
    if (bracket >= 0) s = s.left(bracket);
    s = s.simplified().toLower();
    while (!s.isEmpty() && (s.endsWith(QLatin1Char(':')) || s.endsWith(QLatin1Char('.')))) {
        s.chop(1);
        s = s.trimmed();
    }
    return s;
}

QString columnForLabel(const QString& label)
{
    const QString n = normLabel(label);
    if (n.isEmpty()) return QString();
    for (const Alias& a : aliases()) {
        if (a.labels.contains(n)) return QLatin1String(a.column);
    }
    return QString();
}

QString aliasText()
{
    QStringList parts;
    for (const Alias& a : aliases()) {
        parts << QStringLiteral("%1 = %2").arg(QLatin1String(a.column).mid(6),
                                              a.labels.join(QStringLiteral(" | ")));
    }
    return parts.join(QStringLiteral("; "));
}

OcPairs zeile(const OcLlStamp& stamp, int pos, const QString& bereich,
              const QString& standort, QStringList* unmapped)
{
    OcPairs row;
    row << qMakePair(QStringLiteral("OC_LL_POS"), QString::number(pos))
        << qMakePair(QStringLiteral("OC_LL_BAS"), stamp.bas)
        << qMakePair(QStringLiteral("OC_LL_BEREICH"), bereich)
        << qMakePair(QStringLiteral("OC_LL_STANDORT"), standort);

    QStringList taken;
    QStringList stromOhnePhase, netz;
    for (const auto& z : stamp.zeilen) {
        const QString column = columnForLabel(z.first);
        if (column.isEmpty()) {
            const QString n = normLabel(z.first);
            if (n == QLatin1String("strom")) stromOhnePhase << z.first.trimmed();
            else if (n.startsWith(QLatin1String("netz"))) netz << z.first.trimmed();
            continue;
        }
        if (taken.contains(column)) continue;   // erste passende Zeile gilt
        taken << column;
        row << qMakePair(column, z.second.trimmed());
    }
    // Strom ohne Phase und Netz AV/SV lassen sich ohne Rechnung keiner Spalte
    // zuordnen. Auffallen soll das nur, wenn der Stempel die Werte nicht
    // ohnehin in eigenen Zeilen (Strom L1..L3, Pzu AV/SV) traegt.
    if (unmapped) {
        const bool hasPhase = taken.contains(QStringLiteral("OC_LL_I_L1"))
            || taken.contains(QStringLiteral("OC_LL_I_L2"))
            || taken.contains(QStringLiteral("OC_LL_I_L3"));
        const bool hasNetz = taken.contains(QStringLiteral("OC_LL_P_AV"))
            || taken.contains(QStringLiteral("OC_LL_P_SV"));
        if (!hasPhase) *unmapped << stromOhnePhase;
        if (!hasNetz) *unmapped << netz;
    }
    row << qMakePair(QStringLiteral("OC_LL_BEMERKUNG"), stamp.bemerkung.trimmed());
    return row;
}

double parseNumber(const QString& text, bool* ok)
{
    static const QRegularExpression re(QStringLiteral("[-+]?\\d+(?:[.,]\\d+)*"));
    if (ok) *ok = false;
    const QRegularExpressionMatch m = re.match(text);
    if (!m.hasMatch()) return 0.0;
    QString number = m.captured(0);
    // "1.234,5" -> Punkt als Tausendertrenner; sonst ist Komma oder Punkt das Dezimalzeichen
    if (number.contains(QLatin1Char(',')) && number.contains(QLatin1Char('.'))) {
        number.remove(QLatin1Char('.'));
    }
    number.replace(QLatin1Char(','), QLatin1Char('.'));
    bool good = false;
    const double value = number.toDouble(&good);
    if (ok) *ok = good;
    return good ? value : 0.0;
}

QString formatNumber(double value)
{
    return deNumber(value, 2);
}

void Summen::add(const OcPairs& row)
{
    for (const auto& pair : row) {
        bool ok = false;
        const double v = parseNumber(pair.second, &ok);
        if (!ok) continue;
        if (pair.first == QLatin1String("OC_LL_I_L1")) { l1 += v; hasL1 = true; }
        else if (pair.first == QLatin1String("OC_LL_I_L2")) { l2 += v; hasL2 = true; }
        else if (pair.first == QLatin1String("OC_LL_I_L3")) { l3 += v; hasL3 = true; }
        else if (pair.first == QLatin1String("OC_LL_P_AV")) { av += v; hasAv = true; }
        else if (pair.first == QLatin1String("OC_LL_P_SV")) { sv += v; hasSv = true; }
    }
}

double Summen::maxPhase() const
{
    return std::max({ l1, l2, l3 });
}

QVector<double> readEinspeisungen(const QString& path, bool* found)
{
    static const QRegularExpression sep(QStringLiteral("[;,\\t]"));
    QVector<double> sizes;
    bool ok = false;
    const QStringList lines = OcEngine::readLines(path, &ok);
    if (found) *found = ok;
    for (const QString& line : lines) {
        const QString first = line.section(sep, 0, 0).trimmed();
        bool isNumber = false;
        const double v = first.toDouble(&isNumber);
        if (isNumber && v > 0 && !sizes.contains(v)) sizes.append(v);
    }
    std::sort(sizes.begin(), sizes.end());
    return sizes;
}

QString einspeisung(double maxPhase, const QVector<double>& sizes, bool* tooLarge)
{
    if (tooLarge) *tooLarge = false;
    if (maxPhase <= 0.0) return QString();
    for (double size : sizes) {
        if (maxPhase < size) {
            const bool whole = std::fabs(size - std::round(size)) < 1e-9;
            return (whole ? QString::number(static_cast<qint64>(std::round(size)))
                          : deNumber(size, 1))
                   + QStringLiteral(" A");
        }
    }
    if (tooLarge) *tooLarge = !sizes.isEmpty();
    return QString();
}

OcPairs summeZeile(const Summen& s, const QString& einspeisungText)
{
    auto value = [](bool has, double v) { return has ? formatNumber(v) : QString(); };
    OcPairs pairs;
    pairs << qMakePair(QStringLiteral("OC_LL_SUM_I_L1"), value(s.hasL1, s.l1))
          << qMakePair(QStringLiteral("OC_LL_SUM_I_L2"), value(s.hasL2, s.l2))
          << qMakePair(QStringLiteral("OC_LL_SUM_I_L3"), value(s.hasL3, s.l3))
          << qMakePair(QStringLiteral("OC_LL_SUM_P_AV"), value(s.hasAv, s.av))
          << qMakePair(QStringLiteral("OC_LL_SUM_P_SV"), value(s.hasSv, s.sv))
          << qMakePair(QStringLiteral("OC_LL_EINSPEISUNG"), einspeisungText);
    return pairs;
}

QVector<OcLlPage> paginate(const OcPairs& aspBand, const QVector<Gruppe>& gruppen,
                           int rowsPerPage, int summeRows, const OcPairs& summe)
{
    QVector<OcLlPage> pages;
    if (rowsPerPage <= 0) return pages;

    OcLlPage cur;
    cur.aspBand = aspBand;
    auto flush = [&]() {
        pages.append(cur);
        cur = OcLlPage();
        cur.aspBand = aspBand;
    };

    for (const Gruppe& g : gruppen) {
        if (g.isEmpty()) continue;
        bool blank = !cur.rows.isEmpty();
        // Passt die Gruppe hier nicht mehr, auf einem neuen Blatt aber ganz:
        // neues Blatt, ohne Leerzeile oben
        if (blank && cur.rows.size() + 1 + g.size() > rowsPerPage && g.size() <= rowsPerPage) {
            flush();
            blank = false;
        }
        if (blank && cur.rows.size() + 1 >= rowsPerPage) {
            flush();
            blank = false;
        }
        if (blank) cur.rows.append(OcPairs());
        for (const OcPairs& row : g) {
            if (cur.rows.size() >= rowsPerPage) flush();
            cur.rows.append(row);
        }
    }

    if (cur.rows.isEmpty() && pages.isEmpty()) return pages;   // keine Stempel

    // Leerzeile und Summe; reicht der Platz nicht, steht die Summe allein
    // (unter Band und Kopf) auf einem weiteren Blatt
    if (!cur.rows.isEmpty() && cur.rows.size() + 1 + summeRows > rowsPerPage) {
        flush();
    }
    if (!cur.rows.isEmpty()) cur.rows.append(OcPairs());
    cur.withSumme = true;
    cur.summe = summe;
    pages.append(cur);
    return pages;
}

} // namespace OcLl
} // namespace BatchProcessing
