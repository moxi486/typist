#pragma once

#include <QString>

class QTextDocument;

namespace typist::platform {

inline constexpr int kMinImageWidth = 320;
inline constexpr int kMaxImageWidth = 2400;
inline constexpr int kMinImageFont = 12;
inline constexpr int kMaxImageFont = 48;
inline constexpr int kMaxImageScale = 3;

struct ImageExportOptions {
  int width = 640;
  int fontPixels = 24;
  int scale = 1;
  enum class Theme { Current, Light, Dark };
  Theme theme = Theme::Current;
};

enum class ImageExportError { None, InvalidOptions, ContentTooWide, ImageTooLarge, Render, Write };

// Paint a detached Markdown document as one PNG. The caller builds special
// content with the resolved theme. The target is replaced only after encoding.
ImageExportError exportImage(const QString& path, const QTextDocument& document,
                             const ImageExportOptions& options, bool dark);

}  // namespace typist::platform
