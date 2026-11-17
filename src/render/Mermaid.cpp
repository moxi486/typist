#include "SpecialContent.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QtMath>
#include <cmath>

namespace typist::render {
namespace {
const QString unsupported = QStringLiteral(
    "This Mermaid syntax is not supported by the native renderer. Edit the source to use "
    "flowchart, sequenceDiagram or pie.");
QFont diagramFont() {
  QFont f;
  f.setFamilies({"Arial", "Microsoft YaHei", "Noto Sans"});
  f.setPointSizeF(11);
  return f;
}
QString label(QString text) {
  text = text.trimmed();
  if (text.startsWith('"') && text.endsWith('"')) text = text.mid(1, text.size() - 2);
  text.replace(QRegularExpression("<br\\s*/?>", QRegularExpression::CaseInsensitiveOption), "\n");
  return text;
}
QImage canvas(int width, int height, bool dark) {
  QImage image(width * 2, height * 2, QImage::Format_ARGB32_Premultiplied);
  image.setDevicePixelRatio(2);
  image.fill(dark ? QColor("#262b31") : QColor("#ffffff"));
  return image;
}

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

void arrow(QPainter& painter, QPointF a, QPointF b, bool dashed = false, bool head = true) {
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
struct Node {
  QString id, text;
  int shape = 0, rank = 0;
  QRectF rect;
};
struct Edge {
  int from, to;
  QString text;
  bool dashed = false, head = true;
};
RenderResult flowchart(QStringList lines, bool dark) {
  const auto header = lines.takeFirst().trimmed();
  static const QRegularExpression direction("^(?:graph|flowchart)\\s+(TD|TB|BT|LR|RL)\\s*$");
  const auto match = direction.match(header);
  if (!match.hasMatch()) return {{}, unsupported};
  const QString mode = match.captured(1);
  const bool horizontal = mode == "LR" || mode == "RL";
  QList<Node> nodes;
  QList<Edge> edges;
  QHash<QString, int> ids;
  static const QRegularExpression nodePattern(
      R"rx(^\s*([A-Za-z0-9_]+(?:-[A-Za-z0-9_]+)*)(?:\[([^\]]*)\]|\(\(([^)]*)\)\)|\(([^)]*)\)|\{([^}]*)\})?)rx");
  static const QRegularExpression edgePattern(
      R"rx(^\s*(-->|---|==>|-\.->|--\s+(.+?)\s+-->)\s*(?:\|([^|]*)\|\s*)?)rx");
  auto node = [&](QString& remaining) {
    const auto m = nodePattern.match(remaining);
    if (!m.hasMatch()) return -1;
    remaining.remove(0, m.capturedLength());
    const auto id = m.captured(1);
    int index = ids.value(id, -1);
    if (index < 0) {
      index = nodes.size();
      ids.insert(id, index);
      nodes.append({id, id, 0, 0, {}});
    }
    if (!m.captured(2).isNull()) {
      nodes[index].text = label(m.captured(2));
      nodes[index].shape = 0;
    } else if (!m.captured(3).isNull()) {
      nodes[index].text = label(m.captured(3));
      nodes[index].shape = 2;
    } else if (!m.captured(4).isNull()) {
      nodes[index].text = label(m.captured(4));
      nodes[index].shape = 1;
    } else if (!m.captured(5).isNull()) {
      nodes[index].text = label(m.captured(5));
      nodes[index].shape = 3;
    }
    return index;
  };
  // Semicolons separate statements only outside labels and quoted strings.
  QStringList statements;
  for (const auto& line : lines) {
    if (line.trimmed().isEmpty() || line.trimmed().startsWith("%%")) continue;
    QString statement;
    int nesting = 0;
    bool quoted = false;
    for (QChar c : line) {
      if (c == '"') quoted = !quoted;
      if (!quoted && (c == '[' || c == '{' || c == '(')) ++nesting;
      if (!quoted && (c == ']' || c == '}' || c == ')')) --nesting;
      if (c == ';' && nesting == 0 && !quoted) {
        statements.append(statement);
        statement.clear();
      } else
        statement += c;
    }
    if (!statement.trimmed().isEmpty()) statements.append(statement);
  }
  for (auto statement : statements) {
    int previous = node(statement);
    if (previous < 0) return {{}, unsupported};
    while (!statement.trimmed().isEmpty()) {
      const auto edge = edgePattern.match(statement);
      if (!edge.hasMatch()) return {{}, unsupported};
      statement.remove(0, edge.capturedLength());
      const int next = node(statement);
      if (next < 0) return {{}, unsupported};
      edges.append({previous, next,
                    label(edge.captured(3).isEmpty() ? edge.captured(2) : edge.captured(3)),
                    edge.captured(1) == "-.->", edge.captured(1) != "---"});
      previous = next;
      if (edges.size() > 128) return {{}, QStringLiteral("Diagram exceeds 128 edges")};
    }
    if (nodes.size() > 64) return {{}, QStringLiteral("Diagram exceeds 64 nodes")};
  }
  if (nodes.isEmpty()) return {{}, unsupported};
  // Kahn ordering gives branches their own row/column; cycles remain on bounded
  // fallback ranks rather than iterating rank relaxation indefinitely.
  QList<int> indegree(nodes.size(), 0), queue;
  int maxRank = 0;
  for (const auto& edge : edges)
    if (edge.from != edge.to) ++indegree[edge.to];
  for (int i = 0; i < nodes.size(); ++i)
    if (!indegree[i]) queue.append(i);
  QSet<int> visited;
  for (int at = 0; at < queue.size(); ++at) {
    const int n = queue[at];
    visited.insert(n);
    for (const auto& edge : edges)
      if (edge.from == n && edge.to != n) {
        nodes[edge.to].rank = qMax(nodes[edge.to].rank, nodes[n].rank + 1);
        if (!--indegree[edge.to]) queue.append(edge.to);
      }
  }
  for (int i = 0; i < nodes.size(); ++i)
    if (!visited.contains(i)) nodes[i].rank = maxRank++;
  maxRank = 0;
  QHash<int, QList<int>> ranks;
  for (int i = 0; i < nodes.size(); ++i) {
    maxRank = qMax(maxRank, nodes[i].rank);
    ranks[nodes[i].rank].append(i);
  }
  const QFont font = diagramFont();
  const QFontMetricsF metrics(font);
  qreal nodeWidth = 120, nodeHeight = 54;
  int maxParallel = 1;
  for (const auto& n : nodes) {
    const auto bounds =
        metrics.boundingRect(QRectF(0, 0, 220, 180), Qt::TextWordWrap | Qt::AlignCenter, n.text);
    nodeWidth = qMax(nodeWidth, qMin(250., bounds.width() + 36));
    nodeHeight = qMax(nodeHeight, bounds.height() + 30);
  }
  for (const auto& rank : ranks) maxParallel = qMax(maxParallel, static_cast<int>(rank.size()));
  const qreal majorStep = horizontal ? nodeWidth + 110 : nodeHeight + 82;
  const qreal minorStep = horizontal ? nodeHeight + 40 : nodeWidth + 48;
  const qreal majorSize = (maxRank + 1) * majorStep + 28, minorSize = maxParallel * minorStep + 28;
  const qreal naturalWidth = horizontal ? majorSize : minorSize,
              naturalHeight = horizontal ? minorSize : majorSize;
  const qreal scale = qMin(1., qMin(820. / naturalWidth, 1400. / naturalHeight));
  for (int rank = 0; rank <= maxRank; ++rank) {
    const auto group = ranks.value(rank);
    for (int j = 0; j < group.size(); ++j) {
      const qreal major = 28 + rank * majorStep,
                  minor = 14 + (minorSize - group.size() * minorStep) / 2 + j * minorStep;
      QRectF rect(horizontal ? major : minor, horizontal ? minor : major, nodeWidth, nodeHeight);
      if (mode == "RL") rect.moveLeft(naturalWidth - rect.right() - 14);
      if (mode == "BT") rect.moveTop(naturalHeight - rect.bottom() - 14);
      nodes[group[j]].rect = rect;
    }
  }
  QImage image = canvas(qCeil(naturalWidth * scale), qCeil(naturalHeight * scale), dark);
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  const QColor foreground(dark ? "#dde3ec" : "#445269"), background(dark ? "#262b31" : "#ffffff");
  painter.setPen(foreground);
  for (const auto& edge : edges) {
    const auto a = nodes[edge.from].rect, b = nodes[edge.to].rect;
    const bool reverse =
        horizontal ? a.center().x() > b.center().x() : a.center().y() > b.center().y();
    const QPointF start = horizontal ? QPointF(reverse ? a.left() : a.right(), a.center().y())
                                     : QPointF(a.center().x(), reverse ? a.top() : a.bottom());
    const QPointF finish = horizontal ? QPointF(reverse ? b.right() : b.left(), b.center().y())
                                      : QPointF(b.center().x(), reverse ? b.bottom() : b.top());
    painter.setPen(foreground);
    arrow(painter, start, finish, edge.dashed, edge.head);
    if (!edge.text.isEmpty()) {
      const QPointF midpoint = (start + finish) / 2;
      QRectF text = metrics.boundingRect(edge.text);
      text.moveCenter(midpoint);
      text.adjust(-5, -4, 5, 4);
      painter.fillRect(text, background);
      painter.drawText(text, Qt::AlignCenter, edge.text);
    }
  }
  for (const auto& node : nodes) {
    painter.setPen(QPen(dark ? QColor("#7fa3d2") : QColor("#96b3da"), 1.5));
    painter.setBrush(dark ? QColor("#344359") : QColor("#edf3fc"));
    if (node.shape == 2)
      painter.drawEllipse(node.rect);
    else if (node.shape == 3)
      painter.drawPolygon(QPolygonF({QPointF(node.rect.center().x(), node.rect.top() - 8),
                                     QPointF(node.rect.right() + 8, node.rect.center().y()),
                                     QPointF(node.rect.center().x(), node.rect.bottom() + 8),
                                     QPointF(node.rect.left() - 8, node.rect.center().y())}));
    else
      painter.drawRoundedRect(node.rect, node.shape == 1 ? 22 : 5, node.shape == 1 ? 22 : 5);
    painter.setPen(foreground);
    painter.drawText(node.rect.adjusted(12, 4, -12, -4), Qt::AlignCenter | Qt::TextWordWrap,
                     node.text);
  }
  return {image, {}};
}
struct Message {
  QString from, to, text;
  bool dashed = false, note = false;
};
RenderResult sequence(QStringList lines, bool dark) {
  lines.removeFirst();
  QStringList ids;
  QHash<QString, QString> names;
  QList<Message> messages;
  bool numbered = false;
  auto participant = [&](const QString& id) {
    if (!ids.contains(id)) {
      ids.append(id);
      names[id] = id;
    }
  };
  static const QRegularExpression declaration(
      R"(^\s*(?:participant|actor)\s+(\w+)(?:\s+as\s+(.+))?\s*$)");
  static const QRegularExpression message(R"(^\s*(\w+)\s*(-->>|->>|-->|->)\s*(\w+)\s*:\s*(.*)$)");
  static const QRegularExpression note(R"(^\s*Note\s+over\s+(\w+)(?:\s*,\s*(\w+))?\s*:\s*(.*)$)",
                                       QRegularExpression::CaseInsensitiveOption);
  for (const auto& line : lines) {
    if (line.trimmed().isEmpty() || line.trimmed().startsWith("%%")) continue;
    if (line.trimmed() == "autonumber") {
      numbered = true;
      continue;
    }
    auto match = declaration.match(line);
    if (match.hasMatch()) {
      participant(match.captured(1));
      if (!match.captured(2).isEmpty()) names[match.captured(1)] = label(match.captured(2));
      continue;
    }
    match = message.match(line);
    if (match.hasMatch()) {
      participant(match.captured(1));
      participant(match.captured(3));
      messages.append({match.captured(1), match.captured(3), label(match.captured(4)),
                       match.captured(2).startsWith("--"), false});
    } else {
      match = note.match(line);
      if (!match.hasMatch()) return {{}, unsupported};
      participant(match.captured(1));
      const auto to = match.captured(2).isEmpty() ? match.captured(1) : match.captured(2);
      participant(to);
      messages.append({match.captured(1), to, label(match.captured(3)), false, true});
    }
    if (ids.size() > 8 || messages.size() > 60)
      return {{}, QStringLiteral("Sequence diagram exceeds 8 participants or 60 messages")};
  }
  if (ids.isEmpty()) return {{}, unsupported};
  const int width = qMax(340, static_cast<int>(ids.size()) * 200),
            height = 130 + messages.size() * 74;
  const qreal scale = qMin(1., qMin(820. / width, 1400. / height));
  QImage image = canvas(qCeil(width * scale), qCeil(height * scale), dark);
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(diagramFont());
  const QColor foreground(dark ? "#dde3ec" : "#445269");
  QHash<QString, qreal> x;
  for (int i = 0; i < ids.size(); ++i) {
    const qreal center = width * (i + .5) / ids.size();
    x[ids[i]] = center;
    painter.setPen(QPen(foreground, 1, Qt::DashLine));
    painter.drawLine(QPointF(center, 62), QPointF(center, height - 30));
    painter.setPen(QPen(foreground, 1));
    painter.setBrush(dark ? QColor("#344359") : QColor("#edf3fc"));
    painter.drawRoundedRect(QRectF(center - 75, 18, 150, 44), 5, 5);
    painter.drawText(QRectF(center - 71, 18, 142, 44), Qt::AlignCenter | Qt::TextWordWrap,
                     names.value(ids[i]));
  }
  int number = 0;
  for (int i = 0; i < messages.size(); ++i) {
    const auto& m = messages[i];
    const qreal a = x.value(m.from), b = x.value(m.to), y = 108 + i * 74;
    painter.setPen(foreground);
    if (m.note) {
      const QRectF rect(qMin(a, b) - 65, y - 23, qMax(130., qAbs(a - b) + 130), 48);
      painter.setBrush(dark ? QColor("#625c36") : QColor("#fff7cf"));
      painter.drawRect(rect);
      painter.drawText(rect.adjusted(6, 2, -6, -2), Qt::AlignCenter | Qt::TextWordWrap, m.text);
    } else {
      const QString text = (numbered ? QString::number(++number) + ". " : QString()) + m.text;
      if (a == b) {
        painter.drawLine(QPointF(a, y), QPointF(a + 42, y));
        painter.drawLine(QPointF(a + 42, y), QPointF(a + 42, y + 25));
        arrow(painter, {a + 42, y + 25}, {a, y + 25}, m.dashed);
        painter.drawText(QRectF(a + 10, y - 28, 170, 28), Qt::AlignLeft | Qt::AlignVCenter, text);
      } else {
        arrow(painter, {a, y}, {b, y}, m.dashed);
        painter.drawText(QRectF(qMin(a, b) + 5, y - 48, qAbs(a - b) - 10, 44),
                         Qt::AlignCenter | Qt::TextWordWrap, text);
      }
    }
  }
  return {image, {}};
}
}  // namespace
RenderResult mermaid(const QString& source, bool dark) {
  if (source.size() > 32768) return {{}, QStringLiteral("Diagram exceeds 32768 characters")};
  QStringList lines = source.trimmed().split('\n');
  while (!lines.isEmpty() &&
         (lines.first().trimmed().isEmpty() || lines.first().trimmed().startsWith("%%")))
    lines.removeFirst();
  if (lines.isEmpty()) return {{}, unsupported};
  const auto header = lines.first().trimmed();
  if (header == "sequenceDiagram") return sequence(lines, dark);
  if (header.startsWith("graph ") || header.startsWith("flowchart ")) return flowchart(lines, dark);
  if (header.startsWith("pie") && (header.size() == 3 || header.at(3).isSpace()))
    return pieChart(lines, dark);
  return {{}, unsupported};
}
}  // namespace typist::render
