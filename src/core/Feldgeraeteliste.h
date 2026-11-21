/**
 * @file Feldgeraeteliste.h
 * @brief Feldgeraeteliste: alle Feldgeraete des Projekts, eins je Zeile, und
 *        ihre Summe je Geraeteart
 *
 * Ein Feldgeraet ist jede Blockreferenz einer Quellzeichnung mit gefuelltem
 * OC_AKS (OcDrawing::scanFeldgeraete) - mit oder ohne aktiven Datenpunkt,
 * also auch Geraete, die die GA nur kennt (z. B. eine Pumpe, die die
 * Heizung liefert). Was fuer ein Geraet es ist, sagt die Referenz
 * 01- Referenzen/Feldgeraete.csv:
 *
 *   Block:;BMK:;Funktion:;Geraeteart:;Kategorie:;Lieferung:;Darstellung in Liste:;Ausfuehrung Pflicht:
 *   OC_VORLAGE_3W_VENTIL_HZG;;;3-Wege-Regelventil mit Antrieb;Ventil;GA;ja;
 *   ;TVL;;Tauchtemperaturfuehler;Sensor;GA;ja;ja
 *   ;ZUV;WM;Reparaturschalter;Schalter;ELT;ja;
 *   ;GRF;;Anlagenbild;-;-;nein;
 *
 * - Block ohne Versionsnummer (_V4, _V_2 am Ende entfallen), BMK ohne die
 *   Nummer am Ende (PPE01 -> PPE, BSK-01 -> BSK). Gross-/Kleinschreibung egal.
 * - Zuordnung je Geraet: Block + BMK, sonst nur Block (BMK leer), sonst nur
 *   BMK (Block leer). Zeilen mit Funktion zaehlen dabei nicht.
 * - Funktion: hat das Geraet einen aktiven Datenpunkt, dessen
 *   Funktionskennung mit diesen Buchstaben beginnt (WM_01, REP01), entsteht
 *   eine weitere Zeile, z. B. der Reparaturschalter am Ventilator. Zuordnung
 *   wie oben, zusaetzlich Funktion allein.
 * - Darstellung in Liste: nein = nicht in die Liste (Anlagenbild, virtuelle
 *   Datenpunkte, Schaltschrankmeldungen ...); leer = ja.
 * - Ausfuehrung Pflicht: ja = das Geraet braucht eine Ausfuehrung aus einem
 *   Feldgeraete-Stempel (Klappendrehmoment, Tauchhuelsenlaenge ...); fehlt sie,
 *   steht in der Liste "(Ausfuehrung fehlt)" und das Protokoll nennt das
 *   Geraet. Leer = nein. Die Spalte darf fehlen.
 * - Ausfuehrung: kommt aus dem Feldgeraete-Stempel (OcDrawing::scanFeldgeraete),
 *   die Summe zaehlt je Geraeteart und Ausfuehrung.
 * - Spalten werden ueber die Kopfzeile gefunden (Doppelpunkt, Umlaute und
 *   Gross-/Kleinschreibung egal); Block und Funktion duerfen fehlen.
 *
 * Reine Qt-Logik ohne Zeichnung.
 */

#ifndef FELDGERAETELISTE_H
#define FELDGERAETELISTE_H

#include "OpenCirtEngine.h"

#include <QHash>
#include <QString>
#include <QStringList>
#include <QVector>

namespace BatchProcessing {
namespace OcFg {

/// Blockname ohne Versionsnummer, gross: OC_VORLAGE_VE_1_3W_VENTIL_V_2 ->
/// OC_VORLAGE_VE_1_3W_VENTIL
QString blockStamm(const QString& block);

/// BMK ohne Nummer am Ende, gross: PPE01 -> PPE, BSK-01 -> BSK, TVL- -> TVL
QString bmkStamm(const QString& aks);

/// Buchstaben am Anfang einer Funktionskennung, gross: WM_01 -> WM, REP01 -> REP
QString funktionStamm(const QString& fcode);

/// Eine Zeile der Referenz
struct Eintrag {
    QString block;          ///< Stamm (gross), leer = jeder Block
    QString bmk;            ///< Stamm (gross), leer = jedes BMK
    QString funktion;       ///< Buchstaben (gross), leer = das Geraet selbst
    QString geraeteart;
    QString kategorie;
    QString lieferung;
    bool inListe = true;
    bool ausfuehrungPflicht = false;  ///< Ausfuehrung aus einem Stempel noetig
    int zeile = 0;          ///< Zeile in der Datei (fuer Meldungen)
};

/// Feldgeraete.csv
class Referenz {
public:
    /// Lesen. false: error nennt den Grund (Datei fehlt, Kopfzeile ohne
    /// Geraeteart oder ohne Block und BMK). hinweise: Zeilen, die nicht
    /// zaehlen (doppelt, ohne Block und BMK und Funktion).
    bool load(const QString& path, QString* error, QStringList* hinweise = nullptr);

    int size() const { return m_eintraege.size(); }

    /// Eintrag fuer das Geraet selbst; nullptr = unbekannt
    const Eintrag* geraet(const QString& blockStamm, const QString& bmkStamm) const;

    /// Eintrag fuer einen Datenpunkt des Geraets mit dieser Funktion; nullptr = keiner
    const Eintrag* funktion(const QString& blockStamm, const QString& bmkStamm,
                            const QString& funktionStamm) const;

private:
    static QString key(const QString& block, const QString& bmk, const QString& funktion);
    QVector<Eintrag> m_eintraege;
    QHash<QString, int> m_index;    ///< key -> Index in m_eintraege (erste Zeile gilt)
};

/// Eine Zeile der Liste
struct Zeile {
    QString los, asp, gewerk, anlage, ort;
    QString bmk, bas, bezeichnung;
    QString geraeteart, ausfuehrung, kategorie, lieferung;
    QString produkt, blatt, block;
    bool unbekannt = false;         ///< kein Eintrag in der Referenz
};

/// Kopfzeile der Liste
QStringList kopf();

/// Zellen einer Zeile; pos = laufende Nummer (Breite stelle, fuehrende Nullen)
QStringList zellen(const Zeile& z, int pos, int stellen);

/// Text in der Spalte Ausfuehrung, wenn sie Pflicht ist und fehlt
QString ausfuehrungFehlt();

/// Kreuztabelle der Summe: eine Zeile je Kategorie + Geraeteart + Ausfuehrung + Lieferung,
/// eine Spalte je ASP (in der Reihenfolge der Liste), je Los eine Summe, am
/// Ende Gesamt. Leere Zelle = 0. Geraete ohne Eintrag (?) stehen zuletzt.
/// tabelle[0] ist die Kopfzeile.
QVector<QStringList> summe(const QVector<Zeile>& zeilen);

} // namespace OcFg
} // namespace BatchProcessing

#endif // FELDGERAETELISTE_H
