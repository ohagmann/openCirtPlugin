#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file SeitennamenConfigDialog.cpp
 * @brief "Seitennamen konfigurieren" - Implementierung
 */
#include "SeitennamenConfigDialog.h"
#include "Theming.h"

#include <QComboBox>
#include <QDialogButtonBox>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QLabel>
#include <QLineEdit>
#include <QMessageBox>
#include <QPushButton>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>

namespace BatchProcessing {

namespace {
const int ColArt  = 0;
const int ColWert = 1;
const char* const ArtText     = "Text";
const char* const ArtAttribut = "Attribut";
}

SeitennamenConfigDialog::SeitennamenConfigDialog(const QString& dateiPfad, QWidget* parent)
    : QDialog(parent), m_dateiPfad(dateiPfad)
{
    setWindowTitle("Seitennamen konfigurieren");
    resize(700, 640);

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Der Titel jedes Lesezeichens besteht aus der <b>festen 4-stelligen Nummer</b> "
        "und dem, was hier von links nach rechts steht - jeder Eintrag ist eine "
        "eigene Zelle in Zeile 1 der Datei. <b>Text</b> wird woertlich uebernommen "
        "(in der Datei steht er in Anfuehrungszeichen, Leerzeichen bleiben). "
        "<b>Attribut</b> holt den Wert aus dem Plankopf der jeweiligen Zeichnung "
        "(in der Datei steht der Name ohne Kennzeichnung); fehlt der Wert, faellt "
        "das Segment samt dem Text davor weg, so bleibt nur ein Trenner stehen - nur fuer "
        "ZEICHNUNGSNUMMER steht dann wie bisher der Dateiname.",
        this);
    intro->setTextFormat(Qt::RichText);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Welche Datei gelesen wurde und was dabei geschah - sonst sieht man dem
    // Dialog nicht an, ob er die Seitennamen.csv des Projekts zeigt
    m_fileLabel = new QLabel(this);
    m_fileLabel->setTextFormat(Qt::PlainText);
    m_fileLabel->setWordWrap(true);
    m_fileLabel->setTextInteractionFlags(Qt::TextSelectableByMouse);
    Theming::setRole(m_fileLabel, Theming::Role::Accent);
    layout->addWidget(m_fileLabel);

    m_table = new QTableWidget(0, 2, this);
    m_table->setHorizontalHeaderLabels({"Art", "Wert"});
    m_table->horizontalHeader()->setSectionResizeMode(ColArt, QHeaderView::ResizeToContents);
    m_table->horizontalHeader()->setSectionResizeMode(ColWert, QHeaderView::Stretch);
    m_table->setSelectionBehavior(QAbstractItemView::SelectRows);
    m_table->setSelectionMode(QAbstractItemView::SingleSelection);
    layout->addWidget(m_table, 1);

    auto* buttons = new QHBoxLayout();
    auto mk = [&](const char* text, const char* tip, void (SeitennamenConfigDialog::*slot)()) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        buttons->addWidget(b);
    };
    mk("+ Attribut", "Neue Zeile: Wert aus einem Plankopf-Attribut",
       &SeitennamenConfigDialog::addAttribut);
    mk("+ Text",     "Neue Zeile: fester Text, z. B. das Trennzeichen \"-\"",
       &SeitennamenConfigDialog::addText);
    mk("Entfernen",  "Markierte Zeile entfernen",  &SeitennamenConfigDialog::removeRow);
    mk("vor",        "Markierte Zeile eine Stelle nach vorn schieben",
       &SeitennamenConfigDialog::moveVor);
    mk("zurück",     "Markierte Zeile eine Stelle zurueck schieben",
       &SeitennamenConfigDialog::moveZurueck);
    buttons->addStretch();
    layout->addLayout(buttons);

    layout->addWidget(new QLabel(
        "Vorschau - so steht es in Zeile 1 der Datei (ohne die feste Nummer davor):", this));
    m_preview = new QLabel(this);
    m_preview->setTextFormat(Qt::PlainText);   // "..." ist Text, kein HTML
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setWordWrap(true);
    Theming::setRole(m_preview, Theming::Role::HintBox);
    layout->addWidget(m_preview);

    auto* help = new QLabel(
        "<b>Beispiele</b>"
        "<table cellspacing='0' cellpadding='3'>"
        "<tr><td>Text</td><td><tt>-</tt></td>"
        "<td>Trennzeichen zwischen der Nummer und dem Titel</td></tr>"
        "<tr><td>Attribut</td><td><tt>ZEICHNUNGSNUMMER</tt></td>"
        "<td>Wert aus dem Plankopf - Standard, ergibt z. B. 0001-Testprojekt-BSP-…</td></tr>"
        "<tr><td>Attribut</td><td><tt>ORTSKENNZEICHEN</tt></td>"
        "<td>Wert aus dem Plankopf, ebenso ASP, GEWERK, ANLAGE, LPH, BEMERKUNG, "
        "KOSTENGRUPPE, SCHALTSCHRANK und FREITEXT_01 bis FREITEXT_05</td></tr>"
        "</table>", this);
    help->setTextFormat(Qt::RichText);
    help->setWordWrap(true);
    Theming::setRole(help, Theming::Role::Muted);
    layout->addWidget(help);

    auto* box = new QDialogButtonBox(this);
    QPushButton* speichern = box->addButton("Speichern", QDialogButtonBox::AcceptRole);
    QPushButton* abbrechen = box->addButton("Abbrechen", QDialogButtonBox::RejectRole);
    connect(speichern, &QPushButton::clicked, this, &SeitennamenConfigDialog::save);
    connect(abbrechen, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(box);

    // Die Seitennamen.csv des Projekts lesen. Fehlt sie, wird sie mit dem
    // ueblichen Aufbau angelegt und das gemeldet; ist sie da, bleibt sie bis
    // zum Speichern unangetastet.
    bool gefunden = false;
    QVector<OcSeitennamenSegment> segs = OcEngine::parseSeitennamenCsv(m_dateiPfad, &gefunden);
    bool melden = false;
    if (!gefunden) {
        segs = defaultSegments();
        QString schreibFehler;
        if (OcEngine::writeSeitennamenCsv(m_dateiPfad, segs, &schreibFehler)) {
            m_status = QString("Seitennamen.csv fehlte und wurde mit dem ueblichen Aufbau "
                               "angelegt:\n%1").arg(m_dateiPfad);
        } else {
            m_status = QString("Seitennamen.csv fehlt und konnte nicht angelegt werden (%1):\n%2")
                           .arg(schreibFehler, m_dateiPfad);
        }
        melden = true;
    } else if (segs.isEmpty()) {
        m_status = QString("Seitennamen.csv ist vorhanden, enthaelt aber kein Segment - "
                           "Vorbelegung mit dem ueblichen Aufbau; \"Speichern\" ueberschreibt "
                           "die Datei:\n%1").arg(m_dateiPfad);
        segs = defaultSegments();
        melden = true;
    } else {
        m_status = QString("Seitennamen.csv gelesen, %1 Segmente:\n%2")
                       .arg(segs.size()).arg(m_dateiPfad);
        const QStringList einfach = OcEngine::seitennamenEinfachZitiert(segs);
        if (!einfach.isEmpty()) {
            m_status += QString("\n\nAchtung: %1 stand in einfachen Anfuehrungszeichen und gilt "
                                "als Text, nicht als Attribut (so speichert Calc mit \"Alle "
                                "Textzellen in Anfuehrungszeichen\"). Ist ein Attribut gemeint, "
                                "die Art auf Attribut stellen und speichern.")
                            .arg(einfach.join(QStringLiteral(", ")));
            melden = true;
        }
    }
    m_fileLabel->setText(m_status);
    if (melden) {
        // Erst wenn der Dialog steht, damit die Meldung ueber ihm erscheint
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::information(this, "Seitennamen konfigurieren", m_status);
        });
    }
    for (const OcSeitennamenSegment& s : segs) addRow(s.type, s.value);
    m_table->clearSelection();
    updatePreview();

    Theming::apply(this);
}

QVector<OcSeitennamenSegment> SeitennamenConfigDialog::defaultSegments()
{
    QVector<OcSeitennamenSegment> segs;
    auto add = [&segs](OcSeitennamenSegment::Type type, const char* value) {
        OcSeitennamenSegment s;
        s.type = type;
        s.value = QString::fromLatin1(value);
        segs.append(s);
    };
    add(OcSeitennamenSegment::Text, "-");
    add(OcSeitennamenSegment::Attr, "ZEICHNUNGSNUMMER");
    return segs;
}

// ---------------------------------------------------------------------------
// Zeilen
// ---------------------------------------------------------------------------

void SeitennamenConfigDialog::addRow(OcSeitennamenSegment::Type type, const QString& value,
                                     int at)
{
    const int row = (at < 0 || at > m_table->rowCount()) ? m_table->rowCount() : at;
    m_table->insertRow(row);

    auto* art = new QComboBox(m_table);
    art->addItem(QString::fromLatin1(ArtText));
    art->addItem(QString::fromLatin1(ArtAttribut));
    art->setCurrentIndex(type == OcSeitennamenSegment::Text ? 0 : 1);
    connect(art, &QComboBox::currentIndexChanged, this, &SeitennamenConfigDialog::updatePreview);
    m_table->setCellWidget(row, ColArt, art);

    auto* wert = new QLineEdit(value, m_table);
    wert->setPlaceholderText(type == OcSeitennamenSegment::Text ? "Text" : "Attributname");
    connect(wert, &QLineEdit::textChanged, this, &SeitennamenConfigDialog::updatePreview);
    m_table->setCellWidget(row, ColWert, wert);

    m_table->selectRow(row);
    m_table->setCurrentCell(row, ColWert);
}

void SeitennamenConfigDialog::readRow(int row, OcSeitennamenSegment& seg) const
{
    auto* art  = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColArt));
    auto* wert = qobject_cast<QLineEdit*>(m_table->cellWidget(row, ColWert));
    seg.value = wert ? wert->text() : QString();   // nicht trimmen: Leerzeichen sind Teil
    seg.type = (art && art->currentIndex() == 1) ? OcSeitennamenSegment::Attr
                                                  : OcSeitennamenSegment::Text;
    // Tags gross speichern (gelesen wird tolerant); Text bleibt wie getippt
    if (seg.type == OcSeitennamenSegment::Attr) seg.value = seg.value.toUpper();
}

void SeitennamenConfigDialog::writeRow(int row, const OcSeitennamenSegment& seg)
{
    auto* art  = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColArt));
    auto* wert = qobject_cast<QLineEdit*>(m_table->cellWidget(row, ColWert));
    if (art)  art->setCurrentIndex(seg.type == OcSeitennamenSegment::Text ? 0 : 1);
    if (wert) wert->setText(seg.value);
}

QVector<OcSeitennamenSegment> SeitennamenConfigDialog::collect() const
{
    QVector<OcSeitennamenSegment> segs;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        OcSeitennamenSegment s;
        readRow(r, s);
        segs.append(s);
    }
    return segs;
}

void SeitennamenConfigDialog::addAttribut()
{
    const int cur = m_table->currentRow();
    addRow(OcSeitennamenSegment::Attr, QString(), cur < 0 ? -1 : cur + 1);
}

void SeitennamenConfigDialog::addText()
{
    const int cur = m_table->currentRow();
    addRow(OcSeitennamenSegment::Text, QString(), cur < 0 ? -1 : cur + 1);
}

void SeitennamenConfigDialog::removeRow()
{
    const int cur = m_table->currentRow();
    if (cur < 0) return;
    m_table->removeRow(cur);
    if (m_table->rowCount() > 0) {
        const int sel = qMin(cur, m_table->rowCount() - 1);
        m_table->selectRow(sel);
        m_table->setCurrentCell(sel, ColWert);
    }
    updatePreview();
}

void SeitennamenConfigDialog::moveVor()
{
    const int cur = m_table->currentRow();
    if (cur <= 0) return;
    OcSeitennamenSegment a, b;
    readRow(cur, a);
    readRow(cur - 1, b);
    writeRow(cur - 1, a);
    writeRow(cur, b);
    m_table->selectRow(cur - 1);
    m_table->setCurrentCell(cur - 1, ColWert);
    updatePreview();
}

void SeitennamenConfigDialog::moveZurueck()
{
    const int cur = m_table->currentRow();
    if (cur < 0 || cur >= m_table->rowCount() - 1) return;
    OcSeitennamenSegment a, b;
    readRow(cur, a);
    readRow(cur + 1, b);
    writeRow(cur + 1, a);
    writeRow(cur, b);
    m_table->selectRow(cur + 1);
    m_table->setCurrentCell(cur + 1, ColWert);
    updatePreview();
}

// ---------------------------------------------------------------------------
// Vorschau und Speichern
// ---------------------------------------------------------------------------

void SeitennamenConfigDialog::updatePreview()
{
    const QString s = OcEngine::seitennamenZeile(collect());
    m_preview->setText("Nummer + " + (s.isEmpty() ? QStringLiteral("(keine Segmente)") : s));
}

void SeitennamenConfigDialog::save()
{
    const QVector<OcSeitennamenSegment> segs = collect();
    QStringList probleme;
    if (segs.isEmpty()) probleme << QStringLiteral("Keine Segmente.");
    for (int i = 0; i < segs.size(); ++i) {
        const OcSeitennamenSegment& s = segs.at(i);
        const QString zeile = QString("Zeile %1: ").arg(i + 1);
        if (s.value.isEmpty()) {
            probleme << zeile + "kein Wert.";
        } else if (s.type == OcSeitennamenSegment::Text && s.value.contains(QLatin1Char('"'))) {
            probleme << zeile + "Text darf kein Anfuehrungszeichen enthalten.";
        } else if (s.type == OcSeitennamenSegment::Attr &&
                   (s.value.contains(QLatin1Char(' ')) || s.value.contains(QLatin1Char(';')) ||
                    s.value.contains(QLatin1Char('"')))) {
            probleme << zeile + "ein Attributname darf keine Leerzeichen, Semikolons oder "
                                "Anfuehrungszeichen enthalten.";
        }
    }
    if (!probleme.isEmpty()) {
        QMessageBox::warning(this, "Seitennamen konfigurieren",
                             "Bitte korrigieren:\n\n" + probleme.join("\n"));
        return;
    }

    QString error;
    if (!OcEngine::writeSeitennamenCsv(m_dateiPfad, segs, &error)) {
        QMessageBox::critical(this, "Seitennamen konfigurieren",
                              "Die Seitennamen.csv konnte nicht geschrieben werden:\n\n" + error);
        return;
    }
    m_saved = segs;
    accept();
}

} // namespace BatchProcessing
