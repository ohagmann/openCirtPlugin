#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file OpenCirtTab.cpp
 * @brief openCirt Tab Implementation - GA-Automation Orchestrator
 * @version 1.0.0
 * 
 * Reference: OpenCirt_Tab_TechnicalSpec_v1.1
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
#include "dblayout.h"   // AcDbLayout, AcDbDatabase, AcDbDictionary for side-DB layout reading
#include "dbsymtb.h"   // AcDbBlockTable, AcDbBlockTableRecord for ModelSpace iteration
#include "dbents.h"    // AcDbBlockReference, AcDbAttribute for reading block attributes

#include "OpenCirtTab.h"
#include "Theming.h"
#include "BasConfigDialog.h"
#include "../utils/SensorKeywordLoader.h"
#include "../utils/CsvListWriter.h"
#include "../core/ProjectBuilder.h"
#include "../core/OpenCirtEngine.h"

// Qt Headers
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QCheckBox>
#include <QPushButton>
#include <QLabel>
#include <QProgressBar>
#include <QMessageBox>
#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QFile>
#include <QTextStream>
#include <QProcess>
#include <QJsonDocument>
#include <QJsonObject>
#include <QDateTime>
#include <QStandardPaths>
#include <QDebug>
#include <QApplication>
#include <QRegularExpression>
#include <QTimer>
#include <QSet>
#include <QDialog>
#include <QDialogButtonBox>
#include <QLineEdit>
#include <QFrame>
#include <QListWidget>
#include <QTreeWidget>
#include <QHeaderView>
#include <QAbstractItemView>
#include <QFileDialog>
#include <QSettings>
#include <QElapsedTimer>
#include <QCryptographicHash>
#include <functional>

// BRX result codes
#ifndef RTNORM
#define RTNORM   5100
#endif
#ifndef RTERROR
#define RTERROR  -5001
#endif
#ifndef RTSTR
#define RTSTR    5005
#endif
#ifndef RTNONE
#define RTNONE   5000
#endif
#ifndef RTSHORT
#define RTSHORT  5003
#endif

namespace BatchProcessing {

namespace {

/// Gemeinsamer Rahmen der Sammelmeldungen: Kopftext, eine scrollbare Ansicht,
/// optionaler Fusstext, Buttonleiste. Der Dialog ist skalierbar und traegt
/// einen Groessengriff, damit die Buttons unabhaengig von der Zahl der
/// Eintraege erreichbar bleiben - eine QMessageBox waechst mit ihrem Text und
/// schiebt sie bei langen Listen unter den Bildschirmrand.
///
/// makeView erzeugt die Ansicht mit dem Dialog als Parent. Rueckgabe true bei
/// Bestaetigung; ohne askContinue ist der Dialog eine reine Quittierung und
/// liefert immer true.
static bool runWarningDialog(QWidget* parent,
                             const QString& title,
                             const QString& intro,
                             const std::function<QWidget*(QWidget*)>& makeView,
                             const QString& outro,
                             bool askContinue) {
    QDialog dlg(parent);
    dlg.setWindowTitle(title);
    dlg.setSizeGripEnabled(true);
    dlg.resize(760, 460);

    QVBoxLayout* layout = new QVBoxLayout(&dlg);

    QLabel* head = new QLabel(intro, &dlg);
    head->setWordWrap(true);
    layout->addWidget(head);

    layout->addWidget(makeView(&dlg), 1);

    if (!outro.isEmpty()) {
        QLabel* foot = new QLabel(outro, &dlg);
        foot->setWordWrap(true);
        layout->addWidget(foot);
    }

    QDialogButtonBox* buttons = new QDialogButtonBox(&dlg);
    if (askContinue) {
        buttons->addButton("Fortfahren", QDialogButtonBox::AcceptRole);
        buttons->addButton("Abbrechen", QDialogButtonBox::RejectRole);
    } else {
        buttons->addButton("OK", QDialogButtonBox::AcceptRole);
    }
    QObject::connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
    QObject::connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
    layout->addWidget(buttons);

    return dlg.exec() == QDialog::Accepted;
}

/// Sammelmeldung mit flacher Liste (ein Eintrag je Zeile).
static bool showListWarning(QWidget* parent,
                            const QString& title,
                            const QString& intro,
                            const QStringList& entries,
                            const QString& outro = QString(),
                            bool askContinue = false) {
    return runWarningDialog(parent, title, intro,
        [&entries](QWidget* dlg) -> QWidget* {
            QListWidget* list = new QListWidget(dlg);
            list->addItems(entries);
            list->setSelectionMode(QAbstractItemView::NoSelection);
            list->setTextElideMode(Qt::ElideNone);
            list->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            return list;
        },
        outro, askContinue);
}

/// Eine Gruppe der Baum-Sammelmeldung: Kopfzeile plus Unterzeilen.
struct WarningGroup {
    QString head;
    QStringList children;
};

/// Sammelmeldung als Baum: je Gruppe ein Knoten mit Anzahl der Unterzeilen,
/// die Unterzeilen als Kinder, alles aufgeklappt. Oben sieht man, WAS fehlt,
/// darunter, WO - statt aller Fundstellen in einer einzigen langen Zeile.
static bool showTreeWarning(QWidget* parent,
                            const QString& title,
                            const QString& intro,
                            const QVector<WarningGroup>& groups,
                            const QString& childNoun,      // z.B. "Zeichnung"
                            const QString& outro = QString(),
                            bool askContinue = false) {
    return runWarningDialog(parent, title, intro,
        [&groups, &childNoun](QWidget* dlg) -> QWidget* {
            QTreeWidget* tree = new QTreeWidget(dlg);
            tree->setColumnCount(1);
            tree->setHeaderHidden(true);
            tree->setRootIsDecorated(true);
            tree->setSelectionMode(QAbstractItemView::NoSelection);
            tree->setTextElideMode(Qt::ElideNone);
            tree->setHorizontalScrollBarPolicy(Qt::ScrollBarAsNeeded);
            tree->header()->setStretchLastSection(false);
            tree->header()->setSectionResizeMode(QHeaderView::ResizeToContents);
            for (const WarningGroup& g : groups) {
                const int n = g.children.size();
                QTreeWidgetItem* node = new QTreeWidgetItem(tree);
                node->setText(0, g.head + "  " + QChar(0x2014) + "  "
                                 + QString::number(n) + " "
                                 + (n == 1 ? childNoun : childNoun + "en"));
                QFont bold = node->font(0);
                bold.setBold(true);
                node->setFont(0, bold);
                for (const QString& c : g.children) {
                    QTreeWidgetItem* leaf = new QTreeWidgetItem(node);
                    leaf->setText(0, c);
                }
            }
            tree->expandAll();
            return tree;
        },
        outro, askContinue);
}

/// Funktionsspalten der GA-FL-Blaetter in Blattreihenfolge. Index 0 (OC_INTEG)
/// traegt die Integrationsart, keinen Zaehlwert. Dieselbe Liste bestimmt die
/// Spaltenreihenfolge der Summen-CSVs und die Attributnamen beim Lesen der
/// Blaetter (<Basis>_DP_<Zeile>).
static const QStringList kFuncBases = {
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
    // OC_4_1_1 entfernt: ist Kommentarspalte in ODS, keine GA-Funktion
};

/// Plankopf-Attribute, die ExtractDP.lsp aus einer Quellzeichnung liefert.
/// Beim Lesen der GA-FL-Blaetter (Phase 3) wird auf dieselbe Auswahl
/// gefiltert, damit die Summenblaetter genau die Felder erhalten wie bisher.
static const char* const kPlankopfKeys[] = {
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

/// Funktionszaehler eines Datenpunkts aufaddieren. Quelle sind die Zellen des
/// GA-FL-Blatts (DataPoint::funktionsWerte, Schluessel = Funktionsbasis).
/// Zaehlregel wie bisher: numerischer Wert > 0 zaehlt mit seinem Wert, jeder
/// andere nicht-leere Eintrag zaehlt 1. Index 0 (OC_INTEG) wird uebersprungen.
static void addFuncCounts(QVector<int>& funcCounts, const DataPoint& dp,
                          const QStringList& funcBases) {
    for (int fc = 1; fc < funcBases.size() && fc < funcCounts.size(); ++fc) {
        const QString cellVal = dp.funktionsWerte.value(funcBases[fc]).trimmed();
        if (cellVal.isEmpty()) continue;
        bool ok;
        int numVal = cellVal.toInt(&ok);
        funcCounts[fc] += (ok && numVal > 0) ? numVal : 1;
    }
}

}  // namespace


// ============================================================================
// OpenCirtConfig - Project Configuration
// ============================================================================

bool OpenCirtConfig::loadFromFile(const QString& projectRoot) {
    QString path = projectRoot + "/" + REFERENZEN_DIR + "/" + CONFIG_FILE;
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        return false;
    }
    
    QJsonDocument doc = QJsonDocument::fromJson(file.readAll());
    file.close();
    
    if (doc.isNull() || !doc.isObject()) {
        return false;
    }
    
    QJsonObject obj = doc.object();
    includeBmk = obj.value("includeBmk").toBool(true);
    includeBas = obj.value("includeBas").toBool(true);
    lastProjectRoot = obj.value("lastProjectRoot").toString();
    
    return true;
}

bool OpenCirtConfig::saveToFile(const QString& projectRoot) const {
    QString dirPath = projectRoot + "/" + REFERENZEN_DIR;
    QDir dir;
    if (!dir.mkpath(dirPath)) {
        return false;
    }
    
    QString path = dirPath + "/" + CONFIG_FILE;
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        return false;
    }
    
    QJsonObject obj;
    obj["includeBmk"] = includeBmk;
    obj["includeBas"] = includeBas;
    obj["lastProjectRoot"] = lastProjectRoot;
    obj["version"] = "1.1";
    obj["lastModified"] = QDateTime::currentDateTime().toString(Qt::ISODate);
    
    QJsonDocument doc(obj);
    file.write(doc.toJson(QJsonDocument::Indented));
    file.close();
    
    return true;
}

// ============================================================================
// Constructor & UI Setup
// ============================================================================

OpenCirtTab::OpenCirtTab(QWidget* parent)
    : QWidget(parent)
{
    setupUi();
    updateButtonStates();
    
    // PDF publish completion polling timer (marker-based, via /b batch instance)
    m_pdfDoneTimer = new QTimer(this);
    m_pdfDoneTimer->setInterval(2000);
    connect(m_pdfDoneTimer, &QTimer::timeout, this, &OpenCirtTab::onPdfDonePollTimer);
}

void OpenCirtTab::setupUi() {
    auto* mainLayout = new QVBoxLayout(this);
    
    // --- Projektordner ---
    auto* projectGroup = new QGroupBox("Projekt");
    auto* projectLayout = new QHBoxLayout(projectGroup);
    auto* projectLabel = new QLabel("Projektordner:");
    m_projectEdit = new QLineEdit();
    m_projectEdit->setPlaceholderText(
        "Ordner des openCirt-Projekts (enthaelt '01- Referenzen', '04- Vorlagen', ...)");
    m_btnBrowse = new QPushButton("Durchsuchen...");
    m_btnBrowse->setMinimumHeight(32);
    projectLayout->addWidget(projectLabel);
    projectLayout->addWidget(m_projectEdit, 1);
    projectLayout->addWidget(m_btnBrowse);
    mainLayout->addWidget(projectGroup);
    
    // --- Function Buttons ---
    auto* buttonGroup = new QGroupBox("openCirt Funktionen");
    auto* buttonLayout = new QVBoxLayout(buttonGroup);
    
    // Helper lambda for button creation with description
    auto createButtonRow = [&](const QString& buttonText, const QString& description) -> QPushButton* {
        auto* rowLayout = new QHBoxLayout();
        auto* btn = new QPushButton(buttonText);
        btn->setMinimumWidth(220);
        btn->setMinimumHeight(32);
        auto* descLabel = new QLabel(description);
        Theming::setRole(descLabel, Theming::Role::Muted);
        rowLayout->addWidget(btn);
        rowLayout->addWidget(descLabel);
        rowLayout->addStretch();
        buttonLayout->addLayout(rowLayout);
        return btn;
    };
    
    // Reihenfolge wie im Arbeitsablauf: Projekt aufbauen (Quellzeichnungen aus
    // der Erstellliste), Trenner, Projekt erstellen mit seinen Optionen,
    // Trenner, Projekt bereinigen, Trenner, danach die Ausgaben.
    auto addSeparator = [&]() {
        buttonLayout->addSpacing(8);
        auto* separator = new QFrame();
        separator->setFrameShape(QFrame::HLine);
        separator->setFrameShadow(QFrame::Sunken);
        buttonLayout->addWidget(separator);
        buttonLayout->addSpacing(4);
    };

    m_btnProjektAufbau = createButtonRow(
        "Projekt aufbauen",
        "Quellzeichnungen aus der Erstellliste (CSV) neu aufbauen");
    m_btnProjektAufbau->setToolTip(
        "Kopiert die Vorlagen nach '05- Projekt Zeichnungen', sortiert sie nach\n"
        "Los / ASP / Gewerk / Anlage ein und setzt die Attribute aus der Liste.\n"
        "Nach der Auswahl der Erstellliste: Vorschau (aendert nichts) oder\n"
        "echter Lauf mit Sicherheitsabfrage.");

    addSeparator();

    m_btnFullProject = createButtonRow(
        "Projekt erstellen",
        "Alle Schritte in korrekter Reihenfolge");
    m_btnFullProject->setStyleSheet(
        "QPushButton { font-weight: bold; }");

    // Optionen des Gesamtlaufs direkt unter dem Button, eingerueckt auf die
    // Hoehe des Beschreibungstextes, damit die Zuordnung sichtbar ist.
    auto addOptionRow = [&](QCheckBox* chk) {
        auto* rowLayout = new QHBoxLayout();
        rowLayout->addSpacing(220 + rowLayout->spacing());
        rowLayout->addWidget(chk);
        rowLayout->addStretch();
        buttonLayout->addLayout(rowLayout);
    };

    m_chkIncludeBmk = new QCheckBox("BMK-Nummerierung einschliessen");
    m_chkIncludeBmk->setChecked(true);
    m_chkIncludeBmk->setToolTip(
        "BMK-Nummerierung vor GA-FL-Generierung ausfuehren.\n"
        "Steuerung pro DWG ueber Plankopf-Attribut FREITEXT_05.");
    addOptionRow(m_chkIncludeBmk);

    m_chkIncludeBas = new QCheckBox("BAS-Generierung einschliessen");
    m_chkIncludeBas->setChecked(true);
    m_chkIncludeBas->setToolTip(
        "BAS-Generierung vor GA-FL-Generierung ausfuehren.\n"
        "Setzt korrekte BMK-Nummerierung voraus.");
    {
        // Haken und daneben der Knopf, der den Aufbau des BAS bearbeitet
        auto* rowLayout = new QHBoxLayout();
        rowLayout->addSpacing(220 + rowLayout->spacing());
        rowLayout->addWidget(m_chkIncludeBas);
        rowLayout->addSpacing(16);
        m_btnBasConfig = new QPushButton("BAS konfigurieren");
        m_btnBasConfig->setMinimumHeight(32);   // wie die Knoepfe links, nicht gedrungen
        m_btnBasConfig->setMinimumWidth(180);
        m_btnBasConfig->setToolTip(
            "Aufbau des BAS als Tabelle bearbeiten (Text / Attribut) und als\n"
            "01- Referenzen/BAS.csv im richtigen Format speichern.");
        rowLayout->addWidget(m_btnBasConfig);
        rowLayout->addStretch();
        buttonLayout->addLayout(rowLayout);
    }

    addSeparator();

    m_btnBereinigen = createButtonRow(
        "Projekt bereinigen",
        "Temporaere Dateien und Backups loeschen");

    addSeparator();

    m_btnPublish = createButtonRow(
        "PDF publizieren",
        "Alle Zeichnungen als Multi-Sheet PDF mit Inhaltsverzeichnis");

    m_btnDpExport = createButtonRow(
        "IO-Liste erstellen",
        "Datenpunkte nach Integrationsart filtern, HW zusaetzlich auf Module verteilen");
    m_btnDpExport->setToolTip(
        "Exportiert die Datenpunkte der GA-FL-Blaetter als CSV-Datei.\n"
        "Im folgenden Dialog wird nach Integrationsart gefiltert:\n"
        "  leer  = alle Datenpunkte\n"
        "  HW    = nur Hardware (mit Modul-/Kanalzuordnung)\n"
        "  BUS;SMI = mehrere Arten, semikolongetrennt");

    m_btnSensorliste = createButtonRow(
        "Sensorliste erstellen",
        "Fuehler/Sensoren aus GA-FL-Daten als CSV-Datei exportieren");

    mainLayout->addWidget(buttonGroup);
    
    // --- Progress Bar ---
    m_progressBar = new QProgressBar();
    m_progressBar->setVisible(false);
    m_progressBar->setTextVisible(true);
    mainLayout->addWidget(m_progressBar);
    
    // Kein eigener Protokollkasten: alle Meldungen laufen ueber das Signal
    // logMessage in das Protokoll des Fensters (OpenCirtWindow), das unter
    // diesem Widget den restlichen Platz bekommt.

    // --- Connections ---
    connect(m_btnBrowse, &QPushButton::clicked, this, &OpenCirtTab::onBrowseProject);
    connect(m_projectEdit, &QLineEdit::textChanged, this, &OpenCirtTab::onProjectEdited);
    connect(m_btnFullProject, &QPushButton::clicked, this, &OpenCirtTab::onFullProjectGenerate);
    connect(m_btnPublish, &QPushButton::clicked, this, &OpenCirtTab::onPublishPdf);
    connect(m_btnSensorliste, &QPushButton::clicked, this, &OpenCirtTab::onSensorListeGenerate);
    connect(m_btnDpExport, &QPushButton::clicked, this, &OpenCirtTab::onDatenpunktExport);
    connect(m_btnBasConfig, &QPushButton::clicked, this, &OpenCirtTab::onBasKonfigurieren);
    connect(m_btnBereinigen, &QPushButton::clicked, this, &OpenCirtTab::onProjektBereinigen);
    connect(m_btnProjektAufbau, &QPushButton::clicked, this, &OpenCirtTab::onProjektAufbauen);
}

void OpenCirtTab::onBrowseProject() {
    const QString start = m_projectRoot.isEmpty() ? QDir::homePath() : m_projectRoot;
    const QString dir = QFileDialog::getExistingDirectory(this, "Projektordner waehlen", start);
    if (dir.isEmpty()) return;
    // setText loest textChanged aus, das setzt den Projektordner
    m_projectEdit->setText(QDir::toNativeSeparators(dir));
}

void OpenCirtTab::onProjectEdited(const QString& text) {
    const QString dir = QDir::fromNativeSeparators(text.trimmed());
    if (dir == m_projectRoot) return;
    setProjectRoot(dir);
}

void OpenCirtTab::restoreLastProject() {
    QSettings settings("openCirt", "openCirt");
    QString root = settings.value("projectRoot").toString();
    if (root.isEmpty()) {
        // Einmalige Uebernahme aus batchTool/openCirt bis 1.7
        QSettings old("BatchProcessing", "BricsCAD_Plugin");
        root = old.value("sourceFolder").toString();
    }
    if (root.isEmpty() || !QDir(root).exists()) return;
    m_projectEdit->setText(QDir::toNativeSeparators(root));
}

// ============================================================================
// Laeufe ohne Editor: gemeinsame Helfer
// ============================================================================
//
// Gesamtlauf und Inhaltsverzeichnis bearbeiten die Zeichnungen als
// Side-Database (core/OpenCirtEngine). Ein Lauf ist damit ein gewoehnlicher
// Funktionsaufruf - kein Skript, keine Markerdatei, kein Timer, der auf das
// Ende eines Skripts wartet.

namespace {

/// Werte als Liste (TAG in Grossschreibung, Wert), nach Tag geordnet
OcPairs pairsFromMap(const QMap<QString, QString>& values) {
    OcPairs pairs;
    for (auto it = values.constBegin(); it != values.constEnd(); ++it) {
        pairs.append(qMakePair(it.key().toUpper(), it.value()));
    }
    return pairs;
}

/// ASP, Gewerk und Anlage fuer beide Schreibweisen der Tags
/// (GEWERK/OC_GEWERK, ANLAGE/OC_ANLAGE). Leere Werte loeschen das Attribut.
OcPairs hierarchiePairs(const QString& asp, const QString& gewerk, const QString& anlage) {
    OcPairs pairs;
    pairs << qMakePair(QStringLiteral("ASP"), asp)
          << qMakePair(QStringLiteral("GEWERK"), gewerk)
          << qMakePair(QStringLiteral("OC_GEWERK"), gewerk)
          << qMakePair(QStringLiteral("ANLAGE"), anlage)
          << qMakePair(QStringLiteral("OC_ANLAGE"), anlage);
    return pairs;
}

/// Vorlage an ihr Ziel kopieren. Eine vorhandene Datei wird ersetzt, ein
/// Schreibschutz der Vorlage nicht uebernommen.
bool copyTemplate(const QString& source, const QString& target) {
    if (QFile::exists(target)) {
        QFile::setPermissions(target, QFile::permissions(target)
                                      | QFile::WriteOwner | QFile::WriteUser);
        if (!QFile::remove(target)) return false;
    }
    if (!QFile::copy(source, target)) return false;
    QFile::setPermissions(target, QFile::permissions(target)
                                  | QFile::WriteOwner | QFile::WriteUser);
    return true;
}

}  // namespace

bool OpenCirtTab::checkNoOpenDrawings(const QString& title) {
    // Eine im Editor geoeffnete Zeichnung laesst sich nicht ersetzen. Frueher
    // fiel das nicht auf, weil das Skript die Zeichnungen selbst im Editor
    // oeffnete.
    QStringList open;
    const QString root =
        QDir::fromNativeSeparators(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR)) + "/";
    if (acDocManager) {
        AcApDocumentIterator* it = acDocManager->newAcApDocumentIterator();
        for (; it && !it->done(); it->step()) {
            AcApDocument* doc = it->document();
            if (!doc || !doc->fileName()) continue;
            const QString path =
                QDir::fromNativeSeparators(QString::fromWCharArray(doc->fileName()));
            if (path.startsWith(root, Qt::CaseInsensitive)) {
                open << QDir::toNativeSeparators(path);
            }
        }
        delete it;
    }
    if (open.isEmpty()) return true;

    logError(QString("%1: %2 Zeichnung(en) des Projekts sind im Editor geoeffnet")
             .arg(title).arg(open.size()));
    showListWarning(this, title,
        "Diese Zeichnungen des Projekts sind in BricsCAD geoeffnet:",
        open,
        "Geoeffnete Zeichnungen lassen sich nicht bearbeiten. Bitte schliessen "
        "und den Lauf erneut starten. Es wurde nichts geaendert.");
    return false;
}

void OpenCirtTab::beginRun(QPushButton* button, const QString& busyText) {
    m_running = true;
    OcEngine::beginBackupRun();
    m_runButton = button;
    m_runButtonText = button ? button->text() : QString();
    if (button) button->setText(busyText);
    updateButtonStates();
    showProgress(busyText);
}

void OpenCirtTab::beginPhase(const QString& label, int total) {
    m_phaseLabel = label;
    m_phaseTotal = total;
    m_phaseDone = 0;
    m_progressBar->setRange(0, qMax(total, 1));
    m_progressBar->setValue(0);
    m_progressBar->setFormat(QString("%1: 0 von %2").arg(label).arg(total));
    m_progressBar->setVisible(true);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void OpenCirtTab::stepDone(const QString& fileName) {
    // Waehrend des Laufs wird die Anzeige weiter gezeichnet, Eingaben bleiben
    // liegen: niemand kann mitten im Lauf etwas anderes anstossen.
    ++m_phaseDone;
    m_progressBar->setValue(m_phaseDone);
    m_progressBar->setFormat(QString("%1: %2 von %3 - %4")
                             .arg(m_phaseLabel).arg(m_phaseDone).arg(m_phaseTotal).arg(fileName));
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void OpenCirtTab::endRun() {
    // Extraktionsdaten gelten nur fuer den Lauf, der sie geschrieben hat
    m_extractFresh = false;
    hideProgress();
    if (m_runButton) m_runButton->setText(m_runButtonText);
    m_runButton = nullptr;
    m_running = false;
    updateButtonStates();
}

void OpenCirtTab::showProgress(const QString& text) {
    // Bereich 0..1 statt "unbestimmt" (0,0): im unbestimmten Zustand zeigt
    // Qt keinen Text, der Text ist hier aber die eigentliche Information.
    m_progressBar->setRange(0, 1);
    m_progressBar->setValue(0);
    m_progressBar->setFormat(text);
    m_progressBar->setVisible(true);
    QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
}

void OpenCirtTab::hideProgress() {
    m_progressBar->setVisible(false);
    m_progressBar->setFormat(QString());
}


bool OpenCirtTab::processDrawing(const QString& path, RunCounts& counts,
                                 const std::function<void(OcDrawing&)>& work,
                                 bool backup) {
    OcDrawing dwg;
    if (!dwg.open(path)) {
        logError(QString("%1: %2").arg(QFileInfo(path).fileName(), dwg.lastError()));
        ++counts.errors;
        return false;
    }
    work(dwg);
    if (!dwg.save(backup)) {
        logError(QString("%1: %2").arg(QFileInfo(path).fileName(), dwg.lastError()));
        ++counts.errors;
        return false;
    }
    return true;
}

bool OpenCirtTab::processNewDrawing(const QString& path, RunCounts& counts,
                                    const std::function<void(OcDrawing&)>& work) {
    // Eben erst aus der Vorlage kopiert: es gibt keinen Stand, den eine
    // Sicherungskopie bewahren koennte
    return processDrawing(path, counts, work, false);
}

bool OpenCirtTab::confirmMissingReferences(const QVector<SourceDrawingInfo>& drawings) {
    // Vorab-Validierung: REF_DP gegen die Referenz pruefen
    QMap<QString, QVector<QString>> refData = readOdsReference();
    if (refData.isEmpty()) {
        reportError("Referenz fehlt",
            "GA-FL-Referenz (GA_FL_VORLAGE.csv) leer oder nicht lesbar - Abbruch",
            "Ohne sie koennen die GA-FL-Blaetter nicht befuellt werden. "
            "Der Lauf wird abgebrochen.");
        return false;
    }

    QSet<QString> missingRefs;
    QMap<QString, QStringList> missingPerDrawing;  // ref -> list of drawings
    QDir drawingsRoot(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR));

    for (const SourceDrawingInfo& d : drawings) {
        for (const DataPoint& dp : d.dataPoints) {
            QString ref = dp.refDp.trimmed();
            if (!ref.isEmpty() && !refData.contains(ref)) {
                missingRefs.insert(ref);
                missingPerDrawing[ref].append(drawingsRoot.relativeFilePath(d.filePath));
            }
        }
    }
    if (missingRefs.isEmpty()) return true;

    QStringList sortedRefs(missingRefs.begin(), missingRefs.end());
    sortedRefs.sort();

    QVector<WarningGroup> groups;
    logError(QString("%1 fehlende DP-Referenz(en) in GA_FL_VORLAGE.ods").arg(missingRefs.size()));
    for (const QString& ref : sortedRefs) {
        QStringList dwgs = missingPerDrawing[ref];
        dwgs.removeDuplicates();
        dwgs.sort(Qt::CaseInsensitive);
        groups.append({ref, dwgs});
        logError(QString("  Fehlend: %1 (in: %2)").arg(ref, dwgs.join(", ")));
    }

    const bool weiter = showTreeWarning(
        this, "Fehlende DP-Referenzen",
        QString("ACHTUNG: %1 Datenpunkt-Referenz(en) fehlen in "
                "GA_FL_VORLAGE.ods:").arg(missingRefs.size()),
        groups, "Zeichnung",
        "Betroffene Datenpunkte werden mit 0 belegt und mit [!REF] "
        "markiert.\n\nFortfahren?",
        true);

    if (!weiter) {
        logError("Abbruch durch Benutzer (fehlende DP-Referenzen)");
    }
    return weiter;
}

void OpenCirtTab::updateButtonStates() {
    const bool enabled = !m_projectRoot.isEmpty() && !m_running;
    m_btnProjektAufbau->setEnabled(enabled);
    m_btnPublish->setEnabled(enabled);
    m_btnSensorliste->setEnabled(enabled);
    m_btnDpExport->setEnabled(enabled);
    m_btnFullProject->setEnabled(enabled);
    m_btnBereinigen->setEnabled(enabled);
    m_btnBasConfig->setEnabled(enabled);
    m_chkIncludeBmk->setEnabled(enabled);
    m_chkIncludeBas->setEnabled(enabled);
    // Waehrend eines Laufs bleibt auch der Projektordner fest
    m_projectEdit->setEnabled(!m_running);
    m_btnBrowse->setEnabled(!m_running);
}


// ============================================================================
// Project Root & Configuration
// ============================================================================

void OpenCirtTab::setProjectRoot(const QString& root) {
    m_projectRoot = root;
    // Nichts aus dem vorigen Projekt mitnehmen
    m_extractTempDir.clear();
    m_extractPurpose.clear();
    m_extractFresh = false;
    m_plankopfCsvData.clear();
    m_plankopfCsvPath.clear();
    m_plankopfCsvStamp = QDateTime();
    m_plankopfCsvSize = -1;

    if (!root.isEmpty()) {
        loadConfig();
        QSettings settings("openCirt", "openCirt");
        settings.setValue("projectRoot", root);
    }
    // Aufruf von aussen (letztes Projekt, Testtreiber): Feld nachziehen,
    // ohne ueber textChanged erneut hier zu landen
    if (m_projectEdit
        && QDir::fromNativeSeparators(m_projectEdit->text().trimmed()) != root) {
        const QSignalBlocker blocker(m_projectEdit);
        m_projectEdit->setText(QDir::toNativeSeparators(root));
    }
    updateButtonStates();
}

void OpenCirtTab::loadConfig() {
    if (m_projectRoot.isEmpty()) return;
    
    if (m_config.loadFromFile(m_projectRoot)) {
        m_chkIncludeBmk->setChecked(m_config.includeBmk);
        m_chkIncludeBas->setChecked(m_config.includeBas);
    }
}

void OpenCirtTab::saveConfig() {
    if (m_projectRoot.isEmpty()) return;
    
    m_config.includeBmk = m_chkIncludeBmk->isChecked();
    m_config.includeBas = m_chkIncludeBas->isChecked();
    m_config.lastProjectRoot = m_projectRoot;
    
    m_config.saveToFile(m_projectRoot);
}

// ============================================================================
// Path Helpers
// ============================================================================

QString OpenCirtTab::projectPath(const char* subfolder) const {
    return m_projectRoot + "/" + subfolder;
}

QString OpenCirtTab::referencePath(const char* filename) const {
    return projectPath(OpenCirtConfig::REFERENZEN_DIR) + "/" + filename;
}

QString OpenCirtTab::templatePath(const char* filename) const {
    return projectPath(OpenCirtConfig::VORLAGEN_DIR) + "/" + filename;
}

// ============================================================================
// Validation
// ============================================================================

bool OpenCirtTab::validateProjectStructure(QStringList& errors) {
    errors.clear();
    
    if (m_projectRoot.isEmpty()) {
        errors << "Kein Projektverzeichnis angegeben.";
        return false;
    }
    
    // Pflichtordner. '02- Skripte' gehoert nicht mehr dazu: die LISP-Skripte
    // der Schritte bis Version 1.6 werden seit 1.7 nicht mehr ausgefuehrt.
    QStringList requiredDirs = {
        OpenCirtConfig::REFERENZEN_DIR,
        OpenCirtConfig::VORLAGEN_DIR,
        OpenCirtConfig::ZEICHNUNGEN_DIR
    };
    
    for (const QString& dir : requiredDirs) {
        QString fullPath = projectPath(dir.toUtf8().constData());
        if (!QDir(fullPath).exists()) {
            errors << QString("Ordner fehlt: %1").arg(dir);
        }
    }
    
    return errors.isEmpty();
}

QStringList OpenCirtTab::findProjectDwgs() {
    QStringList dwgFiles;
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    const QString drawingsRootPath = QDir(drawingsDir).absolutePath();

    QDirIterator it(drawingsDir, QStringList() << "*.dwg", QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);

        // Die oberste Ebene von '05- Projekt Zeichnungen' traegt nur
        // Projektblaetter (Deckblatt, Revisionshistorie, Inhaltsverzeichnis,
        // Summen), nie eine Quellzeichnung. Sie bekommen die Plankopf-Stammdaten
        // (siehe findProjektblaetter), laufen aber nicht durch BMK, BAS,
        // Extraktion und GA-FL. Die Regel haengt an der Ebene, nicht am
        // Dateinamen - so sind die Namen der Projektblaetter frei waehlbar.
        if (QString::compare(fi.absolutePath(), drawingsRootPath,
                             Qt::CaseInsensitive) == 0) {
            continue;
        }

        // Erzeugte Blaetter in den Unterordnern ueberspringen
        QString fileName = fi.fileName();
        if (fileName.contains("_GA_FL_") || fileName.contains("_Summe_") ||
            fileName.contains("_Deckblatt") || fileName.contains("_Inhalt_")) {
            continue;
        }
        dwgFiles << path;
    }

    dwgFiles.sort(Qt::CaseInsensitive);
    return dwgFiles;
}

QStringList OpenCirtTab::findProjektblaetter() {
    // Projektblaetter = DWGs direkt auf der obersten Ebene von
    // '05- Projekt Zeichnungen' (Deckblatt_A, Revisionshistorie, ...). Sie
    // entstehen aus projektneutralen Vorlagen, ihre Plankopf-Felder AN/AG/PR/
    // ERSTELLER sind daher leer und werden im Gesamtlauf aus plankopfdaten.csv
    // befuellt. Vom Plugin selbst erzeugte Blaetter (Inhalt, Summen) bekommen
    // ihre Plankopfdaten in ihren eigenen Generatoren und bleiben hier aussen vor.
    QStringList result;
    QDir dir(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR));
    const QStringList files = dir.entryList(QStringList() << "*.dwg", QDir::Files,
                                            QDir::Name | QDir::IgnoreCase);
    for (const QString& f : files) {
        if (f.contains("_Inhalt_", Qt::CaseInsensitive) ||
            f.contains("_Summe", Qt::CaseInsensitive) ||
            f.contains("_GA_FL_", Qt::CaseInsensitive)) {
            continue;
        }
        result << dir.absoluteFilePath(f);
    }
    return result;
}

namespace {
// Ordnerebenen unter '05- Projekt Zeichnungen', wie "Projekt aufbauen" sie anlegt
enum HierarchieEbene { EbeneLos = 0, EbeneAsp = 1, EbeneGewerk = 2, EbeneAnlage = 3 };
}

QStringList OpenCirtTab::hierarchieLevels(const QString& folderPath) const {
    // Welche Ebene ein Ordner ist, bestimmt seine Lage unter dem
    // Zeichnungsordner, nicht sein Name: "Projekt aufbauen" legt die Ordner in
    // der Reihenfolge Los / ASP / Gewerk / Anlage an, die Namen kommen aus der
    // Erstellliste und werden so uebernommen, wie der Planer sie dort
    // eingetragen hat. Bis 1.7.1 galt nur ein Ordner als ASP-Ebene, dessen
    // Name "ASP" oder "ISP" enthielt; eine Kennung wie "MUEK01" blieb damit
    // ohne ASP im Plankopf und in den Listen.
    QString root = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    root.replace("\\", "/");
    QString folder = folderPath;
    folder.replace("\\", "/");

    const QString rel = QDir(root).relativeFilePath(folder);
    if (rel.isEmpty() || rel == "." || rel == ".." || rel.startsWith("../")
        || QDir::isAbsolutePath(rel)) {
        return QStringList();   // nicht unter dem Zeichnungsordner
    }
    QStringList parts = rel.split('/', Qt::SkipEmptyParts);
    parts.removeAll(".");
    return parts;
}

QString OpenCirtTab::detectAspFromPath(const QString& dwgPath) {
    const QStringList levels = hierarchieLevels(QFileInfo(dwgPath).absolutePath());
    return levels.size() > EbeneAsp ? levels.at(EbeneAsp) : QString();
}

QString OpenCirtTab::aspFolderPathOf(const QString& folderPath) const {
    const QStringList levels = hierarchieLevels(folderPath);
    if (levels.size() <= EbeneAsp) return QString();
    return QDir(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR) + "/"
                + levels.at(EbeneLos) + "/" + levels.at(EbeneAsp)).absolutePath();
}

bool OpenCirtTab::isActiveValue(const QString& value) {
    QString v = value.trimmed().toLower();
    return (v == "ja" || v == "true" || v == "1" || v == "x" ||
            v == "high" || v == "aktiv" || v == "wahr");
}

// ============================================================================
// ODS to CSV Conversion
// ============================================================================

QString OpenCirtTab::findLibreOffice() {
#ifdef _WIN32
    // Common Windows paths
    QStringList paths = {
        "C:/Program Files/LibreOffice/program/soffice.exe",
        "C:/Program Files (x86)/LibreOffice/program/soffice.exe"
    };
    for (const QString& p : paths) {
        if (QFileInfo::exists(p)) return p;
    }
    
    // Try PATH
    QProcess proc;
    proc.start("where", QStringList() << "soffice.exe");
    proc.waitForFinished(5000);
    QString output = proc.readAllStandardOutput().trimmed();
    if (!output.isEmpty() && QFileInfo::exists(output.split("\n").first())) {
        return output.split("\n").first().trimmed();
    }
#else
    // Linux: LibreOffice kommt aus dem Paket der Distribution, von
    // libreoffice.org (/opt), als Flatpak oder als Snap. Wer es woanders
    // liegen hat, nennt das Programm in OPENCIRT_LIBREOFFICE.
    auto usable = [](const QString& p) {
        const QFileInfo fi(p);
        return fi.isFile() && fi.isExecutable();
    };

    const QString fromEnv = qEnvironmentVariable("OPENCIRT_LIBREOFFICE").trimmed();
    if (!fromEnv.isEmpty()) {
        if (usable(fromEnv)) return fromEnv;
        log(QString("OPENCIRT_LIBREOFFICE zeigt auf kein ausfuehrbares Programm: %1")
            .arg(fromEnv), "WARN");
    }

    QStringList paths = {
        "/usr/bin/libreoffice",
        "/usr/bin/soffice",
        "/usr/local/bin/libreoffice",
        "/usr/local/bin/soffice",
        "/usr/lib64/libreoffice/program/soffice",
        "/usr/lib/libreoffice/program/soffice"
    };
    // Pakete von libreoffice.org: /opt/libreoffice<Version>, neueste zuerst
    const QStringList optDirs = QDir("/opt").entryList(
        QStringList() << "libreoffice*", QDir::Dirs | QDir::NoDotAndDotDot,
        QDir::Name | QDir::Reversed);
    for (const QString& d : optDirs) {
        paths << "/opt/" + d + "/program/soffice";
    }
    // Flatpak legt je Anwendung ein Startskript an - fuer den Benutzer oder
    // fuer alle. Es nimmt dieselben Argumente wie soffice.
    paths << QDir::homePath() + "/.local/share/flatpak/exports/bin/org.libreoffice.LibreOffice"
          << "/var/lib/flatpak/exports/bin/org.libreoffice.LibreOffice"
          << "/snap/bin/libreoffice";
    for (const QString& p : paths) {
        if (usable(p)) return p;
    }

    // Zuletzt der Suchpfad
    for (const char* name : {"libreoffice", "soffice"}) {
        const QString p = QStandardPaths::findExecutable(QString::fromLatin1(name));
        if (!p.isEmpty()) return p;
    }
#endif
    return QString();
}

QProcessEnvironment OpenCirtTab::externalToolEnvironment() {
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
#ifndef _WIN32
    // BricsCAD stellt sein Programmverzeichnis vor den Bibliothekspfad. Ein
    // fremdes Programm faende dort Bibliotheken, die nicht zu ihm passen.
    const QString appDir = QDir::cleanPath(QCoreApplication::applicationDirPath());
    QStringList keep;
    const QStringList parts = env.value("LD_LIBRARY_PATH").split(':', Qt::SkipEmptyParts);
    for (const QString& part : parts) {
        const QString p = QDir::cleanPath(part);
        if (p == appDir || p.startsWith(appDir + '/')) continue;
        keep << part;
    }
    if (keep.isEmpty()) {
        env.remove("LD_LIBRARY_PATH");
    } else {
        env.insert("LD_LIBRARY_PATH", keep.join(':'));
    }
#endif
    return env;
}

QString OpenCirtTab::findExcel() {
#ifdef _WIN32
    // Common Windows paths for Excel
    QStringList paths = {
        "C:/Program Files/Microsoft Office/root/Office16/EXCEL.EXE",
        "C:/Program Files (x86)/Microsoft Office/root/Office16/EXCEL.EXE",
        "C:/Program Files/Microsoft Office/root/Office15/EXCEL.EXE"
    };
    for (const QString& p : paths) {
        if (QFileInfo::exists(p)) return p;
    }
#endif
    return QString();
}

bool OpenCirtTab::convertOdsToCSV(const QString& odsPath, const QString& csvPath) {
    m_odsProblem.clear();

    // Die bisherige CSV wird nie weiterverwendet (kein Caching). Sie bleibt
    // aber liegen, bis die neue da ist: scheitert die Umwandlung, ist am
    // Projekt nichts geaendert.
    const QString kept = csvPath + ".vorher";
    QFile::remove(kept);
    const bool hadOld = QFile::exists(csvPath) && QFile::rename(csvPath, kept);
    if (QFile::exists(csvPath)) {
        QFile::remove(csvPath);
    }
    auto succeeded = [&](const QString& tool) {
        QFile::remove(kept);
        logSuccess(QString("ODS erfolgreich zu CSV konvertiert (%1)").arg(tool));
        return true;
    };

    QString outDir = QFileInfo(csvPath).absolutePath();
    QStringList problems;

    // Try LibreOffice first
    QString libreOffice = findLibreOffice();
    if (!libreOffice.isEmpty()) {
        log(QString("Konvertiere ODS mit LibreOffice: %1 (%2)")
            .arg(QFileInfo(odsPath).fileName(), QDir::toNativeSeparators(libreOffice)));

        QProcess proc;
        proc.setWorkingDirectory(outDir);
        proc.setProcessEnvironment(externalToolEnvironment());
        proc.setProcessChannelMode(QProcess::MergedChannels);
        QStringList args = {
            "--headless",
            "--convert-to", "csv",
            "--outdir", outDir,
            odsPath
        };
        proc.start(libreOffice, args);

        // Waehrend LibreOffice arbeitet, wird die Anzeige weiter gezeichnet
        const int timeoutMs = 90000;
        QString problem;
        if (!proc.waitForStarted(15000)) {
            problem = QString("laesst sich nicht starten (%1)").arg(proc.errorString());
        } else {
            QElapsedTimer waited;
            waited.start();
            while (!proc.waitForFinished(100)) {
                if (proc.state() == QProcess::NotRunning) break;
                if (waited.elapsed() > timeoutMs) {
                    problem = QString("antwortet seit %1 s nicht").arg(timeoutMs / 1000);
                    proc.kill();
                    proc.waitForFinished(5000);
                    break;
                }
                QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
            }
            if (problem.isEmpty()
                && (proc.exitStatus() != QProcess::NormalExit || proc.exitCode() != 0)) {
                problem = QString("endet mit Fehler %1").arg(proc.exitCode());
            }
        }
        const QString output = QString::fromLocal8Bit(proc.readAll()).trimmed();

        if (problem.isEmpty()) {
            if (QFile::exists(csvPath)) {
                return succeeded("LibreOffice");
            }
            // LibreOffice might name the CSV differently
            QString baseName = QFileInfo(odsPath).completeBaseName();
            QString altCsv = outDir + "/" + baseName + ".csv";
            if (QFile::exists(altCsv) && altCsv != csvPath) {
                QFile::rename(altCsv, csvPath);
                return succeeded("LibreOffice");
            }
            problem = "hat keine CSV-Datei geschrieben";
        }
        logError(QString("LibreOffice-Konvertierung fehlgeschlagen: LibreOffice %1").arg(problem));
        if (!output.isEmpty()) {
            logError(QString("  Ausgabe von LibreOffice: %1").arg(output.left(600)));
        }
        problems << QString("LibreOffice (%1) %2.")
                    .arg(QDir::toNativeSeparators(libreOffice), problem);
        if (!output.isEmpty()) {
            problems << QString("Ausgabe von LibreOffice:\n%1").arg(output.left(600));
        }
        if (libreOffice.contains("flatpak", Qt::CaseInsensitive)) {
            problems << QString("LibreOffice laeuft als Flatpak in einer Sandbox. Es braucht "
                                "Zugriff auf den Projektordner (Berechtigung fuer das "
                                "Dateisystem, z.B. ueber Flatseal).");
        }
    }

    // Fallback: Excel via PowerShell (Windows only)
#ifdef _WIN32
    QString excel = findExcel();
    if (!excel.isEmpty()) {
        log("Fallback: Konvertiere ODS mit Excel via PowerShell...");
        
        QProcess proc;
        QString odsWin = odsPath;
        odsWin.replace("/", "\\");
        QString csvWin = csvPath;
        csvWin.replace("/", "\\");
        QString psScript = QString(
            "$excel = New-Object -ComObject Excel.Application; "
            "$excel.Visible = $false; "
            "$excel.DisplayAlerts = $false; "
            "$wb = $excel.Workbooks.Open('%1'); "
            "$wb.SaveAs('%2', 6); "  // 6 = CSV
            "$wb.Close($false); "
            "$excel.Quit()"
        ).arg(odsWin)
         .arg(csvWin);
        
        proc.start("powershell", QStringList() << "-Command" << psScript);
        if (proc.waitForFinished(60000) && QFile::exists(csvPath)) {
            return succeeded("Excel");
        }
        logError("Excel-Konvertierung fehlgeschlagen");
        problems << QString("Excel konnte die Datei nicht umwandeln.");
    }
#endif

    // Nichts hat geklappt: den Stand von vorher wiederherstellen
    QFile::remove(csvPath);
    if (hadOld) {
        QFile::rename(kept, csvPath);
    }

    if (problems.isEmpty()) {
#ifdef _WIN32
        logError("ODS-Konvertierung fehlgeschlagen: Weder LibreOffice noch Excel gefunden");
        m_odsProblem = QString(
            "Weder LibreOffice noch Excel wurden gefunden.\n\n"
            "Das Plugin wandelt die Referenz %1 mit LibreOffice (ersatzweise Excel) "
            "in eine CSV-Datei um. Bitte LibreOffice installieren.")
            .arg(QFileInfo(odsPath).fileName());
#else
        logError("ODS-Konvertierung fehlgeschlagen: LibreOffice nicht gefunden");
        m_odsProblem = QString(
            "LibreOffice wurde nicht gefunden.\n\n"
            "Das Plugin wandelt die Referenz %1 mit LibreOffice in eine CSV-Datei um. "
            "Gesucht wurde in /usr/bin, /usr/local/bin, /usr/lib64, /usr/lib und /opt, "
            "als Flatpak (org.libreoffice.LibreOffice), als Snap und im Suchpfad.\n\n"
            "Bitte LibreOffice installieren oder das Programm in der "
            "Umgebungsvariablen OPENCIRT_LIBREOFFICE nennen.")
            .arg(QFileInfo(odsPath).fileName());
#endif
    } else {
        logError("ODS-Konvertierung fehlgeschlagen");
        m_odsProblem = QString("Die Referenz %1 liess sich nicht in eine CSV-Datei umwandeln.\n\n%2")
            .arg(QFileInfo(odsPath).fileName(), problems.join("\n\n"));
    }
    return false;
}

// ============================================================================
// Logging
// ============================================================================

void OpenCirtTab::log(const QString& message, const QString& type) {
    // Ausgabe uebernimmt das "Processing Log" des Hauptfensters. Der Tab haelt
    // bewusst keine zweite Ansicht desselben Textes.
    emit logMessage(message, type);
}

void OpenCirtTab::logError(const QString& message) {
    log(message, "ERROR");
}

void OpenCirtTab::logSuccess(const QString& message) {
    log(message, "SUCCESS");
}

void OpenCirtTab::reportError(const QString& title, const QString& line,
                              const QString& details, bool fatal) {
    logError(line);
    const QString text = details.isEmpty() ? line : line + "\n\n" + details;
    if (fatal) {
        QMessageBox::critical(this, title, text);
    } else {
        QMessageBox::warning(this, title, text);
    }
}

bool OpenCirtTab::requireProjectStructure() {
    QStringList errors;
    if (validateProjectStructure(errors)) return true;
    reportError("Projektstruktur", "Projektstruktur unvollstaendig",
                errors.join("\n"), false);
    return false;
}

// ============================================================================
// Button Handlers
// ============================================================================

void OpenCirtTab::onFullProjectGenerate() {
    if (!requireProjectStructure()) return;
    
    // Ohne die Referenz wuerden saemtliche Datenpunkte mit 0 belegt. Die Pruefung
    // steht bewusst vor der Sicherheitsabfrage und vor dem Cleanup, damit im
    // Fehlerfall noch keine bestehenden Blaetter geloescht sind.
    const QString odsPath = referencePath(OpenCirtConfig::GA_FL_VORLAGE_ODS);
    if (!QFileInfo::exists(odsPath)) {
        reportError("Referenz fehlt",
            "GA-FL-Referenz nicht gefunden: " + QDir::toNativeSeparators(odsPath),
            "Ohne sie koennen die GA-FL-Blaetter nicht befuellt werden. "
            "Der Lauf wird nicht gestartet.");
        return;
    }

    if (!checkNoOpenDrawings("Projekt erstellen")) return;

    QMessageBox::StandardButton reply = QMessageBox::warning(this,
        "Projekt erstellen",
        "Empfehlung: Speichern Sie den gesamten Zeichnungsordner vorher als "
        "ZIP-Archiv als Sicherungskopie.\n\n"
        "Schritte: Deckblaetter + BMK + BAS + Extraktion + GA-FL + Summen + Textbreiten\n"
        "Alle bestehenden *_Deckblatt.dwg, *_GA_FL_*.dwg und *_Summe*.dwg werden geloescht.\n\n"
        "Fortfahren?",
        QMessageBox::Yes | QMessageBox::Cancel);
    
    if (reply != QMessageBox::Yes) return;
    
    log("=== PROJEKT ERSTELLEN ===");
    saveConfig();
    
    QStringList dwgFiles = findProjectDwgs();
    if (dwgFiles.isEmpty()) {
        logError("Keine Projektzeichnungen gefunden");
        return;
    }
    
    log(QString("%1 DWG-Dateien gefunden").arg(dwgFiles.size()));

    QElapsedTimer timer;
    timer.start();

    RunCounts counts;
    beginRun(m_btnFullProject, "Projekt wird erstellt...");
    const bool finished = runGesamtlauf(dwgFiles, counts);
    endRun();

    const qint64 seconds = timer.elapsed() / 1000;
    const QString dauer = QString("%1:%2 min")
        .arg(seconds / 60).arg(seconds % 60, 2, 10, QChar('0'));

    if (!finished) {
        logError(QString("Projekt erstellen abgebrochen nach %1").arg(dauer));
        return;
    }

    const QString summary = QString(
        "Deckblaetter:  %1\n"
        "Datenpunkte:  %2\n"
        "GA-FL-Blaetter:  %3\n"
        "Summenblaetter:  %4\n"
        "Textbreiten angepasst:  %5\n"
        "Fehler:  %6\n"
        "Dauer:  %7")
        .arg(counts.deckblaetter).arg(counts.datenpunkte).arg(counts.gaFl)
        .arg(counts.summen).arg(counts.textbreiten).arg(counts.errors).arg(dauer);

    if (counts.errors == 0) {
        logSuccess(QString("Projekt erstellt in %1: %2 GA-FL-Blaetter, %3 Summenblaetter, "
                           "%4 Deckblaetter")
                   .arg(dauer).arg(counts.gaFl).arg(counts.summen).arg(counts.deckblaetter));
        QMessageBox::information(this, "Projekt erstellen",
            "Projekt erstellt.\n\n" + summary);
    } else {
        logError(QString("Projekt erstellt mit %1 Fehler(n) in %2 - siehe Protokoll")
                 .arg(counts.errors).arg(dauer));
        QMessageBox::warning(this, "Projekt erstellen",
            "Projekt erstellt - es gab Fehler, bitte das Protokoll pruefen.\n\n" + summary);
    }
}

void OpenCirtTab::onBasKonfigurieren() {
    if (m_projectRoot.isEmpty()) return;
    const QString basPath = referencePath(OpenCirtConfig::BAS_CSV);
    BasConfigDialog dlg(basPath, this);
    log("BAS konfigurieren: " + QString(dlg.status()).replace('\n', ' '));
    if (dlg.exec() != QDialog::Accepted) return;
    const QVector<OcBasSegment> segs = dlg.segments();
    log(QString("BAS.csv gespeichert: %1 Segmente, Aufbau: %2")
        .arg(segs.size()).arg(OcEngine::basLayoutText(segs)));
    log("Datei: " + basPath + " (bisherige Fassung als BAS.csv.bak)");
}

QString OpenCirtTab::projectExtractDir(const QString& purpose) const {
    // Die Kennung entsteht aus dem vollen Pfad: gleichnamige Projekte an
    // verschiedenen Orten bekommen verschiedene Ordner
    const QString root = QDir::cleanPath(QDir(m_projectRoot).absolutePath());
#ifdef _WIN32
    const QString key = root.toLower();
#else
    const QString key = root;
#endif
    const QString id = QString::fromLatin1(
        QCryptographicHash::hash(key.toUtf8(), QCryptographicHash::Sha1).toHex().left(10));

    // Der Name dient nur dem Wiederfinden; kurz halten wegen der Pfadlaenge
    QString name = QDir(root).dirName().left(24).trimmed();
    name.replace(QRegularExpression(QStringLiteral("[\\\\/:*?\"<>|]")), QStringLiteral("_"));
    if (name.isEmpty()) name = QStringLiteral("Projekt");

    return QStandardPaths::writableLocation(QStandardPaths::TempLocation)
           + "/OpenCirt_extract/" + name + "_" + id
           + (purpose.isEmpty() ? QString() : "_" + purpose);
}

bool OpenCirtTab::extractDirUsable() const {
    return m_extractFresh && !m_projectRoot.isEmpty()
           && m_extractTempDir == projectExtractDir(m_extractPurpose);
}

bool OpenCirtTab::resetExtractDir(const QString& purpose) {
    m_extractFresh = false;
    m_extractPurpose = purpose;
    m_extractTempDir = projectExtractDir(purpose);

    QDir dir(m_extractTempDir);
    if (dir.exists() && !dir.removeRecursively()) {
        reportError("Tempordner",
            QString("Tempordner mit den Extraktionsdaten laesst sich nicht leeren: %1")
                .arg(QDir::toNativeSeparators(m_extractTempDir)),
            "Vermutlich ist eine Datei daraus in einem anderen Programm geoeffnet. "
            "Mit alten Daten wird nicht gearbeitet - bitte die Datei schliessen "
            "und den Lauf erneut starten. Es wurde nichts geaendert.");
        return false;
    }
    if (!QDir().mkpath(m_extractTempDir)) {
        reportError("Tempordner",
            QString("Tempordner fuer die Extraktionsdaten laesst sich nicht anlegen: %1")
                .arg(QDir::toNativeSeparators(m_extractTempDir)));
        return false;
    }

    // Herkunft festhalten, zum Nachsehen
    QFile origin(m_extractTempDir + "/projekt.txt");
    if (origin.open(QIODevice::WriteOnly | QIODevice::Text)) {
        QTextStream out(&origin);
        out.setEncoding(QStringConverter::Utf8);
        out << "Projekt: " << QDir::toNativeSeparators(m_projectRoot) << "\n"
            << "Erzeugt: " << QDateTime::currentDateTime().toString(Qt::ISODate) << "\n";
    }

    m_extractFresh = true;
    return true;
}

bool OpenCirtTab::extractDrawings(const QStringList& dwgFiles, const QString& purpose,
                                  QStringList& unreadable) {
    if (!resetExtractDir(purpose)) return false;

    const QDir drawingsRoot(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR));
    OcLogFile extractLog;
    extractLog.open(m_extractTempDir + "/extractdp_log.txt", true);

    beginPhase("Zeichnungen lesen", dwgFiles.size());
    for (const QString& path : dwgFiles) {
        const QString folder = QFileInfo(path).absolutePath();
        const QString subDir = m_extractTempDir + "/" + drawingsRoot.relativeFilePath(folder);
        QDir().mkpath(subDir);

        OcDrawing dwg;
        QString error;
        if (!dwg.open(path, true)) {
            error = dwg.lastError();
        } else {
            const OcExtractResult ex = dwg.extractDp(subDir, &extractLog);
            if (!ex.ok) error = ex.error;
        }
        if (!error.isEmpty()) {
            logError(QString("%1: %2").arg(QFileInfo(path).fileName(), error));
            unreadable << QDir::toNativeSeparators(drawingsRoot.relativeFilePath(path));
        }
        stepDone(QFileInfo(path).fileName());
    }
    extractLog.close();
    return true;
}

int OpenCirtTab::removeBmkCounters() {
    int removed = 0;
    QDirIterator it(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR),
                    QStringList() << "bmk_counters.tmp",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        const QString path = it.next();
        if (QFile::remove(path)) {
            ++removed;
        } else {
            log(QString("BMK-Zaehlerdatei nicht loeschbar: %1")
                .arg(QDir::toNativeSeparators(path)), "WARN");
        }
    }
    return removed;
}

bool OpenCirtTab::runGesamtlauf(const QStringList& dwgFiles, RunCounts& counts) {
    const QDir drawingsRoot(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR));

    // Vor allem anderen: scheitert es hier, ist am Projekt noch nichts geaendert
    if (!resetExtractDir()) {
        return false;
    }

    // Step 0: ODS conversion. Sie steht vor dem Cleanup: fehlt LibreOffice
    // oder scheitert die Umwandlung, sind noch keine Blaetter geloescht.
    showProgress("GA-FL-Referenz umwandeln (ODS nach CSV)...");
    const QString odsPath = referencePath(OpenCirtConfig::GA_FL_VORLAGE_ODS);
    const QString refCsvPath = referencePath("GA_FL_VORLAGE.csv");
    if (!convertOdsToCSV(odsPath, refCsvPath)) {
        reportError("Referenz umwandeln", "ODS-Konvertierung fehlgeschlagen - Abbruch",
            m_odsProblem + "\n\nDer Lauf wurde abgebrochen. An den Zeichnungen wurde "
                           "nichts geaendert.");
        return false;
    }

    // Step 1: Cleanup
    showProgress("Bestehende Deckblaetter, GA-FL- und Summenblaetter entfernen...");
    cleanupDeckblaetter();
    cleanupInhalt();
    cleanupGaFl();

    // Step 1.5: Plankopf-Stammdaten aus CSV (AG, AN, PR etc.) - auch auf die
    // Projektblaetter der obersten Ebene (Deckblatt_A, Revisionshistorie),
    // die sonst keinen Schritt des Gesamtlaufs durchlaufen.
    showProgress("Plankopf-Daten aus CSV setzen...");
    loadPlankopfCsv();
    const OcPairs plankopfPairs = pairsFromMap(m_plankopfCsvData);

    const QStringList projektblaetter = findProjektblaetter();
    if (!projektblaetter.isEmpty()) {
        log(QString("%1 Projektblatt/-blaetter auf der obersten Ebene erhalten Plankopf-Daten")
            .arg(projektblaetter.size()));
    }
    if (!plankopfPairs.isEmpty()) {
        beginPhase("Projektblaetter", projektblaetter.size());
        for (const QString& path : projektblaetter) {
            processDrawing(path, counts, [&](OcDrawing& dwg) {
                dwg.setAttributes(plankopfPairs);
            });
            stepDone(QFileInfo(path).fileName());
        }
    }

    // Step 2: Deckblaetter
    showProgress("Deckblaetter erzeugen...");
    createDeckblaetter(counts);

    // Steps 2.5 bis 5 je Quellzeichnung in einem Zug: Plankopf-Stammdaten,
    // ASP/GEWERK/ANLAGE aus der Ordnerhierarchie, BMK, BAS, Extraktion.
    // Frueher oeffnete jeder Schritt jede Zeichnung einzeln. Das Ergebnis ist
    // dasselbe, weil kein Schritt auf einen spaeteren Schritt einer anderen
    // Zeichnung zurueckgreift; die BMK-Zaehler laufen wie bisher in der
    // Reihenfolge der Zeichnungen weiter.
    const bool doBmk = m_chkIncludeBmk->isChecked();
    bool doBas = m_chkIncludeBas->isChecked();
    QVector<OcBasSegment> basSegments;
    if (!doBmk) {
        log("BMK-Nummerierung uebersprungen (deaktiviert)");
    }
    if (doBas) {
        bool found = false;
        basSegments = OcEngine::parseBasCsv(referencePath(OpenCirtConfig::BAS_CSV), &found);
        if (!found) {
            logError("BAS.csv nicht gefunden - uebersprungen");
            doBas = false;
        } else if (basSegments.isEmpty()) {
            logError("BAS.csv enthaelt keine Segmente - BAS-Generierung uebersprungen");
            doBas = false;
        } else {
            log(QString("BAS.csv geladen: %1 Segmente, Aufbau: %2")
                .arg(basSegments.size()).arg(OcEngine::basLayoutText(basSegments)));
        }
    } else {
        log("BAS-Generierung uebersprungen (deaktiviert)");
    }

    showProgress(QString("Quellzeichnungen: Plankopf%1%2, Extraktion...")
        .arg(doBmk ? ", BMK" : "", doBas ? ", BAS" : ""));

    // Zaehler eines frueheren Laufs duerfen nicht weiterzaehlen
    if (doBmk) removeBmkCounters();

    OcLogFile extractLog;
    extractLog.open(m_extractTempDir + "/extractdp_log.txt", true);
    QMap<QString, QStringList> extracted;   // Zeichnung -> Zeilen ihrer CSV
    int hierarchieCount = 0;

    beginPhase("Quellzeichnungen", dwgFiles.size());
    for (const QString& path : dwgFiles) {
        const QString folder = QFileInfo(path).absolutePath();
        QString asp, gewerk, anlage;
        deriveHierarchieFromFolder(folder, asp, gewerk, anlage);

        // Der Tempordner spiegelt den Zeichnungsordner, damit gleichnamige
        // Zeichnungen verschiedener Anlagen einander nicht ueberschreiben
        const QString subDir = m_extractTempDir + "/" + drawingsRoot.relativeFilePath(folder);
        QDir().mkpath(subDir);

        processDrawing(path, counts, [&](OcDrawing& dwg) {
            if (!plankopfPairs.isEmpty()) {
                dwg.setAttributes(plankopfPairs);
            }
            if (!asp.isEmpty()) {
                dwg.setAttributes(hierarchiePairs(asp, gewerk, anlage),
                                  QStringLiteral("OC_RSH_Plankopf_quer*"));
                ++hierarchieCount;
            }
            if (doBmk) {
                counts.bmk += dwg.bmkNummerierung().numbered;
            }
            if (doBas) {
                counts.bas += dwg.genBas(basSegments);
            }
            const OcExtractResult ex = dwg.extractDp(subDir, &extractLog);
            if (ex.ok) {
                extracted.insert(path, ex.lines);
                counts.datenpunkte += ex.dpCount;
            } else {
                logError(QString("%1: %2").arg(QFileInfo(path).fileName(), ex.error));
                ++counts.errors;
            }
        });
        stepDone(QFileInfo(path).fileName());
    }
    extractLog.close();

    // Die Zaehler reichen die BMK-Nummern von Zeichnung zu Zeichnung weiter.
    // Nach der letzten Zeichnung haben sie ihren Zweck erfuellt.
    if (doBmk) {
        const int removed = removeBmkCounters();
        if (removed > 0) {
            log(QString("BMK-Zaehlerdateien entfernt: %1").arg(removed));
        }
    }

    if (!plankopfPairs.isEmpty()) {
        log(QString("Plankopf-Stammdaten: %1 Attribute in %2 Dateien")
            .arg(m_plankopfCsvData.size()).arg(dwgFiles.size() + projektblaetter.size()));
    }
    log(QString("Plankopf-Attribute: %1 von %2 Dateien erhalten ASP/GEWERK/ANLAGE aus Ordnerhierarchie")
        .arg(hierarchieCount).arg(dwgFiles.size()));
    if (doBmk) {
        log(QString("BMK-Nummerierung: %1 Kennzeichen vergeben").arg(counts.bmk));
    }
    if (doBas) {
        log(QString("BAS-Generierung: %1 BAS-Strings geschrieben").arg(counts.bas));
    }
    logSuccess(QString("Phase 1 abgeschlossen: %1 Datenpunkte extrahiert")
               .arg(counts.datenpunkte));

    // Phase 2: GA-FL-Blaetter
    QVector<SourceDrawingInfo> drawings = readExtractedData(dwgFiles);
    if (drawings.isEmpty()) {
        logError("Keine extrahierten Datenpunkte gefunden.");
        return false;
    }
    if (!confirmMissingReferences(drawings)) {
        return false;
    }

    showProgress("GA-FL-Blaetter erzeugen und befuellen...");
    createGaFlSheets(drawings, extracted, refCsvPath, counts);

    // Phase 3: Summen aus den fertigen GA-FL-Blaettern, danach Textbreiten
    createSummen(counts);
    return true;
}

// ============================================================================
// Sensorliste Generation (One-Shot, eigenstaendiger Button)
// ============================================================================

void OpenCirtTab::onSensorListeGenerate() {
    if (!requireProjectStructure()) return;

    log("=== SENSORLISTE ERSTELLEN ===");

    // --- 1. sensor.csv laden ---
    QString sensorCsvPath = projectPath(OpenCirtConfig::REFERENZEN_DIR) + "/sensor.csv";
    if (!QFileInfo::exists(sensorCsvPath)) {
        reportError("Sensorliste",
            "sensor.csv nicht gefunden: " + QDir::toNativeSeparators(sensorCsvPath),
            "Bitte sensor.csv im Ordner '01- Referenzen' anlegen.\n"
            "Zeile 1 = Header, ab Zeile 2 = Keywords (Spalte 1).", false);
        return;
    }

    SensorKeywordLoader keywords;
    if (!keywords.load(sensorCsvPath)) {
        logError("Keine Keywords in sensor.csv gefunden!");
        return;
    }
    log(QString("%1 Sensor-Keywords geladen").arg(keywords.keywordCount()));

    // --- 2. Datenpunkte aus den Zeichnungen lesen ---
    // Die Liste liest die Zeichnungen des geladenen Projekts selbst. Frueher
    // griff sie auf die Extraktion des letzten Gesamtlaufs im Tempordner
    // zurueck - die konnte zu einem anderen Projekt oder zu einem aelteren
    // Stand der Zeichnungen gehoeren.
    QStringList dwgFiles = findProjectDwgs();
    if (dwgFiles.isEmpty()) {
        logError("Keine DWG-Dateien im Projekt gefunden!");
        return;
    }

    QStringList unreadable;
    QVector<SourceDrawingInfo> drawings;
    beginRun(m_btnSensorliste, "Sensorliste wird erstellt...");
    const bool extracted = extractDrawings(dwgFiles, QStringLiteral("Sensorliste"), unreadable);
    if (extracted && unreadable.isEmpty()) {
        drawings = readExtractedData(dwgFiles);
    }
    endRun();

    if (!extracted) return;
    if (!unreadable.isEmpty()) {
        logError(QString("Sensorliste nicht erstellt: %1 Zeichnung(en) nicht lesbar")
                 .arg(unreadable.size()));
        showListWarning(this, "Sensorliste",
            "Diese Zeichnungen liessen sich nicht lesen:",
            unreadable,
            "Die Sensorliste waere unvollstaendig und wurde nicht erstellt.");
        return;
    }
    if (drawings.isEmpty()) {
        reportError("Sensorliste",
            QString("Keine aktiven Datenpunkte in den %1 Zeichnungen des Projekts gefunden")
                .arg(dwgFiles.size()), QString(), false);
        return;
    }

    // --- 3. Datenpunkte filtern ---
    struct SensorEntry {
        QString asp;
        QString anlage;
        QString bezeichnung;
        QString bmk;
        QString bas;
        QString fuehlertyp;
    };

    QList<SensorEntry> sensors;
    QSet<QString> seenBmk;  // Deduplizierung: pro ASP+BMK nur einmal
    int totalDp = 0;

    for (const SourceDrawingInfo& d : drawings) {
        QString asp = d.plankopfAttributes.value("ASP", d.aspName);
        QString anlage = d.anlage;

        for (const DataPoint& dp : d.dataPoints) {
            totalDp++;
            if (keywords.isSensor(dp.produkt)) {
                // Deduplizierung: Ein Fuehler hat oft MW_xx und TL_xx,
                // ist aber das gleiche physische Geraet -> nur einmal listen
                // Key muss Anlage enthalten: TVL-01 in WPL-1010 != TVL-01 in HZK-1010
                QString dedupKey = asp + "|" + anlage + "|" + dp.aks;
                if (seenBmk.contains(dedupKey)) continue;
                seenBmk.insert(dedupKey);

                SensorEntry e;
                e.asp = asp;
                e.anlage = anlage;
                e.bezeichnung = dp.bezeichnung;
                e.bmk = dp.aks;

                // BAS: Funktionscode am Ende entfernen
                // BAS_DP z.B. "KOE46-ASP01-HZG-WPL-1010-U20.00.601-TVL-01-MW_01"
                // BMK z.B. "TVL-01" -> BAS bis einschl. BMK: "KOE46-...-TVL-01"
                e.bas = dp.basString;
                int bmkPos = e.bas.lastIndexOf(dp.aks);
                if (bmkPos >= 0) {
                    e.bas = e.bas.left(bmkPos + dp.aks.length());
                }

                e.fuehlertyp = dp.produkt;
                sensors.append(e);
            }
        }
    }

    log(QString("%1 von %2 Datenpunkten als Sensor erkannt")
        .arg(sensors.size()).arg(totalDp));

    if (sensors.isEmpty()) {
        logError("Keine Sensoren gefunden! Keywords in sensor.csv pruefen.");
        QMessageBox::information(this, "Sensorliste",
            QString("Keine Datenpunkte matched die Keywords aus sensor.csv.\n\n"
                    "Geprueft: %1 Datenpunkte mit %2 Keywords.")
            .arg(totalDp).arg(keywords.keywordCount()));
        return;
    }

    // --- 4. CSV schreiben ---
    // Ausgabe in Plot-Ordner (zentraler Ausgabeort)
    QString plotDir = m_projectRoot + "/06- Plot";
    QDir().mkpath(plotDir);
    QString outputPath = plotDir + "/Sensorliste.csv";

    CsvListWriter writer;
    if (!writer.open(outputPath, {
            QStringLiteral("Pos.:"),
            QStringLiteral("ASP"),
            QStringLiteral("Anlage:"),
            QStringLiteral("Klartextbezeichnung:"),
            QStringLiteral("BMK:"),
            QStringLiteral("BAS:"),
            QString::fromUtf8("F\xC3\xBC" "hlertyp:")})) {
        logError("CSV-Fehler: " + writer.lastError());
        return;
    }

    // Datenzeilen (7 Spalten: Pos, ASP, Anlage, Bezeichnung, BMK, BAS, Typ)
    int pos = 1;
    for (const SensorEntry& s : sensors) {
        writer.addRow({
            QString("%1").arg(pos, 3, 10, QChar('0')),
            s.asp,
            s.anlage,
            s.bezeichnung,
            s.bmk,
            s.bas,
            s.fuehlertyp
        });
        pos++;
    }

    if (!writer.save()) {
        logError("CSV-Speichern fehlgeschlagen: " + writer.lastError());
        return;
    }

    logSuccess(QString("Sensorliste erstellt: %1 Eintraege -> %2")
               .arg(sensors.size()).arg(outputPath));

    QMessageBox::information(this, "Sensorliste",
        QString("Sensorliste erfolgreich erstellt!\n\n"
                "%1 Sensoren aus %2 Datenpunkten\n\n"
                "Ausgabe: %3")
        .arg(sensors.size()).arg(totalDp).arg(outputPath));
}

// ============================================================================
// GA-FL-Blaetter direkt lesen (Side-Database, ohne Editor)
// ============================================================================
//
// Die Datenpunkt-/IO-Liste wird aus den fertigen GA-FL-Blaettern erzeugt und
// nicht mehr aus der Extraktion plus ODS-Referenz. Damit gibt es nur noch eine
// Wahrheit: was im Blatt steht, steht in der Liste - auch dann, wenn im Blatt
// von Hand nachgebessert wurde. Ein Auseinanderlaufen von Zeichnung und Liste
// ist damit ausgeschlossen.
//
// Gelesen wird ueber eine Side-Database. Die Zeichnung wird also nicht im
// Editor geoeffnet: kein Bildaufbau, kein Flackern, keine Dateisperre.

struct SheetAttributes {
    QMap<QString, QString> plankopf;  ///< Attribute des Plankopf-Blocks
    QMap<QString, QString> gaFl;      ///< Attribute des GA-FL-Datenblocks
    bool ok = false;                  ///< false = Datei nicht lesbar
};

/// Alle Attribute einer Blockreferenz einsammeln (Tag in Grossschreibung)
static void collectBlockAttributes(AcDbBlockReference* pRef, QMap<QString, QString>& out)
{
    AcDbObjectIterator* pAttIter = pRef->attributeIterator();
    while (pAttIter && !pAttIter->done()) {
        AcDbObject* pAttObj = nullptr;
        if (acdbOpenObject(pAttObj, pAttIter->objectId(), AcDb::kForRead) == Acad::eOk && pAttObj) {
            AcDbAttribute* pAtt = AcDbAttribute::cast(pAttObj);
            if (pAtt) {
                const ACHAR* tag = pAtt->tag();
                if (tag) {
                    const ACHAR* val = pAtt->textString();
                    out.insert(QString::fromWCharArray(tag).toUpper(),
                               val ? QString::fromWCharArray(val) : QString());
                }
            }
            pAttObj->close();
        }
        pAttIter->step();
    }
    delete pAttIter;
}

/// Liest Plankopf- und GA-FL-Attribute eines Blatts.
static SheetAttributes readSheetAttributes(const QString& dwgPath, const QString& gaFlBlockName)
{
    SheetAttributes result;

    AcDbDatabase* pDb = new AcDbDatabase(false, false);
    std::wstring wpath = dwgPath.toStdWString();

    if (pDb->readDwgFile(wpath.c_str(), AcDbDatabase::kForReadAndAllShare, false) != Acad::eOk) {
        delete pDb;
        return result;  // ok bleibt false
    }

    AcDbBlockTable* pBT = nullptr;
    if (pDb->getBlockTable(pBT, AcDb::kForRead) != Acad::eOk || !pBT) {
        delete pDb;
        return result;
    }

    AcDbBlockTableRecord* pMS = nullptr;
    Acad::ErrorStatus es = pBT->getAt(ACDB_MODEL_SPACE, pMS, AcDb::kForRead);
    pBT->close();
    if (es != Acad::eOk || !pMS) {
        delete pDb;
        return result;
    }

    AcDbBlockTableRecordIterator* pIter = nullptr;
    pMS->newIterator(pIter);

    while (pIter && !pIter->done()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForRead) == Acad::eOk && pEnt) {
            AcDbBlockReference* pRef = AcDbBlockReference::cast(pEnt);
            if (pRef) {
                AcDbBlockTableRecord* pBlkRec = nullptr;
                if (acdbOpenObject(pBlkRec, pRef->blockTableRecord(), AcDb::kForRead) == Acad::eOk
                    && pBlkRec) {
                    const ACHAR* blkName = nullptr;
                    pBlkRec->getName(blkName);
                    QString qBlkName = blkName ? QString::fromWCharArray(blkName) : QString();
                    pBlkRec->close();

                    if (qBlkName.startsWith(QStringLiteral("OC_RSH_Plankopf_quer"),
                                            Qt::CaseInsensitive)) {
                        if (result.plankopf.isEmpty())
                            collectBlockAttributes(pRef, result.plankopf);
                    } else if (qBlkName.compare(gaFlBlockName, Qt::CaseInsensitive) == 0) {
                        if (result.gaFl.isEmpty())
                            collectBlockAttributes(pRef, result.gaFl);
                    }
                }
            }
            pEnt->close();
        }
        pIter->step();
    }

    delete pIter;
    pMS->close();
    delete pDb;

    result.ok = true;
    return result;
}

// ============================================================================
// Datenpunkt-Export (One-Shot, eigenstaendiger Button)
// ============================================================================
//
// Ein Button fuer beide Anwendungsfaelle. Im Dialog wird nach Integrationsart
// gefiltert (Attribut OC_INTEGRATIONSART_DP_n, Werte laut Vokabular z.B.
// HW / BUS / SMI / KNX / virtuell):
//
//   leer      -> alle Datenpunkte
//   HW        -> nur Hardware, das ist die klassische SPS-/DDC-Belegungsliste
//   BUS;SMI   -> mehrere Arten, semikolongetrennt
//
// Die Spalte "Modul-Typ" wird ausschliesslich fuer HW-Zeilen befuellt; alle
// anderen Integrationsarten belegen keinen Klemmenkanal. Wer die Spalte nicht
// braucht, loescht sie in der erzeugten CSV-Datei.

void OpenCirtTab::onDatenpunktExport() {
    if (!requireProjectStructure()) return;

    // --- 0. Filterdialog ---
    QString filterText;
    {
        QDialog dlg(this);
        dlg.setWindowTitle("Datenpunkte exportieren");

        auto* layout = new QVBoxLayout(&dlg);

        auto* info = new QLabel(
            "<b>Filter nach Integrationsart</b><br><br>"
            "Die Liste wird aus den fertigen <b>GA-FL-Blaettern</b> erzeugt. "
            "Gefiltert wird nach dem Wert, der dort in der Spalte "
            "<i>Integration</i> steht &ndash; was im Blatt steht, steht in der "
            "Liste, auch nach Korrekturen von Hand.<br><br>"
            "&nbsp;&nbsp;<tt>(leer)</tt>&nbsp;&nbsp;&ndash; alle Datenpunkte<br>"
            "&nbsp;&nbsp;<tt>HW</tt>&nbsp;&nbsp;&ndash; nur Hardware "
            "(SPS-/DDC-Belegungsliste)<br>"
            "&nbsp;&nbsp;<tt>BUS;SMI</tt>&nbsp;&nbsp;&ndash; mehrere Arten, "
            "semikolongetrennt<br><br>"
            "Gross-/Kleinschreibung ist egal. Die Spalte <i>Modul-Typ</i> "
            "(Modul&nbsp;/&nbsp;Kanal inkl. Reserve) wird nur fuer "
            "<tt>HW</tt>-Zeilen gefuellt &ndash; alle anderen Integrationsarten "
            "belegen keinen Klemmenkanal.", &dlg);
        info->setWordWrap(true);
        info->setTextFormat(Qt::RichText);
        layout->addWidget(info);

        auto* rowLayout = new QHBoxLayout();
        rowLayout->addWidget(new QLabel("Integrationsart(en):", &dlg));
        auto* edit = new QLineEdit(m_dpExportFilter, &dlg);
        edit->setPlaceholderText("leer = alle Datenpunkte");
        edit->setMinimumWidth(240);
        edit->selectAll();
        rowLayout->addWidget(edit);
        layout->addLayout(rowLayout);

        auto* buttons = new QDialogButtonBox(
            QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dlg);
        connect(buttons, &QDialogButtonBox::accepted, &dlg, &QDialog::accept);
        connect(buttons, &QDialogButtonBox::rejected, &dlg, &QDialog::reject);
        layout->addWidget(buttons);

        if (dlg.exec() != QDialog::Accepted) return;
        filterText = edit->text().trimmed();
    }

    m_dpExportFilter = filterText;

    // Filterliste normalisieren (Grossschreibung, ohne Leereintraege)
    QStringList filterList;
    for (const QString& part : filterText.split(';', Qt::SkipEmptyParts)) {
        QString v = part.trimmed().toUpper();
        if (!v.isEmpty() && !filterList.contains(v)) filterList << v;
    }
    const bool filterAll = filterList.isEmpty();
    const bool wantsHw = filterAll || filterList.contains("HW");

    log("=== DATENPUNKTE EXPORTIEREN ===");
    log(filterAll ? QString("Filter: alle Integrationsarten")
                  : QString("Filter Integrationsart: %1").arg(filterList.join(", ")));

    // --- 1. iomodule.csv laden (Kanalaufteilung fuer HW) ---
    QString ioModuleCsvPath = projectPath(OpenCirtConfig::REFERENZEN_DIR) + "/iomodule.csv";
    if (!QFileInfo::exists(ioModuleCsvPath)) {
        reportError("Datenpunkte exportieren",
            "iomodule.csv nicht gefunden: " + QDir::toNativeSeparators(ioModuleCsvPath),
            "Bitte iomodule.csv im Ordner '01- Referenzen' anlegen.", false);
        return;
    }

    // IO-Typ Definitionen: Name, Kurzname, Zaehlspalte im GA-FL-Block, Kanalanzahl
    struct IoModuleDef {
        QString label;      // Aus CSV, z.B. "Analoge Eingaenge"
        QString shortName;  // AI, DI, AO, DO
        QString attrBase;   // Attributbasis im GA-FL-Block, z.B. "OC_1_1_1"
        int channels;       // Kanaele pro Modul
    };

    // Feste Zuordnung: Keyword aus iomodule.csv -> Kurzname + GA-FL-Zaehlspalte
    struct IoTypeMapping {
        QString keyword;
        QString shortName;
        QString attrBase;
    };
    QList<IoTypeMapping> mappings = {
        {"Analoge Eing",  "AI", "OC_1_1_1"},
        {"Digitale Eing", "DI", "OC_1_1_2"},
        {"Analoge Ausg",  "AO", "OC_1_1_3"},
        {"Digitale Ausg", "DO", "OC_1_1_4"},
    };

    QList<IoModuleDef> moduleDefs;
    {
        QFile f(ioModuleCsvPath);
        if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
            logError("iomodule.csv nicht lesbar");
            return;
        }
        QTextStream in(&f);
        in.setEncoding(QStringConverter::Utf8);
        bool firstLine = true;
        while (!in.atEnd()) {
            QString line = in.readLine().trimmed();
            if (firstLine) { firstLine = false; continue; }
            if (line.isEmpty()) continue;

            QStringList parts = line.split(';');
            if (parts.size() < 2) continue;

            QString label = parts[0].trimmed();
            int channels = parts[1].trimmed().toInt();
            if (channels <= 0) continue;

            // Mapping finden
            for (const IoTypeMapping& m : mappings) {
                if (label.contains(m.keyword, Qt::CaseInsensitive)) {
                    IoModuleDef def;
                    def.label = label;
                    def.shortName = m.shortName;
                    def.attrBase = m.attrBase;
                    def.channels = channels;
                    moduleDefs.append(def);
                    break;
                }
            }
        }
        f.close();
    }

    if (moduleDefs.isEmpty()) {
        logError("Keine gueltigen Modultypen in iomodule.csv!");
        return;
    }
    for (const IoModuleDef& md : moduleDefs) {
        log(QString("  Modultyp: %1 (%2) = %3 Kanaele/Modul")
            .arg(md.label, md.shortName).arg(md.channels));
    }

    // Index fuer Zeilen ohne Kanalbelegung (sortiert hinter allen HW-Zeilen)
    const int NO_IO = static_cast<int>(moduleDefs.size());

    // --- 2. GA-FL-Blaetter einsammeln ---
    QStringList gaFlFiles;
    {
        QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
        QDirIterator it(drawingsDir, QStringList() << "*_GA_FL_*.dwg", QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) gaFlFiles << it.next();
        gaFlFiles.sort(Qt::CaseInsensitive);
    }

    if (gaFlFiles.isEmpty()) {
        reportError("Datenpunkte exportieren",
            "Keine GA-FL-Blaetter im Zeichnungsordner gefunden",
            "Die Liste wird aus den fertigen Blaettern erzeugt - bitte zuerst "
            "'Projekt erstellen' vollstaendig durchlaufen lassen.", false);
        return;
    }

    showProgress(QString("%1 GA-FL-Blaetter lesen...").arg(gaFlFiles.size()));

    // --- 3. Datenpunkte filtern und auf IO-Typen verteilen ---
    struct IoEntry {
        QString asp;
        QString anlage;
        QString bezeichnung;
        QString bmk;
        QString bas;
        QString integ;    // Integrationsart wie im Blatt, Originalschreibweise
        int ioTypeIndex;  // Index in moduleDefs, oder NO_IO = keine Kanalbelegung
    };

    QMap<QString, QList<IoEntry>> aspIoEntries;
    int totalIo = 0;         // Zeilen mit Kanalbelegung (HW)
    int totalRows = 0;       // Zeilen gesamt
    int totalDp = 0;         // gelesene Datenpunkte
    int matchedDp = 0;       // Datenpunkte nach Filter
    int unlesbareBlaetter = 0;
    QMap<QString, int> integCounts;   // Integrationsart -> Anzahl DPs
    QStringList mehrfachBelegung;     // Zaehlwerte > 1

    int sheetNr = 0;
    for (const QString& sheetPath : gaFlFiles) {
        // Fortschritt in den Balken, nicht ins Protokoll: das Lesen der
        // Blaetter ist ein Zwischenschritt, im Protokoll bleiben die Ergebnisse.
        if (++sheetNr % 20 == 0) {
            showProgress(QString("GA-FL lesen: %1 von %2 Blaettern")
                         .arg(sheetNr).arg(gaFlFiles.size()));
        }

        SheetAttributes sa = readSheetAttributes(
            sheetPath, QString::fromLatin1(OpenCirtConfig::GA_FL_BLOCK_NAME));

        if (!sa.ok || sa.gaFl.isEmpty()) {
            unlesbareBlaetter++;
            logError(QString("  Kein GA-FL-Block lesbar: %1")
                     .arg(QFileInfo(sheetPath).fileName()));
            continue;
        }

        // ASP und Anlage aus dem Plankopf des Blatts, ersatzweise aus der Ordnerhierarchie
        QString asp    = sa.plankopf.value(QStringLiteral("ASP")).trimmed();
        QString anlage = sa.plankopf.value(QStringLiteral("ANLAGE")).trimmed();
        if (asp.isEmpty() || anlage.isEmpty()) {
            QString fAsp, fGewerk, fAnlage;
            deriveHierarchieFromFolder(QFileInfo(sheetPath).absolutePath(),
                                       fAsp, fGewerk, fAnlage);
            if (asp.isEmpty())    asp = fAsp;
            if (anlage.isEmpty()) anlage = fAnlage;
        }
        if (asp.isEmpty()) asp = QStringLiteral("(ohne ASP)");

        for (int n = 1; n <= OpenCirtConfig::MAX_DP_FIRST_SHEET; ++n) {
            const QString suffix = QString("_DP_%1").arg(n);

            QString bez = sa.gaFl.value(QStringLiteral("OC_BEZEICHNUNG") + suffix).trimmed();
            QString bas = sa.gaFl.value(QStringLiteral("OC_AKS") + suffix).trimmed();

            // Leere Zeile
            if (bez.isEmpty() && bas.isEmpty()) continue;

            // Zeile 1 der Folgeblaetter traegt den Uebertrag, keinen Datenpunkt
            if (bas.isEmpty() &&
                (bez.compare(QStringLiteral("Uebertrag"), Qt::CaseInsensitive) == 0 ||
                 bez.compare(QString::fromUtf8("\xC3\x9C" "bertrag"), Qt::CaseInsensitive) == 0)) {
                continue;
            }

            totalDp++;

            QString integRaw = sa.gaFl.value(QStringLiteral("OC_INTEG") + suffix).trimmed();
            QString integ = integRaw.toUpper();
            integCounts[integ.isEmpty() ? QStringLiteral("(leer)") : integ]++;

            if (!filterAll && !filterList.contains(integ)) continue;
            matchedDp++;

            // OC_BEZEICHNUNG_DP_n traegt "BMK - Klartext" (siehe FillGaFl.lsp)
            QString bmk;
            QString klartext = bez;
            int sep = bez.indexOf(QStringLiteral(" - "));
            if (sep > 0) {
                bmk = bez.left(sep).trimmed();
                klartext = bez.mid(sep + 3).trimmed();
            }

            const bool isHw = (integ == QLatin1String("HW"));

            IoEntry base;
            base.asp = asp;
            base.anlage = anlage;
            base.bezeichnung = klartext;
            base.bmk = bmk;
            base.bas = bas;
            base.integ = integRaw;
            base.ioTypeIndex = NO_IO;

            bool placed = false;

            // Kanalbelegung nur fuer Hardware, aus den Zaehlspalten des Blatts
            if (isHw) {
                for (int mi = 0; mi < NO_IO; ++mi) {
                    const IoModuleDef& md = moduleDefs[mi];

                    QString cellVal = sa.gaFl.value(md.attrBase + suffix).trimmed();
                    if (cellVal.isEmpty()) continue;

                    bool ok;
                    int numVal = cellVal.toInt(&ok);
                    if (!ok || numVal <= 0) continue;

                    // Ein Datenpunkt belegt genau einen Kanal - er traegt genau
                    // ein AKS/BAS und wird zeilenweise dargestellt. Ein Wert > 1
                    // ist ein Pflegefehler in der Referenz und wird gemeldet.
                    if (numVal > 1 && mehrfachBelegung.size() < 50) {
                        mehrfachBelegung << QString("%1 / %2: %3 = %4")
                                            .arg(bmk.isEmpty() ? klartext : bmk,
                                                 QFileInfo(sheetPath).completeBaseName(),
                                                 md.shortName)
                                            .arg(numVal);
                    }

                    IoEntry e = base;
                    e.ioTypeIndex = mi;
                    aspIoEntries[asp].append(e);
                    totalIo++;
                    totalRows++;
                    placed = true;
                }
            }

            // Nicht-Hardware oder Hardware ohne Klemmenbedarf: eine Zeile ohne Modul
            if (!placed) {
                aspIoEntries[asp].append(base);
                totalRows++;
            }
        }
    }

    // Fortschritt ausblenden - der Rest laeuft ohne spuerbare Wartezeit.
    hideProgress();

    if (unlesbareBlaetter > 0) {
        log(QString("%1 Blatt/Blaetter ohne lesbaren GA-FL-Block uebersprungen")
            .arg(unlesbareBlaetter), "WARN");
    }

    log(QString("%1 von %2 Datenpunkten nach Filter, davon %3 mit Kanalbelegung")
        .arg(matchedDp).arg(totalDp).arg(totalIo));
    {
        QStringList verteilung;
        for (auto it = integCounts.constBegin(); it != integCounts.constEnd(); ++it) {
            verteilung << QString("%1=%2").arg(it.key()).arg(it.value());
        }
        log(QString("Integrationsarten im Projekt: %1").arg(verteilung.join(", ")));
    }

    if (totalRows == 0) {
        logError("Keine Datenpunkte nach Filter uebrig!");
        QStringList verteilung;
        for (auto it = integCounts.constBegin(); it != integCounts.constEnd(); ++it) {
            verteilung << QString("  %1: %2 Datenpunkte").arg(it.key()).arg(it.value());
        }
        QMessageBox::information(this, "Datenpunkte exportieren",
            QString("Kein Datenpunkt passt zum Filter '%1'.\n\n"
                    "Im Projekt vorhandene Integrationsarten:\n%2\n\n"
                    "Steht dort nur '(leer)', ist die Spalte Integration in den "
                    "GA-FL-Blaettern nicht gefuellt - dann zuerst 'Projekt erstellen' "
                    "erneut durchlaufen lassen.")
            .arg(filterText.isEmpty() ? QStringLiteral("(leer)") : filterText,
                 verteilung.join("\n")));
        return;
    }

    // --- 4. Module zuweisen und CSV schreiben ---
    QString plotDir = m_projectRoot + "/06- Plot";
    QDir().mkpath(plotDir);

    // Ausgabename richtet sich nach dem Filter
    QString outputName;
    if (filterAll) {
        outputName = "Datenpunktliste.csv";
    } else if (filterList.size() == 1 && wantsHw) {
        outputName = "IO-Belegungsliste.csv";
    } else {
        outputName = "Datenpunktliste_" + filterList.join("-") + ".csv";
    }
    QString outputPath = plotDir + "/" + outputName;

    CsvListWriter writer;
    if (!writer.open(outputPath, {
            QStringLiteral("Pos.:"),
            QStringLiteral("ASP"),
            QStringLiteral("Anlage:"),
            QStringLiteral("Klartextbezeichnung:"),
            QStringLiteral("BMK:"),
            QStringLiteral("BAS:"),
            QStringLiteral("Integrationsart:"),
            QStringLiteral("Modul-Typ:")})) {
        logError("CSV-Fehler: " + writer.lastError());
        return;
    }

    int pos = 1;

    // Pro ASP: nach IO-Typ gruppiert, Module zuweisen
    for (auto aspIt = aspIoEntries.constBegin(); aspIt != aspIoEntries.constEnd(); ++aspIt) {
        const QList<IoEntry>& entries = aspIt.value();

        // Sortieren: nach IO-Typ (Zeilen ohne Kanal zuletzt), dann Anlage, dann BMK
        QList<IoEntry> sorted = entries;
        std::sort(sorted.begin(), sorted.end(), [](const IoEntry& a, const IoEntry& b) {
            if (a.ioTypeIndex != b.ioTypeIndex) return a.ioTypeIndex < b.ioTypeIndex;
            if (a.anlage != b.anlage) return a.anlage < b.anlage;
            return a.bmk < b.bmk;
        });

        // Kanalzaehler pro IO-Typ: modulNr und kanalNr
        QMap<int, int> moduleNr;  // ioTypeIndex -> aktuelles Modul (1-basiert)
        QMap<int, int> channelNr; // ioTypeIndex -> aktueller Kanal (1-basiert)

        for (int si = 0; si < sorted.size(); ++si) {
            const IoEntry& e = sorted[si];

            // Zeile ohne Kanalbelegung (Nicht-HW oder HW ohne Klemmenbedarf)
            if (e.ioTypeIndex >= NO_IO) {
                writer.addRow({
                    QString("%1").arg(pos, 3, 10, QChar('0')),
                    e.asp,
                    e.anlage,
                    e.bezeichnung,
                    e.bmk,
                    e.bas,
                    e.integ,
                    QString()   // Modul-Typ leer
                });
                pos++;
                continue;
            }

            const IoModuleDef& md = moduleDefs[e.ioTypeIndex];

            // Modul/Kanal initialisieren falls noetig
            if (!moduleNr.contains(e.ioTypeIndex)) {
                moduleNr[e.ioTypeIndex] = 1;
                channelNr[e.ioTypeIndex] = 1;
            }

            int mod = moduleNr[e.ioTypeIndex];
            int ch = channelNr[e.ioTypeIndex];

            QString modulTyp = QString("%1 Modul %2 / Kanal %3")
                               .arg(md.shortName).arg(mod).arg(ch);

            writer.addRow({
                QString("%1").arg(pos, 3, 10, QChar('0')),
                e.asp,
                e.anlage,
                e.bezeichnung,
                e.bmk,
                e.bas,
                e.integ,
                modulTyp
            });
            pos++;

            // Naechster Kanal, ggf. naechstes Modul
            ch++;
            if (ch > md.channels) {
                ch = 1;
                mod++;
            }
            moduleNr[e.ioTypeIndex] = mod;
            channelNr[e.ioTypeIndex] = ch;

            // Reserve-Kanaele auffuellen wenn IO-Typ wechselt oder letzter Eintrag
            bool isLastOfType = (si == sorted.size() - 1)
                || (sorted[si + 1].ioTypeIndex != e.ioTypeIndex);

            if (isLastOfType && ch > 1) {
                // Modul ist angefangen aber nicht voll -> Reserve bis Ende
                int currentMod = moduleNr[e.ioTypeIndex];
                int currentCh = channelNr[e.ioTypeIndex];
                while (currentCh <= md.channels) {
                    QString resModulTyp = QString("%1 Modul %2 / Kanal %3")
                                         .arg(md.shortName).arg(currentMod).arg(currentCh);
                    // Reservekanal: der Klemmenplatz existiert physisch und gehoert
                    // zum ASP - nur Datenpunktangaben bleiben leer.
                    writer.addRow({
                        QString("%1").arg(pos, 3, 10, QChar('0')),
                        e.asp,       // ASP gesetzt - der Kanal ist real vorhanden
                        QString(),   // Anlage leer
                        "Reserve",   // Klartext
                        QString(),   // BMK leer
                        QString(),   // BAS leer
                        QString(),   // Integrationsart leer - kein Datenpunkt
                        resModulTyp
                    });
                    pos++;
                    currentCh++;
                }
                // Modul ist jetzt voll, naechstes Modul bereit
                moduleNr[e.ioTypeIndex] = currentMod + 1;
                channelNr[e.ioTypeIndex] = 1;
            }
        }

        // Reserve-Kanaele auffuellen: nicht volle Module mit "Reserve" auffuellen
        for (int mi = 0; mi < NO_IO; ++mi) {
            if (!channelNr.contains(mi)) continue;  // IO-Typ nicht verwendet

            int ch = channelNr[mi];
            if (ch == 1) continue;  // Modul ist voll oder kein angefangenes Modul

            const IoModuleDef& md = moduleDefs[mi];
            int mod = moduleNr[mi];

            // Restliche Kanaele des angefangenen Moduls als Reserve
            while (ch <= md.channels) {
                QString modulTyp = QString("%1 Modul %2 / Kanal %3")
                                   .arg(md.shortName).arg(mod).arg(ch);
                // Reservekanal: siehe oben - ASP gesetzt, Datenpunktangaben leer.
                writer.addRow({
                    QString("%1").arg(pos, 3, 10, QChar('0')),
                    aspIt.key(),  // ASP gesetzt - der Kanal ist real vorhanden
                    QString(),    // Anlage leer
                    "Reserve",    // Klartext
                    QString(),    // BMK leer
                    QString(),    // BAS leer
                    QString(),    // Integrationsart leer - kein Datenpunkt
                    modulTyp
                });
                pos++;
                ch++;
            }
        }
    }

    if (!writer.save()) {
        logError("CSV-Speichern fehlgeschlagen: " + writer.lastError());
        return;
    }

    // Zusammenfassung
    QString summary;
    for (auto aspIt = aspIoEntries.constBegin(); aspIt != aspIoEntries.constEnd(); ++aspIt) {
        summary += QString("\n  %1:").arg(aspIt.key());
        QMap<int, int> typeCounts;
        int ohneKanal = 0;
        for (const IoEntry& e : aspIt.value()) {
            if (e.ioTypeIndex >= NO_IO) ohneKanal++;
            else typeCounts[e.ioTypeIndex]++;
        }
        for (auto tc = typeCounts.constBegin(); tc != typeCounts.constEnd(); ++tc) {
            const IoModuleDef& md = moduleDefs[tc.key()];
            int modules = (tc.value() + md.channels - 1) / md.channels;
            summary += QString(" %1x%2 (%3 Module)").arg(tc.value()).arg(md.shortName).arg(modules);
        }
        if (ohneKanal > 0) {
            summary += QString(" %1 ohne Kanal").arg(ohneKanal);
        }
    }

    // pos laeuft ueber alle ASPs durch und wird nach jeder geschriebenen Zeile
    // erhoeht - pos-1 ist damit die tatsaechliche Zeilenzahl der Datei,
    // Reservezeilen eingeschlossen.
    const int zeilenInDatei = pos - 1;
    const int reserveZeilen = zeilenInDatei - totalRows;

    logSuccess(QString("Datenpunkt-Export erstellt: %1 Zeilen "
                       "(%2 Datenpunktzeilen + %3 Reservekanaele) -> %4%5")
               .arg(zeilenInDatei).arg(totalRows).arg(reserveZeilen)
               .arg(outputPath).arg(summary));

    // Warnung bei Referenzwerten > 1
    if (!mehrfachBelegung.isEmpty()) {
        logError(QString("%1 Datenpunkt(e) mit Referenzwert > 1 gefunden")
                 .arg(mehrfachBelegung.size()));
        for (const QString& m : mehrfachBelegung) logError("  " + m);

        showListWarning(this, "Datenpunkte exportieren",
                        "Achtung: Es wurden IOs mit Eintraegen > 1 gefunden.\n\n"
                        "Ein Datenpunkt traegt genau ein AKS/BAS und belegt daher "
                        "genau einen Kanal. Ein Referenzwert groesser 1 ist ein "
                        "Pflegefehler in GA_FL_VORLAGE.ods.\n\n"
                        "Betroffen (AKS / Referenz: Typ = Wert):",
                        mehrfachBelegung);
    }

    QMessageBox::information(this, "Datenpunkte exportieren",
        QString("Export erfolgreich!\n\n"
                "Filter: %1\n"
                "%2 Zeilen in der Datei = %3 Datenpunktzeilen + %4 Reservekanaele\n"
                "%5 von %6 Datenpunkten passten zum Filter, davon %7 mit Kanalbelegung\n"
                "%8\n\n"
                "Ausgabe: %9")
        .arg(filterAll ? QStringLiteral("alle Integrationsarten") : filterList.join("; "))
        .arg(zeilenInDatei).arg(totalRows).arg(reserveZeilen)
        .arg(matchedDp).arg(totalDp).arg(totalIo)
        .arg(summary).arg(outputPath));
}

// ============================================================================
// Projekt bereinigen
// ============================================================================

void OpenCirtTab::onProjektBereinigen() {
    if (!requireProjectStructure()) return;
    
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    
    // File patterns to clean up
    QStringList patterns = {
        "*.bak", "*.dwl", "*.dwl2", "*.sv$", "*.ac$",
        "*.tmp", "*.log",
        "crash_report.txt",
        "Thumbs.db", "desktop.ini"
    };
    
    // Scan for matching files
    QStringList filesToDelete;
    qint64 totalSize = 0;
    
    QDirIterator it(drawingsDir, patterns, QDir::Files | QDir::Hidden,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QFileInfo fi(path);
        totalSize += fi.size();
        filesToDelete << path;
    }
    
    if (filesToDelete.isEmpty()) {
        QMessageBox::information(this, "Projekt bereinigen",
            "Keine temporaeren Dateien gefunden.\n\n"
            "Das Projekt ist bereits sauber.");
        log("Projekt bereinigen: Keine temporaeren Dateien gefunden");
        return;
    }
    
    // Build summary by extension
    QMap<QString, int> extCount;
    QMap<QString, qint64> extSize;
    for (const QString& path : filesToDelete) {
        QString name = QFileInfo(path).fileName();
        QString ext;
        if (name == "crash_report.txt" || name == "Thumbs.db" || name == "desktop.ini") {
            ext = name;
        } else {
            ext = "*." + QFileInfo(path).suffix();
        }
        extCount[ext]++;
        extSize[ext] += QFileInfo(path).size();
    }
    
    QString summary;
    for (auto it = extCount.constBegin(); it != extCount.constEnd(); ++it) {
        double sizeMB = extSize[it.key()] / (1024.0 * 1024.0);
        summary += QString("  %1: %2 Dateien (%3 MB)\n")
                   .arg(it.key())
                   .arg(it.value())
                   .arg(sizeMB, 0, 'f', 1);
    }
    
    double totalMB = totalSize / (1024.0 * 1024.0);
    
    QMessageBox::StandardButton reply = QMessageBox::warning(this,
        "Projekt bereinigen",
        QString("Folgende temporaere Dateien wurden in\n"
                "'%1' gefunden:\n\n"
                "%2\n"
                "Gesamt: %3 Dateien (%4 MB)\n\n"
                "Endgueltig loeschen?")
        .arg(OpenCirtConfig::ZEICHNUNGEN_DIR)
        .arg(summary)
        .arg(filesToDelete.size())
        .arg(totalMB, 0, 'f', 1),
        QMessageBox::Yes | QMessageBox::Cancel);
    
    if (reply != QMessageBox::Yes) return;
    
    log("=== PROJEKT BEREINIGEN ===");
    
    int deleted = 0;
    int failed = 0;
    for (const QString& path : filesToDelete) {
        if (QFile::remove(path)) {
            deleted++;
        } else {
            failed++;
            logError(QString("Konnte nicht loeschen: %1").arg(QFileInfo(path).fileName()));
        }
    }
    
    if (failed == 0) {
        logSuccess(QString("Bereinigung abgeschlossen: %1 Dateien geloescht (%2 MB freigegeben)")
                   .arg(deleted).arg(totalMB, 0, 'f', 1));
    } else {
        log(QString("Bereinigung: %1 geloescht, %2 fehlgeschlagen").arg(deleted).arg(failed), "WARN");
    }
}

// ============================================================================
// Projekt aufbauen (Erstellliste)
// ============================================================================

void OpenCirtTab::onProjektAufbauen() {
    const QString title = "Projekt aufbauen";

    if (m_projectRoot.isEmpty()) {
        reportError(title, "Kein Projektordner angegeben",
                    "Bitte oben den Projektordner waehlen.", false);
        return;
    }
    // Nur der Vorlagen-Ordner ist Pflicht: '05- Projekt Zeichnungen' legt der
    // Aufbau selbst an, wenn der Ordner noch fehlt.
    if (!QDir(projectPath(OpenCirtConfig::VORLAGEN_DIR)).exists()) {
        reportError(title, "Vorlagen-Ordner nicht gefunden: "
                    + QDir::toNativeSeparators(projectPath(OpenCirtConfig::VORLAGEN_DIR)),
                    QString(), false);
        return;
    }

    // -------- Erstellliste waehlen --------
    QSettings settings("BatchProcessing", "BricsCAD_Plugin");
    QString startDir = settings.value("openCirt/erstelllisteDir").toString();
    if (startDir.isEmpty() || !QDir(startDir).exists()) {
        startDir = m_projectRoot;
    }
    const QString csvPath = QFileDialog::getOpenFileName(this,
        "CSV-Erstellliste waehlen", startDir,
        "Erstellliste (*.csv);;Alle Dateien (*)");
    if (csvPath.isEmpty()) return;
    settings.setValue("openCirt/erstelllisteDir", QFileInfo(csvPath).absolutePath());

    // -------- Vorschau oder echter Lauf --------
    QMessageBox modeBox(this);
    modeBox.setWindowTitle(title);
    modeBox.setIcon(QMessageBox::Question);
    modeBox.setText(QString("Erstellliste:\n  %1\n\nProjekt:\n  %2")
        .arg(QDir::toNativeSeparators(csvPath),
             QDir::toNativeSeparators(m_projectRoot)));
    modeBox.setInformativeText(
        "Vorschau: schreibt nur das Log, aendert nichts am Projekt.\n\n"
        "Aufbauen: loescht den Inhalt von '05- Projekt Zeichnungen' und erzeugt "
        "die Zeichnungen neu. Vorher folgt eine Sicherheitsabfrage.");
    QPushButton* btnDry = modeBox.addButton("Vorschau", QMessageBox::AcceptRole);
    QPushButton* btnReal = modeBox.addButton("Aufbauen", QMessageBox::DestructiveRole);
    modeBox.addButton("Abbrechen", QMessageBox::RejectRole);
    modeBox.setDefaultButton(btnDry);
    modeBox.exec();

    const bool dryRun = (modeBox.clickedButton() == btnDry);
    if (!dryRun && modeBox.clickedButton() != btnReal) return;

    log(dryRun ? "=== PROJEKT AUFBAUEN (Vorschau) ===" : "=== PROJEKT AUFBAUEN ===");
    log(QString("Erstellliste: %1").arg(QDir::toNativeSeparators(csvPath)));

    ProjectBuilder builder;
    const bool prepared = builder.prepare(csvPath, m_projectRoot, dryRun);
    const QString logPath = QDir::toNativeSeparators(builder.logPath());

    if (!prepared) {
        if (builder.blockedByOpenDrawings()) {
            QStringList entries;
            for (const QString& f : builder.preview().openDocs) {
                entries << "In dieser BricsCAD-Sitzung geoeffnet:  " + QDir::toNativeSeparators(f);
            }
            for (const QString& f : builder.preview().lockFiles) {
                entries << "Sperrdatei (andere Sitzung oder Absturz-Rest):  "
                           + QDir::toNativeSeparators(f);
            }
            logError(QString("Aufbau abgebrochen: %1 offene Zeichnung(en)/Sperrdatei(en)")
                     .arg(entries.size()));
            showListWarning(this, title,
                "Aufbau abgebrochen - Zeichnungen sind offen!\n\n"
                "Es wurde NICHTS geloescht und NICHTS erzeugt.",
                entries,
                "Bitte Zeichnungen schliessen (bzw. verwaiste Sperrdateien loeschen) "
                "und den Aufbau erneut starten.\n\nLog: " + logPath);
        } else {
            reportError(title, "Aufbau abgebrochen: " + builder.lastError(),
                        logPath.isEmpty() ? QString() : "Log: " + logPath);
        }
        return;
    }

    const ProjectBuilder::Preview& pv = builder.preview();

    // -------- Vorschau: Plan ins Log, fertig --------
    if (dryRun) {
        builder.runDry();
        logSuccess(QString("Vorschau: %1 Zeichnung(en) geplant, %2 Datei(en) wuerden "
                           "geloescht, %3 Warnung(en)")
                   .arg(pv.planCount).arg(pv.deleteCount).arg(pv.warnings));
        log(QString("Log: %1").arg(logPath));
        QMessageBox::information(this, title,
            QString("Vorschau abgeschlossen - es wurde nichts geaendert.\n\n"
                    "Zu erzeugende Zeichnungen:  %1\n"
                    "Zu loeschende Dateien:  %2\n"
                    "Geschuetzt (*Deckblatt_A.dwg):  %3\n"
                    "Warnungen:  %4\n\n"
                    "Log: %5")
            .arg(pv.planCount).arg(pv.deleteCount).arg(pv.protectedCount)
            .arg(pv.warnings).arg(logPath));
        return;
    }

    // -------- Sicherheitsabfrage --------
    QString question = QString(
        "WICHTIG: Vor dem Lauf den Projektordner sichern!\n\n"
        "Es werden %1 Datei(en) unter '05- Projekt Zeichnungen' geloescht\n%2\n\n"
        "Anschliessend werden %3 Zeichnung(en) neu erzeugt.")
        .arg(pv.deleteCount)
        .arg(pv.protectDeckblatt
             ? "(ausser *Deckblatt_A.dwg auf oberster Ebene)."
             : "(INKLUSIVE *Deckblatt_A.dwg - die Liste erzeugt die Projektblaetter selbst).")
        .arg(pv.planCount);
    if (pv.warnings > 0) {
        question += QString("\n\nBeim Lesen der Liste gab es %1 Warnung(en), "
                            "Einzelheiten im Log:\n%2").arg(pv.warnings).arg(logPath);
    }
    question += "\n\nFortfahren?";

    if (QMessageBox::warning(this, title, question,
                             QMessageBox::Yes | QMessageBox::No,
                             QMessageBox::No) != QMessageBox::Yes) {
        builder.cancelByUser();
        log("Aufbau abgebrochen durch Benutzer (Sicherheitsabfrage)");
        return;
    }

    // -------- Aufbau --------
    // Waehrend des Laufs keine Eingaben annehmen: die Anzeige wird weiter
    // gezeichnet, Klicks und Tasten bleiben liegen.
    m_btnProjektAufbau->setEnabled(false);
    m_btnProjektAufbau->setText("Projekt wird aufgebaut...");
    m_progressBar->setRange(0, pv.planCount);
    m_progressBar->setValue(0);
    m_progressBar->setVisible(true);

    connect(&builder, &ProjectBuilder::progress, this,
        [this](int current, int total, const QString& fileName) {
            m_progressBar->setValue(current);
            m_progressBar->setFormat(QString("Projekt aufbauen: %1 von %2 - %3")
                                     .arg(current).arg(total).arg(fileName));
            QApplication::processEvents(QEventLoop::ExcludeUserInputEvents);
        });

    const ProjectBuilder::Result result = builder.runReal();

    hideProgress();
    m_btnProjektAufbau->setText("Projekt aufbauen");
    m_btnProjektAufbau->setEnabled(true);

    if (result.aborted) {
        reportError(title, "Aufbau abgebrochen: Stempel nicht gefunden",
                    result.abortMessage + "\n\nLog: " + logPath);
        return;
    }

    const QString summary = QString(
        "Erstellt mit Attributen:  %1\n"
        "Nur kopiert (ohne Attribute):  %2\n"
        "Fehler beim Aufbau:  %3\n"
        "Fehler/Warnungen insgesamt:  %4")
        .arg(result.created).arg(result.copiedOnly)
        .arg(result.errors).arg(result.warnings);

    if (result.errors == 0) {
        logSuccess(QString("Projekt aufgebaut: %1 Zeichnung(en) erstellt, %2 nur kopiert, "
                           "%3 Warnung(en)")
                   .arg(result.created).arg(result.copiedOnly).arg(result.warnings));
    } else {
        logError(QString("Projekt aufgebaut mit %1 Fehler(n): %2 erstellt, %3 nur kopiert")
                 .arg(result.errors).arg(result.created).arg(result.copiedOnly));
    }
    log(QString("Log: %1").arg(logPath));

    if (result.errors == 0 && result.warnings == 0) {
        QMessageBox::information(this, title,
            "Projekt aufgebaut.\n\n" + summary + "\n\nLog: " + logPath);
    } else {
        QMessageBox::warning(this, title,
            "Projekt aufgebaut - bitte das Log pruefen.\n\n" + summary
            + "\n\nLog: " + logPath);
    }
}

// ============================================================================
// Deckblatt Generation
// ============================================================================

QString OpenCirtTab::folderDisplayName(const QString& folderName) {
    // "01 ASP01" -> "ASP01", "01 Los 1 Anlagenautomation" -> "Los 1 Anlagenautomation"
    // "00 GA" -> "GA", "03 KAE" -> "KAE"
    QRegularExpression re("^\\d+[-\\s]+(.+)$");
    QRegularExpressionMatch match = re.match(folderName);
    if (match.hasMatch()) {
        return match.captured(1).trimmed();
    }
    return folderName;
}

namespace {

/// Extract the trailing version number: "..._V13.dwg" -> 13, "..._V_5.dwg" -> 5.
/// Returns -1 when the name carries no version.
int vorlagenVersion(const QString& fileName) {
    static const QRegularExpression re("_V_?(\\d+)\\.dwg$",
                                       QRegularExpression::CaseInsensitiveOption);
    QRegularExpressionMatch m = re.match(fileName);
    return m.hasMatch() ? m.captured(1).toInt() : -1;
}

/// Newest template matching pattern, skipping names that contain an exclude token.
/// Picks by version number, not alphabetically - "V13" must beat "V12".
QString newestVorlage(const QString& vorlagenDir, const QString& pattern,
                      const QStringList& excludeTokens = QStringList()) {
    const QStringList matches =
        QDir(vorlagenDir).entryList(QStringList() << pattern, QDir::Files);

    QString best;
    int bestVersion = -2;
    for (const QString& candidate : matches) {
        bool excluded = false;
        for (const QString& token : excludeTokens) {
            if (candidate.contains(token, Qt::CaseInsensitive)) {
                excluded = true;
                break;
            }
        }
        if (excluded) continue;

        const int version = vorlagenVersion(candidate);
        if (version > bestVersion) {
            bestVersion = version;
            best = candidate;
        }
    }

    if (best.isEmpty()) return QString();

    QString path = vorlagenDir + "/" + best;
    path.replace("\\", "/");
    return path;
}

} // namespace

QString OpenCirtTab::findDeckblattVorlage() {
    // Highest OC_VORLAGE_DIN_A2_V<n>.dwg. The Inhaltsverzeichnis sheet shares the
    // OC_VORLAGE_DIN_A2 prefix and must not be picked up here.
    return newestVorlage(projectPath(OpenCirtConfig::VORLAGEN_DIR),
                         "OC_VORLAGE_DIN_A2*.dwg",
                         QStringList() << "INHALTSVERZEICHNIS");
}

int OpenCirtTab::cleanupDeckblaetter() {
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    int deletedCount = 0;
    
    QDirIterator it(drawingsDir, QStringList() << "*_Deckblatt.dwg" << "*_Deckblatt.bak"
                    << "*_Deckblatt_B.dwg" << "*_Deckblatt_B.bak",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        // Never delete manually created _A (Uebersichtsseite)
        if (QFileInfo(path).fileName().contains("_Deckblatt_A")) continue;
        if (QFile::remove(path)) {
            deletedCount++;
        }
    }
    
    log(QString("Deckblatt-Cleanup: %1 Dateien geloescht").arg(deletedCount));
    return deletedCount;
}

void OpenCirtTab::createDeckblaetter(RunCounts& counts) {
    QString vorlage = findDeckblattVorlage();
    if (vorlage.isEmpty()) {
        logError("Deckblatt-Vorlage OC_VORLAGE_DIN_A2_*.dwg nicht gefunden");
        return;
    }
    
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    drawingsDir.replace("\\", "/");

    // Plankopf-Stammdaten sicherstellen: Deckblaetter entstehen frisch aus der
    // Vorlage und blieben sonst ohne AG/AN/PR, wenn die Generierung nicht aus
    // dem Gesamtlauf heraus angestossen wurde.
    ensurePlankopfCsvLoaded();
    const OcPairs plankopfPairs = pairsFromMap(m_plankopfCsvData);

    // Recursively find all subdirectories under Zeichnungen
    QDirIterator dirIt(drawingsDir, QDir::Dirs | QDir::NoDotAndDotDot,
                       QDirIterator::Subdirectories);

    QStringList folders;
    while (dirIt.hasNext()) {
        folders << dirIt.next();
    }
    folders.sort();  // Ensure alphabetical order

    // Layer, die auf einem Deckblatt nichts zu suchen haben
    const QStringList frozenLayers = {
        "GA-Trennlinie-BAS", "GA-Trennlinie-LVB",
        "GA-Trennlinie-Regeldiagramme", "GA-Trennlinie-Regelstruktur",
        "GA-Konstruktionslinie", "GA-MBE-Grafik"
    };

    beginPhase("Deckblaetter", folders.size());

    // === Folder-level Deckblaetter ===
    for (const QString& folderPath : folders) {
        QDir folder(folderPath);
        QString folderName = folder.dirName();
        QString displayName = folderDisplayName(folderName);
        
        // Target: {folderPath}/0000 {displayName}_Deckblatt.dwg
        // "0000 " sorts before "00 " and "01 " -> always first in folder
        QString targetName = QString("0000 %1_Deckblatt.dwg").arg(displayName);
        QString targetPath = folderPath + "/" + targetName;
        targetPath.replace("\\", "/");

        // ASP/GEWERK/ANLAGE aus der Ordnerhierarchie.
        // Los-Deckblatt   -> alle drei leer
        // ASP-Deckblatt   -> ASP
        // Gewerk-Deckblatt-> ASP + GEWERK
        // Anlagen-Deckblatt-> ASP + GEWERK + ANLAGE
        QString aspValue, gewerkValue, anlageValue;
        deriveHierarchieFromFolder(folderPath, aspValue, gewerkValue, anlageValue);

        if (!copyTemplate(vorlage, targetPath)) {
            logError(QString("Deckblatt nicht kopierbar: %1").arg(targetName));
            ++counts.errors;
        } else if (processNewDrawing(targetPath, counts, [&](OcDrawing& dwg) {
                       // Layer GA-Deckblatt zeigen, Trennlinien ausblenden
                       dwg.thawLayer("GA-Deckblatt");
                       dwg.freezeLayers(frozenLayers);
                       // Text "DECKBLATT" durch den Namen der Ebene ersetzen
                       dwg.replaceText("DECKBLATT", displayName);
                       dwg.setAttributes(hierarchiePairs(aspValue, gewerkValue, anlageValue));
                       if (!plankopfPairs.isEmpty()) {
                           dwg.setAttributes(plankopfPairs);
                       }
                   })) {
            ++counts.deckblaetter;
        }
        stepDone(targetName);
    }
    
    log(QString("Deckblaetter: %1 erzeugt").arg(counts.deckblaetter));
}

void OpenCirtTab::deriveHierarchieFromFolder(const QString& folderPath,
                                             QString& asp, QString& gewerk, QString& anlage) {
    // Ebenen nach ihrer Lage im Pfad; Los-Ordner oder hoeher: alle drei leer
    const QStringList levels = hierarchieLevels(folderPath);
    asp    = levels.size() > EbeneAsp    ? folderDisplayName(levels.at(EbeneAsp))    : QString();
    gewerk = levels.size() > EbeneGewerk ? folderDisplayName(levels.at(EbeneGewerk)) : QString();
    anlage = levels.size() > EbeneAnlage ? folderDisplayName(levels.at(EbeneAnlage)) : QString();
}

void OpenCirtTab::loadPlankopfCsv() {
    const QFileInfo info(referencePath(OpenCirtConfig::PLANKOPF_CSV));
    m_plankopfCsvData = readPlankopfCsv();
    m_plankopfCsvPath = info.absoluteFilePath();
    m_plankopfCsvStamp = info.exists() ? info.lastModified() : QDateTime();
    m_plankopfCsvSize = info.exists() ? info.size() : -1;
}

void OpenCirtTab::ensurePlankopfCsvLoaded() {
    // Die Daten gehoeren zum Projekt und koennen sich zwischen zwei Laeufen
    // aendern. Geladenes gilt nur, solange es dieselbe Datei im selben Stand ist.
    const QFileInfo info(referencePath(OpenCirtConfig::PLANKOPF_CSV));
    const bool current =
        info.absoluteFilePath() == m_plankopfCsvPath
        && (info.exists() ? info.lastModified() : QDateTime()) == m_plankopfCsvStamp
        && (info.exists() ? info.size() : -1) == m_plankopfCsvSize;
    if (!current) loadPlankopfCsv();
}

// ============================================================================
// Plankopf CSV: Read & Apply
// ============================================================================

QMap<QString, QString> OpenCirtTab::readPlankopfCsv() {
    QMap<QString, QString> data;
    QString csvPath = referencePath(OpenCirtConfig::PLANKOPF_CSV);
    
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        log(QString("plankopfdaten.csv nicht gefunden: %1").arg(csvPath), "WARN");
        return data;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    bool headerSkipped = false;
    int lineNum = 0;
    
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        lineNum++;
        if (line.isEmpty()) continue;
        
        // Zeile 1 = Header, ueberspringen
        if (!headerSkipped) {
            headerSkipped = true;
            continue;
        }
        
        // Semikolon-getrennt: Spalte1=Attributname, Spalte2=Wert, Spalte3=Kommentar(ignoriert)
        QStringList fields = line.split(";");
        if (fields.size() < 2) continue;
        
        QString attrName = fields[0].trimmed();
        QString attrValue = fields[1].trimmed();
        
        if (attrName.isEmpty()) continue;
        
        data[attrName] = attrValue;
    }
    
    file.close();
    log(QString("plankopfdaten.csv: %1 Attribute gelesen").arg(data.size()));
    return data;
}

// ============================================================================
// GA-FL Cleanup
// ============================================================================

bool OpenCirtTab::cleanupGaFl() {
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    int deletedCount = 0;
    
    QDirIterator it(drawingsDir, QStringList() << "*.dwg", QDir::Files,
                    QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        QString fileName = QFileInfo(path).fileName();
        
        if (fileName.contains("_GA_FL_") || 
            fileName.contains("_Summe_") ||
            fileName.startsWith("0001 Projekt_Summe", Qt::CaseInsensitive) ||
            fileName.startsWith("00 Projekt_Summe", Qt::CaseInsensitive) ||  // Legacy
            fileName == "Projekt_Summe.dwg") {  // Legacy
            if (QFile::remove(path)) {
                deletedCount++;
            } else {
                logError(QString("Konnte nicht loeschen: %1").arg(fileName));
            }
        }
    }
    
    // GA_FL_VORLAGE.csv bleibt stehen: der Gesamtlauf hat sie unmittelbar
    // vorher frisch aus der ODS erzeugt (convertOdsToCSV)

    log(QString("Cleanup: %1 Dateien geloescht").arg(deletedCount));
    return true;
}

// ============================================================================
// Phase 2: Calculate Sheet Count
// ============================================================================

int OpenCirtTab::calculateSheetCount(int dpCount) {
    if (dpCount <= 0) return 0;
    if (dpCount <= OpenCirtConfig::MAX_DP_FIRST_SHEET) return 1;
    
    // First sheet: 25 DPs, follow-up sheets: 24 DPs each (row 1 = carryover)
    int remaining = dpCount - OpenCirtConfig::MAX_DP_FIRST_SHEET;
    int followSheets = (remaining + OpenCirtConfig::MAX_DP_FOLLOW_SHEET - 1) 
                       / OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
    return 1 + followSheets;
}

// ============================================================================
// Phase 2: Parse Single Extracted CSV
// ============================================================================

SourceDrawingInfo OpenCirtTab::parseExtractedCsv(const QString& csvPath, const QString& dwgPath) {
    SourceDrawingInfo info;
    info.filePath = dwgPath;
    info.fileName = QFileInfo(dwgPath).completeBaseName();
    applyFolderHierarchie(info, dwgPath);
    
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logError(QString("CSV nicht lesbar: %1").arg(csvPath));
        return info;
    }
    
    QTextStream stream(&file);
    // ExtractDP.lsp schreibt ueber BricsCAD LISP (open) in Windows-Systemcodepage (CP1252)
    stream.setEncoding(QStringConverter::Latin1);
    
    QStringList header;
    bool headerRead = false;
    
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        
        // Parse Plankopf comment line
        if (line.startsWith("#PLANKOPF;")) {
            QStringList parts = line.mid(10).split(";");
            for (const QString& part : parts) {
                int eqPos = part.indexOf('=');
                if (eqPos > 0) {
                    QString key = part.left(eqPos);
                    QString val = part.mid(eqPos + 1);
                    info.plankopfAttributes[key] = val;
                }
            }
            // Plankopf-Stammdaten aus CSV drueberlagern (z.B. AG, AN, PR)
            if (!m_plankopfCsvData.isEmpty()) {
                for (auto it = m_plankopfCsvData.constBegin(); it != m_plankopfCsvData.constEnd(); ++it) {
                    info.plankopfAttributes[it.key()] = it.value();
                }
            }
            
            // ASP aus Ordnerhierarchie setzen (ueberschreibt evtl. vorhandenen Wert)
            if (!info.aspName.isEmpty()) {
                info.plankopfAttributes["ASP"] = folderDisplayName(info.aspName);
            } else {
                info.plankopfAttributes["ASP"] = QString();
            }
            continue;
        }
        
        // Skip other comment lines
        if (line.startsWith("#")) continue;
        
        QStringList fields = line.split(";");
        
        // First non-comment line is header
        if (!headerRead) {
            header = fields;
            headerRead = true;
            continue;
        }
        
        // Data line -> DataPoint
        DataPoint dp;
        if (fields.size() > 0) dp.bmk = fields[0];
        if (fields.size() > 1) dp.bezeichnung = fields[1];
        if (fields.size() > 2) dp.aks = fields[2];
        if (fields.size() > 3) dp.refDp = fields[3];
        if (fields.size() > 4) dp.fcodeDp = fields[4];
        if (fields.size() > 5) dp.basString = fields[5];
        if (fields.size() > 6) dp.integDp = fields[6];
        if (fields.size() > 7) dp.produkt = fields[7];
        
        // Columns 8+ are the 60 function values (OC_x_x_x)
        for (int col = 8; col < fields.size() && col < header.size(); ++col) {
            QString colName = header[col];
            QString val = fields[col];
            if (!val.trimmed().isEmpty()) {
                dp.funktionsWerte[colName] = val;
            }
        }
        
        // Assign DP index based on order
        dp.dpIndex = info.dataPoints.size() + 1;
        info.dataPoints.append(dp);
    }
    
    file.close();
    
    // Calculate sheet count
    info.gaFlSheetCount = calculateSheetCount(info.dataPoints.size());
    
    log(QString("  %1: %2 DPs -> %3 Blaetter")
        .arg(info.fileName)
        .arg(info.dataPoints.size())
        .arg(info.gaFlSheetCount));
    
    return info;
}

// ============================================================================
// Phase 2: Read All Extracted Data
// ============================================================================

QVector<SourceDrawingInfo> OpenCirtTab::readExtractedData(const QStringList& dwgFiles) {
    QVector<SourceDrawingInfo> drawings;

    // Extraktionsdaten werden nur in dem Lauf gelesen, der sie geschrieben
    // hat. Was von frueher im Tempordner liegt, kann zu einem anderen Projekt
    // oder zu einem aelteren Stand der Zeichnungen gehoeren.
    if (!extractDirUsable()) {
        logError("Extraktionsdaten stammen nicht aus diesem Lauf - nichts gelesen");
        return drawings;
    }

    log(QString("Lese extrahierte Daten aus: %1").arg(m_extractTempDir));
    ensurePlankopfCsvLoaded();
    
    int totalDp = 0;
    int totalSheets = 0;
    
    // Mirror Zeichnungen folder structure to find CSVs (matches generateExtractionScr)
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    QDir drawingsRoot(drawingsDir);
    
    for (const QString& dwg : dwgFiles) {
        QString baseName = QFileInfo(dwg).completeBaseName();
        QString relPath = drawingsRoot.relativeFilePath(QFileInfo(dwg).absolutePath());
        QString csvPath = m_extractTempDir + "/" + relPath + "/" + baseName + ".csv";
        
        if (!QFileInfo::exists(csvPath)) {
            // No CSV means no active DPs in this DWG - skip silently
            continue;
        }
        
        SourceDrawingInfo info = parseExtractedCsv(csvPath, dwg);
        
        if (!info.dataPoints.isEmpty()) {
            totalDp += info.dataPoints.size();
            totalSheets += info.gaFlSheetCount;
            drawings.append(info);
        }
    }
    
    log(QString("Extraktion gelesen: %1 Zeichnungen, %2 DPs, %3 GA-FL-Blaetter")
        .arg(drawings.size()).arg(totalDp).arg(totalSheets));
    
    return drawings;
}

// ============================================================================
// Phase 2: Read ODS Reference Data
// ============================================================================

QMap<QString, QVector<QString>> OpenCirtTab::readOdsReference() {
    QMap<QString, QVector<QString>> referenceData;
    
    QString csvPath = referencePath("GA_FL_VORLAGE.csv");
    if (!QFileInfo::exists(csvPath)) {
        logError(QString("Referenz-CSV nicht gefunden: %1").arg(csvPath));
        return referenceData;
    }
    
    QFile file(csvPath);
    if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
        logError("Referenz-CSV nicht lesbar");
        return referenceData;
    }
    
    QTextStream stream(&file);
    stream.setEncoding(QStringConverter::Utf8);
    
    // Skip header line
    if (!stream.atEnd()) stream.readLine();
    
    while (!stream.atEnd()) {
        QString line = stream.readLine().trimmed();
        if (line.isEmpty()) continue;
        
        // LibreOffice CSV is comma-separated with optional quoting
        QVector<QString> fields;
        bool inQuotes = false;
        QString current;
        
        for (int i = 0; i < line.length(); ++i) {
            QChar ch = line[i];
            if (ch == '"') {
                inQuotes = !inQuotes;
            } else if (ch == ',' && !inQuotes) {
                fields.append(current.trimmed());
                current.clear();
            } else {
                current += ch;
            }
        }
        fields.append(current.trimmed());
        
        // Column B (index 1) = DP name (key for lookup)
        if (fields.size() > 1 && !fields[1].isEmpty()) {
            referenceData[fields[1]] = fields;
        }
    }
    
    file.close();
    log(QString("Referenz-CSV geladen: %1 Eintraege").arg(referenceData.size()));
    
    return referenceData;
}

// ============================================================================
// Phase 2: GA-FL-Blaetter erzeugen und befuellen
// ============================================================================

void OpenCirtTab::createGaFlSheets(const QVector<SourceDrawingInfo>& drawings,
                                   const QMap<QString, QStringList>& extracted,
                                   const QString& refCsvPath, RunCounts& counts) {
    const QString vorlage = templatePath(OpenCirtConfig::GA_FL_VORLAGE_DWG);

    bool referenceFound = false;
    const QVector<QStringList> reference = OcEngine::readReferenceCsv(refCsvPath, &referenceFound);

    OcLogFile fillLog;
    fillLog.open(m_extractTempDir + "/fillgafl_log.txt", true);
    OcFillState fillState;

    int totalSheets = 0;
    for (const SourceDrawingInfo& drawing : drawings) {
        totalSheets += drawing.gaFlSheetCount;
    }
    beginPhase("GA-FL", totalSheets);

    const QStringList frozenLayers = { "GA-Konstruktionslinie", "GA-MBE-Grafik" };

    for (const SourceDrawingInfo& drawing : drawings) {
        if (drawing.dataPoints.isEmpty()) continue;

        const QStringList lines = extracted.value(drawing.filePath);
        OcFillJob job;
        job.rows = OcEngine::rowsFromLines(lines);
        job.plankopf = OcEngine::plankopfFromLines(lines);
        job.useReference = referenceFound;

        int dpCount = drawing.dataPoints.size();
        int sheetCount = drawing.gaFlSheetCount;

        // Target folder for GA-FL sheets = same as source DWG folder
        QString targetFolder = QFileInfo(drawing.filePath).absolutePath();
        targetFolder.replace("\\", "/");

        int dpOffset = 0;  // Running offset into the datapoint list

        for (int sheet = 1; sheet <= sheetCount; ++sheet) {
            bool isFirstSheet = (sheet == 1);
            int maxDpThisSheet = isFirstSheet
                ? OpenCirtConfig::MAX_DP_FIRST_SHEET
                : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
            int dpThisSheet = qMin(maxDpThisSheet, dpCount - dpOffset);

            // GA-FL filename: <SourceName>_GA_FL_<SheetNum>.dwg
            QString gaFlName = QString("%1_GA_FL_%2.dwg")
                               .arg(drawing.fileName)
                               .arg(sheet, 2, 10, QChar('0'));
            QString gaFlPath = targetFolder + "/" + gaFlName;

            job.sheetNum = sheet;
            job.startRow = dpOffset;
            job.dpCount = dpThisSheet;

            if (!copyTemplate(vorlage, gaFlPath)) {
                logError(QString("GA-FL-Vorlage nicht kopierbar: %1").arg(gaFlName));
                ++counts.errors;
            } else if (processNewDrawing(gaFlPath, counts, [&](OcDrawing& dwg) {
                           dwg.freezeLayers(frozenLayers);
                           const OcFillResult filled =
                               dwg.fillGaFl(job, reference, fillState, &fillLog);
                           if (!filled.ok) {
                               logError(QString("%1: %2").arg(gaFlName, filled.error));
                           }
                       })) {
                ++counts.gaFl;
            }

            dpOffset += dpThisSheet;
            stepDone(gaFlName);
        }
    }
    fillLog.close();

    log(QString("GA-FL: %1 Blaetter fuer %2 Zeichnungen erzeugt")
        .arg(counts.gaFl).arg(drawings.size()));
    if (!fillState.missingRefs.isEmpty()) {
        logError(QString("%1 fehlende DP-Referenz(en) wurden mit 0 belegt")
                 .arg(fillState.missingRefs.size()));
    }
}

// ============================================================================
// Phase 3: Summenblaetter planen
// ============================================================================

void OpenCirtTab::planSummarySheets(const QVector<SourceDrawingInfo>& drawings) {
    // Schreibt je Summenebene eine CSV in den Tempordner und merkt sich, welche
    // Blaetter daraus entstehen (m_summaryJobs). Erzeugt und befuellt werden
    // sie in createSummen().
    m_summaryJobs.clear();
    m_plannedSummarySheets.clear();

    auto addJob = [this](const QString& target, const QString& csvPath,
                         int sheetNum, int startRow, int dpCount) {
        SummaryJob job;
        job.target = target;
        job.csvPath = csvPath;
        job.sheetNum = sheetNum;
        job.startRow = startRow;
        job.dpCount = dpCount;
        m_summaryJobs.append(job);
        m_plannedSummarySheets << target;
    };

    if (!extractDirUsable()) {
        logError("Summenblaetter: der Ordner der Extraktionsdaten gehoert nicht zu diesem Lauf");
        return;
    }
    QString tempDir = m_extractTempDir;
    tempDir.replace("\\", "/");
    
    // ================================================================
    // Step 1: Build per-ASP aggregation
    // ================================================================
    // Map: ASP-Name -> list of drawings in that ASP
    QMap<QString, QVector<const SourceDrawingInfo*>> aspMap;
    for (const SourceDrawingInfo& d : drawings) {
        QString aspKey = d.aspName.isEmpty() ? "_default" : d.aspName;
        aspMap[aspKey].append(&d);
    }
    
    // ================================================================
    // Step 2: Create merged CSV per ASP (combining all DPs)
    // ================================================================
    QStringList aspSumCsvPaths;
    int aspSumSheetTotal = 0;
    
    // Funktionsspalten (gemeinsam fuer alle Summenebenen)
    const QStringList& funcBases = kFuncBases;
    int numFuncs = funcBases.size(); // 57
    
    for (auto it = aspMap.constBegin(); it != aspMap.constEnd(); ++it) {
        QString aspName = it.key();
        const QVector<const SourceDrawingInfo*>& aspDrawings = it.value();
        
        // Group drawings by Gewerk within this ASP
        QMap<QString, QVector<const SourceDrawingInfo*>> gewerkMap;
        for (const SourceDrawingInfo* d : aspDrawings) {
            QString gw = d->gewerk;
            if (gw.isEmpty()) gw = "Unbekannt";
            gewerkMap[gw].append(d);
        }
        
        int gewerkCount = gewerkMap.size();
        if (gewerkCount == 0) continue;
        
        // ================================================================
        // Step 1.5: Gewerk-Summe (one row per Anlage within each Gewerk)
        // ================================================================
        {
            // ASP-Ordner (Lage im Pfad) fuer die Suche nach dem Gewerk-Ordner
            QString aspFolderPath;
            if (!aspDrawings.isEmpty()) {
                aspFolderPath = aspFolderPathOf(aspDrawings.first()->aspFolder);
            }

            for (auto gwIt = gewerkMap.constBegin(); gwIt != gewerkMap.constEnd(); ++gwIt) {
                QString gewerkName = gwIt.key();
                const QVector<const SourceDrawingInfo*>& gwDrawings = gwIt.value();
                
                // Group drawings by Anlage within this Gewerk
                QMap<QString, QVector<const SourceDrawingInfo*>> anlageMap;
                for (const SourceDrawingInfo* d : gwDrawings) {
                    QString anl = d->anlage;
                    if (anl.isEmpty()) anl = gewerkName;  // DWG directly in Gewerk folder
                    anlageMap[anl].append(d);
                }
                
                int anlageCount = anlageMap.size();
                if (anlageCount < 2) continue;  // Skip Gewerk-Summe for single-Anlage Gewerke
                
                // Find Gewerk folder path from ASP folder
                QString gewerkFolderPath;
                if (!aspFolderPath.isEmpty()) {
                    QDir aspDir(aspFolderPath);
                    QStringList subDirs = aspDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
                    for (const QString& sub : subDirs) {
                        if (folderDisplayName(sub) == gewerkName) {
                            gewerkFolderPath = aspFolderPath + "/" + sub;
                            break;
                        }
                    }
                }
                if (gewerkFolderPath.isEmpty()) continue;
                gewerkFolderPath.replace("\\", "/");
                
                // Create Gewerk-Summe CSV: one row per Anlage
                QString gwCsvPath = tempDir + "/" + aspName + "_" + gewerkName + "_Summe.csv";
                QFile gwCsvFile(gwCsvPath);
                if (!gwCsvFile.open(QIODevice::WriteOnly | QIODevice::Text)) continue;
                
                QTextStream gwStream(&gwCsvFile);
                gwStream.setEncoding(QStringConverter::Utf8);
                
                // Header
                gwStream << "BMK;BEZEICHNUNG;AKS;REF_DP;FCODE_DP;BAS_DP;INTEG_DP";
                for (const QString& fb : funcBases) {
                    gwStream << ";" << fb;
                }
                gwStream << "\n";
                
                // Plankopf
                if (!gwDrawings.isEmpty()) {
                    QString aspDisplayForPk = folderDisplayName(aspName);
                    gwStream << "#PLANKOPF";
                    const QMap<QString, QString>& pk = gwDrawings.first()->plankopfAttributes;
                    for (auto pIt = pk.constBegin(); pIt != pk.constEnd(); ++pIt) {
                        if (pIt.key() == "ANLAGE") {
                            gwStream << ";" << pIt.key() << "=";  // empty
                        } else if (pIt.key() == "ASP") {
                            gwStream << ";ASP=" << aspDisplayForPk;
                        } else if (pIt.key() == "GEWERK") {
                            gwStream << ";GEWERK=" << gewerkName;
                        } else if (pIt.key() == "ZEICHNUNGSNUMMER") {
                            gwStream << ";ZEICHNUNGSNUMMER=" << gewerkName << " Summe";
                        } else {
                            gwStream << ";" << pIt.key() << "=" << pIt.value();
                        }
                    }
                    gwStream << "\n";
                }
                
                // One row per Anlage
                for (auto anIt = anlageMap.constBegin(); anIt != anlageMap.constEnd(); ++anIt) {
                    QString anlageName = anIt.key();
                    const QVector<const SourceDrawingInfo*>& anDrawings = anIt.value();
                    
                    QVector<int> funcCounts(numFuncs, 0);
                    for (const SourceDrawingInfo* d : anDrawings) {
                        for (const DataPoint& dp : d->dataPoints) {
                            addFuncCounts(funcCounts, dp, funcBases);
                        }
                    }
                    
                    gwStream << anlageName << ";"
                             << ";"  // BEZEICHNUNG leer (vermeidet Dopplung)
                             << anlageName << ";"
                             << ";" << ";" << ";";
                    for (int fc = 0; fc < numFuncs; ++fc) {
                        if (fc == 0) {
                            gwStream << ";";  // OC_INTEG: empty
                        } else {
                            gwStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                        }
                    }
                    gwStream << "\n";
                }
                gwCsvFile.close();
                log(QString("Gewerk-Summe CSV %1/%2: %3 Anlagen").arg(aspName, gewerkName).arg(anlageCount));
                
                // Generate Gewerk-Summe DWG sheets
                QString gwCsvSlash = gwCsvPath;
                gwCsvSlash.replace("\\", "/");
                int gwSheetCount = calculateSheetCount(anlageCount);
                int gwDpOffset = 0;
                
                for (int sheet = 1; sheet <= gwSheetCount; ++sheet) {
                    bool isFirst = (sheet == 1);
                    int maxDp = isFirst ? OpenCirtConfig::MAX_DP_FIRST_SHEET : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
                    int dpThis = qMin(maxDp, anlageCount - gwDpOffset);
                    
                    QString sumName = QString("0001 %1_Summe_%2.dwg")
                                      .arg(gewerkName)
                                      .arg(sheet, 2, 10, QChar('0'));
                    QString sumPath = gewerkFolderPath + "/" + sumName;
                    addJob(sumPath, gwCsvSlash, sheet, gwDpOffset, dpThis);
                    
                    gwDpOffset += dpThis;
                }
            }
        }
        
        // Create ASP summary CSV: one row per Gewerk with aggregated counts
        QString aspCsvPath = tempDir + "/" + aspName + "_Summe.csv";
        QFile aspCsvFile(aspCsvPath);
        if (!aspCsvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            logError(QString("ASP-Summen-CSV nicht schreibbar: %1").arg(aspCsvPath));
            continue;
        }
        
        QTextStream aspStream(&aspCsvFile);
        aspStream.setEncoding(QStringConverter::Utf8);
        
        // Header
        aspStream << "BMK;BEZEICHNUNG;AKS;REF_DP;FCODE_DP;BAS_DP;INTEG_DP";
        for (const QString& fb : funcBases) {
            aspStream << ";" << fb;
        }
        aspStream << "\n";
        
        // Plankopf: GEWERK and ANLAGE empty, ASP = display name, ZEICHNUNGSNUMMER = "ASPxx Summe"
        if (!aspDrawings.isEmpty()) {
            QString aspDisplayForPk = folderDisplayName(aspName);
            aspStream << "#PLANKOPF";
            const QMap<QString, QString>& pk = aspDrawings.first()->plankopfAttributes;
            for (auto pIt = pk.constBegin(); pIt != pk.constEnd(); ++pIt) {
                if (pIt.key() == "GEWERK" || pIt.key() == "ANLAGE") {
                    aspStream << ";" << pIt.key() << "=";  // empty
                } else if (pIt.key() == "ASP") {
                    aspStream << ";ASP=" << aspDisplayForPk;
                } else if (pIt.key() == "ZEICHNUNGSNUMMER") {
                    aspStream << ";ZEICHNUNGSNUMMER=" << aspDisplayForPk << " Summe";
                } else {
                    aspStream << ";" << pIt.key() << "=" << pIt.value();
                }
            }
            aspStream << "\n";
        }
        
        // One row per Gewerk with aggregated function counts
        for (auto gwIt = gewerkMap.constBegin(); gwIt != gewerkMap.constEnd(); ++gwIt) {
            QString gewerkName = gwIt.key();
            const QVector<const SourceDrawingInfo*>& gwDrawings = gwIt.value();
            
            QVector<int> funcCounts(numFuncs, 0);
            
            for (const SourceDrawingInfo* d : gwDrawings) {
                for (const DataPoint& dp : d->dataPoints) {
                    addFuncCounts(funcCounts, dp, funcBases);
                }
            }
            
            // BMK=Gewerk, BEZ=Gewerk, AKS=Gewerk
            // REF_DP/FCODE/BAS/INTEG empty
            aspStream << gewerkName << ";"
                      << ";"  // BEZEICHNUNG leer (vermeidet Dopplung)
                      << gewerkName << ";"
                      << ";" << ";" << ";";
            
            // OC_INTEG (fc==0) empty, rest = counts
            for (int fc = 0; fc < numFuncs; ++fc) {
                if (fc == 0) {
                    aspStream << ";";  // OC_INTEG: leer
                } else {
                    aspStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                }
            }
            aspStream << "\n";
        }
        
        aspCsvFile.close();
        log(QString("ASP-Summe CSV %1: %2 Gewerke").arg(aspName).arg(gewerkCount));
        
        // Ordner der ASP-Summe: der ASP-Ordner (Lage im Pfad), sonst der Zeichnungsordner
        QString aspFolder;
        if (!aspDrawings.isEmpty()) {
            aspFolder = aspFolderPathOf(aspDrawings.first()->aspFolder);
        }
        if (aspFolder.isEmpty()) {
            aspFolder = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
        }
        aspFolder.replace("\\", "/");

        // Generate SCR for ASP summary sheets (based on Gewerk count)
        int aspSheetCount = calculateSheetCount(gewerkCount);
        int dpOffset = 0;
        QString aspCsvPathSlash = aspCsvPath;
        aspCsvPathSlash.replace("\\", "/");
        
        for (int sheet = 1; sheet <= aspSheetCount; ++sheet) {
            bool isFirstSheet = (sheet == 1);
            int maxDp = isFirstSheet
                ? OpenCirtConfig::MAX_DP_FIRST_SHEET
                : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
            int dpThisSheet = qMin(maxDp, gewerkCount - dpOffset);
            
            QString aspDisplayName = folderDisplayName(aspName);
            QString sumName = QString("0001 %1_Summe_%2.dwg")
                              .arg(aspDisplayName)
                              .arg(sheet, 2, 10, QChar('0'));
            QString sumPath = aspFolder + "/" + sumName;
            addJob(sumPath, aspCsvPathSlash, sheet, dpOffset, dpThisSheet);
            
            dpOffset += dpThisSheet;
            aspSumSheetTotal++;
        }
        
        aspSumCsvPaths << aspCsvPath;
    }
    
    // ================================================================
    // Step 2.5: Los-Summe (one row per ASP within each Los)
    // ================================================================
    // Build Los map: Los folder path -> list of ASP names in that Los
    QMap<QString, QStringList> losAspMap;
    QMap<QString, QString> losFolderNames;  // Los folder path -> display name
    
    QString drawingsRootLos = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    
    for (auto it = aspMap.constBegin(); it != aspMap.constEnd(); ++it) {
        QString aspName = it.key();
        const QVector<const SourceDrawingInfo*>& aspDrawings = it.value();
        if (aspDrawings.isEmpty()) continue;
        
        // Los-Ordner = Ebene ueber dem ASP-Ordner (Lage im Pfad); Zeichnungen
        // oberhalb der ASP-Ebene gehoeren zu keinem Los
        const QStringList levels = hierarchieLevels(aspDrawings.first()->aspFolder);
        if (levels.size() <= EbeneAsp) continue;
        const QString losFolder =
            QDir(drawingsRootLos + "/" + levels.at(EbeneLos)).absolutePath();
        losAspMap[losFolder].append(aspName);
        if (!losFolderNames.contains(losFolder)) {
            losFolderNames[losFolder] = folderDisplayName(levels.at(EbeneLos));
        }
    }

    int losSumSheetTotal = 0;
    
    for (auto losIt = losAspMap.constBegin(); losIt != losAspMap.constEnd(); ++losIt) {
        QString losFolder = losIt.key();
        const QStringList& losAspNames = losIt.value();
        QString losDisplayName = losFolderNames[losFolder];
        
        // Count total ASPs in this Los (one row per ASP, like Projekt-Summe)
        int losAspCount = losAspNames.size();
        if (losAspCount == 0) continue;
        
        // Create Los summary CSV: one row per ASP with aggregated counts
        QString losCsvPath = tempDir + "/" + losDisplayName + "_Summe.csv";
        QFile losCsvFile(losCsvPath);
        if (!losCsvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            logError(QString("Los-Summen-CSV nicht schreibbar: %1").arg(losCsvPath));
            continue;
        }
        
        QTextStream losStream(&losCsvFile);
        losStream.setEncoding(QStringConverter::Utf8);
        
        // Header
        losStream << "BMK;BEZEICHNUNG;AKS;REF_DP;FCODE_DP;BAS_DP;INTEG_DP";
        for (const QString& fb : funcBases) {
            losStream << ";" << fb;
        }
        losStream << "\n";
        
        // Plankopf from first ASP's first drawing, GEWERK/ANLAGE empty, ZEICHNUNGSNUMMER override
        bool plankopfDone = false;
        for (const QString& aspName : losAspNames) {
            if (plankopfDone) break;
            const QVector<const SourceDrawingInfo*>& aspDrawings = aspMap[aspName];
            if (!aspDrawings.isEmpty()) {
                losStream << "#PLANKOPF";
                const auto& pk = aspDrawings.first()->plankopfAttributes;
                for (auto pIt = pk.constBegin(); pIt != pk.constEnd(); ++pIt) {
                    if (pIt.key() == "GEWERK" || pIt.key() == "ANLAGE" || pIt.key() == "ASP") {
                        losStream << ";" << pIt.key() << "=";  // empty
                    } else if (pIt.key() == "ZEICHNUNGSNUMMER") {
                        losStream << ";ZEICHNUNGSNUMMER=" << losDisplayName << " Summe";
                    } else {
                        losStream << ";" << pIt.key() << "=" << pIt.value();
                    }
                }
                losStream << "\n";
                plankopfDone = true;
            }
        }
        
        // One row per ASP with aggregated function counts
        for (const QString& aspName : losAspNames) {
            const QVector<const SourceDrawingInfo*>& aspDrawings = aspMap[aspName];
            
            QVector<int> funcCounts(numFuncs, 0);
            for (const SourceDrawingInfo* d : aspDrawings) {
                for (const DataPoint& dp : d->dataPoints) {
                    addFuncCounts(funcCounts, dp, funcBases);
                }
            }
            
            QString aspDisplayName = folderDisplayName(aspName);
            losStream << aspDisplayName << ";"
                      << ";"  // BEZEICHNUNG leer (vermeidet Dopplung)
                      << aspDisplayName << ";"
                      << ";" << ";" << ";";
            
            for (int fc = 0; fc < numFuncs; ++fc) {
                if (fc == 0) {
                    losStream << ";";  // OC_INTEG: empty
                } else {
                    losStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                }
            }
            losStream << "\n";
        }
        losCsvFile.close();
        log(QString("Los-Summe CSV %1: %2 ASPs").arg(losDisplayName).arg(losAspCount));
        
        // Generate Los-Summe sheets
        QString losFolderSlash = losFolder;
        losFolderSlash.replace("\\", "/");
        QString losCsvSlash = losCsvPath;
        losCsvSlash.replace("\\", "/");
        
        int losSheetCount = calculateSheetCount(losAspCount);
        int dpOffset = 0;
        
        for (int sheet = 1; sheet <= losSheetCount; ++sheet) {
            bool isFirstSheet = (sheet == 1);
            int maxDp = isFirstSheet
                ? OpenCirtConfig::MAX_DP_FIRST_SHEET
                : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
            int dpThisSheet = qMin(maxDp, losAspCount - dpOffset);
            
            QString sumName = QString("0001 %1_Summe_%2.dwg")
                              .arg(losDisplayName)
                              .arg(sheet, 2, 10, QChar('0'));
            QString sumPath = losFolderSlash + "/" + sumName;
            addJob(sumPath, losCsvSlash, sheet, dpOffset, dpThisSheet);
            
            dpOffset += dpThisSheet;
            losSumSheetTotal++;
        }
    }
    
    // ================================================================
    // Step 3: Projekt_Summe (one row per Los with aggregated counts)
    // ================================================================
    // Hierarchy: ASP-Summe=per Gewerk, Los-Summe=per ASP, Projekt-Summe=per Los
    // If no Los structure exists, fall back to one row per ASP.
    
    int losCount = losAspMap.size();
    bool hasLosStructure = (losCount > 0);
    
    // Determine row count for Projekt-Summe
    int projektRowCount = 0;
    
    if (hasLosStructure) {
        projektRowCount = losCount;
    } else {
        // No Los folders found -> fall back to one row per ASP
        projektRowCount = aspMap.size();
    }
    
    if (projektRowCount > 0) {
        QString projektCsvPath = tempDir + "/Projekt_Summe.csv";
        QFile projektFile(projektCsvPath);
        if (projektFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
            QTextStream pStream(&projektFile);
            pStream.setEncoding(QStringConverter::Utf8);
            
            // Header
            pStream << "BMK;BEZEICHNUNG;AKS;REF_DP;FCODE_DP;BAS_DP;INTEG_DP";
            for (const QString& fb : funcBases) {
                pStream << ";" << fb;
            }
            pStream << "\n";
            
            // Plankopf from first drawing, GEWERK/ANLAGE empty, ZEICHNUNGSNUMMER = "Projekt Summe"
            if (!drawings.isEmpty()) {
                pStream << "#PLANKOPF";
                const auto& pk = drawings.first().plankopfAttributes;
                for (auto pkIt = pk.constBegin(); pkIt != pk.constEnd(); ++pkIt) {
                    if (pkIt.key() == "GEWERK" || pkIt.key() == "ANLAGE" || pkIt.key() == "ASP") {
                        pStream << ";" << pkIt.key() << "=";  // empty
                    } else if (pkIt.key() == "ZEICHNUNGSNUMMER") {
                        pStream << ";ZEICHNUNGSNUMMER=Projekt Summe";
                    } else {
                        pStream << ";" << pkIt.key() << "=" << pkIt.value();
                    }
                }
                pStream << "\n";
            }
            
            if (hasLosStructure) {
                // One row per Los with aggregated function counts
                for (auto losIt = losAspMap.constBegin(); losIt != losAspMap.constEnd(); ++losIt) {
                    QString losFolder = losIt.key();
                    const QStringList& losAspNames = losIt.value();
                    QString losDisplayName = losFolderNames[losFolder];
                    
                    QVector<int> funcCounts(numFuncs, 0);
                    
                    // Aggregate all DPs from all ASPs in this Los
                    for (const QString& aspName : losAspNames) {
                        const QVector<const SourceDrawingInfo*>& aspDrawings = aspMap[aspName];
                        for (const SourceDrawingInfo* d : aspDrawings) {
                            for (const DataPoint& dp : d->dataPoints) {
                                addFuncCounts(funcCounts, dp, funcBases);
                            }
                        }
                    }
                    
                    pStream << losDisplayName << ";"
                            << ";"  // BEZEICHNUNG leer (vermeidet Dopplung)
                            << losDisplayName << ";"
                            << ";" << ";" << ";";
                    
                    for (int fc = 0; fc < numFuncs; ++fc) {
                        if (fc == 0) {
                            pStream << ";";  // OC_INTEG: empty
                        } else {
                            pStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                        }
                    }
                    pStream << "\n";
                }
                log(QString("Projekt-Summe CSV: %1 Lose").arg(losCount));
            } else {
                // Fallback: No Los structure -> one row per ASP
                for (auto it = aspMap.constBegin(); it != aspMap.constEnd(); ++it) {
                    QString aspName = it.key();
                    const QVector<const SourceDrawingInfo*>& aspDrawings = it.value();
                    
                    QVector<int> funcCounts(numFuncs, 0);
                    for (const SourceDrawingInfo* d : aspDrawings) {
                        for (const DataPoint& dp : d->dataPoints) {
                            addFuncCounts(funcCounts, dp, funcBases);
                        }
                    }
                    
                    QString aspDisplayName = folderDisplayName(aspName);
                    pStream << aspDisplayName << ";"
                            << ";"  // BEZEICHNUNG leer (vermeidet Dopplung)
                            << aspDisplayName << ";"
                            << ";" << ";" << ";";
                    
                    for (int fc = 0; fc < numFuncs; ++fc) {
                        if (fc == 0) {
                            pStream << ";";  // OC_INTEG: empty
                        } else {
                            pStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                        }
                    }
                    pStream << "\n";
                }
                log(QString("Projekt-Summe CSV: %1 ASPs (kein Los)").arg(aspMap.size()));
            }
            projektFile.close();
        }
        
        // Generate Projekt_Summe sheets
        QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
        drawingsDir.replace("\\", "/");
        
        QString projektCsvSlash = projektCsvPath;
        projektCsvSlash.replace("\\", "/");
        
        int projektSheets = calculateSheetCount(projektRowCount);
        int dpOffset = 0;
        
        for (int sheet = 1; sheet <= projektSheets; ++sheet) {
            bool isFirstSheet = (sheet == 1);
            int maxDp = isFirstSheet
                ? OpenCirtConfig::MAX_DP_FIRST_SHEET
                : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
            int dpThisSheet = qMin(maxDp, projektRowCount - dpOffset);
            
            QString sumName = QString("0001 Projekt_Summe_%1.dwg")
                              .arg(sheet, 2, 10, QChar('0'));
            QString sumPath = drawingsDir + "/" + sumName;
            addJob(sumPath, projektCsvSlash, sheet, dpOffset, dpThisSheet);
            
            dpOffset += dpThisSheet;
        }
        
        log(QString("Summenblaetter geplant: %1 ASP + %2 Los + %3 Projekt-Summen")
            .arg(aspSumSheetTotal).arg(losSumSheetTotal).arg(projektSheets));
    }

    // ================================================================
    // Step 4: Gewerke-Summe je Los (one row per Gewerk, over all ASPs)
    // ================================================================
    // Liegt bewusst im Wurzelordner direkt hinter der Projekt-Summe:
    // collectOrderedDwgsForPublish() sortiert je Ordnerebene
    // Deckblatt -> Summen (alphabetisch) -> Inhalte, "0002 ..." folgt also
    // unmittelbar auf "0001 Projekt_Summe_NN.dwg".
    {
        QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
        drawingsDir.replace("\\", "/");

        int losGewerkSheetTotal = 0;

        for (auto losIt = losAspMap.constBegin(); losIt != losAspMap.constEnd(); ++losIt) {
            const QStringList& losAspNames = losIt.value();
            QString losDisplayName = losFolderNames[losIt.key()];

            // Gewerke ueber ALLE ASPs dieses Loses einsammeln
            QMap<QString, QVector<const SourceDrawingInfo*>> losGewerkMap;
            for (const QString& aspName : losAspNames) {
                for (const SourceDrawingInfo* d : aspMap[aspName]) {
                    QString gw = d->gewerk;
                    if (gw.isEmpty()) gw = "Unbekannt";
                    losGewerkMap[gw].append(d);
                }
            }

            int gewerkRowCount = losGewerkMap.size();
            if (gewerkRowCount == 0) continue;

            QString lgCsvPath = tempDir + "/" + losDisplayName + "_Summe_Gewerke.csv";
            QFile lgCsvFile(lgCsvPath);
            if (!lgCsvFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
                logError(QString("Los-Gewerke-CSV nicht schreibbar: %1").arg(lgCsvPath));
                continue;
            }

            QTextStream lgStream(&lgCsvFile);
            lgStream.setEncoding(QStringConverter::Utf8);

            lgStream << "BMK;BEZEICHNUNG;AKS;REF_DP;FCODE_DP;BAS_DP;INTEG_DP";
            for (const QString& fb : funcBases) {
                lgStream << ";" << fb;
            }
            lgStream << "\n";

            // Plankopf vom ersten Datensatz, ASP/GEWERK/ANLAGE leer
            {
                const SourceDrawingInfo* first = nullptr;
                for (auto gwIt = losGewerkMap.constBegin();
                     gwIt != losGewerkMap.constEnd() && !first; ++gwIt) {
                    if (!gwIt.value().isEmpty()) first = gwIt.value().first();
                }
                if (first) {
                    lgStream << "#PLANKOPF";
                    const auto& pk = first->plankopfAttributes;
                    for (auto pIt = pk.constBegin(); pIt != pk.constEnd(); ++pIt) {
                        if (pIt.key() == "GEWERK" || pIt.key() == "ANLAGE" || pIt.key() == "ASP") {
                            lgStream << ";" << pIt.key() << "=";
                        } else if (pIt.key() == "ZEICHNUNGSNUMMER") {
                            lgStream << ";ZEICHNUNGSNUMMER=" << losDisplayName << " Summe Gewerke";
                        } else {
                            lgStream << ";" << pIt.key() << "=" << pIt.value();
                        }
                    }
                    lgStream << "\n";
                }
            }

            // Eine Zeile je Gewerk, Funktionszaehler ueber alle ASPs summiert
            for (auto gwIt = losGewerkMap.constBegin(); gwIt != losGewerkMap.constEnd(); ++gwIt) {
                const QString& gewerkName = gwIt.key();

                QVector<int> funcCounts(numFuncs, 0);
                for (const SourceDrawingInfo* d : gwIt.value()) {
                    for (const DataPoint& dp : d->dataPoints) {
                        addFuncCounts(funcCounts, dp, funcBases);
                    }
                }

                lgStream << gewerkName << ";"
                         << ";"              // BEZEICHNUNG leer (vermeidet Dopplung)
                         << gewerkName << ";"
                         << ";" << ";" << ";";

                for (int fc = 0; fc < numFuncs; ++fc) {
                    if (fc == 0) {
                        lgStream << ";";     // OC_INTEG: leer
                    } else {
                        lgStream << ";" << (funcCounts[fc] > 0 ? QString::number(funcCounts[fc]) : "");
                    }
                }
                lgStream << "\n";
            }
            lgCsvFile.close();
            log(QString("Los-Gewerke-Summe CSV %1: %2 Gewerke ueber %3 ASPs")
                .arg(losDisplayName).arg(gewerkRowCount).arg(losAspNames.size()));

            QString lgCsvSlash = lgCsvPath;
            lgCsvSlash.replace("\\", "/");

            int lgSheets = calculateSheetCount(gewerkRowCount);
            int lgOffset = 0;

            for (int sheet = 1; sheet <= lgSheets; ++sheet) {
                bool isFirstSheet = (sheet == 1);
                int maxDp = isFirstSheet
                    ? OpenCirtConfig::MAX_DP_FIRST_SHEET
                    : OpenCirtConfig::MAX_DP_FOLLOW_SHEET;
                int dpThisSheet = qMin(maxDp, gewerkRowCount - lgOffset);

                QString sumName = QString("0002 Projekt_Summe_Gewerke_%1_%2.dwg")
                                  .arg(losDisplayName)
                                  .arg(sheet, 2, 10, QChar('0'));
                QString sumPath = drawingsDir + "/" + sumName;
                addJob(sumPath, lgCsvSlash, sheet, lgOffset, dpThisSheet);

                lgOffset += dpThisSheet;
                losGewerkSheetTotal++;
            }
        }

        if (losGewerkSheetTotal > 0) {
            log(QString("Gewerke-Summen je Los: %1 Blaetter").arg(losGewerkSheetTotal));
        }
    }
}

// ============================================================================
// Inhaltsverzeichnis Generation
// ============================================================================

// Forward declaration (defined below near DSD generation)
static QString readZeichnungsnummer(const QString& dwgPath);

void OpenCirtTab::applyFolderHierarchie(SourceDrawingInfo& info, const QString& dwgPath) {
    // Struktur: Los / ASP / Gewerk / Anlage / *.dwg - die Ebenen nach ihrer Lage
    const QString dwgParent = QFileInfo(dwgPath).absolutePath();
    const QStringList levels = hierarchieLevels(dwgParent);

    info.aspName   = levels.size() > EbeneAsp    ? levels.at(EbeneAsp) : QString();
    info.aspFolder = dwgParent;
    info.gewerk    = levels.size() > EbeneGewerk ? folderDisplayName(levels.at(EbeneGewerk)) : QString();
    info.anlage    = levels.size() > EbeneAnlage ? folderDisplayName(levels.at(EbeneAnlage)) : QString();

    if (info.aspName.isEmpty()) {
        // Oberhalb der ASP-Ebene: Elternordner als Gewerk (wie bisher)
        info.gewerk = folderDisplayName(QDir(dwgParent).dirName());
    }
}

// ============================================================================
// Phase 3: Summen aus den fertigen GA-FL-Blaettern
// ============================================================================
//
// Die Summenblaetter aggregieren die GA-FL-Blaetter, sie rechnen nicht aus der
// Referenz neu. Wer ein GA-FL-Blatt von Hand korrigiert, bekommt die Korrektur
// in den Summen wieder - und die Summen brauchen die GA_FL_VORLAGE nicht.
//
// Ablauf (createSummen): die fertigen GA-FL-Blaetter lesen, je Summenebene
// eine CSV schreiben, daraus die Summenblaetter erzeugen und befuellen, zum
// Schluss die Textbreiten aller GA-FL- und Summenblaetter anpassen.

QVector<SourceDrawingInfo> OpenCirtTab::readGaFlSheetsForSummary(QStringList& sheetPaths) {
    QVector<SourceDrawingInfo> result;
    sheetPaths.clear();

    QStringList gaFlFiles;
    {
        QDirIterator it(projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR),
                        QStringList() << "*_GA_FL_*.dwg", QDir::Files,
                        QDirIterator::Subdirectories);
        while (it.hasNext()) gaFlFiles << it.next();
        gaFlFiles.sort(Qt::CaseInsensitive);
    }
    if (gaFlFiles.isEmpty()) {
        logError("Keine GA-FL-Blaetter im Zeichnungsordner gefunden");
        return result;
    }

    ensurePlankopfCsvLoaded();
    const QString blockName = QString::fromLatin1(OpenCirtConfig::GA_FL_BLOCK_NAME);
    const QString uebertragAscii = QStringLiteral("Uebertrag");
    const QString uebertragUmlaut = QString::fromUtf8("\xC3\x9C" "bertrag");

    int unlesbar = 0;
    int dpTotal = 0;

    for (const QString& sheetPath : gaFlFiles) {
        SheetAttributes sa = readSheetAttributes(sheetPath, blockName);
        if (!sa.ok || sa.gaFl.isEmpty()) {
            ++unlesbar;
            logError(QString("  Kein GA-FL-Block lesbar: %1")
                     .arg(QFileInfo(sheetPath).fileName()));
            continue;
        }

        SourceDrawingInfo info;
        info.filePath = sheetPath;
        info.fileName = QFileInfo(sheetPath).completeBaseName();
        applyFolderHierarchie(info, sheetPath);

        // Plankopf wie aus der Extraktion: nur die bekannten Felder, dann die
        // Stammdaten aus plankopfdaten.csv drueber, ASP aus der Ordnerhierarchie.
        for (const char* key : kPlankopfKeys) {
            const QString k = QString::fromLatin1(key);
            if (sa.plankopf.contains(k)) {
                info.plankopfAttributes[k] = sa.plankopf.value(k);
            }
        }
        for (auto it = m_plankopfCsvData.constBegin(); it != m_plankopfCsvData.constEnd(); ++it) {
            info.plankopfAttributes[it.key()] = it.value();
        }
        info.plankopfAttributes["ASP"] =
            info.aspName.isEmpty() ? QString() : folderDisplayName(info.aspName);

        for (int n = 1; n <= OpenCirtConfig::MAX_DP_FIRST_SHEET; ++n) {
            const QString suffix = QString("_DP_%1").arg(n);
            const QString bez = sa.gaFl.value(QStringLiteral("OC_BEZEICHNUNG") + suffix).trimmed();
            const QString bas = sa.gaFl.value(QStringLiteral("OC_AKS") + suffix).trimmed();

            // Leere Zeile
            if (bez.isEmpty() && bas.isEmpty()) continue;

            // Zeile 1 der Folgeblaetter traegt den Uebertrag, keinen Datenpunkt
            if (bas.isEmpty() &&
                (bez.compare(uebertragAscii, Qt::CaseInsensitive) == 0 ||
                 bez.compare(uebertragUmlaut, Qt::CaseInsensitive) == 0)) {
                continue;
            }

            DataPoint dp;
            dp.dpIndex = n;
            dp.aks = bas;
            dp.basString = bas;
            dp.integDp = sa.gaFl.value(QStringLiteral("OC_INTEG") + suffix).trimmed();

            // OC_BEZEICHNUNG_DP_n traegt "BMK - Klartext" (siehe FillGaFl.lsp)
            const int sep = bez.indexOf(QStringLiteral(" - "));
            if (sep > 0) {
                dp.bmk = bez.left(sep).trimmed();
                dp.bezeichnung = bez.mid(sep + 3).trimmed();
            } else {
                dp.bezeichnung = bez;
            }

            for (const QString& fb : kFuncBases) {
                const QString v = sa.gaFl.value(fb + suffix).trimmed();
                if (!v.isEmpty()) dp.funktionsWerte[fb] = v;
            }

            info.dataPoints.append(dp);
        }

        dpTotal += info.dataPoints.size();
        info.gaFlSheetCount = 1;
        sheetPaths << sheetPath;
        result.append(info);
    }

    log(QString("GA-FL-Blaetter gelesen: %1 Blaetter, %2 Datenpunkte%3")
        .arg(result.size()).arg(dpTotal)
        .arg(unlesbar > 0 ? QString(", %1 nicht lesbar").arg(unlesbar) : QString()));
    return result;
}

void OpenCirtTab::createSummen(RunCounts& counts) {
    showProgress("Phase 3: GA-FL-Blaetter lesen...");
    QStringList sheetPaths;
    QVector<SourceDrawingInfo> sheets = readGaFlSheetsForSummary(sheetPaths);

    if (sheets.isEmpty()) {
        logError("Phase 3: keine lesbaren GA-FL-Blaetter - es werden keine Summenblaetter erzeugt");
        return;
    }

    showProgress("Summenblaetter erzeugen...");
    planSummarySheets(sheets);

    const QString vorlage = templatePath(OpenCirtConfig::GA_FL_VORLAGE_DWG);
    const QVector<QStringList> noReference;   // Summen: ohne Referenz

    OcLogFile fillLog;
    fillLog.open(m_extractTempDir + "/fillgafl_log.txt", false);
    OcFillState fillState;

    beginPhase("Summenblaetter", m_summaryJobs.size());
    for (const SummaryJob& summary : m_summaryJobs) {
        const QString name = QFileInfo(summary.target).fileName();
        const QStringList lines = OcEngine::readLines(summary.csvPath);

        OcFillJob job;
        job.rows = OcEngine::rowsFromLines(lines);
        job.plankopf = OcEngine::plankopfFromLines(lines);
        job.useReference = false;
        job.sheetNum = summary.sheetNum;
        job.startRow = summary.startRow;
        job.dpCount = summary.dpCount;

        if (!copyTemplate(vorlage, summary.target)) {
            logError(QString("GA-FL-Vorlage nicht kopierbar: %1").arg(name));
            ++counts.errors;
        } else if (processNewDrawing(summary.target, counts, [&](OcDrawing& dwg) {
                       const OcFillResult filled =
                           dwg.fillGaFl(job, noReference, fillState, &fillLog);
                       if (!filled.ok) {
                           logError(QString("%1: %2").arg(name, filled.error));
                       }
                   })) {
            ++counts.summen;
        }
        stepDone(name);
    }
    fillLog.close();

    // Textbreiten ueber alle GA-FL-Blaetter und alle Summenblaetter
    QStringList textwidthTargets = sheetPaths;
    textwidthTargets += m_plannedSummarySheets;
    log(QString("Textbreiten anpassen: %1 Blaetter (GA-FL + Summen)")
        .arg(textwidthTargets.size()));

    beginPhase("Textbreiten", textwidthTargets.size());
    for (const QString& path : textwidthTargets) {
        processDrawing(path, counts, [&](OcDrawing& dwg) {
            const OcTextWidthResult adjusted = dwg.textBreitenAnpassen();
            counts.textbreiten += adjusted.blockCount + adjusted.modelCount;
        });
        stepDone(QFileInfo(path).fileName());
    }
    log(QString("Textbreiten: %1 Texte angepasst").arg(counts.textbreiten));
}

QString OpenCirtTab::findInhaltVorlage() {
    // Complete DIN-A2 sheet: frame, Plankopf and the 22-row entry block already
    // placed. Nothing gets inserted at generation time, only filled.
    return newestVorlage(projectPath(OpenCirtConfig::VORLAGEN_DIR),
                         "OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS*.dwg");
}

int OpenCirtTab::cleanupInhalt() {
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    int deletedCount = 0;
    
    QDirIterator it(drawingsDir, QStringList() << "*_Inhalt_*.dwg" << "*_Inhalt_*.bak"
                    << "0000 Projekt_Inhalt*",
                    QDir::Files, QDirIterator::Subdirectories);
    while (it.hasNext()) {
        QString path = it.next();
        if (QFile::remove(path)) {
            deletedCount++;
        }
    }
    
    if (deletedCount > 0)
        log(QString("Inhalt-Cleanup: %1 Dateien geloescht").arg(deletedCount));
    return deletedCount;
}

/**
 * @brief Build TOC entries from the ordered DWG list.
 *
 * Iterates all DWGs in publish order, skips Deckblatt/Summe/GA-FL/Inhalt,
 * and creates one TocEntry per content drawing. Group headers (Los, ASP,
 * Gewerk changes) are inserted as separate entries with only the group
 * field filled.
 *
 * Page numbers account for: Deckblatt(1) + Inhalt(tocPageCount) + Summe(n)
 * appearing before content.
 */
QVector<OpenCirtTab::TocEntry> OpenCirtTab::buildTocEntries(
    const QStringList& orderedDwgs, int tocPageCount)
{
    QVector<TocEntry> entries;
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    drawingsDir.replace("\\", "/");
    
    QString lastLos, lastAsp, lastGewerk;
    
    for (int i = 0; i < orderedDwgs.size(); ++i) {
        QString dwgPath = orderedDwgs[i];
        dwgPath.replace("\\", "/");
        QString fileName = QFileInfo(dwgPath).completeBaseName();
        
        // Skip non-content files
        if (fileName.contains("_Deckblatt") || fileName.contains("_Summe_") ||
            fileName.contains("_GA_FL_") || fileName.contains("_Inhalt_") ||
            fileName.startsWith("0000 Projekt_Deckblatt") ||
            fileName.startsWith("0000 Projekt_Inhalt") ||
            fileName.startsWith("0001 Projekt_Summe")) {
            continue;
        }
        
        // Parse hierarchy from folder path
        // Full:  .../Zeichnungen/Los/ASP/Gewerk/Anlage/filename.dwg = 5 parts
        // Short: .../Zeichnungen/Los/ASP/Gewerk/filename.dwg = 4 parts
        QString relPath = dwgPath.mid(drawingsDir.length() + 1);
        QStringList parts = relPath.split("/");
        
        // Need at least: Los/ASP/Gewerk/filename.dwg = 4 parts
        if (parts.size() < 4) continue;
        
        QString losFolder = parts[0];       // "01 Los 1 Anlagenautomation"
        QString aspFolder = parts[1];       // "01 ASP01"
        QString gewerkFolder = parts[2];    // "01 RLT"
        
        // Anlage subfolder is optional (5+ parts means Anlage exists)
        QString anlageFolder;
        if (parts.size() >= 5) {
            anlageFolder = parts[3];        // "00 TKA1010"
        }
        
        QString losName = folderDisplayName(losFolder);
        QString aspName = folderDisplayName(aspFolder);
        QString gewerkName = folderDisplayName(gewerkFolder);
        QString anlageName = anlageFolder.isEmpty() ? QString() : folderDisplayName(anlageFolder);
        
        // Page number: position i in orderedDwgs, shifted by tocPageCount
        // because Inhalt pages are inserted between Deckblatt(pos 0) and rest
        // Final: Deckblatt(1), Inhalt(2..1+toc), then pos 1+ shifted by toc
        int pageNum = i + 1 + tocPageCount;
        
        // Insert group headers on change
        if (losName != lastLos) {
            TocEntry losEntry;
            losEntry.los = losName;
            entries.append(losEntry);
            lastLos = losName;
            lastAsp.clear();
            lastGewerk.clear();
        }
        
        if (aspName != lastAsp) {
            TocEntry aspEntry;
            aspEntry.asp = aspName;
            entries.append(aspEntry);
            lastAsp = aspName;
            lastGewerk.clear();
        }
        
        if (gewerkName != lastGewerk) {
            TocEntry gewerkEntry;
            gewerkEntry.gewerk = gewerkName;
            entries.append(gewerkEntry);
            lastGewerk = gewerkName;
        }
        
        // Content entry: Anlage + ZEICHNUNGSNUMMER + page
        TocEntry entry;
        entry.anlage = anlageName;
        
        // Read ZEICHNUNGSNUMMER from DWG, fallback to filename
        QString zeichnungsNr = readZeichnungsnummer(dwgPath);
        entry.zeichnungsNr = zeichnungsNr.isEmpty() ? fileName : zeichnungsNr;
        entry.seite = pageNum;
        
        entries.append(entry);
    }
    
    return entries;
}

/**
 * @brief Create the Inhaltsverzeichnis DWG pages.
 *
 * The template is a complete DIN-A2 sheet that already carries the 22-row
 * entry block in its final position, so no INSERT and no geometry maths are
 * needed. Per page: copy the template, write the row attributes, set the
 * Plankopf master data, save.
 *
 * Row attributes are addressed by tag: OC_INHALT_<FIELD>_<NN>, NN = 01..22.
 * Rows left over on the last page keep the template's empty attributes.
 *
 * @return number of pages created, -1 if the template is missing
 */
int OpenCirtTab::createInhaltPages(
    const QVector<TocEntry>& entries, int tocPageCount)
{
    QString vorlage = findInhaltVorlage();
    if (vorlage.isEmpty()) {
        logError("Inhaltsverzeichnis: Vorlage OC_VORLAGE_DIN_A2_INHALTSVERZEICHNIS*.dwg "
                 "nicht im Vorlagenordner gefunden");
        return -1;
    }

    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    drawingsDir.replace("\\", "/");

    // Plankopf-Stammdaten sicherstellen. Die Inhaltsseiten werden frisch aus der
    // Vorlage kopiert; ohne diesen Schritt bliebe im Plankopf der Vorlagenstand
    // stehen (AG/AN/PR leer bzw. Platzhalter).
    ensurePlankopfCsvLoaded();
    const OcPairs plankopfPairs = pairsFromMap(m_plankopfCsvData);

    RunCounts counts;
    int created = 0;
    int entryIdx = 0;

    beginPhase("Inhaltsverzeichnis", tocPageCount);
    for (int page = 1; page <= tocPageCount; ++page) {
        QString targetName = QString("0000 Projekt_Inhalt_%1.dwg")
                             .arg(page, 2, 10, QChar('0'));
        QString targetPath = drawingsDir + "/" + targetName;

        // Collect this page's tag/value pairs
        int rowsThisPage = qMin(OpenCirtConfig::INHALT_ROWS_PER_PAGE, entries.size() - entryIdx);
        OcPairs pairs;

        for (int row = 0; row < rowsThisPage && entryIdx < entries.size(); ++row, ++entryIdx) {
            const TocEntry& e = entries[entryIdx];
            QString suffix = QString("_%1").arg(row + 1, 2, 10, QChar('0'));

            auto addPair = [&pairs, &suffix](const QString& field, const QString& value) {
                if (value.isEmpty()) return;
                // Anfuehrungszeichen fielen beim Weg ueber das LISP weg
                QString cleaned = value;
                cleaned.remove(QLatin1Char('"'));
                pairs << qMakePair(QString("OC_INHALT_%1%2").arg(field, suffix), cleaned);
            };

            addPair("LOS", e.los);
            addPair("ASP", e.asp);
            addPair("GEWERK", e.gewerk);
            addPair("ANLAGE", e.anlage);
            addPair("ZEICHNUNGSNUMMER", e.zeichnungsNr);
            if (e.seite > 0)
                addPair("SEITE", QString::number(e.seite));
        }

        // Plankopf-Zeichnungsnummer. Bewusst nach den Stammdaten, damit dieser
        // Wert gewinnt, falls die plankopfdaten.csv selbst ZEICHNUNGSNUMMER fuehrt.
        // Die Eintragszeilen nutzen OC_INHALT_ZEICHNUNGSNUMMER_<NN> und bleiben
        // davon unberuehrt.
        OcPairs inhaltPlankopf;
        inhaltPlankopf << qMakePair(QStringLiteral("ZEICHNUNGSNUMMER"),
            (tocPageCount > 1)
            ? QString("Inhaltsverzeichnis Seite %1 von %2").arg(page).arg(tocPageCount)
            : QString("Inhaltsverzeichnis"));

        if (!copyTemplate(vorlage, targetPath)) {
            logError(QString("Inhaltsverzeichnis-Vorlage nicht kopierbar: %1").arg(targetName));
        } else if (processNewDrawing(targetPath, counts, [&](OcDrawing& dwg) {
                       // Der Eintragsblock steht bereits im Blatt. Er wird am
                       // Namen mit Platzhalter erkannt, damit eine spaetere
                       // Fassung _V_6 keine Aenderung am Code braucht.
                       if (!pairs.isEmpty()) {
                           const int hits = dwg.setAttributes(
                               pairs, OpenCirtConfig::INHALT_BLOCK_PATTERN, true);
                           if (hits == 0) {
                               log(QString("Eintragsblock %1 nicht gefunden oder Attribut-Tags "
                                           "ohne Zeilenindex: %2")
                                   .arg(OpenCirtConfig::INHALT_BLOCK_PATTERN, targetName), "WARN");
                           }
                       }
                       if (!plankopfPairs.isEmpty()) {
                           dwg.setAttributes(plankopfPairs);
                       }
                       dwg.setAttributes(inhaltPlankopf);
                   })) {
            ++created;
        }
        stepDone(targetName);
    }

    log(QString("Inhaltsverzeichnis: %1 Seiten, %2 Eintraege%3")
        .arg(created).arg(entries.size())
        .arg(plankopfPairs.isEmpty() ? ", ohne Plankopf-Stammdaten"
                                     : QString(", inkl. %1 Plankopf-Attributen")
                                       .arg(m_plankopfCsvData.size())));
    return created;
}

// ============================================================================
// PDF Publish - DSD Generation & PUBLISH Command
// ============================================================================

/**
 * @brief Collect all DWGs in correct publish order.
 *
 * Order per folder level:
 *   1. Deckblatt (0000 prefix, non-Summe files)
 *   2. Subfolders recursively (sorted)
 *   3. Summe sheets (contain "_Summe")
 *
 * This ensures: Projekt_Deckblatt -> ASP contents -> Projekt_Summe
 * Within ASP:   ASP_Deckblatt -> Gewerk contents -> ASP_Summe
 * Within Gewerk: Gewerk_Deckblatt -> Source + GA-FL pairs
 */
QStringList OpenCirtTab::collectOrderedDwgsForPublish() {
    QStringList result;
    QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
    
    // Recursive lambda: process one folder level
    std::function<void(const QString&)> processFolder;
    processFolder = [&](const QString& folderPath) {
        QDir dir(folderPath);
        
        // 1. Collect DWGs in this folder, split into 3 groups:
        //    - Deckblatt files (0000 prefix, no _Summe) -> first
        //    - Content files (everything else, no _Summe) -> after Deckblatt
        //    - Summe files (contain _Summe) -> last at this level
        QStringList allDwgs = dir.entryList({"*.dwg"}, QDir::Files, QDir::Name | QDir::IgnoreCase);
        QStringList deckblattFiles;
        QStringList contentFiles;
        QStringList summeFiles;
        
        for (const QString& f : allDwgs) {
            if (f.contains("_Summe", Qt::CaseInsensitive)) {
                summeFiles << folderPath + "/" + f;
            } else if (f.startsWith("0000")) {
                deckblattFiles << folderPath + "/" + f;
            } else {
                contentFiles << folderPath + "/" + f;
            }
        }
        
        // 2. Deckblatt first (0000 prefix)
        result.append(deckblattFiles);
        
        // 3. Summe directly after Deckblatt
        result.append(summeFiles);
        
        // 4. Content files (sorted alphabetically)
        result.append(contentFiles);
        
        // 5. Recurse into subfolders (sorted)
        QStringList subDirs = dir.entryList(QDir::Dirs | QDir::NoDotAndDotDot, QDir::Name | QDir::IgnoreCase);
        for (const QString& sub : subDirs) {
            processFolder(folderPath + "/" + sub);
        }
    };
    
    processFolder(drawingsDir);
    return result;
}

/**
 * @brief Read the first paperspace layout name from a DWG via side-database.
 *
 * Opens the DWG as a read-only side-database (not in the editor),
 * iterates the layout dictionary, and returns the name of the
 * paperspace layout with the lowest tab order (i.e. the first tab).
 *
 * @param dwgPath Full path to the DWG file
 * @return Layout name (e.g. "DIN-A2"), or "Layout1" as fallback
 */
static QString getFirstPaperSpaceLayoutName(const QString& dwgPath)
{
    AcDbDatabase* pDb = new AcDbDatabase(false, false);
    std::wstring wpath = dwgPath.toStdWString();

    Acad::ErrorStatus es = pDb->readDwgFile(
        wpath.c_str(), AcDbDatabase::kForReadAndAllShare, false);
    if (es != Acad::eOk) {
        delete pDb;
        return QStringLiteral("Layout1");
    }

    AcDbDictionary* pLayoutDict = nullptr;
    es = pDb->getLayoutDictionary(pLayoutDict, AcDb::kForRead);
    if (es != Acad::eOk || !pLayoutDict) {
        delete pDb;
        return QStringLiteral("Layout1");
    }

    QString bestName = QStringLiteral("Layout1");
    int bestTab = INT_MAX;

    AcDbDictionaryIterator* pIter = pLayoutDict->newIterator();
    while (pIter && !pIter->done()) {
        AcDbObject* pObj = nullptr;
        if (pIter->getObject(pObj, AcDb::kForRead) == Acad::eOk && pObj) {
            AcDbLayout* pLayout = AcDbLayout::cast(pObj);
            if (pLayout) {
                const ACHAR* name = nullptr;
                pLayout->getLayoutName(name);
                if (name) {
                    QString qName = QString::fromWCharArray(name);
                    if (qName.compare(QStringLiteral("Model"), Qt::CaseInsensitive) != 0) {
                        int tab = pLayout->getTabOrder();
                        if (tab < bestTab) {
                            bestTab = tab;
                            bestName = qName;
                        }
                    }
                }
            }
            pObj->close();
        }
        pIter->next();
    }
    delete pIter;
    pLayoutDict->close();
    delete pDb;

    return bestName;
}

/**
 * @brief Read the ZEICHNUNGSNUMMER attribute from ModelSpace via side-database.
 *
 * Opens the DWG as a read-only side-database, iterates all block references
 * in ModelSpace, finds one whose block name starts with "OC_RSH_Plankopf_quer"
 * (version suffix ignored), and returns the value of the ZEICHNUNGSNUMMER attribute.
 *
 * @param dwgPath Full path to the DWG file
 * @return Attribute value, or empty string if not found
 */
static QString readZeichnungsnummer(const QString& dwgPath)
{
    AcDbDatabase* pDb = new AcDbDatabase(false, false);
    std::wstring wpath = dwgPath.toStdWString();

    Acad::ErrorStatus es = pDb->readDwgFile(
        wpath.c_str(), AcDbDatabase::kForReadAndAllShare, false);
    if (es != Acad::eOk) {
        delete pDb;
        return QString();
    }

    QString result;

    // Open ModelSpace block table record
    AcDbBlockTable* pBT = nullptr;
    es = pDb->getBlockTable(pBT, AcDb::kForRead);
    if (es != Acad::eOk || !pBT) {
        delete pDb;
        return QString();
    }

    AcDbBlockTableRecord* pMS = nullptr;
    es = pBT->getAt(ACDB_MODEL_SPACE, pMS, AcDb::kForRead);
    pBT->close();
    if (es != Acad::eOk || !pMS) {
        delete pDb;
        return QString();
    }

    // Iterate all entities in ModelSpace
    AcDbBlockTableRecordIterator* pIter = nullptr;
    pMS->newIterator(pIter);

    while (pIter && !pIter->done()) {
        AcDbEntity* pEnt = nullptr;
        if (pIter->getEntity(pEnt, AcDb::kForRead) == Acad::eOk && pEnt) {
            AcDbBlockReference* pRef = AcDbBlockReference::cast(pEnt);
            if (pRef) {
                // Get block name
                AcDbObjectId blockId = pRef->blockTableRecord();
                AcDbBlockTableRecord* pBlkRec = nullptr;
                if (acdbOpenObject(pBlkRec, blockId, AcDb::kForRead) == Acad::eOk && pBlkRec) {
                    const ACHAR* blkName = nullptr;
                    pBlkRec->getName(blkName);
                    QString qBlkName = blkName ? QString::fromWCharArray(blkName) : QString();
                    pBlkRec->close();

                    // Check if block name starts with "OC_RSH_Plankopf_quer" (ignore version)
                    if (qBlkName.startsWith(QStringLiteral("OC_RSH_Plankopf_quer"), Qt::CaseInsensitive)) {
                        // Iterate attributes
                        AcDbObjectIterator* pAttIter = pRef->attributeIterator();
                        while (pAttIter && !pAttIter->done()) {
                            AcDbObject* pAttObj = nullptr;
                            if (acdbOpenObject(pAttObj, pAttIter->objectId(), AcDb::kForRead) == Acad::eOk && pAttObj) {
                                AcDbAttribute* pAtt = AcDbAttribute::cast(pAttObj);
                                if (pAtt) {
                                    const ACHAR* tag = pAtt->tag();
                                    if (tag) {
                                        QString qTag = QString::fromWCharArray(tag);
                                        if (qTag.compare(QStringLiteral("ZEICHNUNGSNUMMER"), Qt::CaseInsensitive) == 0) {
                                            const ACHAR* val = pAtt->textString();
                                            if (val) {
                                                result = QString::fromWCharArray(val).trimmed();
                                            }
                                        }
                                    }
                                }
                                pAttObj->close();
                            }
                            pAttIter->step();
                        }
                        delete pAttIter;

                        // Found the Plankopf block, stop searching
                        if (!result.isEmpty()) {
                            pEnt->close();
                            break;
                        }
                    }
                }
            }
            pEnt->close();
        }
        pIter->step();
    }
    delete pIter;
    pMS->close();
    delete pDb;

    return result;
}

/**
 * @brief Generate a DSD (Drawing Set Description) file for PUBLISH command.
 *
 * DSD is a Windows INI-style format:
 *   [DWF6Version] / [DWF6MinorVersion]
 *   [DWF6Sheet:SheetName] per drawing
 *   [Target] with Type=6 for multi-sheet PDF
 *   [PdfOptions] with CreateBookmarks=TRUE
 *
 * For each DWG, the actual paperspace layout name is read from the file
 * via a side-database (not "Layout1" hardcoded).
 *
 * @param orderedDwgs List of DWG paths in desired publish order
 * @return Path to generated DSD file, or empty on error
 */
QString OpenCirtTab::generateDsdFile(const QStringList& orderedDwgs) {
    // Output PDF path: project root / 06- Plot / Projektname.pdf
    QString projektName = QDir(m_projectRoot).dirName();
    QString plotDir = m_projectRoot + "/06- Plot";
    QDir().mkpath(plotDir);  // Create if not exists
    // Pfade im DSD in der Schreibweise des Betriebssystems:
    // Windows mit Backslash, Linux mit Schraegstrich
    QString pdfPath = plotDir + "/" + projektName + ".pdf";
    pdfPath = QDir::toNativeSeparators(pdfPath);
    
    QString outDir = plotDir;
    outDir = QDir::toNativeSeparators(outDir);
    
    QString dsd;
    dsd += "[DWF6Version]\n";
    dsd += "Ver=1\n";
    dsd += "[DWF6MinorVersion]\n";
    dsd += "MinorVer=1\n";
    
    int sheetIdx = 0;
    for (const QString& dwgPath : orderedDwgs) {
        QString fileName = QFileInfo(dwgPath).completeBaseName();
        QString dwgWin = QDir::toNativeSeparators(dwgPath);
        
        // Read actual layout name from DWG (e.g. "DIN-A2", "DIN-A1")
        QString layoutName = getFirstPaperSpaceLayoutName(dwgPath);
        
        // Read ZEICHNUNGSNUMMER attribute from ModelSpace Plankopf block
        // Use as bookmark title if available, fallback to filename
        QString zeichnungsNr = readZeichnungsnummer(dwgPath);
        QString displayName = zeichnungsNr.isEmpty() ? fileName : zeichnungsNr;
        
        // Sheet name: 4-digit sequential number + display name
        // Guarantees uniqueness in DSD (INI format: duplicate sections overwrite!)
        // Also serves as readable bookmark title in the PDF table of contents
        QString sheetName = QString("%1-%2")
            .arg(sheetIdx + 1, 4, 10, QChar('0'))
            .arg(displayName);
        
        dsd += QString("[DWF6Sheet:%1]\n").arg(sheetName);
        dsd += QString("DWG=%1\n").arg(dwgWin);
        dsd += QString("Layout=%1\n").arg(layoutName);
        dsd += "Setup=\n";
        dsd += QString("OriginalSheetPath=%1\n").arg(dwgWin);
        dsd += "Has Plot Port=0\n";
        dsd += "Has3DDWF=0\n";
        
        log(QString("  Sheet %1: %2 -> \"%3\" (Layout \"%4\")")
            .arg(sheetIdx + 1, 3).arg(fileName, displayName, layoutName));
        sheetIdx++;
    }
    
    // Target section: Type=6 = multi-sheet PDF
    dsd += "[Target]\n";
    dsd += "Type=6\n";
    dsd += QString("DWF=%1\n").arg(pdfPath);
    dsd += QString("OUT=%1\n").arg(outDir);
    dsd += "PWD=\n";
    
    // MRU sections (required by BricsCAD)
    dsd += "[MRU Local]\n";
    dsd += "MRU=0\n";
    dsd += "[MRU Sheet List]\n";
    dsd += "MRU=0\n";
    
    // PDF options: bookmarks = table of contents
    dsd += "[PdfOptions]\n";
    dsd += "IncludeHyperlinks=TRUE\n";
    dsd += "CreateBookmarks=TRUE\n";
    dsd += "CaptureFontsInDrawing=TRUE\n";
    dsd += "ConvertTextToGeometry=FALSE\n";
    
    // Write DSD to temp file
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString dsdPath = tempPath + "/OpenCirt_publish.dsd";
    
    QFile file(dsdPath);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QString("DSD-Datei konnte nicht geschrieben werden: %1").arg(dsdPath));
        return QString();
    }
    
    QTextStream stream(&file);
    stream << dsd;
    file.close();
    
    log(QString("DSD-Datei erzeugt: %1 Blaetter, Ausgabe: %2")
        .arg(sheetIdx).arg(QFileInfo(pdfPath).fileName()));
    
    return dsdPath;
}

void OpenCirtTab::onPublishPdf() {
    if (!requireProjectStructure()) return;
    
    log("=== PDF PUBLIZIEREN ===");
    
    // Step 0: Cleanup old Inhalt DWGs
    cleanupInhalt();
    
    // Step 1: Collect DWGs WITHOUT Inhalt pages (they don't exist yet)
    QStringList orderedDwgs = collectOrderedDwgsForPublish();
    
    if (orderedDwgs.isEmpty()) {
        reportError("PDF publizieren", "Keine DWG-Dateien im Zeichnungsordner gefunden",
                    QString(), false);
        return;
    }
    
    // Step 2: Calculate TOC size
    // First pass: count content entries + group headers to determine page count
    // We need a preliminary pass without tocPageCount to count entries,
    // then calculate tocPageCount from entry count.
    QVector<TocEntry> prelimEntries = buildTocEntries(orderedDwgs, 0);
    
    static const int MAX_ROWS_PER_PAGE = OpenCirtConfig::INHALT_ROWS_PER_PAGE;
    int tocPageCount = 0;
    if (!prelimEntries.isEmpty()) {
        tocPageCount = (prelimEntries.size() + MAX_ROWS_PER_PAGE - 1) / MAX_ROWS_PER_PAGE;
    }
    
    // Step 3: Rebuild entries with correct page numbers (shifted by tocPageCount)
    QVector<TocEntry> entries = buildTocEntries(orderedDwgs, tocPageCount);
    
    // Recalculate in case group headers changed the page count
    int recalcPages = (entries.size() + MAX_ROWS_PER_PAGE - 1) / MAX_ROWS_PER_PAGE;
    if (recalcPages != tocPageCount) {
        tocPageCount = recalcPages;
        entries = buildTocEntries(orderedDwgs, tocPageCount);
    }
    
    // Show summary
    QString projektName = QDir(m_projectRoot).dirName();
    QString pdfName = projektName + ".pdf";
    QString plotDir = m_projectRoot + "/06- Plot";
    int totalPages = orderedDwgs.size() + tocPageCount;
    
    QMessageBox::StandardButton reply = QMessageBox::question(this,
        "PDF publizieren",
        QString("%1 Zeichnungen + %2 Inhaltsseiten = %3 Blaetter\n"
                "werden als Multi-Sheet PDF publiziert.\n\n"
                "Ausgabedatei: %4\n"
                "Speicherort: %5\n\n"
                "Schritt 1: Inhaltsverzeichnis erzeugen\n"
                "Schritt 2: PDF automatisch publizieren\n\n"
                "Fortfahren?")
        .arg(orderedDwgs.size())
        .arg(tocPageCount)
        .arg(totalPages)
        .arg(pdfName)
        .arg(plotDir),
        QMessageBox::Yes | QMessageBox::Cancel);
    
    if (reply != QMessageBox::Yes) return;
    
    log(QString("Inhaltsverzeichnis: %1 Eintraege auf %2 Seiten")
        .arg(entries.size()).arg(tocPageCount));
    
    if (tocPageCount > 0) {
        beginRun(m_btnPublish, "Inhalt wird erstellt...");
        const int pages = createInhaltPages(entries, tocPageCount);
        endRun();
        if (pages < tocPageCount) {
            logError("Inhaltsverzeichnis konnte nicht vollstaendig erzeugt werden - "
                     "PDF wird nicht publiziert");
            return;
        }
        logSuccess("Inhaltsverzeichnis erzeugt.");

        // Re-collect DWGs (now includes the new Inhalt pages)
        QStringList finalDwgs = collectOrderedDwgsForPublish();

        log(QString("Publish-Reihenfolge (%1 Blaetter):").arg(finalDwgs.size()));
        QString drawingsDir = projectPath(OpenCirtConfig::ZEICHNUNGEN_DIR);
        for (int i = 0; i < finalDwgs.size(); ++i) {
            QString rel = finalDwgs[i];
            if (rel.startsWith(drawingsDir)) {
                rel = rel.mid(drawingsDir.length() + 1);
            }
            log(QString("  %1. %2").arg(i + 1, 3).arg(rel));
        }

        launchPublish(finalDwgs);
    } else {
        // No TOC entries -> publish directly
        log("Keine Inhaltseintraege gefunden - publiziere ohne Inhaltsverzeichnis.");
        launchPublish(orderedDwgs);
    }
}

/**
 * @brief Launch BricsCAD PDF publish via /b batch instance with SCR control.
 *
 * Creates a SCR file that:
 *   1. Suppresses all dialogs (FILEDIA=0, CMDECHO=0, EXPERT=5)
 *   2. Runs -PUBLISH with the generated DSD file
 *   3. Writes a completion marker file
 *   4. Quits BricsCAD (_.QUIT)
 *
 * The /b switch forces a separate BricsCAD batch instance.
 * Proven call pattern: bricscad.exe /b "first.dwg" "script.scr"
 * Since FILEDIA=0, BricsCAD uses the PDF path from the DSD (no filepicker).
 * A QTimer polls for the marker file to detect completion.
 */
void OpenCirtTab::launchPublish(const QStringList& orderedDwgs) {
    // Generate DSD file
    QString dsdPath = generateDsdFile(orderedDwgs);
    if (dsdPath.isEmpty()) {
        logError("DSD-Generierung fehlgeschlagen");
        return;
    }
    
    // Compute the PDF output path (same logic as generateDsdFile)
    QString projektName = QDir(m_projectRoot).dirName();
    QString plotDir = m_projectRoot + "/06- Plot";
    m_publishedPdfPath = plotDir + "/" + projektName + ".pdf";
    
    // Paths for SCR generation
    QString tempPath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
    QString scrPath = tempPath + "/OpenCirt_publish.scr";
    QString markerPath = tempPath + "/OpenCirt_publish_done.marker";
    
    // Clean old marker
    QFile::remove(markerPath);
    
    // Forward-slash versions for LISP/SCR
    QString dsdSlash = dsdPath;
    dsdSlash.replace("\\", "/");
    QString markerSlash = markerPath;
    markerSlash.replace("\\", "/");
    
    // Build SCR content
    QString scr;
    scr += "(progn (setvar \"FILEDIA\" 0)(princ))\n";
    scr += "(progn (setvar \"CMDECHO\" 0)(princ))\n";
    scr += "(progn (setvar \"EXPERT\" 5)(princ))\n";
    scr += "(progn (setvar \"BACKGROUNDPLOT\" 0)(princ))\n";
    scr += QString("_.-PUBLISH \"%1\"\n").arg(dsdSlash);
    scr += "(progn (setvar \"BACKGROUNDPLOT\" 2)(princ))\n";
    scr += "(progn (setvar \"FILEDIA\" 1)(princ))\n";
    scr += "(progn (setvar \"CMDECHO\" 1)(princ))\n";
    scr += "(progn (setvar \"EXPERT\" 0)(princ))\n";
    
    // Find newest PDF in plot folder and write its path to marker file
    QString plotSlash = (m_projectRoot + "/06- Plot/");
    plotSlash.replace("\\", "/");
    scr += QString(
        "(progn"
        "  (setq oc-dir \"%1\")"
        "  (setq oc-files (vl-directory-files oc-dir \"*.pdf\" 1))"
        "  (setq oc-best nil oc-btime 0)"
        "  (foreach oc-f oc-files"
        "    (setq oc-ft (vl-file-systime (strcat oc-dir oc-f)))"
        "    (if oc-ft (progn"
        "      (setq oc-tv (+ (* (nth 0 oc-ft) 10000000000.0)"
        "                     (* (nth 1 oc-ft) 100000000.0)"
        "                     (* (nth 3 oc-ft) 1000000.0)"
        "                     (* (nth 4 oc-ft) 10000.0)"
        "                     (* (nth 5 oc-ft) 100.0)"
        "                     (nth 6 oc-ft)))"
        "      (if (> oc-tv oc-btime)"
        "        (setq oc-btime oc-tv oc-best oc-f)))))"
        "  (setq oc-mk (open \"%2\" \"w\"))"
        "  (if oc-best"
        "    (write-line (strcat oc-dir oc-best) oc-mk)"
        "    (write-line \"PUBLISH_COMPLETE\" oc-mk))"
        "  (close oc-mk)"
        "  (princ))\n")
        .arg(plotSlash, markerSlash);
    scr += "_.QUIT\n";
#ifndef _WIN32
    // Unter Linux gilt die Zeichnung der Batch-Instanz nach dem Publizieren als
    // geaendert; QUIT fragt dann "Aenderungen verwerfen?" und die Instanz
    // bliebe offen. Die Antwort wird nur gelesen, wenn die Frage kommt.
    scr += "_Y\n";
#endif
    
    // Write SCR to temp file
    QFile scrFile(scrPath);
    if (!scrFile.open(QIODevice::WriteOnly | QIODevice::Text)) {
        logError(QString("SCR-Datei konnte nicht geschrieben werden: %1").arg(scrPath));
        return;
    }
    QTextStream stream(&scrFile);
    stream << scr;
    scrFile.close();
    
    // Launch separate BricsCAD batch instance: /b "dwg" "scr"
    QString bricscadExe = QCoreApplication::applicationFilePath();
    QString firstDwg = orderedDwgs.first();
    // Schreibweise des Betriebssystems; der Aufruf /b "dwg" "scr" ist unter
    // Windows und Linux derselbe
    QString dwgPathWin = QDir::toNativeSeparators(firstDwg);
    QString scrPathWin = QDir::toNativeSeparators(scrPath);
    
    QStringList args;
    args << "/b" << dwgPathWin << scrPathWin;
    
    QString pdfName = projektName + ".pdf";
    
    log(QString("Starte BricsCAD Batch-Instanz: %1").arg(bricscadExe));
    log(QString("  /b \"%1\" \"%2\"").arg(dwgPathWin, scrPathWin));
    log(QString("  SCR: FILEDIA=0 -> -PUBLISH -> Marker -> _.QUIT"));
    log(QString("  PDF-Ausgabe: %1").arg(m_publishedPdfPath));
    
    bool started = QProcess::startDetached(bricscadExe, args);
    if (started) {
        logSuccess(QString("PDF-Publish gestartet in Batch-Instanz - Ausgabe: %1").arg(pdfName));
        
        // Start polling for completion marker
        m_pdfDoneMarkerPath = markerPath;
        m_btnPublish->setEnabled(false);
        m_btnPublish->setText("PDF wird erzeugt...");
        m_pdfDoneTimer->start();
    } else {
        logError("BricsCAD Batch-Instanz konnte nicht gestartet werden");
    }
}

/**
 * @brief Poll for PDF publish completion via marker file.
 *
 * The SCR in the /b batch instance writes a marker file after -PUBLISH
 * completes and before _.QUIT. Once the marker exists, publish is done.
 * Show success notification with PDF file info.
 */
void OpenCirtTab::onPdfDonePollTimer() {
    if (m_pdfDoneMarkerPath.isEmpty()) {
        m_pdfDoneTimer->stop();
        return;
    }
    
    if (!QFile::exists(m_pdfDoneMarkerPath)) {
        return;  // Still running
    }
    
    // === Marker found! Read PDF path from marker content ===
    QString pdfPath;
    {
        QFile markerFile(m_pdfDoneMarkerPath);
        if (markerFile.open(QIODevice::ReadOnly | QIODevice::Text)) {
            pdfPath = QTextStream(&markerFile).readLine().trimmed();
            markerFile.close();
        }
    }
    
    // Check if the PDF exists and has content (may still be flushing)
    if (!pdfPath.isEmpty() && pdfPath != "PUBLISH_COMPLETE") {
        QFileInfo pdfInfo(pdfPath);
        if (!pdfInfo.exists() || pdfInfo.size() == 0) {
            return;  // Keep polling - PDF still being written to disk
        }
    }
    
    // === Publish complete! ===
    m_pdfDoneTimer->stop();
    QFile::remove(m_pdfDoneMarkerPath);
    m_pdfDoneMarkerPath.clear();
    
    // Restore button
    m_btnPublish->setText("PDF publizieren");
    m_btnPublish->setEnabled(true);
    
    // Show result
    if (!pdfPath.isEmpty() && pdfPath != "PUBLISH_COMPLETE") {
        QFileInfo pdfInfo(pdfPath);
        QString sizeStr;
        qint64 bytes = pdfInfo.size();
        if (bytes >= 1024 * 1024) {
            sizeStr = QString("%1 MB").arg(bytes / (1024.0 * 1024.0), 0, 'f', 1);
        } else {
            sizeStr = QString("%1 KB").arg(bytes / 1024.0, 0, 'f', 0);
        }
        
        logSuccess(QString("PDF-Publish abgeschlossen: %1 (%2)")
                   .arg(pdfInfo.fileName(), sizeStr));
        
        QMessageBox::information(this, "PDF-Publish abgeschlossen",
            QString("PDF erfolgreich erzeugt:\n\n"
                    "%1\n"
                    "Groesse: %2\n"
                    "Speicherort: %3")
            .arg(pdfInfo.fileName())
            .arg(sizeStr)
            .arg(pdfInfo.absolutePath()));
    } else {
        logSuccess("PDF-Publish abgeschlossen.");
        QMessageBox::information(this, "PDF-Publish abgeschlossen",
            "Der Publish-Vorgang wurde erfolgreich beendet.\n\n"
            "Die PDF-Datei wurde am gewaehlten Speicherort abgelegt.");
    }
    
    m_publishedPdfPath.clear();
}

} // namespace BatchProcessing
