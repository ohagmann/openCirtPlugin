/**
 * @file OpenCirtWindow.h
 * @brief Fenster von openCirt: Bedienflaeche (OpenCirtTab) oben, Protokoll unten
 *
 * Das Protokoll ist die einzige Ausgabe des Plugins. Der Tab schreibt ueber
 * sein Signal logMessage hinein; Dialoge zeigen nur, was eine Antwort braucht,
 * und die Zusammenfassung eines Laufs.
 */

#ifndef OPENCIRTWINDOW_H
#define OPENCIRTWINDOW_H

// WICHTIG: Platform header MUSS zuerst kommen!
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

#include <QMainWindow>
#include <QString>
#include <QVector>

QT_BEGIN_NAMESPACE
class QTextEdit;
QT_END_NAMESPACE

namespace BatchProcessing {

class OpenCirtTab;

class OpenCirtWindow : public QMainWindow {
    Q_OBJECT

public:
    explicit OpenCirtWindow(QWidget* parent = nullptr);
    ~OpenCirtWindow() override;

    OpenCirtTab* tab() const { return m_tab; }

    /// Thema von BricsCAD uebernehmen und das Protokoll neu einfaerben.
    /// Wird beim Oeffnen des Fensters aufgerufen, damit ein Wechsel zwischen
    /// hellem und dunklem Thema uebernommen wird.
    void applyTheme();

private slots:
    void onClearLog();
    void onExportLog();
    void showAbout();

private:
    void createMenuBar();
    void createCentralWidget();

    /// Zeile ins Protokoll; type: INFO, SUCCESS, WARNING, ERROR
    void logMessage(const QString& message, const QString& type);

    /// HTML einer Protokollzeile im aktuellen Thema
    QString formatLogEntry(const QString& timestamp,
                           const QString& type,
                           const QString& message) const;

    /// Protokoll aus m_logEntries im aktuellen Thema neu aufbauen
    void renderLog();

    OpenCirtTab* m_tab = nullptr;
    QTextEdit* m_logTextEdit = nullptr;

    /// Rohfassung der Protokollzeilen. Noetig, weil die Farben als HTML im
    /// Text stecken: ohne die Rohdaten liessen sich bereits geschriebene
    /// Zeilen bei einem Themenwechsel nicht neu einfaerben.
    struct LogEntry {
        QString timestamp;
        QString type;
        QString message;
    };
    QVector<LogEntry> m_logEntries;
};

} // namespace BatchProcessing

#endif // OPENCIRTWINDOW_H
