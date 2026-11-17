#pragma once

#include "SpecialContent.h"

#include <QFont>
#include <QStringList>

namespace typist::render::mermaid_detail {
QFont diagramFont();
QImage canvas(int width, int height, bool dark);
RenderResult xyChart(const QStringList& lines, bool dark);
}  // namespace typist::render::mermaid_detail
