#include "windows_fix.h"
/**
 * @file SensorKeywordLoader.cpp
 * @brief Implementation - Laedt Sensor-Keywords aus REFERENZEN/sensor.csv
 */

#include "SensorKeywordLoader.h"

#include <QFile>
#include <QTextStream>
#include <QStringConverter>
#include <QRegularExpression>
#include <QDebug>

namespace BatchProcessing {

bool SensorKeywordLoader::load(const QString& csvPath)
{
    m_keywords.clear();

    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        qWarning() << "[SensorKeywords] Kann nicht oeffnen:" << csvPath;
        return false;
    }

    QTextStream in(&file);
    // sensor.csv wird vom Anwender in LibreOffice gepflegt -> UTF-8 (Default)
    // parseExtractedCsv liest die LISP-CSVs als Latin1, aber Qt konvertiert
    // intern alles zu Unicode -> contains-Vergleich funktioniert encoding-unabhaengig
    in.setEncoding(QStringConverter::Utf8);
    bool firstLine = true;

    while (!in.atEnd()) {
        QString line = in.readLine().trimmed();
        if (firstLine) { firstLine = false; continue; }
        if (line.isEmpty()) continue;
        if (line.startsWith('#')) continue; // Kommentare ueberspringen

        // Erste Spalte extrahieren (Semikolon oder Komma als Trenner)
        QString keyword = line.split(QRegularExpression("[;,]")).first().trimmed();
        if (!keyword.isEmpty()) {
            m_keywords.append(keyword);
        }
    }

    file.close();

    qDebug() << "[SensorKeywords]" << m_keywords.size()
             << "Keywords geladen aus" << csvPath;
    return !m_keywords.isEmpty();
}

bool SensorKeywordLoader::isSensor(const QString& ocProdukt) const
{
    if (ocProdukt.trimmed().isEmpty()) return false;

    for (const QString& kw : m_keywords) {
        if (ocProdukt.contains(kw, Qt::CaseInsensitive))
            return true;
    }
    return false;
}

} // namespace BatchProcessing
