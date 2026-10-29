#pragma once

#include "Editing.h"
#include "SpecialContent.h"
#include "SyntaxHighlighter.h"
#include "editor_document.h"

#include <QPainter>
#include <QPointer>
#include <QResizeEvent>
#include <QScopedValueRollback>
#include <QScrollBar>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextFragment>
#include <QTimer>
#include <functional>

namespace typist::ui {
class ParagraphSourcePanel;
class CodeBlockBar;

// Keep a readable writing column while retaining native QTextEdit editing and scrolling.
class Editor : public QTextEdit {
  Q_OBJECT
 public:
  explicit Editor(QWidget* parent = nullptr) : QTextEdit(parent) {
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    setTabStopDistance(32);
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      updateDecorations();
      updateParagraphSourceGeometry();
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
      updateCodeBlockBar();
      if (m_focusMode) updateDecorations();
      if (m_typewriterMode) centerWritingLine();
    });
  }

  void configurePresentation(bool rendered) {
    finishParagraphSourceEdit(false);
    m_rendered = rendered;
    m_lastInlineBlock = -1;
    m_searchHighlights.clear();
    disconnect(m_documentConnection);
    m_documentConnection = connect(document(), &QTextDocument::contentsChanged, this, [this] {
      if (m_decorationPending) return;
      m_decorationPending = true;
      QTimer::singleShot(0, this, [this] {
        m_decorationPending = false;
        if (m_rendered) typist::render::invalidateToc(document());
        updateDecorations();
        updateCodeBlockBar();
      });
    });
    updateDecorations();
    updateCodeBlockBar(true);
    if (m_rendered) editor::refreshSpecialContent(document(), m_dark);
    render::installSyntaxHighlighter(document(), !m_rendered, m_dark, m_syntaxHighlight);
    if (m_rendered && textCursor().block().text().isEmpty() &&
        textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty))
      setCurrentCharFormat(textCursor().block().charFormat());
  }

  void setDarkTheme(bool dark) {
    m_dark = dark;
    if (m_rendered) editor::refreshSpecialContent(document(), dark);
    render::installSyntaxHighlighter(document(), !m_rendered, dark, m_syntaxHighlight);
    updateDecorations();
    viewport()->update();
    updateParagraphSourceTheme();
    updateCodeBlockTheme();
  }

  void setSearchHighlights(const QList<ExtraSelection>& highlights) {
    m_searchHighlights = highlights;
    updateDecorations();
  }

  void formatInline(editor::InlineStyle style);
  void formatParagraph(editor::ParagraphStyle style, int heading = 0);
  void insertLink(const QString& target, const QString& label = QString());
  void createTable(int rows, int columns);
  bool changeTable(editor::TableCommand command);
  void changeListIndent(int delta);
  void setFocusMode(bool enabled) {
    m_focusMode = enabled;
    updateDecorations();
  }
  void setTypewriterMode(bool enabled);
  void setAutoPair(bool enabled) { m_autoPair = enabled; }
  bool focusMode() const { return m_focusMode; }
  bool typewriterMode() const { return m_typewriterMode; }
  void zoomDocument(int steps) {
    zoomIn(steps);
    m_zoomSteps += steps;
  }
  void resetZoom() {
    zoomOut(m_zoomSteps);
    m_zoomSteps = 0;
  }
  void insertAdvancedContent(const QString& type);
  void setSyntaxHighlight(bool enabled) {
    m_syntaxHighlight = enabled;
    render::installSyntaxHighlighter(document(), !m_rendered, m_dark, enabled);
  }
  bool setCodeLanguage(const QString& language);
  bool exitCodeBlock();
  bool copyCodeBlock();
  bool startParagraphSourceEdit();
  bool flushParagraphSourceEdit(bool keepOpen = true);
  void finishParagraphSourceEdit(bool apply = true);
  bool paragraphSourceActive() const { return m_paragraphSourceActive; }
  bool undoParagraphSource(bool redo = false);
  bool paragraphSourceUndoAvailable(bool redo = false) const;
  QString markdownWithParagraphDraft() const;
  QString paragraphSourceSelection() const;
  void cut();
  void copy();
  void paste();
  void selectAll();
  void insertPlainText(const QString& text);
  void setImagePasteHandler(std::function<bool(const QMimeData*)> handler) {
    m_imagePasteHandler = std::move(handler);
  }
 signals:
  void paragraphSourceChanged();
  void paragraphSourceClosed();
  void markdownFileDropped(const QString& path);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void inputMethodEvent(QInputMethodEvent* event) override;
  void insertFromMimeData(const QMimeData* source) override;
  bool canInsertFromMimeData(const QMimeData* source) const override;
  void dragEnterEvent(QDragEnterEvent* event) override;
  void dragMoveEvent(QDragMoveEvent* event) override;
  void dropEvent(QDropEvent* event) override;
  void resizeEvent(QResizeEvent* event) override {
    if (m_updatingMargins) {
      QTextEdit::resizeEvent(event);
      return;
    }
    QScopedValueRollback<bool> guard(m_updatingMargins, true);
    // QAbstractScrollArea forwards viewport resize events here. Their width
    // already excludes the margins; feeding it back makes wide windows recurse.
    const int side = qMax(24, (width() - 860) / 2);
    const QMargins margins(side, 36, side, 28);
    if (viewportMargins() != margins) setViewportMargins(margins);
    QTextEdit::resizeEvent(event);
    updateDecorations();
    updateParagraphSourceGeometry();
    updateCodeBlockBar();
    if (m_typewriterMode) centerWritingLine();
  }

  void paintEvent(QPaintEvent* event) override {
    if (m_rendered) paintCodeBackgrounds();
    QTextEdit::paintEvent(event);
    if (!m_rendered) return;
    QPainter painter(viewport());
    const QColor border = m_dark ? QColor("#555b62") : QColor("#e8e8e8");
    int visited = 0;
    for (QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
         block.isValid() && visited++ < 200; block = block.next()) {
      QTextCursor start(block), end(block);
      end.movePosition(QTextCursor::EndOfBlock);
      const QRect first = cursorRect(start), last = cursorRect(end);
      if (first.top() > viewport()->height()) break;
      const QTextBlockFormat format = block.blockFormat();
      if (!block.text().isEmpty() && !block.textList() &&
          (format.headingLevel() == 1 || format.headingLevel() == 2)) {
        painter.setPen(QPen(border, 1));
        painter.drawLine(0, last.bottom() + 5, viewport()->width() - 1, last.bottom() + 5);
      }
      if (format.hasProperty(editor::kQuoteProperty))
        painter.fillRect(QRect(0, first.top(), 4, last.bottom() - first.top() + 4), border);
      if (format.hasProperty(editor::kRuleProperty)) {
        painter.setPen(QPen(border, 1));
        painter.drawLine(0, first.center().y(), viewport()->width() - 1, first.center().y());
      }
    }
  }

 private:
  void paintCodeBackgrounds() {
    QPainter painter(viewport());
    painter.setPen(Qt::NoPen);
    painter.setBrush(m_dark ? QColor("#2d3237") : QColor("#f7f7f7"));
    int group = 0, visited = 0;
    QRect bounds;
    const auto fill = [&] {
      if (group && bounds.isValid()) painter.drawRoundedRect(bounds.adjusted(0, -4, 0, 4), 5, 5);
    };
    for (auto block = cursorForPosition(QPoint(0, 0)).block(); block.isValid() && visited++ < 200;
         block = block.next()) {
      QTextCursor start(block), end(block);
      end.movePosition(QTextCursor::EndOfBlock);
      const QRect first = cursorRect(start), last = cursorRect(end);
      if (first.top() > viewport()->height()) break;
      const int next = block.blockFormat().intProperty(editor::kCodeBlockProperty);
      if (next != group) {
        fill();
        bounds = {};
        group = next;
      }
      if (group) {
        const QRect line(0, first.top(), viewport()->width() - 1, last.bottom() - first.top() + 1);
        bounds = bounds.isValid() ? bounds.united(line) : line;
      }
    }
    fill();
  }
  void updateParagraphSourceGeometry();
  void updateParagraphSourceTheme();
  void clearImageTypingFormat();
  void updateCodeBlockBar(bool reset = false);
  void updateCodeBlockTheme();
  CodeBlockBar* m_codeBar = nullptr;
  std::function<bool(const QMimeData*)> m_imagePasteHandler;
  ParagraphSourcePanel* m_paragraphPanel = nullptr;
  bool m_paragraphSourceActive = false;
  bool m_flushingParagraph = false;
  QTextCursor m_paragraphRange;
  QPointer<QTextDocument> m_paragraphDocument;
  QString m_paragraphCommitted;
  void editAdvancedSource(const QString& type, const QString& source, QTextCursor cursor);
  void updateDecorations() {
    if (m_updatingDecorations) return;
    QScopedValueRollback<bool> guard(m_updatingDecorations, true);
    QList<ExtraSelection> selections;
    if (m_rendered) {
      int visited = 0;
      for (QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
           block.isValid() && visited++ < 200; block = block.next()) {
        if (cursorRect(QTextCursor(block)).top() > viewport()->height()) break;
        const QTextBlockFormat format = block.blockFormat();
        const bool code = format.hasProperty(editor::kCodeBlockProperty);
        if (code || format.hasProperty(editor::kQuoteProperty) ||
            format.hasProperty(editor::kRuleProperty)) {
          ExtraSelection selection;
          selection.cursor = QTextCursor(block);
          selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
          if (code) {
            selection.cursor.clearSelection();
            selection.format.setBackground(m_dark ? QColor("#2d3237") : QColor("#f7f7f7"));
            selection.format.setProperty(QTextFormat::FullWidthSelection, true);
          } else if (format.hasProperty(editor::kRuleProperty)) {
            selection.format.setForeground(Qt::transparent);
          } else {
            selection.format.setForeground(m_dark ? QColor("#aab0b6") : QColor("#777777"));
          }
          selections.append(selection);
        }
        if (code) continue;
        for (auto it = block.begin(); !it.atEnd(); ++it) {
          const QTextFragment fragment = it.fragment();
          if (!fragment.isValid()) continue;
          const QTextCharFormat character = fragment.charFormat();
          const bool inlineCode =
              character.fontFamilies().toStringList().contains(QStringLiteral("Consolas"));
          if (!inlineCode && !character.isAnchor()) continue;
          ExtraSelection selection;
          selection.cursor = QTextCursor(document());
          selection.cursor.setPosition(fragment.position());
          selection.cursor.setPosition(fragment.position() + fragment.length(),
                                       QTextCursor::KeepAnchor);
          if (inlineCode)
            selection.format.setBackground(m_dark ? QColor("#2d3237") : QColor("#f3f3f3"));
          if (character.isAnchor())
            selection.format.setForeground(m_dark ? QColor("#8ab9d8") : QColor("#4183c4"));
          selections.append(selection);
        }
      }
    }
    if (m_focusMode) {
      int visited = 0;
      for (QTextBlock block = cursorForPosition(QPoint(0, 0)).block();
           block.isValid() && visited++ < 200; block = block.next()) {
        if (cursorRect(QTextCursor(block)).top() > viewport()->height()) break;
        if (block == textCursor().block()) continue;
        ExtraSelection selection;
        selection.cursor = QTextCursor(block);
        selection.cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        selection.format.setForeground(m_dark ? QColor("#72787e") : QColor("#bbbbbb"));
        selections.append(selection);
      }
    }
    selections.append(m_searchHighlights);
    setExtraSelections(selections);
  }

  bool m_updatingMargins = false;
  bool m_updatingDecorations = false;
  bool m_decorationPending = false;
  bool m_rendered = true;
  bool m_dark = false;
  bool m_focusMode = false;
  bool m_typewriterMode = false;
  bool m_centering = false;
  bool m_autoPair = true;
  bool m_syntaxHighlight = true;
  QString m_lastInlineText;
  int m_lastInlineBlock = -1;
  int m_zoomSteps = 0;
  void centerWritingLine();
  bool tryRenderInline();
  QMetaObject::Connection m_documentConnection;
  QList<ExtraSelection> m_searchHighlights;
};
}  // namespace typist::ui
