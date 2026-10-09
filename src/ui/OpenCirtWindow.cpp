#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file OpenCirtWindow.cpp
 * @brief Fenster von openCirt: Bedienflaeche oben, Protokoll unten
 */

// WICHTIG: Platform header MUSS zuerst kommen!
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

#include "OpenCirtWindow.h"
#include "OpenCirtTab.h"
#include "Theming.h"

#include <QDateTime>
#include <QFile>
#include <QFileDialog>
#include <QGroupBox>
#include <QMenuBar>
#include <QMessageBox>
#include <QSettings>
#include <QTextCursor>
#include <QTextEdit>
#include <QTextStream>
#include <QVBoxLayout>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "unbekannt"
#endif

namespace BatchProcessing {

OpenCirtWindow::OpenCirtWindow(QWidget* parent)
    : QMainWindow(parent)
{
    // Zuerst das Thema setzen: setRole() waehrend des Aufbaus rechnet seine
    // Farben aus der Palette, die muss also schon stimmen.
    Theming::apply(this);

    setWindowTitle("openCirt " PLUGIN_VERSION);
    createMenuBar();
    createCentralWidget();

    QSettings settings("openCirt", "openCirt");
    const QByteArray geometry = settings.value("geometry").toByteArray();
    if (geometry.isEmpty() || !restoreGeometry(geometry)) {
        resize(900, 760);
    }

    // Nach dem Aufbau erneut, damit Stil und Rollen auch die eben erzeugten
    // Widgets erreichen.
    applyTheme();

    m_tab->restoreLastProject();
}

OpenCirtWindow::~OpenCirtWindow() {
    QSettings settings("openCirt", "openCirt");
    settings.setValue("geometry", saveGeometry());
}

void OpenCirtWindow::createMenuBar() {
    QMenuBar* bar = menuBar();

    QMenu* fileMenu = bar->addMenu("&Datei");
    fileMenu->addAction("&Beenden", this, &QWidget::close);

    QMenu* logMenu = bar->addMenu("&Protokoll");
    logMenu->addAction("&Leeren", this, &OpenCirtWindow::onClearLog);
    logMenu->addAction("&Exportieren...", this, &OpenCirtWindow::onExportLog);

    QMenu* helpMenu = bar->addMenu("&Hilfe");
    helpMenu->addAction("&Ueber openCirt...", this, &OpenCirtWindow::showAbout);
}

void OpenCirtWindow::createCentralWidget() {
    QWidget* central = new QWidget(this);
    setCentralWidget(central);
    QVBoxLayout* layout = new QVBoxLayout(central);

    m_tab = new OpenCirtTab(this);
    connect(m_tab, &OpenCirtTab::logMessage, this, &OpenCirtWindow::logMessage);
    layout->addWidget(m_tab, 0);

    // Das Protokoll bekommt den restlichen Platz
    QGroupBox* logGroup = new QGroupBox("Protokoll");
    QVBoxLayout* logLayout = new QVBoxLayout(logGroup);
    m_logTextEdit = new QTextEdit();
    m_logTextEdit->setReadOnly(true);
    m_logTextEdit->setMinimumHeight(150);
    m_logTextEdit->setFont(QFont("Consolas", 9));
    logLayout->addWidget(m_logTextEdit);
    layout->addWidget(logGroup, 1);
}

// ============================================================================
// Protokoll
// ============================================================================

QString OpenCirtWindow::formatLogEntry(const QString& timestamp,
                                       const QString& type,
                                       const QString& message) const
{
    QColor farbe = palette().color(QPalette::Text);
    if (type == "ERROR" || type == "ERR")         farbe = Theming::errorText(this);
    else if (type == "SUCCESS")                   farbe = Theming::successText(this);
    else if (type == "WARN" || type == "WARNING") farbe = Theming::warningText(this);

    return QString("<span style='color:%1'>[%2]</span> "
                   "<span style='color:%3'>%4</span>")
           .arg(Theming::mutedText(this).name(),
                timestamp,
                farbe.name(),
                message.toHtmlEscaped());
}

void OpenCirtWindow::logMessage(const QString& message, const QString& type) {
    const QString timestamp = QDateTime::currentDateTime().toString("hh:mm:ss");

    // Rohfassung merken, damit die Zeile bei einem Themenwechsel neu
    // eingefaerbt werden kann. Deckel gegen unbegrenztes Wachsen.
    m_logEntries.append({timestamp, type, message});
    if (m_logEntries.size() > 20000) m_logEntries.remove(0, 5000);

    m_logTextEdit->append(formatLogEntry(timestamp, type, message));

    // immer am Ende bleiben
    QTextCursor cursor = m_logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logTextEdit->setTextCursor(cursor);
}

void OpenCirtWindow::renderLog() {
    // Die Farben stecken als HTML im Text - beim Themenwechsel muss das
    // Protokoll daher komplett neu aufgebaut werden.
    m_logTextEdit->clear();
    for (const LogEntry& e : m_logEntries) {
        m_logTextEdit->append(formatLogEntry(e.timestamp, e.type, e.message));
    }
    QTextCursor cursor = m_logTextEdit->textCursor();
    cursor.movePosition(QTextCursor::End);
    m_logTextEdit->setTextCursor(cursor);
}

void OpenCirtWindow::applyTheme() {
    Theming::apply(this);
    renderLog();
}

void OpenCirtWindow::onClearLog() {
    m_logEntries.clear();
    m_logTextEdit->clear();
}

void OpenCirtWindow::onExportLog() {
    const QString fileName = QFileDialog::getSaveFileName(this,
        "Protokoll exportieren",
        QString("openCirt_Protokoll_%1.txt")
            .arg(QDateTime::currentDateTime().toString("yyyyMMdd_HHmmss")),
        "Textdateien (*.txt)");
    if (fileName.isEmpty()) return;

    QFile file(fileName);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logMessage(QString("Protokoll konnte nicht geschrieben werden: %1")
                   .arg(QDir::toNativeSeparators(fileName)), "ERROR");
        return;
    }
    QTextStream stream(&file);
    stream << m_logTextEdit->toPlainText();
    file.close();
    logMessage(QString("Protokoll exportiert: %1").arg(QDir::toNativeSeparators(fileName)), "INFO");
}

void OpenCirtWindow::showAbout() {
    QMessageBox::about(this, "Ueber openCirt",
        "openCirt " PLUGIN_VERSION "\n"
        "GA-Planungsautomatisierung fuer BricsCAD V26 (VDI 3814)\n\n"
        "Befehle: OPENCIRT, OC\n\n"
        "Qt 6 und BRX SDK - Lizenz: BSL 1.1");
}

} // namespace BatchProcessing
