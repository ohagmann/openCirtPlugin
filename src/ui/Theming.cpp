#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file Theming.cpp
 * @brief Implementation - Oberflaeche an BricsCADs Thema angleichen
 */

// BRX Platform headers
#ifdef __linux__
#include "brx_platform_linux.h"
#else
#include "brx_platform_windows.h"
#endif

#include "aced.h"

#include "Theming.h"

#include <QApplication>
#include <QStyle>
#include <QStyleFactory>
#include <QWidget>

namespace BatchProcessing {
namespace Theming {

// ============================================================================
// Thema ermitteln
// ============================================================================

bool isDark()
{
    // BricsCAD COLORTHEME: 0 = dunkel, 1 = hell
    struct resbuf rb;
    rb.restype = RTSHORT;
    rb.resval.rint = -1;

    if (acedGetVar(_T("COLORTHEME"), &rb) == RTNORM) {
        if (rb.resval.rint == 0) return true;
        if (rb.resval.rint == 1) return false;
    }

    // Variable nicht verfuegbar (aeltere Version, Linux): aus der
    // vorhandenen Palette schaetzen.
    QColor bg = qApp ? qApp->palette().color(QPalette::Window) : QColor(240, 240, 240);
    return bg.lightness() < 128;
}

// ============================================================================
// Palette
// ============================================================================

QPalette buildPalette(bool dark)
{
    QPalette p;

    if (dark) {
        // An BricsCADs dunkles Thema angelehnt
        const QColor window(0x2B, 0x2B, 0x2B);
        const QColor base(0x1E, 0x1E, 0x1E);
        const QColor alt(0x25, 0x25, 0x25);
        const QColor text(0xD4, 0xD4, 0xD4);
        const QColor button(0x35, 0x35, 0x35);
        const QColor highlight(0x2D, 0x6C, 0xA8);
        const QColor disabled(0x7A, 0x7A, 0x7A);

        p.setColor(QPalette::Window,          window);
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            base);
        p.setColor(QPalette::AlternateBase,   alt);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          button);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::ToolTipBase,     window);
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::Highlight,       highlight);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link,            QColor(0x4F, 0xA8, 0xE0));
        p.setColor(QPalette::PlaceholderText, disabled);

        p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    } else {
        // Helles Thema: Qt-Standard als Grundlage, nur wenig nachgezogen
        const QColor window(0xF0, 0xF0, 0xF0);
        const QColor base(0xFF, 0xFF, 0xFF);
        const QColor alt(0xF7, 0xF7, 0xF7);
        const QColor text(0x1E, 0x1E, 0x1E);
        const QColor button(0xE6, 0xE6, 0xE6);
        const QColor highlight(0x30, 0x7C, 0xC0);
        const QColor disabled(0x9A, 0x9A, 0x9A);

        p.setColor(QPalette::Window,          window);
        p.setColor(QPalette::WindowText,      text);
        p.setColor(QPalette::Base,            base);
        p.setColor(QPalette::AlternateBase,   alt);
        p.setColor(QPalette::Text,            text);
        p.setColor(QPalette::Button,          button);
        p.setColor(QPalette::ButtonText,      text);
        p.setColor(QPalette::ToolTipBase,     base);
        p.setColor(QPalette::ToolTipText,     text);
        p.setColor(QPalette::Highlight,       highlight);
        p.setColor(QPalette::HighlightedText, Qt::white);
        p.setColor(QPalette::Link,            QColor(0x00, 0x66, 0xCC));
        p.setColor(QPalette::PlaceholderText, disabled);

        p.setColor(QPalette::Disabled, QPalette::WindowText, disabled);
        p.setColor(QPalette::Disabled, QPalette::Text,       disabled);
        p.setColor(QPalette::Disabled, QPalette::ButtonText, disabled);
    }

    return p;
}

// ============================================================================
// Abgeleitete Farben
// ============================================================================

static QPalette paletteOf(const QWidget* w)
{
    // Die Palette des Fensters, nicht die des Widgets: Ein Stylesheet mit
    // "color:" schreibt seine Farbe in die Palette des Widgets zurueck. Wer
    // daraus erneut eine gedaempfte Farbe mischt, daempft bei jedem apply()
    // weiter. Unter Linux begann das bei der hellen Standardpalette eines noch
    // elternlosen Labels, danach waren die Beschreibungen neben den Knoepfen
    // im dunklen Thema nicht mehr lesbar.
    if (w) {
        const QWidget* top = w->window();
        return (top && top != w) ? top->palette() : w->palette();
    }
    if (qApp) return qApp->palette();
    return QPalette();
}

QColor mutedText(const QWidget* w)
{
    const QPalette p = paletteOf(w);
    const QColor fg = p.color(QPalette::WindowText);
    const QColor bg = p.color(QPalette::Window);

    // Vordergrund zum Hintergrund hin abmischen: gedaempft, aber lesbar.
    // 62 % Text, 38 % Hintergrund haelt in beiden Themes genug Kontrast.
    return QColor(
        (fg.red()   * 62 + bg.red()   * 38) / 100,
        (fg.green() * 62 + bg.green() * 38) / 100,
        (fg.blue()  * 62 + bg.blue()  * 38) / 100);
}

/// Semantische Farbe je nach Hintergrundhelligkeit waehlen
static QColor semantic(const QWidget* w, const QColor& fuerDunkel, const QColor& fuerHell)
{
    return paletteOf(w).color(QPalette::Window).lightness() < 128 ? fuerDunkel : fuerHell;
}

QColor successText(const QWidget* w)
{
    return semantic(w, QColor(0x6F, 0xC6, 0x9A), QColor(0x2E, 0x7D, 0x32));
}

QColor warningText(const QWidget* w)
{
    return semantic(w, QColor(0xE0, 0xA0, 0x54), QColor(0xE6, 0x51, 0x00));
}

QColor errorText(const QWidget* w)
{
    return semantic(w, QColor(0xE5, 0x7A, 0x73), QColor(0xC6, 0x28, 0x28));
}

QColor accentText(const QWidget* w)
{
    return semantic(w, QColor(0x56, 0xC2, 0xE4), QColor(0x00, 0x66, 0xCC));
}

// ============================================================================
// Rollen
// ============================================================================

static const char* PROP_ROLE = "bpThemeRole";

/// Stylesheet zur Rolle im aktuellen Thema
static QString qssForRole(const QWidget* w, const QString& role)
{
    if (role == QLatin1String(Role::Muted))
        return QStringLiteral("QLabel { color: %1; }").arg(mutedText(w).name());

    if (role == QLatin1String(Role::MutedItalic))
        return QStringLiteral("QLabel { color: %1; font-style: italic; }")
               .arg(mutedText(w).name());

    if (role == QLatin1String(Role::Accent))
        return QStringLiteral("QLabel { color: %1; font-style: italic; }")
               .arg(accentText(w).name());

    if (role == QLatin1String(Role::HintBox)) {
        const QColor bg = paletteOf(w).color(QPalette::Window);
        const QColor flaeche = bg.lightness() < 128 ? bg.lighter(125) : bg.darker(105);
        return QStringLiteral("QLabel { color: %1; background-color: %2; "
                              "font-style: italic; padding: 5px; }")
               .arg(accentText(w).name(), flaeche.name());
    }

    if (role == QLatin1String(Role::Success))
        return QStringLiteral("QLabel { color: %1; }").arg(successText(w).name());

    if (role == QLatin1String(Role::SuccessBold))
        return QStringLiteral("QLabel { color: %1; font-weight: bold; }")
               .arg(successText(w).name());

    if (role == QLatin1String(Role::Warning))
        return QStringLiteral("QLabel { color: %1; }").arg(warningText(w).name());

    if (role == QLatin1String(Role::ErrorBold))
        return QStringLiteral("QLabel { color: %1; font-weight: bold; }")
               .arg(errorText(w).name());

    return QString();
}

void setRole(QWidget* w, const char* role)
{
    if (!w) return;
    w->setProperty(PROP_ROLE, QString::fromLatin1(role));
    const QString qss = qssForRole(w, QString::fromLatin1(role));
    if (!qss.isEmpty()) w->setStyleSheet(qss);
}

// ============================================================================
// Anwenden
// ============================================================================

void apply(QWidget* topLevel)
{
    if (!topLevel) return;

    // Fusion respektiert die Palette vollstaendig und zeichnet eckig.
    // Der auf Windows 11 voreingestellte Stil taete beides nicht.
    static QStyle* fusion = nullptr;
    if (!fusion) fusion = QStyleFactory::create(QStringLiteral("Fusion"));
    if (fusion) topLevel->setStyle(fusion);

    topLevel->setPalette(buildPalette(isDark()));

    // Stil an die Kinder durchreichen und alle Rollen im neuen Thema
    // neu berechnen - so folgt auch ein bereits gebautes Fenster.
    const QList<QWidget*> kinder = topLevel->findChildren<QWidget*>();
    for (QWidget* w : kinder) {
        if (fusion) w->setStyle(fusion);

        const QVariant rolle = w->property(PROP_ROLE);
        if (rolle.isValid()) {
            const QString qss = qssForRole(w, rolle.toString());
            if (!qss.isEmpty()) w->setStyleSheet(qss);
        }
    }
}

} // namespace Theming
} // namespace BatchProcessing
