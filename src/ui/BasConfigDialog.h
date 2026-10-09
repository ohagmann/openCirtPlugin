/**
 * @file BasConfigDialog.h
 * @brief "BAS konfigurieren": Aufbau des BAS als Tabelle bearbeiten und als
 *        BAS.csv im Format des Plugins speichern
 */
#ifndef BP_BAS_CONFIG_DIALOG_H
#define BP_BAS_CONFIG_DIALOG_H

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
/// Speichern schreibt die BAS.csv so, wie parseBasCsv sie liest. Die
/// Datei bleibt eine schlichte CSV und laesst sich weiter von Hand pflegen.
class BasConfigDialog : public QDialog {
    Q_OBJECT
public:
    explicit BasConfigDialog(const QString& basPath, QWidget* parent = nullptr);

    /// Segmente, wie sie beim Speichern geschrieben wurden (leer bei Abbruch)
    QVector<OcBasSegment> segments() const { return m_saved; }

    /// Was beim Oeffnen geschah: Datei gelesen, angelegt oder ohne Segmente
    QString status() const { return m_status; }

private:
    void addAttribut();
    void addText();
    void addTrennzeichen();
    void removeRow();
    void moveUp();
    void moveDown();
    void save();

    void addRow(OcBasSegment::Type type, const QString& value, int at = -1);
    void readRow(int row, OcBasSegment& seg) const;
    void writeRow(int row, const OcBasSegment& seg);
    QVector<OcBasSegment> collect() const;
    void updatePreview();

    static QVector<OcBasSegment> defaultSegments();

    QString m_basPath;
    QString m_status;
    QTableWidget* m_table = nullptr;
    QLabel* m_preview = nullptr;
    QLabel* m_fileLabel = nullptr;
    QVector<OcBasSegment> m_saved;
};

} // namespace BatchProcessing

#endif // BP_BAS_CONFIG_DIALOG_H
