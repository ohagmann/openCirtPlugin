#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file OpenCirtPlugin.cpp
 * @brief BRX-Einstiegspunkt: Befehle OPENCIRT und OC, Fenster oeffnen,
 *        LISP-Funktion opencirt-auftrag (Auftraege von aussen)
 */

// WICHTIG: Platform header MUSS zuerst kommen!
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

// Dann erst die anderen BRX headers
#include "aced.h"
#include "AcRx/AcRxDynamicLinker.h"
#include "OpenCirtPlugin.h"
#include "../ui/OpenCirtWindow.h"
#include "../ui/OpenCirtTab.h"

#include <QApplication>
#include <string>

#ifndef PLUGIN_VERSION
#define PLUGIN_VERSION "unbekannt"
#endif

namespace {

bool g_isInitialized = false;
QApplication* g_qApp = nullptr;
BatchProcessing::OpenCirtWindow* g_window = nullptr;

/// Fenster anlegen (falls noch nicht da) und zeigen
void showWindow(bool activate) {
    if (!g_window) {
        g_window = new BatchProcessing::OpenCirtWindow();
    }
    // Thema bei jedem Oeffnen neu anwenden: schaltet der Anwender BricsCAD
    // zwischenzeitlich zwischen hell und dunkel um, folgt das Fenster.
    g_window->applyTheme();
    g_window->show();
    g_window->raise();
    if (activate) g_window->activateWindow();
}

/// Befehl OPENCIRT bzw. OC: Fenster anlegen oder nach vorn holen
void openCirtCommand() {
    if (!g_isInitialized) {
        acutPrintf(_T("\nopenCirt: Plugin nicht korrekt initialisiert.\n"));
        return;
    }
    showWindow(true);
    if (g_qApp) g_qApp->processEvents();
}

// ----------------------------------------------------------------------------
// LISP-Funktion (opencirt-auftrag "<Pfad der Auftragsdatei>")
// ----------------------------------------------------------------------------
// Uebergibt einen Auftrag (JSON, siehe core/OcAuftrag.h) an das Fenster. Die
// Funktion kehrt sofort zurueck; der Lauf beginnt danach in der
// Ereignisschleife wie nach einem Knopfdruck und ist im Fenster zu sehen.
// Rueckgabe: Text "OK: ..." oder "FEHLER: ...". Den Fortschritt liest der
// Aufrufer aus <Temp>/OpenCirt_status.json.

const ACHAR* const kLispAuftrag = _T("opencirt-auftrag");
const int kLispAuftragCode = 1;

int retText(const QString& text) {
    const std::wstring w = text.toStdWString();
    acedRetStr(w.c_str());
    return RSRSLT;
}

int lispAuftrag() {
    if (!g_isInitialized) {
        return retText("FEHLER: openCirt ist nicht korrekt initialisiert");
    }
    const resbuf* args = acedGetArgs();
    if (!args || args->restype != RTSTR || !args->resval.rstring || args->rbnext) {
        return retText("FEHLER: Aufruf (opencirt-auftrag \"<Pfad der Auftragsdatei>\")");
    }
    const QString path = QString::fromWCharArray(args->resval.rstring);
    showWindow(false);
    return retText(g_window->tab()->acceptAuftrag(path));
}

/// LISP-Funktionen gelten je Zeichnung: bei jeder neu geladenen anmelden
void registerLispFunctions() {
    acedDefun(kLispAuftrag, kLispAuftragCode);
    acedRegFunc(lispAuftrag, kLispAuftragCode);
}

void unregisterLispFunctions() {
    acedUndef(kLispAuftrag, kLispAuftragCode);
}

void registerCommands() {
    // Langform und Kurzform fuehren auf denselben Befehl. Die Kurzform ist
    // ein echter Befehl und braucht keinen Eintrag in der default.pgp.
    acedRegCmds->addCommand(_T("OPENCIRT_CMDS"), _T("OPENCIRT"), _T("OPENCIRT"),
                            ACRX_CMD_MODAL, openCirtCommand);
    acedRegCmds->addCommand(_T("OPENCIRT_CMDS"), _T("OC"), _T("OC"),
                            ACRX_CMD_MODAL, openCirtCommand);
}

void unregisterCommands() {
    acedRegCmds->removeGroup(_T("OPENCIRT_CMDS"));
}

}  // namespace

// ============================================================================
// BRX Entry Point
// ============================================================================

extern "C" BRX_EXPORT
AcRx::AppRetCode acrxEntryPoint(AcRx::AppMsgCode msg, void* pAppId)
{
    switch (msg) {
        case AcRx::kInitAppMsg:
            acrxDynamicLinker->unlockApplication(pAppId);
            acrxDynamicLinker->registerAppMDIAware(pAppId);

            if (!g_qApp) {
                if (QCoreApplication::instance()) {
                    // Der Prozess hat schon eine Qt-Anwendung: BricsCAD selbst
                    // (Linux) oder ein zuvor geladenes Qt-Plugin wie batchTool.
                    // Eine zweite Instanz ist nicht zulaessig, die vorhandene
                    // wird mitbenutzt.
                    g_qApp = qobject_cast<QApplication*>(QCoreApplication::instance());
                    if (!g_qApp) {
                        acutPrintf(_T("\nopenCirt: vorhandene Qt-Instanz ist keine QApplication - Oberflaeche nicht verfuegbar.\n"));
                        return AcRx::kRetError;
                    }
                } else {
                    static int argc = 1;
                    static char* argv[] = { (char*)"openCirt", nullptr };
                    g_qApp = new QApplication(argc, argv);
                    g_qApp->setApplicationName("openCirt");
                    g_qApp->setOrganizationName("openCirt");
                }
            }

            registerCommands();
            acutPrintf(_T("\nopenCirt %hs geladen. Befehl: OPENCIRT (Kurzform OC)\n"), PLUGIN_VERSION);
            g_isInitialized = true;
            break;

        case AcRx::kLoadDwgMsg:
            registerLispFunctions();
            break;

        case AcRx::kUnloadDwgMsg:
            unregisterLispFunctions();
            break;

        case AcRx::kUnloadAppMsg:
            if (g_window) {
                g_window->close();
                delete g_window;
                g_window = nullptr;
            }
            unregisterCommands();
            // Die QApplication bleibt stehen, auch wenn dieses Plugin sie
            // angelegt hat (Windows): ein zweites Qt-Plugin (batchTool) kann
            // sie weiter benutzen, und BricsCAD raeumt beim Beenden auf.
            g_qApp = nullptr;
            g_isInitialized = false;
            acutPrintf(_T("\nopenCirt entladen.\n"));
            break;

        default:
            break;
    }
    return AcRx::kRetOK;
}

// acrxGetApiVersion wird von drx_entrypoint bereitgestellt - hier NICHT definieren.
