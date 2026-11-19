#pragma once

#include <QColor>
#include <QList>
#include <QPen>
#include <QString>
#include <optional>

namespace typist::render::mermaid_detail {
struct GraphStyle {
  QColor fill, stroke, color;
  std::optional<qreal> width;
  std::optional<QList<qreal>> dashes;
};

void mergeStyle(GraphStyle& target, const GraphStyle& source);
bool parseStyle(const QString& text, GraphStyle& style);
QPen graphPen(const GraphStyle& style, const QColor& color, qreal defaultWidth,
              Qt::PenStyle defaultPattern = Qt::SolidLine);
}  // namespace typist::render::mermaid_detail
