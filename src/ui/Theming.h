/**
 * @file Theming.h
 * @brief Oberflaeche an BricsCADs Hell-/Dunkeleinstellung angleichen
 *
 * BricsCADs eigene Oberflaeche ist MFC-basiert, es gibt also keine Qt-Palette
 * zum Erben. Massgeblich ist stattdessen die Systemvariable COLORTHEME
 * (0 = dunkel, 1 = hell). Daraus wird hier eine Qt-Palette gebaut.
 *
 * Wichtig ist dabei der Stil: der ab Qt 6.8 auf Windows 11 voreingestellte
 * "windows11"-Stil zeichnet Flaechen, Rahmen und abgerundete Ecken selbst und
 * ignoriert die Palette weitgehend - eine dunkle Palette bliebe dort wirkungslos.
 * Fusion respektiert die Palette vollstaendig und zeichnet eckig, entspricht
 * also zugleich BricsCADs Erscheinungsbild.
 *
 * Farben werden nirgends fest verdrahtet. Beschriftungen bekommen stattdessen
 * eine Rolle zugewiesen (setRole). apply() rechnet die Rollen bei jedem Aufruf
 * neu aus - deshalb folgt die Oberflaeche einem Themenwechsel auch dann, wenn
 * das Fenster bereits gebaut ist.
 */

#ifndef BP_THEMING_H
#define BP_THEMING_H

#include <QColor>
#include <QPalette>
#include <QString>

QT_BEGIN_NAMESPACE
class QWidget;
QT_END_NAMESPACE

namespace BatchProcessing {
namespace Theming {

/// Rollen fuer Beschriftungen. Die Farbe ergibt sich zur Laufzeit aus dem Thema.
namespace Role {
    constexpr const char* Muted       = "muted";        ///< Hinweis, Beschreibung
    constexpr const char* MutedItalic = "muted-italic"; ///< Statuszeile, ruhend
    constexpr const char* Accent      = "accent";       ///< Bedienhinweis
    constexpr const char* HintBox     = "hint-box";     ///< Hinweis mit Flaeche
    constexpr const char* Success     = "success";      ///< fertig, bereit
    constexpr const char* SuccessBold = "success-bold";
    constexpr const char* Warning     = "warning";      ///< Aufforderung
    constexpr const char* ErrorBold   = "error-bold";   ///< Fehler
}

/// Liest BricsCADs COLORTHEME. true = dunkles Thema.
/// Ist die Variable nicht verfuegbar, wird anhand der Anwendungspalette geraten.
bool isDark();

/// Baut die Palette zum gewuenschten Thema.
QPalette buildPalette(bool dark);

/// Weist einer Beschriftung eine Rolle zu und faerbt sie sofort passend ein.
/// Die Rolle bleibt am Widget haengen, damit apply() sie spaeter neu berechnen kann.
void setRole(QWidget* w, const char* role);

/// Setzt Fusion-Stil und Palette auf ein Fenster und faerbt alle Widgets mit
/// Rolle neu ein. Beim Oeffnen des Fensters aufrufen, damit ein Themenwechsel
/// in BricsCAD uebernommen wird.
void apply(QWidget* topLevel);

/// Gedaempfte Textfarbe passend zum Hintergrund.
QColor mutedText(const QWidget* w);

/// Semantische Farben, jeweils auf das aktuelle Thema abgestimmt.
QColor successText(const QWidget* w);   ///< gruen
QColor warningText(const QWidget* w);   ///< orange
QColor errorText(const QWidget* w);     ///< rot
QColor accentText(const QWidget* w);    ///< blau

} // namespace Theming
} // namespace BatchProcessing

#endif // BP_THEMING_H
