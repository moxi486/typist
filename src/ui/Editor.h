#pragma once

#include "CodeEditing.h"
#include "Editing.h"
#include "ListEditing.h"
#include "Settings.h"
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
class CodeLineNumberGutter;
class TableBar;

// Keep a readable writing column while retaining native QTextEdit editing and scrolling.
class Editor : public QTextEdit {
  Q_OBJECT
 public:
  explicit Editor(QWidget* parent = nullptr) : QTextEdit(parent) {
    setFrameShape(QFrame::NoFrame);
    setTabChangesFocus(false);
    updateCodeTabStops();
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this, [this] {
      updateDecorations();
      updateParagraphSourceGeometry();
    });
    connect(this, &QTextEdit::cursorPositionChanged, this, [this] {
      updateCodeBlockBar();
      updateTableBar();
      if (m_focusMode) updateDecorations();
      if (m_typewriterMode) centerWritingLine();
    });
  }

  void configurePresentation(bool rendered) {
    finishParagraphSourceEdit(false);
    const bool viewChanged = m_rendered != rendered;
    m_rendered = rendered;
    updateCodeTabStops();
    m_lastInlineBlock = -1;
    m_searchHighlights.clear();
    disconnect(m_documentConnection);
    m_documentConnection = connect(document(), &QTextDocument::contentsChanged, this, [this] {
      if (m_decorationPending) return;
      m_decorationPending = true;
      QTimer::singleShot(0, this, [this] {
        m_decorationPending = false;
        if (m_rendered) typist::render::invalidateToc(document());
        if (m_rendered && m_codeLineNumbers) updateWritingArea();
        updateDecorations();
        updateCodeBlockBar();
        updateTableBar();
      });
    });
    if (viewChanged || m_codeLineNumbers) updateWritingArea();
    updateDecorations();
    updateCodeBlockBar(true);
    updateTableBar();
    if (m_rendered) editor::refreshSpecialContent(document(), m_dark);
    render::installSyntaxHighlighter(document(), !m_rendered, m_dark, m_syntaxHighlight);
    if (m_rendered && textCursor().block().text().isEmpty() &&
        textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty))
      setCurrentCharFormat(textCursor().block().charFormat());
    if (viewChanged) emit renderedEditingAvailableChanged(renderedEditingAvailable());
  }

  void setDarkTheme(bool dark) {
    m_dark = dark;
    if (m_rendered) editor::refreshSpecialContent(document(), dark);
    render::installSyntaxHighlighter(document(), !m_rendered, dark, m_syntaxHighlight);
    updateDecorations();
    viewport()->update();
    updateParagraphSourceTheme();
    updateCodeBlockTheme();
    updateTableTheme();
  }

  void setSearchHighlights(const QList<ExtraSelection>& highlights) {
    m_searchHighlights = highlights;
    updateDecorations();
  }

  void formatInline(editor::InlineStyle style);
  void formatParagraph(editor::ParagraphStyle style, int heading = 0);
  bool renderedEditingAvailable() const { return m_rendered && !m_paragraphSourceActive; }
  bool shiftHeadingLevels(int delta);
  void insertLink(const QString& target, const QString& label = QString());
  void createTable(int rows, int columns);
  bool changeTable(editor::TableCommand command);
  void changeListIndent(int delta);
  bool changeTaskStatus(editor::TaskStatus status);
  bool openCurrentLink();
  bool copyHeadingLink();
  bool jumpToHeading(const QString& fragment);
  void setFocusMode(bool enabled) {
    m_focusMode = enabled;
    updateDecorations();
  }
  void setTypewriterMode(bool enabled);
  void setWritingAreaWidths(const platform::WritingAreaWidths& widths);
  void setAutoPair(bool enabled) { m_autoPair = enabled; }
  void setCopyCutWholeLine(bool enabled) { m_copyCutWholeLine = enabled; }
  bool focusMode() const { return m_focusMode; }
  bool typewriterMode() const { return m_typewriterMode; }
  void zoomDocument(int steps) {
    zoomIn(steps);
    m_zoomSteps += steps;
    updateCodeTabStops();
  }
  void resetZoom() {
    zoomOut(m_zoomSteps);
    m_zoomSteps = 0;
    updateCodeTabStops();
  }
  void insertAdvancedContent(const QString& type);
  bool insertPageBreak();
  void setSyntaxHighlight(bool enabled) {
    m_syntaxHighlight = enabled;
    render::installSyntaxHighlighter(document(), !m_rendered, m_dark, enabled);
  }
  void setCodeLineNumbers(bool enabled);
  int codeIndentSize() const { return m_codeIndentSize; }
  void setCodeIndentSize(int columns);
  void setAutoIndentOnBacktab(bool enabled) { m_autoIndentOnBacktab = enabled; }
  enum class CodeFenceCreation { Command, Typed };
  void setDefaultCodeLanguageResolver(std::function<QString(CodeFenceCreation)> resolver) {
    m_defaultCodeLanguage = std::move(resolver);
  }
  bool setCodeLanguage(const QString& language);
  bool exitCodeBlock();
  bool copyCodeBlock();
  bool autoIndentCode();
  bool copyTable(bool spreadsheet = false);
  bool exitTable();
  bool pasteTableText(const QString& text);
  bool startParagraphSourceEdit();
  bool flushParagraphSourceEdit(bool keepOpen = true);
  void finishParagraphSourceEdit(bool apply = true);
  bool paragraphSourceActive() const { return m_paragraphSourceActive; }
  // Modal navigation must not apply the draft through deferred focus-out events.
  // Returns the previous state; re-enabling restores focus to an active draft.
  bool setParagraphSourceFocusCommitEnabled(bool enabled);
  bool undoParagraphSource(bool redo = false);
  bool paragraphSourceUndoAvailable(bool redo = false) const;
  QString markdownWithParagraphDraft() const;
  QString paragraphSourceSelection() const;
  void cut();
  void copy();
  enum class CopyFormat { Markdown, PlainText, HtmlCode, WithoutTheme };
  void copySelection(CopyFormat format) const;
  void paste();
  void selectAll();
  void selectRange(editor::SelectionRange range);
  bool deleteRange(editor::SelectionRange range);
  void moveCaret(QTextCursor::MoveOperation operation);
  void revealSelection();
  void insertPlainText(const QString& text);
  void setImagePasteHandler(std::function<bool(const QMimeData*)> handler) {
    m_imagePasteHandler = std::move(handler);
  }
 signals:
  void codeLanguageChosen(const QString& language);
  void renderedEditingAvailableChanged(bool available);
  void paragraphSourceChanged();
  void paragraphSourceClosed();
  void markdownFileDropped(const QString& path);
  void tablePasteRejected(const QString& error);
  void linkActivated(const QString& target);
  void editLinkRequested();
  void imageFileStatus(const QString& message);

 protected:
  void keyPressEvent(QKeyEvent* event) override;
  void mouseDoubleClickEvent(QMouseEvent* event) override;
  void mouseReleaseEvent(QMouseEvent* event) override;
  void mousePressEvent(QMouseEvent* event) override;
  void contextMenuEvent(QContextMenuEvent* event) override;
  bool viewportEvent(QEvent* event) override;
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
    updateWritingArea();
    QTextEdit::resizeEvent(event);
    updateDecorations();
    updateParagraphSourceGeometry();
    updateCodeBlockBar();
    updateTableBar();
    if (m_typewriterMode) centerWritingLine();
  }

  void paintEvent(QPaintEvent* event) override;

 private:
  void addImageFileActions(QMenu* menu, const QContextMenuEvent& event);
  void saveImageResource(QTextCursor image, bool relink);
  QString selectedPlainText() const;
  bool copyOrCutCurrentLine(bool cut);
  bool copyCodeBlock(const QTextCursor& cursor);
  bool autoIndentCode(QTextCursor cursor);
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
  void updateCodeTabStops();
  QString defaultCodeLanguage(CodeFenceCreation creation) const;
  void updateWritingArea();
  void updateTableBar();
  void updateTableTheme();
  TableBar* m_tableBar = nullptr;
  CodeBlockBar* m_codeBar = nullptr;
  CodeLineNumberGutter* m_codeLineNumberGutter = nullptr;
  std::function<bool(const QMimeData*)> m_imagePasteHandler;
  std::function<QString(CodeFenceCreation)> m_defaultCodeLanguage;
  ParagraphSourcePanel* m_paragraphPanel = nullptr;
  bool m_paragraphSourceActive = false;
  bool m_paragraphSourceFocusCommitEnabled = true;
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
  bool m_copyCutWholeLine = false;
  bool m_syntaxHighlight = true;
  bool m_codeLineNumbers = false;
  platform::WritingAreaWidths m_writingAreaWidths;
  bool m_autoIndentOnBacktab = false;
  int m_codeIndentSize = editor::kDefaultCodeIndentSize;
  QString m_lastInlineText;
  int m_lastInlineBlock = -1;
  int m_zoomSteps = 0;
  QPoint m_linkPressPosition;
  QString m_pressedLink;
  void centerWritingLine();
  bool tryRenderInline();
  QMetaObject::Connection m_documentConnection;
  QList<ExtraSelection> m_searchHighlights;
};
}  // namespace typist::ui
