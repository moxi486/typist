#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QPainterPath>
#include <QRegularExpression>
#include <QSet>
#include <QtMath>
#include <algorithm>
#include <array>

namespace typist::render::mermaid_detail {
namespace {
enum class CommitType { Normal, Reverse, Highlight };

struct Commit {
  QString id;
  QStringList tags;
  CommitType type = CommitType::Normal;
  int branch = 0, parent = -1, merged = -1;
};

struct Branch {
  QString name;
  int head = -1, fork = -1, order = -1;
};

struct Graph {
  QList<Commit> commits;
  QList<Branch> branches{{QStringLiteral("main")}};
  QString direction;
};

bool statements(const QStringList& lines, QStringList& result) {
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto& line = lines.at(i);
    if (line.trimmed().startsWith("%%{")) return false;
    QString statement;
    bool quoted = false;
    for (qsizetype j = 0; j < line.size(); ++j) {
      const auto ch = line.at(j);
      if (!quoted && ch == '%' && j + 1 < line.size() && line.at(j + 1) == '%') break;
      if (ch == '"') quoted = !quoted;
      if (!quoted && ch == ';') {
        if (!statement.trimmed().isEmpty()) result.append(statement.trimmed());
        statement.clear();
      } else {
        statement += ch;
      }
    }
    if (quoted) return false;
    if (!statement.trimmed().isEmpty()) result.append(statement.trimmed());
  }
  return true;
}

bool attributes(QString text, Commit& commit) {
  static const QRegularExpression field(
      R"rx(^\s*(?:(id|tag)\s*:\s*"([^"]+)"|type\s*:\s*(NORMAL|REVERSE|HIGHLIGHT))(?=\s|$))rx");
  bool hasType = false;
  while (!text.trimmed().isEmpty()) {
    const auto match = field.match(text);
    if (!match.hasMatch()) return false;
    text.remove(0, match.capturedLength());
    if (match.captured(1) == "tag") {
      commit.tags.append(match.captured(2));
    } else if (match.captured(1) == "id") {
      if (!commit.id.isEmpty()) return false;
      commit.id = match.captured(2);
    } else {
      if (hasType) return false;
      hasType = true;
      const auto value = match.captured(3);
      commit.type = value == "HIGHLIGHT" ? CommitType::Highlight
                    : value == "REVERSE" ? CommitType::Reverse
                                         : CommitType::Normal;
    }
  }
  return true;
}

bool readGraph(const QStringList& lines, Graph& graph) {
  static const QRegularExpression header(R"(^gitGraph(?:\s+(LR|TB|BT))?\s*:?\s*$)",
                                         QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression command(R"(^(commit|branch|checkout|switch|merge)(?:\s+(.*))?$)");
  static const QRegularExpression name(R"rx(^("[^"]+"|[\p{L}\p{N}_./-]+)(?=\s|$))rx");
  static const QRegularExpression order(R"(^\s+order\s*:\s*([0-9]+)\s*$)");
  const auto orientation = header.match(lines.first().trimmed());
  if (!orientation.hasMatch()) return false;
  graph.direction = orientation.captured(1).toUpper();
  QStringList commands;
  if (!statements(lines, commands)) return false;
  QHash<QString, int> branches{{QStringLiteral("main"), 0}};
  QSet<QString> ids;
  int active = 0, generatedId = 0;
  for (const auto& statement : commands) {
    const auto match = command.match(statement);
    if (!match.hasMatch()) return false;
    const auto operation = match.captured(1);
    auto rest = match.captured(2);
    int other = -1;
    if (operation != "commit") {
      const auto target = name.match(rest);
      if (!target.hasMatch()) return false;
      auto branchName = target.captured(1);
      if (branchName.startsWith('"')) branchName = branchName.mid(1, branchName.size() - 2);
      if (branchName.trimmed().isEmpty()) return false;
      rest.remove(0, target.capturedLength());
      other = branches.value(branchName, -1);
      if (operation == "branch") {
        if (other >= 0 || graph.branches.size() >= 8) return false;
        int priority = -1;
        if (!rest.trimmed().isEmpty()) {
          const auto rank = order.match(rest);
          bool ok = false;
          priority = rank.captured(1).toInt(&ok);
          if (!rank.hasMatch() || !ok || priority <= 0) return false;
        }
        const int head = graph.branches.at(active).head;
        active = int(graph.branches.size());
        branches.insert(branchName, active);
        graph.branches.append({branchName, head, head, priority});
        continue;
      }
      if (other < 0) return false;
      if (operation == "checkout" || operation == "switch") {
        if (!rest.trimmed().isEmpty()) return false;
        active = other;
        continue;
      }
    }
    if (graph.commits.size() >= 32) return false;
    Commit commit;
    commit.branch = active;
    commit.parent = graph.branches.at(active).head;
    if (operation == "merge") {
      commit.merged = graph.branches.at(other).head;
      if (other == active || commit.parent < 0 || commit.merged < 0 ||
          commit.parent == commit.merged)
        return false;
    }
    if (!attributes(rest, commit)) return false;
    if (commit.id.isEmpty()) {
      do {
        commit.id = QStringLiteral("c%1").arg(++generatedId);
      } while (ids.contains(commit.id));
    }
    if (ids.contains(commit.id)) return false;
    ids.insert(commit.id);
    graph.branches[active].head = int(graph.commits.size());
    graph.commits.append(commit);
  }
  return !graph.commits.isEmpty();
}

void caption(QPainter& painter, const QString& text, QRectF box, const QColor& ink,
             const QColor& background, bool badge = false) {
  if (text.isEmpty()) return;
  const QFontMetricsF metrics(painter.font());
  const auto display = metrics.elidedText(text.simplified(), Qt::ElideRight, box.width() - 10);
  QRectF bounds(QPointF(), QSizeF(metrics.horizontalAdvance(display) + 10, box.height()));
  bounds.moveCenter(box.center());
  painter.setPen(Qt::NoPen);
  painter.setBrush(background);
  painter.drawRoundedRect(bounds, badge ? 5 : 2, badge ? 5 : 2);
  painter.setPen(ink);
  painter.drawText(bounds, Qt::AlignCenter, display);
}

QImage drawGraph(const Graph& graph, bool dark) {
  auto font = diagramFont();
  font.setPointSizeF(9.5);
  auto branchFont = diagramFont();
  branchFont.setBold(true);
  const QFontMetricsF metrics(font), branchMetrics(branchFont);
  const bool horizontal = graph.direction.isEmpty() || graph.direction == "LR";
  const bool upwards = graph.direction == "BT";
  constexpr qreal margin = 24, labelWidth = 112;
  const qreal rowHeight = qMax(20., metrics.height() + 4);
  const qreal branchHeight = qMax(25., branchMetrics.height() + 8);
  const qreal laneStep = horizontal ? qMax(100., rowHeight * 3 + 28) : labelWidth + 46;
  const qreal timeStep = horizontal ? labelWidth + 12 : qMax(68., rowHeight * 2 + 25);
  const qreal lead = horizontal ? margin + labelWidth + 20 : margin + branchHeight + 35;
  const qreal laneStart = horizontal ? margin + rowHeight + 20 : margin + labelWidth / 2;
  const qreal timeEnd = lead + (graph.commits.size() - 1) * timeStep + timeStep * .55;
  const qreal timeSize = timeEnd + (horizontal ? labelWidth / 2 : 32) + margin;
  const qreal laneSize = laneStart + (graph.branches.size() - 1) * laneStep +
                         (horizontal ? rowHeight * 2 + margin : labelWidth / 2 + margin);
  const QSizeF area(horizontal ? timeSize : laneSize, horizontal ? laneSize : timeSize);
  QList<int> order;
  for (int i = 0; i < graph.branches.size(); ++i) order.append(i);
  std::stable_sort(order.begin() + 1, order.end(), [&](int a, int b) {
    const auto x = graph.branches.at(a).order, y = graph.branches.at(b).order;
    if (x < 0 || y < 0) return x < 0 && y >= 0;
    return x < y;
  });
  QList<qreal> lanes(graph.branches.size());
  for (int i = 0; i < order.size(); ++i) lanes[order.at(i)] = laneStart + i * laneStep;
  const auto point = [&](qreal time, qreal lane) {
    return horizontal ? QPointF(time, lane) : QPointF(lane, upwards ? timeSize - time : time);
  };
  const auto position = [&](int commit) {
    return point(lead + commit * timeStep, lanes.at(graph.commits.at(commit).branch));
  };
  const qreal fit = qMin(1., qMin(1060. / area.width(), 1320. / area.height()));
  auto image = canvas(qCeil(area.width() * fit), qCeil(area.height() * fit), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(fit, fit);
  const QColor paper(dark ? "#262b31" : "#ffffff"), ink(dark ? "#e9f0fb" : "#24344b");
  const std::array<QColor, 8> colors{
      QColor(dark ? "#94bfff" : "#3b77b5"), QColor(dark ? "#8dceb0" : "#357e5b"),
      QColor(dark ? "#c1a9e9" : "#8963b5"), QColor(dark ? "#e6b38e" : "#a5673c"),
      QColor(dark ? "#e0a4c0" : "#ad5980"), QColor(dark ? "#9ad0d6" : "#33818a"),
      QColor(dark ? "#d0cb90" : "#868133"), QColor(dark ? "#a9b2c7" : "#65738b")};
  painter.setFont(branchFont);
  for (int i = 0; i < graph.branches.size(); ++i) {
    const auto& branch = graph.branches.at(i);
    const auto origin = point(lead - timeStep * .35, lanes.at(i));
    const auto end = point(timeEnd, lanes.at(i));
    auto faint = colors[i];
    faint.setAlpha(dark ? 110 : 95);
    painter.setPen(QPen(faint, 1, Qt::DashLine));
    const qreal birth = branch.fork < 0 ? lead - timeStep * .35 : lead + branch.fork * timeStep;
    painter.drawLine(point(birth, lanes.at(i)), end);
    QRectF box(QPointF(), QSizeF(labelWidth, branchHeight));
    box.moveCenter(horizontal
                       ? QPointF(margin + labelWidth / 2, origin.y())
                       : origin + QPointF(0, upwards ? branchHeight + 5 : -branchHeight - 5));
    auto tint = colors[i];
    tint.setAlpha(dark ? 45 : 26);
    caption(painter, branch.name, box, ink, tint, true);
  }
  for (int i = 0; i < graph.commits.size(); ++i) {
    const auto& commit = graph.commits.at(i);
    const auto end = position(i);
    for (const int parent : {commit.parent, commit.merged}) {
      if (parent < 0) continue;
      const auto start = position(parent);
      const auto color =
          parent == commit.merged ? colors[graph.commits.at(parent).branch] : colors[commit.branch];
      painter.setPen(QPen(color, 2.2, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
      painter.setBrush(Qt::NoBrush);
      QPainterPath path(start);
      if (graph.commits.at(parent).branch == commit.branch) {
        path.lineTo(end);
      } else {
        const qreal bend = lead + (i - .5) * timeStep;
        path.lineTo(point(bend - timeStep * .22, lanes.at(graph.commits.at(parent).branch)));
        path.cubicTo(point(bend + timeStep * .1, lanes.at(graph.commits.at(parent).branch)),
                     point(bend + timeStep * .1, lanes.at(commit.branch)), end);
      }
      painter.drawPath(path);
    }
  }
  painter.setFont(font);
  for (int i = 0; i < graph.commits.size(); ++i) {
    const auto& commit = graph.commits.at(i);
    const auto at = position(i);
    const auto color = colors[commit.branch];
    painter.setPen(QPen(paper, 2));
    painter.setBrush(color);
    if (commit.type == CommitType::Highlight) {
      painter.drawRect(QRectF(at - QPointF(8, 8), QSizeF(16, 16)));
    } else {
      painter.drawEllipse(at, 7, 7);
      if (commit.type == CommitType::Reverse) {
        painter.setPen(QPen(paper, 1.8, Qt::SolidLine, Qt::RoundCap));
        painter.drawLine(at - QPointF(3, 3), at + QPointF(3, 3));
        painter.drawLine(at + QPointF(-3, 3), at + QPointF(3, -3));
      }
    }
    if (commit.merged >= 0) {
      painter.setPen(QPen(color, 1.8));
      painter.setBrush(Qt::NoBrush);
      painter.drawEllipse(at, 10, 10);
    }
    const QRectF id(at.x() - labelWidth / 2, at.y() + 13, labelWidth, rowHeight);
    caption(painter, commit.id, id, ink, paper);
    if (!commit.tags.isEmpty()) {
      const auto text = commit.tags.join(QStringLiteral(" · "));
      const QRectF tag(at.x() - labelWidth / 2, at.y() - rowHeight - 14, labelWidth, rowHeight);
      caption(painter, text, tag, ink, paper, true);
      painter.setPen(QPen(color, 1));
      painter.setBrush(Qt::NoBrush);
      const auto elided = metrics.elidedText(text.simplified(), Qt::ElideRight, labelWidth - 10);
      const qreal badgeWidth = metrics.horizontalAdvance(elided) + 10;
      painter.drawRoundedRect(
          QRectF(tag.center().x() - badgeWidth / 2, tag.top(), badgeWidth, tag.height()), 5, 5);
    }
  }
  return image;
}
}  // namespace

RenderResult gitGraph(const QStringList& lines, bool dark) {
  Graph graph;
  if (!readGraph(lines, graph)) return {{}, unsupported};
  return {drawGraph(graph, dark), {}};
}
}  // namespace typist::render::mermaid_detail
