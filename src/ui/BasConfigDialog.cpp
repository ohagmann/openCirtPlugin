#include "windows_fix.h"  // CRITICAL: Qt 6.8+ fix - MUST be FIRST
/**
 * @file BasConfigDialog.cpp
 * @brief "BAS konfigurieren" - Implementierung
 */
#include "BasConfigDialog.h"
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

BasConfigDialog::BasConfigDialog(const QString& basPath, QWidget* parent)
    : QDialog(parent), m_basPath(basPath)
{
    setWindowTitle("BAS konfigurieren");
    resize(700, 780);   // 13 Zeilen des ueblichen Aufbaus ohne Rollen

    auto* layout = new QVBoxLayout(this);

    auto* intro = new QLabel(
        "Der BAS wird aus den Segmenten von oben nach unten zusammengesetzt. "
        "<b>Text</b> wird woertlich uebernommen. <b>Attribut</b> holt den Wert aus dem "
        "Funktionsblock, ersatzweise aus dem Plankopf der Zeichnung. Ein Attribut, dessen "
        "Name auf <tt>_DP</tt> endet, gilt je Datenpunkt (<tt>_1</tt>, <tt>_2</tt>, ...).",
        this);
    intro->setTextFormat(Qt::RichText);
    intro->setWordWrap(true);
    layout->addWidget(intro);

    // Welche Datei gelesen wurde und was dabei geschah - sonst sieht man dem
    // Dialog nicht an, ob er die BAS.csv des Projekts oder die Vorbelegung zeigt
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
    auto mk = [&](const char* text, const char* tip, void (BasConfigDialog::*slot)()) {
        auto* b = new QPushButton(text, this);
        b->setToolTip(tip);
        connect(b, &QPushButton::clicked, this, slot);
        buttons->addWidget(b);
    };
    mk("+ Attribut",     "Neue Zeile: Wert aus einem Attribut",            &BasConfigDialog::addAttribut);
    mk("+ Text",         "Neue Zeile: fester Text, z.B. das Projektkuerzel", &BasConfigDialog::addText);
    mk("+ Trennzeichen", "Neue Zeile: fester Text \"-\"",                   &BasConfigDialog::addTrennzeichen);
    mk("Entfernen",      "Markierte Zeile entfernen",                        &BasConfigDialog::removeRow);
    mk("Nach oben",      "Markierte Zeile nach oben schieben",               &BasConfigDialog::moveUp);
    mk("Nach unten",     "Markierte Zeile nach unten schieben",              &BasConfigDialog::moveDown);
    buttons->addStretch();
    layout->addLayout(buttons);

    layout->addWidget(new QLabel("Vorschau (Attribute als <NAME>):", this));
    m_preview = new QLabel(this);
    m_preview->setTextFormat(Qt::PlainText);   // "<ASP>" darf nicht als HTML gelten
    m_preview->setTextInteractionFlags(Qt::TextSelectableByMouse);
    m_preview->setWordWrap(true);
    Theming::setRole(m_preview, Theming::Role::HintBox);
    layout->addWidget(m_preview);

    auto* help = new QLabel(
        "<b>Beispiele</b>"
        "<table cellspacing='0' cellpadding='3'>"
        "<tr><td>Text</td><td><tt>GEB1</tt></td>"
        "<td>Projektkuerzel, steht woertlich im BAS</td></tr>"
        "<tr><td>Text</td><td><tt>-</tt></td>"
        "<td>Trennzeichen zwischen zwei Segmenten</td></tr>"
        "<tr><td>Attribut</td><td><tt>ORTSKENNZEICHEN</tt></td>"
        "<td>Wert aus dem Plankopf der Zeichnung</td></tr>"
        "<tr><td>Attribut</td><td><tt>ASP</tt></td>"
        "<td>Wert aus dem Plankopf, ebenso GEWERK und ANLAGE</td></tr>"
        "<tr><td>Attribut</td><td><tt>OC_AKS</tt></td>"
        "<td>Wert aus dem Funktionsblock</td></tr>"
        "<tr><td>Attribut</td><td><tt>OC_FCODE_DP</tt></td>"
        "<td>je Datenpunkt: OC_FCODE_DP_1, OC_FCODE_DP_2, ...</td></tr>"
        "</table>", this);
    help->setTextFormat(Qt::RichText);
    help->setWordWrap(true);
    Theming::setRole(help, Theming::Role::Muted);
    layout->addWidget(help);

    auto* box = new QDialogButtonBox(this);
    QPushButton* speichern = box->addButton("Speichern", QDialogButtonBox::AcceptRole);
    QPushButton* abbrechen = box->addButton("Abbrechen", QDialogButtonBox::RejectRole);
    connect(speichern, &QPushButton::clicked, this, &BasConfigDialog::save);
    connect(abbrechen, &QPushButton::clicked, this, &QDialog::reject);
    layout->addWidget(box);

    // Die BAS.csv des Projekts lesen. Fehlt sie, wird sie mit dem ueblichen
    // Aufbau angelegt und das gemeldet; Speichern schreibt sie dann neu.
    bool found = false;
    QVector<OcBasSegment> segs = OcEngine::parseBasCsv(m_basPath, &found);
    bool melden = false;
    if (!found) {
        segs = defaultSegments();
        QString error;
        if (OcEngine::writeBasCsv(m_basPath, segs, &error)) {
            m_status = QString("BAS.csv fehlte und wurde mit dem ueblichen Aufbau angelegt:\n%1")
                       .arg(m_basPath);
        } else {
            m_status = QString("BAS.csv fehlt und konnte nicht angelegt werden (%1):\n%2")
                       .arg(error, m_basPath);
        }
        melden = true;
    } else if (segs.isEmpty()) {
        segs = defaultSegments();
        m_status = QString("BAS.csv ist vorhanden, enthaelt aber kein Segment - "
                           "Vorbelegung mit dem ueblichen Aufbau:\n%1").arg(m_basPath);
        melden = true;
    } else {
        m_status = QString("BAS.csv gelesen, %1 Segmente:\n%2").arg(segs.size()).arg(m_basPath);
    }
    m_fileLabel->setText(m_status);
    if (melden) {
        // Erst wenn der Dialog steht, damit die Meldung ueber ihm erscheint
        QTimer::singleShot(0, this, [this]() {
            QMessageBox::information(this, "BAS konfigurieren", m_status);
        });
    }
    for (const OcBasSegment& s : segs) addRow(s.type, s.value);
    m_table->clearSelection();
    updatePreview();

    Theming::apply(this);
}

QVector<OcBasSegment> BasConfigDialog::defaultSegments()
{
    QVector<OcBasSegment> segs;
    auto add = [&segs](OcBasSegment::Type type, const char* value) {
        OcBasSegment s;
        s.type = type;
        s.value = QString::fromLatin1(value);
        segs.append(s);
    };
    add(OcBasSegment::Static, "Projekt");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::Attr, "ASP");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::Attr, "GEWERK");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::Attr, "ANLAGE");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::Attr, "ORTSKENNZEICHEN");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::Attr, "OC_AKS");
    add(OcBasSegment::Static, "-");
    add(OcBasSegment::AttrDp, "OC_FCODE_DP");
    return segs;
}

// ---------------------------------------------------------------------------
// Zeilen
// ---------------------------------------------------------------------------

void BasConfigDialog::addRow(OcBasSegment::Type type, const QString& value, int at)
{
    const int row = (at < 0 || at > m_table->rowCount()) ? m_table->rowCount() : at;
    m_table->insertRow(row);

    auto* art = new QComboBox(m_table);
    art->addItem(QString::fromLatin1(ArtText));
    art->addItem(QString::fromLatin1(ArtAttribut));
    art->setCurrentIndex(type == OcBasSegment::Static ? 0 : 1);
    connect(art, &QComboBox::currentIndexChanged, this, &BasConfigDialog::updatePreview);
    m_table->setCellWidget(row, ColArt, art);

    auto* wert = new QLineEdit(value, m_table);
    wert->setPlaceholderText(type == OcBasSegment::Static ? "Text" : "Attributname");
    connect(wert, &QLineEdit::textChanged, this, &BasConfigDialog::updatePreview);
    m_table->setCellWidget(row, ColWert, wert);

    m_table->selectRow(row);
    m_table->setCurrentCell(row, ColWert);
}

void BasConfigDialog::readRow(int row, OcBasSegment& seg) const
{
    auto* art  = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColArt));
    auto* wert = qobject_cast<QLineEdit*>(m_table->cellWidget(row, ColWert));
    seg.value = wert ? wert->text().trimmed() : QString();
    if (art && art->currentIndex() == 0) {
        seg.type = OcBasSegment::Static;
    } else {
        // Dieselbe Regel wie parseBasCsv: Name auf _DP gilt je Datenpunkt
        seg.type = (seg.value.size() >= 3
                    && seg.value.right(3).compare(QLatin1String("_DP"), Qt::CaseInsensitive) == 0)
                   ? OcBasSegment::AttrDp : OcBasSegment::Attr;
    }
}

void BasConfigDialog::writeRow(int row, const OcBasSegment& seg)
{
    auto* art  = qobject_cast<QComboBox*>(m_table->cellWidget(row, ColArt));
    auto* wert = qobject_cast<QLineEdit*>(m_table->cellWidget(row, ColWert));
    if (art)  art->setCurrentIndex(seg.type == OcBasSegment::Static ? 0 : 1);
    if (wert) wert->setText(seg.value);
}

QVector<OcBasSegment> BasConfigDialog::collect() const
{
    QVector<OcBasSegment> segs;
    for (int r = 0; r < m_table->rowCount(); ++r) {
        OcBasSegment s;
        readRow(r, s);
        segs.append(s);
    }
    return segs;
}

void BasConfigDialog::addAttribut()
{
    const int cur = m_table->currentRow();
    addRow(OcBasSegment::Attr, QString(), cur < 0 ? -1 : cur + 1);
}

void BasConfigDialog::addText()
{
    const int cur = m_table->currentRow();
    addRow(OcBasSegment::Static, QString(), cur < 0 ? -1 : cur + 1);
}

void BasConfigDialog::addTrennzeichen()
{
    const int cur = m_table->currentRow();
    addRow(OcBasSegment::Static, QStringLiteral("-"), cur < 0 ? -1 : cur + 1);
}

void BasConfigDialog::removeRow()
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

void BasConfigDialog::moveUp()
{
    const int cur = m_table->currentRow();
    if (cur <= 0) return;
    OcBasSegment a, b;
    readRow(cur, a);
    readRow(cur - 1, b);
    writeRow(cur - 1, a);
    writeRow(cur, b);
    m_table->selectRow(cur - 1);
    m_table->setCurrentCell(cur - 1, ColWert);
    updatePreview();
}

void BasConfigDialog::moveDown()
{
    const int cur = m_table->currentRow();
    if (cur < 0 || cur >= m_table->rowCount() - 1) return;
    OcBasSegment a, b;
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

void BasConfigDialog::updatePreview()
{
    QString s;
    for (const OcBasSegment& seg : collect()) {
        switch (seg.type) {
        case OcBasSegment::Static:
            s += seg.value;
            break;
        case OcBasSegment::AttrDp:
            s += QLatin1Char('<') + seg.value + QStringLiteral("_n>");
            break;
        default:
            s += QLatin1Char('<') + seg.value + QLatin1Char('>');
            break;
        }
    }
    m_preview->setText(s.isEmpty() ? QStringLiteral("(keine Segmente)") : s);
}

void BasConfigDialog::save()
{
    const QVector<OcBasSegment> segs = collect();
    QStringList probleme;
    if (segs.isEmpty()) probleme << "Keine Segmente.";
    for (int i = 0; i < segs.size(); ++i) {
        const OcBasSegment& s = segs.at(i);
        const QString zeile = QString("Zeile %1: ").arg(i + 1);
        if (s.value.isEmpty()) {
            probleme << zeile + "kein Wert.";
        } else if (s.value.contains(QLatin1Char(';'))) {
            probleme << zeile + "ein Semikolon ist nicht erlaubt.";
        } else if (s.value.contains(QLatin1Char('"'))) {
            probleme << zeile + "Anfuehrungszeichen sind nicht erlaubt.";
        } else if (s.type != OcBasSegment::Static && s.value.contains(QLatin1Char(' '))) {
            probleme << zeile + "ein Attributname enthaelt kein Leerzeichen.";
        }
    }
    if (!probleme.isEmpty()) {
        QMessageBox::warning(this, "BAS konfigurieren",
                             "Bitte korrigieren:\n\n" + probleme.join("\n"));
        return;
    }

    QString error;
    if (!OcEngine::writeBasCsv(m_basPath, segs, &error)) {
        QMessageBox::critical(this, "BAS konfigurieren",
                              "Die BAS.csv konnte nicht geschrieben werden:\n\n" + error);
        return;
    }
    m_saved = segs;
    accept();
}

} // namespace BatchProcessing
