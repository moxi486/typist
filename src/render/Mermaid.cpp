#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QPair>
#include <QPicture>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtMath>
#include <cmath>
#include <utility>

namespace typist::render {
namespace mermaid_detail {
QFont diagramFont() {
  QFont f;
  f.setFamilies({"Arial", "Microsoft YaHei", "Noto Sans"});
  f.setPointSizeF(11);
  return f;
}
QImage canvas(int width, int height, bool dark) {
  QImage image(width * 2, height * 2, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(2);
  image.fill(dark ? QColor("#262b31") : QColor("#ffffff"));
  return image;
}
const QString unsupported = QStringLiteral(
    "This Mermaid syntax is not supported by the native renderer. Edit the source to use "
    "flowchart, sequenceDiagram, stateDiagram, classDiagram, gantt, pie, timeline, mindmap, "
    "quadrantChart, sankey, gitGraph or xychart.");
QString label(QString text) {
  text = text.trimmed();
  if (text.startsWith('"') && text.endsWith('"')) text = text.mid(1, text.size() - 2);
  text.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
  return text;
}

void arrow(QPainter& painter, QPointF a, QPointF b, bool dashed, bool head) {
  auto pen = painter.pen();
  pen.setWidthF(1.6);
  pen.setStyle(dashed ? Qt::DashLine : Qt::SolidLine);
  painter.setPen(pen);
  painter.drawLine(a, b);
  if (!head) return;
  const qreal angle = std::atan2(b.y() - a.y(), b.x() - a.x());
  const QPointF left = b - QPointF(std::cos(angle - .45) * 9, std::sin(angle - .45) * 9);
  const QPointF right = b - QPointF(std::cos(angle + .45) * 9, std::sin(angle + .45) * 9);
  painter.setBrush(pen.color());
  painter.drawPolygon(QPolygonF({b, left, right}));
}

}  // namespace mermaid_detail

namespace {
using mermaid_detail::arrow;
using mermaid_detail::canvas;
using mermaid_detail::diagramFont;
using mermaid_detail::label;
using mermaid_detail::unsupported;

struct PieSlice {
  QString legend;
  double value;
};

RenderResult pieChart(const QStringList& lines, bool dark) {
  static const QRegularExpression header(R"(^pie(?:\s+(showData))?(?:\s+title\s+(.+))?$)");
  static const QRegularExpression titleLine(R"(^title\s+(.+)$)");
  static const QRegularExpression sliceLine(
      R"rx(^"([^"]+)"\s*:\s*(-?[0-9]+(?:\.[0-9]+)?)\s*;?\s*(?:%%.*)?$)rx");
  const auto options = header.match(lines.first().trimmed());
  if (!options.hasMatch()) return {{}, unsupported};
  const bool showData = options.capturedStart(1) >= 0;
  QString title = options.captured(2);
  QList<PieSlice> slices;
  QSet<QString> names;
  double total = 0;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto line = lines.at(i).trimmed();
    if (line.isEmpty() || line.startsWith("%%")) continue;
    const auto titleMatch = titleLine.match(line);
    if (titleMatch.hasMatch() && title.isEmpty()) {
      title = titleMatch.captured(1);
      continue;
    }
    const auto slice = sliceLine.match(line);
    if (!slice.hasMatch()) return {{}, QStringLiteral("Invalid pie chart syntax")};
    const auto name = slice.captured(1);
    const auto quantity = slice.captured(2);
    bool ok;
    const double value = quantity.toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < 0)
      return {{}, QStringLiteral("Pie chart values must be finite and non-negative")};
    if (names.contains(name)) continue;
    names.insert(name);
    slices.append({showData ? name + QStringLiteral(" [") + quantity + ']' : name, value});
    total += value;
    if (slices.size() > 64) return {{}, QStringLiteral("Pie chart exceeds 64 slices")};
  }
  if (!std::isfinite(total) || total <= 0)
    return {{}, QStringLiteral("Pie chart requires a positive total")};

  constexpr int maxWidth = 600, margin = 24, diameter = 232, swatch = 12;
  constexpr int legendLeft = margin * 2 + diameter;
  constexpr int legendTextLeft = legendLeft + swatch + 10;
  constexpr int wrap = Qt::TextWordWrap | Qt::TextWrapAnywhere;
  const auto font = diagramFont();
  auto titleFont = font;
  titleFont.setBold(true);
  titleFont.setPointSizeF(15);
  const QFontMetricsF metrics(font), titleMetrics(titleFont);
  qreal legendTextWidth = 120;
  for (const auto& slice : slices)
    legendTextWidth = qMax(legendTextWidth, metrics.horizontalAdvance(slice.legend));
  const int width =
      qCeil(qMin(qreal(maxWidth), qMax(legendTextLeft + legendTextWidth + margin,
                                       titleMetrics.horizontalAdvance(title) + margin * 2)));
  const int legendWidth = width - margin - legendTextLeft;
  const qreal titleHeight =
      title.isEmpty()
          ? 0
          : titleMetrics.boundingRect(QRectF(0, 0, width - margin * 2, 0), wrap, title).height();
  QList<qreal> heights;
  qreal legendHeight = 0;
  for (const auto& slice : slices) {
    const qreal lineHeight =
        metrics.boundingRect(QRectF(0, 0, legendWidth, 0), wrap, slice.legend).height();
    heights.append(qMax(metrics.lineSpacing(), lineHeight) + 8);
    legendHeight += heights.last();
  }
  const qreal contentTop = margin + (title.isEmpty() ? 0 : titleHeight + 16);
  const qreal contentHeight = qMax(qreal(diameter), legendHeight);
  const qreal height = contentTop + contentHeight + margin;
  const qreal scale = qMin(1., 720. / height);
  QImage image = canvas(qCeil(width * scale), qCeil(height * scale), dark);
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  const QColor foreground(dark ? "#e8edf5" : "#263449");
  if (!title.isEmpty()) {
    painter.setFont(titleFont);
    painter.setPen(foreground);
    painter.drawText(QRectF(margin, margin, width - margin * 2, titleHeight),
                     Qt::AlignHCenter | wrap, title);
  }

  const QRectF circle(margin, contentTop + (contentHeight - diameter) / 2, diameter, diameter);
  constexpr int top = 90 * 16, fullTurn = 360 * 16;
  int start = top;
  double cumulative = 0;
  qreal legendY = contentTop + (contentHeight - legendHeight) / 2;
  auto numberFont = font;
  numberFont.setBold(true);
  painter.setFont(font);
  for (qsizetype i = 0; i < slices.size(); ++i) {
    const auto& slice = slices.at(i);
    const auto color = QColor::fromHsv((215 + int(i) * 137) % 360, 148, dark ? 223 : 210);
    cumulative += slice.value;
    const int end =
        top - (i + 1 == slices.size() ? fullTurn : qRound(fullTurn * (cumulative / total)));
    const int span = start - end;
    if (span > 0) {
      painter.setPen(Qt::NoPen);
      painter.setBrush(color);
      painter.drawPie(circle, start, -span);
    }
    const double share = slice.value / total;
    if (share >= 0.10) {
      const auto angle = qDegreesToRadians((start - span / 2.) / 16.);
      const auto point = circle.center() + QPointF(std::cos(angle), -std::sin(angle)) * 72;
      painter.setFont(numberFont);
      painter.setPen(qGray(color.rgb()) > 140 ? QColor("#1c2432") : QColor("#ffffff"));
      painter.drawText(QRectF(point.x() - 24, point.y() - 12, 48, 24), Qt::AlignCenter,
                       QString::number(qRound(share * 100)) + '%');
      painter.setFont(font);
    }
    start = end;

    const qreal rowHeight = heights.at(i);
    painter.setPen(Qt::NoPen);
    painter.setBrush(color);
    painter.drawRoundedRect(
        QRectF(legendLeft, legendY + (metrics.height() - swatch) / 2, swatch, swatch), 2, 2);
    painter.setPen(foreground);
    painter.drawText(QRectF(legendTextLeft, legendY, legendWidth, rowHeight - 8),
                     Qt::AlignTop | wrap, slice.legend);
    legendY += rowHeight;
  }
  return {image, {}};
}

struct TimelinePeriod {
  QString name;
  QStringList events;
  int section;
};

RenderResult timeline(const QStringList& lines, bool dark) {
  static const QRegularExpression heading(R"(^(title|section)[ \t]+(.+)$)",
                                          QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression separator(R"(:[ \t]+)");
  static const QRegularExpression metadata(R"(^acc(?:Title|Descr)[ \t]*[:{])",
                                           QRegularExpression::CaseInsensitiveOption);
  const auto invalid = QStringLiteral("Invalid timeline syntax");
  QString title;
  QStringList sections;
  QList<TimelinePeriod> periods;
  int section = -1, eventCount = 0;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    auto line = lines.at(i);
    const auto comment = line.indexOf(QStringLiteral("%%"));
    if (comment >= 0) line.truncate(comment);
    line = line.trimmed();
    if (line.isEmpty() || line.startsWith('#')) continue;
    if (metadata.match(line).hasMatch()) return {{}, unsupported};
    const auto match = heading.match(line);
    if (match.hasMatch()) {
      const auto text = label(match.captured(2));
      if (text.isEmpty()) return {{}, invalid};
      if (match.captured(1).compare(QStringLiteral("title"), Qt::CaseInsensitive) == 0)
        title = text;
      else {
        section = int(sections.size());
        sections.append(text);
      }
      continue;
    }
    if (line.endsWith(':')) return {{}, invalid};
    auto fields = line.split(separator);
    auto name = label(fields.takeFirst());
    if (!name.isEmpty())
      periods.append({std::move(name), {}, section});
    else if (periods.isEmpty() || periods.last().section != section || fields.isEmpty())
      return {{}, invalid};
    for (auto event : fields) {
      event = label(event);
      if (event.isEmpty()) return {{}, invalid};
      periods.last().events.append(event);
      ++eventCount;
    }
    if (periods.size() > 24 || eventCount > 64)
      return {{}, QStringLiteral("Timeline exceeds 24 periods or 64 events")};
  }
  if (periods.isEmpty()) return {{}, invalid};

  // Keep section boundaries visible and wrap consecutive periods at four columns.
  QList<QPair<qsizetype, qsizetype>> rows;
  int columns = 1;
  for (qsizetype first = 0; first < periods.size();) {
    qsizetype end = first + 1;
    while (end < periods.size() && end - first < 4 &&
           periods.at(end).section == periods.at(first).section)
      ++end;
    rows.append({first, end});
    columns = qMax(columns, int(end - first));
    first = end;
  }
  constexpr int margin = 24, gap = 16, padding = 10;
  constexpr int wrap = Qt::TextWordWrap | Qt::TextWrapAnywhere;
  const int columnWidth = columns == 1 ? 280 : 164;
  const int width = 2 * margin + columns * columnWidth + (columns - 1) * gap;
  const auto font = diagramFont();
  auto headingFont = font;
  headingFont.setBold(true);
  auto titleFont = headingFont;
  titleFont.setPointSizeF(16);
  const auto textHeight = [](const QString& text, const QFontMetricsF& metrics, int available) {
    return qMax(metrics.height(),
                metrics.boundingRect(QRectF(0, 0, available, 0), wrap, text).height());
  };
  const QFontMetricsF bodyMetrics(font), headingMetrics(headingFont);
  const QColor foreground(dark ? "#e8edf5" : "#263449");
  const QColor axis(dark ? "#7d8999" : "#8290a3");
  const QColor eventFill(dark ? "#343e4b" : "#f1f4f8");
  QPicture layout;
  QPainter drawing(&layout);
  drawing.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  const auto box = [&](const QRectF& rect, const QColor& fill) {
    drawing.setPen(Qt::NoPen);
    drawing.setBrush(fill);
    drawing.drawRoundedRect(rect, 5, 5);
  };
  const auto write = [&](const QRectF& rect, const QString& text) {
    drawing.setPen(foreground);
    drawing.drawText(rect, wrap | Qt::AlignVCenter, text);
  };
  const auto accent = [dark](int group) {
    return QColor::fromHsv((215 + group * 113) % 360, 148, dark ? 228 : 173);
  };
  qreal y = margin;
  if (!title.isEmpty()) {
    const auto height = textHeight(title, QFontMetricsF(titleFont), width - 2 * margin);
    drawing.setFont(titleFont);
    write(QRectF(margin, y, width - 2 * margin, height), title);
    y += height + 20;
  }
  for (const auto& [first, end] : rows) {
    const int group = periods.at(first).section;
    const int rowWidth = int(end - first) * (columnWidth + gap) - gap;
    drawing.setFont(headingFont);
    if (group >= 0) {
      const auto& name = sections.at(group);
      const auto height = textHeight(name, headingMetrics, rowWidth - 12);
      box(QRectF(margin, y, 4, height), accent(group));
      write(QRectF(margin + 12, y, rowWidth - 12, height), name);
      y += height + 10;
    }
    qreal periodHeight = 0;
    for (qsizetype i = first; i < end; ++i)
      periodHeight = qMax(
          periodHeight,
          textHeight(periods.at(i).name, headingMetrics, columnWidth - 2 * padding) + 2 * padding);
    const qreal axisY = y + periodHeight + 14;
    drawing.setPen(axis);
    arrow(drawing, QPointF(margin, axisY), QPointF(margin + rowWidth, axisY));
    qreal rowBottom = axisY + 7;
    for (qsizetype i = first; i < end; ++i) {
      const auto& period = periods.at(i);
      const auto marker = accent(group < 0 ? int(i) : group);
      const auto fill = QColor::fromHsv(marker.hue(), dark ? 92 : 38, dark ? 94 : 247);
      const qreal x = margin + (i - first) * (columnWidth + gap);
      const qreal center = x + columnWidth / 2.;
      const QRectF periodBox(x, y, columnWidth, periodHeight);
      box(periodBox, fill);
      drawing.setFont(headingFont);
      write(periodBox.adjusted(padding, padding, -padding, -padding), period.name);
      drawing.setPen(Qt::NoPen);
      drawing.setBrush(marker);
      drawing.drawEllipse(QPointF(center, axisY), 4.5, 4.5);
      drawing.setFont(font);
      qreal previous = axisY + 4;
      qreal top = axisY + 16;
      for (const auto& event : period.events) {
        const auto height = textHeight(event, bodyMetrics, columnWidth - 2 * padding) + 2 * padding;
        drawing.setPen(marker);
        drawing.drawLine(QPointF(center, previous), QPointF(center, top));
        const QRectF card(x, top, columnWidth, height);
        box(card, eventFill);
        write(card.adjusted(padding, padding, -padding, -padding), event);
        previous = card.bottom();
        rowBottom = qMax(rowBottom, previous);
        top = previous + 8;
      }
    }
    y = rowBottom + 24;
  }
  drawing.end();
  const qreal height = qMin(y, 720.);
  const qreal scale = height / y;
  auto image = canvas(qCeil(width * scale), qCeil(height), dark);
  QPainter painter(&image);
  painter.scale(scale, scale);
  painter.drawPicture(QPoint(0, 0), layout);
  return {image, {}};
}

}  // namespace
RenderResult mermaid(const QString& source, bool dark) {
  if (source.size() > 32768) return {{}, QStringLiteral("Diagram exceeds 32768 characters")};
  QStringList lines = source.trimmed().split('\n');
  bool hasConfig = false;
  while (!lines.isEmpty() &&
         (lines.first().trimmed().isEmpty() || lines.first().trimmed().startsWith("%%"))) {
    hasConfig = hasConfig || lines.first().trimmed().startsWith("%%{");
    lines.removeFirst();
  }
  if (lines.isEmpty()) return {{}, unsupported};
  const auto header = lines.first().trimmed();
  if (header == "sequenceDiagram" && !hasConfig)
    return mermaid_detail::sequenceDiagram(lines, dark);
  if (header == "stateDiagram" || header == "stateDiagram-v2")
    return mermaid_detail::stateDiagram(lines, dark);
  if (header == "classDiagram" && !hasConfig) return mermaid_detail::classDiagram(lines, dark);
  if (header.compare(QStringLiteral("gantt"), Qt::CaseInsensitive) == 0 && !hasConfig)
    return mermaid_detail::ganttChart(lines, dark);
  if (header == "quadrantChart" && !hasConfig) return mermaid_detail::quadrantChart(lines, dark);
  if ((header == "sankey" || header == "sankey-beta") && !hasConfig)
    return mermaid_detail::sankey(lines, dark);
  if (header.startsWith(QLatin1String("gitGraph"), Qt::CaseInsensitive) && !hasConfig)
    return mermaid_detail::gitGraph(lines, dark);
  if (header.compare(QStringLiteral("mindmap"), Qt::CaseInsensitive) == 0 && !hasConfig)
    return mermaid_detail::mindmap(lines, dark);
  if (header.compare(QStringLiteral("timeline"), Qt::CaseInsensitive) == 0)
    return timeline(lines, dark);
  if (header.startsWith("graph ") || header.startsWith("flowchart "))
    return mermaid_detail::flowchart(lines, dark);
  if (header.startsWith("pie") && (header.size() == 3 || header.at(3).isSpace()))
    return pieChart(lines, dark);
  if (header.startsWith(QLatin1String("xychart"), Qt::CaseInsensitive))
    return mermaid_detail::xyChart(lines, dark);
  return {{}, unsupported};
}
}  // namespace typist::render
