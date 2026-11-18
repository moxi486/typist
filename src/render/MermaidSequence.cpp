#include "MermaidInternal.h"

#include <QFontMetricsF>
#include <QHash>
#include <QPainter>
#include <QRegularExpression>
#include <QtMath>

namespace typist::render::mermaid_detail {
namespace {
enum class EventType { Message, Note, Open, Divide, Close, Activate, Deactivate };
constexpr qreal activationWidth = 12, activationStep = 6;

struct Participant {
  QString name;
  bool actor = false;
};

struct Event {
  EventType type;
  QString text;
  int from = -1, to = -1, frame = -1, number = 0, activation = -1;
  int fromDepth = 0, toDepth = 0;
  bool dashed = false;
  QRectF rect{};
};

struct Frame {
  QString kind;
  int depth;
  bool hasElse = false;
  QRectF rect;
};

struct Activation {
  int participant, depth;
  qreal top = 0, bottom = 0;
};

struct Sequence {
  QList<Participant> participants;
  QList<Event> events;
  QList<Frame> frames;
  QList<Activation> activations;
};

bool parseSequence(const QStringList& lines, Sequence& sequence) {
  static const QString id = QStringLiteral(R"([\p{L}\p{N}_]+)");
  static const QRegularExpression declaration(
      QStringLiteral(R"(^(participant|actor)\s+(%1)(?:\s+as\s+(.+))?$)").arg(id));
  static const QRegularExpression message(
      QStringLiteral(R"(^(%1)\s*(-->>|->>|-->|->)\s*([+-])?\s*(%1)\s*:\s*(.*)$)").arg(id));
  static const QRegularExpression activation(
      QStringLiteral(R"(^(activate|deactivate)\s+(%1)$)").arg(id));
  static const QRegularExpression note(
      QStringLiteral(R"(^Note\s+over\s+(%1)(?:\s*,\s*(%1))?\s*:\s*(.*)$)").arg(id),
      QRegularExpression::CaseInsensitiveOption);
  static const QRegularExpression control(R"(^(alt|opt|loop|par|else|and)(?:\s+(.*))?$)");
  QHash<QString, int> ids;
  QList<int> stack;
  QList<QList<int>> active;
  int count = 0, number = 0;
  bool numbered = false;
  const auto participant = [&](const QString& token) {
    const auto found = ids.constFind(token);
    if (found != ids.cend()) return *found;
    const int index = int(sequence.participants.size());
    sequence.participants.append({token, false});
    active.append(QList<int>());
    ids.insert(token, index);
    return index;
  };
  const auto activate = [&](int index, bool start) {
    auto& levels = active[index];
    if ((start && (levels.size() >= 8 || sequence.activations.size() >= 64)) ||
        (!start && levels.isEmpty()))
      return false;
    Event event{start ? EventType::Activate : EventType::Deactivate, {}};
    if (start) {
      event.activation = int(sequence.activations.size());
      sequence.activations.append({index, int(levels.size())});
      if (!sequence.events.isEmpty() && sequence.events.last().type == EventType::Message &&
          sequence.events.last().to == index)
        sequence.events.last().toDepth = int(levels.size()) + 1;
      levels.append(event.activation);
    } else {
      event.activation = levels.takeLast();
    }
    sequence.events.append(event);
    return true;
  };
  for (qsizetype i = 1; i < lines.size(); ++i) {
    const auto line = lines.at(i).trimmed();
    if (line.startsWith("%%{")) return false;
    if (line.isEmpty() || line.startsWith("%%")) continue;
    if (line == "autonumber") {
      numbered = true;
    } else if (const auto declared = declaration.match(line); declared.hasMatch()) {
      const int index = participant(declared.captured(2));
      auto& entry = sequence.participants[index];
      entry.actor = declared.captured(1) == "actor";
      if (declared.capturedStart(3) >= 0) entry.name = label(declared.captured(3));
    } else if (const auto bar = activation.match(line); bar.hasMatch()) {
      const int index = participant(bar.captured(2));
      if (!activate(index, bar.captured(1) == "activate")) return false;
    } else if (const auto m = message.match(line); m.hasMatch()) {
      const int from = participant(m.captured(1)), to = participant(m.captured(4));
      Event event{EventType::Message, label(m.captured(5))};
      event.from = from;
      event.to = to;
      event.fromDepth = int(active.at(from).size());
      event.toDepth = int(active.at(to).size()) + (m.captured(3) == "+" ? 1 : 0);
      event.dashed = m.captured(2).startsWith("--");
      event.number = numbered ? ++number : 0;
      sequence.events.append(event);
      if ((m.captured(3) == "+" && !activate(to, true)) ||
          (m.captured(3) == "-" && !activate(from, false)))
        return false;
      ++count;
    } else if (const auto n = note.match(line); n.hasMatch()) {
      Event event{EventType::Note, label(n.captured(3))};
      event.from = participant(n.captured(1));
      event.to = n.capturedStart(2) < 0 ? event.from : participant(n.captured(2));
      sequence.events.append(event);
      ++count;
    } else if (const auto block = control.match(line); block.hasMatch()) {
      const auto kind = block.captured(1), text = label(block.captured(2));
      const bool divider = kind == "else" || kind == "and";
      if (divider) {
        if (stack.isEmpty()) return false;
        auto& frame = sequence.frames[stack.last()];
        if ((kind == "else" && (frame.kind != "alt" || frame.hasElse)) ||
            (kind == "and" && frame.kind != "par"))
          return false;
        frame.hasElse = kind == "else";
        Event event{EventType::Divide, text};
        event.frame = stack.last();
        sequence.events.append(event);
      } else {
        if (stack.size() >= 6 || sequence.frames.size() >= 24) return false;
        const int frame = int(sequence.frames.size());
        sequence.frames.append({kind, int(stack.size()), false, {}});
        stack.append(frame);
        Event event{EventType::Open, text};
        event.frame = frame;
        sequence.events.append(event);
      }
    } else if (line == "end") {
      if (stack.isEmpty()) return false;
      Event event{EventType::Close, {}};
      event.frame = stack.takeLast();
      sequence.events.append(event);
    } else {
      return false;
    }
    if (sequence.participants.size() > 8 || count > 60 || sequence.events.size() > 360)
      return false;
  }
  return stack.isEmpty() && !sequence.participants.isEmpty();
}

qreal textHeight(const QFontMetricsF& metrics, const QString& text, qreal width) {
  const auto rect =
      metrics.boundingRect(QRectF(0, 0, width, 0), Qt::TextWordWrap | Qt::TextWrapAnywhere, text);
  const qreal row = qMax(20., metrics.height());
  const qreal minimum = row * qMin(qsizetype(4), text.count('\n') + 1);
  return qBound(minimum, rect.height(), row * 4);
}

struct Layout {
  qreal width, height, header;
  QList<qreal> centers;
};

QPair<qreal, qreal> messageEndpoints(const Event& event, const Layout& layout) {
  const qreal a = layout.centers.at(event.from), b = layout.centers.at(event.to);
  const bool self = event.from == event.to, forward = a < b;
  const auto port = [](qreal center, int depth, bool right) {
    if (!depth) return center;
    return center + (depth - 1) * activationStep + (right ? 1 : -1) * activationWidth / 2;
  };
  return {port(a, event.fromDepth, self || forward), port(b, event.toDepth, self || !forward)};
}

Layout arrange(Sequence& sequence, const QFontMetricsF& metrics) {
  constexpr qreal column = 210, margin = 42;
  qreal extra = 0;
  for (const auto& bar : sequence.activations)
    extra = qMax(extra, bar.depth * activationStep + activationWidth / 2);
  Layout layout{
      qMax(360., sequence.participants.size() * column + margin * 2 + 32 + extra), 0, 78, {}};
  for (int i = 0; i < sequence.participants.size(); ++i) {
    const auto& entry = sequence.participants.at(i);
    layout.centers.append(margin + column * (i + .5));
    layout.header =
        qMax(layout.header, textHeight(metrics, entry.name, 158) + (entry.actor ? 75 : 36));
  }
  qreal y = layout.header + 18;
  qreal anchor = layout.header + 6;
  for (auto& event : sequence.events) {
    if (event.type == EventType::Activate || event.type == EventType::Deactivate) {
      auto& bar = sequence.activations[event.activation];
      (event.type == EventType::Activate ? bar.top : bar.bottom) = anchor;
    } else if (event.type == EventType::Open || event.type == EventType::Divide) {
      auto& frame = sequence.frames[event.frame];
      const qreal inset = 14 + frame.depth * 10;
      if (event.type == EventType::Open) {
        frame.rect = QRectF(inset, y, layout.width - inset * 2, 0);
      }
      const qreal height = textHeight(metrics, event.text, frame.rect.width() - 94) + 14;
      event.rect = QRectF(frame.rect.left(), y, frame.rect.width(), height);
      anchor = event.rect.bottom();
      y += height + 5;
    } else if (event.type == EventType::Close) {
      y += 12;
      sequence.frames[event.frame].rect.setBottom(y);
      anchor = y;
      y += 10;
    } else {
      const bool note = event.type == EventType::Note, self = event.from == event.to;
      const auto endpoints =
          note ? QPair<qreal, qreal>(layout.centers.at(event.from), layout.centers.at(event.to))
               : messageEndpoints(event, layout);
      const auto [a, b] = endpoints;
      const qreal width = note ? qMax(150., qAbs(a - b) + 140) : self ? 150 : qAbs(a - b) - 18;
      const QString text =
          event.number ? QString::number(event.number) + ". " + event.text : event.text;
      const qreal height = textHeight(metrics, text, width - (note ? 14 : 0)) + (note ? 16 : 5);
      event.rect = QRectF(note   ? qMin(a, b) - 70
                          : self ? qMax(a, b) + 18
                                 : qMin(a, b) + 9,
                          y, width, height);
      anchor = event.rect.bottom() + (note ? 0 : self ? 30 : 6);
      y += height + (note ? 17 : self ? 44 : 28);
    }
  }
  layout.height = y + 16;
  for (auto& bar : sequence.activations)
    if (!bar.bottom) bar.bottom = layout.height - 20;
  return layout;
}

void frameHeader(QPainter& painter, const Frame& frame, const Event& event, const QColor& ink,
                 const QColor& stroke, const QColor& panel) {
  const auto rect = event.rect;
  painter.fillRect(rect.adjusted(1, 1, -1, 0), panel);
  painter.setPen(QPen(stroke, 1.2, event.type == EventType::Divide ? Qt::DashLine : Qt::SolidLine));
  painter.drawLine(rect.topLeft(), rect.topRight());
  if (event.type == EventType::Open) {
    const qreal tabWidth =
        qMax(51., QFontMetricsF(painter.font()).horizontalAdvance(frame.kind) + 18);
    const QRectF tab(rect.topLeft(), QSizeF(tabWidth, rect.height()));
    painter.setPen(QPen(stroke, 1));
    painter.drawPolyline(
        QPolygonF({tab.topRight(), tab.bottomRight() - QPointF(7, 0), tab.bottomLeft()}));
    painter.setPen(ink);
    painter.drawText(tab.adjusted(3, 0, -8, 0), Qt::AlignCenter, frame.kind);
  }
  painter.setPen(ink);
  painter.drawText(rect.adjusted(65, 5, -10, -5),
                   Qt::AlignLeft | Qt::AlignVCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere,
                   event.text.isEmpty() ? QString() : QStringLiteral("[") + event.text + ']');
}

QImage drawSequence(Sequence& sequence, bool dark) {
  const auto font = diagramFont();
  const QFontMetricsF metrics(font);
  const auto layout = arrange(sequence, metrics);
  const qreal scale = qMin(1., qMin(940. / layout.width, 1400. / layout.height));
  auto image = canvas(qCeil(layout.width * scale), qCeil(layout.height * scale), dark);
  if (image.isNull()) return image;
  QPainter painter(&image);
  painter.setRenderHints(QPainter::Antialiasing | QPainter::TextAntialiasing);
  painter.scale(scale, scale);
  painter.setFont(font);
  const QColor ink(dark ? "#e4ebf5" : "#34455e"), stroke(dark ? "#8ba4c5" : "#7e9cc3"),
      panel(dark ? "#344359" : "#edf3fc"), note(dark ? "#574e32" : "#fff5d0");
  for (const auto& frame : sequence.frames) {
    auto tint = panel;
    tint.setAlpha(dark ? 30 : 55);
    painter.fillRect(frame.rect, tint);
  }
  for (int i = 0; i < sequence.participants.size(); ++i) {
    const auto& entry = sequence.participants.at(i);
    const qreal x = layout.centers.at(i);
    painter.setPen(QPen(stroke, 1, Qt::DashLine));
    painter.drawLine(QPointF(x, layout.header), QPointF(x, layout.height - 20));
    const qreal height = textHeight(metrics, entry.name, 158);
    painter.setPen(QPen(stroke, 1.4));
    painter.setBrush(panel);
    if (entry.actor) {
      painter.drawEllipse(QPointF(x, 26), 8, 8);
      painter.drawLine(QPointF(x, 34), QPointF(x, 51));
      painter.drawLine(QPointF(x - 14, 40), QPointF(x + 14, 40));
      painter.drawLine(QPointF(x, 51), QPointF(x - 12, 65));
      painter.drawLine(QPointF(x, 51), QPointF(x + 12, 65));
    } else {
      painter.drawRoundedRect(QRectF(x - 82, 18, 164, height + 14), 6, 6);
    }
    painter.setPen(ink);
    painter.drawText(QRectF(x - 79, entry.actor ? 71 : 25, 158, height),
                     Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, entry.name);
  }
  for (const auto& frame : sequence.frames) {
    painter.setPen(QPen(stroke, 1.2));
    painter.setBrush(Qt::NoBrush);
    painter.drawRect(frame.rect);
  }
  for (const auto& bar : sequence.activations) {
    const qreal x = layout.centers.at(bar.participant) + bar.depth * activationStep;
    painter.setPen(QPen(stroke, 1.2));
    painter.setBrush(dark ? QColor("#56759d") : QColor("#d8e7ff"));
    painter.drawRect(
        QRectF(x - activationWidth / 2, bar.top, activationWidth, qMax(10., bar.bottom - bar.top)));
  }
  for (const auto& event : sequence.events) {
    if (event.type == EventType::Close || event.type == EventType::Activate ||
        event.type == EventType::Deactivate)
      continue;
    if (event.type == EventType::Open || event.type == EventType::Divide) {
      frameHeader(painter, sequence.frames.at(event.frame), event, ink, stroke, panel);
      continue;
    }
    painter.setPen(QPen(ink, 1.5));
    if (event.type == EventType::Note) {
      painter.setPen(QPen(stroke, 1));
      painter.setBrush(note);
      painter.drawRoundedRect(event.rect, 3, 3);
      painter.setPen(ink);
      painter.drawText(event.rect.adjusted(7, 5, -7, -5),
                       Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, event.text);
    } else {
      const auto [a, b] = messageEndpoints(event, layout);
      const qreal y = event.rect.bottom() + 6;
      if (event.from == event.to) {
        const qreal turn = qMax(a, b) + 56;
        painter.setPen(QPen(ink, 1.5, event.dashed ? Qt::DashLine : Qt::SolidLine));
        painter.drawPolyline(QPolygonF({{a, y}, {turn, y}, {turn, y + 24}}));
        arrow(painter, {turn, y + 24}, {b, y + 24}, event.dashed);
      } else {
        arrow(painter, {a, y}, {b, y}, event.dashed);
      }
      const auto text =
          event.number ? QString::number(event.number) + ". " + event.text : event.text;
      painter.setPen(ink);
      painter.drawText(event.rect, Qt::AlignCenter | Qt::TextWordWrap | Qt::TextWrapAnywhere, text);
    }
  }
  return image;
}
}  // namespace

RenderResult sequenceDiagram(const QStringList& lines, bool dark) {
  Sequence sequence;
  if (!parseSequence(lines, sequence)) return {{}, unsupported};
  return {drawSequence(sequence, dark), {}};
}
}  // namespace typist::render::mermaid_detail
