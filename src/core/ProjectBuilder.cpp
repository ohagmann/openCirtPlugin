#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file ProjectBuilder.cpp
 * @brief Projektaufbau aus der Erstellliste (CSV), siehe ProjectBuilder.h
 *
 * Reihenfolge, Warnungen und Logzeilen folgen OC_PROJECT_BUILD_v3_5.lsp,
 * damit sich beide Fassungen ueber ihre Logs vergleichen lassen.
 */

// BRX Platform headers
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

// BRX API headers
#include "aced.h"
#include "AcApDMgr.h"
#include "dbsymtb.h"    // AcDbBlockTable, AcDbBlockTableRecord
#include "dbents.h"     // AcDbBlockReference, AcDbAttribute
#include "dbdynblk.h"   // AcDbDynBlockReference: Name dynamischer Bloecke
#include "BrxSpecific/BrxGenericPropertiesAccess.h"   // BrxDbProperties: Name parametrischer Bloecke

#include "ProjectBuilder.h"
#include "TextAlignment.h"

#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QRegularExpression>
#include <QStringDecoder>

#include <algorithm>

namespace BatchProcessing {

namespace {

const char* const kZeichnungenDir = "05- Projekt Zeichnungen";
const char* const kVorlagenDir = "04- Vorlagen";
const char* const kCsvLineTag = "_CSV-LINE_";
const char* const kRuler =
    "==========================================================================";

/// Windows-1252: die Bytes 0x80..0x9F weichen von Latin-1 ab
QString decodeCp1252(const QByteArray& bytes)
{
    static const char16_t high[32] = {
        0x20AC, 0x0081, 0x201A, 0x0192, 0x201E, 0x2026, 0x2020, 0x2021,
        0x02C6, 0x2030, 0x0160, 0x2039, 0x0152, 0x008D, 0x017D, 0x008F,
        0x0090, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2013, 0x2014,
        0x02DC, 0x2122, 0x0161, 0x203A, 0x0153, 0x009D, 0x017E, 0x0178
    };
    QString out;
    out.reserve(bytes.size());
    for (const char c : bytes) {
        const unsigned char b = static_cast<unsigned char>(c);
        if (b >= 0x80 && b <= 0x9F) {
            out.append(QChar(high[b - 0x80]));
        } else {
            out.append(QChar(static_cast<char16_t>(b)));
        }
    }
    return out;
}

/// Wert in einer Attributliste suchen (erster Treffer gilt)
const QPair<QString, QString>* findAttr(const ProjectBuilder::AttrList& attrs,
                                        const QString& tag)
{
    for (const auto& p : attrs) {
        if (p.first == tag) return &p;
    }
    return nullptr;
}

QString attrValue(const ProjectBuilder::AttrList& attrs, const QString& tag)
{
    const auto* p = findAttr(attrs, tag);
    return p ? p->second : QString();
}

QString blockRecordName(const AcDbObjectId& recordId)
{
    QString name;
    AcDbBlockTableRecord* pRecord = nullptr;
    if (acdbOpenObject(pRecord, recordId, AcDb::kForRead) == Acad::eOk && pRecord) {
        const ACHAR* raw = nullptr;
        pRecord->getName(raw);
        if (raw) name = QString::fromWCharArray(raw);
        pRecord->close();
    }
    return name;
}

/// Blockname einer Referenz, wie ihn die Eigenschaft "EffectiveName" zeigt.
/// Dynamische und parametrische Bloecke verweisen auf einen anonymen Block
/// (*U...); gesucht ist der Name der Definition dahinter.
QString effectiveBlockName(AcDbBlockReference* pRef)
{
    const QString name = blockRecordName(pRef->blockTableRecord());
    if (!name.startsWith(QLatin1Char('*'))) return name;

    // Dynamischer Block
    AcDbDynBlockReference dynRef(pRef);
    if (dynRef.isDynamicBlock()) {
        const AcDbObjectId dynId = dynRef.dynamicBlockTableRecord();
        if (!dynId.isNull()) {
            const QString dynName = blockRecordName(dynId);
            if (!dynName.isEmpty()) return dynName;
        }
    }

    // Parametrischer Block (BricsCAD): nur ueber die Eigenschaft erreichbar
    AcValue value;
    if (BrxDbProperties::getValue(pRef->objectId(), L"EffectiveName~Native", value)) {
        AcString text;
        if (value.get(text) && !text.isEmpty()) {
            return QString::fromWCharArray(text.kwszPtr());
        }
    }
    return name;
}

/// Attribute einer Blockreferenz zum Schreiben oeffnen. Der Aufrufer schliesst sie.
QVector<AcDbAttribute*> openAttributes(AcDbBlockReference* pRef)
{
    QVector<AcDbAttribute*> result;
    AcDbObjectIterator* pIter = pRef->attributeIterator();
    while (pIter && !pIter->done()) {
        AcDbObject* pObj = nullptr;
        if (acdbOpenObject(pObj, pIter->objectId(), AcDb::kForWrite) == Acad::eOk && pObj) {
            AcDbAttribute* pAttr = AcDbAttribute::cast(pObj);
            if (pAttr) {
                result.append(pAttr);
            } else {
                pObj->close();
            }
        }
        pIter->step();
    }
    delete pIter;
    return result;
}

void closeAttributes(const QVector<AcDbAttribute*>& attrs)
{
    for (AcDbAttribute* pAttr : attrs) pAttr->close();
}

QString attributeTag(AcDbAttribute* pAttr)
{
    const ACHAR* raw = pAttr->tagConst();
    return raw ? QString::fromWCharArray(raw).toUpper() : QString();
}

QString attributeText(AcDbAttribute* pAttr)
{
    const ACHAR* raw = pAttr->textStringConst();
    return raw ? QString::fromWCharArray(raw) : QString();
}

/// Attributwert setzen. Im Editor rechnet BricsCAD die Lage zentrierter und
/// rechtsbuendiger Texte selbst nach, in einer Side-Database nicht - deshalb
/// hier ausdruecklich.
void setAttributeText(AcDbAttribute* pAttr, const QString& value, AcDbDatabase* pDb)
{
    pAttr->setTextString(value.toStdWString().c_str());
    if (pAttr->isMTextAttribute()) {
        pAttr->updateMTextAttribute();
    }
    adjustTextAlignment(pAttr, pDb);
}

/// Alle Blockreferenzen des Modellbereichs zum Lesen oeffnen.
/// Der Aufrufer schliesst sie.
QVector<AcDbBlockReference*> openModelSpaceReferences(AcDbDatabase* pDb)
{
    QVector<AcDbBlockReference*> result;

    AcDbBlockTable* pTable = nullptr;
    if (pDb->getBlockTable(pTable, AcDb::kForRead) != Acad::eOk || !pTable) return result;

    AcDbBlockTableRecord* pModelSpace = nullptr;
    const Acad::ErrorStatus es = pTable->getAt(ACDB_MODEL_SPACE, pModelSpace, AcDb::kForRead);
    pTable->close();
    if (es != Acad::eOk || !pModelSpace) return result;

    AcDbBlockTableRecordIterator* pIter = nullptr;
    pModelSpace->newIterator(pIter);
    while (pIter && !pIter->done()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForRead) == Acad::eOk && pEnt) {
            AcDbBlockReference* pRef = AcDbBlockReference::cast(pEnt);
            if (pRef) {
                result.append(pRef);
            } else {
                pEnt->close();
            }
        }
        pIter->step();
    }
    delete pIter;
    pModelSpace->close();
    return result;
}

/// Anlagen-Header-Modus: schreibt in alle Bloecke mit Attributen, deren Name
/// NICHT *STEMPEL* enthaelt. Leere Werte werden geschrieben.
void applyHeaderAttributes(AcDbDatabase* pDb, const ProjectBuilder::AttrList& attrs,
                           int& blockCount, int& setCount)
{
    blockCount = 0;
    setCount = 0;
    if (attrs.isEmpty()) return;

    const QVector<AcDbBlockReference*> refs = openModelSpaceReferences(pDb);
    for (AcDbBlockReference* pRef : refs) {
        const QVector<AcDbAttribute*> blockAttrs = openAttributes(pRef);
        const bool isStempel =
            effectiveBlockName(pRef).toUpper().contains(QLatin1String("STEMPEL"));

        if (!blockAttrs.isEmpty() && !isStempel) {
            ++blockCount;
            for (AcDbAttribute* pAttr : blockAttrs) {
                const QString tag = attributeTag(pAttr);
                if (tag == QLatin1String(kCsvLineTag)) continue;
                const auto* pair = findAttr(attrs, tag);
                if (pair) {
                    setAttributeText(pAttr, pair->second, pDb);
                    ++setCount;
                }
            }
        }
        closeAttributes(blockAttrs);
        pRef->close();
    }
}

/// Stempel-Modus: schreibt nur in Stempel-Bloecke, deren OC_ANLAGE mit
/// demselben [n] beginnt wie der Wert der Liste.
void applyStempel(AcDbDatabase* pDb, const ProjectBuilder::AttrList& stempelAttrs,
                  const QString& csvPrefix, int& hitCount, int& setCount)
{
    hitCount = 0;
    setCount = 0;

    static const QRegularExpression prefixRe(QStringLiteral("^\\[\\d+\\]"));

    const QVector<AcDbBlockReference*> refs = openModelSpaceReferences(pDb);
    for (AcDbBlockReference* pRef : refs) {
        const QVector<AcDbAttribute*> blockAttrs = openAttributes(pRef);
        const bool isStempel =
            effectiveBlockName(pRef).toUpper().contains(QLatin1String("STEMPEL"));

        if (!blockAttrs.isEmpty() && isStempel) {
            // Passt dieser Stempel zum [n] der Liste?
            QString blockPrefix;
            for (AcDbAttribute* pAttr : blockAttrs) {
                if (attributeTag(pAttr) == QLatin1String("OC_ANLAGE")) {
                    const QRegularExpressionMatch m = prefixRe.match(attributeText(pAttr));
                    blockPrefix = m.hasMatch() ? m.captured(0) : QString();
                }
            }

            if (!blockPrefix.isEmpty() && blockPrefix == csvPrefix) {
                ++hitCount;
                for (AcDbAttribute* pAttr : blockAttrs) {
                    const QString tag = attributeTag(pAttr);
                    if (tag == QLatin1String(kCsvLineTag)) continue;
                    const auto* pair = findAttr(stempelAttrs, tag);
                    if (pair) {
                        setAttributeText(pAttr, pair->second, pDb);
                        ++setCount;
                    }
                }
            }
        }
        closeAttributes(blockAttrs);
        pRef->close();
    }
}

} // namespace

// ============================================================================
// Aufbau / Abbau
// ============================================================================

ProjectBuilder::ProjectBuilder(QObject* parent)
    : QObject(parent)
{
}

ProjectBuilder::~ProjectBuilder()
{
    closeLog();
}

// ============================================================================
// Log
// ============================================================================

QString ProjectBuilder::native(const QString& path)
{
    return QDir::toNativeSeparators(path);
}

bool ProjectBuilder::openLog(const QString& path)
{
    closeLog();
    m_logPath = path;
    m_logFile.setFileName(path);
    if (!m_logFile.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        return false;
    }
    // UTF-8 mit BOM, damit auch Windows-Editoren die Umlaute richtig zeigen
    m_logFile.write("\xEF\xBB\xBF");
    return true;
}

void ProjectBuilder::closeLog()
{
    if (m_logFile.isOpen()) m_logFile.close();
}

void ProjectBuilder::log(const QString& line)
{
    if (!m_logFile.isOpen()) return;
#ifdef _WIN32
    m_logFile.write((line + QStringLiteral("\r\n")).toUtf8());
#else
    m_logFile.write((line + QStringLiteral("\n")).toUtf8());
#endif
    // Nach jeder Zeile auf die Platte, damit das Log auch nach einem Absturz da ist
    m_logFile.flush();
}

void ProjectBuilder::logHeader(const QString& csvPath, const QString& vorlagenPath)
{
    log(QString::fromLatin1(kRuler));
    log(QStringLiteral("openCirt Projektaufbau  -  %1")
        .arg(m_dryRun ? QStringLiteral("DRY RUN (keine Aenderungen!)")
                      : QStringLiteral("ECHTER LAUF")));
    log(QStringLiteral("Zeitpunkt:      %1")
        .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd HH:mm:ss"))));
    log(QStringLiteral("CSV:            %1").arg(native(csvPath)));
    log(QStringLiteral("Projekt-Root:   %1").arg(native(m_projectRoot)));
    log(QStringLiteral("Vorlagen:       %1").arg(native(vorlagenPath)));
    log(QString::fromLatin1(kRuler));
}

// ============================================================================
// Liste lesen
// ============================================================================

QStringList ProjectBuilder::readCsv(const QString& csvPath, QString& encodingName)
{
    QStringList lines;
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly)) return lines;
    QByteArray bytes = file.readAll();
    file.close();

    QString text;
    if (bytes.startsWith("\xEF\xBB\xBF")) {
        bytes.remove(0, 3);
        text = QString::fromUtf8(bytes);
        encodingName = QStringLiteral("UTF-8 mit BOM");
    } else {
        bool ascii = true;
        for (const char c : bytes) {
            if (static_cast<unsigned char>(c) > 127) { ascii = false; break; }
        }
        if (ascii) {
            text = QString::fromLatin1(bytes);
            encodingName = QStringLiteral("ASCII (keine Sonderzeichen)");
        } else {
            QStringDecoder utf8(QStringConverter::Utf8, QStringConverter::Flag::Stateless);
            const QString decoded = utf8.decode(bytes);
            if (!utf8.hasError()) {
                text = decoded;
                encodingName = QStringLiteral("UTF-8");
            } else {
                text = decodeCp1252(bytes);
                encodingName = QStringLiteral("Windows-1252");
            }
        }
    }

    lines = text.split(QLatin1Char('\n'));
    // Endet die Datei mit einem Zeilenende, ist das letzte Element keine Zeile
    if (!lines.isEmpty() && lines.last().isEmpty()) lines.removeLast();
    for (QString& line : lines) {
        if (line.endsWith(QLatin1Char('\r'))) line.chop(1);
    }
    return lines;
}

QString ProjectBuilder::trimField(const QString& s)
{
    // Leerzeichen, Tabulator, CR und LF - nichts sonst
    int i = 0;
    int j = s.size() - 1;
    auto isBlank = [](QChar c) {
        return c == QLatin1Char(' ') || c == QLatin1Char('\t')
            || c == QLatin1Char('\r') || c == QLatin1Char('\n');
    };
    while (i <= j && isBlank(s.at(i))) ++i;
    while (j >= i && isBlank(s.at(j))) --j;
    return (i > j) ? QString() : s.mid(i, j - i + 1);
}

QString ProjectBuilder::cleanHeader(const QString& s)
{
    // "Dateiname:", "Dateiname" und " DATEINAME " ergeben alle "DATEINAME"
    QString h = trimField(s);
    if (h.endsWith(QLatin1Char(':'))) h.chop(1);
    return trimField(h).toUpper();
}

QString ProjectBuilder::rowValue(const QStringList& fields, int col)
{
    if (col < 0 || col >= fields.size()) return QString();
    return trimField(fields.at(col));
}

int ProjectBuilder::headerIndex(const QStringList& header, const QString& key)
{
    // Bei doppelten Spaltennamen gilt die letzte Spalte
    int found = -1;
    for (int i = 0; i < header.size(); ++i) {
        if (cleanHeader(header.at(i)) == key) found = i;
    }
    return found;
}

QString ProjectBuilder::stempelPrefix(const QString& value)
{
    // "[2] VE Ventil" -> "[2]". Ohne fuehrendes "[<Ziffern>]" leer.
    static const QRegularExpression re(QStringLiteral("^\\[\\d+\\]"));
    const QRegularExpressionMatch m = re.match(value);
    return m.hasMatch() ? m.captured(0) : QString();
}

QString ProjectBuilder::padNumber(int n, int width)
{
    return QStringLiteral("%1").arg(n, width, 10, QLatin1Char('0'));
}

// ============================================================================
// Vorlagen
// ============================================================================

QStringList ProjectBuilder::listFiles(const QString& path, const QStringList& patterns)
{
    const QDir dir(path);
    QStringList names = patterns.isEmpty()
        ? dir.entryList(QDir::Files | QDir::Hidden | QDir::System, QDir::Unsorted)
        : dir.entryList(patterns, QDir::Files | QDir::Hidden | QDir::System, QDir::Unsorted);
    sortLikeWindows(names);
    return names;
}

QStringList ProjectBuilder::listDirs(const QString& path)
{
    QStringList names = QDir(path).entryList(
        QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden, QDir::Unsorted);
    sortLikeWindows(names);
    return names;
}

void ProjectBuilder::sortLikeWindows(QStringList& names)
{
    // Reihenfolge wie ein Windows-Laufwerk sie liefert: nach Name in
    // Grossschreibung. So stehen die Logzeilen auf beiden Systemen gleich.
    std::sort(names.begin(), names.end(), [](const QString& a, const QString& b) {
        const int c = a.toUpper().compare(b.toUpper());
        return (c != 0) ? (c < 0) : (a < b);
    });
}

QStringList ProjectBuilder::findFilesRecursive(const QString& basePath,
                                               const QStringList& patterns)
{
    // Erst die Dateien des Ordners, dann die Unterordner
    QStringList result;
    const QDir dir(basePath);
    if (!dir.exists()) return result;

    for (const QString& f : listFiles(basePath, patterns)) {
        result << dir.absoluteFilePath(f);
    }
    for (const QString& d : listDirs(basePath)) {
        result << findFilesRecursive(dir.absoluteFilePath(d), patterns);
    }
    return result;
}

bool ProjectBuilder::buildVorlagenMap(const QString& vorlagenPath)
{
    m_vorlagen.clear();

    struct Conflict { QString name, first, second; };
    QVector<Conflict> conflicts;

    const QStringList dwgs = findFilesRecursive(vorlagenPath, QStringList() << "*.dwg");
    for (const QString& path : dwgs) {
        const QString base = QFileInfo(path).completeBaseName().toUpper();
        if (m_vorlagen.contains(base)) {
            conflicts.append({base, m_vorlagen.value(base), path});
        } else {
            m_vorlagen.insert(base, path);
        }
    }

    if (!conflicts.isEmpty()) {
        log(QStringLiteral("*** FEHLER: Mehrdeutige Vorlagen gefunden:"));
        for (const Conflict& c : conflicts) {
            log(QStringLiteral("  %1:").arg(c.name));
            log(QStringLiteral("    %1").arg(native(c.first)));
            log(QStringLiteral("    %1").arg(native(c.second)));
        }
        log(QStringLiteral("Abbruch. Bitte Vorlagen-Ordner bereinigen."));
        m_lastError = QStringLiteral(
            "Im Vorlagen-Ordner gibt es %1 Vorlage(n) mit doppeltem Namen. "
            "Einzelheiten stehen im Log.").arg(conflicts.size());
        return false;
    }
    if (m_vorlagen.isEmpty()) {
        log(QStringLiteral("*** FEHLER: Keine Vorlagen (*.dwg) im Vorlagen-Ordner gefunden."));
        log(QStringLiteral("Abbruch."));
        m_lastError = QStringLiteral("Im Vorlagen-Ordner liegt keine Vorlage (*.dwg).");
        return false;
    }
    return true;
}

// ============================================================================
// Plan
// ============================================================================

bool ProjectBuilder::rowHasData(const QStringList& fields, const ColumnList& cols)
{
    for (const auto& col : cols) {
        if (!rowValue(fields, col.first).isEmpty()) return true;
    }
    return false;
}

ProjectBuilder::AttrList ProjectBuilder::rowHashAttrs(const QStringList& fields,
                                                      const ColumnList& hashCols, int n)
{
    // "#" im Tag durch die Nummer der Meldung ersetzen: OC_FCODE_DP_# -> OC_FCODE_DP_2
    AttrList attrs;
    for (const auto& col : hashCols) {
        QString tag = col.second;
        tag.replace(QLatin1Char('#'), QString::number(n));
        attrs.append(qMakePair(tag, rowValue(fields, col.first)));
    }
    return attrs;
}

int ProjectBuilder::nextIndex(QMap<QString, QStringList>& map, const QString& parentKey,
                              const QString& childName)
{
    // Index lokal je Eltern-Ordner, in der Reihenfolge des ersten Auftretens
    QStringList& children = map[parentKey];
    int idx = children.indexOf(childName);
    if (idx < 0) {
        children.append(childName);
        idx = children.size() - 1;
    }
    return idx;
}

bool ProjectBuilder::buildPlan(const QStringList& lines, const QStringList& header,
                               const ColumnList& attrCols)
{
    m_plan.clear();

    const int colVorl     = headerIndex(header, QStringLiteral("VORLAGE"));
    const int colLos      = headerIndex(header, QStringLiteral("LOS"));
    const int colAsp      = headerIndex(header, QStringLiteral("ASP"));
    const int colGewerk   = headerIndex(header, QStringLiteral("GEWERK"));
    const int colAnlage   = headerIndex(header, QStringLiteral("ANLAGE"));
    const int colDatei    = headerIndex(header, QStringLiteral("DATEINAME"));
    const int colOcAnlage = headerIndex(header, QStringLiteral("OC_ANLAGE"));

    // Pflichtspalten pruefen
    QStringList missing;
    if (colVorl < 0)   missing << QStringLiteral("Vorlage");
    if (colLos < 0)    missing << QStringLiteral("Los");
    if (colAsp < 0)    missing << QStringLiteral("ASP");
    if (colGewerk < 0) missing << QStringLiteral("Gewerk");
    if (colAnlage < 0) missing << QStringLiteral("Anlage");
    if (colDatei < 0)  missing << QStringLiteral("Dateiname");
    if (!missing.isEmpty()) {
        QString list;
        for (const QString& m : missing) list += m + QLatin1Char(' ');
        log(QStringLiteral("*** FEHLER: Pflichtspalten in CSV fehlen: %1").arg(list));
        m_lastError = QStringLiteral("In der Erstellliste fehlen Pflichtspalten: %1")
                      .arg(missing.join(QStringLiteral(", ")));
        return false;
    }
    if (colOcAnlage < 0) {
        log(QStringLiteral("Hinweis: Spalte 'OC_ANLAGE:' nicht in CSV -> "
                           "Stempel-Mechanismus deaktiviert."));
    }

    // Attribut-Spalten in drei Klassen:
    //   hashCols        Meldungs-Schablonen mit "#"
    //   groupPlainCols  OC_AKS, OC_BEZEICHNUNG - nur mit OC_AKS-Marker geschrieben
    //   normalPlainCols alle uebrigen, immer geschrieben, auch leer
    ColumnList hashCols, groupPlainCols, normalPlainCols;
    for (const auto& col : attrCols) {
        if (col.second.contains(QLatin1Char('#'))) {
            hashCols.append(col);
        } else if (col.second == QLatin1String("OC_AKS")
                   || col.second == QLatin1String("OC_BEZEICHNUNG")) {
            groupPlainCols.append(col);
        } else {
            normalPlainCols.append(col);
        }
    }
    const int colAks = headerIndex(header, QStringLiteral("OC_AKS"));

    if (!hashCols.isEmpty() || !groupPlainCols.isEmpty()) {
        log(QStringLiteral("Meldungsgruppe: %1 #-Spalte(n), %2 Gruppen-plain-Spalte(n)")
            .arg(hashCols.size()).arg(groupPlainCols.size()));
        log(QStringLiteral("  Marker: OC_AKS != \"\" (pro Anlagen-Header-Zeile)"));
        for (const auto& col : groupPlainCols) log(QStringLiteral("  -> %1").arg(col.second));
        for (const auto& col : hashCols)       log(QStringLiteral("  -> %1").arg(col.second));
        if (colAks < 0) {
            log(QStringLiteral("  WARN  Spalte 'OC_AKS' fehlt -> "
                               "Meldungsgruppe wird NIE geschrieben!"));
            ++m_errCount;
        }
    }

    QMap<QString, QStringList> losMap, aspMap, gewerkMap, anlageMap;
    QStringList topLevelNames;   // Dateinamen bereits geplanter Projektzeilen
    int lastEntry = -1;
    int lineIdx = 1;             // Zeile 1 = Kopfzeile

    for (int r = 1; r < lines.size(); ++r) {
        ++lineIdx;
        const QStringList fields = lines.at(r).split(QLatin1Char(';'));
        const QString vorl   = rowValue(fields, colVorl);
        const QString los    = rowValue(fields, colLos);
        const QString asp    = rowValue(fields, colAsp);
        const QString gewerk = rowValue(fields, colGewerk);
        const QString anlage = rowValue(fields, colAnlage);
        const QString datei  = rowValue(fields, colDatei);
        const QString ocAnlage = (colOcAnlage >= 0) ? rowValue(fields, colOcAnlage) : QString();

        const bool isStempelzeile =
            vorl.isEmpty() && datei.isEmpty() && !ocAnlage.isEmpty();
        const bool isMeldungszeile =
            vorl.isEmpty() && datei.isEmpty() && ocAnlage.isEmpty()
            && rowHasData(fields, hashCols);
        const bool isProjektzeile =
            !vorl.isEmpty() && !datei.isEmpty()
            && los.isEmpty() && asp.isEmpty() && gewerk.isEmpty() && anlage.isEmpty();

        // -------------------- Stempelzeile --------------------
        if (isStempelzeile) {
            if (stempelPrefix(ocAnlage).isEmpty()) {
                log(QStringLiteral("  WARN  Zeile %1: Stempelzeile ohne [n]-Praefix in "
                                   "OC_ANLAGE='%2' -> uebersprungen")
                    .arg(lineIdx).arg(ocAnlage));
                ++m_errCount;
            } else if (lastEntry < 0) {
                log(QStringLiteral("  WARN  Zeile %1: Stempelzeile ohne vorherige "
                                   "Anlagen-Header-Zeile (OC_ANLAGE='%2') -> uebersprungen")
                    .arg(lineIdx).arg(ocAnlage));
                ++m_errCount;
            } else {
                AttrList stempelEntry;
                for (const auto& col : normalPlainCols) {
                    stempelEntry.append(qMakePair(col.second, rowValue(fields, col.first)));
                }
                stempelEntry.append(qMakePair(QString::fromLatin1(kCsvLineTag),
                                              QString::number(lineIdx)));
                m_plan[lastEntry].stempel.append(stempelEntry);
            }
            continue;
        }

        // -------------------- Meldungszeile --------------------
        if (isMeldungszeile) {
            if (lastEntry < 0) {
                log(QStringLiteral("  WARN  Zeile %1: Meldungszeile ohne vorherige "
                                   "Anlagen-Header-Zeile -> uebersprungen").arg(lineIdx));
                ++m_errCount;
            } else if (!m_plan.at(lastEntry).meldungsblock) {
                log(QStringLiteral("  WARN  Zeile %1: Meldungszeile (#-Daten) unter Anlage "
                                   "OHNE OC_AKS-Marker -> uebersprungen, Meldung VERWORFEN")
                    .arg(lineIdx));
                ++m_errCount;
            } else {
                PlanEntry& entry = m_plan[lastEntry];
                ++entry.msgCount;
                entry.attrs += rowHashAttrs(fields, hashCols, entry.msgCount);
            }
            continue;
        }

        // -------------------- Trennzeile / Kommentar --------------------
        if (datei.isEmpty() && ocAnlage.isEmpty()) continue;

        // -------------------- Anlagen-Header / Projektzeile --------------------
        if (!isProjektzeile
            && (vorl.isEmpty() || los.isEmpty() || asp.isEmpty()
                || gewerk.isEmpty() || anlage.isEmpty())) {
            log(QStringLiteral("  WARN  Zeile %1: Dateiname='%2' aber Metadaten unvollstaendig "
                               "[Vorlage='%3' Los='%4' ASP='%5' Gewerk='%6' Anlage='%7'] "
                               "-> uebersprungen (Projektzeile = alle vier Ebenen leer)")
                .arg(lineIdx).arg(datei, vorl, los, asp, gewerk, anlage));
            ++m_errCount;
            continue;
        }

        if (isProjektzeile && topLevelNames.contains(datei.toUpper())) {
            log(QStringLiteral("  WARN  Zeile %1: Projektzeile mit doppeltem Dateinamen '%2' "
                               "-> uebersprungen").arg(lineIdx).arg(datei));
            ++m_errCount;
            continue;
        }

        const QString vorlSrc = m_vorlagen.value(vorl.toUpper());
        if (vorlSrc.isEmpty()) {
            log(QStringLiteral("  WARN  Zeile %1: Vorlage '%2' nicht in 04- Vorlagen gefunden "
                               "-> uebersprungen").arg(lineIdx).arg(vorl));
            ++m_errCount;
            continue;
        }

        // OK -> Plan-Eintrag bauen
        PlanEntry entry;
        entry.src = vorlSrc;
        entry.dstBase = datei;
        entry.topLevel = isProjektzeile;
        entry.csvLine = lineIdx;

        if (isProjektzeile) {
            entry.dstDir = m_zeichnungsRoot;
            topLevelNames << datei.toUpper();
            log(QStringLiteral("  Zeile %1: Projektzeile '%2' -> oberste Ebene "
                               "(ohne NN-Praefix)").arg(lineIdx).arg(datei));
        } else {
            const int losIdx    = nextIndex(losMap, QString(), los);
            const int aspIdx    = nextIndex(aspMap, los, asp);
            const int gewerkIdx = nextIndex(gewerkMap, los + QLatin1Char('|') + asp, gewerk);
            const int anlageIdx = nextIndex(
                anlageMap, los + QLatin1Char('|') + asp + QLatin1Char('|') + gewerk, anlage);

            entry.dstDir = m_zeichnungsRoot
                + QLatin1Char('/') + padNumber(losIdx, 2)    + QLatin1Char(' ') + los
                + QLatin1Char('/') + padNumber(aspIdx, 2)    + QLatin1Char(' ') + asp
                + QLatin1Char('/') + padNumber(gewerkIdx, 2) + QLatin1Char(' ') + gewerk
                + QLatin1Char('/') + padNumber(anlageIdx, 2) + QLatin1Char(' ') + anlage;
        }

        // OC_AKS der Header-Zeile entscheidet, ob die Meldungsgruppe geschrieben wird
        const QString aksMarker = (colAks >= 0) ? rowValue(fields, colAks) : QString();
        entry.meldungsblock = !aksMarker.isEmpty();

        // Normale Attribute: immer, auch leer
        for (const auto& col : normalPlainCols) {
            entry.attrs.append(qMakePair(col.second, rowValue(fields, col.first)));
        }

        // OC_ANLAGE in der Header-Zeile gefuellt -> erster Stempel
        if (colOcAnlage >= 0 && !ocAnlage.isEmpty()) {
            if (stempelPrefix(ocAnlage).isEmpty()) {
                log(QStringLiteral("  WARN  Zeile %1: OC_ANLAGE in Anlagen-Header ohne "
                                   "[n]-Praefix: '%2' -> Stempel wird nicht bef uellt")
                    .arg(lineIdx).arg(ocAnlage));
                ++m_errCount;
            } else {
                AttrList stempelEntry = entry.attrs;
                stempelEntry.append(qMakePair(QString::fromLatin1(kCsvLineTag),
                                              QString::number(lineIdx)));
                entry.stempel.append(stempelEntry);
            }
        }

        // Meldungsgruppe erst nach dem Stempel, damit der Stempel nur die
        // normalen Attribute sieht
        if (entry.meldungsblock) {
            for (const auto& col : groupPlainCols) {
                entry.attrs.append(qMakePair(col.second, rowValue(fields, col.first)));
            }
            if (rowHasData(fields, hashCols)) {
                entry.attrs += rowHashAttrs(fields, hashCols, 1);
                entry.msgCount = 1;
            }
        } else if (rowHasData(fields, hashCols) || rowHasData(fields, groupPlainCols)) {
            log(QStringLiteral("  WARN  Zeile %1: Meldungsdaten (# oder OC_BEZEICHNUNG) "
                               "vorhanden, aber OC_AKS LEER -> Gruppe NICHT geschrieben "
                               "(Marker vergessen?)").arg(lineIdx));
            ++m_errCount;
        }

        m_plan.append(entry);
        lastEntry = m_plan.size() - 1;
    }
    return true;
}

void ProjectBuilder::assignIndices()
{
    // Anzahl je Zielordner. Projektzeilen zaehlen nicht, sie bekommen kein Praefix.
    QMap<QString, int> counts;
    for (const PlanEntry& entry : m_plan) {
        if (!entry.topLevel) ++counts[entry.dstDir];
    }

    QMap<QString, int> next;
    for (PlanEntry& entry : m_plan) {
        if (entry.topLevel) {
            entry.dstPath = entry.dstDir + QLatin1Char('/') + entry.dstBase
                          + QStringLiteral(".dwg");
            continue;
        }
        const int count = counts.value(entry.dstDir);
        const int width = (count >= 1000) ? 4 : (count >= 100) ? 3 : 2;
        const int idx = next.value(entry.dstDir, 0);
        entry.dstPath = entry.dstDir + QLatin1Char('/') + padNumber(idx, width)
                      + QLatin1Char(' ') + entry.dstBase + QStringLiteral(".dwg");
        next[entry.dstDir] = idx + 1;
    }
}

// ============================================================================
// Pre-Flight und Vorschau
// ============================================================================

bool ProjectBuilder::isProtectedDeckblatt(const QString& fileName) const
{
    return m_preview.protectDeckblatt
        && fileName.endsWith(QStringLiteral("Deckblatt_A.dwg"), Qt::CaseInsensitive);
}

bool ProjectBuilder::preflight()
{
    log(QString());
    log(QStringLiteral("--- Pre-Flight: offene Zeichnungen? ---"));

    m_preview.openDocs.clear();
    m_preview.lockFiles.clear();

    // In dieser Sitzung offene Zeichnungen unter dem Zeichnungsordner
    const QString rootPrefix = QDir::fromNativeSeparators(m_zeichnungsRoot) + QLatin1Char('/');
    if (acDocManager) {
        AcApDocumentIterator* pIter = acDocManager->newAcApDocumentIterator();
        for (; pIter && !pIter->done(); pIter->step()) {
            AcApDocument* pDoc = pIter->document();
            if (!pDoc || !pDoc->fileName()) continue;
            const QString full =
                QDir::fromNativeSeparators(QString::fromWCharArray(pDoc->fileName()));
            if (full.startsWith(rootPrefix, Qt::CaseInsensitive)) {
                m_preview.openDocs << full;
            }
        }
        delete pIter;
    }

    // Sperrdateien: andere Sitzung oder Rest eines Absturzes
    for (const char* pattern : {"*.dwl", "*.dwl2"}) {
        m_preview.lockFiles << findFilesRecursive(
            m_zeichnungsRoot, QStringList() << QString::fromLatin1(pattern));
    }

    for (const QString& f : m_preview.openDocs) {
        log(QStringLiteral("  OFFEN  (diese Sitzung)   %1").arg(native(f)));
    }
    for (const QString& f : m_preview.lockFiles) {
        log(QStringLiteral("  SPERRE (*.dwl vorhanden) %1").arg(native(f)));
    }

    const int n = m_preview.openDocs.size() + m_preview.lockFiles.size();
    if (n == 0) {
        log(QStringLiteral("  OK - keine offenen Zeichnungen / Sperrdateien gefunden."));
        return true;
    }
    if (m_dryRun) {
        log(QStringLiteral("  WARN  %1 offene Zeichnung(en)/Sperrdatei(en) - ein echter Lauf "
                           "wuerde hier ABBRECHEN.").arg(n));
        ++m_errCount;
        return true;
    }

    log(QString());
    log(QString::fromLatin1(kRuler));
    log(QStringLiteral("*** ABBRUCH: Zeichnungen unter '05- Projekt Zeichnungen' sind offen ***"));
    log(QStringLiteral("  Offene DWGs lassen sich nicht loeschen/ersetzen. Es wurde NICHTS"));
    log(QStringLiteral("  geloescht und NICHTS erzeugt. Bitte Zeichnungen schliessen bzw."));
    log(QStringLiteral("  verwaiste *.dwl/*.dwl2 (Absturz-Reste) von Hand entfernen."));
    log(QString::fromLatin1(kRuler));
    closeLog();

    m_blocked = true;
    m_lastError = QStringLiteral("Unter '05- Projekt Zeichnungen' sind Zeichnungen offen.");
    return false;
}

void ProjectBuilder::previewDeletions()
{
    m_preview.deleteCount = 0;
    m_preview.protectedCount = 0;

    const QDir root(m_zeichnungsRoot);
    if (!root.exists()) {
        log(QStringLiteral("  (Ordner existiert noch nicht: %1)").arg(native(m_zeichnungsRoot)));
        return;
    }

    for (const QString& name : listFiles(m_zeichnungsRoot, QStringList())) {
        const QString full = root.absoluteFilePath(name);
        if (isProtectedDeckblatt(name)) {
            log(QStringLiteral("  PROT   %1").arg(native(full)));
            ++m_preview.protectedCount;
        } else {
            log(QStringLiteral("  DEL    %1").arg(native(full)));
            ++m_preview.deleteCount;
        }
    }

    for (const QString& name : listDirs(m_zeichnungsRoot)) {
        const QStringList sub = findFilesRecursive(root.absoluteFilePath(name), QStringList());
        for (const QString& f : sub) {
            log(QStringLiteral("  DEL    %1").arg(native(f)));
            ++m_preview.deleteCount;
        }
    }
}

// ============================================================================
// prepare
// ============================================================================

bool ProjectBuilder::prepare(const QString& csvPath, const QString& projectRoot, bool dryRun)
{
    m_csvPath = csvPath;
    m_projectRoot = QDir::fromNativeSeparators(projectRoot);
    while (m_projectRoot.size() > 1 && m_projectRoot.endsWith(QLatin1Char('/'))) {
        m_projectRoot.chop(1);
    }
    m_dryRun = dryRun;
    m_blocked = false;
    m_errCount = 0;
    m_lastError.clear();
    m_plan.clear();
    m_preview = Preview();

    m_vorlagenPath = m_projectRoot + QLatin1Char('/') + QString::fromLatin1(kVorlagenDir);
    m_zeichnungsRoot = m_projectRoot + QLatin1Char('/') + QString::fromLatin1(kZeichnungenDir);

    if (!QDir(m_vorlagenPath).exists()) {
        m_lastError = QStringLiteral("Vorlagen-Ordner nicht gefunden: %1")
                      .arg(native(m_vorlagenPath));
        return false;
    }

    // -------- Logdatei oeffnen --------
    const QString stamp =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd_HHmm"));
    const QString logPath = m_projectRoot + QStringLiteral("/OC_Log_") + stamp
                          + (dryRun ? QStringLiteral("_DRY") : QString())
                          + QStringLiteral(".txt");
    if (!openLog(logPath)) {
        m_lastError = QStringLiteral("Log nicht schreibbar: %1").arg(native(logPath));
        return false;
    }
    logHeader(csvPath, m_vorlagenPath);

    // -------- CSV einlesen --------
    QString encodingName;
    const QStringList lines = readCsv(csvPath, encodingName);
    if (lines.size() < 2) {
        log(QStringLiteral("*** FEHLER: CSV leer oder hat keine Datenzeilen."));
        closeLog();
        m_lastError = QStringLiteral("Die Erstellliste ist leer oder hat keine Datenzeilen.");
        return false;
    }

    const QStringList header = lines.first().split(QLatin1Char(';'));
    log(QStringLiteral("CSV-Kodierung:             %1").arg(encodingName));
    log(QStringLiteral("CSV-Zeilen (inkl. Header): %1").arg(lines.size()));
    log(QStringLiteral("Header-Spalten:            %1").arg(header.size()));

    // -------- Attribut-Spalten: alles ausser den Metadaten-Spalten --------
    static const QStringList reserved = {
        QStringLiteral("POS."), QStringLiteral("POS"), QStringLiteral("VORLAGE"),
        QStringLiteral("LOS"), QStringLiteral("ASP"), QStringLiteral("GEWERK"),
        QStringLiteral("ANLAGE"), QStringLiteral("INDEX"), QStringLiteral("DATEINAME")
    };
    ColumnList attrCols;
    for (int i = 0; i < header.size(); ++i) {
        const QString clean = cleanHeader(header.at(i));
        if (!clean.isEmpty() && !reserved.contains(clean)) {
            attrCols.append(qMakePair(i, clean));
        }
    }
    log(QStringLiteral("Attribut-Spalten erkannt:  %1").arg(attrCols.size()));
    for (const auto& col : attrCols) {
        log(QStringLiteral("  -> Spalte %1: %2").arg(col.first + 1).arg(col.second));
    }

    // -------- Vorlagen-Index --------
    log(QString());
    log(QStringLiteral("Scanne Vorlagen-Ordner rekursiv..."));
    if (!buildVorlagenMap(m_vorlagenPath)) {
        closeLog();
        return false;
    }
    log(QStringLiteral("Vorlagen erkannt: %1").arg(m_vorlagen.size()));

    // -------- Plan --------
    log(QString());
    log(QStringLiteral("Erstelle Plan aus CSV..."));
    if (!buildPlan(lines, header, attrCols)) {
        closeLog();
        return false;
    }
    assignIndices();
    log(QStringLiteral("Plan-Eintraege: %1").arg(m_plan.size()));
    m_preview.planCount = m_plan.size();

    // -------- Loeschschutz --------
    // Liste mit Projektzeile(n): die oberste Ebene gehoert der Liste, kein Schutz.
    // Liste ohne Projektzeile:   *Deckblatt_A.dwg bleibt wie bisher stehen.
    bool hasTopLevel = false;
    for (const PlanEntry& entry : m_plan) {
        if (entry.topLevel) hasTopLevel = true;
    }
    m_preview.protectDeckblatt = !hasTopLevel;
    log(m_preview.protectDeckblatt
        ? QStringLiteral("Loeschschutz: *Deckblatt_A.dwg auf oberster Ebene bleibt erhalten "
                         "(keine Projektzeile in der Liste)")
        : QStringLiteral("Loeschschutz: AUS - die Liste enthaelt Projektzeilen, oberste Ebene "
                         "wird komplett neu erzeugt"));

    // -------- Pre-Flight: vor jedem Loeschen --------
    if (!preflight()) {
        return false;
    }

    // -------- Loesch-Vorschau --------
    log(QString());
    log(QStringLiteral("--- Loesch-Vorschau (was unter '05- Projekt Zeichnungen' "
                       "wegmuesste) ---"));
    previewDeletions();
    log(QString());
    log(QString::fromLatin1(kRuler));
    log(QStringLiteral("VORSCHAU"));
    log(QStringLiteral("  Zu loeschende Dateien:                %1").arg(m_preview.deleteCount));
    log(QStringLiteral("  Geschuetzt (*Deckblatt_A.dwg):        %1").arg(m_preview.protectedCount));
    log(QStringLiteral("  Zu erzeugende Zeichnungen:            %1").arg(m_plan.size()));
    log(QString::fromLatin1(kRuler));

    m_preview.warnings = m_errCount;
    return true;
}

// ============================================================================
// Vorschau-Lauf
// ============================================================================

void ProjectBuilder::executeDry()
{
    for (const PlanEntry& entry : m_plan) {
        log(QStringLiteral("  DRY    %1 -> %2").arg(native(entry.src), native(entry.dstPath)));
        if (entry.msgCount > 0) {
            log(QStringLiteral("         Meldungen (Slots befuellt): %1").arg(entry.msgCount));
        }
        for (const auto& a : entry.attrs) {
            log(QStringLiteral("         attr  %1 = '%2'").arg(a.first, a.second));
        }
        if (!entry.stempel.isEmpty()) {
            log(QStringLiteral("         %1 Stempel-Eintrag/Eintraege:")
                .arg(entry.stempel.size()));
            for (const AttrList& s : entry.stempel) {
                log(QStringLiteral("           [Zeile %1] OC_ANLAGE='%2'")
                    .arg(attrValue(s, QString::fromLatin1(kCsvLineTag)),
                         attrValue(s, QStringLiteral("OC_ANLAGE"))));
            }
        }
    }
}

void ProjectBuilder::runDry()
{
    log(QString());
    log(QStringLiteral("--- Plan (was passieren WUERDE) ---"));
    executeDry();
    log(QString());
    log(QStringLiteral("Dry-Run abgeschlossen. Fehler/Warnungen: %1").arg(m_errCount));
    log(QStringLiteral("Log: %1").arg(native(m_logPath)));
    closeLog();
}

void ProjectBuilder::cancelByUser()
{
    log(QStringLiteral("Abbruch durch Benutzer (Sicherheitsabfrage)."));
    closeLog();
}

// ============================================================================
// Echter Lauf
// ============================================================================

void ProjectBuilder::cleanProjectFolder()
{
    QDir root(m_zeichnungsRoot);
    if (!root.exists()) {
        QDir().mkpath(m_zeichnungsRoot);
        log(QStringLiteral("  Ordner neu angelegt: %1").arg(native(m_zeichnungsRoot)));
        return;
    }

    for (const QString& name : listFiles(m_zeichnungsRoot, QStringList())) {
        const QString full = root.absoluteFilePath(name);
        if (isProtectedDeckblatt(name)) {
            log(QStringLiteral("  PROT   %1").arg(native(full)));
            continue;
        }
        // Schreibschutz aufheben, sonst scheitert das Loeschen unter Windows
        QFile::setPermissions(full, QFile::permissions(full)
                                    | QFile::WriteOwner | QFile::WriteUser);
        if (QFile::remove(full)) {
            log(QStringLiteral("  DEL    %1").arg(native(full)));
        } else {
            log(QStringLiteral("  FEHLER Loeschen schlug fehl: %1").arg(native(full)));
            ++m_errCount;
        }
    }

    for (const QString& name : listDirs(m_zeichnungsRoot)) {
        const QString full = root.absoluteFilePath(name);
        if (QDir(full).removeRecursively()) {
            log(QStringLiteral("  RMDIR  %1").arg(native(full)));
        } else {
            log(QStringLiteral("  FEHLER RMDIR fehlgeschlagen: %1").arg(native(full)));
            ++m_errCount;
        }
    }
}

void ProjectBuilder::abortStempel(const QString& csvLine, const QString& dwgPath,
                                  const AttrList& stempelAttrs, Result& result)
{
    const QString ocAnlage = attrValue(stempelAttrs, QStringLiteral("OC_ANLAGE"));
    QString prefix = stempelPrefix(ocAnlage);
    if (prefix.isEmpty()) prefix = QStringLiteral("<kein [n]-Praefix>");

    log(QString());
    log(QString::fromLatin1(kRuler));
    log(QStringLiteral("*** ABBRUCH: Stempel nicht gefunden ***"));
    log(QStringLiteral("  CSV:           %1").arg(native(m_csvPath)));
    log(QStringLiteral("  CSV-Zeile:     %1").arg(csvLine));
    log(QStringLiteral("  DWG:           %1").arg(native(dwgPath)));
    log(QStringLiteral("  Gesuchter [n]: %1").arg(prefix));
    log(QStringLiteral("  Vollwert:      OC_ANLAGE = '%1'").arg(ocAnlage));
    log(QString::fromLatin1(kRuler));
    log(QString());
    log(QStringLiteral("Lauf abgebrochen. Bisherige DWGs bleiben erhalten."));
    log(QStringLiteral("Fehler/Warnungen bis hierhin: %1").arg(m_errCount));
    closeLog();

    result.aborted = true;
    result.abortMessage = QStringLiteral(
        "Stempel nicht gefunden!\n\n"
        "CSV-Liste:\n  %1\n\n"
        "CSV-Zeile:  %2\n\n"
        "Zu befuellende DWG:\n  %3\n\n"
        "Gesuchter Stempel-Praefix: %4\n"
        "Voller OC_ANLAGE-Wert:     %5\n\n"
        "Pruefe bitte die Erstellliste UND das Template-DWG.\n"
        "Lauf wurde abgebrochen, bisherige Dateien sind erhalten.\n\n"
        "Log: %6")
        .arg(native(m_csvPath), csvLine, native(dwgPath), prefix, ocAnlage, native(m_logPath));
}

bool ProjectBuilder::executeEntry(const PlanEntry& entry, Result& result)
{
    // Zielordner anlegen, Vorlage kopieren
    QDir().mkpath(entry.dstDir);
    if (!QFile::copy(entry.src, entry.dstPath)) {
        log(QStringLiteral("  FEHLER Kopie: %1 -> %2")
            .arg(native(entry.src), native(entry.dstPath)));
        ++result.errors;
        ++m_errCount;
        return true;
    }
    // Eine schreibgeschuetzte Vorlage darf die Kopie nicht schreibgeschuetzt machen
    QFile::setPermissions(entry.dstPath, QFile::permissions(entry.dstPath)
                                         | QFile::WriteOwner | QFile::WriteUser);
    log(QStringLiteral("  COPY   %1").arg(native(entry.dstPath)));

    if (entry.attrs.isEmpty() && entry.stempel.isEmpty()) {
        log(QStringLiteral("         (keine Attributwerte gesetzt)"));
        ++result.copiedOnly;
        return true;
    }

    // Zeichnung als Side-Database oeffnen: kein Dokument, kein Bildaufbau
    AcDbDatabase* pDb = new AcDbDatabase(false, true);
    const std::wstring wpath = entry.dstPath.toStdWString();
    if (pDb->readDwgFile(wpath.c_str()) != Acad::eOk) {
        delete pDb;
        log(QStringLiteral("  FEHLER Konnte nicht oeffnen: %1").arg(native(entry.dstPath)));
        ++result.errors;
        ++m_errCount;
        return true;
    }
    // Datei vollstaendig einlesen und freigeben, sonst laesst sie sich nicht
    // unter demselben Namen speichern
    pDb->closeInput(true);
    pDb->setRetainOriginalThumbnailBitmap(true);

    bool keepGoing = true;
    bool saved = false;

    // --- Anlagen-Header-Attribute (Nicht-Stempel-Bloecke) ---
    int blockCount = 0;
    int setCount = 0;
    applyHeaderAttributes(pDb, entry.attrs, blockCount, setCount);
    log(QStringLiteral("         Header: %1 Block(e), %2 Attribut(e) gesetzt")
        .arg(blockCount).arg(setCount));
    for (const auto& a : entry.attrs) {
        log(QStringLiteral("         attr  %1 = '%2'").arg(a.first, a.second));
    }

    // --- Stempel-Eintraege ---
    for (const AttrList& stempelEntry : entry.stempel) {
        const QString csvLine = attrValue(stempelEntry, QString::fromLatin1(kCsvLineTag));
        const QString ocAnlage = attrValue(stempelEntry, QStringLiteral("OC_ANLAGE"));
        const QString prefix = stempelPrefix(ocAnlage);

        int hitCount = 0;
        int stempelSet = 0;
        if (!prefix.isEmpty()) {
            applyStempel(pDb, stempelEntry, prefix, hitCount, stempelSet);
        } else {
            log(QStringLiteral("  FEHLER SET-STEMPEL: kein [n]-Praefix in '%1'").arg(ocAnlage));
        }

        if (hitCount == 0) {
            // Stand sichern, dann den Lauf abbrechen
            saved = (pDb->saveAs(wpath.c_str()) == Acad::eOk);
            abortStempel(csvLine, entry.dstPath, stempelEntry, result);
            keepGoing = false;
            break;
        }
        log(QStringLiteral("         Stempel [Zeile %1] '%2' -> %3 Treffer, "
                           "%4 Attribut(e) gesetzt")
            .arg(csvLine, ocAnlage).arg(hitCount).arg(stempelSet));
    }

    if (keepGoing) {
        if (pDb->saveAs(wpath.c_str()) == Acad::eOk) {
            ++result.created;
        } else {
            log(QStringLiteral("  FEHLER Speichern schlug fehl: %1").arg(native(entry.dstPath)));
            ++result.errors;
            ++m_errCount;
        }
    }
    Q_UNUSED(saved);

    delete pDb;
    return keepGoing;
}

int ProjectBuilder::sweep()
{
    int count = 0;
    for (const char* pattern : {"*.bak", "*.dwl", "*.dwl2"}) {
        const QStringList files = findFilesRecursive(
            m_zeichnungsRoot, QStringList() << QString::fromLatin1(pattern));
        for (const QString& f : files) {
            if (QFile::remove(f)) {
                log(QStringLiteral("  SWEEP  %1").arg(native(f)));
                ++count;
            } else {
                log(QStringLiteral("  FEHLER SWEEP konnte nicht loeschen: %1").arg(native(f)));
            }
        }
    }
    return count;
}

ProjectBuilder::Result ProjectBuilder::runReal()
{
    Result result;

    // -------- Loeschen --------
    log(QString());
    log(QStringLiteral("--- Saeuberung ---"));
    cleanProjectFolder();

    // -------- Erzeugen --------
    log(QString());
    log(QStringLiteral("--- Aufbau ---"));
    const int total = m_plan.size();
    for (int i = 0; i < total; ++i) {
        emit progress(i + 1, total, QFileInfo(m_plan.at(i).dstPath).fileName());
        if (!executeEntry(m_plan.at(i), result)) {
            // Abbruch wegen fehlendem Stempel, Log ist bereits geschlossen
            result.warnings = m_errCount;
            return result;
        }
    }

    // -------- Sweep --------
    log(QString());
    log(QStringLiteral("--- Sweep (*.bak, *.dwl, *.dwl2) ---"));
    result.swept = sweep();

    // -------- Zusammenfassung --------
    result.warnings = m_errCount;
    log(QString());
    log(QString::fromLatin1(kRuler));
    log(QStringLiteral("ZUSAMMENFASSUNG"));
    log(QStringLiteral("  Erstellt mit Attributen:      %1").arg(result.created));
    log(QStringLiteral("  Nur kopiert (ohne Attribute): %1").arg(result.copiedOnly));
    log(QStringLiteral("  Fehler beim Aufbau:           %1").arg(result.errors));
    log(QStringLiteral("  Sweep entfernt:               %1").arg(result.swept));
    log(QStringLiteral("  Fehler/Warnungen insgesamt:   %1").arg(m_errCount));
    log(QStringLiteral("  Log: %1").arg(native(m_logPath)));
    log(QString::fromLatin1(kRuler));
    closeLog();

    return result;
}

} // namespace BatchProcessing
