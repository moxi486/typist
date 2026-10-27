#include "MainWindow.h"

#include "AutoSave.h"
#include "DocxWriter.h"
#include "SystemTheme.h"
#include "FileOps.h"
#include "FileSearch.h"
#include "FileTree.h"
#include "FileWriter.h"
#include "FindBar.h"
#include "PdfWriter.h"
#include "Settings.h"
#include "Theme.h"
#include "ViewSwitch.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QApplication>
#include <QCloseEvent>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QListWidget>
#include <QMimeData>
#include <QUrl>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QColor>
#include <QKeySequence>
#include <QMenuBar>
#include <QMenu>
#include <QPalette>
#include <QStatusBar>
#include <QString>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QVBoxLayout>
#include <QWidget>

namespace typist {

namespace {

bool systemPrefersDark() {
  const typist::platform::DarkHint hint = typist::platform::nativeDarkHint();
  if (hint == typist::platform::DarkHint::Dark) return true;
  if (hint == typist::platform::DarkHint::Light) return false;
  // Qt exposes no explicit dark-mode flag, so the host window colour is the fallback hint.
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
  // Two documents over one buffer: each mode keeps its own undo history.
  m_view = new ui::ViewSwitch(this);
  m_document = m_view->active();
  m_editor = new QTextEdit(this);
  m_view->showActiveIn(m_editor);
  m_findBar = new ui::FindBar(this);

  auto* central = new QWidget(this);
  auto* centralLayout = new QVBoxLayout(central);
  centralLayout->setContentsMargins(0, 0, 0, 0);
  centralLayout->addWidget(m_editor);
  centralLayout->addWidget(m_findBar);
  setCentralWidget(central);

  m_tree = new ui::FileTree(this);
  m_sidebarDock = new QDockWidget(tr("Files"), this);
  m_sidebarDock->setWidget(m_tree);
  m_sidebarDock->hide();
  addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
  setAcceptDrops(true);
  connect(m_tree, &ui::FileTree::entryActivated, this, &MainWindow::openTreeEntry);
  connect(m_tree, &ui::FileTree::fileRenamed, this, &MainWindow::treeFileRenamed);
  connect(m_tree, &ui::FileTree::fileRemoved, this, &MainWindow::treeFileRemoved);

  m_results = new QListWidget(this);
  m_resultsDock = new QDockWidget(tr("Search Results"), this);
  m_resultsDock->setWidget(m_results);
  m_resultsDock->hide();
  addDockWidget(Qt::BottomDockWidgetArea, m_resultsDock);
  connect(m_results, &QListWidget::itemDoubleClicked, this, &MainWindow::openSearchHit);

  connect(m_findBar, &ui::FindBar::patternChanged, this, &MainWindow::refreshMatches);
  connect(m_findBar, &ui::FindBar::findNext, this, &MainWindow::searchNext);
  connect(m_findBar, &ui::FindBar::findPrevious, this, &MainWindow::searchPrevious);

  m_autoSave = new ui::AutoSave(this);
  m_autoSave->setCallback([this] { autoSaveNow(); });

  m_themePreference = platform::themePreference();

  QMenu* fileMenu = menuBar()->addMenu(tr("File"));

  QAction* openAction = new QAction(tr("Open"), this);
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addAction(openAction);
  connect(openAction, &QAction::triggered, this, &MainWindow::openDialog);

  QAction* saveAction = new QAction(tr("Save"), this);
  saveAction->setShortcut(QKeySequence::Save);
  fileMenu->addAction(saveAction);
  connect(saveAction, &QAction::triggered, this, &MainWindow::save);

  QAction* saveAsAction = new QAction(tr("Save As"), this);
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  fileMenu->addAction(saveAsAction);
  connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

  QAction* exportHtmlAction = new QAction(tr("Export HTML"), this);
  exportHtmlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
  fileMenu->addAction(exportHtmlAction);
  connect(exportHtmlAction, &QAction::triggered, this, &MainWindow::exportHtml);

  QAction* exportPdfAction = new QAction(tr("Export PDF"), this);
  exportPdfAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  fileMenu->addAction(exportPdfAction);
  connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportPdf);
  QAction* exportDocxAction = fileMenu->addAction(tr("Export DOCX"));
  exportDocxAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
  connect(exportDocxAction, &QAction::triggered, this, &MainWindow::exportDocx);

  QMenu* editMenu = menuBar()->addMenu(tr("Edit"));
  m_undoAction = new QAction(tr("Undo"), this);
  m_undoAction->setShortcut(QKeySequence::Undo);
  m_undoAction->setEnabled(false);
  editMenu->addAction(m_undoAction);
  connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

  m_redoAction = new QAction(tr("Redo"), this);
  m_redoAction->setShortcut(QKeySequence::Redo);
  m_redoAction->setEnabled(false);
  editMenu->addAction(m_redoAction);
  connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

  m_findAction = new QAction(tr("Find"), this);
  m_findAction->setShortcut(QKeySequence::Find);
  editMenu->addAction(m_findAction);
  connect(m_findAction, &QAction::triggered, this, &MainWindow::toggleFindBar);

  QAction* findNextAction = new QAction(tr("Find Next"), this);
  findNextAction->setShortcut(QKeySequence(QStringLiteral("F3")));
  editMenu->addAction(findNextAction);
  connect(findNextAction, &QAction::triggered, this, &MainWindow::searchNext);

  QAction* findPreviousAction = new QAction(tr("Find Previous"), this);
  findPreviousAction->setShortcut(QKeySequence(QStringLiteral("Shift+F3")));
  editMenu->addAction(findPreviousAction);
  connect(findPreviousAction, &QAction::triggered, this, &MainWindow::searchPrevious);

  m_folderSearchAction = new QAction(tr("Search in Folder"), this);
  m_folderSearchAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
  editMenu->addAction(m_folderSearchAction);
  connect(m_folderSearchAction, &QAction::triggered, this, &MainWindow::searchInFolder);


  m_folderAction = new QAction(tr("Open Folder"), this);
  m_folderAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
  fileMenu->addAction(m_folderAction);
  connect(m_folderAction, &QAction::triggered, this, &MainWindow::openFolder);

  QMenu* viewMenu = menuBar()->addMenu(tr("View"));
  m_sourceAction = new QAction(tr("Source View"), this);
  m_sourceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
  viewMenu->addAction(m_sourceAction);
  connect(m_sourceAction, &QAction::triggered, this, &MainWindow::toggleSourceView);

  m_themeAction = new QAction(tr("Theme"), this);
  m_themeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
  viewMenu->addAction(m_themeAction);
  connect(m_themeAction, &QAction::triggered, this, &MainWindow::cycleTheme);

  m_sidebarAction = new QAction(tr("Show Files"), this);
  m_sidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
  viewMenu->addAction(m_sidebarAction);
  connect(m_sidebarAction, &QAction::triggered, this, &MainWindow::toggleSidebar);

  // Applied after the actions exist: applyTheme labels the theme action.
  applyTheme();

  // Both documents feed the same dirty flag and autosave window; only the active one
  // drives the undo/redo actions.
  for (QTextDocument* doc : {m_view->rendered(), m_view->source()}) {
    connect(doc, &QTextDocument::contentsChanged, this, [this, doc] {
      // A plain flag keeps typing cheap; the exact comparison against the saved
      // snapshot happens at save, undo, redo and view switch instead.
      typist::editor::boundUndoHistory(doc);
      m_dirty = true;
      // An untitled buffer has no target file, so autosave only runs once a path exists.
      if (!m_currentPath.isEmpty() && !m_benchMode) m_autoSave->notifyChanged();
      updateTitleAndStatus();
    });
    connect(doc, &QTextDocument::undoAvailable, this, [this, doc](bool available) {
      if (doc == m_document) m_undoAction->setEnabled(available);
    });
    connect(doc, &QTextDocument::redoAvailable, this, [this, doc](bool available) {
      if (doc == m_document) m_redoAction->setEnabled(available);
    });
  }
  connect(qGuiApp, &QGuiApplication::paletteChanged, this, [this] { applyTheme(); });

  updateTitleAndStatus();
}

void MainWindow::openDialog() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Open Markdown"), QString(),
                                                    QStringLiteral("Markdown (*.md)"));
  if (!path.isEmpty()) openFile(path);
}

void MainWindow::openFile(const QString& path) {
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(tr("Cannot open file"));
    return;
  }

  const QString source = QString::fromUtf8(file.readAll());
  QString error;
  // A new file is a new baseline for both views, so both undo stacks start empty.
  if (!m_view->loadMarkdownIntoBoth(source, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_document = m_view->active();
  m_view->showActiveIn(m_editor);
  m_sourceMode = m_view->isSourceMode();
  m_currentPath = path;
  m_savedMarkdown = source;
  m_sourceAction->setText(tr("Source View"));
  markClean();

  if (m_sidebarDock->isVisible()) {
    m_tree->setRoot(QFileInfo(path).absolutePath());
    m_tree->selectPath(path);
  }
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
  const QString path = QFileDialog::getSaveFileName(this, tr("Save Markdown"), m_currentPath,
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
  const QString path = QFileDialog::getSaveFileName(this, tr("Export HTML"), suggested,
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
  statusBar()->showMessage(tr("Exported HTML"));
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
  const QString path = QFileDialog::getSaveFileName(this, tr("Export PDF"), suggested,
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

  platform::PdfPageOptions options;
  options.paper = platform::pdfPaper();
  options.marginMm = platform::pdfMarginMm();
  options.header = QFileInfo(path).baseName();
  options.footer = QStringLiteral("Page #");

  if (!typist::platform::exportPdf(path, &rendered, options, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  statusBar()->showMessage(tr("Exported PDF"));
}

void MainWindow::exportDocx() {
  const QString suggested = m_currentPath.isEmpty()
      ? QDir::homePath() + QStringLiteral("/untitled.docx")
      : QFileInfo(m_currentPath).absolutePath() + QStringLiteral("/") +
            QFileInfo(m_currentPath).baseName() + QStringLiteral(".docx");
  const QString path = QFileDialog::getSaveFileName(this, tr("Export DOCX"), suggested,
                                                    QStringLiteral("Word document (*.docx)"));
  if (path.isEmpty()) return;

  // DOCX reflects Markdown semantics directly, so it renders from the source text.
  QString error;
  if (!typist::platform::exportDocx(path, currentMarkdown(), &error)) {
    statusBar()->showMessage(error);
    return;
  }
  statusBar()->showMessage(tr("Exported DOCX"));
}

void MainWindow::autoSaveNow() {
  if (!m_dirty || m_currentPath.isEmpty()) return;

  const QString markdown = currentMarkdown();
  QString error;
  if (!typist::platform::writeAtomic(m_currentPath, markdown, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_savedMarkdown = markdown;
  markClean();
  statusBar()->showMessage(tr("Autosaved"));
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
  // Flush first so the file on disk matches whichever view is about to be shown.
  if (!m_benchMode) m_autoSave->flushNow();

  QString error;
  if (!m_view->switchView(&error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_document = m_view->active();
  m_view->showActiveIn(m_editor);
  m_sourceMode = m_view->isSourceMode();
  m_sourceAction->setText(m_sourceMode ? tr("Rendered View") : tr("Source View"));
  m_undoAction->setEnabled(m_document->isUndoAvailable());
  m_redoAction->setEnabled(m_document->isRedoAvailable());
  m_dirty = currentMarkdown() != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::toggleFindBar() {
  if (m_findBar->isVisible()) {
    m_findBar->closeBar();
    m_findBar->setPattern(QString());
    m_editor->setFocus();
  } else {
    m_findBar->openFocused();
  }
  refreshMatches();
}

void MainWindow::searchNext() {
  QTextCursor cursor = m_editor->textCursor();
  m_search.setCaseSensitive(m_findBar->caseSensitive());
  if (m_search.selectNext(cursor, m_findBar->pattern())) {
    m_editor->setTextCursor(cursor);
    m_editor->ensureCursorVisible();
  }
  refreshMatches();
}

void MainWindow::searchPrevious() {
  QTextCursor cursor = m_editor->textCursor();
  m_search.setCaseSensitive(m_findBar->caseSensitive());
  if (m_search.selectPrevious(cursor, m_findBar->pattern())) {
    m_editor->setTextCursor(cursor);
    m_editor->ensureCursorVisible();
  }
  refreshMatches();
}

void MainWindow::refreshMatches() {
  m_search.setCaseSensitive(m_findBar->caseSensitive());
  const QString pattern = m_findBar->pattern();
  m_findBar->setMatchCount(m_search.countMatches(m_document, pattern));

  QList<QTextEdit::ExtraSelection> selections;
  if (!pattern.isEmpty()) {
    QColor highlight = m_editor->palette().color(QPalette::Highlight);
    highlight.setAlpha(110);
    const QVector<QTextCursor> hits = m_search.allMatches(m_document, pattern);
    selections.reserve(hits.size());
    for (const QTextCursor& hit : hits) {
      QTextEdit::ExtraSelection selection;
      selection.cursor = hit;
      selection.format.setBackground(highlight);
      selections.append(selection);
    }
  }
  m_editor->setExtraSelections(selections);
}

int MainWindow::highlightCountForBench(const QString& pattern) const {
  typist::ui::SearchController search = m_search;
  return search.allMatches(m_document, pattern).size();
}

qint64 MainWindow::exportDocxForBench(const QString& path) const {
  return typist::platform::docxSizeForBench(path, currentMarkdown());
}

int MainWindow::searchMatchesForBench(const QString& pattern) const {
  return m_search.countMatches(m_document, pattern);
}

int MainWindow::fileTreeEntriesForBench(const QString& rootDir) {
  m_tree->setRoot(rootDir);
  return m_tree->entryCount();
}

int MainWindow::fileTreeDirectoriesForBench(const QString& rootDir) {
  m_tree->setRoot(rootDir);
  return m_tree->directoryCount();
}

void MainWindow::treeFileRenamed(const QString& oldPath, const QString& newPath) {
  if (m_currentPath == oldPath) {
    m_currentPath = newPath;
    updateTitleAndStatus();
  }
}

void MainWindow::treeFileRemoved(const QString& path) {
  if (m_currentPath != path) return;
  // The buffer under the deleted file becomes an unnamed document.
  QString error;
  m_view->loadMarkdownIntoBoth(QString(), &error);
  m_document = m_view->active();
  m_view->showActiveIn(m_editor);
  m_currentPath.clear();
  m_savedMarkdown.clear();
  markClean();
  updateTitleAndStatus();
  statusBar()->showMessage(tr("Deleted file; buffer is now unsaved"));
}

bool MainWindow::openDroppedFiles(const QList<QUrl>& urls) {
  const QString path = platform::firstMarkdownPath(urls);
  if (path.isEmpty()) return false;
  openFile(path);
  return true;
}

void MainWindow::dragEnterEvent(QDragEnterEvent* event) {
  if (event->mimeData()->hasUrls()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (openDroppedFiles(event->mimeData()->urls())) event->acceptProposedAction();
}

void MainWindow::toggleSidebar() {
  if (m_sidebarDock->isVisible()) {
    m_tree->setWatchEnabled(false);
    m_sidebarDock->hide();
    return;
  }
  if (!m_currentPath.isEmpty()) m_tree->setRoot(QFileInfo(m_currentPath).absolutePath());
  m_sidebarDock->show();
  m_tree->setWatchEnabled(true);
  m_tree->selectPath(m_currentPath);
}

void MainWindow::openFolder() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
  if (dir.isEmpty()) return;
  m_tree->setRoot(dir);
  m_sidebarDock->show();
  m_tree->setWatchEnabled(true);
  statusBar()->showMessage(QStringLiteral("%1 markdown files in %2").arg(m_tree->entryCount()).arg(dir));
}

void MainWindow::openTreeEntry(const QString& path) {
  openFile(path);
}

qint64 MainWindow::searchFolderForBench(const QString& rootDir, const QString& pattern, int* hitsOut) {
  platform::SearchRequest request;
  request.rootDir = rootDir;
  request.pattern = pattern;
  request.maxHits = 0;
  QElapsedTimer timer;
  timer.start();
  QString error;
  const QVector<platform::SearchHit> hits = platform::searchFiles(request, &error);
  if (hitsOut) *hitsOut = hits.size();
  return timer.elapsed();
}

void MainWindow::searchInFolder() {
  const QString pattern = m_findBar->pattern();
  if (pattern.isEmpty()) {
    statusBar()->showMessage(tr("Enter a pattern in the find bar first."));
    m_findBar->openFocused();
    return;
  }

  const QString root = m_currentPath.isEmpty() ? QDir::homePath() : QFileInfo(m_currentPath).absolutePath();
  platform::SearchRequest request;
  request.rootDir = root;
  request.pattern = pattern;
  request.caseSensitive = m_findBar->caseSensitive();

  QString error;
  const QVector<platform::SearchHit> hits = platform::searchFiles(request, &error);

  m_results->clear();
  for (const platform::SearchHit& hit : hits) {
    QListWidgetItem* item = new QListWidgetItem(QStringLiteral("%1:%2  %3").arg(QFileInfo(hit.path).fileName()).arg(hit.line).arg(hit.preview));
    item->setData(Qt::UserRole, hit.path);
    m_results->addItem(item);
  }
  m_resultsDock->show();
  statusBar()->showMessage(error.isEmpty() ? QStringLiteral("%1 matches in %2").arg(hits.size()).arg(root)
                                                : QStringLiteral("%1 matches in %2 (%3)").arg(hits.size()).arg(root).arg(error));
}

void MainWindow::openSearchHit() {
  QListWidgetItem* item = m_results->currentItem();
  if (item == nullptr) return;
  openFile(item->data(Qt::UserRole).toString());
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
  statusBar()->showMessage(tr("Theme: %1").arg(m_themePreference));
}

void MainWindow::closeEvent(QCloseEvent* event) {
  // Closing is the last chance to persist edits; the debounce window may still be open.
  if (!m_benchMode) m_autoSave->flushNow();
  QMainWindow::closeEvent(event);
}

void MainWindow::applyTheme() {
  const ui::Theme theme = ui::resolveTheme(themePreference(), systemPrefersDark());
  m_editor->setStyleSheet(ui::editorStyleSheet(theme));
  applyThemePalette(themedPalette(theme));
  if (m_themeAction != nullptr) {
    m_themeAction->setText(tr("Theme: %1").arg(themePreference()));
  }
}

void MainWindow::applyThemePalette(const QPalette& palette) {
  m_editor->setPalette(palette);
  setPalette(palette);
}

void MainWindow::setBenchMode(bool enabled) { m_benchMode = enabled; }

bool MainWindow::viewSwitchKeepsUndoForBench() {
  QTextCursor cursor(m_document);
  cursor.beginEditBlock();
  cursor.movePosition(QTextCursor::End);
  cursor.insertText(QStringLiteral("x"));
  cursor.endEditBlock();

  toggleSourceView();
  toggleSourceView();
  return m_document->isUndoAvailable();
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
  return m_view->isSourceMode() ? m_editor->toPlainText() : typist::editor::serializeDocument(m_document);
}

void MainWindow::markClean() {
  m_dirty = false;
  updateTitleAndStatus();
}

void MainWindow::updateTitleAndStatus() {
  const QString base = m_currentPath.isEmpty() ? QStringLiteral("Typist") : m_currentPath;
  setWindowTitle(m_dirty ? base + QStringLiteral(" *") : base);
  statusBar()->showMessage(m_dirty ? tr("Unsaved changes") : tr("Saved"));
}

}  // namespace typist
