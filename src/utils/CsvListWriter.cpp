#include "windows_fix.h"
/**
 * @file CsvListWriter.cpp
 * @brief Implementation - schreibt eine Liste als CSV-Datei
 */

#include "CsvListWriter.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QDebug>

namespace BatchProcessing {

namespace {

const QChar kSeparator = QLatin1Char(';');
const char* const kLineEnd = "\r\n";
const char* const kUtf8Bom = "\xEF\xBB\xBF";

}  // namespace

// ============================================================================
// Public API
// ============================================================================

bool CsvListWriter::open(const QString& outputPath, const QStringList& header)
{
    m_lastError.clear();
    m_rows.clear();
    m_outputPath = outputPath;
    m_header = header;

    if (m_outputPath.isEmpty()) {
        m_lastError = "Kein Ausgabepfad gesetzt";
        return false;
    }

    // Ausgabeverzeichnis sicherstellen
    const QString dir = QFileInfo(m_outputPath).absolutePath();
    if (!QDir().mkpath(dir)) {
        m_lastError = QString("Kann Ausgabeordner nicht anlegen: %1").arg(dir);
        return false;
    }

    return true;
}

void CsvListWriter::addRow(const QStringList& cellValues)
{
    m_rows.append(cellValues);
}

bool CsvListWriter::save()
{
    if (m_outputPath.isEmpty()) {
        m_lastError = "Kein Ausgabepfad gesetzt";
        return false;
    }

    QByteArray data(kUtf8Bom);
    data += joinRow(m_header).toUtf8();
    data += kLineEnd;
    for (const QStringList& row : m_rows) {
        data += joinRow(row).toUtf8();
        data += kLineEnd;
    }

    // Ohne QIODevice::Text: das Zeilenende steht bereits im Puffer und soll
    // unter Windows nicht ein zweites Mal umgesetzt werden.
    QFile file(m_outputPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        m_lastError = QString("Kann Datei nicht schreiben: %1 (%2)")
                      .arg(m_outputPath, file.errorString());
        return false;
    }

    const qint64 written = file.write(data);
    file.close();

    if (written != data.size()) {
        m_lastError = QString("Datei unvollstaendig geschrieben: %1").arg(m_outputPath);
        return false;
    }

    qDebug() << "[CsvWriter] CSV gespeichert:" << m_outputPath
             << "Zeilen:" << m_rows.size();
    return true;
}

QString CsvListWriter::escapeField(const QString& field)
{
    const bool needsQuotes = field.contains(kSeparator)
                          || field.contains(QLatin1Char('"'))
                          || field.contains(QLatin1Char('\n'))
                          || field.contains(QLatin1Char('\r'));
    if (!needsQuotes) return field;

    QString escaped = field;
    escaped.replace(QLatin1Char('"'), QLatin1String("\"\""));
    return QLatin1Char('"') + escaped + QLatin1Char('"');
}

// ============================================================================
// Private Helpers
// ============================================================================

QString CsvListWriter::joinRow(const QStringList& cells)
{
    QStringList escaped;
    escaped.reserve(cells.size());
    for (const QString& cell : cells) {
        escaped << escapeField(cell);
    }
    return escaped.join(kSeparator);
}

} // namespace BatchProcessing
