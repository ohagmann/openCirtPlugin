/**
 * @file SensorKeywordLoader.h
 * @brief Laedt Sensor-Keywords aus REFERENZEN/sensor.csv
 * 
 * Zeile 1 wird ignoriert (Header), Spalte 1 ab Zeile 2 = Keywords.
 * Abgleich: OC_PRODUKT.contains(keyword, case-insensitive)
 */

#ifndef SENSORKEYWORDLOADER_H
#define SENSORKEYWORDLOADER_H

#include <QString>
#include <QStringList>

namespace BatchProcessing {

class SensorKeywordLoader
{
public:
    SensorKeywordLoader() = default;

    /// Laedt Keywords aus CSV-Datei
    /// @param csvPath Pfad zu REFERENZEN/sensor.csv
    /// @return true wenn mindestens ein Keyword geladen wurde
    bool load(const QString& csvPath);

    /// Prueft ob OC_PRODUKT einen der Keywords enthaelt (case-insensitive contains)
    bool isSensor(const QString& ocProdukt) const;

    int keywordCount() const { return m_keywords.size(); }
    const QStringList& keywords() const { return m_keywords; }

private:
    QStringList m_keywords;
};

} // namespace BatchProcessing

#endif // SENSORKEYWORDLOADER_H
