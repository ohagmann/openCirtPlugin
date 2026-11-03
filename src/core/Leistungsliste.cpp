/**
 * @file Leistungsliste.cpp
 * @brief Anlagenleistungsliste: Zuordnung, Summen, Einspeisung, Seiten
 */

#include "Leistungsliste.h"

#include <QMap>
#include <QRegularExpression>

#include <algorithm>
#include <cmath>

namespace BatchProcessing {
namespace OcLl {

double parseNumber(const QString& text, bool* ok);

namespace {

struct Alias {
    const char* key;        ///< Listenspalte OC_LL_... oder Eingabe STROM / NETZ
    QStringList labels;     ///< normalisiert, siehe normLabel
};

/// Bezeichnungen der Stempelzeilen. Die Bezeichnungen kommen aus der
/// Erstellliste; erkannt werden die Spaltenkoepfe der Liste und die
/// Bezeichnungen, die die Vorlagen-Stempel tragen. Die ersten fuenf werden so
/// uebernommen, wie sie im Stempel stehen; STROM und NETZ sind nur Eingaben
/// der Rechnung.
const QVector<Alias>& aliases()
{
    static const QVector<Alias> table = {
        { "OC_LL_ANZAHL",      { "anzahl" } },
        { "OC_LL_P_EINZELN",   { "pzu-einzeln", "pzu einzeln", "leistung", "leistung einzeln",
                                 QString::fromUtf8("leistung je st\xC3\xBC" "ck") } },
        { "OC_LL_GLZ",         { "g-faktor", "gleichzeitigkeit", "gleichzeitigkeitsfaktor",
                                 "glz", "glz-faktor" } },
        { "OC_LL_ART",         { "art d. antr", "art d antr", "art des antriebes",
                                 "art des antriebs", "antriebsart", "antrieb",
                                 "ventilatortyp", "erhitzertyp" } },
        { "OC_LL_SPANNUNG",    { "spannung" } },
        { "STROM",             { "strom", "nennstrom", QString::fromUtf8("strom je st\xC3\xBC" "ck") } },
        { "NETZ",              { "netz", "netzart" } },
    };
    return table;
}

QString deNumber(double value, int decimals)
{
    QString text = QString::number(value, 'f', decimals);
    text.replace(QLatin1Char('.'), QLatin1Char(','));
    return text;
}

/// Spannung in V: die groesste Zahl im Text ("400", "3~400 V", "1~230" -> 230)
double voltageOf(const QString& text, bool* ok)
{
    static const QRegularExpression re(QStringLiteral("\\d+(?:[.,]\\d+)?"));
    double best = 0.0;
    bool any = false;
    QRegularExpressionMatchIterator it = re.globalMatch(text);
    while (it.hasNext()) {
        bool good = false;
        const double v = parseNumber(it.next().captured(0), &good);
        if (good) {
            best = any ? std::max(best, v) : v;
            any = true;
        }
    }
    if (ok) *ok = any;
    return best;
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
        if (a.labels.contains(n)) return QLatin1String(a.key);
    }
    return QString();
}

QString aliasText()
{
    QStringList parts;
    for (const Alias& a : aliases()) {
        QString key = QLatin1String(a.key);
        if (key.startsWith(QLatin1String("OC_LL_"))) key = key.mid(6);
        parts << QStringLiteral("%1 = %2").arg(key, a.labels.join(QStringLiteral(" | ")));
    }
    return parts.join(QStringLiteral("; "));
}

OcPairs zeile(const OcLlStamp& stamp, const QString& bereich,
              const QString& standort, int& phase, QStringList* hinweise)
{
    // Stempelzeilen nach Bezeichnung; die erste passende Zeile gilt
    QMap<QString, QString> in;
    for (const auto& z : stamp.zeilen) {
        const QString key = columnForLabel(z.first);
        if (!key.isEmpty() && !in.contains(key)) in.insert(key, z.second.trimmed());
    }

    OcPairs row;
    row << qMakePair(QStringLiteral("OC_LL_BAS"), stamp.bas)
        << qMakePair(QStringLiteral("OC_LL_BEREICH"), bereich)
        << qMakePair(QStringLiteral("OC_LL_STANDORT"), standort);
    for (const char* key : { "OC_LL_ANZAHL", "OC_LL_P_EINZELN", "OC_LL_GLZ",
                             "OC_LL_ART", "OC_LL_SPANNUNG" }) {
        row << qMakePair(QString::fromLatin1(key), in.value(QLatin1String(key)));
    }

    auto hinweis = [hinweise](const QString& text) { if (hinweise) *hinweise << text; };

    // Anzahl und Gleichzeitigkeit: ohne Angabe 1
    bool okN = false, okG = false, okP = false, okI = false, okU = false;
    double n = parseNumber(in.value(QStringLiteral("OC_LL_ANZAHL")), &okN);
    if (!okN) { n = 1.0; hinweis(QStringLiteral("Anzahl fehlt, 1 angenommen")); }
    double g = parseNumber(in.value(QStringLiteral("OC_LL_GLZ")), &okG);
    if (!okG) { g = 1.0; hinweis(QStringLiteral("Gleichzeitigkeit fehlt, 1 angenommen")); }

    // Pzu-Ges. = Anzahl * Pzu-Einzeln * Gleichzeitigkeit
    const double p = parseNumber(in.value(QStringLiteral("OC_LL_P_EINZELN")), &okP);
    const double pges = n * p * g;
    if (okP) {
        row << qMakePair(QStringLiteral("OC_LL_P_GES"), formatNumber(pges));
    } else {
        hinweis(QStringLiteral("Leistung fehlt"));
    }

    // Strom je Phase = Strom * Anzahl * Gleichzeitigkeit; 400 V auf L1-L3,
    // 230 V reihum auf L1, L2, L3, L1 ...
    const double i = parseNumber(in.value(QStringLiteral("STROM")), &okI);
    const double u = voltageOf(in.value(QStringLiteral("OC_LL_SPANNUNG")), &okU);
    if (!okI) {
        hinweis(QStringLiteral("Strom fehlt"));
    } else if (okU && u >= 380.0) {
        const QString itext = formatNumber(i * n * g);
        row << qMakePair(QStringLiteral("OC_LL_I_L1"), itext)
            << qMakePair(QStringLiteral("OC_LL_I_L2"), itext)
            << qMakePair(QStringLiteral("OC_LL_I_L3"), itext);
    } else if (okU && u >= 180.0 && u <= 260.0) {
        const QString tag = QStringLiteral("OC_LL_I_L%1").arg(phase % 3 + 1);
        ++phase;
        row << qMakePair(tag, formatNumber(i * n * g));
    } else {
        hinweis(QStringLiteral("Spannung weder 230 noch 400 V, Strom nicht verteilt"));
    }

    // Pzu-Ges. in die Spalte der Netzart aus dem Stempel
    const QString netz = in.value(QStringLiteral("NETZ")).toUpper();
    const bool av = netz.contains(QLatin1String("AV"));
    const bool sv = netz.contains(QLatin1String("SV"));
    if (okP) {
        if (av && !sv) row << qMakePair(QStringLiteral("OC_LL_P_AV"), formatNumber(pges));
        else if (sv && !av) row << qMakePair(QStringLiteral("OC_LL_P_SV"), formatNumber(pges));
        else hinweis(QStringLiteral("Netz weder AV noch SV"));
    }

    row << qMakePair(QStringLiteral("OC_LL_ABSICHERUNG"), stamp.absicherung.trimmed())
        << qMakePair(QStringLiteral("OC_LL_BEMERKUNG"), stamp.bemerkung.trimmed());
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

    // Pos. ueber alle Zeilen durch, auch die Leerzeilen
    int pos = 1;
    for (OcLlPage& page : pages) {
        for (OcPairs& row : page.rows) {
            row.prepend(qMakePair(QStringLiteral("OC_LL_POS"), QString::number(pos++)));
        }
    }
    return pages;
}

} // namespace OcLl
} // namespace BatchProcessing
