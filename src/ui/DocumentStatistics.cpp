#include "DocumentStatistics.h"

#include "Settings.h"
#include "markdown.h"

#include <QActionGroup>
#include <QEvent>
#include <QLocale>
#include <QMenu>
#include <QThread>

namespace typist::ui {
namespace {

template <typename Visitor>
void forEachCodePoint(QStringView text, Visitor visit) {
  for (qsizetype i = 0; i < text.size(); ++i) {
    const auto first = text.at(i);
    if (first.isHighSurrogate() && i + 1 < text.size() && text.at(i + 1).isLowSurrogate()) {
      visit(QChar::surrogateToUcs4(first, text.at(++i)));
    } else {
      visit(char32_t(first.unicode()));
    }
  }
}

qsizetype countWords(QStringView text) {
  qsizetype words = 0;
  bool inWord = false;
  forEachCodePoint(text, [&](char32_t ch) {
    const auto script = QChar::script(ch);
    if (script == QChar::Script_Han || script == QChar::Script_Hiragana ||
        script == QChar::Script_Katakana || script == QChar::Script_Hangul) {
      if (QChar::isLetterOrNumber(ch)) ++words;
      inWord = false;
    } else if (QChar::isLetterOrNumber(ch)) {
      if (!inWord) ++words;
      inWord = true;
    } else if (!QChar::isMark(ch) && ch != U'\'' && ch != U'’' && ch != U'-') {
      inWord = false;
    }
  });
  return words;
}

qsizetype countBlockWords(const core::Block& block) {
  qsizetype words = 0;
  if (!block.runs.empty()) {
    QString text;
    for (const auto& run : block.runs) {
      if (run.math || run.footnote || run.image ||
          (run.html && QString::fromStdString(run.text).startsWith(QLatin1String("<br"),
                                                                   Qt::CaseInsensitive))) {
        text += ' ';
      } else if (!run.html) {
        text += QString::fromStdString(run.text);
      }
    }
    words = countWords(text);
  } else if (block.type == core::BlockType::CodeBlock || block.type == core::BlockType::Footnote) {
    words = countWords(QString::fromStdString(block.text));
  }
  for (const auto& child : block.children) words += countBlockWords(child);
  return words;
}

std::array<qsizetype, 4> analyze(const QString& markdown) {
  qsizetype words = 0, characters = 0, visible = 0, lines = 0;
  if (!markdown.isEmpty()) {
    lines = markdown.count('\n') + (markdown.endsWith('\n') ? 0 : 1);
    forEachCodePoint(markdown, [&](char32_t ch) {
      ++characters;
      if (!QChar::isSpace(ch)) ++visible;
    });
    const auto parsed = core::parseMarkdown(markdown.toUtf8().toStdString());
    for (const auto& block : parsed.blocks) words += countBlockWords(block);
  }
  return {words, characters, visible, lines};
}

}  // namespace

class StatisticsJob : public QThread {
 public:
  StatisticsJob(StatisticsInput input, quint64 revision)
      : input(std::move(input)), revision(revision) {}

  const StatisticsInput input;
  const quint64 revision;
  std::array<qsizetype, 4> counts{};

 protected:
  void run() override { counts = analyze(input.markdown); }
};

DocumentStatisticsButton::DocumentStatisticsButton(std::function<StatisticsInput()> snapshot,
                                                   QWidget* parent)
    : QToolButton(parent), m_snapshot(std::move(snapshot)) {
  setObjectName(QStringLiteral("documentStatistics"));
  setFocusPolicy(Qt::TabFocus);
  setPopupMode(QToolButton::InstantPopup);
  auto* popup = new QMenu(this);
  popup->setObjectName(QStringLiteral("documentStatisticsMenu"));
  setMenu(popup);
  m_scope = popup->addAction(QString());
  m_scope->setObjectName(QStringLiteral("statisticsScope"));
  m_scope->setEnabled(false);
  auto* units = new QActionGroup(popup);
  const std::array names{"statisticsWords", "statisticsCharacters", "statisticsNonSpace",
                         "statisticsLines"};
  for (int i = 0; i < int(m_units.size()); ++i) {
    auto* action = popup->addAction(QString());
    action->setObjectName(QString::fromLatin1(names[i]));
    action->setCheckable(true);
    units->addAction(action);
    m_units[i] = action;
    connect(action, &QAction::triggered, this, [this, i] {
      platform::setStatisticsUnit(platform::StatisticsUnit(i));
      refreshPreferences();
    });
  }
  popup->addSeparator();
  m_readingTime = popup->addAction(QString());
  m_readingTime->setObjectName(QStringLiteral("statisticsReadingTime"));
  m_readingTime->setEnabled(false);

  m_debounce.setSingleShot(true);
  m_debounce.setInterval(180);
  connect(&m_debounce, &QTimer::timeout, this, &DocumentStatisticsButton::startAnalysis);
  connect(popup, &QMenu::aboutToShow, this, [this] {
    refreshPreferences();
    m_debounce.stop();
    startAnalysis();
  });
  refreshPreferences();
}

DocumentStatisticsButton::~DocumentStatisticsButton() {
  if (!m_job) return;
  m_job->wait();
  delete m_job;
}

void DocumentStatisticsButton::schedule() {
  ++m_revision;
  m_debounce.start();
}

void DocumentStatisticsButton::startAnalysis() {
  if (m_job || m_revision == m_presentedRevision) return;
  m_job = new StatisticsJob(m_snapshot(), m_revision);
  connect(m_job, &QThread::finished, this, [this] {
    auto* finished = m_job;
    m_job = nullptr;
    if (finished->revision == m_revision) {
      m_counts = finished->counts;
      m_selection = finished->input.selection;
      m_presentedRevision = finished->revision;
      updateLabels();
    } else if (!m_debounce.isActive()) {
      startAnalysis();
    }
    finished->deleteLater();
  });
  m_job->start();
}

void DocumentStatisticsButton::refreshPreferences() {
  m_unit = int(platform::statisticsUnit());
  m_speed = platform::readingSpeed();
  updateLabels();
}

void DocumentStatisticsButton::changeEvent(QEvent* event) {
  QToolButton::changeEvent(event);
  if (event->type() == QEvent::LanguageChange) updateLabels();
}

void DocumentStatisticsButton::updateLabels() {
  const QLocale locale;
  const std::array details{tr("Words: %1"), tr("Characters: %1"),
                           tr("Characters without spaces: %1"), tr("Lines: %1")};
  m_scope->setText(m_selection ? tr("Selection Statistics") : tr("Document Statistics"));
  for (int i = 0; i < int(m_units.size()); ++i) {
    m_units[i]->setText(details[i].arg(locale.toString(m_counts[i])));
    m_units[i]->setChecked(i == m_unit);
  }
  m_readingTime->setText(
      tr("Reading time: %1 min").arg(locale.toString((m_counts.front() + m_speed - 1) / m_speed)));
  const auto label = m_units[m_unit]->text();
  setText(m_selection ? tr("Selection · %1").arg(label) : label);
  setToolTip(m_scope->text());
  setAccessibleName(m_scope->text() + QStringLiteral(" · ") + text());
}

}  // namespace typist::ui
