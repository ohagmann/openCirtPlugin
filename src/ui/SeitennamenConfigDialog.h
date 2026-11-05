/**
 * @file SeitennamenConfigDialog.h
 * @brief "Seitennamen konfigurieren": Titel der PDF-Lesezeichen als Segmentfolge
 *        bearbeiten und als Seitennamen.csv im Format des Plugins speichern
 */
#ifndef OPENCIRT_SEITENNAMEN_CONFIG_DIALOG_H
#define OPENCIRT_SEITENNAMEN_CONFIG_DIALOG_H

#include <QDialog>
#include <QString>
#include <QVector>
#include "../core/OpenCirtEngine.h"

QT_BEGIN_NAMESPACE
class QTableWidget;
class QLabel;
QT_END_NAMESPACE

namespace BatchProcessing {

/// Tabelle der Segmente (Art: Text oder Attribut, Wert) mit Vorschau.
/// Speichern schreibt die Seitennamen.csv so, wie parseSeitennamenCsv sie
/// liest: jede Zeile des Dialogs eine Zelle in Zeile 1, getrennt durch
/// Semikolon. Was der Anwender darueber hinaus in die Datei schreibt, ist
/// seine freie Anleitung. Die Datei wird angelegt, wenn sie noch fehlt.
/// Anders als der BAS-Dialog gibt es keine Up/Down-Knoepfe, sondern "vor"
/// und "zurueck".
class SeitennamenConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit SeitennamenConfigDialog(const QString& dateiPfad, QWidget* parent = nullptr);

    /// Segmente, wie sie beim Speichern geschrieben wurden (leer bei Abbruch)
    QVector<OcSeitennamenSegment> segments() const { return m_saved; }

    /// Was beim Oeffnen geschah: Datei gelesen, angelegt oder ohne Segmente
    QString status() const { return m_status; }

private:
    void addAttribut();
    void addText();
    void removeRow();
    void moveVor();      ///< Segment eine Stelle nach vorn (links) schieben
    void moveZurueck();  ///< Segment eine Stelle zurueck (rechts) schieben
    void save();

    void addRow(OcSeitennamenSegment::Type type, const QString& value, int at = -1);
    void readRow(int row, OcSeitennamenSegment& seg) const;
    void writeRow(int row, const OcSeitennamenSegment& seg);
    QVector<OcSeitennamenSegment> collect() const;
    void updatePreview();

    static QVector<OcSeitennamenSegment> defaultSegments();

    QString m_dateiPfad;
    QString m_status;
    QTableWidget* m_table = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_fileLabel = nullptr;
    QVector<OcSeitennamenSegment> m_saved;
};

} // namespace BatchProcessing

#endif // OPENCIRT_SEITENNAMEN_CONFIG_DIALOG_H
