/**
 * @file CsvListWriter.h
 * @brief Schreibt eine Liste als CSV-Datei
 *
 * Ausgabeformat der Sensorliste und der Datenpunkt-/IO-Belegungsliste:
 *   - UTF-8 mit BOM (damit auch Excel die Umlaute beim Doppelklick erkennt)
 *   - Trennzeichen Semikolon, wie bei den uebrigen CSV-Dateien des Projekts
 *   - Zeilenende CRLF, auf jedem Betriebssystem gleich
 *   - Zeile 1 ist die Kopfzeile, danach eine Zeile je Eintrag
 *   - Felder mit Semikolon, Anfuehrungszeichen oder Zeilenumbruch stehen in
 *     Anfuehrungszeichen; ein Anfuehrungszeichen im Feld wird verdoppelt
 *
 * Abhaengigkeiten: nur Qt6
 */

#ifndef CSVLISTWRITER_H
#define CSVLISTWRITER_H

#include <QString>
#include <QStringList>

namespace BatchProcessing {

class CsvListWriter
{
public:
    CsvListWriter() = default;
    ~CsvListWriter() = default;

    /// Legt die Ausgabedatei fest und uebernimmt die Kopfzeile.
    /// Geschrieben wird erst mit save().
    /// @param outputPath  Pfad der CSV-Datei (Ordner wird bei Bedarf angelegt)
    /// @param header      Spaltenueberschriften
    /// @return true bei Erfolg
    bool open(const QString& outputPath, const QStringList& header);

    /// Fuegt eine Datenzeile hinzu
    void addRow(const QStringList& cellValues);

    /// Schreibt die Datei. Eine vorhandene Datei wird ersetzt.
    /// @return true bei Erfolg
    bool save();

    /// Anzahl der Datenzeilen (ohne Kopfzeile)
    int rowCount() const { return static_cast<int>(m_rows.size()); }

    /// Letzter Fehler
    QString lastError() const { return m_lastError; }

    /// Ein Feld fuer die Ausgabe aufbereiten (Anfuehrungszeichen bei Bedarf)
    static QString escapeField(const QString& field);

private:
    static QString joinRow(const QStringList& cells);

    QString m_outputPath;
    QString m_lastError;
    QStringList m_header;
    QList<QStringList> m_rows;
};

} // namespace BatchProcessing

#endif // CSVLISTWRITER_H
