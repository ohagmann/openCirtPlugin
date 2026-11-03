/**
 * @file Leistungsliste.h
 * @brief Anlagenleistungsliste je ASP aus den Leistungsstempeln
 *
 * Die Eingaben stehen in den Stempeln: sie kommen aus der Erstellliste und
 * werden beim Projektaufbau in die Stempelzeilen geschrieben. Jede
 * Stempelzeile wird ueber ihre Bezeichnung (OC_TEXT_Zn_S1) erkannt. Anzahl,
 * Pzu-Einzeln, G-Faktor, Art d. Antr. und Spannung werden uebernommen,
 * gerechnet werden:
 *   Pzu-Ges.        = Anzahl * Pzu-Einzeln * Gleichzeitigkeit
 *   Strom           = Strom (Stempel) * Anzahl * Gleichzeitigkeit,
 *                     400 V auf L1, L2 und L3, 230 V reihum auf eine Phase
 *   AV bzw. SV      = Pzu-Ges. in der Spalte der Netzart aus dem Stempel
 *   Summe, Einspeisung je ASP.
 *
 * Reine Qt-Logik ohne Zeichnung; Lesen der Stempel und Aufbau der Blaetter
 * stehen in OcDrawing (OpenCirtEngine).
 */

#ifndef LEISTUNGSLISTE_H
#define LEISTUNGSLISTE_H

#include "OpenCirtEngine.h"

#include <QString>
#include <QStringList>
#include <QVector>

namespace BatchProcessing {
namespace OcLl {

/// Bezeichnung einer Stempelzeile vergleichbar machen: ohne Einheit in [],
/// ohne ":" oder "." am Ende, Kleinschreibung, einfache Leerzeichen.
/// "Pzu-Ges. [kW]" -> "pzu-ges", "Anzahl [-]" -> "anzahl"
QString normLabel(const QString& label);

/// Listenspalte (OC_LL_...) zu einer Stempelzeile, leer wenn keine passt
QString columnForLabel(const QString& label);

/// Bezeichnungen, die je Spalte erkannt werden (fuer Doku und Protokoll)
QString aliasText();

/// Zeile der Liste aus einem Stempel. Bereich und Standort kommen aus dem
/// Plankopf des Blattes. phase zaehlt die 230-V-Verbraucher des ASP und legt
/// fest, auf welche Phase der naechste kommt (0 = L1, 1 = L2, 2 = L3, ...).
/// Fehlende oder unklare Eingaben stehen danach in hinweise. Die Pos.
/// vergibt paginate.
OcPairs zeile(const OcLlStamp& stamp, const QString& bereich,
              const QString& standort, int& phase, QStringList* hinweise = nullptr);

/// Fuehrende Zahl eines Textes ("5,9 A" -> 5.9). ok = false ohne Zahl.
double parseNumber(const QString& text, bool* ok = nullptr);

/// Zahl mit zwei Nachkommastellen und Komma
QString formatNumber(double value);

/// Summen der Strom- und Leistungsspalten
struct Summen {
    double l1 = 0, l2 = 0, l3 = 0, av = 0, sv = 0;
    bool hasL1 = false, hasL2 = false, hasL3 = false, hasAv = false, hasSv = false;

    void add(const OcPairs& row);
    double maxPhase() const;
};

/// Einspeisungen.csv: erste Spalte = Groesse in A, alles Weitere wird
/// ignoriert; Zeilen ohne Zahl vorne (Kopfzeile) ebenso. Aufsteigend.
QVector<double> readEinspeisungen(const QString& path, bool* found = nullptr);

/// Kleinste Einspeisung, die groesser ist als die groesste Phasensumme.
/// Leer, wenn es keinen Strom gibt oder keine Groesse reicht (tooLarge).
QString einspeisung(double maxPhase, const QVector<double>& sizes, bool* tooLarge = nullptr);

/// Attribute der Summenzeile
OcPairs summeZeile(const Summen& s, const QString& einspeisungText);

/// Stempel eines Quellblatts als Zeilengruppe
using Gruppe = QVector<OcPairs>;

/// Gruppen auf Blaetter verteilen: zwischen zwei Gruppen eine Leerzeile,
/// nach der letzten eine Leerzeile und die Summe. Eine Gruppe beginnt auf
/// dem naechsten Blatt, wenn sie dort ganz Platz hat, hier aber nicht; nur
/// laengere Gruppen werden geteilt. Jedes Blatt traegt ASP-Band und Kopf.
/// Die Pos. (OC_LL_POS) laeuft ab 1 ueber alle Zeilen der Blaetter durch,
/// Leerzeilen eingeschlossen.
QVector<OcLlPage> paginate(const OcPairs& aspBand, const QVector<Gruppe>& gruppen,
                           int rowsPerPage, int summeRows, const OcPairs& summe);

} // namespace OcLl
} // namespace BatchProcessing

#endif // LEISTUNGSLISTE_H
