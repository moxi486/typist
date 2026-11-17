#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QPainter>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace typist::render::mermaid_detail {
namespace {
constexpr int kMaxSeries = 8;
constexpr int kMaxValues = 32;
constexpr int kIntervals = 4;

struct Range {
  double lower, upper;
  double span() const { return upper - lower; }
  bool valid() const { return lower < upper && std::isfinite(span()); }
  double valueAt(double fraction) const {
    return fraction == 1 ? upper : lower + span() * fraction;
  }
  double fraction(double value) const { return qBound(-128., (value - lower) / span(), 129.); }
};

struct Axis {
  QString title;
  QStringList categories;
  std::optional<Range> range;
};

struct Series {
  bool bar;
  QString name;
  QList<double> values;
};

struct Chart {
  bool horizontal;
  QString title;
  Axis x, y;
  QList<Series> series;
};

qsizetype outsideQuotes(QStringView text, QStringView token, qsizetype offset = 0) {
  bool quoted = false;
  for (qsizetype i = offset; i < text.size(); ++i) {
    if (text.at(i) == '"')
      quoted = !quoted;
    else if (!quoted && token.size() <= text.size() - i &&
             std::equal(token.begin(), token.end(), text.begin() + i))
      return i;
  }
  return -1;
}

std::optional<QString> caption(QStringView input) {
  input = input.trimmed();
  if (input.size() >= 2 && input.front() == '"' && input.back() == '"') {
    input = input.sliced(1, input.size() - 2);
  } else if (input.contains('[') || input.contains(']')) {
    return std::nullopt;
  }
  if (input.contains('"')) return std::nullopt;
  return input.toString();
}

qsizetype arrayStart(QStringView text) {
  const auto start = outsideQuotes(text, u"[");
  return start >= 0 && outsideQuotes(text, u"]", start + 1) == text.size() - 1 ? start : -1;
}

std::optional<Axis> readAxis(QStringView text, bool allowCategories) {
  if (outsideQuotes(text, u"[") >= 0) {
    const auto open = arrayStart(text);
    if (!allowCategories || open < 0) return std::nullopt;
    const auto title = caption(text.first(open));
    if (!title) return std::nullopt;
    Axis axis;
    axis.title = *title;
    const auto entries = text.sliced(open + 1).chopped(1);
    qsizetype start = 0;
    while (true) {
      const auto comma = outsideQuotes(entries, u",", start);
      const auto end = comma < 0 ? entries.size() : comma;
      const auto name = caption(entries.sliced(start, end - start));
      if (!name || name->trimmed().isEmpty() || axis.categories.size() == kMaxValues)
        return std::nullopt;
      axis.categories.append(*name);
      if (comma < 0) return axis;
      start = comma + 1;
    }
  }
  if (outsideQuotes(text, u"-->") >= 0) {
    static const QString number =
        QStringLiteral(R"([+-]?(?:[0-9]+(?:\.[0-9]+)?|\.[0-9]+)(?:[eE][+-]?[0-9]+)?)");
    static const QRegularExpression range(
        QStringLiteral(R"(^(?:(.+?)\s+)?(%1)\s*-->\s*(%1)$)").arg(number));
    const auto line = text.toString();
    const auto fields = range.match(line);
    if (!fields.hasMatch()) return std::nullopt;
    const auto rawTitle = fields.captured(1);
    const auto title = caption(rawTitle);
    bool leftOk, rightOk;
    const auto left = fields.captured(2).toDouble(&leftOk);
    const auto right = fields.captured(3).toDouble(&rightOk);
    const Range limits{left, right};
    if (!title || !leftOk || !rightOk || !limits.valid()) return std::nullopt;
    return Axis{*title, {}, limits};
  }
  const auto title = caption(text);
  if (!title || title->isEmpty()) return std::nullopt;
  return Axis{*title, {}, std::nullopt};
}

std::optional<Series> readSeries(QStringView text, bool bar) {
  const auto open = arrayStart(text);
  if (open < 0) return std::nullopt;
  const auto name = caption(text.first(open));
  if (!name) return std::nullopt;
  Series series{bar, *name, {}};
  const auto entries = text.sliced(open + 1).chopped(1);
  for (const auto& token : entries.split(',')) {
    bool ok;
    const auto value = token.trimmed().toDouble(&ok);
    if (!ok || !std::isfinite(value) || series.values.size() == kMaxValues) return std::nullopt;
    series.values.append(value);
  }
  return series;
}

bool readChart(const QStringList& lines, Chart& chart) {
  static const QRegularExpression header(R"(^xychart(?:-beta)?(?:\s+(horizontal|vertical))?$)",
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression statement(R"(^([a-zA-Z-]+)\s+(.+)$)");
  const auto first = lines.first().trimmed();
  const auto options = header.match(first);
  if (!options.hasMatch()) return false;
  chart.horizontal =
      options.captured(1).compare(QLatin1String("horizontal"), Qt::CaseInsensitive) == 0;
  for (qsizetype i = 1; i < lines.size(); ++i) {
    QStringView text(lines.at(i));
    const auto comment = outsideQuotes(text, u"%%");
    if (comment >= 0) text = text.first(comment);
    text = text.trimmed();
    if (text.endsWith(';')) text = text.chopped(1).trimmed();
    if (text.isEmpty()) continue;
    const auto line = text.toString();
    const auto fields = statement.match(line);
    if (!fields.hasMatch()) return false;
    const auto command = fields.captured(1).toLower();
    const auto data = fields.captured(2);
    if (command == "title") {
      const auto title = caption(data);
      if (!title) return false;
      chart.title = *title;
    } else if (command == "x-axis" || command == "y-axis") {
      const bool categoryAxis = command == "x-axis";
      auto axis = readAxis(data, categoryAxis);
      if (!axis) return false;
      (categoryAxis ? chart.x : chart.y) = std::move(*axis);
    } else if (command == "bar" || command == "line") {
      auto series = readSeries(data, command == "bar");
      if (!series || chart.series.size() == kMaxSeries) return false;
      chart.series.append(std::move(*series));
    } else {
      return false;
    }
  }
  return !chart.series.isEmpty();
}

QString tickText(double value) { return QString::number(value, 'g', 12); }

QColor seriesColor(int i, bool dark) {
  static const std::array<QColor, kMaxSeries> colors{
      QColor("#327fc2"), QColor("#bf521c"), QColor("#13846f"), QColor("#8157ac"),
      QColor("#ac3863"), QColor("#1689a3"), QColor("#817114"), QColor("#555e9a")};
  return dark ? colors.at(i).lighter(130) : colors.at(i);
}
}  // namespace

RenderResult xyChart(const QStringList& lines, bool dark) {
  Chart chart{};
  if (!readChart(lines, chart))
    return {{}, QStringLiteral("Invalid or unsupported XY chart syntax")};
  const int categories = int(chart.x.categories.size());
  int count = categories, bars = 0;
  QList<int> named;
  double low = std::numeric_limits<double>::infinity(), high = -low;
  for (int i = 0; i < chart.series.size(); ++i) {
    const auto& series = chart.series.at(i);
    if (!series.name.isEmpty()) named.append(i);
    bars += series.bar;
    if (categories == 0) count = qMax(count, int(series.values.size()));
    const auto length =
        categories == 0 ? series.values.size() : qMin(qsizetype(categories), series.values.size());
    for (qsizetype j = 0; j < length; ++j) {
      const auto value = series.values.at(j);
      low = qMin(low, value);
      high = qMax(high, value);
    }
  }
  if (bars) {
    low = qMin(low, 0.);
    high = qMax(high, 0.);
  }
  if (low == high) {
    const double padding =
        low == 0 ? 1 : qMax(std::abs(low) / 10., std::numeric_limits<double>::denorm_min());
    if (std::isfinite(low - padding)) low -= padding;
    if (std::isfinite(high + padding)) high += padding;
  }
  const Range limits = chart.y.range.value_or(Range{low, high});
  if (!limits.valid()) return {{}, QStringLiteral("XY chart values exceed the supported range")};
  const bool numericX = chart.x.range.has_value();
  const int xTicks = numericX ? kIntervals + 1 : count;
  QStringList xLabels, yLabels;
  for (int i = 0; i < xTicks; ++i)
    xLabels.append(numericX     ? tickText(chart.x.range->valueAt(double(i) / kIntervals))
                   : categories ? chart.x.categories.at(i)
                                : QString::number(i + 1));
  for (int i = 0; i <= kIntervals; ++i)
    yLabels.append(tickText(limits.valueAt(double(i) / kIntervals)));

  constexpr int width = 704, margin = 22;
  const auto font = diagramFont();
  auto titleFont = font;
  titleFont.setBold(true);
  titleFont.setPointSizeF(16);
  const QFontMetricsF metrics(font), titleMetrics(titleFont);
  const int textHeight = qCeil(metrics.height()) + 4;
  const int titleHeight = chart.title.isEmpty() ? 0 : qCeil(titleMetrics.height()) + 13;
  const int legendHeight =
      named.isEmpty() ? 0 : (int(named.size()) + 3) / 4 * (textHeight + 5) + 10;
  const int height = qMin(720, qMax(396, chart.horizontal ? 130 + count * 24 : 396) + legendHeight);
  const auto& leftLabels = chart.horizontal ? xLabels : yLabels;
  const auto& leftTitle = chart.horizontal ? chart.x.title : chart.y.title;
  const auto& bottomTitle = chart.horizontal ? chart.y.title : chart.x.title;
  int leftWidth = 12;
  for (const auto& text : leftLabels)
    leftWidth = qMax(leftWidth, qCeil(metrics.horizontalAdvance(text)));
  leftWidth = qMin(166, leftWidth);
  const int left = margin + leftWidth + 12 + (leftTitle.isEmpty() ? 0 : textHeight + 4);
  const int bottom =
      height - margin - legendHeight - textHeight - (bottomTitle.isEmpty() ? 0 : textHeight + 7);
  const QRectF plot(left, margin + titleHeight, width - left - 2 * margin,
                    bottom - margin - titleHeight);
  const auto position = [&](double u, double v) -> QPointF {
    if (chart.horizontal) return {plot.left() + plot.width() * v, plot.top() + plot.height() * u};
    return {plot.left() + plot.width() * u, plot.bottom() - plot.height() * v};
  };
  const auto categoryLength = chart.horizontal ? plot.height() : plot.width();
  const double groupWidth = qMin(0.72 / count, 52. / categoryLength);
  const double padding = numericX ? groupWidth / 2. : 0.;
  const auto numericPosition = [&](double fraction) {
    return padding + (1 - padding * 2) * fraction;
  };
  const auto datumPosition = [&](int index, int length) {
    return numericX ? numericPosition(length == 1 ? 0. : double(index) / (length - 1))
                    : (index + .5) / count;
  };
  const QColor ink(dark ? "#e8edf5" : "#253145");
  const QColor guide(dark ? "#4c596a" : "#e0e6ef");
  const QColor axis(dark ? "#92a2b7" : "#8491a5");
  auto image = canvas(width, height, dark);
  QPainter paint(&image);
  paint.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  paint.setPen(ink);
  if (titleHeight) {
    paint.setFont(titleFont);
    paint.drawText(QRectF(margin, margin, width - 2 * margin, titleHeight),
                   Qt::AlignHCenter | Qt::AlignTop,
                   titleMetrics.elidedText(chart.title, Qt::ElideRight, width - 2 * margin));
  }
  paint.setFont(font);
  const auto leftLabel = [&](const QPointF& at, const QString& text) {
    paint.setPen(ink);
    paint.drawText(
        QRectF(plot.left() - 8 - leftWidth, at.y() - textHeight / 2., leftWidth, textHeight),
        Qt::AlignRight | Qt::AlignVCenter, metrics.elidedText(text, Qt::ElideRight, leftWidth));
  };
  const auto bottomLabel = [&](const QPointF& at, const QString& text, double space) {
    paint.setPen(ink);
    paint.drawText(QRectF(at.x() - space / 2., plot.bottom() + 3, space, textHeight),
                   Qt::AlignHCenter | Qt::AlignVCenter,
                   metrics.elidedText(text, Qt::ElideRight, space));
  };
  for (int i = 0; i <= kIntervals; ++i) {
    const double tick = double(i) / kIntervals;
    paint.setPen(guide);
    paint.drawLine(position(0, tick), position(1, tick));
    if (chart.horizontal)
      bottomLabel(position(1, tick), yLabels.at(i), plot.width() / kIntervals - 8);
    else
      leftLabel(position(0, tick), yLabels.at(i));
  }
  qreal labelSpace = 34;
  for (const auto& text : xLabels)
    labelSpace = qMax(labelSpace, qMin(90., metrics.horizontalAdvance(text) + 8));
  const int labelStride = numericX
                              ? 1
                              : qMax(1, qCeil((chart.horizontal ? textHeight + 2. : labelSpace) *
                                              count / categoryLength));
  for (int i = 0; i < xTicks; i += labelStride) {
    const double u = numericX ? numericPosition(double(i) / kIntervals) : (i + .5) / count;
    if (chart.horizontal)
      leftLabel(position(u, 0), xLabels.at(i));
    else
      bottomLabel(position(u, 0), xLabels.at(i),
                  categoryLength * labelStride / (numericX ? kIntervals + 1 : count) - 5);
  }
  paint.setPen(axis);
  paint.drawLine(plot.topLeft(), plot.bottomLeft());
  paint.drawLine(plot.bottomLeft(), plot.bottomRight());
  if (limits.lower < 0 && limits.upper > 0)
    paint.drawLine(position(0, limits.fraction(0)), position(1, limits.fraction(0)));

  paint.save();
  paint.setClipRect(plot);
  const double barWidth = bars ? groupWidth / bars : 0;
  const double baseline = qBound(0., limits.fraction(0), 1.);
  int barIndex = 0;
  // Bars go below lines even when a line is declared first. Colors still follow source order.
  for (int i = 0; i < chart.series.size(); ++i) {
    const auto& series = chart.series.at(i);
    if (!series.bar) continue;
    const int length = qMin(count, int(series.values.size()));
    paint.setPen(Qt::NoPen);
    paint.setBrush(seriesColor(i, dark));
    for (int j = 0; j < length; ++j) {
      const auto u = datumPosition(j, length) - groupWidth / 2 + barWidth * (barIndex + .5);
      const double end = limits.fraction(series.values.at(j));
      const QRectF bar(position(u - barWidth * .43, baseline), position(u + barWidth * .43, end));
      paint.drawRect(bar.normalized());
    }
    ++barIndex;
  }
  for (int i = 0; i < chart.series.size(); ++i) {
    const auto& series = chart.series.at(i);
    if (series.bar) continue;
    const int length = qMin(count, int(series.values.size()));
    QPolygonF points;
    for (int j = 0; j < length; ++j)
      points.append(position(datumPosition(j, length), limits.fraction(series.values.at(j))));
    const auto color = seriesColor(i, dark);
    paint.setPen(QPen(color, 2.4, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
    paint.drawPolyline(points);
    paint.setPen(Qt::NoPen);
    paint.setBrush(color);
    for (const auto& point : points) paint.drawEllipse(point, 3.3, 3.3);
  }
  paint.restore();

  paint.setPen(ink);
  if (!leftTitle.isEmpty()) {
    paint.save();
    paint.translate(margin + textHeight / 2., plot.center().y());
    paint.rotate(-90);
    paint.drawText(QRectF(-plot.height() / 2, -textHeight / 2., plot.height(), textHeight),
                   Qt::AlignCenter, metrics.elidedText(leftTitle, Qt::ElideRight, plot.height()));
    paint.restore();
  }
  if (!bottomTitle.isEmpty())
    paint.drawText(QRectF(plot.left(), plot.bottom() + textHeight + 6, plot.width(), textHeight),
                   Qt::AlignCenter, metrics.elidedText(bottomTitle, Qt::ElideRight, plot.width()));

  const int legendWidth = (width - 2 * margin) / qBound(1, int(named.size()), 4);
  for (int i = 0; i < named.size(); ++i) {
    const int index = named.at(i);
    const auto& series = chart.series.at(index);
    const qreal x = margin + (i % 4) * legendWidth;
    const qreal y = height - margin - legendHeight + 10 + (i / 4) * (textHeight + 5);
    paint.setPen(Qt::NoPen);
    paint.setBrush(seriesColor(index, dark));
    if (series.bar)
      paint.drawRoundedRect(QRectF(x, y + (textHeight - 10) / 2., 14, 10), 2, 2);
    else {
      paint.setPen(QPen(seriesColor(index, dark), 2));
      paint.drawLine(QPointF(x, y + textHeight / 2.), QPointF(x + 14, y + textHeight / 2.));
    }
    paint.setPen(ink);
    paint.drawText(QRectF(x + 20, y, legendWidth - 24, textHeight),
                   Qt::AlignLeft | Qt::AlignVCenter,
                   metrics.elidedText(series.name, Qt::ElideRight, legendWidth - 24));
  }
  return {image, {}};
}
}  // namespace typist::render::mermaid_detail
