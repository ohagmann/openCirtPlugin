#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file OpenCirtEngine.cpp
 * @brief openCirt-Operationen auf Zeichnungen ohne Editor, siehe OpenCirtEngine.h
 *
 * Die Funktionen folgen den LISP-Skripten Schritt fuer Schritt. Kommentare
 * der Form (oc-...) nennen die Funktion des Skripts, die hier uebertragen ist.
 */

// BRX Platform headers
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

// BRX API headers
#include "aced.h"
#include "dbsymtb.h"    // Block- und Layertabelle
#include "dbents.h"     // AcDbBlockReference, AcDbAttribute, AcDbText
#include "dbmtext.h"    // AcDbMText
#include "dbpl.h"       // AcDbPolyline
#include "BrxSpecific/BrxGenericPropertiesAccess.h"   // Name parametrischer Bloecke

#include "OpenCirtEngine.h"
#include "TextAlignment.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QRegularExpression>
#include <QSet>
#include <QStringDecoder>

#include <algorithm>
#include <cmath>
#include <vector>

namespace BatchProcessing {

// ============================================================================
// Protokolldatei
// ============================================================================

OcLogFile::~OcLogFile()
{
    close();
}

bool OcLogFile::open(const QString& path, bool truncate)
{
    close();
    m_file.setFileName(path);
    return m_file.open(truncate ? (QIODevice::WriteOnly | QIODevice::Truncate)
                                : (QIODevice::WriteOnly | QIODevice::Append));
}

void OcLogFile::close()
{
    if (m_file.isOpen()) m_file.close();
}

void OcLogFile::write(const QString& message)
{
    if (m_file.isOpen()) m_file.write(message.toUtf8());
}

// ============================================================================
// Hilfsfunktionen ohne Zeichnung
// ============================================================================

namespace {

/// Windows-1252: Zeichen der Bytes 0x80..0x9F (0 = nicht belegt)
const char16_t kCp1252High[32] = {
    0x20AC, 0,      0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
    0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0,      0x017D, 0,
    0,      0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
    0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0,      0x017E, 0x0178
};

QString decodeCp1252(const QByteArray& bytes)
{
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b >= 0x80 && b <= 0x9F && kCp1252High[b - 0x80] != 0) {
            out.append(QChar(kCp1252High[b - 0x80]));
        } else {
            out.append(QChar(static_cast<char16_t>(b)));
        }
    }
    return out;
}

/// Byte eines Zeichens in Windows-1252, -1 wenn nicht darstellbar
int cp1252Byte(char32_t cp)
{
    if (cp < 0x80) return static_cast<int>(cp);
    if (cp >= 0xA0 && cp <= 0xFF) return static_cast<int>(cp);
    for (int i = 0; i < 32; ++i) {
        if (kCp1252High[i] != 0 && kCp1252High[i] == cp) return 0x80 + i;
    }
    return -1;
}

QString decodeText(QByteArray bytes)
{
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
        return QString::fromUtf8(bytes);
    }
    QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
    const QString decoded = utf8.decode(bytes);
    if (!utf8.hasError()) return decoded;
    return decodeCp1252(bytes);
}

const char* const kNewline =
#ifdef _WIN32
    "\r\n";
#else
    "\n";
#endif

QString fromAchar(const ACHAR* s)
{
    return s ? QString::fromWCharArray(s) : QString();
}

/// Sortierung wie (vl-sort). BricsCAD benutzt dafuer die stabile Sortierung
/// der C++-Bibliothek. Solange der Vergleich widerspruchsfrei ist, liefert
/// jede stabile Sortierung dasselbe. Der Vergleich der BMK-Nummerierung ist
/// es wegen seiner Toleranz nicht immer; dann entscheidet das Verfahren. Hier
/// steht das der Windows-Fassung: bis 32 Elemente Einfuegesortierung, darueber
/// Abschnitte von 32 Elementen, die paarweise gemischt werden.
template <typename T, typename Less>
void insertionSort(std::vector<T>& v, size_t first, size_t last, Less less)
{
    for (size_t mid = first + 1; mid < last; ++mid) {
        T val = v[mid];
        if (less(val, v[first])) {
            for (size_t k = mid; k > first; --k) v[k] = v[k - 1];
            v[first] = val;
        } else {
            size_t hole = mid;
            while (less(val, v[hole - 1])) {
                v[hole] = v[hole - 1];
                --hole;
            }
            v[hole] = val;
        }
    }
}

template <typename T, typename Less>
void mergeRuns(const std::vector<T>& src, std::vector<T>& dst,
               size_t first, size_t mid, size_t last, Less less)
{
    size_t a = first, b = mid, o = first;
    while (a < mid && b < last) {
        if (less(src[b], src[a])) dst[o++] = src[b++];
        else                      dst[o++] = src[a++];
    }
    while (a < mid)  dst[o++] = src[a++];
    while (b < last) dst[o++] = src[b++];
}

template <typename T, typename Less>
void chunkedMergeSort(std::vector<T>& v, size_t first, size_t last, Less less)
{
    const size_t kChunk = 32;
    const size_t count = last - first;
    for (size_t i = first; i < last; i += kChunk) {
        insertionSort(v, i, std::min(i + kChunk, last), less);
    }
    if (count <= kChunk) return;

    std::vector<T> buffer(v);
    std::vector<T>* src = &v;
    std::vector<T>* dst = &buffer;
    for (size_t chunk = kChunk; chunk < count; chunk *= 2) {
        for (size_t i = first; i < last; i += 2 * chunk) {
            const size_t mid = std::min(i + chunk, last);
            const size_t end = std::min(i + 2 * chunk, last);
            mergeRuns(*src, *dst, i, mid, end, less);
        }
        std::swap(src, dst);
    }
    if (src != &v) {
        for (size_t i = first; i < last; ++i) v[i] = (*src)[i];
    }
}

template <typename T, typename Less>
void vlSort(QVector<T>& list, Less less)
{
    std::vector<T> v(list.begin(), list.end());
    const size_t n = v.size();
    if (n <= 32) {
        if (n > 1) insertionSort(v, 0, n, less);
    } else {
        const size_t half = n - n / 2;
        chunkedMergeSort(v, 0, half, less);
        chunkedMergeSort(v, half, n, less);
        std::vector<T> merged(v);
        mergeRuns(v, merged, 0, half, n, less);
        v.swap(merged);
    }
    list = QVector<T>(v.begin(), v.end());
}

/// Schluessel wie (rtos y 2 1): auf eine Nachkommastelle, kaufmaennisch
qint64 yKey(double y)
{
    const qint64 k = static_cast<qint64>(std::floor(std::fabs(y) * 10.0 + 0.5));
    return (y < 0.0) ? -k : k;
}

/// Funktionsspalten der GA-FL (oc-fl-get-function-attr-bases), Index 0 = OC_INTEG
const QStringList& functionBases()
{
    static const QStringList bases = {
        "OC_INTEG",
        "OC_1_1_1", "OC_1_1_2", "OC_1_1_3", "OC_1_1_4",
        "OC_1_2_1", "OC_1_2_2",
        "OC_1_3_1", "OC_1_3_2", "OC_1_3_3", "OC_1_3_4", "OC_1_3_5",
        "OC_2_1_1", "OC_2_1_2",
        "OC_2_2_1", "OC_2_2_2", "OC_2_2_3", "OC_2_2_4", "OC_2_2_5",
        "OC_2_2_6", "OC_2_2_7", "OC_2_2_8", "OC_2_2_9", "OC_2_2_10",
        "OC_2_2_11", "OC_2_2_12",
        "OC_2_3_1", "OC_2_3_2", "OC_2_3_3", "OC_2_3_4", "OC_2_3_5",
        "OC_2_3_6", "OC_2_3_7", "OC_2_3_8", "OC_2_3_9",
        "OC_2_4_1", "OC_2_4_2", "OC_2_4_3", "OC_2_4_4", "OC_2_4_5",
        "OC_2_5_1", "OC_2_5_2", "OC_2_5_3", "OC_2_5_4",
        "OC_2_6_1", "OC_2_6_2", "OC_2_6_3", "OC_2_6_4", "OC_2_6_5",
        "OC_2_7_1", "OC_2_7_2", "OC_2_7_3", "OC_2_7_4",
        "OC_3_1_1", "OC_3_1_2", "OC_3_1_3", "OC_3_1_4", "OC_3_1_5", "OC_3_1_6"
    };
    return bases;
}

/// Spalten der extrahierten CSV (oc-dp-get-function-columns), mit OC_4_1_1
const QStringList& extractColumns()
{
    static const QStringList cols = [] {
        QStringList c = functionBases();
        c.removeFirst();
        c << "OC_4_1_1";
        return c;
    }();
    return cols;
}

/// Plankopf-Attribute, die die Extraktion mitnimmt (oc-dp-extract-plankopf)
const QStringList& plankopfAttributes()
{
    static const QStringList attrs = {
        "ASP", "GEWERK", "ANLAGE", "ZEICHNUNGSNUMMER", "SSK",
        "LPH",
        "KOSTENGRUPPE", "ORTSKENNZEICHEN", "BEMERKUNG",
        "ERSTELLER", "ERSTELLDATUM", "GEPRUEFT", "NORM",
        "NAME1", "NAME2", "NAME3",
        "AN1", "AN2", "AN3", "AN4", "AN5",
        "AG1", "AG2", "AG3", "AG4", "AG5",
        "PR1", "PR2", "PR3", "PR4", "PR5",
        "FREITEXT_01", "FREITEXT_02", "FREITEXT_03",
        "FREITEXT_04", "FREITEXT_05",
        "AENDERUNG1", "AENDERUNG2", "AENDERUNG3",
        "DATUM1", "DATUM2", "DATUM3",
        "INDEX1", "INDEX2", "INDEX3",
        "ERSATZFUER"
    };
    return attrs;
}

/// (oc-dp-escape-csv)
QString escapeCsv(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (const QChar ch : s) {
        if (ch == QLatin1Char(';'))       out.append(QLatin1Char(','));
        else if (ch == QLatin1Char('\n')) out.append(QLatin1Char(' '));
        else if (ch == QLatin1Char('\r')) { /* entfaellt */ }
        else                              out.append(ch);
    }
    return out;
}

/// (oc-str-split): Teile ohne die leeren
QStringList splitNonEmpty(const QString& s, QChar delimiter)
{
    return s.split(delimiter, Qt::SkipEmptyParts);
}

/// (oc-remove-trailing-numbers)
QString removeTrailingDigits(const QString& text)
{
    int i = text.size();
    while (i > 0) {
        const ushort u = text.at(i - 1).unicode();
        if (u < '0' || u > '9') break;
        --i;
    }
    return text.left(i);
}

/// (oc-format-number)
QString twoDigits(int n)
{
    return (n < 10) ? QStringLiteral("0") + QString::number(n) : QString::number(n);
}

bool isLockValue(const QString& value)
{
    if (value.isEmpty()) return false;
    const QString v = OcEngine::lispUpper(OcEngine::lispTrim(value, QStringLiteral(" \t")));
    return v == QLatin1String("JA") || v == QLatin1String("TRUE") || v == QLatin1String("1")
        || v == QLatin1String("X") || v == QLatin1String("HIGH") || v == QLatin1String("WAHR");
}

struct Rect {
    double cx = 0, cy = 0, w = 0, h = 0;
};

/// Y-Buckets wie im Skript: neue Buckets vorn, neue Rechtecke vorn im Bucket
struct Buckets {
    QVector<QPair<qint64, QVector<Rect>>> list;

    void add(const Rect& r)   // (oc-tbf-bucket-add)
    {
        const qint64 key = yKey(r.cy);
        for (auto& b : list) {
            if (b.first == key) {
                b.second.prepend(r);
                return;
            }
        }
        list.prepend(qMakePair(key, QVector<Rect>() << r));
    }

    bool find(double x, double y, double tol, Rect& found) const   // (oc-tbf-find-rect)
    {
        const qint64 keys[3] = { yKey(y), yKey(y - tol), yKey(y + tol) };
        for (const qint64 key : keys) {
            for (const auto& b : list) {
                if (b.first != key) continue;
                for (const Rect& r : b.second) {
                    if (std::fabs(x - r.cx) < tol && std::fabs(y - r.cy) < tol) {
                        found = r;
                        return true;
                    }
                }
                break;   // (assoc) liefert nur den ersten Bucket mit diesem Schluessel
            }
        }
        return false;
    }

    bool isEmpty() const { return list.isEmpty(); }
};

/// (oc-tbf-rect-data): geschlossene Polylinie mit vier Punkten
bool rectFromPolyline(AcDbPolyline* pLine, Rect& rect)
{
    if (pLine->numVerts() != 4) return false;
    double x0 = 0, x1 = 0, y0 = 0, y1 = 0;
    for (unsigned int i = 0; i < 4; ++i) {
        AcGePoint2d pt;
        if (pLine->getPointAt(i, pt) != Acad::eOk) return false;
        if (i == 0) {
            x0 = x1 = pt.x;
            y0 = y1 = pt.y;
        } else {
            x0 = std::min(x0, pt.x); x1 = std::max(x1, pt.x);
            y0 = std::min(y0, pt.y); y1 = std::max(y1, pt.y);
        }
    }
    if (!((x1 - x0) > 1e-6 && (y1 - y0) > 1e-6)) return false;
    rect.cx = (x0 + x1) / 2.0;
    rect.cy = (y0 + y1) / 2.0;
    rect.w = x1 - x0;
    rect.h = y1 - y0;
    return true;
}

/// (oc-tbf-find-enclosing): kleinstes Rechteck, das den Punkt umschliesst
bool findEnclosing(const QVector<Rect>& rects, double x, double y, Rect& best)
{
    bool have = false;
    for (const Rect& r : rects) {
        if (x >= r.cx - 0.5 * r.w && x <= r.cx + 0.5 * r.w
            && y >= r.cy - 0.5 * r.h && y <= r.cy + 0.5 * r.h) {
            if (!have || (r.w * r.h) < (best.w * best.h)) {
                best = r;
                have = true;
            }
        }
    }
    return have;
}

const double kTbfTol = 0.01;    // Toleranz "mittig im Rechteck"
const double kTbfFill = 0.95;   // Zielbreite = 95 % der Zellbreite

} // namespace

namespace OcEngine {

QString lispUpper(const QString& s)
{
    QString out = s;
    for (int i = 0; i < out.size(); ++i) {
        out[i] = out.at(i).toUpper();
    }
    return out;
}

QString lispTrim(const QString& s, const QString& chars)
{
    int i = 0;
    int j = s.size() - 1;
    while (i <= j && chars.contains(s.at(i))) ++i;
    while (j >= i && chars.contains(s.at(j))) --j;
    return (i > j) ? QString() : s.mid(i, j - i + 1);
}

int lispAtoi(const QString& s)
{
    int i = 0;
    const int n = s.size();
    while (i < n && s.at(i).isSpace()) ++i;
    bool negative = false;
    if (i < n && (s.at(i) == QLatin1Char('+') || s.at(i) == QLatin1Char('-'))) {
        negative = (s.at(i) == QLatin1Char('-'));
        ++i;
    }
    qint64 value = 0;
    while (i < n) {
        const ushort u = s.at(i).unicode();
        if (u < '0' || u > '9') break;
        value = value * 10 + (u - '0');
        if (value > 2147483647LL) { value = 2147483647LL; break; }
        ++i;
    }
    return static_cast<int>(negative ? -value : value);
}

bool wildMatch(const QString& pattern, const QString& text)
{
    // Rueckverfolgung nur fuer '*'
    int p = 0, t = 0, starP = -1, starT = 0;
    const int pn = pattern.size(), tn = text.size();
    auto matchesOne = [](QChar pc, QChar tc) {
        switch (pc.unicode()) {
        case '?': return true;
        case '#': return tc.isDigit();
        case '@': return tc.isLetter();
        case '.': return !tc.isLetterOrNumber();
        default:  return pc == tc;
        }
    };
    while (t < tn) {
        if (p < pn && pattern.at(p) == QLatin1Char('*')) {
            starP = p++;
            starT = t;
        } else if (p < pn && matchesOne(pattern.at(p), text.at(t))) {
            ++p;
            ++t;
        } else if (starP >= 0) {
            p = starP + 1;
            t = ++starT;
        } else {
            return false;
        }
    }
    while (p < pn && pattern.at(p) == QLatin1Char('*')) ++p;
    return p == pn;
}

bool isActiveValue(const QString& value)
{
    if (value.isEmpty()) return false;
    const QString v = lispUpper(lispTrim(value, QStringLiteral(" \t")));
    return v == QLatin1String("JA") || v == QLatin1String("TRUE") || v == QLatin1String("1")
        || v == QLatin1String("X") || v == QLatin1String("HIGH") || v == QLatin1String("AKTIV")
        || v == QLatin1String("WAHR");
}

QStringList readLines(const QString& path, bool* ok)
{
    QStringList lines;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        if (ok) *ok = false;
        return lines;
    }
    const QString text = decodeText(file.readAll());
    file.close();
    if (ok) *ok = true;

    lines = text.split(QLatin1Char('\n'));
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
    }
    return lines;
}

QByteArray encodeLikeLisp(const QString& text)
{
    QByteArray out;
    out.reserve(text.size());
    const QList<uint> codePoints = text.toUcs4();
    for (const uint cp : codePoints) {
        const int b = cp1252Byte(cp);
        if (b >= 0) {
            out.append(static_cast<char>(b));
        } else {
            out.append("\\U+");
            out.append(QByteArray::number(cp, 16).toUpper().rightJustified(4, '0'));
        }
    }
    return out;
}

bool writeLinesLikeLisp(const QString& path, const QStringList& lines)
{
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) return false;
    for (const QString& line : lines) {
        file.write(encodeLikeLisp(line));
        file.write(kNewline);
    }
    file.close();
    return true;
}

QStringList splitSemicolon(const QString& line)
{
    return line.split(QLatin1Char(';'));
}

QVector<QStringList> rowsFromLines(const QStringList& lines)
{
    QVector<QStringList> rows;
    for (const QString& raw : lines) {
        const QString line = lispTrim(raw, QStringLiteral(" \t\r"));
        if (line.isEmpty() || line.startsWith(QLatin1Char('#'))) continue;
        rows.append(splitSemicolon(line));
    }
    return rows;
}

OcPairs plankopfFromLines(const QStringList& lines)
{
    // Das Skript baut die Liste mit (cons): der letzte Eintrag steht vorn
    OcPairs result;
    for (const QString& line : lines) {
        if (line.size() > 10 && line.startsWith(QLatin1String("#PLANKOPF;"))) {
            const QStringList parts = splitSemicolon(line.mid(10));
            for (const QString& part : parts) {
                const int eq = part.indexOf(QLatin1Char('='));
                if (eq >= 0) {
                    result.prepend(qMakePair(part.left(eq), part.mid(eq + 1)));
                }
            }
        }
    }
    return result;
}

QVector<QStringList> readReferenceCsv(const QString& path, bool* ok)
{
    QVector<QStringList> rows;
    bool readOk = false;
    const QStringList lines = readLines(path, &readOk);
    if (ok) *ok = readOk;
    if (!readOk) return rows;

    // Zeile 1 ist der Kopf
    for (int i = 1; i < lines.size(); ++i) {
        const QString line = lispTrim(lines.at(i), QStringLiteral(" \t\r"));
        if (line.isEmpty()) continue;

        // (oc-fl-parse-comma-csv-line)
        QStringList fields;
        QString current;
        bool inQuotes = false;
        for (const QChar ch : line) {
            if (ch == QLatin1Char('"')) {
                inQuotes = !inQuotes;
            } else if (ch == QLatin1Char(',') && !inQuotes) {
                fields << lispTrim(current, QStringLiteral(" "));
                current.clear();
            } else {
                current.append(ch);
            }
        }
        fields << lispTrim(current, QStringLiteral(" "));
        rows.append(fields);
    }
    return rows;
}

QVector<OcBasSegment> parseBasCsv(const QString& path, bool* ok)
{
    QVector<OcBasSegment> segments;
    bool readOk = false;
    const QStringList lines = readLines(path, &readOk);
    if (ok) *ok = readOk;
    if (!readOk) return segments;

    auto isQuoted = [](const QString& s, QChar q) {
        return s.startsWith(q) && s.endsWith(q);
    };

    for (const QString& raw : lines) {
        QString trimmed = lispTrim(raw, QStringLiteral(" \t\r"));
        if (trimmed.startsWith(QLatin1Char('#'))) trimmed.clear();
        const int semicolon = trimmed.indexOf(QLatin1Char(';'));
        if (semicolon >= 0) {
            trimmed = lispTrim(trimmed.left(semicolon), QStringLiteral(" \t\r"));
        }
        if (trimmed.isEmpty()) continue;

        OcBasSegment seg;
        if (isQuoted(trimmed, QLatin1Char('"')) || isQuoted(trimmed, QLatin1Char('\''))) {
            // (oc-strip-quotes): alle Ebenen, auch """Text"""
            QString s = trimmed;
            while (s.size() >= 2
                   && (isQuoted(s, QLatin1Char('"')) || isQuoted(s, QLatin1Char('\'')))) {
                s = s.mid(1, s.size() - 2);
            }
            seg.type = OcBasSegment::Static;
            seg.value = s;
        } else if (trimmed == QLatin1String("-")) {
            seg.type = OcBasSegment::Static;
            seg.value = QStringLiteral("-");
        } else if (trimmed.size() >= 3
                   && lispUpper(trimmed.right(3)) == QLatin1String("_DP")) {
            seg.type = OcBasSegment::AttrDp;
            seg.value = trimmed;
        } else {
            seg.type = OcBasSegment::Attr;
            seg.value = trimmed;
        }
        segments.append(seg);
    }
    return segments;
}

QString basLayoutText(const QVector<OcBasSegment>& segments)
{
    QStringList parts;
    for (const OcBasSegment& seg : segments) {
        switch (seg.type) {
        case OcBasSegment::Static:
            parts << QLatin1Char('"') + seg.value + QLatin1Char('"');
            break;
        case OcBasSegment::AttrDp:
            parts << seg.value + QStringLiteral("_n");
            break;
        default:
            parts << seg.value;
            break;
        }
    }
    return parts.join(QStringLiteral(" + "));
}

bool writeBasCsv(const QString& path, const QVector<OcBasSegment>& segments, QString* error)
{
    QString text;
    for (const OcBasSegment& seg : segments) {
        if (seg.type == OcBasSegment::Static && seg.value != QLatin1String("-")) {
            text += QStringLiteral("\"\"\"") + seg.value + QStringLiteral("\"\"\"\n");
        } else {
            text += seg.value + QLatin1Char('\n');
        }
    }

    if (QFileInfo::exists(path)) {
        const QString bak = path + QStringLiteral(".bak");
        QFile::remove(bak);
        if (!QFile::copy(path, bak)) {
            if (error) *error = QStringLiteral("Sicherung nicht moeglich: ") + bak;
            return false;
        }
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (error) *error = f.errorString();
        return false;
    }
    const QByteArray bytes = text.toUtf8();
    if (f.write(bytes) != bytes.size()) {
        if (error) *error = f.errorString();
        return false;
    }
    f.close();
    return true;
}

} // namespace OcEngine

// ============================================================================
// Sicherungskopien
// ============================================================================

namespace {

/// Zeichnungen, die in diesem Lauf schon gespeichert wurden
QSet<QString>& savedInRun()
{
    static QSet<QString> paths;
    return paths;
}

QString runKey(const QString& path)
{
    const QString clean = QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef _WIN32
    return clean.toLower();
#else
    return clean;
#endif
}

/// Bisherigen Stand als <Name>.bak neben die Zeichnung legen
bool writeBackup(const QString& path)
{
    const QFileInfo info(path);
    if (!info.exists()) return true;

    const QString bak = info.absolutePath() + QLatin1Char('/')
                        + info.completeBaseName() + QStringLiteral(".bak");
    if (QFile::exists(bak)) {
        QFile::setPermissions(bak, QFile::permissions(bak)
                                   | QFile::WriteOwner | QFile::WriteUser);
        if (!QFile::remove(bak)) return false;
    }
    if (!QFile::copy(path, bak)) return false;
    QFile::setPermissions(bak, QFile::permissions(bak)
                               | QFile::WriteOwner | QFile::WriteUser);
    return true;
}

} // namespace

namespace OcEngine {

void beginBackupRun()
{
    savedInRun().clear();
}

} // namespace OcEngine

// ============================================================================
// OcDrawing: Aufbau
// ============================================================================

struct OcDrawing::Impl {
    struct Attr {
        AcDbObjectId id;
        QString tag;      ///< wie in der Zeichnung
        QString upper;    ///< in Grossschreibung
        QString text;
    };
    struct Ins {
        AcDbObjectId id;
        AcDbObjectId recordId;
        QString name;         ///< Name des Blocks, bei anonymen Bloecken *U...
        QString upper;
        QString effUpper;     ///< Name wie im Eigenschaftenfenster, bei Bedarf ermittelt
        bool effKnown = false;
        double x = 0, y = 0;
        double sx = 1.0;
        QVector<Attr> attrs;  ///< in der Reihenfolge der ATTRIB-Kette
    };

    AcDbDatabase* db = nullptr;
    QString path;
    QString error;
    bool readOnly = false;
    QVector<Ins> inserts;     ///< in der Reihenfolge von (ssget "X")

    ~Impl() { release(); }

    void release()
    {
        inserts.clear();
        if (db) {
            delete db;
            db = nullptr;
        }
    }

    /// Blockdefinitionen der Layouts, geordnet nach ihrem Handle
    QVector<AcDbObjectId> layoutRecords() const
    {
        QVector<QPair<quint64, AcDbObjectId>> found;
        AcDbBlockTable* pTable = nullptr;
        if (db->getBlockTable(pTable, AcDb::kForRead) != Acad::eOk || !pTable) return {};
        AcDbBlockTableIterator* pIter = nullptr;
        pTable->newIterator(pIter);
        for (; pIter && !pIter->done(); pIter->step()) {
            AcDbBlockTableRecord* pRecord = nullptr;
            if (pIter->getRecord(pRecord, AcDb::kForRead) != Acad::eOk || !pRecord) continue;
            if (pRecord->isLayout()) {
                AcDbHandle handle;
                pRecord->getAcDbHandle(handle);
                const quint64 value = (static_cast<quint64>(handle.high()) << 32) | handle.low();
                found.append(qMakePair(value, pRecord->objectId()));
            }
            pRecord->close();
        }
        delete pIter;
        pTable->close();

        std::stable_sort(found.begin(), found.end(),
                         [](const QPair<quint64, AcDbObjectId>& a,
                            const QPair<quint64, AcDbObjectId>& b) { return a.first < b.first; });
        QVector<AcDbObjectId> ids;
        for (const auto& f : found) ids.append(f.second);
        return ids;
    }

    /// Objekte aller Layouts in der Reihenfolge von (ssget "X"): die
    /// Datenbankreihenfolge rueckwaerts
    template <typename Visitor>
    void collectEntities(Visitor visit) const
    {
        QVector<AcDbObjectId> ids;
        for (const AcDbObjectId& recordId : layoutRecords()) {
            AcDbBlockTableRecord* pRecord = nullptr;
            if (acdbOpenObject(pRecord, recordId, AcDb::kForRead) != Acad::eOk || !pRecord) continue;
            AcDbBlockTableRecordIterator* pIter = nullptr;
            pRecord->newIterator(pIter);
            for (; pIter && !pIter->done(); pIter->step()) {
                AcDbObjectId id;
                if (pIter->getEntityId(id) == Acad::eOk) ids.append(id);
            }
            delete pIter;
            pRecord->close();
        }
        for (int i = ids.size() - 1; i >= 0; --i) visit(ids.at(i));
    }

    static QString recordName(const AcDbObjectId& recordId)
    {
        QString name;
        AcDbBlockTableRecord* pRecord = nullptr;
        if (acdbOpenObject(pRecord, recordId, AcDb::kForRead) == Acad::eOk && pRecord) {
            const ACHAR* raw = nullptr;
            pRecord->getName(raw);
            name = fromAchar(raw);
            pRecord->close();
        }
        return name;
    }

    void loadInserts()
    {
        inserts.clear();
        collectEntities([this](const AcDbObjectId& id) {
            AcDbEntity* pEnt = nullptr;
            if (acdbOpenObject(pEnt, id, AcDb::kForRead) != Acad::eOk || !pEnt) return;
            // Nur was im Editor als INSERT gilt - Tabellen und andere
            // Ableitungen der Blockreferenz gehoeren nicht dazu
            if (pEnt->isA() == AcDbBlockReference::desc()
                || pEnt->isA() == AcDbMInsertBlock::desc()) {
                AcDbBlockReference* pRef = AcDbBlockReference::cast(pEnt);
                Ins ins;
                ins.id = id;
                ins.recordId = pRef->blockTableRecord();
                ins.name = recordName(ins.recordId);
                ins.upper = OcEngine::lispUpper(ins.name);
                ins.x = pRef->position().x;
                ins.y = pRef->position().y;
                ins.sx = pRef->scaleFactors().sx;

                AcDbObjectIterator* pIter = pRef->attributeIterator();
                for (; pIter && !pIter->done(); pIter->step()) {
                    AcDbAttribute* pAttr = nullptr;
                    if (acdbOpenObject(pAttr, pIter->objectId(), AcDb::kForRead) != Acad::eOk
                        || !pAttr) {
                        continue;
                    }
                    Attr a;
                    a.id = pIter->objectId();
                    a.tag = fromAchar(pAttr->tagConst());
                    a.upper = OcEngine::lispUpper(a.tag);
                    a.text = fromAchar(pAttr->textStringConst());
                    ins.attrs.append(a);
                    pAttr->close();
                }
                delete pIter;
                inserts.append(ins);
            }
            pEnt->close();
        });
    }

    /// Name wie im Eigenschaftenfenster ("EffectiveName"), in Grossschreibung
    const QString& effectiveUpper(Ins& ins)
    {
        if (!ins.effKnown) {
            ins.effKnown = true;
            ins.effUpper = ins.upper;
            if (ins.name.startsWith(QLatin1Char('*'))) {
                AcValue value;
                if (BrxDbProperties::getValue(ins.id, L"EffectiveName~Native", value)) {
                    AcString text;
                    if (value.get(text) && !text.isEmpty()) {
                        ins.effUpper = OcEngine::lispUpper(QString::fromWCharArray(text.kwszPtr()));
                    }
                }
            }
        }
        return ins.effUpper;
    }

    /// Attributwert setzen. Im Editor rechnet BricsCAD die Lage zentrierter
    /// und rechtsbuendiger Texte selbst nach, hier geschieht es ausdruecklich.
    bool writeAttr(Attr& attr, const QString& value)
    {
        AcDbAttribute* pAttr = nullptr;
        if (acdbOpenObject(pAttr, attr.id, AcDb::kForWrite) != Acad::eOk || !pAttr) return false;
        pAttr->setTextString(value.toStdWString().c_str());
        if (pAttr->isMTextAttribute()) pAttr->updateMTextAttribute();
        adjustTextAlignment(pAttr, db);
        pAttr->close();
        attr.text = value;
        return true;
    }

    /// Alle Attribute eines Blocks mit diesem Tag beschreiben
    int writeAll(Ins& ins, const QString& upperTag, const QString& value)
    {
        int count = 0;
        for (Attr& a : ins.attrs) {
            if (a.upper == upperTag && writeAttr(a, value)) ++count;
        }
        return count;
    }

    /// Wert des ersten Attributs mit diesem Tag
    static bool readFirst(const Ins& ins, const QString& upperTag, QString& value)
    {
        for (const Attr& a : ins.attrs) {
            if (a.upper == upperTag) {
                value = a.text;
                return true;
            }
        }
        return false;
    }

    /// Wert des letzten Attributs mit diesem Tag
    static bool readLast(const Ins& ins, const QString& upperTag, QString& value)
    {
        bool found = false;
        for (const Attr& a : ins.attrs) {
            if (a.upper == upperTag) {
                value = a.text;
                found = true;
            }
        }
        return found;
    }

    static QString lastOrEmpty(const Ins& ins, const QString& upperTag)
    {
        QString value;
        return readLast(ins, upperTag, value) ? value : QString();
    }

    /// (oc-find-attr-in-drawing): letzter nicht-leerer Treffer
    bool findAttrInDrawing(const QString& upperTag, QString& result) const
    {
        bool found = false;
        for (const Ins& ins : inserts) {
            for (int i = ins.attrs.size() - 1; i >= 0; --i) {
                const Attr& a = ins.attrs.at(i);
                if (a.upper == upperTag) {
                    const QString value = OcEngine::lispTrim(a.text, QStringLiteral(" \t"));
                    if (!value.isEmpty()) {
                        result = value;
                        found = true;
                    }
                }
            }
        }
        return found;
    }

    /// Breitenfaktor eines Textes herabsetzen, wenn er ueber die verfuegbare
    /// Breite hinausragt (oc-tbf-apply)
    template <typename TextType>
    bool applyWidth(const AcDbObjectId& id, double avail)
    {
        TextType* pText = nullptr;
        if (acdbOpenObject(pText, id, AcDb::kForWrite) != Acad::eOk || !pText) return false;

        bool changed = false;
        AcDbExtents extents;
        if (avail > 1e-6 && pText->getGeomExtents(extents) == Acad::eOk) {
            const double width = extents.maxPoint().x - extents.minPoint().x;
            const double height = extents.maxPoint().y - extents.minPoint().y;
            // Bei um 90 Grad gedrehtem Text ist die Textbreite die Y-Ausdehnung
            const double actual =
                (std::fabs(std::sin(pText->rotation())) > 0.5) ? height : width;
            if (actual > avail * kTbfFill) {
                const double factor = pText->widthFactor() * ((avail * kTbfFill) / actual);
                pText->setWidthFactor(factor);
                adjustTextAlignment(pText, db);
                changed = true;
            }
        }
        pText->close();
        return changed;
    }
};

OcDrawing::OcDrawing()
    : d(new Impl)
{
}

OcDrawing::~OcDrawing() = default;

bool OcDrawing::open(const QString& path, bool readOnly)
{
    close();
    d->path = path;
    d->error.clear();
    d->readOnly = readOnly;

    d->db = new AcDbDatabase(false, true);
    const std::wstring wpath = path.toStdWString();
    const Acad::ErrorStatus es = readOnly
        ? d->db->readDwgFile(wpath.c_str(), AcDbDatabase::kForReadAndAllShare, false)
        : d->db->readDwgFile(wpath.c_str());
    if (es != Acad::eOk) {
        d->error = QStringLiteral("Zeichnung nicht lesbar (Fehler %1)").arg(static_cast<int>(es));
        d->release();
        return false;
    }
    // Datei vollstaendig einlesen und freigeben, sonst laesst sie sich nicht
    // unter demselben Namen speichern
    d->db->closeInput(true);
    d->db->setRetainOriginalThumbnailBitmap(true);

    d->loadInserts();
    return true;
}

bool OcDrawing::save(bool backup)
{
    if (!d->db) {
        d->error = QStringLiteral("Keine Zeichnung geoeffnet");
        return false;
    }
    if (d->readOnly) {
        d->error = QStringLiteral("Zeichnung ist nur zum Lesen geoeffnet");
        return false;
    }

    // Die Sicherung zeigt den Stand vor dem Lauf: nur das erste Speichern
    // einer Zeichnung legt sie an
    const QString key = runKey(d->path);
    if (!savedInRun().contains(key)) {
        if (backup && !writeBackup(d->path)) {
            d->error = QStringLiteral("Sicherungskopie (.bak) liess sich nicht anlegen - "
                                      "Zeichnung nicht gespeichert");
            return false;
        }
        savedInRun().insert(key);
    }

    const std::wstring wpath = d->path.toStdWString();
    const Acad::ErrorStatus es = d->db->saveAs(wpath.c_str());
    if (es != Acad::eOk) {
        d->error = QStringLiteral("Speichern schlug fehl (Fehler %1)").arg(static_cast<int>(es));
        return false;
    }
    return true;
}

void OcDrawing::close()
{
    d->release();
}

bool OcDrawing::isOpen() const
{
    return d->db != nullptr;
}

QString OcDrawing::path() const
{
    return d->path;
}

QString OcDrawing::lastError() const
{
    return d->error;
}

int OcDrawing::insertCount() const
{
    return d->inserts.size();
}

// ============================================================================
// Attribute
// ============================================================================

int OcDrawing::setAttributes(const OcPairs& upperTagValues, const QString& blockPattern,
                             bool effectiveName)
{
    if (!d->db || upperTagValues.isEmpty()) return 0;

    const QString pattern = OcEngine::lispUpper(blockPattern);
    int count = 0;
    for (Impl::Ins& ins : d->inserts) {
        if (ins.attrs.isEmpty()) continue;
        if (!pattern.isEmpty()) {
            const QString& name = effectiveName ? d->effectiveUpper(ins) : ins.upper;
            if (!OcEngine::wildMatch(pattern, name)) continue;
        }
        for (Impl::Attr& a : ins.attrs) {
            for (const auto& pair : upperTagValues) {
                if (pair.first == a.upper) {
                    if (d->writeAttr(a, pair.second)) ++count;
                    break;
                }
            }
        }
    }
    return count;
}

QString OcDrawing::attributeValue(const QString& upperTag, const QString& blockPattern,
                                  bool* found) const
{
    if (found) *found = false;
    if (!d->db) return QString();

    const QString pattern = OcEngine::lispUpper(blockPattern);
    for (const Impl::Ins& ins : d->inserts) {
        if (!pattern.isEmpty() && !OcEngine::wildMatch(pattern, ins.upper)) continue;
        QString value;
        if (Impl::readFirst(ins, upperTag, value)) {
            if (found) *found = true;
            return value;
        }
    }
    return QString();
}

// ============================================================================
// Deckblatt und Layer
// ============================================================================

void OcDrawing::thawLayer(const QString& name)
{
    if (!d->db) return;
    AcDbLayerTable* pTable = nullptr;
    if (d->db->getLayerTable(pTable, AcDb::kForRead) != Acad::eOk || !pTable) return;
    AcDbLayerTableRecord* pLayer = nullptr;
    if (pTable->getAt(name.toStdWString().c_str(), pLayer, AcDb::kForWrite) == Acad::eOk
        && pLayer) {
        pLayer->setIsFrozen(false);
        pLayer->setIsOff(false);
        pLayer->close();
    }
    pTable->close();
}

void OcDrawing::freezeLayers(const QStringList& names)
{
    if (!d->db) return;
    AcDbLayerTable* pTable = nullptr;
    if (d->db->getLayerTable(pTable, AcDb::kForRead) != Acad::eOk || !pTable) return;

    const AcDbObjectId current = d->db->clayer();
    for (const QString& name : names) {
        AcDbObjectId layerId;
        if (pTable->getAt(name.toStdWString().c_str(), layerId) != Acad::eOk) continue;
        if (layerId == current) {
            // Im Editor scheitert das Einfrieren des aktuellen Layers mit
            // einem Fehler, der den Rest der Liste abbricht
            break;
        }
        AcDbLayerTableRecord* pLayer = nullptr;
        if (acdbOpenObject(pLayer, layerId, AcDb::kForWrite) == Acad::eOk && pLayer) {
            pLayer->setIsFrozen(true);
            pLayer->close();
        }
    }
    pTable->close();
}

int OcDrawing::replaceText(const QString& upperSearch, const QString& replacement)
{
    if (!d->db) return 0;

    int count = 0;
    const std::wstring value = replacement.toStdWString();
    AcDbDatabase* db = d->db;
    d->collectEntities([&](const AcDbObjectId& id) {
        AcDbEntity* pEnt = nullptr;
        if (acdbOpenObject(pEnt, id, AcDb::kForRead) != Acad::eOk || !pEnt) return;

        if (pEnt->isA() == AcDbText::desc()) {
            AcDbText* pText = AcDbText::cast(pEnt);
            if (OcEngine::lispUpper(fromAchar(pText->textStringConst())) == upperSearch
                && pText->upgradeOpen() == Acad::eOk) {
                pText->setTextString(value.c_str());
                adjustTextAlignment(pText, db);
                ++count;
            }
        } else if (pEnt->isA() == AcDbMText::desc()) {
            AcDbMText* pMText = AcDbMText::cast(pEnt);
            AcString contents;
            if (pMText->contents(contents) == Acad::eOk
                && OcEngine::lispUpper(QString::fromWCharArray(contents.kwszPtr())) == upperSearch
                && pMText->upgradeOpen() == Acad::eOk) {
                pMText->setContents(value.c_str());
                ++count;
            }
        }
        pEnt->close();
    });
    return count;
}

// ============================================================================
// BMK-Nummerierung (BmkNummerierung.lsp v2.3)
// ============================================================================

OcBmkResult OcDrawing::bmkNummerierung(QStringList* messages)
{
    OcBmkResult result;
    if (!d->db) return result;

    auto say = [messages](const QString& text) {
        if (messages) messages->append(text);
    };

    // (oc-get-blocks-with-bmk): ein Eintrag je Attribut OC_AKS. Das Skript
    // sammelt mit (cons), die Liste steht also in umgekehrter Besuchsfolge.
    struct Entry {
        int insert = 0;     ///< Index in d->inserts
        QString tag;        ///< Tag des Attributs wie in der Zeichnung
        QString text;       ///< Wert beim Einsammeln
        double x = 0, y = 0;
    };
    QVector<Entry> blocks;
    for (int i = 0; i < d->inserts.size(); ++i) {
        const Impl::Ins& ins = d->inserts.at(i);
        for (int k = ins.attrs.size() - 1; k >= 0; --k) {
            const Impl::Attr& a = ins.attrs.at(k);
            if (a.upper == QLatin1String("OC_AKS")) {
                Entry e;
                e.insert = i;
                e.tag = a.tag;
                e.text = a.text;
                e.x = ins.x;
                e.y = ins.y;
                blocks.prepend(e);
            }
        }
    }
    result.blocks = blocks.size();
    if (blocks.isEmpty()) {
        say(QStringLiteral("Keine Bloecke mit OC_AKS Attribut gefunden."));
        return result;
    }

    // (oc-sort-blocks-by-position): links nach rechts, in einer Spalte
    // (Abstand unter 10) von unten nach oben
    vlSort(blocks, [](const Entry& a, const Entry& b) {
        if (std::fabs(a.x - b.x) < 10.0) return a.y < b.y;
        return a.x < b.x;
    });

    // (oc-get-bmk-mode)
    QString mode;
    if (d->findAttrInDrawing(QStringLiteral("BMK_NUMMERIERUNG"), mode)) {
        result.modeSource = QStringLiteral("BMK_NUMMERIERUNG");
    } else if (d->findAttrInDrawing(QStringLiteral("FREITEXT_05"), mode)) {
        result.modeSource = QStringLiteral("FREITEXT_05");
    } else {
        mode = QStringLiteral("NEUSTARTEN");
        result.modeSource = QStringLiteral("Default");
    }
    mode = OcEngine::lispUpper(mode);
    result.mode = mode;

    // (oc-load-counters): Zaehler liegen neben der Zeichnung
    const QString counterFile =
        QFileInfo(d->path).absolutePath() + QStringLiteral("/bmk_counters.tmp");
    QVector<QPair<QString, int>> counters;
    if (QFileInfo::exists(counterFile) && mode == QLatin1String("FORTSETZEN")) {
        bool ok = false;
        const QStringList lines = OcEngine::readLines(counterFile, &ok);
        for (const QString& line : lines) {
            if (!line.contains(QLatin1Char(':'))) continue;
            const QStringList parts = splitNonEmpty(line, QLatin1Char(':'));
            if (parts.size() == 2) {
                counters.prepend(qMakePair(parts.at(0), OcEngine::lispAtoi(parts.at(1))));
            }
        }
    } else if (QFileInfo::exists(counterFile)) {
        QFile::remove(counterFile);
    }

    // (oc-process-bmk-block)
    for (const Entry& e : blocks) {
        if (e.text.isEmpty()) continue;

        Impl::Ins& ins = d->inserts[e.insert];
        if (isLockValue(Impl::lastOrEmpty(ins, QStringLiteral("OC_AKS_LOCK")))) {
            ++result.locked;
            say(QStringLiteral("%1 -> [LOCK, uebersprungen]").arg(e.text));
            continue;
        }

        const QString clean = removeTrailingDigits(e.text);
        if (clean.isEmpty()) continue;

        // (oc-get-or-increment)
        int counter = 1;
        for (const auto& c : counters) {
            if (c.first == clean) {
                counter = c.second + 1;
                break;
            }
        }
        // (oc-update-counter)
        QVector<QPair<QString, int>> updated;
        bool found = false;
        for (const auto& c : counters) {
            if (c.first == clean) {
                updated.prepend(qMakePair(clean, counter));
                found = true;
            } else {
                updated.prepend(c);
            }
        }
        if (!found) updated.prepend(qMakePair(clean, counter));
        counters = updated;

        const QString newText = clean + twoDigits(counter);
        d->writeAll(ins, OcEngine::lispUpper(e.tag), newText);
        ++result.numbered;
        say(QStringLiteral("%1 -> %2").arg(e.text, newText));
    }

    // (oc-save-counters)
    QStringList counterLines;
    for (const auto& c : counters) {
        counterLines << c.first + QLatin1Char(':') + QString::number(c.second);
    }
    OcEngine::writeLinesLikeLisp(counterFile, counterLines);

    // (oc-sync-aks-einfueg): OC_AKS in alle OC_AKS_EINFUEG_n desselben Blocks
    for (Impl::Ins& ins : d->inserts) {
        const QString aks = Impl::lastOrEmpty(ins, QStringLiteral("OC_AKS"));
        if (aks.isEmpty()) continue;
        for (Impl::Attr& a : ins.attrs) {
            if (a.upper.size() >= 15 && a.upper.startsWith(QLatin1String("OC_AKS_EINFUEG_"))) {
                d->writeAttr(a, aks);
            }
        }
    }

    return result;
}

// ============================================================================
// BAS-Generierung (GenBas.lsp v1.3)
// ============================================================================

int OcDrawing::genBas(const QVector<OcBasSegment>& segments, QStringList* messages)
{
    if (!d->db || segments.isEmpty()) return 0;

    // (oc-find-plankopf): erster Block, dessen Name mit OC_RSH_PLANKOPF_QUER beginnt
    int plankopf = -1;
    for (int i = 0; i < d->inserts.size(); ++i) {
        if (d->inserts.at(i).upper.startsWith(QLatin1String("OC_RSH_PLANKOPF_QUER"))) {
            plankopf = i;
            break;
        }
    }

    // (oc-read-attribute): erster Treffer, sonst leer
    auto readAttr = [this](int insert, const QString& name) {
        QString value;
        Impl::readFirst(d->inserts.at(insert), OcEngine::lispUpper(name), value);
        return value;
    };

    int total = 0;
    for (int i = 0; i < d->inserts.size(); ++i) {
        if (d->inserts.at(i).attrs.isEmpty()) continue;

        for (int dp = 1; dp <= 25; ++dp) {
            const QString suffix = QString::number(dp);
            if (!OcEngine::isActiveValue(readAttr(i, QStringLiteral("OC_FL_AKTIV_") + suffix))) {
                continue;
            }

            // (oc-build-bas-string): erst im Block suchen, dann im Plankopf
            QString bas;
            for (const OcBasSegment& seg : segments) {
                if (seg.type == OcBasSegment::Static) {
                    bas += seg.value;
                    continue;
                }
                const QString name = (seg.type == OcBasSegment::AttrDp)
                    ? seg.value + QLatin1Char('_') + suffix
                    : seg.value;
                QString value = readAttr(i, name);
                if (value.isEmpty() && plankopf >= 0) value = readAttr(plankopf, name);
                bas += value;
            }

            if (d->writeAll(d->inserts[i], QStringLiteral("OC_BAS_DP_") + suffix, bas) > 0) {
                ++total;
                if (messages) {
                    messages->append(QStringLiteral("OC_BAS_DP_%1 = %2").arg(suffix, bas));
                }
            }
        }
    }
    return total;
}

// ============================================================================
// Datenpunkt-Extraktion (ExtractDP.lsp v1.7)
// ============================================================================

OcExtractResult OcDrawing::extractDp(const QString& extractDir, OcLogFile* log)
{
    OcExtractResult result;
    if (!d->db) {
        result.error = QStringLiteral("Keine Zeichnung geoeffnet");
        return result;
    }

    auto say = [log](const QString& text) {
        if (log) log->write(text);
    };

    const QString dwgName = QFileInfo(d->path).completeBaseName();
    const QString csvPath = extractDir + QLatin1Char('/') + dwgName + QStringLiteral(".csv");

    say(QStringLiteral("\n=== Datenpunkt-Extraktion gestartet ==="));
    say(QStringLiteral("\n  Zeichnung: ") + dwgName);
    say(QStringLiteral("\n  Ausgabe: ") + csvPath);

    // ---- Kopfzeile ----
    QString header = QStringLiteral("AKS;BEZEICHNUNG;AKS2;REF_DP;FCODE_DP;BAS_DP;INTEG_DP;PRODUKT");
    for (const QString& col : extractColumns()) header += QLatin1Char(';') + col;
    header += QStringLiteral(";KOMMENTAR");
    result.lines << header;

    // ---- Plankopf (oc-dp-extract-plankopf): je Attribut der erste Treffer ----
    OcPairs plankopf;
    for (const Impl::Ins& ins : d->inserts) {
        for (const Impl::Attr& a : ins.attrs) {
            for (const QString& pa : plankopfAttributes()) {
                if (a.upper != pa) continue;
                bool known = false;
                for (const auto& p : plankopf) {
                    if (p.first == pa) { known = true; break; }
                }
                if (!known) plankopf.prepend(qMakePair(pa, a.text));
            }
        }
    }
    QString plankopfLine = QStringLiteral("#PLANKOPF");
    for (const auto& p : plankopf) {
        plankopfLine += QLatin1Char(';') + p.first + QLatin1Char('=') + escapeCsv(p.second);
    }
    result.lines << plankopfLine;

    // ---- Durchgang 1: Bloecke mit Koordinaten sammeln ----
    struct Collected {
        double x = 0, y = 0;
        int insert = 0;
    };
    QVector<Collected> collected;   // wie block-collect: zuletzt gefundener zuerst

    say(QStringLiteral("\n  DEBUG: ssget fand %1 INSERT-Entities").arg(d->inserts.size()));
    for (int i = 0; i < d->inserts.size(); ++i) {
        const Impl::Ins& ins = d->inserts.at(i);
        say(QStringLiteral("\n  DEBUG [%1]: %2 | DXF66=%3")
            .arg(i).arg(ins.name, ins.attrs.isEmpty() ? QStringLiteral("nil")
                                                      : QStringLiteral("1")));
        if (ins.attrs.isEmpty()) {
            say(QStringLiteral(" -> KEIN ATTRIB-FLAG"));
            continue;
        }

        const QString aks = Impl::lastOrEmpty(ins, QStringLiteral("OC_AKS"));
        say(QStringLiteral(" | OC_AKS=\"") + aks + QLatin1Char('"'));
        bool hasActive = false;
        if (aks.isEmpty()) {
            for (const Impl::Attr& a : ins.attrs) {
                if (hasActive) break;
                if (a.upper.size() >= 12 && a.upper.startsWith(QLatin1String("OC_FL_AKTIV_"))) {
                    say(QStringLiteral(" | ") + a.upper + QLatin1Char('=') + a.text);
                    if (OcEngine::isActiveValue(a.text)) hasActive = true;
                }
            }
        }
        if (!aks.isEmpty() || hasActive) {
            say(QStringLiteral(" -> GESAMMELT"));
            Collected c;
            c.x = ins.x;
            c.y = ins.y;
            c.insert = i;
            collected.prepend(c);
        } else {
            say(QStringLiteral(" -> UEBERSPRUNGEN"));
        }
    }

    // ---- Hauptlinie: haeufigster Y-Wert, Toleranz 2 mm ----
    const double tol = 2.0;
    QVector<QPair<double, int>> bins;
    for (const Collected& c : collected) {
        const double rounded = std::trunc(c.y / tol + 0.5) * tol;
        bool found = false;
        for (auto& bin : bins) {
            if (std::fabs(bin.first - rounded) < tol) {
                ++bin.second;
                found = true;
                break;
            }
        }
        if (!found) bins.append(qMakePair(rounded, 1));
    }
    double mainY = 0.0;
    int maxCount = 0;
    for (const auto& bin : bins) {
        if (bin.second > maxCount) {
            mainY = bin.first;
            maxCount = bin.second;
        }
    }
    say(QStringLiteral("\n  Hauptlinie Y=%1 (%2 von %3 Bloecken)")
        .arg(QString::number(mainY, 'f', 1)).arg(maxCount).arg(collected.size()));

    // ---- Aufteilen: Hauptlinie (6 mm) und Ausreisser, je nach X geordnet ----
    QVector<Collected> onLine, offLine;
    for (const Collected& c : collected) {
        if (std::fabs(c.y - mainY) < tol * 3) onLine.prepend(c);
        else                                  offLine.prepend(c);
    }
    auto byX = [](const Collected& a, const Collected& b) { return a.x < b.x; };
    vlSort(onLine, byX);
    vlSort(offLine, byX);

    QVector<Collected> ordered = offLine;
    ordered += onLine;
    say(QStringLiteral("\n  %1 Bloecke sortiert (%2 vor Hauptlinie, %3 auf Hauptlinie)")
        .arg(ordered.size()).arg(offLine.size()).arg(onLine.size()));

    // ---- Durchgang 2: Datenpunkte der geordneten Bloecke ----
    for (const Collected& c : ordered) {
        const Impl::Ins& ins = d->inserts.at(c.insert);
        const QString aks = Impl::lastOrEmpty(ins, QStringLiteral("OC_AKS"));
        const QString bez = Impl::lastOrEmpty(ins, QStringLiteral("OC_BEZEICHNUNG"));
        const QString produkt = Impl::lastOrEmpty(ins, QStringLiteral("OC_PRODUKT"));

        for (int dp = 1; dp <= 25; ++dp) {
            const QString n = QString::number(dp);
            if (!OcEngine::isActiveValue(Impl::lastOrEmpty(ins, QStringLiteral("OC_FL_AKTIV_") + n))) {
                continue;
            }
            const QString ref = Impl::lastOrEmpty(ins, QStringLiteral("OC_REF_DP_") + n);
            const QString fcode = Impl::lastOrEmpty(ins, QStringLiteral("OC_FCODE_DP_") + n);
            const QString bas = Impl::lastOrEmpty(ins, QStringLiteral("OC_BAS_DP_") + n);
            QString integ = Impl::lastOrEmpty(ins, QStringLiteral("OC_INTEGRATIONSART_DP_") + n);
            if (integ.isEmpty()) {
                integ = Impl::lastOrEmpty(ins, QStringLiteral("OC_INTEG_DP_") + n);
            }
            const QString kommentar = Impl::lastOrEmpty(ins, QStringLiteral("OC_KOMMENTAR_DP_") + n);

            QString line = escapeCsv(aks) + QLatin1Char(';')
                         + escapeCsv(bez) + QLatin1Char(';')
                         + escapeCsv(aks) + QLatin1Char(';')
                         + escapeCsv(ref) + QLatin1Char(';')
                         + escapeCsv(fcode) + QLatin1Char(';')
                         + escapeCsv(bas) + QLatin1Char(';')
                         + escapeCsv(integ) + QLatin1Char(';')
                         + escapeCsv(produkt);
            for (const QString& col : extractColumns()) {
                line += QLatin1Char(';')
                      + escapeCsv(Impl::lastOrEmpty(ins, col + QStringLiteral("_DP_") + n));
            }
            line += QLatin1Char(';') + escapeCsv(kommentar);

            result.lines << line;
            ++result.dpCount;
            say(QStringLiteral("\n  DP %1: %2 / %3 (%4)").arg(result.dpCount).arg(aks, ref, bas));
        }
    }

    if (!OcEngine::writeLinesLikeLisp(csvPath, result.lines)) {
        result.error = QStringLiteral("Kann CSV nicht schreiben: %1").arg(csvPath);
        say(QStringLiteral("\n  FEHLER: ") + result.error);
        return result;
    }

    say(QStringLiteral("\n=== Extraktion abgeschlossen: %1 Datenpunkte aus %2 ===")
        .arg(result.dpCount).arg(dwgName));
    result.ok = true;
    return result;
}

// ============================================================================
// GA-FL befuellen (FillGaFl.lsp v1.5)
// ============================================================================

OcFillResult OcDrawing::fillGaFl(const OcFillJob& job, const QVector<QStringList>& reference,
                                 OcFillState& state, OcLogFile* log)
{
    OcFillResult result;
    if (!d->db) {
        result.error = QStringLiteral("Keine Zeichnung geoeffnet");
        return result;
    }

    auto say = [log](const QString& text) {
        if (log) log->write(text);
    };

    say(QStringLiteral("\n=== GA-FL Befuellung gestartet ==="));

    const int sheetNum = job.sheetNum;
    const int startRow = job.startRow;
    const int dpCount = job.dpCount;
    const bool isFirstSheet = (sheetNum == 1);

    // Erstblatt: alte Uebertragsdaten verwerfen
    if (isFirstSheet) {
        state.hasCarry = false;
        state.carry.clear();
    }
    say(QStringLiteral("\n  Blatt %1, Start-DP: %2, Anzahl: %3%4")
        .arg(sheetNum).arg(startRow).arg(dpCount)
        .arg(isFirstSheet ? QStringLiteral(" (Erstblatt)") : QStringLiteral(" (Folgeblatt)")));

    // ---- GA-FL-Bloecke (oc-fl-find-gafl-blocks), mit (cons) gesammelt ----
    QVector<int> gaFl;
    for (int i = 0; i < d->inserts.size(); ++i) {
        if (d->inserts.at(i).upper == QLatin1String("VDI3814_GA_FL_V_1_0")) gaFl.prepend(i);
    }
    if (gaFl.isEmpty()) {
        for (int i = 0; i < d->inserts.size(); ++i) {
            if (OcEngine::wildMatch(QStringLiteral("*VDI3814*GA*FL*"), d->inserts.at(i).upper)) {
                gaFl.prepend(i);
            }
        }
    }
    if (gaFl.isEmpty()) {
        result.error = QStringLiteral("Block VDI3814_GA_FL_V_1_0 nicht gefunden");
        say(QStringLiteral("\n  FEHLER: Block VDI3814_GA_FL_V_1_0 nicht gefunden!"));
        return result;
    }
    result.gaFlBlocks = gaFl.size();
    say(QStringLiteral("\n  %1 GA-FL Block(e) gefunden").arg(gaFl.size()));

    // ---- CSV: Zeile 0 ist der Kopf ----
    if (job.rows.isEmpty()) {
        result.error = QStringLiteral("Keine CSV-Daten geladen");
        say(QStringLiteral("\n  FEHLER: Keine CSV-Daten geladen!"));
        return result;
    }
    bool hasKommentar = false;
    int kommentarIdx = 0;
    {
        const QStringList& header = job.rows.first();
        for (int i = 0; i < header.size(); ++i) {
            if (OcEngine::lispUpper(OcEngine::lispTrim(header.at(i), QStringLiteral(" ")))
                == QLatin1String("KOMMENTAR")) {
                hasKommentar = true;
                kommentarIdx = i;
            }
        }
    }
    const int dataCount = job.rows.size() - 1;
    say(QStringLiteral("\n  CSV: %1 Datenpunkte%2").arg(dataCount)
        .arg(hasKommentar ? QStringLiteral(" (KOMMENTAR-Spalte: %1)").arg(kommentarIdx)
                          : QStringLiteral(" (keine KOMMENTAR-Spalte)")));

    const bool haveReference = job.useReference && !reference.isEmpty();

    auto writeAll = [this](int insert, const QString& name, const QString& value) {
        return d->writeAll(d->inserts[insert], OcEngine::lispUpper(name), value);
    };

    // ---- Plankopf ----
    if (!job.plankopf.isEmpty()) {
        OcPairs plankopf = job.plankopf;
        auto assoc = [&plankopf](const QString& key, QString& value) {
            for (const auto& p : plankopf) {
                if (p.first == key) {
                    value = p.second;
                    return true;
                }
            }
            return false;
        };

        // ZEICHNUNGSNUMMER erweitern: "<Original> GA-FL n"
        QString znr;
        if (assoc(QStringLiteral("ZEICHNUNGSNUMMER"), znr)) {
            const auto old = qMakePair(QStringLiteral("ZEICHNUNGSNUMMER"), znr);
            const auto neu = qMakePair(QStringLiteral("ZEICHNUNGSNUMMER"),
                                       znr + QStringLiteral(" GA-FL ") + QString::number(sheetNum));
            for (auto& p : plankopf) {
                if (p == old) p = neu;
            }
        }

        say(QStringLiteral("\n  Plankopf-Attribute uebertragen..."));
        for (int i = 0; i < d->inserts.size(); ++i) {
            for (const auto& p : plankopf) writeAll(i, p.first, p.second);
        }
        say(QStringLiteral("\n  Plankopf an %1 Bloecke verteilt").arg(d->inserts.size()));

        QString gewerk, anlage;
        assoc(QStringLiteral("GEWERK"), gewerk);
        assoc(QStringLiteral("ANLAGE"), anlage);
        for (const int g : gaFl) {
            writeAll(g, QStringLiteral("OC_GEWERK"), gewerk);
            writeAll(g, QStringLiteral("OC_ANLAGE"), anlage);
            say(QStringLiteral("\n  OC_GEWERK=%1 OC_ANLAGE=%2").arg(gewerk, anlage));
        }
    }

    const QStringList& funcBases = functionBases();
    const QStringList sumBases = funcBases.mid(1);   // ohne OC_INTEG
    const int csvColOffset = 7;

    // ---- Uebertrag auf Folgeblaettern (Zeile 1) ----
    if (!isFirstSheet) {
        say(QStringLiteral("\n  Uebertrag aus Vorgaengerblatt in Zeile 1..."));
        if (state.hasCarry && !state.carry.isEmpty()) {
            for (const int g : gaFl) {
                writeAll(g, QStringLiteral("OC_BEZEICHNUNG_DP_1"), QStringLiteral("Uebertrag"));
                for (int i = 0; i < sumBases.size(); ++i) {
                    if (i < state.carry.size() && state.carry.at(i) > 0) {
                        writeAll(g, sumBases.at(i) + QStringLiteral("_DP_1"),
                                 QString::number(state.carry.at(i)));
                    }
                }
            }
            say(QStringLiteral("\n  Uebertrag in Zeile 1 geschrieben (%1 Spalten).")
                .arg(state.carry.size()));
        } else {
            say(QStringLiteral("\n  WARNUNG: Keine Summendaten vom Vorgaengerblatt vorhanden."));
        }
    }

    // ---- Datenpunkte ----
    auto trimBlank = [](const QString& s) { return OcEngine::lispTrim(s, QStringLiteral(" ")); };
    auto writeKommentar = [&](int insert, int row, const QString& text) {
        // (oc-fl-write-kommentar): Zeichen 1-40 in Zeile 1, der Rest in Zeile 2
        const QString line1 = text.left(40);
        const QString line2 = text.mid(40);
        writeAll(insert, QStringLiteral("OC_KOMMENTAR_1_DP_") + QString::number(row), line1);
        if (!line2.isEmpty()) {
            writeAll(insert, QStringLiteral("OC_KOMMENTAR_2_DP_") + QString::number(row), line2);
        }
    };

    const int rowOffset = isFirstSheet ? 1 : 2;
    for (int dpRow = 0; dpRow < dpCount && (startRow + dpRow) < dataCount; ++dpRow) {
        const int targetRow = rowOffset + dpRow;
        const QStringList& entry = job.rows.at(1 + startRow + dpRow);
        const QString row = QString::number(targetRow);

        const QString bmk = entry.value(0);
        const QString bez = entry.value(1);
        const QString refDp = entry.value(3);
        const QString basDp = entry.value(5);
        const QString integDp = entry.value(6);

        QString kommentarCsv;
        if (hasKommentar && entry.size() > kommentarIdx) {
            kommentarCsv = trimBlank(entry.at(kommentarIdx));
        }

        const QString bezeichnung = (!bmk.isEmpty() && !bez.isEmpty())
            ? bmk + QStringLiteral(" - ") + bez
            : bmk + bez;

        for (const int g : gaFl) {
            writeAll(g, QStringLiteral("OC_BEZEICHNUNG_DP_") + row, bezeichnung);
            writeAll(g, QStringLiteral("OC_AKS_DP_") + row, basDp);
            writeAll(g, QStringLiteral("OC_INTEG_DP_") + row, integDp);

            if (haveReference && !refDp.isEmpty()) {
                // (oc-fl-lookup-reference): erster Treffer in Spalte B
                const QStringList* refRow = nullptr;
                const QString wanted = OcEngine::lispUpper(refDp);
                for (const QStringList& r : reference) {
                    if (r.size() > 1 && OcEngine::lispUpper(trimBlank(r.at(1))) == wanted) {
                        refRow = &r;
                        break;
                    }
                }

                if (refRow) {
                    // Spalte C der Referenz = OC_INTEG (Index 0 der Funktionsspalten)
                    for (int col = 0; col < funcBases.size(); ++col) {
                        const QString value = refRow->value(col + 2);
                        const bool integFromSymbol =
                            (funcBases.at(col) == QLatin1String("OC_INTEG"))
                            && !trimBlank(integDp).isEmpty();
                        if (!trimBlank(value).isEmpty() && !integFromSymbol) {
                            writeAll(g, funcBases.at(col) + QStringLiteral("_DP_") + row, value);
                        }
                    }
                    // Kommentar: Symbol > Referenz (Spalte BJ) > leer
                    QString kommentar = kommentarCsv;
                    if (kommentar.isEmpty()) {
                        const int kRefComment = 61;
                        if (kRefComment < refRow->size()) {
                            kommentar = trimBlank(refRow->at(kRefComment));
                        }
                    }
                    if (!kommentar.isEmpty()) writeKommentar(g, targetRow, kommentar);
                } else {
                    // Fehlende Referenz: alle Funktionsspalten auf 0, Zeile markieren
                    say(QStringLiteral("\n    !!! FEHLENDE REFERENZ: %1 nicht in GA_FL_VORLAGE.ods")
                        .arg(refDp));
                    writeAll(g, QStringLiteral("OC_BEZEICHNUNG_DP_") + row,
                             QStringLiteral("[!REF] ") + bezeichnung);
                    for (const QString& base : funcBases) {
                        writeAll(g, base + QStringLiteral("_DP_") + row, QStringLiteral("0"));
                    }
                    if (!kommentarCsv.isEmpty()) writeKommentar(g, targetRow, kommentarCsv);
                    if (!state.missingRefs.contains(refDp)) state.missingRefs << refDp;
                }
            } else {
                // Ohne Referenz: Funktionswerte aus der CSV
                for (int col = 0; col < funcBases.size(); ++col) {
                    const QString value = entry.value(csvColOffset + col);
                    if (!trimBlank(value).isEmpty()) {
                        writeAll(g, funcBases.at(col) + QStringLiteral("_DP_") + row, value);
                    }
                }
                if (!kommentarCsv.isEmpty()) writeKommentar(g, targetRow, kommentarCsv);
            }
        }

        ++result.filled;
        say(QStringLiteral("\n  Zeile %1: %2").arg(targetRow).arg(bezeichnung));
    }

    // ---- Summenzeile OC_SUM_1..57 (ohne OC_INTEG) ----
    say(QStringLiteral("\n  Summenzeile berechnen..."));
    QVector<int> sums(sumBases.size(), 0);
    const int countStart = isFirstSheet ? rowOffset : 1;
    const int countTotal = dpCount + (isFirstSheet ? 0 : 1);   // +1 fuer den Uebertrag
    const Impl::Ins& firstBlock = d->inserts.at(gaFl.first());
    for (int r = 0; r < countTotal; ++r) {
        const QString row = QString::number(countStart + r);
        for (int col = 0; col < sumBases.size(); ++col) {
            QString value;
            if (!Impl::readLast(firstBlock, sumBases.at(col) + QStringLiteral("_DP_") + row, value)) {
                continue;
            }
            if (trimBlank(value).isEmpty()) continue;
            const int number = OcEngine::lispAtoi(value);
            sums[col] += (number > 0) ? number : 1;
        }
    }

    int sumWritten = 0;
    for (int i = 0; i < sums.size(); ++i) {
        for (const int g : gaFl) {
            if (writeAll(g, QStringLiteral("OC_SUM_") + QString::number(i + 1),
                         QString::number(sums.at(i))) > 0) {
                ++sumWritten;
            }
        }
    }
    say(QStringLiteral("\n  Summenzeile: %1 Werte geschrieben").arg(sumWritten));

    state.carry = sums;
    state.hasCarry = true;
    say(QStringLiteral("\n  Summenwerte fuer Uebertrag gespeichert."));

    say(QStringLiteral("\n=== GA-FL Befuellung abgeschlossen: %1 Datenpunkte in %2 Block(e) ===")
        .arg(result.filled).arg(gaFl.size()));
    if (!state.missingRefs.isEmpty()) {
        say(QStringLiteral("\n  ACHTUNG: %1 fehlende DP-Referenz(en) in GA_FL_VORLAGE.ods!")
            .arg(state.missingRefs.size()));
    }

    result.ok = true;
    return result;
}

// ============================================================================
// Textbreiten anpassen (TextBreitenAnpassenBloecke.lsp v3.0)
// ============================================================================

OcTextWidthResult OcDrawing::textBreitenAnpassen()
{
    OcTextWidthResult result;
    if (!d->db) return result;

    // ---- Teil 1: Attribute gegen die Zellen ihrer Blockdefinition ----
    struct Cell {
        QString tag;
        double w = 0, h = 0;
    };
    QVector<QPair<QString, QVector<Cell>>> cache;   // Blockname -> Zellen

    auto cellMap = [&cache](const Impl::Ins& ins) -> const QVector<Cell>& {
        for (const auto& c : cache) {
            if (c.first == ins.name) return c.second;
        }

        // (oc-tbf-cell-map)
        QVector<Cell> map;
        Buckets buckets;
        QVector<Rect> rects;   // zuletzt gefundenes zuerst

        AcDbBlockTableRecord* pRecord = nullptr;
        if (acdbOpenObject(pRecord, ins.recordId, AcDb::kForRead) == Acad::eOk && pRecord) {
            // 1. Durchlauf: alle geschlossenen Rechtecke
            AcDbBlockTableRecordIterator* pIter = nullptr;
            pRecord->newIterator(pIter);
            for (; pIter && !pIter->done(); pIter->step()) {
                AcDbEntity* pEnt = nullptr;
                if (pIter->getEntity(pEnt, AcDb::kForRead) != Acad::eOk || !pEnt) continue;
                if (pEnt->isA() == AcDbPolyline::desc()) {
                    AcDbPolyline* pLine = AcDbPolyline::cast(pEnt);
                    Rect rect;
                    if (pLine->isClosed() && rectFromPolyline(pLine, rect)) {
                        rects.prepend(rect);
                        buckets.add(rect);
                    }
                }
                pEnt->close();
            }
            delete pIter;

            // 2. Durchlauf: jeder Attributdefinition ihre Zelle zuordnen
            pIter = nullptr;
            pRecord->newIterator(pIter);
            for (; pIter && !pIter->done(); pIter->step()) {
                AcDbEntity* pEnt = nullptr;
                if (pIter->getEntity(pEnt, AcDb::kForRead) != Acad::eOk || !pEnt) continue;
                if (pEnt->isA() == AcDbAttributeDefinition::desc()) {
                    AcDbAttributeDefinition* pDef = AcDbAttributeDefinition::cast(pEnt);
                    const QString tag = OcEngine::lispUpper(fromAchar(pDef->tagConst()));
                    // (oc-tbf-text-point): Ausrichtungspunkt, wenn ausgerichtet
                    const AcGePoint3d pt = (static_cast<int>(pDef->horizontalMode()) > 0)
                        ? pDef->alignmentPoint()
                        : pDef->position();

                    Rect rect;
                    bool found = buckets.find(pt.x, pt.y, kTbfTol, rect);
                    if (!found) found = findEnclosing(rects, pt.x, pt.y, rect);
                    if (found) {
                        bool known = false;
                        for (const Cell& c : map) {
                            if (c.tag == tag) { known = true; break; }
                        }
                        if (!known) {
                            Cell c;
                            c.tag = tag;
                            c.w = rect.w;
                            c.h = rect.h;
                            map.prepend(c);
                        }
                    }
                }
                pEnt->close();
            }
            delete pIter;
            pRecord->close();
        }

        cache.prepend(qMakePair(ins.name, map));
        return cache.first().second;
    };

    for (Impl::Ins& ins : d->inserts) {
        double sx = std::fabs(ins.sx);
        if (sx <= 1e-6) sx = 1.0;

        const QVector<Cell> cells = cellMap(ins);
        if (cells.isEmpty()) continue;

        for (Impl::Attr& a : ins.attrs) {
            if (a.text.isEmpty()) continue;   // leere Attribute kosten nur Zeit
            const Cell* cell = nullptr;
            for (const Cell& c : cells) {
                if (c.tag == a.upper) { cell = &c; break; }
            }
            if (!cell) {
                ++result.skipCount;
                continue;
            }
            // Zellbreite = Breite in der Blockdefinition * Massstab
            if (d->applyWidth<AcDbAttribute>(a.id, cell->w * sx)) ++result.blockCount;
        }
    }

    // ---- Teil 2: Texte und Rechtecke direkt in den Layouts ----
    Buckets layoutRects;
    QVector<AcDbObjectId> texts;
    d->collectEntities([&](const AcDbObjectId& id) {
        AcDbEntity* pEnt = nullptr;
        if (acdbOpenObject(pEnt, id, AcDb::kForRead) != Acad::eOk || !pEnt) return;
        if (pEnt->isA() == AcDbPolyline::desc()) {
            AcDbPolyline* pLine = AcDbPolyline::cast(pEnt);
            Rect rect;
            // Filter (70 . 1): geschlossen und sonst nichts
            if (pLine->isClosed() && !pLine->hasPlinegen() && rectFromPolyline(pLine, rect)) {
                layoutRects.add(rect);
            }
        } else if (pEnt->isA() == AcDbText::desc()) {
            texts.append(id);
        }
        pEnt->close();
    });

    if (!layoutRects.isEmpty()) {
        for (const AcDbObjectId& id : texts) {
            AcDbText* pText = nullptr;
            if (acdbOpenObject(pText, id, AcDb::kForRead) != Acad::eOk || !pText) continue;
            const bool empty = fromAchar(pText->textStringConst()).isEmpty();
            const AcGePoint3d pt = (static_cast<int>(pText->horizontalMode()) > 0)
                ? pText->alignmentPoint()
                : pText->position();
            pText->close();
            if (empty) continue;

            Rect rect;
            if (layoutRects.find(pt.x, pt.y, kTbfTol, rect)) {
                if (d->applyWidth<AcDbText>(id, rect.w)) ++result.modelCount;
            }
        }
    }

    return result;
}

} // namespace BatchProcessing
