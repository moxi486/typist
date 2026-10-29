#include "Editor.h"

#include "CodeBlockBar.h"
#include "CodeEditing.h"
#include "ParagraphSourcePanel.h"
#include "TableBar.h"
#include "TableEditing.h"
#include "markdown.h"

#include <QApplication>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QInputDialog>
#include <QInputMethodEvent>
#include <QKeyEvent>
#include <QLabel>
#include <QMimeData>
#include <QMouseEvent>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScrollArea>
#include <QSplitter>
#include <QTextList>
#include <QTextTable>
#include <QVBoxLayout>
#include <QtMath>

namespace {
QString editorText(const char* text) { return QCoreApplication::translate("Editor", text); }
QString advancedMarkdown(const QString& type, const QString& source) {
  if (type == QLatin1String("inline-math"))
    return QStringLiteral("$") + source + QStringLiteral("$");
  if (type == QLatin1String("math"))
    return QStringLiteral("$$\n") + source + QStringLiteral("\n$$\n\n");
  if (type == QLatin1String("toc")) return QStringLiteral("[TOC]\n\n");
  return QStringLiteral("```mermaid\n") + source +
         (source.endsWith(QLatin1Char('\n')) ? QString() : QStringLiteral("\n")) +
         QStringLiteral("```\n\n");
}
}  // namespace

namespace typist::ui {
bool Editor::startParagraphSourceEdit() {
  if (m_paragraphSourceActive) {
    finishParagraphSourceEdit();
    return true;
  }
  const auto block = textCursor().block();
  const auto format = block.blockFormat();
  if (!m_rendered || textCursor().currentTable() || block.textList() ||
      format.hasProperty(editor::kCodeBlockProperty) ||
      format.hasProperty(editor::kQuoteProperty) ||
      format.hasProperty(editor::kSpecialBlockProperty) ||
      format.hasProperty(editor::kFootnoteDefinitionProperty) ||
      format.hasProperty(editor::kRuleProperty))
    return false;
  QString source = editor::inlineMarkdown(block);
  if (format.headingLevel() > 0) source.prepend(QString(format.headingLevel(), '#') + ' ');
  if (source.size() > 32768) return false;
  if (!m_paragraphPanel) {
    m_paragraphPanel = new ParagraphSourcePanel(viewport());
    connect(m_paragraphPanel, &ParagraphSourcePanel::draftChanged, this, [this] {
      if (!m_paragraphSourceActive) return;
      updateParagraphSourceGeometry();
      emit paragraphSourceChanged();
    });
    connect(m_paragraphPanel, &ParagraphSourcePanel::applyRequested, this,
            [this] { finishParagraphSourceEdit(); });
    connect(m_paragraphPanel, &ParagraphSourcePanel::cancelRequested, this,
            [this] { finishParagraphSourceEdit(false); });
    connect(m_paragraphPanel, &ParagraphSourcePanel::imagePasteRequested, this,
            [this] { paste(); });
    connect(m_paragraphPanel, &ParagraphSourcePanel::focusLeft, this, [this] {
      QTextCursor destination = textCursor();
      finishParagraphSourceEdit();
      if (destination.document() == document()) setTextCursor(destination);
    });
  }
  m_paragraphRange = QTextCursor(block);
  m_paragraphRange.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
  m_paragraphDocument = document();
  m_paragraphCommitted = source;
  m_paragraphPanel->setSource(source);
  m_paragraphSourceActive = true;
  m_paragraphPanel->setDark(m_dark);
  m_paragraphPanel->show();
  updateParagraphSourceGeometry();
  m_paragraphPanel->raise();
  m_paragraphPanel->input()->setFocus();
  auto caret = m_paragraphPanel->input()->textCursor();
  caret.movePosition(QTextCursor::End);
  m_paragraphPanel->input()->setTextCursor(caret);
  return true;
}
bool Editor::flushParagraphSourceEdit(bool keepOpen) {
  if (!m_paragraphSourceActive || m_flushingParagraph) return true;
  QScopedValueRollback<bool> guard(m_flushingParagraph, true);
  if (m_paragraphDocument != document()) {
    finishParagraphSourceEdit(false);
    return true;
  }
  const QString source = m_paragraphPanel->source();
  if (source != m_paragraphCommitted) {
    const auto parsed = core::parseMarkdown(source.toStdString());
    if (!parsed.ok) return false;
    const int start = m_paragraphRange.selectionStart();
    QTextCursor cursor = m_paragraphRange;
    cursor.beginEditBlock();
    if (parsed.blocks.empty()) {
      cursor.removeSelectedText();
      cursor.setBlockFormat(QTextBlockFormat());
      cursor.setCharFormat(QTextCharFormat());
    } else {
      QString error;
      if (!editor::insertMarkdownAtCursor(source, cursor, &error)) {
        cursor.endEditBlock();
        return false;
      }
      if (parsed.blocks.size() == 1 && parsed.blocks.front().type == core::BlockType::Paragraph)
        editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
    }
    const int end = cursor.position();
    cursor.endEditBlock();
    m_paragraphRange = QTextCursor(document());
    m_paragraphRange.setPosition(start);
    m_paragraphRange.setPosition(end, QTextCursor::KeepAnchor);
    m_paragraphCommitted = source;
    if (parsed.blocks.size() != 1 || (parsed.blocks.front().type != core::BlockType::Paragraph &&
                                      parsed.blocks.front().type != core::BlockType::Heading))
      keepOpen = false;
    updateDecorations();
  }
  if (!keepOpen) {
    const bool focused = m_paragraphPanel->input()->hasFocus();
    QTextCursor caret = m_paragraphRange;
    caret.setPosition(caret.selectionEnd());
    m_paragraphSourceActive = false;
    m_paragraphPanel->hide();
    m_paragraphDocument.clear();
    setTextCursor(caret);
    if (!caret.blockFormat().hasProperty(editor::kCodeBlockProperty)) {
      QTextCharFormat typing;
      const int heading = caret.blockFormat().headingLevel();
      if (heading > 0) {
        static const double sizes[] = {28, 22, 18, 15, 13, 12};
        typing.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
        typing.setFontWeight(QFont::Bold);
        typing.setProperty(editor::kPresentationBoldProperty, true);
      }
      setCurrentCharFormat(typing);
    }
    if (focused) setFocus();
    emit paragraphSourceClosed();
  } else
    updateParagraphSourceGeometry();
  return true;
}
void Editor::finishParagraphSourceEdit(bool apply) {
  if (!m_paragraphSourceActive) return;
  if (apply) {
    flushParagraphSourceEdit(false);
    return;
  }
  const bool focused = m_paragraphPanel->input()->hasFocus();
  m_paragraphSourceActive = false;
  m_paragraphPanel->hide();
  m_paragraphDocument.clear();
  if (focused) setFocus();
  emit paragraphSourceClosed();
}
bool Editor::undoParagraphSource(bool redo) {
  if (!m_paragraphSourceActive) return false;
  if (redo)
    m_paragraphPanel->input()->redo();
  else
    m_paragraphPanel->input()->undo();
  return true;
}
bool Editor::paragraphSourceUndoAvailable(bool redo) const {
  return m_paragraphSourceActive &&
         (redo ? m_paragraphPanel->input()->document()->isRedoAvailable()
               : m_paragraphPanel->input()->document()->isUndoAvailable());
}
QString Editor::markdownWithParagraphDraft() const {
  return m_paragraphSourceActive && m_paragraphDocument == document()
             ? editor::serializeDocument(document(), m_paragraphRange.selectionStart(),
                                         m_paragraphPanel->source())
             : editor::serializeDocument(document());
}
QString Editor::paragraphSourceSelection() const {
  return m_paragraphSourceActive ? m_paragraphPanel->input()->textCursor().selectedText().replace(
                                       QChar::ParagraphSeparator, QLatin1Char('\n'))
                                 : QString();
}
void Editor::cut() {
  if (m_rendered && !m_paragraphSourceActive && textCursor().hasComplexSelection()) {
    copy();
    QTextCursor cursor = textCursor();
    editor::clearTableCells(cursor);
    setTextCursor(cursor);
    return;
  }
  if (m_paragraphSourceActive)
    m_paragraphPanel->input()->cut();
  else
    QTextEdit::cut();
}
void Editor::copy() {
  if (m_rendered && !m_paragraphSourceActive && textCursor().hasComplexSelection()) {
    auto* data = new QMimeData;
    data->setText(editor::tableTsv(textCursor(), true));
    data->setData(QStringLiteral("text/tab-separated-values"), data->text().toUtf8());
    QApplication::clipboard()->setMimeData(data);
    return;
  }
  if (m_paragraphSourceActive)
    m_paragraphPanel->input()->copy();
  else
    QTextEdit::copy();
}
void Editor::paste() {
  const auto* source = QApplication::clipboard()->mimeData();
  if (source && source->hasText() && pasteTableText(source->text())) return;
  if (m_imagePasteHandler && m_imagePasteHandler(QApplication::clipboard()->mimeData())) return;
  if (m_paragraphSourceActive)
    m_paragraphPanel->input()->paste();
  else
    QTextEdit::paste();
}
void Editor::selectAll() {
  if (m_paragraphSourceActive)
    m_paragraphPanel->input()->selectAll();
  else
    QTextEdit::selectAll();
}
void Editor::insertPlainText(const QString& text) {
  if (m_paragraphSourceActive)
    m_paragraphPanel->input()->insertPlainText(text);
  else {
    clearImageTypingFormat();
    QTextCursor cursor = textCursor();
    if (m_rendered && editor::insertCodeText(cursor, text))
      setTextCursor(cursor);
    else
      QTextEdit::insertPlainText(text);
  }
}
void Editor::updateParagraphSourceTheme() {
  if (m_paragraphPanel) m_paragraphPanel->setDark(m_dark);
}
void Editor::updateParagraphSourceGeometry() {
  if (!m_paragraphSourceActive || !m_paragraphPanel || m_paragraphDocument != document()) return;
  QTextCursor start = m_paragraphRange;
  start.setPosition(start.selectionStart());
  const int height = qBound(150, m_paragraphPanel->input()->document()->blockCount() * 22 + 80,
                            qMax(150, viewport()->height() * 2 / 3));
  const int y = qBound(4, cursorRect(start).top() - 6, qMax(4, viewport()->height() - height - 8));
  m_paragraphPanel->setGeometry(4, y, qMax(150, viewport()->width() - 12), height);
  m_paragraphPanel->raise();
}
bool Editor::setCodeLanguage(const QString& language) {
  finishParagraphSourceEdit();
  const auto range = editor::codeRange(textCursor());
  if (!m_rendered || !range.valid()) return false;
  const QString identifier =
      language.trimmed().section(QRegularExpression(QStringLiteral("\\s+")), 0, 0).left(64);
  if (!QRegularExpression(QStringLiteral("^[A-Za-z0-9_+.#-]*$")).match(identifier).hasMatch())
    return false;
  if (range.language() == identifier) return true;
  QTextCursor edit(document());
  edit.beginEditBlock();
  for (auto block = range.first; block.isValid(); block = block.next()) {
    QTextBlockFormat format = block.blockFormat();
    format.setProperty(editor::kCodeInfoProperty, identifier);
    QTextCursor(block).setBlockFormat(format);
    if (block == range.last) break;
  }
  edit.endEditBlock();
  updateCodeBlockBar();
  return true;
}
bool Editor::copyCodeBlock() {
  if (!m_rendered) return false;
  const auto range = editor::codeRange(textCursor());
  if (!range.valid()) return false;
  QApplication::clipboard()->setText(range.text());
  if (m_codeBar) m_codeBar->showCopied();
  return true;
}
bool Editor::exitCodeBlock() {
  if (!m_rendered) return false;
  QTextCursor cursor = textCursor();
  if (!editor::exitCode(cursor)) return false;
  setTextCursor(cursor);
  setCurrentCharFormat(QTextCharFormat());
  setFocus();
  ensureCursorVisible();
  updateCodeBlockBar();
  return true;
}
void Editor::updateCodeBlockTheme() {
  if (m_codeBar) m_codeBar->setDark(m_dark);
}
bool Editor::copyTable(bool spreadsheet) {
  if (!m_rendered || !textCursor().currentTable()) return false;
  const auto text =
      spreadsheet ? editor::tableTsv(textCursor()) : editor::tableMarkdown(textCursor());
  auto* data = new QMimeData;
  data->setText(text);
  if (spreadsheet) data->setData(QStringLiteral("text/tab-separated-values"), text.toUtf8());
  QApplication::clipboard()->setMimeData(data);
  return true;
}
bool Editor::exitTable() {
  if (!m_rendered) return false;
  auto cursor = textCursor();
  if (!editor::exitTable(cursor)) return false;
  setTextCursor(cursor);
  setCurrentCharFormat(QTextCharFormat());
  setFocus();
  ensureCursorVisible();
  updateTableBar();
  return true;
}
bool Editor::pasteTableText(const QString& text) {
  if (!m_rendered || m_paragraphSourceActive ||
      textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty))
    return false;
  auto cursor = textCursor();
  QString error;
  if (!editor::pasteTableCells(cursor, text, &error)) {
    if (error.isEmpty()) return false;
    emit tablePasteRejected(error);
    return true;
  }
  setTextCursor(cursor);
  auto typing = cursor.block().charFormat();
  typing.clearProperty(editor::kLiteralTextProperty);
  setCurrentCharFormat(typing);
  updateTableBar();
  return true;
}
void Editor::updateTableTheme() {
  if (m_tableBar) m_tableBar->setDark(m_dark);
}
void Editor::updateTableBar() {
  const auto cursor = textCursor();
  const auto cells = editor::tableCells(cursor);
  if (!m_rendered || !cells.valid() || m_paragraphSourceActive || viewport()->width() < 360) {
    if (m_tableBar) m_tableBar->hide();
    return;
  }
  if (!m_tableBar) {
    m_tableBar = new TableBar(this);
    connect(m_tableBar, &TableBar::operationRequested, this,
            [this](editor::TableCommand operation) {
              changeTable(operation);
              setFocus();
            });
    connect(m_tableBar, &TableBar::copyRequested, this,
            [this](bool spreadsheet) { copyTable(spreadsheet); });
    connect(m_tableBar, &TableBar::exitRequested, this, [this] { exitTable(); });
    updateTableTheme();
  }
  auto* table = cursor.currentTable();
  int alignment = table->cellAt(0, cells.column)
                      .firstCursorPosition()
                      .blockFormat()
                      .intProperty(editor::kTableAlignmentProperty);
  for (int column = cells.column + 1; column < cells.column + cells.columns; ++column)
    if (table->cellAt(0, column).firstCursorPosition().blockFormat().intProperty(
            editor::kTableAlignmentProperty) != alignment)
      alignment = 0;
  m_tableBar->setTableState(table->rows(), table->columns(), cells.row, cells.column, cells.rows,
                            cells.columns, alignment);
  const int width = qMin(viewport()->width(), m_tableBar->sizeHint().width());
  m_tableBar->setGeometry(viewport()->geometry().right() - width + 1, 3, width, 28);
  m_tableBar->show();
  m_tableBar->raise();
}
void Editor::updateCodeBlockBar(bool reset) {
  if (reset && m_codeBar) {
    if (m_codeBar->isAncestorOf(QApplication::focusWidget())) setFocus();
    m_codeBar->hide();
  }
  const auto cursor = textCursor();
  const int group = cursor.blockFormat().intProperty(editor::kCodeBlockProperty);
  const bool mixed =
      cursor.hasSelection() && document()
                                       ->findBlock(cursor.selectionStart())
                                       .blockFormat()
                                       .intProperty(editor::kCodeBlockProperty) != group;
  if (!m_rendered || !group || mixed || m_paragraphSourceActive || viewport()->width() < 320) {
    if (m_codeBar) m_codeBar->hide();
    return;
  }
  if (!m_codeBar) {
    m_codeBar = new CodeBlockBar(this);
    connect(m_codeBar, &CodeBlockBar::languageRequested, this, [this](const QString& language) {
      setFocus();
      setCodeLanguage(language);
    });
    connect(m_codeBar, &CodeBlockBar::copyRequested, this, [this] { copyCodeBlock(); });
    connect(m_codeBar, &CodeBlockBar::exitRequested, this, [this] { exitCodeBlock(); });
    connect(m_codeBar, &CodeBlockBar::returnToEditor, this, [this] {
      setFocus();
      updateCodeBlockBar();
    });
    updateCodeBlockTheme();
  }
  m_codeBar->setLanguage(cursor.blockFormat().property(editor::kCodeInfoProperty).toString());
  const int barWidth = qMin(viewport()->width(), qMax(300, m_codeBar->sizeHint().width()));
  // The writing area's top margin is reserved for contextual controls. Keeping the
  // bar outside the viewport avoids covering text or changing the document layout.
  m_codeBar->setGeometry(viewport()->geometry().right() - barWidth + 1, 3, barWidth, 28);
  m_codeBar->show();
  m_codeBar->raise();
}

void Editor::insertAdvancedContent(const QString& type) {
  finishParagraphSourceEdit();
  if (type == QLatin1String("toc")) {
    QTextCursor cursor = textCursor();
    if (m_rendered) {
      QString error;
      editor::insertMarkdownAtCursor(advancedMarkdown(type, {}), cursor, &error);
    } else
      cursor.insertText(QStringLiteral("\n\n[TOC]\n\n"));
    setTextCursor(cursor);
    setFocus();
    return;
  }
  if (type == QLatin1String("footnote")) {
    bool ok = false;
    const QString body = QInputDialog::getMultiLineText(
        this, editorText("Footnote"), editorText("Footnote text"), QString(), &ok);
    if (!ok || body.trimmed().isEmpty()) return;
    const QString current = m_rendered ? editor::serializeDocument(document()) : toPlainText();
    int number = 1;
    while (current.contains(QStringLiteral("[^%1]").arg(number))) ++number;
    const QString id = QString::number(number), reference = QStringLiteral("[^%1]").arg(id);
    const QString definition =
        QStringLiteral("[^%1]: %2\n\n")
            .arg(id, QString(body).replace(QStringLiteral("\n"), QStringLiteral("\n    ")));
    QTextCursor cursor = textCursor();
    cursor.beginEditBlock();
    QString error;
    if (m_rendered)
      editor::insertMarkdownAtCursor(reference, cursor, &error);
    else
      cursor.insertText(reference);
    const QTextCursor caret = cursor;
    cursor.movePosition(QTextCursor::End);
    if (m_rendered) {
      cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
      editor::insertMarkdownAtCursor(definition, cursor, &error);
    } else
      cursor.insertText(QStringLiteral("\n\n") + definition);
    cursor.endEditBlock();
    setTextCursor(caret);
    setFocus();
    return;
  }
  editAdvancedSource(type,
                     type == QLatin1String("mermaid")
                         ? QStringLiteral("flowchart TD\n    A[Start] --> B{Ready?}\n    B "
                                          "-->|Yes| C[Continue]\n    B -->|No| A\n")
                         : QStringLiteral("\\frac{-b \\pm \\sqrt{b^2-4ac}}{2a}"),
                     textCursor());
}

void Editor::editAdvancedSource(const QString& type, const QString& source, QTextCursor cursor) {
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("advancedContentDialog"));
  dialog.resize(860, 540);
  const bool diagram = type == QLatin1String("mermaid");
  dialog.setWindowTitle(diagram ? editorText("Mermaid Diagram") : editorText("Math Formula"));
  auto* layout = new QVBoxLayout(&dialog);
  layout->addWidget(
      new QLabel(editorText("Edit the source. The preview updates automatically."), &dialog));
  auto* splitter = new QSplitter(&dialog);
  auto* input = new QPlainTextEdit(splitter);
  input->setObjectName(QStringLiteral("advancedSource"));
  QFont mono;
  mono.setFamilies({"Consolas", "Menlo", "monospace"});
  mono.setPointSizeF(11);
  input->setFont(mono);
  input->setPlainText(source);
  auto* scroll = new QScrollArea(splitter);
  scroll->setWidgetResizable(true);
  auto* preview = new QLabel(scroll);
  preview->setAlignment(Qt::AlignCenter);
  preview->setWordWrap(true);
  preview->setStyleSheet(
      QStringLiteral("QLabel { background: white; color: #333; padding: 12px; }"));
  scroll->setWidget(preview);
  splitter->setStretchFactor(0, 1);
  splitter->setStretchFactor(1, 1);
  layout->addWidget(splitter, 1);
  auto* hint = new QLabel(
      diagram
          ? editorText("Supports flowchart and sequenceDiagram. Other syntax is kept as source.")
          : editorText("Double-click a formula or diagram in the document to edit it."),
      &dialog);
  hint->setWordWrap(true);
  layout->addWidget(hint);
  auto* buttons = new QDialogButtonBox(QDialogButtonBox::Save | QDialogButtonBox::Cancel, &dialog);
  buttons->button(QDialogButtonBox::Save)->setText(editorText("Apply"));
  buttons->button(QDialogButtonBox::Cancel)->setText(editorText("Cancel"));
  layout->addWidget(buttons);
  connect(buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  QTimer timer;
  timer.setSingleShot(true);
  timer.setInterval(150);
  const auto update = [=] {
    const auto result =
        diagram ? render::mermaid(input->toPlainText())
                : render::math(input->toPlainText(), type != QLatin1String("inline-math"));
    preview->clear();
    if (result.image.isNull())
      preview->setText(editorText("Preview unavailable. The source will be preserved."));
    else {
      const QPixmap pixmap = QPixmap::fromImage(result.image);
      preview->setPixmap(
          pixmap.scaled(QSize(740, 880), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    buttons->button(QDialogButtonBox::Save)->setEnabled(!input->toPlainText().trimmed().isEmpty());
  };
  connect(input, &QPlainTextEdit::textChanged, &timer, qOverload<>(&QTimer::start));
  connect(&timer, &QTimer::timeout, &dialog, update);
  update();
  if (dialog.exec() != QDialog::Accepted) return;
  cursor.beginEditBlock();
  const QString markdown = advancedMarkdown(type, input->toPlainText());
  if (m_rendered) {
    QString error;
    editor::insertMarkdownAtCursor(markdown, cursor, &error);
  } else
    cursor.insertText(markdown);
  cursor.endEditBlock();
  setTextCursor(cursor);
  setFocus();
}

void Editor::mouseDoubleClickEvent(QMouseEvent* event) {
  if (m_rendered) {
    const QTextCursor clicked = cursorForPosition(event->position().toPoint());
    for (int position : {clicked.position(), clicked.position() - 1}) {
      if (position < clicked.block().position() ||
          position >= clicked.block().position() + clicked.block().length() - 1)
        continue;
      QTextCursor cursor(document());
      cursor.setPosition(position);
      cursor.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
      const auto format = cursor.charFormat();
      if (format.hasProperty(editor::kMathSourceProperty)) {
        editAdvancedSource(QStringLiteral("inline-math"),
                           format.property(editor::kMathSourceProperty).toString(), cursor);
        event->accept();
        return;
      }
      if (format.hasProperty(editor::kSpecialSourceProperty)) {
        editAdvancedSource(cursor.blockFormat().property(editor::kSpecialBlockProperty).toString(),
                           format.property(editor::kSpecialSourceProperty).toString(), cursor);
        event->accept();
        return;
      }
    }
  }
  QTextEdit::mouseDoubleClickEvent(event);
}

void Editor::mouseReleaseEvent(QMouseEvent* event) {
  QTextEdit::mouseReleaseEvent(event);
  if (!m_rendered || event->button() != Qt::LeftButton || textCursor().hasSelection()) return;
  auto clicked = cursorForPosition(event->position().toPoint());
  if (clicked.blockFormat().property(editor::kSpecialBlockProperty).toString() ==
      QLatin1String("toc")) {
    const auto entries = render::headings(document());
    const int index =
        (event->position().y() - cursorRect(QTextCursor(clicked.block())).top() - 8) / 26;
    if (index >= 0 && index < entries.size()) {
      QTextCursor target(document());
      target.setPosition(entries[index].position);
      setTextCursor(target);
      ensureCursorVisible();
    }
    return;
  }
  QString id;
  for (const int position : {clicked.position(), clicked.position() - 1}) {
    if (position < 0 || position >= document()->characterCount() - 1) continue;
    QTextCursor character(document());
    character.setPosition(position);
    character.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
    id = character.charFormat().property(editor::kFootnoteReferenceProperty).toString();
    if (!id.isEmpty()) break;
  }
  if (!id.isEmpty())
    for (auto block = document()->begin(); block.isValid(); block = block.next())
      if (block.blockFormat().property(editor::kFootnoteDefinitionProperty).toString() == id) {
        setTextCursor(QTextCursor(block));
        ensureCursorVisible();
        break;
      }
}

void Editor::formatInline(editor::InlineStyle style) {
  finishParagraphSourceEdit();
  m_lastInlineBlock = -1;
  QTextCursor cursor = textCursor();
  if (!m_rendered) {
    const QString marker = style == editor::InlineStyle::Bold     ? QStringLiteral("**")
                           : style == editor::InlineStyle::Italic ? QStringLiteral("*")
                           : style == editor::InlineStyle::Strike ? QStringLiteral("~~")
                                                                  : QStringLiteral("`");
    if (style == editor::InlineStyle::Clear) return;
    const QString selected = cursor.selectedText();
    const int start = cursor.selectionStart();
    cursor.beginEditBlock();
    cursor.insertText(marker + selected + marker);
    cursor.setPosition(start + marker.size());
    if (!selected.isEmpty())
      cursor.setPosition(start + marker.size() + selected.size(), QTextCursor::KeepAnchor);
    cursor.endEditBlock();
  } else {
    cursor.beginEditBlock();
    editor::toggleInlineStyle(cursor, style);
    cursor.endEditBlock();
  }
  setTextCursor(cursor);
  setFocus();
}

void Editor::formatParagraph(editor::ParagraphStyle style, int heading) {
  finishParagraphSourceEdit();
  QTextCursor cursor = textCursor();
  if (!m_rendered) {
    const QString prefix = style == editor::ParagraphStyle::Heading
                               ? QString(heading, QLatin1Char('#')) + QLatin1Char(' ')
                           : style == editor::ParagraphStyle::Quote       ? QStringLiteral("> ")
                           : style == editor::ParagraphStyle::OrderedList ? QStringLiteral("1. ")
                           : style == editor::ParagraphStyle::BulletList  ? QStringLiteral("- ")
                           : style == editor::ParagraphStyle::TaskList    ? QStringLiteral("- [ ] ")
                                                                          : QString();
    cursor.beginEditBlock();
    const int start = cursor.document()->findBlock(cursor.selectionStart()).position();
    QTextBlock last = cursor.document()->findBlock(cursor.hasSelection() ? cursor.selectionEnd() - 1
                                                                         : cursor.position());
    cursor.setPosition(start);
    cursor.setPosition(last.position() + last.length() - 1, QTextCursor::KeepAnchor);
    QString text = cursor.selectedText().replace(QChar::ParagraphSeparator, QLatin1Char('\n'));
    if (style == editor::ParagraphStyle::Code)
      text = QStringLiteral("```\n") + text + QStringLiteral("\n```");
    else if (style == editor::ParagraphStyle::Rule)
      text += QStringLiteral("\n\n---\n");
    else {
      QStringList lines = text.split(QLatin1Char('\n'));
      for (QString& line : lines) {
        line.remove(QRegularExpression(
            QStringLiteral("^(#{1,6} |>[ ]?|[-+*] (?:\\[[ xX]\\] )?|\\d+\\. )")));
        line.prepend(prefix);
      }
      text = lines.join(QLatin1Char('\n'));
    }
    cursor.insertText(text);
    cursor.endEditBlock();
  } else {
    cursor.beginEditBlock();
    if (style == editor::ParagraphStyle::Rule && !cursor.block().text().isEmpty()) {
      cursor.clearSelection();
      cursor.movePosition(QTextCursor::EndOfBlock);
      cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    }
    editor::setParagraphStyle(cursor, style, heading);
    if (style == editor::ParagraphStyle::Rule) {
      cursor.movePosition(QTextCursor::EndOfBlock);
      cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
      editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
    }
    cursor.endEditBlock();
  }
  setTextCursor(cursor);
  setFocus();
  updateDecorations();
}

void Editor::insertLink(const QString& target, const QString& label) {
  finishParagraphSourceEdit();
  QTextCursor cursor = textCursor();
  if (m_rendered)
    editor::setLink(cursor, target, label);
  else
    cursor.insertText(QStringLiteral("[%1](%2)")
                          .arg(cursor.hasSelection() ? cursor.selectedText() : label, target));
  setTextCursor(cursor);
  setFocus();
}

void Editor::createTable(int rows, int columns) {
  finishParagraphSourceEdit();
  QTextCursor cursor = textCursor();
  if (m_rendered)
    editor::insertTable(cursor, rows, columns);
  else {
    QString source = QStringLiteral("\n\n|");
    for (int column = 0; column < columns; ++column)
      source += QStringLiteral(" Column %1 |").arg(column + 1);
    source += QStringLiteral("\n|") + QStringLiteral(" --- |").repeated(columns);
    for (int row = 1; row < rows; ++row)
      source += QStringLiteral("\n|") + QStringLiteral("  |").repeated(columns);
    cursor.insertText(source + QStringLiteral("\n\n"));
  }
  setTextCursor(cursor);
  setFocus();
}

bool Editor::changeTable(editor::TableCommand command) {
  finishParagraphSourceEdit();
  if (!m_rendered) return false;
  QTextCursor cursor = textCursor();
  const bool result = editor::editTable(cursor, command);
  setTextCursor(cursor);
  updateTableBar();
  return result;
}

void Editor::changeListIndent(int delta) {
  finishParagraphSourceEdit();
  QTextCursor cursor = textCursor();
  if (m_rendered) {
    if (cursor.blockFormat().hasProperty(editor::kCodeBlockProperty))
      editor::indentCode(cursor, delta < 0);
    else
      editor::indentList(cursor, delta);
  } else {
    const int end = cursor.hasSelection() ? cursor.selectionEnd() - 1 : cursor.position();
    QVector<QTextBlock> lines;
    for (QTextBlock block = document()->findBlock(cursor.selectionStart());
         block.isValid() && block.position() <= end; block = block.next())
      lines.append(block);
    cursor.beginEditBlock();
    for (int index = lines.size() - 1; index >= 0; --index) {
      QTextCursor leading(lines.at(index));
      if (delta > 0)
        leading.insertText(QStringLiteral("  "));
      else {
        const QString text = lines.at(index).text();
        const int spaces = text.startsWith(QStringLiteral("  ")) ? 2
                           : text.startsWith(QLatin1Char(' ')) || text.startsWith(QLatin1Char('\t'))
                               ? 1
                               : 0;
        if (spaces > 0) {
          leading.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor, spaces);
          leading.removeSelectedText();
        }
      }
    }
    cursor.endEditBlock();
  }
  setTextCursor(cursor);
}

void Editor::setTypewriterMode(bool enabled) {
  m_typewriterMode = enabled;
  if (enabled)
    centerWritingLine();
  else
    verticalScrollBar()->setMaximum(
        qMax(0, qCeil(document()->size().height()) - viewport()->height()));
}

void Editor::centerWritingLine() {
  if (m_centering) return;
  QScopedValueRollback<bool> guard(m_centering, true);
  QScrollBar* scroll = verticalScrollBar();
  const int natural = qMax(0, qCeil(document()->size().height()) - viewport()->height());
  scroll->setMaximum(natural + viewport()->height() / 2);
  scroll->setValue(scroll->value() + cursorRect().center().y() - viewport()->height() / 2);
}

void Editor::inputMethodEvent(QInputMethodEvent* event) {
  if (!event->commitString().isEmpty()) clearImageTypingFormat();
  QTextEdit::inputMethodEvent(event);
  if (m_rendered && event->preeditString().isEmpty() && !event->commitString().isEmpty()) {
    tryRenderInline();
  }
}

void Editor::insertFromMimeData(const QMimeData* source) {
  if (source->hasText() && pasteTableText(source->text())) return;
  clearImageTypingFormat();
  if (m_imagePasteHandler && m_imagePasteHandler(source)) return;
  if (m_rendered && source->hasText()) {
    QTextCursor cursor = textCursor();
    if (editor::insertCodeText(cursor, source->text())) {
      setTextCursor(cursor);
      return;
    }
  }
  if (!m_rendered || !source->hasText()) {
    QTextEdit::insertFromMimeData(source);
    return;
  }
  const QString text = source->text();
  const bool markdown = text.contains(QLatin1Char('\n')) || text.contains(QStringLiteral("**")) ||
                        text.contains(QStringLiteral("](")) || text.contains(QLatin1Char('`'));
  QTextCursor cursor = textCursor();
  if (markdown && editor::insertMarkdownAtCursor(text, cursor, nullptr))
    setTextCursor(cursor);
  else {
    insertPlainText(text);
    tryRenderInline();
  }
}
bool Editor::canInsertFromMimeData(const QMimeData* source) const {
  return (m_imagePasteHandler && (source->hasImage() || source->hasUrls())) ||
         QTextEdit::canInsertFromMimeData(source);
}
namespace {
QString droppedMarkdown(const QMimeData* source) {
  for (const auto& url : source->urls())
    if (url.isLocalFile() && url.toLocalFile().endsWith(QStringLiteral(".md"), Qt::CaseInsensitive))
      return url.toLocalFile();
  return {};
}
}  // namespace
void Editor::dragEnterEvent(QDragEnterEvent* event) {
  if (!droppedMarkdown(event->mimeData()).isEmpty())
    event->acceptProposedAction();
  else
    QTextEdit::dragEnterEvent(event);
}
void Editor::dragMoveEvent(QDragMoveEvent* event) {
  if (!droppedMarkdown(event->mimeData()).isEmpty())
    event->acceptProposedAction();
  else
    QTextEdit::dragMoveEvent(event);
}
void Editor::dropEvent(QDropEvent* event) {
  const QString path = droppedMarkdown(event->mimeData());
  if (!path.isEmpty()) {
    emit markdownFileDropped(path);
    event->acceptProposedAction();
    return;
  }
  // Import external images at the actual drop location, without allowing a move
  // action to delete content in the source application.
  if (event->source() != this && m_imagePasteHandler &&
      (event->mimeData()->hasImage() || event->mimeData()->hasUrls())) {
    finishParagraphSourceEdit();
    setTextCursor(cursorForPosition(event->position().toPoint()));
    if (m_imagePasteHandler(event->mimeData())) {
      event->setDropAction(Qt::CopyAction);
      event->accept();
      return;
    }
  }
  QTextEdit::dropEvent(event);
}

void Editor::keyPressEvent(QKeyEvent* event) {
  if (event->matches(QKeySequence::Copy)) {
    copy();
    return;
  }
  if (event->matches(QKeySequence::Cut)) {
    cut();
    return;
  }
  if (event->matches(QKeySequence::Paste)) {
    paste();
    return;
  }
  if (!event->text().isEmpty()) clearImageTypingFormat();
  QTextCursor cursor = textCursor();
  const bool control = event->modifiers().testFlag(Qt::ControlModifier) ||
                       event->modifiers().testFlag(Qt::MetaModifier);
  if (!event->text().isEmpty() && !control && !cursor.hasSelection() && cursor.atBlockEnd() &&
      cursor.blockNumber() == m_lastInlineBlock && cursor.block().text() == m_lastInlineText) {
    QTextCharFormat typing;
    const int heading = cursor.blockFormat().headingLevel();
    if (heading > 0) {
      static const double sizes[] = {28, 22, 18, 15, 13, 12};
      typing.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
      typing.setFontWeight(QFont::Bold);
      typing.setProperty(editor::kPresentationBoldProperty, true);
    }
    setCurrentCharFormat(typing);
    cursor = textCursor();
  }
  if (control && !event->modifiers().testFlag(Qt::AltModifier) &&
      !event->modifiers().testFlag(Qt::ShiftModifier)) {
    if (event->key() == Qt::Key_B) {
      formatInline(editor::InlineStyle::Bold);
      return;
    }
    if (event->key() == Qt::Key_I) {
      formatInline(editor::InlineStyle::Italic);
      return;
    }
  }
  const bool plain = !control && !event->modifiers().testFlag(Qt::AltModifier);
  const bool special =
      m_rendered && cursor.blockFormat().hasProperty(editor::kSpecialBlockProperty);
  const bool returnKey = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
  const bool code = m_rendered && cursor.blockFormat().hasProperty(editor::kCodeBlockProperty);
  if (m_rendered && cursor.currentTable() && control && returnKey &&
      !event->modifiers().testFlag(Qt::AltModifier)) {
    exitTable();
    return;
  }
  if (m_rendered && cursor.currentTable() && plain) {
    if ((event->key() == Qt::Key_Delete || event->key() == Qt::Key_Backspace) &&
        editor::clearTableCells(cursor)) {
      setTextCursor(cursor);
      return;
    }
    if (event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) {
      editor::moveTableCell(cursor, event->key() == Qt::Key_Backtab ||
                                        event->modifiers().testFlag(Qt::ShiftModifier));
      setTextCursor(cursor);
      setCurrentCharFormat(cursor.block().charFormat());
      return;
    }
  }
  if (code && control && returnKey && !event->modifiers().testFlag(Qt::AltModifier)) {
    exitCodeBlock();
    return;
  }
  if (code && plain) {
    const bool outdent =
        event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier);
    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
        editor::indentCode(cursor, outdent)) {
      setTextCursor(cursor);
      return;
    }
    if (event->key() == Qt::Key_Backspace && editor::backspaceCodeIndent(cursor)) {
      setTextCursor(cursor);
      return;
    }
    if (event->key() == Qt::Key_Home) {
      const QString text = cursor.block().text();
      int first = 0;
      while (first < text.size() && (text.at(first) == ' ' || text.at(first) == '\t')) ++first;
      const int target = cursor.positionInBlock() == first ? 0 : first;
      cursor.setPosition(cursor.block().position() + target,
                         event->modifiers().testFlag(Qt::ShiftModifier) ? QTextCursor::KeepAnchor
                                                                        : QTextCursor::MoveAnchor);
      setTextCursor(cursor);
      return;
    }
    if (returnKey && cursor.hasSelection() && editor::insertCodeLine(cursor, outdent)) {
      setTextCursor(cursor);
      return;
    }
  }
  if (plain && !cursor.hasSelection() &&
      ((special && ((!event->text().isEmpty() && event->text().at(0).isPrint()) || returnKey)) ||
       (m_rendered && returnKey && !event->modifiers().testFlag(Qt::ShiftModifier) &&
        cursor.blockFormat().hasProperty(editor::kFootnoteDefinitionProperty)))) {
    cursor.beginEditBlock();
    cursor.movePosition(QTextCursor::EndOfBlock);
    cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
    cursor.endEditBlock();
    setTextCursor(cursor);
    if (returnKey) return;
  }
  if (m_autoPair && plain && event->key() == Qt::Key_Backspace && !cursor.hasSelection() &&
      cursor.positionInBlock() > 0) {
    const QString opens = QStringLiteral("([{\"'");
    const QString closes = QStringLiteral(")]}\"'");
    const int opening = opens.indexOf(document()->characterAt(cursor.position() - 1));
    if (opening >= 0 && document()->characterAt(cursor.position()) == closes.at(opening)) {
      cursor.beginEditBlock();
      cursor.deleteChar();
      cursor.deletePreviousChar();
      cursor.endEditBlock();
      setTextCursor(cursor);
      return;
    }
  }
  const bool enter = event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter;
  if (m_rendered && plain && !cursor.hasSelection()) {
    const QString text = cursor.block().text();
    if ((event->key() == Qt::Key_Tab || event->key() == Qt::Key_Backtab) &&
        cursor.block().textList()) {
      changeListIndent(
          event->key() == Qt::Key_Backtab || event->modifiers().testFlag(Qt::ShiftModifier) ? -1
                                                                                            : 1);
      return;
    }
    if (event->key() == Qt::Key_Backspace &&
        (cursor.atBlockStart() || (cursor.blockFormat().hasProperty(editor::kTaskProperty) &&
                                   cursor.positionInBlock() == 2)) &&
        (cursor.block().textList() || cursor.blockFormat().headingLevel() ||
         cursor.blockFormat().hasProperty(editor::kQuoteProperty) || code)) {
      if (cursor.block().textList() && cursor.block().textList()->format().indent() > 1)
        changeListIndent(-1);
      else
        formatParagraph(editor::ParagraphStyle::Body);
      return;
    }
    if (event->key() == Qt::Key_Space && cursor.atBlockEnd() && !code) {
      const auto heading = QRegularExpression(QStringLiteral("^(#{1,6})$")).match(text);
      const auto ordered = QRegularExpression(QStringLiteral("^\\d+\\.$")).match(text);
      const auto task =
          QRegularExpression(QStringLiteral("^(?:[-+*] )?\\[([ xX])\\]$")).match(text);
      editor::ParagraphStyle style = editor::ParagraphStyle::Body;
      if (heading.hasMatch())
        style = editor::ParagraphStyle::Heading;
      else if (text == QStringLiteral(">"))
        style = editor::ParagraphStyle::Quote;
      else if (text == QStringLiteral("-") || text == QStringLiteral("+") ||
               text == QStringLiteral("*"))
        style = editor::ParagraphStyle::BulletList;
      else if (ordered.hasMatch())
        style = editor::ParagraphStyle::OrderedList;
      else if (task.hasMatch())
        style = editor::ParagraphStyle::TaskList;
      if (style != editor::ParagraphStyle::Body && !cursor.currentTable()) {
        cursor.beginEditBlock();
        cursor.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        editor::setParagraphStyle(cursor, style, heading.capturedLength(1));
        cursor.movePosition(QTextCursor::EndOfBlock);
        if (task.hasMatch() && task.captured(1).trimmed().toLower() == QStringLiteral("x"))
          editor::toggleTaskCheckbox(document(), cursor.blockNumber());
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
    }
    if (enter) {
      const bool shift = event->modifiers().testFlag(Qt::ShiftModifier);
      if (!code && !shift && cursor.atBlockEnd() &&
          (text.trimmed() == QStringLiteral("[TOC]") ||
           text.trimmed() == QStringLiteral("[toc]"))) {
        cursor.beginEditBlock();
        cursor.setPosition(cursor.block().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        QString error;
        editor::insertMarkdownAtCursor(QStringLiteral("[TOC]"), cursor, &error);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
      const auto fence = QRegularExpression(QStringLiteral("^(?:```|~~~)([A-Za-z0-9_+.-]*)$"))
                             .match(text.trimmed());
      if (!code && fence.hasMatch() && !cursor.currentTable()) {
        cursor.beginEditBlock();
        cursor.setPosition(cursor.block().position());
        cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
        cursor.removeSelectedText();
        editor::setParagraphStyle(cursor, editor::ParagraphStyle::Code);
        QTextBlockFormat format = cursor.blockFormat();
        format.setProperty(editor::kCodeInfoProperty, fence.captured(1));
        cursor.setBlockFormat(format);
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
      if (code) {
        cursor.beginEditBlock();
        const bool lastInGroup =
            cursor.block().next().blockFormat().intProperty(editor::kCodeBlockProperty) !=
            cursor.blockFormat().intProperty(editor::kCodeBlockProperty);
        if (lastInGroup && cursor.atBlockEnd() &&
            ((text.trimmed().isEmpty() && !shift) || text.trimmed() == QStringLiteral("```") ||
             text.trimmed() == QStringLiteral("~~~"))) {
          const auto previous = cursor.block().previous();
          if (previous.isValid() &&
              previous.blockFormat().intProperty(editor::kCodeBlockProperty) ==
                  cursor.blockFormat().intProperty(editor::kCodeBlockProperty)) {
            auto spacing = previous.blockFormat();
            spacing.setBottomMargin(10);
            QTextCursor(previous).setBlockFormat(spacing);
          }
          cursor.setPosition(cursor.block().position());
          cursor.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
          cursor.removeSelectedText();
          editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
          cursor.setCharFormat(QTextCharFormat());
        } else {
          editor::insertCodeLine(cursor, shift);
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
      if (!shift &&
          (text.trimmed() == QStringLiteral("---") || text.trimmed() == QStringLiteral("***") ||
           text.trimmed() == QStringLiteral("___"))) {
        cursor.beginEditBlock();
        editor::setParagraphStyle(cursor, editor::ParagraphStyle::Rule);
        cursor.movePosition(QTextCursor::EndOfBlock);
        cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
        editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
      if (!shift && cursor.block().textList()) {
        const bool task = cursor.blockFormat().hasProperty(editor::kTaskProperty);
        const bool empty = text.trimmed().isEmpty() || (task && text.mid(2).trimmed().isEmpty());
        if (empty) {
          formatParagraph(editor::ParagraphStyle::Body);
          return;
        }
        cursor.beginEditBlock();
        QTextBlockFormat format = cursor.blockFormat();
        format.setProperty(editor::kTaskProperty, false);
        if (!task) format.clearProperty(editor::kTaskProperty);
        cursor.insertBlock(format, QTextCharFormat());
        if (task) {
          QTextCharFormat marker;
          marker.setProperty(editor::kTaskMarkProperty, false);
          cursor.insertText(QString(QChar(0x2610)) + QLatin1Char(' '), marker);
          cursor.setCharFormat(QTextCharFormat());
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
      if (!shift && (cursor.blockFormat().headingLevel() > 0 ||
                     cursor.blockFormat().hasProperty(editor::kQuoteProperty))) {
        cursor.beginEditBlock();
        const bool quote = cursor.blockFormat().hasProperty(editor::kQuoteProperty);
        if (quote && !text.trimmed().isEmpty())
          cursor.insertBlock(cursor.blockFormat(), QTextCharFormat());
        else if (quote) {
          editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
          cursor.setCharFormat(QTextCharFormat());
        } else {
          cursor.insertBlock(QTextBlockFormat(), QTextCharFormat());
          editor::setParagraphStyle(cursor, editor::ParagraphStyle::Body);
        }
        cursor.endEditBlock();
        setTextCursor(cursor);
        return;
      }
    }
  }
  if (m_autoPair && plain && event->text().size() == 1) {
    const QString opens = QStringLiteral("([{\"'");
    const QString closes = QStringLiteral(")]}\"'");
    const QChar typed = event->text().at(0);
    const int opening = opens.indexOf(typed);
    const QChar next = document()->characterAt(cursor.position());
    if (closes.contains(typed) && next == typed && !cursor.hasSelection()) {
      cursor.movePosition(QTextCursor::NextCharacter);
      setTextCursor(cursor);
      if (m_rendered) tryRenderInline();
      return;
    }
    const bool quoteInWord = !cursor.hasSelection() &&
                             (typed == QLatin1Char('\'') || typed == QLatin1Char('"')) &&
                             cursor.position() > cursor.block().position() &&
                             document()->characterAt(cursor.position() - 1).isLetterOrNumber();
    if (opening >= 0 && !quoteInWord) {
      const QString selected = cursor.selectedText();
      const int start = cursor.selectionStart();
      cursor.beginEditBlock();
      cursor.insertText(QString(typed) + selected + closes.at(opening));
      cursor.setPosition(start + 1);
      if (!selected.isEmpty())
        cursor.setPosition(start + 1 + selected.size(), QTextCursor::KeepAnchor);
      cursor.endEditBlock();
      setTextCursor(cursor);
      return;
    }
  }
  QTextEdit::keyPressEvent(event);
  if (m_rendered && plain && !code &&
      (event->text() == QStringLiteral("*") || event->text() == QStringLiteral("_") ||
       event->text() == QStringLiteral("~") || event->text() == QStringLiteral("`") ||
       event->text() == QStringLiteral(")") || event->text() == QStringLiteral("$") ||
       event->text() == QStringLiteral("]") || event->key() == Qt::Key_Space)) {
    tryRenderInline();
  }
}

void Editor::clearImageTypingFormat() {
  if (!m_rendered || textCursor().hasSelection()) return;
  const auto current = currentCharFormat();
  if (!current.isImageFormat() && !current.hasProperty(editor::kImageProperty) &&
      !current.hasProperty(editor::kSpecialSourceProperty) &&
      !current.hasProperty(editor::kMathSourceProperty) &&
      !current.hasProperty(editor::kRawHtmlProperty))
    return;
  QTextCharFormat typing;
  const int heading = textCursor().blockFormat().headingLevel();
  if (heading > 0) {
    static const double sizes[] = {28, 22, 18, 15, 13, 12};
    typing.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
    typing.setFontWeight(QFont::Bold);
    typing.setProperty(editor::kPresentationBoldProperty, true);
  }
  setCurrentCharFormat(typing);
}

bool Editor::tryRenderInline() {
  QTextCursor cursor = textCursor();
  cursor.beginEditBlock();
  if (!editor::renderInlineMarkdown(cursor)) {
    cursor.endEditBlock();
    return false;
  }
  const int heading = cursor.blockFormat().headingLevel();
  if (heading > 0) editor::setParagraphStyle(cursor, editor::ParagraphStyle::Heading, heading);
  setTextCursor(cursor);
  QTextCharFormat typing;
  if (heading > 0) {
    // Heading presentation remains independent from semantic bold emphasis.
    static const double sizes[] = {28, 22, 18, 15, 13, 12};
    typing.setFontPointSize(sizes[qBound(1, heading, 6) - 1]);
    typing.setFontWeight(QFont::Bold);
    typing.setProperty(editor::kPresentationBoldProperty, true);
  }
  setCurrentCharFormat(typing);
  cursor.endEditBlock();
  m_lastInlineText = cursor.block().text();
  m_lastInlineBlock = cursor.blockNumber();
  return true;
}
}  // namespace typist::ui
