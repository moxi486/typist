#pragma once

#include "SpecialContent.h"

#include <QFont>
#include <QPair>
#include <QPointF>
#include <QStringList>

class QPainter;

namespace typist::render::mermaid_detail {
extern const QString unsupported;
QFont diagramFont();
QImage canvas(int width, int height, bool dark);
QString label(QString text);
void arrow(QPainter& painter, QPointF a, QPointF b, bool dashed = false, bool head = true);
QList<int> graphRanks(int count, const QList<QPair<int, int>>& links);
RenderResult classDiagram(const QStringList& lines, bool dark);
RenderResult flowchart(QStringList lines, bool dark);
RenderResult ganttChart(const QStringList& lines, bool dark);
RenderResult mindmap(const QStringList& lines, bool dark);
RenderResult quadrantChart(const QStringList& lines, bool dark);
RenderResult sankey(const QStringList& lines, bool dark);
RenderResult stateDiagram(QStringList lines, bool dark);
RenderResult xyChart(const QStringList& lines, bool dark);
}  // namespace typist::render::mermaid_detail
