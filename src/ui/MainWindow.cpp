#include "MainWindow.h"

#include "FileWriter.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "Theme.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QPalette>
#include <QStatusBar>
#include <QString>
#include <QTextDocument>
#include <QTextEdit>

namespace typist {

namespace {

bool systemPrefersDark() {
  // Qt exposes no explicit dark-mode flag, so the host window colour is the hint.
  return QGuiApplication::palette().color(QPalette::Window).lightness() < 128;
}

QPalette themedPalette(ui::Theme theme) {
  QPalette palette = QPalette();
  if (theme == ui::Theme::Dark) {
    palette.setColor(QPalette::Window, QColor(0x1f, 0x23, 0x28));
    palette.setColor(QPalette::Base, QColor(0x1f, 0x23, 0x28));
    palette.setColor(QPalette::Text, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::WindowText, QColor(0xe6, 0xe6, 0xe6));
    palette.setColor(QPalette::Highlight, QColor(0x3a, 0x5f, 0x8a));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
  } else {
    palette.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Text, QColor(0x1f, 0x23, 0x28));
    palette.setColor(QPalette::WindowText, QColor(0x1f, 0x23, 0x28));
    palette.setColor(QPalette::Highlight, QColor(0xc7, 0xd7, 0xf0));
    palette.setColor(QPalette::HighlightedText, QColor(0x1f, 0x23, 0x28));
  }
  return palette;
}

}  // namespace

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent) {
  m_document = new QTextDocument(this);
  m_document->setUndoRedoEnabled(true);
  m_editor = new QTextEdit(this);
  m_editor->setDocument(m_document);
  m_editor->setAcceptRichText(true);
  setCentralWidget(m_editor);

  m_themePreference = platform::themePreference();

  QMenu* fileMenu = menuBar()->addMenu(QStringLiteral("File"));

  QAction* openAction = new QAction(QStringLiteral("Open"), this);
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addAction(openAction);
  connect(openAction, &QAction::triggered, this, &MainWindow::openDialog);

  QAction* saveAction = new QAction(QStringLiteral("Save"), this);
  saveAction->setShortcut(QKeySequence::Save);
  fileMenu->addAction(saveAction);
  connect(saveAction, &QAction::triggered, this, &MainWindow::save);

  QAction* saveAsAction = new QAction(QStringLiteral("Save As"), this);
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  fileMenu->addAction(saveAsAction);
  connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

  QAction* exportHtmlAction = new QAction(QStringLiteral("Export HTML"), this);
  exportHtmlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
  fileMenu->addAction(exportHtmlAction);
  connect(exportHtmlAction, &QAction::triggered, this, &MainWindow::exportHtml);

  QAction* exportPdfAction = new QAction(QStringLiteral("Export PDF"), this);
  exportPdfAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  fileMenu->addAction(exportPdfAction);
  connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportPdf);

  QMenu* editMenu = menuBar()->addMenu(QStringLiteral("Edit"));
  m_undoAction = new QAction(QStringLiteral("Undo"), this);
  m_undoAction->setShortcut(QKeySequence::Undo);
  m_undoAction->setEnabled(false);
  editMenu->addAction(m_undoAction);
  connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

  m_redoAction = new QAction(QStringLiteral("Redo"), this);
  m_redoAction->setShortcut(QKeySequence::Redo);
  m_redoAction->setEnabled(false);
  editMenu->addAction(m_redoAction);
  connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

  QMenu* viewMenu = menuBar()->addMenu(QStringLiteral("View"));
  m_sourceAction = new QAction(QStringLiteral("Source View"), this);
  m_sourceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
  viewMenu->addAction(m_sourceAction);
  connect(m_sourceAction, &QAction::triggered, this, &MainWindow::toggleSourceView);

  m_themeAction = new QAction(QStringLiteral("Theme"), this);
  m_themeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
  viewMenu->addAction(m_themeAction);
  connect(m_themeAction, &QAction::triggered, this, &MainWindow::cycleTheme);

  // Applied after the actions exist: applyTheme labels the theme action.
  applyTheme();

  connect(m_document, &QTextDocument::contentsChanged, this, [this] {
    // A plain flag keeps typing cheap; the exact comparison against the saved
    // snapshot happens at save, undo, redo and view switch instead.
    typist::editor::boundUndoHistory(m_document);
    m_dirty = true;
    updateTitleAndStatus();
  });
  connect(m_document, &QTextDocument::undoAvailable, m_undoAction, &QAction::setEnabled);
  connect(m_document, &QTextDocument::redoAvailable, m_redoAction, &QAction::setEnabled);
  connect(qGuiApp, &QGuiApplication::paletteChanged, this, [this] { applyTheme(); });

  updateTitleAndStatus();
}

void MainWindow::openDialog() {
  const QString path = QFileDialog::getOpenFileName(this, QStringLiteral("Open Markdown"), QString(),
                                                    QStringLiteral("Markdown (*.md)"));
  if (!path.isEmpty()) openFile(path);
}

void MainWindow::openFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(QStringLiteral("Cannot open file"));
    return;
  }

  const QString source = QString::fromUtf8(file.readAll());
  QString error;
  if (!typist::editor::loadMarkdown(source, m_document, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_currentPath = path;
  m_savedMarkdown = source;
  m_sourceMode = false;
  m_editor->setAcceptRichText(true);
  m_sourceAction->setText(QStringLiteral("Source View"));
  markClean();
}

void MainWindow::save() {
  if (m_currentPath.isEmpty()) {
    saveAs();
    return;
  }

  QString error;
  const QString markdown = currentMarkdown();
  if (!typist::platform::writeAtomic(m_currentPath, markdown, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_savedMarkdown = markdown;
  markClean();
}

void MainWindow::saveAs() {
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Save Markdown"), m_currentPath,
                                                    QStringLiteral("Markdown (*.md)"));
  if (path.isEmpty()) return;
  m_currentPath = path;
  save();
}

void MainWindow::exportHtml() {
  const QString suggested = m_currentPath.isEmpty()
      ? QDir::homePath() + QStringLiteral("/untitled.html")
      : QFileInfo(m_currentPath).absolutePath() + QStringLiteral("/") +
            QFileInfo(m_currentPath).baseName() + QStringLiteral(".html");
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export HTML"), suggested,
                                                    QStringLiteral("HTML (*.html)"));
  if (path.isEmpty()) return;

  const std::string body = typist::core::toHtml(currentMarkdown().toStdString());
  const std::string css = ui::htmlStyle(ui::resolveTheme(themePreference(), systemPrefersDark())).toStdString();
  const std::string page = typist::core::wrapHtmlDocument(body, QFileInfo(path).baseName().toStdString(), css);
  QString error;
  if (!typist::platform::writeAtomic(path, QString::fromStdString(page), &error)) {
    statusBar()->showMessage(error);
    return;
  }
  statusBar()->showMessage(QStringLiteral("Exported HTML"));
}

void MainWindow::exportPdf() {
  if (!typist::platform::pdfExportAvailable()) {
    statusBar()->showMessage(QStringLiteral("PDF export needs Qt PrintSupport, which this build lacks"));
    return;
  }

  const QString suggested = m_currentPath.isEmpty()
      ? QDir::homePath() + QStringLiteral("/untitled.pdf")
      : QFileInfo(m_currentPath).absolutePath() + QStringLiteral("/") +
            QFileInfo(m_currentPath).baseName() + QStringLiteral(".pdf");
  const QString path = QFileDialog::getSaveFileName(this, QStringLiteral("Export PDF"), suggested,
                                                    QStringLiteral("PDF (*.pdf)"));
  if (path.isEmpty()) return;

  // PDF always reflects Markdown semantics, so it prints a detached rendered document
  // instead of whatever the editor view is currently showing.
  QTextDocument rendered;
  QString error;
  if (!typist::editor::loadMarkdown(currentMarkdown(), &rendered, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  if (!typist::platform::exportPdf(path, &rendered, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  statusBar()->showMessage(QStringLiteral("Exported PDF"));
}

void MainWindow::undo() {
  m_document->undo();
  m_dirty = currentMarkdown() != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::redo() {
  m_document->redo();
  m_dirty = currentMarkdown() != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::toggleSourceView() {
  if (m_sourceMode) {
    QString error;
    // A failed parse must not discard what the author typed in source mode.
    if (!typist::editor::loadMarkdown(m_editor->toPlainText(), m_document, &error)) {
      statusBar()->showMessage(error);
      return;
    }
    m_editor->setAcceptRichText(true);
    m_sourceMode = false;
    m_sourceAction->setText(QStringLiteral("Source View"));
  } else {
    m_editor->setAcceptRichText(false);
    m_editor->setPlainText(typist::editor::serializeDocument(m_document));
    m_sourceMode = true;
    m_sourceAction->setText(QStringLiteral("Rendered View"));
  }
  m_document->clearUndoRedoStacks(QTextDocument::UndoAndRedoStacks);
  m_dirty = currentMarkdown() != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::cycleTheme() {
  if (m_themePreference == QStringLiteral("system")) {
    m_themePreference = QStringLiteral("light");
  } else if (m_themePreference == QStringLiteral("light")) {
    m_themePreference = QStringLiteral("dark");
  } else {
    m_themePreference = QStringLiteral("system");
  }
  platform::setThemePreference(m_themePreference);
  applyTheme();
  statusBar()->showMessage(QStringLiteral("Theme: ") + m_themePreference);
}

void MainWindow::applyTheme() {
  const ui::Theme theme = ui::resolveTheme(themePreference(), systemPrefersDark());
  m_editor->setStyleSheet(ui::editorStyleSheet(theme));
  applyThemePalette(themedPalette(theme));
  if (m_themeAction != nullptr) {
    m_themeAction->setText(QStringLiteral("Theme: ") + themePreference());
  }
}

void MainWindow::applyThemePalette(const QPalette& palette) {
  m_editor->setPalette(palette);
  setPalette(palette);
}

int MainWindow::documentBlockCount() const { return m_document->blockCount(); }

qint64 MainWindow::measureTypingRepaint() {
  QElapsedTimer timer;
  timer.start();
  QTextCursor cursor(m_document);
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("x"));
  // A synchronous repaint runs the paint path now instead of waiting for the event loop.
  m_editor->repaint();
  return timer.elapsed();
}

QString MainWindow::themePreference() const { return m_themePreference; }

QString MainWindow::currentMarkdown() const {
  return m_sourceMode ? m_editor->toPlainText() : typist::editor::serializeDocument(m_document);
}

void MainWindow::markClean() {
  m_dirty = false;
  updateTitleAndStatus();
}

void MainWindow::updateTitleAndStatus() {
  const QString base = m_currentPath.isEmpty() ? QStringLiteral("Typist") : m_currentPath;
  setWindowTitle(m_dirty ? base + QStringLiteral(" *") : base);
  statusBar()->showMessage(m_dirty ? QStringLiteral("Unsaved changes") : QStringLiteral("Saved"));
}

}  // namespace typist
