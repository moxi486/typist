#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QLinearGradient>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QtMath>
#include <algorithm>
#include <array>
#include <cmath>

namespace typist::render::mermaid_detail {
namespace {
struct Node {
  QString name;
  QList<int> inputs, outputs;
  double incoming = 0, outgoing = 0;
  QRectF bar;

  double total() const { return qMax(incoming, outgoing); }
};

struct Link {
  int from, to;
  double value;
  qreal fromY = 0, toY = 0;
};

struct Diagram {
  QList<Node> nodes;
  QList<Link> links;
};

QString csvField(QString field) {
  field = field.trimmed();
  if (field.startsWith('"')) field = field.mid(1, field.size() - 2).replace("\"\"", "\"").trimmed();
  return field;
}

bool parseDiagram(const QStringList& lines, Diagram& diagram) {
  constexpr int maxRecords = 128, maxNodes = 64;
  static const QString field = QStringLiteral(R"rx("(?:[^"]|"")*"|[^,"\n]*)rx");
  static const QRegularExpression record(
      QStringLiteral(R"(^(?![ \t]*%%\{)[ \t]*(%1)[ \t]*,[ \t]*(%1)[ \t]*,[ \t]*(%1)[ \t]*(?:\n|$))")
          .arg(field),
      QRegularExpression::MultilineOption);
  static const QRegularExpression gap(R"(^[ \t]*(?:%%(?!\{)[^\n]*)?(?:\n|$))",
                                      QRegularExpression::MultilineOption);
  const auto csv = lines.mid(1).join('\n').replace("\r\n", "\n");
  QHash<QString, int> nodes;
  QHash<QPair<int, int>, int> pairs;
  const auto nodeIndex = [&](const QString& name) {
    const auto existing = nodes.constFind(name);
    if (existing != nodes.cend()) return *existing;
    const int index = int(diagram.nodes.size());
    nodes.insert(name, index);
    Node node;
    node.name = name;
    diagram.nodes.append(node);
    return index;
  };
  constexpr auto anchored = QRegularExpression::AnchorAtOffsetMatchOption;
  int count = 0;
  for (qsizetype pos = 0; pos < csv.size();) {
    const auto separator = gap.match(csv, pos, QRegularExpression::NormalMatch, anchored);
    if (separator.hasMatch()) {
      pos = separator.capturedEnd();
      continue;
    }
    const auto match = record.match(csv, pos, QRegularExpression::NormalMatch, anchored);
    if (!match.hasMatch() || ++count > maxRecords) return false;
    pos = match.capturedEnd();
    const auto source = csvField(match.captured(1)), target = csvField(match.captured(2));
    bool ok;
    const double value = csvField(match.captured(3)).toDouble(&ok);
    if (!ok || !std::isfinite(value) || value < 0 || source.isEmpty() || target.isEmpty())
      return false;
    if (value == 0) continue;
    if (source == target) return false;
    const int from = nodeIndex(source), to = nodeIndex(target);
    if (diagram.nodes.size() > maxNodes) return false;
    auto& a = diagram.nodes[from];
    auto& b = diagram.nodes[to];
    a.outgoing += value;
    b.incoming += value;
    if (!std::isfinite(a.outgoing) || !std::isfinite(b.incoming)) return false;
    const QPair<int, int> key(from, to);
    const auto repeated = pairs.constFind(key);
    if (repeated != pairs.cend()) {
      diagram.links[*repeated].value += value;
      continue;
    }
    const int index = int(diagram.links.size());
    pairs.insert(key, index);
    a.outputs.append(index);
    b.inputs.append(index);
    diagram.links.append({from, to, value});
  }
  return !diagram.links.isEmpty();
}

QList<QList<int>> diagramColumns(const Diagram& diagram) {
  QList<QPair<int, int>> links;
  for (const auto& link : diagram.links) links.append({link.from, link.to});
  auto ranks = graphRanks(int(diagram.nodes.size()), links);
  for (const auto& [from, to] : links)
    if (ranks.at(from) >= ranks.at(to)) return {};
  const int last = *std::max_element(ranks.cbegin(), ranks.cend());
  QList<QList<int>> columns(last + 1);
  for (int i = 0; i < diagram.nodes.size(); ++i) {
    const int column = diagram.nodes.at(i).outputs.isEmpty() ? last : ranks.at(i);
    columns[column].append(i);
  }
  return columns;
}

void orderColumns(const Diagram& diagram, QList<QList<int>>& columns) {
  // Make a few passes toward the connected endpoints to reduce ribbon crossings.
  QList<qreal> positions(diagram.nodes.size()), centers(diagram.nodes.size());
  const auto update = [&](const QList<int>& column) {
    for (int i = 0; i < column.size(); ++i) positions[column.at(i)] = (i + .5) / column.size();
  };
  for (const auto& column : columns) update(column);
  for (int pass = 0; pass < 4; ++pass) {
    const bool forward = pass % 2 == 0;
    for (int i = 1; i < columns.size(); ++i) {
      auto& column = columns[forward ? i : columns.size() - 1 - i];
      for (const int index : column) {
        const auto& node = diagram.nodes.at(index);
        const auto& edges = forward ? node.inputs : node.outputs;
        const double total = forward ? node.incoming : node.outgoing;
        qreal average = 0;
        for (const int edge : edges) {
          const auto& link = diagram.links.at(edge);
          average += (link.value / total) * positions.at(forward ? link.from : link.to);
        }
        centers[index] = edges.isEmpty() ? positions.at(index) : average;
      }
      std::stable_sort(column.begin(), column.end(),
                       [&](int a, int b) { return centers.at(a) < centers.at(b); });
      update(column);
    }
  }
}

void distributeLinks(Diagram& diagram, int index, bool incoming, double largest, qreal scale) {
  auto& node = diagram.nodes[index];
  auto& links = incoming ? node.inputs : node.outputs;
  const auto endpoint = [&](int link) {
    const auto& edge = diagram.links.at(link);
    return diagram.nodes.at(incoming ? edge.from : edge.to).bar.center().y();
  };
  std::stable_sort(links.begin(), links.end(),
                   [&](int a, int b) { return endpoint(a) < endpoint(b); });
  const double total = incoming ? node.incoming : node.outgoing;
  qreal offset = node.bar.center().y() - (total / largest) * scale / 2;
  for (const int link : links) {
    auto& edge = diagram.links[link];
    (incoming ? edge.toY : edge.fromY) = offset;
    offset += (edge.value / largest) * scale;
  }
}

QImage drawDiagram(Diagram& diagram, const QList<QList<int>>& columns, bool dark) {
  constexpr qreal margin = 22, labelWidth = 142, columnStep = 210, barWidth = 12;
  const auto font = diagramFont();
  auto names = font;
  names.setBold(true);
  auto values = font;
  values.setPointSizeF(9);
  const QFontMetricsF labelMetrics(names), valueMetrics(values);
  const qreal labelHeight = qMax(18., labelMetrics.height());
  const qreal valueHeight = qMax(15., valueMetrics.height());
  const qreal gap = labelHeight + valueHeight + 7;
  const auto maximum =
      std::max_element(diagram.nodes.cbegin(), diagram.nodes.cend(),
                       [](const Node& a, const Node& b) { return a.total() < b.total(); });
  const double largest = maximum->total();
  qsizetype largestColumn = 0;
  for (const auto& column : columns) largestColumn = qMax(largestColumn, column.size());
  const qreal plotHeight = qMax(300., largestColumn * gap + 125);
  qreal scale = plotHeight;
  for (const auto& column : columns) {
    if (column.isEmpty()) continue;
    double amount = 0;
    for (const int index : column) amount += diagram.nodes.at(index).total() / largest;
    if (amount > 0) scale = qMin(scale, (plotHeight - (column.size() - 1) * gap) / amount);
  }
  const qreal first = margin + labelWidth + 10;
  const qreal last = first + (columns.size() - 1) * columnStep;
  for (int i = 0; i < columns.size(); ++i) {
    const auto& column = columns.at(i);
    if (column.isEmpty()) continue;
    qreal filled = (column.size() - 1) * gap;
    for (const int index : column) filled += (diagram.nodes.at(index).total() / largest) * scale;
    qreal y = margin + (plotHeight - filled) / 2;
    for (const int index : column) {
      auto& node = diagram.nodes[index];
      node.bar = QRectF(first + i * columnStep, y, barWidth, (node.total() / largest) * scale);
      y += node.bar.height() + gap;
    }
  }
  for (int i = 0; i < diagram.nodes.size(); ++i) {
    distributeLinks(diagram, i, true, largest, scale);
    distributeLinks(diagram, i, false, largest, scale);
  }

  const qreal width = last + barWidth + labelWidth + 10 + margin;
  const qreal height = plotHeight + margin * 2;
  const qreal fit = qMin(1., qMin(1060. / width, 1280. / height));
  auto image = canvas(qCeil(width * fit), qCeil(height * fit), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(fit, fit);
  const std::array<QColor, 7> colors{
      QColor(dark ? "#88baff" : "#3779be"), QColor(dark ? "#85c7ad" : "#398667"),
      QColor(dark ? "#b5a5e9" : "#8061b3"), QColor(dark ? "#efb894" : "#a6613f"),
      QColor(dark ? "#e0a0bd" : "#a05278"), QColor(dark ? "#91cbd1" : "#297e87"),
      QColor(dark ? "#d2c38a" : "#827230")};
  const auto palette = [&](int index) { return colors[index % colors.size()]; };
  painter.setPen(Qt::NoPen);
  for (const auto& link : diagram.links) {
    const qreal start = diagram.nodes.at(link.from).bar.right();
    const qreal end = diagram.nodes.at(link.to).bar.left();
    const qreal center = (start + end) / 2;
    const qreal thickness = (link.value / largest) * scale;
    auto from = palette(link.from), to = palette(link.to);
    const int opacity = dark ? 125 : 106;
    from.setAlpha(opacity);
    to.setAlpha(opacity);
    QLinearGradient gradient(start, 0, end, 0);
    gradient.setColorAt(0, from);
    gradient.setColorAt(1, to);
    QPainterPath ribbon(QPointF(start, link.fromY));
    ribbon.cubicTo(center, link.fromY, center, link.toY, end, link.toY);
    ribbon.lineTo(end, link.toY + thickness);
    ribbon.cubicTo(center, link.toY + thickness, center, link.fromY + thickness, start,
                   link.fromY + thickness);
    ribbon.closeSubpath();
    painter.setBrush(gradient);
    painter.drawPath(ribbon);
  }

  const QColor ink(dark ? "#eff4fc" : "#1d2d45"), muted(dark ? "#b9c8dc" : "#4d6178");
  const QColor textBackground(dark ? "#262b31" : "#ffffff");
  for (int i = 0; i < diagram.nodes.size(); ++i) {
    const auto& node = diagram.nodes.at(i);
    auto bar = node.bar;
    if (bar.height() < 1) {
      bar.setHeight(1);
      bar.moveCenter(node.bar.center());
    }
    painter.fillRect(bar, palette(i));
    const bool left = node.bar.left() == first;
    const auto text = labelMetrics.elidedText(node.name.simplified(), Qt::ElideRight, labelWidth);
    const auto value =
        valueMetrics.elidedText(QString::number(node.total(), 'g', 9), Qt::ElideRight, labelWidth);
    const qreal w =
        qMax(labelMetrics.horizontalAdvance(text), valueMetrics.horizontalAdvance(value));
    const QRectF caption(left ? bar.left() - w - 9 : bar.right() + 9,
                         bar.center().y() - (labelHeight + valueHeight) / 2, w, labelHeight);
    if (!left && bar.left() != last) {
      auto background = textBackground;
      background.setAlpha(232);
      painter.setPen(Qt::NoPen);
      painter.setBrush(background);
      painter.drawRoundedRect(caption.adjusted(-4, -2, 4, valueHeight + 2), 3, 3);
    }
    const int alignment = Qt::AlignVCenter | (left ? Qt::AlignRight : Qt::AlignLeft);
    painter.setPen(ink);
    painter.setFont(names);
    painter.drawText(caption, alignment, text);
    painter.setPen(muted);
    painter.setFont(values);
    painter.drawText(QRectF(caption.bottomLeft(), QSizeF(w, valueHeight)), alignment, value);
  }
  return image;
}
}  // namespace

RenderResult sankey(const QStringList& lines, bool dark) {
  Diagram diagram;
  if (!parseDiagram(lines, diagram)) return {{}, unsupported};
  auto columns = diagramColumns(diagram);
  if (columns.isEmpty()) return {{}, QStringLiteral("Sankey links must not form a cycle")};
  orderColumns(diagram, columns);
  return {drawDiagram(diagram, columns, dark), {}};
}
}  // namespace typist::render::mermaid_detail
