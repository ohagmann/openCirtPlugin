/**
 * @file TextAlignment.h
 * @brief Lage eines Textes nach einer Aenderung neu berechnen
 *
 * BricsCAD berechnet die Lage eines ausgerichteten Textes aus seinem
 * Ausrichtungspunkt - im Editor nach jeder Aenderung, in einer Side-Database
 * mit adjustAlignment() und noch einmal beim Schliessen des Objekts. Eine
 * von Hand gesetzte Lage (setPosition) uebersteht das Schliessen nicht.
 *
 * Bei unten ausgerichtetem Text (vertikal "Unten") liegt die Grundlinie um
 * die Unterlaenge der Schrift ueber dem Ausrichtungspunkt. Dieser Abstand
 * haengt nur von Schrift und Texthoehe ab, nicht vom Inhalt. BricsCAD fuer
 * Linux rechnet ihn bei TrueType-Schriften kleiner als BricsCAD fuer Windows
 * (Arial: 0,202 statt 0,296 der Texthoehe). Ein unter Linux geaenderter Text
 * stuende deshalb tiefer als in der Vorlage, bei Texthoehe 2,07 um 0,19 mm.
 *
 * Unter Linux wird deshalb der Ausrichtungspunkt um diesen Unterschied
 * angehoben. Der Text steht danach dort, wo er in der Vorlage steht und wo
 * ihn BricsCAD fuer Windows hinsetzt. Ein zweiter Aufruf aendert nichts mehr.
 * Unter Windows bleibt es bei adjustAlignment().
 *
 * Einbinden nach den BRX-Headern (dbents.h).
 */

#ifndef TEXTALIGNMENT_H
#define TEXTALIGNMENT_H

#include <cmath>

namespace BatchProcessing {

#ifndef _WIN32
namespace TextAlignmentDetail {

/// Richtung senkrecht zur Grundlinie in der Ebene des Textes
inline AcGeVector3d upDirection(const AcDbText* pText)
{
    AcGeVector3d n = pText->normal();
    if (n.length() < 1e-12) n = AcGeVector3d::kZAxis;
    n.normalize();

    // Achsen der Objektebene ("arbitrary axis algorithm")
    AcGeVector3d ax = (std::fabs(n.x) < 1.0 / 64.0 && std::fabs(n.y) < 1.0 / 64.0)
        ? AcGeVector3d::kYAxis.crossProduct(n)
        : AcGeVector3d::kZAxis.crossProduct(n);
    ax.normalize();
    AcGeVector3d ay = n.crossProduct(ax);
    ay.normalize();

    const double rot = pText->rotation();
    return ax * (-std::sin(rot)) + ay * std::cos(rot);
}

inline bool isBottomAligned(const AcDbText* pText)
{
    if (pText->verticalMode() != AcDb::kTextBottom) return false;
    const AcDb::TextHorzMode h = pText->horizontalMode();
    return h == AcDb::kTextLeft || h == AcDb::kTextCenter || h == AcDb::kTextRight;
}

/// Groesster Unterschied der Unterlaenge, der als Unterschied der
/// Schriftberechnung gilt (Anteil der Texthoehe). Arial: 0,094.
const double kMaxDescentDifference = 0.2;

} // namespace TextAlignmentDetail
#endif

/// adjustAlignment() mit gleicher Textlage unter Windows und Linux
inline void adjustTextAlignment(AcDbText* pText, AcDbDatabase* pDb)
{
#ifdef _WIN32
    pText->adjustAlignment(pDb);
#else
    if (!TextAlignmentDetail::isBottomAligned(pText)) {
        pText->adjustAlignment(pDb);
        return;
    }

    const AcGeVector3d up = TextAlignmentDetail::upDirection(pText);
    const double before = (pText->position() - pText->alignmentPoint()).dotProduct(up);

    pText->adjustAlignment(pDb);

    const double after = (pText->position() - pText->alignmentPoint()).dotProduct(up);
    const double delta = before - after;
    const double height = pText->height();
    // Nur ausgleichen, was ein Unterschied der Schriftberechnung sein kann.
    // Ein Text, dessen Lage noch nie berechnet wurde, behaelt die neue.
    if (before > 1e-9 && std::fabs(delta) > 1e-9
        && std::fabs(delta) <= height * TextAlignmentDetail::kMaxDescentDifference) {
        pText->setAlignmentPoint(pText->alignmentPoint() + up * delta);
        pText->adjustAlignment(pDb);
    }
#endif
}

} // namespace BatchProcessing

#endif // TEXTALIGNMENT_H
