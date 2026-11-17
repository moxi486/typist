#include "MermaidInternal.h"

#include <QDate>
#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QSet>
#include <QTime>
#include <QtMath>

namespace typist::render::mermaid_detail {
namespace {
constexpr qint64 day = 24 * 60;
constexpr qint64 maxSpan = 3660 * day;
constexpr int maxTasks = 24;

struct Task {
  QString name, section, startToken, endToken;
  bool active = false, done = false, critical = false, milestone = false;
  bool resolving = false;
  qint64 start = 0, end = 0, drawEnd = 0;
};

struct Schedule {
  QString title;
  QList<Task> tasks;
  QHash<QString, int> ids;
  QSet<qint64> datesOff;
  bool weekendsOff = false;

  bool isDayOff(qint64 date) const {
    return datesOff.contains(date) ||
           (weekendsOff && QDate::fromJulianDay(date).dayOfWeek() >= Qt::Saturday);
  }
};

qint64 dateValue(const QString& text) {
  const auto date = QDate::fromString(text, Qt::ISODate);
  return date.isValid() && date.toString(Qt::ISODate) == text ? date.toJulianDay() * day : 0;
}

bool parseTask(Schedule& schedule, const QString& line, const QString& section) {
  const auto colon = line.indexOf(':');
  if (colon <= 0 || schedule.tasks.size() >= maxTasks) return false;
  Task task;
  task.name = line.left(colon).trimmed();
  task.section = section;
  auto fields = line.mid(colon + 1).split(',');
  for (auto& field : fields) field = field.trimmed();
  if (task.name.isEmpty() || fields.contains(QString())) return false;
  while (!fields.isEmpty()) {
    const auto tag = fields.first();
    if (tag == "active")
      task.active = true;
    else if (tag == "done")
      task.done = true;
    else if (tag == "crit")
      task.critical = true;
    else if (tag == "milestone")
      task.milestone = true;
    else
      break;
    fields.removeFirst();
  }
  if (fields.isEmpty() || fields.size() > 3) return false;
  if (fields.size() == 3) {
    static const QRegularExpression idPattern(R"(^[\p{L}\p{N}_][\p{L}\p{N}_.-]*$)");
    const auto id = fields.takeFirst();
    if (!idPattern.match(id).hasMatch() || schedule.ids.contains(id)) return false;
    schedule.ids.insert(id, int(schedule.tasks.size()));
  }
  task.endToken = fields.takeLast();
  if (!fields.isEmpty()) task.startToken = fields.first();
  schedule.tasks.append(task);
  return true;
}

bool parseSchedule(Schedule& schedule, const QStringList& lines) {
  static const QRegularExpression directive(R"(^(title|section|dateFormat|excludes)\s+(.+)$)",
                                            QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression separators(R"([,\s]+)");
  QString section;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto& input = lines.at(i);
    if (input.trimmed().startsWith("%%{")) return false;
    const auto comment = input.indexOf("%%");
    const auto line = (comment < 0 ? input : input.left(comment)).trimmed();
    if (line.isEmpty()) continue;
    const auto match = directive.match(line);
    if (!match.hasMatch()) {
      if (!parseTask(schedule, line, section)) return false;
      continue;
    }
    const auto keyword = match.captured(1).toLower();
    const auto value = match.captured(2).trimmed();
    if (keyword == "title") {
      schedule.title = value;
    } else if (keyword == "section") {
      section = value;
    } else if (keyword == "dateformat") {
      if (value != "YYYY-MM-DD") return false;
    } else {
      const auto entries = value.split(separators, Qt::SkipEmptyParts);
      if (entries.isEmpty()) return false;
      for (const auto& entry : entries) {
        if (entry.compare(QStringLiteral("weekends"), Qt::CaseInsensitive) == 0) {
          schedule.weekendsOff = true;
        } else {
          const qint64 date = dateValue(entry);
          if (!date) return false;
          schedule.datesOff.insert(date / day);
        }
      }
    }
  }
  return !schedule.tasks.isEmpty();
}

bool resolveDuration(const Schedule& schedule, Task& task) {
  static const QRegularExpression duration(R"(^([0-9]+(?:\.[0-9]+)?)\s*([mhdw])$)");
  const auto field = duration.match(task.endToken);
  if (!field.hasMatch()) return false;
  const auto unit = field.captured(2);
  const qint64 factor = unit == "w" ? 7 * day : unit == "d" ? day : unit == "h" ? 60 : 1;
  const qreal minutes = field.captured(1).toDouble() * factor;
  if (!(minutes <= maxSpan)) return false;
  task.end = task.drawEnd = task.start + qRound64(minutes);
  if (schedule.weekendsOff || !schedule.datesOff.isEmpty()) {
    // Mermaid advances dependencies past trailing nonworking days, while the bar stops
    // at the start of that trailing break.
    bool previousDayOff = false;
    for (qint64 next = task.start + day; next <= task.end; next += day) {
      if (!previousDayOff) task.drawEnd = task.end;
      previousDayOff = schedule.isDayOff(next / day);
      if (previousDayOff) task.end += day;
      if (task.end - task.start > maxSpan) return false;
    }
  }
  return true;
}

bool resolve(Schedule& schedule, int index) {
  static const QRegularExpression after(R"(^after\s+(.+)$)");
  static const QRegularExpression whitespace(R"(\s+)");
  auto& task = schedule.tasks[index];
  if (task.end) return true;
  if (task.resolving) return false;
  task.resolving = true;
  if (task.startToken.isEmpty()) {
    if (!index || !resolve(schedule, index - 1)) return false;
    task.start = schedule.tasks.at(index - 1).end;
  } else if (const auto match = after.match(task.startToken); match.hasMatch()) {
    const auto ids = match.captured(1).split(whitespace, Qt::SkipEmptyParts);
    for (const auto& id : ids) {
      const int dependency = schedule.ids.value(id, -1);
      if (dependency < 0 || !resolve(schedule, dependency)) return false;
      task.start = qMax(task.start, schedule.tasks.at(dependency).end);
    }
  } else {
    task.start = dateValue(task.startToken);
  }
  if (!task.start) return false;
  task.end = task.drawEnd = dateValue(task.endToken);
  if (!task.end && !resolveDuration(schedule, task)) return false;
  return task.end >= task.start && task.end / day <= QDate(9999, 12, 31).toJulianDay();
}

qint64 tickStep(qint64 span) {
  for (const qint64 step : {15LL, 30LL, 60LL, 120LL, 180LL, 360LL, 720LL, day, 2 * day, 3 * day,
                            7 * day, 14 * day, 28 * day, 56 * day}) {
    if (span <= step * 6) return step;
  }
  return ((span + 42 * day - 1) / (42 * day)) * 7 * day;
}

QImage drawSchedule(const Schedule& schedule, qint64 first, qint64 last, bool dark) {
  constexpr int width = 740, margin = 20, rowHeight = 30, sectionHeight = 25;
  const QColor ink(dark ? "#e8edf5" : "#1f2b40");
  const QColor muted(dark ? "#abb9cd" : "#4b5870");
  const QColor grid(dark ? "#4a5362" : "#dfe5ee");
  const QColor panel(dark ? "#303843" : "#f6f8fc");
  const QColor sectionFill(dark ? "#3d495b" : "#e7eef9");
  const QColor dayOff(dark ? "#514649" : "#f1e7e9");
  const QColor normal(dark ? "#7eaef4" : "#4576bc");
  const QColor active(dark ? "#6dd5dc" : "#167d86");
  const QColor done(dark ? "#8ac9a7" : "#397458");
  const QColor critical(dark ? "#ffad9c" : "#b34542");

  const auto body = diagramFont();
  auto titleFont = body;
  titleFont.setBold(true);
  titleFont.setPointSizeF(15);
  auto sectionFont = body;
  sectionFont.setBold(true);
  auto axisFont = body;
  axisFont.setPointSizeF(9);
  const QFontMetricsF metrics(body), headings(sectionFont), titles(titleFont);
  qreal labelWidth = 130;
  int bands = 0;
  QString section;
  for (const auto& task : schedule.tasks) {
    labelWidth = qMax(labelWidth, metrics.horizontalAdvance(task.name) + 24);
    if (section != task.section) {
      section = task.section;
      ++bands;
    }
  }
  const qreal plot = margin + qMin(labelWidth, 226.);
  const qreal right = width - margin - 34;
  const int top = margin + (schedule.title.isEmpty() ? 0 : 39);
  const int bottom = top + int(schedule.tasks.size()) * rowHeight + bands * sectionHeight;
  const qint64 step = tickStep(last - first);
  const qint64 start = (first / qMin(step, day)) * qMin(step, day);
  const qint64 end = start + qMax(qint64(1), (last - start + step - 1) / step) * step;
  const auto x = [&](qint64 time) {
    return plot + (right - plot) * (time - start) / (end - start);
  };
  QImage image = canvas(width, bottom + 48, dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setFont(titleFont);
  painter.setPen(ink);
  painter.drawText(QRectF(margin, margin - 2, width - margin * 2, 32), Qt::AlignVCenter,
                   titles.elidedText(schedule.title, Qt::ElideRight, width - margin * 2));
  painter.fillRect(QRectF(margin, top, right - margin, bottom - top), panel);
  for (qint64 i = start / day; i * day < end; ++i) {
    if (!schedule.isDayOff(i)) continue;
    const qreal left = x(qMax(i * day, start)), edge = x(qMin((i + 1) * day, end));
    painter.fillRect(QRectF(left, top, edge - left, bottom - top), dayOff);
  }
  painter.setFont(axisFont);
  for (qint64 tick = start; tick <= end; tick += step) {
    const qreal at = x(tick);
    painter.setPen(grid);
    painter.drawLine(QPointF(at, top), QPointF(at, bottom + 3));
    const auto date = QDate::fromJulianDay(tick / day);
    auto caption = date.toString(Qt::ISODate);
    if (step < day) caption += '\n' + QTime(0, 0).addSecs(int(tick % day) * 60).toString("HH:mm");
    painter.setPen(muted);
    painter.drawText(QRectF(at - 42, bottom + 8, 84, 38), Qt::AlignHCenter, caption);
  }
  section.clear();
  int row = top;
  for (const auto& task : schedule.tasks) {
    if (section != task.section) {
      section = task.section;
      painter.fillRect(QRectF(margin, row, right - margin, sectionHeight), sectionFill);
      painter.setFont(sectionFont);
      painter.setPen(ink);
      painter.drawText(QRectF(margin + 9, row, right - margin - 18, sectionHeight),
                       Qt::AlignVCenter,
                       headings.elidedText(section, Qt::ElideRight, right - margin - 18));
      row += sectionHeight;
    }
    painter.setPen(grid);
    painter.drawLine(QPointF(margin, row + rowHeight), QPointF(right, row + rowHeight));
    painter.setPen(ink);
    painter.setFont(body);
    painter.drawText(QRectF(margin + 9, row, plot - margin - 18, rowHeight), Qt::AlignVCenter,
                     metrics.elidedText(task.name, Qt::ElideRight, plot - margin - 18));

    const qreal mid = row + rowHeight / 2.;
    const QColor fill = task.active ? active : task.done ? done : task.critical ? critical : normal;
    painter.setPen(task.critical ? QPen(critical, 2) : QPen(Qt::NoPen));
    painter.setBrush(fill);
    if (task.milestone) {
      const qreal at = x(task.start + (task.end - task.start) / 2);
      painter.drawPolygon(QPolygonF({{at, mid - 7}, {at + 7, mid}, {at, mid + 7}, {at - 7, mid}}));
    } else {
      const qreal left = x(task.start), length = x(task.drawEnd) - left;
      painter.drawRoundedRect(QRectF(left, mid - 8, qMax(2., length), 16), 3, 3);
    }
    row += rowHeight;
  }
  return image;
}
}  // namespace

RenderResult ganttChart(const QStringList& lines, bool dark) {
  Schedule schedule;
  if (!parseSchedule(schedule, lines)) return {{}, unsupported};
  qint64 first = 0, last = 0;
  for (int i = 0; i < schedule.tasks.size(); ++i) {
    if (!resolve(schedule, i)) return {{}, unsupported};
    const auto& task = schedule.tasks.at(i);
    first = first ? qMin(first, task.start) : task.start;
    last = qMax(last, task.end);
  }
  if (last - first > maxSpan) return {{}, unsupported};
  return {drawSchedule(schedule, first, last, dark), {}};
}
}  // namespace typist::render::mermaid_detail
