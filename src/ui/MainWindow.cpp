#include "MainWindow.h"

#include "AutoSave.h"
#include "CodeEditing.h"
#include "Conflict.h"
#include "DocxWriter.h"
#include "Editor.h"
#include "EpubDialog.h"
#include "FileOps.h"
#include "FileSearch.h"
#include "FileTree.h"
#include "FileWriter.h"
#include "FindBar.h"
#include "FolderLocationsMenu.h"
#include "FolderSearchRunner.h"
#include "HeadingLinks.h"
#include "HtmlImportDialog.h"
#include "HtmlImporter.h"
#include "ImageAssets.h"
#include "ImageExportDialog.h"
#include "ImageResources.h"
#include "LinkDialog.h"
#include "LinkEditing.h"
#include "LinkNavigation.h"
#include "Localization.h"
#include "OutlinePanel.h"
#include "PdfExportDialog.h"
#include "PdfWriter.h"
#include "QuickOpenDialog.h"
#include "Settings.h"
#include "SpecialContent.h"
#include "SystemTheme.h"
#include "Theme.h"
#include "ViewSwitch.h"
#include "WorkspaceSearchPanel.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QDesktopServices>
#include <QMouseEvent>
#include <QTextBlock>
#ifdef TYPIST_HAS_NETWORK
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QNetworkRequest>
#endif
#include "UpdateChecker.h"

#include <QCheckBox>
#include <QCloseEvent>
#include <QColor>
#include <QComboBox>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QElapsedTimer>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QImage>
#include <QInputDialog>
#include <QKeySequence>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPainter>
#include <QPalette>
#include <QPlainTextEdit>
#include <QProcess>
#include <QPushButton>
#include <QRegularExpression>
#include <QRegularExpressionValidator>
#include <QScopeGuard>
#include <QScreen>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QString>
#include <QStyle>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextEdit>
#include <QTextFragment>
#include <QTextList>
#include <QTextTable>
#include <QToolButton>
#include <QUrl>
#include <QVBoxLayout>
#include <QWidget>
#include <functional>

namespace typist {

namespace {

const auto htmlOutlineSetting = QStringLiteral("export/htmlOutline");
const auto htmlContentSetting = QStringLiteral("export/htmlContent");

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
    palette.setColor(QPalette::Window, QColor("#363b40"));
    palette.setColor(QPalette::Base, QColor("#363b40"));
    palette.setColor(QPalette::Text, QColor("#e0e0e0"));
    palette.setColor(QPalette::WindowText, QColor("#e0e0e0"));
    palette.setColor(QPalette::Highlight, QColor(0x3a, 0x5f, 0x8a));
    palette.setColor(QPalette::HighlightedText, QColor(0xff, 0xff, 0xff));
  } else {
    palette.setColor(QPalette::Window, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Base, QColor(0xff, 0xff, 0xff));
    palette.setColor(QPalette::Text, QColor("#333333"));
    palette.setColor(QPalette::WindowText, QColor("#333333"));
    palette.setColor(QPalette::Highlight, QColor(0xc7, 0xd7, 0xf0));
    palette.setColor(QPalette::HighlightedText, QColor(0x1f, 0x23, 0x28));
  }
  palette.setColor(QPalette::Button, palette.color(QPalette::Window));
  palette.setColor(QPalette::ButtonText, palette.color(QPalette::Text));
  palette.setColor(QPalette::ToolTipBase, palette.color(QPalette::Base));
  palette.setColor(QPalette::ToolTipText, palette.color(QPalette::Text));
  palette.setColor(QPalette::PlaceholderText,
                   theme == ui::Theme::Dark ? QColor("#8b96a8") : QColor("#8b93a3"));
  palette.setColor(QPalette::Disabled, QPalette::Text, QColor("#7e8796"));
  palette.setColor(QPalette::Disabled, QPalette::ButtonText, QColor("#7e8796"));
  return palette;
}

QIcon navigationIcon(bool source, const QColor& color) {
  QPixmap pixmap(40, 40);
  pixmap.fill(Qt::transparent);
  QPainter painter(&pixmap);
  painter.setRenderHint(QPainter::Antialiasing);
  painter.setPen(QPen(color, 2.5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
  if (source) {
    painter.drawPolyline(QPolygonF({QPointF(13, 12), QPointF(5, 20), QPointF(13, 28)}));
    painter.drawPolyline(QPolygonF({QPointF(27, 12), QPointF(35, 20), QPointF(27, 28)}));
    painter.drawLine(QPointF(23, 9), QPointF(17, 31));
  } else {
    painter.drawRoundedRect(QRectF(5, 7, 30, 26), 2, 2);
    painter.drawLine(QPointF(15, 8), QPointF(15, 32));
    painter.drawLine(QPointF(8, 13), QPointF(11, 13));
    painter.drawLine(QPointF(8, 18), QPointF(11, 18));
  }
  return QIcon(pixmap);
}

}  // namespace

MainWindow::MainWindow(QWidget* parent, bool benchmark)
    : QMainWindow(parent), m_benchMode(benchmark) {
  const QRect available = screen()->availableGeometry();
  setMinimumSize(qMin(760, available.width()), qMin(540, available.height()));
  resize(qMin(1180, int(available.width() * 0.9)), qMin(820, int(available.height() * 0.9)));
  QSettings settings;
  if (!restoreGeometry(settings.value(QStringLiteral("window/geometry")).toByteArray()))
    move(available.center() - rect().center());
  // Two documents over one buffer: each mode keeps its own undo history.
  m_view = new ui::ViewSwitch(this);
  m_document = m_view->active();
  m_editor = new ui::Editor(this);
  m_editor->setObjectName(QStringLiteral("writingEditor"));
  m_editor->setPlaceholderText(tr("Start writing, or open a Markdown file..."));
  m_editor->setAccessibleName(tr("Markdown editor"));
  m_editor->viewport()->installEventFilter(this);
  m_view->showActiveIn(m_editor);
  static_cast<ui::Editor*>(m_editor)->setImagePasteHandler(
      [this](const QMimeData* source) { return pasteImages(source); });
  connect(static_cast<ui::Editor*>(m_editor), &ui::Editor::markdownFileDropped, this,
          &MainWindow::openFile);
  connect(static_cast<ui::Editor*>(m_editor), &ui::Editor::tablePasteRejected, this,
          [this](const QString& error) { statusBar()->showMessage(error, 4000); });
  connect(static_cast<ui::Editor*>(m_editor), &ui::Editor::linkActivated, this,
          &MainWindow::followLink);
  connect(static_cast<ui::Editor*>(m_editor), &ui::Editor::editLinkRequested, this,
          &MainWindow::insertLinkDialog);
  connect(static_cast<ui::Editor*>(m_editor), &ui::Editor::imageFileStatus, this,
          [this](const QString& message) { statusBar()->showMessage(message, 5000); });
  m_findBar = new ui::FindBar(this);

  auto* central = new QWidget(this);
  auto* centralLayout = new QVBoxLayout(central);
  centralLayout->setContentsMargins(0, 0, 0, 0);
  centralLayout->addWidget(m_editor);
  centralLayout->addWidget(m_findBar);
  setCentralWidget(central);

  m_tree = new ui::FileTree(this);
  m_tree->setOptions(platform::fileBrowserOptions());
  connect(m_tree, &ui::FileTree::optionsChanged, this,
          [this] { platform::setFileBrowserOptions(m_tree->options()); });
  m_tree->setAccessibleName(tr("Files"));
  m_sidebarDock = new QDockWidget(tr("Files"), this);
  m_sidebarDock->setObjectName(QStringLiteral("navigationSidebar"));
  m_sidebarDock->setFeatures(QDockWidget::NoDockWidgetFeatures);
  m_sidebarDock->setTitleBarWidget(new QWidget(m_sidebarDock));
  m_sidebarDock->setMinimumWidth(220);
  auto* sidebar = new QWidget(m_sidebarDock);
  sidebar->setObjectName(QStringLiteral("sidebarSurface"));
  auto* sidebarLayout = new QVBoxLayout(sidebar);
  sidebarLayout->setContentsMargins(0, 0, 0, 0);
  sidebarLayout->setSpacing(0);
  m_sidebarRootLabel = new QLabel(tr("Workspace"), sidebar);
  m_sidebarRootLabel->setObjectName(QStringLiteral("workspaceName"));
  auto* sidebarHeader = new QHBoxLayout;
  sidebarHeader->setContentsMargins(0, 0, 8, 0);
  sidebarHeader->addWidget(m_sidebarRootLabel, 1);
  m_folderLocationsButton = new QToolButton(sidebar);
  m_folderLocationsButton->setObjectName(QStringLiteral("folderLocations"));
  m_folderLocationsButton->setIcon(style()->standardIcon(QStyle::SP_DirOpenIcon));
  m_folderLocationsButton->setPopupMode(QToolButton::InstantPopup);
  m_folderLocationsButton->setToolTip(tr("Recent and Pinned Folders"));
  m_folderLocationsButton->setAccessibleName(tr("Recent and Pinned Folders"));
  m_folderLocationsMenu = new ui::FolderLocationsMenu(m_folderLocationsButton);
  m_folderLocationsButton->setMenu(m_folderLocationsMenu);
  connect(m_folderLocationsMenu, &ui::FolderLocationsMenu::folderActivated, this,
          &MainWindow::openFolderPath);
  connect(m_folderLocationsMenu, &ui::FolderLocationsMenu::browseRequested, this,
          &MainWindow::openFolder);
  connect(m_folderLocationsMenu, &ui::FolderLocationsMenu::pinLimitReached, this, [this] {
    statusBar()->showMessage(tr("Cannot pin folder. Check the folder and settings (limit: 16)."),
                             5000);
  });
  sidebarHeader->addWidget(m_folderLocationsButton);
  m_fileOptionsButton = new QToolButton(sidebar);
  m_fileOptionsButton->setObjectName(QStringLiteral("fileOptions"));
  m_fileOptionsButton->setText(QStringLiteral("⋯"));
  m_fileOptionsButton->setToolTip(tr("File Options"));
  m_fileOptionsButton->setAccessibleName(tr("File Options"));
  m_fileOptionsButton->setPopupMode(QToolButton::InstantPopup);
  m_fileOptionsButton->setMenu(m_tree->createOptionsMenu(m_fileOptionsButton));
  sidebarHeader->addWidget(m_fileOptionsButton);
  sidebarLayout->addLayout(sidebarHeader);
  m_sidebarTabs = new QTabWidget(sidebar);
  m_sidebarTabs->setObjectName(QStringLiteral("sidebarTabs"));
  m_sidebarTabs->setDocumentMode(true);
  m_sidebarTabs->addTab(m_tree, tr("Files"));
  m_outlinePanel = new ui::OutlinePanel(m_editor, sidebar);
  m_outlinePanel->setDocument(m_document);
  m_sidebarTabs->addTab(m_outlinePanel, tr("Outline"));
  m_workspaceSearch = new ui::WorkspaceSearchPanel(sidebar);
  m_sidebarTabs->addTab(m_workspaceSearch, tr("Search"));
  connect(m_workspaceSearch, &ui::WorkspaceSearchPanel::hitActivated, this,
          &MainWindow::openSearchHit);
  sidebarLayout->addWidget(m_sidebarTabs);
  m_sidebarDock->setWidget(sidebar);
  m_sidebarDock->hide();
  addDockWidget(Qt::LeftDockWidgetArea, m_sidebarDock);
  setAcceptDrops(true);
  connect(m_sidebarTabs, &QTabWidget::currentChanged, this, [this] { updateSearchFocusGuard(); });
  connect(m_sidebarDock, &QDockWidget::visibilityChanged, this,
          [this] { updateSearchFocusGuard(); });

  connect(m_tree, &ui::FileTree::entryActivated, this, &MainWindow::openTreeEntry);
  connect(m_tree, &ui::FileTree::entryOpenInNewWindow, this, &MainWindow::openNewWindow);
  connect(m_tree, &ui::FileTree::fileRenamed, this, &MainWindow::treeFileRenamed);
  connect(m_tree, &ui::FileTree::fileRemoved, this, &MainWindow::treeFileRemoved);
  connect(m_tree, &ui::FileTree::directoryChanged, this, &MainWindow::checkCurrentFileConflict);

  connect(m_findBar, &ui::FindBar::patternChanged, this, &MainWindow::refreshMatches);
  connect(m_findBar, &ui::FindBar::findNext, this, &MainWindow::searchNext);
  connect(m_findBar, &ui::FindBar::findPrevious, this, &MainWindow::searchPrevious);
  connect(m_findBar, &ui::FindBar::replaceOne, this, &MainWindow::replaceOne);
  connect(m_findBar, &ui::FindBar::replaceAll, this, &MainWindow::replaceAll);

  m_autoSave = new ui::AutoSave(this);
  m_autoSave->setCallback([this] {
    writeRecoveryDraft();
    autoSaveNow();
  });

  m_themePreference = platform::themePreference();
  m_autoSaveEnabled = settings.value(QStringLiteral("editor/autoSave"), true).toBool();

  QMenu* fileMenu = menuBar()->addMenu(tr("File"));

  QAction* openAction = new QAction(tr("Open"), this);
  openAction->setShortcut(QKeySequence::Open);
  fileMenu->addAction(openAction);
  connect(openAction, &QAction::triggered, this, &MainWindow::openDialog);
  auto* importAction = fileMenu->addAction(tr("Import HTML..."));
  importAction->setObjectName(QStringLiteral("importHtml"));
  connect(importAction, &QAction::triggered, this, &MainWindow::importHtmlDialog);

  QAction* saveAction = new QAction(tr("Save"), this);
  saveAction->setShortcut(QKeySequence::Save);
  fileMenu->addAction(saveAction);
  connect(saveAction, &QAction::triggered, this, &MainWindow::save);

  QAction* saveAsAction = new QAction(tr("Save As"), this);
  saveAsAction->setShortcut(QKeySequence::SaveAs);
  fileMenu->addAction(saveAsAction);
  connect(saveAsAction, &QAction::triggered, this, &MainWindow::saveAs);

  QAction* exportHtmlAction = new QAction(tr("Export HTML"), this);
  exportHtmlAction->setObjectName(QStringLiteral("exportHtml"));
  exportHtmlAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+E")));
  fileMenu->addAction(exportHtmlAction);
  connect(exportHtmlAction, &QAction::triggered, this, &MainWindow::exportHtml);
  auto* plainHtmlAction = fileMenu->addAction(tr("Export HTML (without styles)"));
  plainHtmlAction->setObjectName(QStringLiteral("exportPlainHtml"));
  connect(plainHtmlAction, &QAction::triggered, this, [this] { exportHtmlDocument(false); });
  auto* imageExport = fileMenu->addAction(tr("Export Image"));
  imageExport->setObjectName(QStringLiteral("exportImage"));
  connect(imageExport, &QAction::triggered, this, &MainWindow::exportImage);

  QAction* exportPdfAction = new QAction(tr("Export PDF"), this);
  exportPdfAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+P")));
  fileMenu->addAction(exportPdfAction);
  connect(exportPdfAction, &QAction::triggered, this, &MainWindow::exportPdf);
  QAction* exportDocxAction = fileMenu->addAction(tr("Export DOCX"));
  exportDocxAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+X")));
  connect(exportDocxAction, &QAction::triggered, this, &MainWindow::exportDocx);
  auto* exportEpubAction = fileMenu->addAction(tr("Export EPUB"));
  connect(exportEpubAction, &QAction::triggered, this, &MainWindow::exportEpub);
  m_repeatExportAction = fileMenu->addAction(tr("Export with Previous Settings"));
  m_repeatExportAction->setObjectName(QStringLiteral("repeatExport"));
  m_repeatExportAction->setEnabled(false);
  connect(m_repeatExportAction, &QAction::triggered, this, &MainWindow::repeatExport);
  m_overwriteExportAction = fileMenu->addAction(tr("Export and Overwrite with Previous Settings"));
  m_overwriteExportAction->setObjectName(QStringLiteral("overwritePreviousExport"));
  m_overwriteExportAction->setEnabled(false);
  connect(m_overwriteExportAction, &QAction::triggered, this, &MainWindow::overwritePreviousExport);

  QAction* recoverAction = new QAction(tr("Recover Draft"), this);
  recoverAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+R")));
  fileMenu->addAction(recoverAction);
  connect(recoverAction, &QAction::triggered, this, &MainWindow::recoverLatestDraft);

  auto* reopenClosed = fileMenu->addAction(tr("Reopen Closed File"));
  reopenClosed->setObjectName(QStringLiteral("reopenClosedFile"));
  connect(reopenClosed, &QAction::triggered, this, &MainWindow::reopenClosedFile);
  m_recentMenu = fileMenu->addMenu(tr("Recent Files"));
  m_recentMenu->setObjectName(QStringLiteral("recentFilesMenu"));
  m_recentMenu->setToolTipsVisible(true);
  fileMenu->addMenu(m_folderLocationsMenu);
  connect(m_recentMenu, &QMenu::triggered, this, &MainWindow::openRecentEntry);
  connect(m_recentMenu, &QMenu::aboutToShow, this, &MainWindow::rebuildRecentMenu);
  rebuildRecentMenu();

  QMenu* editMenu = menuBar()->addMenu(tr("Edit"));
  m_undoAction = new QAction(tr("Undo"), this);
  m_undoAction->setToolTip(tr("Undo"));
  m_undoAction->setShortcut(QKeySequence::Undo);
  m_undoAction->setEnabled(false);
  editMenu->addAction(m_undoAction);
  connect(m_undoAction, &QAction::triggered, this, &MainWindow::undo);

  m_redoAction = new QAction(tr("Redo"), this);
  m_redoAction->setToolTip(tr("Redo"));
  m_redoAction->setShortcut(QKeySequence::Redo);
  m_redoAction->setEnabled(false);
  editMenu->addAction(m_redoAction);
  connect(m_redoAction, &QAction::triggered, this, &MainWindow::redo);

  m_pasteMarkdownAction = new QAction(tr("Paste Markdown"), this);
  m_pasteMarkdownAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+V")));
  m_pasteMarkdownAction->setToolTip(tr("Paste Markdown"));
  editMenu->addAction(m_pasteMarkdownAction);
  connect(m_pasteMarkdownAction, &QAction::triggered, this, &MainWindow::pasteAsMarkdown);

  m_findAction = new QAction(tr("Find"), this);
  m_findAction->setToolTip(tr("Find"));
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
  m_folderSearchAction->setToolTip(tr("Search in Folder"));
  m_folderSearchAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+F")));
  editMenu->addAction(m_folderSearchAction);
  connect(m_folderSearchAction, &QAction::triggered, this, &MainWindow::searchInFolder);

  m_folderAction = new QAction(tr("Open Folder"), this);
  m_folderAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+O")));
  fileMenu->addAction(m_folderAction);
  connect(m_folderAction, &QAction::triggered, this, &MainWindow::openFolder);

  QMenu* viewMenu = menuBar()->addMenu(tr("View"));
  m_sourceAction = new QAction(tr("Source View"), this);
  m_sourceAction->setCheckable(true);
  m_sourceAction->setToolTip(tr("Source View"));
  m_sourceAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+/")));
  viewMenu->addAction(m_sourceAction);
  connect(m_sourceAction, &QAction::triggered, this, &MainWindow::toggleSourceView);

  m_themeAction = new QAction(tr("Theme"), this);
  m_themeAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+D")));
  viewMenu->addAction(m_themeAction);
  connect(m_themeAction, &QAction::triggered, this, &MainWindow::cycleTheme);

  m_outlineAction = new QAction(tr("Outline"), this);
  m_outlineAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+T")));
  m_outlineAction->setToolTip(tr("Outline"));
  viewMenu->addAction(m_outlineAction);
  connect(m_outlineAction, &QAction::triggered, this, &MainWindow::toggleOutline);

  QMenu* helpMenu = menuBar()->addMenu(tr("Help"));
  m_updateAction = new QAction(tr("Check for Updates"), this);
  m_updateAction->setToolTip(tr("Check for Updates"));
#ifdef TYPIST_HAS_NETWORK
  m_updateAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+U")));
  connect(m_updateAction, &QAction::triggered, this, &MainWindow::checkForUpdates);
#endif
  helpMenu->addAction(m_updateAction);

  m_sidebarAction = new QAction(tr("Show Files"), this);
  m_sidebarAction->setCheckable(true);
  m_sidebarAction->setToolTip(tr("Show Files"));
  m_sidebarAction->setShortcut(QKeySequence(QStringLiteral("Ctrl+Shift+L")));
  viewMenu->addAction(m_sidebarAction);
  connect(m_sidebarAction, &QAction::triggered, this, &MainWindow::toggleSidebar);
  connect(m_sidebarDock, &QDockWidget::visibilityChanged, m_sidebarAction, &QAction::setChecked);

  QMenu* settingsMenu = new QMenu(tr("Settings"), menuBar());
  menuBar()->insertMenu(helpMenu->menuAction(), settingsMenu);
  createLaunchMenu(settingsMenu);
  createExportMenu(settingsMenu);
  QMenu* languageMenu = settingsMenu->addMenu(tr("Language"));
  auto* languageGroup = new QActionGroup(this);
  const QStringList languageIds = {QStringLiteral("system"), QStringLiteral("zh"),
                                   QStringLiteral("en")};
  const QStringList languageNames = {tr("Follow system"), QStringLiteral("简体中文"),
                                     QStringLiteral("English")};
  for (int i = 0; i < languageIds.size(); ++i) {
    QAction* action = languageMenu->addAction(languageNames.at(i));
    action->setCheckable(true);
    action->setData(languageIds.at(i));
    action->setObjectName(QStringLiteral("language_") + languageIds.at(i));
    action->setChecked(platform::languagePreference() == languageIds.at(i));
    languageGroup->addAction(action);
    connect(action, &QAction::triggered, this,
            [this, action] { setLanguage(action->data().toString()); });
  }
  QMenu* appearanceMenu = settingsMenu->addMenu(tr("Appearance"));
  auto* appearanceGroup = new QActionGroup(this);
  const QStringList appearanceIds = {QStringLiteral("system"), QStringLiteral("light"),
                                     QStringLiteral("dark")};
  const QStringList appearanceNames = {tr("Follow system"), tr("Light"), tr("Dark")};
  for (int i = 0; i < appearanceIds.size(); ++i) {
    QAction* action = appearanceMenu->addAction(appearanceNames.at(i));
    action->setCheckable(true);
    action->setData(appearanceIds.at(i));
    action->setObjectName(QStringLiteral("appearance_") + appearanceIds.at(i));
    appearanceGroup->addAction(action);
    connect(action, &QAction::triggered, this, [this, action] {
      m_themePreference = action->data().toString();
      platform::setThemePreference(m_themePreference);
      applyTheme();
    });
  }

  createEditingActions(fileMenu, editMenu, viewMenu, settingsMenu);

  statusBar()->setSizeGripEnabled(false);
  auto* sidebarButton = new QToolButton(this);
  sidebarButton->setObjectName(QStringLiteral("sidebarToggle"));
  sidebarButton->setDefaultAction(m_sidebarAction);
  sidebarButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  sidebarButton->setIconSize(QSize(17, 17));
  statusBar()->addWidget(sidebarButton);
  auto* sourceButton = new QToolButton(this);
  sourceButton->setObjectName(QStringLiteral("sourceToggle"));
  sourceButton->setDefaultAction(m_sourceAction);
  sourceButton->setToolButtonStyle(Qt::ToolButtonIconOnly);
  sourceButton->setIconSize(QSize(17, 17));
  statusBar()->addWidget(sourceButton);
  m_detailsLabel = new QLabel(this);
  m_detailsLabel->setObjectName(QStringLiteral("documentStatistics"));
  m_detailsLabel->setCursor(Qt::PointingHandCursor);
  m_detailsLabel->installEventFilter(this);
  statusBar()->addPermanentWidget(m_detailsLabel);

  // Keep original labels so live language changes preserve the document and undo history.
  for (QAction* action : findChildren<QAction*>())
    if (!action->property("fileBrowserSource").isValid() &&
        !action->property("folderLocationAction").isValid() && action->parent() != m_outlinePanel)
      action->setProperty("translationSource", action->text());
  m_translator = new ui::AppTranslator(this);
  setLanguage(platform::languagePreference());

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
      if (!m_benchMode) m_autoSave->notifyChanged();
      updateTitleAndStatus();
    });
    connect(doc, &QTextDocument::undoAvailable, this, [this, doc](bool available) {
      if (doc == m_document) m_undoAction->setEnabled(available);
    });
    connect(doc, &QTextDocument::redoAvailable, this, [this, doc](bool available) {
      if (doc == m_document) m_redoAction->setEnabled(available);
    });
  }

  updateTitleAndStatus();
  const int drafts = typist::ui::draftPaths().size();
  auto* writingEditor = static_cast<ui::Editor*>(m_editor);
  connect(writingEditor, &ui::Editor::paragraphSourceChanged, this, [this, writingEditor] {
    m_dirty = true;
    if (!m_benchMode) m_autoSave->notifyChanged();
    m_undoAction->setEnabled(writingEditor->paragraphSourceUndoAvailable());
    m_redoAction->setEnabled(writingEditor->paragraphSourceUndoAvailable(true));
    updateTitleAndStatus();
  });
  connect(writingEditor, &ui::Editor::paragraphSourceClosed, this, [this] {
    const QString current = currentMarkdown();
    m_dirty = current != m_savedMarkdown;
    if (m_dirty) {
      const auto now = core::parseMarkdown(current.toStdString()),
                 saved = core::parseMarkdown(m_savedMarkdown.toStdString());
      if (now.ok && saved.ok && core::astEqual(now.blocks, saved.blocks)) m_dirty = false;
    }
    m_undoAction->setEnabled(m_document->isUndoAvailable());
    m_redoAction->setEnabled(m_document->isRedoAvailable());
    if (!m_benchMode) {
      if (m_dirty)
        m_autoSave->notifyChanged();
      else {
        m_autoSave->cancelPending();
        ui::removeDraft(m_currentPath);
      }
    }
    updateTitleAndStatus();
  });
  if (drafts > 0)
    statusBar()->showMessage(
        tr("%1 drafts can be recovered with File > Recover Draft").arg(drafts));
}

void MainWindow::createEditingActions(QMenu* file, QMenu* edit, QMenu* view, QMenu* settings) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const auto command = [this](QMenu* menu, const QString& id, const QString& text,
                              const QString& shortcut, const std::function<void()>& callback) {
    QAction* action = menu->addAction(text);
    action->setObjectName(id);
    if (!shortcut.isEmpty()) action->setShortcut(QKeySequence(shortcut));
    connect(action, &QAction::triggered, this, [callback] { callback(); });
    return action;
  };
  QAction* newAction = command(file, QStringLiteral("newDocument"), tr("New"),
                               QStringLiteral("Ctrl+N"), [this] { newDocument(); });
  file->insertAction(file->actions().first(), newAction);
  command(file, QStringLiteral("newWindow"), tr("New Window"), QStringLiteral("Ctrl+Shift+N"),
          [this] { openNewWindow(); });
  command(file, QStringLiteral("openQuickly"), tr("Open Quickly"), QStringLiteral("Ctrl+P"),
          [this] { openQuickly(); });
  command(file, QStringLiteral("closeDocument"), tr("Close"), QStringLiteral("Ctrl+W"),
          [this] { close(); });

  edit->addSeparator();
  command(edit, QStringLiteral("cut"), tr("Cut"), QStringLiteral("Ctrl+X"),
          [editor] { editor->cut(); });
  command(edit, QStringLiteral("copy"), tr("Copy"), QStringLiteral("Ctrl+C"),
          [editor] { editor->copy(); });
  command(edit, QStringLiteral("paste"), tr("Paste"), QStringLiteral("Ctrl+V"),
          [editor] { editor->paste(); });
  auto* selectAll = command(edit, QStringLiteral("selectAll"), tr("Select All"),
                            QStringLiteral("Ctrl+A"), [editor] { editor->selectAll(); });
  selectAll->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  editor->addAction(selectAll);
  auto* selectMenu = edit->addMenu(tr("Select Range"));
  selectMenu->setObjectName(QStringLiteral("selectRangeMenu"));
  command(selectMenu, QStringLiteral("selectWord"), tr("Select Word"), QStringLiteral("Ctrl+D"),
          [editor] { editor->selectRange(editor::SelectionRange::Word); });
  command(selectMenu, QStringLiteral("selectSentence"), tr("Select Line / Sentence"),
          QStringLiteral("Ctrl+L"),
          [editor] { editor->selectRange(editor::SelectionRange::LineOrSentence); });
  command(selectMenu, QStringLiteral("selectParagraph"), tr("Select Paragraph / Block"), QString(),
          [editor] { editor->selectRange(editor::SelectionRange::Paragraph); });
  command(selectMenu, QStringLiteral("selectStyle"), tr("Select Styled Scope / Table Cell"),
          QStringLiteral("Ctrl+E"),
          [editor] { editor->selectRange(editor::SelectionRange::StyledScope); });
  auto* deleteMenu = edit->addMenu(tr("Delete Range"));
  deleteMenu->setObjectName(QStringLiteral("deleteRangeMenu"));
  command(deleteMenu, QStringLiteral("deleteWord"), tr("Delete Word"), QString(),
          [editor] { editor->deleteRange(editor::SelectionRange::Word); });
  command(deleteMenu, QStringLiteral("deleteSentence"), tr("Delete Line / Sentence"),
          QStringLiteral("Ctrl+Shift+Backspace"), [this, editor] {
            const auto cursor = editor->textCursor();
            const bool deletingRows =
                cursor.currentTable() && (!cursor.hasSelection() || cursor.hasComplexSelection());
            if (!editor->deleteRange(editor::SelectionRange::LineOrSentence) && deletingRows)
              statusBar()->showMessage(tr("Keep the table header and at least one body row"), 2500);
          });
  command(deleteMenu, QStringLiteral("deleteParagraph"), tr("Delete Paragraph / Block"), QString(),
          [editor] { editor->deleteRange(editor::SelectionRange::Paragraph); });
  command(deleteMenu, QStringLiteral("deleteStyle"), tr("Delete Styled Scope / Table Cell"),
          QString(), [editor] { editor->deleteRange(editor::SelectionRange::StyledScope); });
  command(edit, QStringLiteral("copyMarkdown"), tr("Copy as Markdown"),
          QStringLiteral("Ctrl+Shift+C"),
          [editor] { editor->copySelection(ui::Editor::CopyFormat::Markdown); });
  command(edit, QStringLiteral("copyPlainText"), tr("Copy as Plain Text"), QString(),
          [editor] { editor->copySelection(ui::Editor::CopyFormat::PlainText); });
  command(edit, QStringLiteral("copyHtmlCode"), tr("Copy as HTML Code"), QString(),
          [editor] { editor->copySelection(ui::Editor::CopyFormat::HtmlCode); });
  command(edit, QStringLiteral("copyWithoutTheme"), tr("Copy without Theme Styling"), QString(),
          [editor] { editor->copySelection(ui::Editor::CopyFormat::WithoutTheme); });
  command(edit, QStringLiteral("pastePlain"), tr("Paste as Plain Text"),
          QStringLiteral("Ctrl+Alt+Shift+V"),
          [editor] { editor->insertPlainText(QApplication::clipboard()->text()); });
  command(edit, QStringLiteral("pasteHtml"), tr("Paste HTML as Markdown"),
          QStringLiteral("Ctrl+Alt+V"), [this] { pasteHtmlAsMarkdown(); });
  command(edit, QStringLiteral("replace"), tr("Replace"), QStringLiteral("Ctrl+H"), [this] {
    static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
    m_findBar->openReplace();
    refreshMatches();
  });
  command(edit, QStringLiteral("editParagraphSource"), tr("Edit Paragraph Markdown"),
          QStringLiteral("Ctrl+Return"), [this, editor] {
            if (editor->exitTable()) return;
            if (editor->exitCodeBlock()) return;
            if (!editor->startParagraphSourceEdit())
              statusBar()->showMessage(tr("Place the cursor in a normal paragraph or heading"),
                                       2500);
          });

  auto* paragraph = new QMenu(tr("Paragraph"), menuBar());
  menuBar()->insertMenu(view->menuAction(), paragraph);
  command(paragraph, QStringLiteral("paragraph_body"), tr("Normal Paragraph"),
          QStringLiteral("Ctrl+0"),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::Body); });
  for (int level = 1; level <= 6; ++level) {
    QAction* action = command(
        paragraph, QStringLiteral("heading_%1").arg(level), tr("Heading %1").arg(level),
        QStringLiteral("Ctrl+%1").arg(level),
        [editor, level] { editor->formatParagraph(editor::ParagraphStyle::Heading, level); });
    action->setProperty("translationTemplate", QStringLiteral("Heading %1"));
    action->setProperty("translationNumber", level);
  }
  paragraph->addSeparator();
  auto* promote = command(paragraph, QStringLiteral("promoteHeading"), tr("Increase Heading Level"),
                          QStringLiteral("Ctrl+="), [editor] { editor->shiftHeadingLevels(-1); });
  auto* demote = command(paragraph, QStringLiteral("demoteHeading"), tr("Decrease Heading Level"),
                         QStringLiteral("Ctrl+-"), [editor] { editor->shiftHeadingLevels(1); });
  for (auto* action : {promote, demote}) {
    action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
    action->setEnabled(editor->renderedEditingAvailable());
    editor->addAction(action);
    connect(editor, &ui::Editor::renderedEditingAvailableChanged, action, &QAction::setEnabled);
  }
  paragraph->addSeparator();
  command(paragraph, QStringLiteral("paragraph_bullet"), tr("Unordered List"),
          QStringLiteral("Ctrl+Shift+]"),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::BulletList); });
  command(paragraph, QStringLiteral("paragraph_ordered"), tr("Ordered List"),
          QStringLiteral("Ctrl+Shift+["),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::OrderedList); });
  command(paragraph, QStringLiteral("paragraph_task"), tr("Task List"), QString(),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::TaskList); });
  command(paragraph, QStringLiteral("paragraph_quote"), tr("Quote"), QStringLiteral("Ctrl+Shift+Q"),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::Quote); });
  command(paragraph, QStringLiteral("paragraph_code"), tr("Code Fences"),
          QStringLiteral("Ctrl+Shift+K"),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::Code); });
  command(paragraph, QStringLiteral("paragraph_rule"), tr("Horizontal Rule"), QString(),
          [editor] { editor->formatParagraph(editor::ParagraphStyle::Rule); });
  command(paragraph, QStringLiteral("insert_page_break"), tr("Insert Page Break"), QString(),
          [this, editor] {
            if (!editor->insertPageBreak())
              statusBar()->showMessage(tr("Place the cursor in a normal paragraph or heading"),
                                       2500);
          });
  command(paragraph, QStringLiteral("insert_math"), tr("Math Block"),
          QStringLiteral("Ctrl+Shift+M"),
          [editor] { editor->insertAdvancedContent(QStringLiteral("math")); });
  command(paragraph, QStringLiteral("codeLanguage"), tr("Code Language"), QString(),
          [this, editor] {
            if (m_sourceMode ||
                !editor->textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty)) {
              statusBar()->showMessage(tr("Place the cursor in a code block"), 2500);
              return;
            }
            bool ok = false;
            const QString current =
                editor->textCursor().blockFormat().property(editor::kCodeInfoProperty).toString();
            auto languages = editor::codeLanguages();
            languages.prepend(QString());
            if (!languages.contains(current)) languages.append(current);
            const QString chosen = QInputDialog::getItem(
                this, tr("Code Language"), tr("Language identifier (empty for plain text)"),
                languages, qMax(0, int(languages.indexOf(current))), true, &ok);
            if (ok) editor->setCodeLanguage(chosen);
          });
  command(paragraph, QStringLiteral("copyCode"), tr("Copy Code Block"), QString(), [this, editor] {
    if (!editor->copyCodeBlock())
      statusBar()->showMessage(tr("Place the cursor in a code block"), 2500);
  });
  command(paragraph, QStringLiteral("autoIndentCode"), tr("Auto Indent Code"), QString(),
          [this, editor] {
            if (!editor->autoIndentCode())
              statusBar()->showMessage(
                  tr("Auto indent is available in C, C++, Go, JSON, CSS and SCSS code blocks"),
                  3000);
          });
  command(paragraph, QStringLiteral("exitCode"), tr("Exit Code Block"), QString(), [this, editor] {
    if (!editor->exitCodeBlock())
      statusBar()->showMessage(tr("Place the cursor in a code block"), 2500);
  });
  command(paragraph, QStringLiteral("insert_mermaid"), tr("Mermaid Diagram"), QString(),
          [editor] { editor->insertAdvancedContent(QStringLiteral("mermaid")); });
  command(paragraph, QStringLiteral("insert_toc"), tr("Table of Contents"), QString(),
          [editor] { editor->insertAdvancedContent(QStringLiteral("toc")); });
  command(paragraph, QStringLiteral("indent"), tr("Indent"), QStringLiteral("Ctrl+["),
          [editor] { editor->changeListIndent(1); });
  command(paragraph, QStringLiteral("outdent"), tr("Outdent"), QStringLiteral("Ctrl+]"),
          [editor] { editor->changeListIndent(-1); });
  auto* taskStatus = paragraph->addMenu(tr("Task Status"));
  command(taskStatus, QStringLiteral("task_toggle"), tr("Toggle Task Status"),
          QStringLiteral("Ctrl+Shift+Space"),
          [editor] { editor->changeTaskStatus(editor::TaskStatus::Toggle); });
  command(taskStatus, QStringLiteral("task_done"), tr("Mark as Complete"), QString(),
          [editor] { editor->changeTaskStatus(editor::TaskStatus::Done); });
  command(taskStatus, QStringLiteral("task_todo"), tr("Mark as Incomplete"), QString(),
          [editor] { editor->changeTaskStatus(editor::TaskStatus::Todo); });

  auto* format = new QMenu(tr("Format"), menuBar());
  menuBar()->insertMenu(view->menuAction(), format);
  command(format, QStringLiteral("format_bold"), tr("Strong"), QStringLiteral("Ctrl+B"),
          [editor] { editor->formatInline(editor::InlineStyle::Bold); });
  command(format, QStringLiteral("format_italic"), tr("Emphasis"), QStringLiteral("Ctrl+I"),
          [editor] { editor->formatInline(editor::InlineStyle::Italic); });
  auto* underline =
      command(format, QStringLiteral("format_underline"), tr("Underline"), QStringLiteral("Ctrl+U"),
              [editor] { editor->formatInline(editor::InlineStyle::Underline); });
  underline->setShortcutContext(Qt::WidgetWithChildrenShortcut);
  editor->addAction(underline);
  command(format, QStringLiteral("format_strike"), tr("Strikethrough"),
          QStringLiteral("Alt+Shift+5"),
          [editor] { editor->formatInline(editor::InlineStyle::Strike); });
  command(format, QStringLiteral("format_code"), tr("Inline Code"), QStringLiteral("Ctrl+Shift+`"),
          [editor] { editor->formatInline(editor::InlineStyle::Code); });
  command(format, QStringLiteral("format_link"), tr("Hyperlink"), QStringLiteral("Ctrl+K"),
          [this] { insertLinkDialog(); });
  command(format, QStringLiteral("inline_math"), tr("Inline Math"), QString(),
          [editor] { editor->insertAdvancedContent(QStringLiteral("inline-math")); });
  command(format, QStringLiteral("insert_footnote"), tr("Footnote"), QString(),
          [editor] { editor->insertAdvancedContent(QStringLiteral("footnote")); });
  command(format, QStringLiteral("insert_image"), tr("Image"), QStringLiteral("Ctrl+Shift+I"),
          [this] {
            const QString path = QFileDialog::getOpenFileName(
                this, tr("Insert Image"),
                m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).absolutePath(),
                QStringLiteral("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp)"));
            if (!path.isEmpty()) insertImageLink(path);
          });
  QAction* copyImages = command(settings, QStringLiteral("copyImagesToAssets"),
                                tr("Copy Images to Document Assets"), QString(), [] {});
  copyImages->setCheckable(true);
  copyImages->setChecked(platform::copyImagesToAssets());
  connect(copyImages, &QAction::toggled, this, &platform::setCopyImagesToAssets);
  command(format, QStringLiteral("format_clear"), tr("Clear Format"), QStringLiteral("Ctrl+\\"),
          [editor] { editor->formatInline(editor::InlineStyle::Clear); });

  auto* table = paragraph->addMenu(tr("Table"));
  command(table, QStringLiteral("insert_table"), tr("Insert Table"), QStringLiteral("Ctrl+T"),
          [this] { insertTableDialog(); });
  const auto tableAction = [&](const QString& id, const QString& text,
                               editor::TableCommand operation) {
    command(table, id, text, QString(), [this, editor, operation] {
      if (!editor->changeTable(operation))
        statusBar()->showMessage(tr("Place the cursor in a table body cell"), 2500);
    });
  };
  tableAction(QStringLiteral("table_row_above"), tr("Insert Row Above"),
              editor::TableCommand::RowAbove);
  tableAction(QStringLiteral("table_row_below"), tr("Insert Row Below"),
              editor::TableCommand::RowBelow);
  tableAction(QStringLiteral("table_delete_row"), tr("Delete Row"),
              editor::TableCommand::DeleteRow);
  tableAction(QStringLiteral("table_column_before"), tr("Insert Column Before"),
              editor::TableCommand::ColumnBefore);
  tableAction(QStringLiteral("table_column_after"), tr("Insert Column After"),
              editor::TableCommand::ColumnAfter);
  tableAction(QStringLiteral("table_delete_column"), tr("Delete Column"),
              editor::TableCommand::DeleteColumn);
  table->addSeparator();
  tableAction(QStringLiteral("table_align_left"), tr("Align Left"),
              editor::TableCommand::AlignLeft);
  tableAction(QStringLiteral("table_align_center"), tr("Align Center"),
              editor::TableCommand::AlignCenter);
  tableAction(QStringLiteral("table_align_right"), tr("Align Right"),
              editor::TableCommand::AlignRight);
  tableAction(QStringLiteral("table_delete"), tr("Delete Table"),
              editor::TableCommand::DeleteTable);
  table->addSeparator();
  command(table, QStringLiteral("table_copy_markdown"), tr("Copy Table as Markdown"), QString(),
          [editor] { editor->copyTable(); });
  command(table, QStringLiteral("table_copy_spreadsheet"), tr("Copy Table for Spreadsheet"),
          QString(), [editor] { editor->copyTable(true); });
  command(table, QStringLiteral("table_exit"), tr("Exit Table"), QString(),
          [editor] { editor->exitTable(); });

  view->addSeparator();
  QAction* focus =
      command(view, QStringLiteral("focusMode"), tr("Focus Mode"), QStringLiteral("F8"), [] {});
  QAction* typewriter = command(view, QStringLiteral("typewriterMode"), tr("Typewriter Mode"),
                                QStringLiteral("F9"), [] {});
  QAction* fullscreen =
      command(view, QStringLiteral("fullscreen"), tr("Fullscreen"), QStringLiteral("F11"), [] {});
  focus->setCheckable(true);
  typewriter->setCheckable(true);
  fullscreen->setCheckable(true);
  connect(focus, &QAction::toggled, this, [editor](bool enabled) {
    editor->setFocusMode(enabled);
    QSettings().setValue(QStringLiteral("editor/focusMode"), enabled);
  });
  connect(typewriter, &QAction::toggled, this, [editor](bool enabled) {
    editor->setTypewriterMode(enabled);
    QSettings().setValue(QStringLiteral("editor/typewriterMode"), enabled);
  });
  connect(fullscreen, &QAction::toggled, this, [this](bool enabled) {
    if (enabled)
      showFullScreen();
    else
      showNormal();
  });
  focus->setChecked(QSettings().value(QStringLiteral("editor/focusMode"), false).toBool());
  typewriter->setChecked(
      QSettings().value(QStringLiteral("editor/typewriterMode"), false).toBool());
  command(view, QStringLiteral("zoomIn"), tr("Zoom In"), QStringLiteral("Ctrl+Shift+="),
          [editor] { editor->zoomDocument(1); });
  command(view, QStringLiteral("zoomOut"), tr("Zoom Out"), QStringLiteral("Ctrl+Shift+-"),
          [editor] { editor->zoomDocument(-1); });
  command(view, QStringLiteral("zoomReset"), tr("Actual Size"), QStringLiteral("Ctrl+Shift+0"),
          [editor] { editor->resetZoom(); });
  editor->setWritingAreaWidths(platform::writingAreaWidths());
  command(view, QStringLiteral("writingAreaWidth"), tr("Writing Area Width"), QString(),
          [this] { configureWritingArea(); });
  command(view, QStringLiteral("wordCount"), tr("Document Statistics"), QString(),
          [this] { showDocumentStatistics(); });
  auto* jumpMenu = view->addMenu(tr("Jump"));
  jumpMenu->setObjectName(QStringLiteral("jumpMenu"));
  command(jumpMenu, QStringLiteral("jumpSelection"), tr("Jump to Selection"),
          QStringLiteral("Ctrl+J"), [editor] { editor->revealSelection(); });
  command(jumpMenu, QStringLiteral("jumpTop"), tr("Jump to Top"), QStringLiteral("Ctrl+Home"),
          [editor] { editor->moveCaret(QTextCursor::Start); });
  command(jumpMenu, QStringLiteral("jumpBottom"), tr("Jump to Bottom"), QStringLiteral("Ctrl+End"),
          [editor] { editor->moveCaret(QTextCursor::End); });
  command(jumpMenu, QStringLiteral("jumpLineStart"), tr("Jump to Line Start"), QString(),
          [editor] { editor->moveCaret(QTextCursor::StartOfLine); });
  command(jumpMenu, QStringLiteral("jumpLineEnd"), tr("Jump to Line End"), QString(),
          [editor] { editor->moveCaret(QTextCursor::EndOfLine); });
  for (auto* menu : {selectMenu, deleteMenu, jumpMenu}) {
    for (auto* action : menu->actions()) {
      action->setShortcutContext(Qt::WidgetWithChildrenShortcut);
      editor->addAction(action);
    }
  }
  m_outlineAction->setShortcuts(
      {QKeySequence(QStringLiteral("Ctrl+Shift+T")), QKeySequence(QStringLiteral("Ctrl+Shift+1"))});
  command(view, QStringLiteral("fileTree"), tr("File Tree"), QStringLiteral("Ctrl+Shift+3"),
          [this] {
            if (!m_sidebarDock->isVisible()) toggleSidebar();
            m_sidebarTabs->setCurrentIndex(0);
            auto options = m_tree->options();
            options.view = platform::FileBrowserView::Tree;
            m_tree->setOptions(options);
          });
  command(view, QStringLiteral("fileList"), tr("File List"), QString(), [this] {
    if (!m_sidebarDock->isVisible()) toggleSidebar();
    m_sidebarTabs->setCurrentIndex(0);
    auto options = m_tree->options();
    options.view = platform::FileBrowserView::List;
    m_tree->setOptions(options);
  });

  QAction* autoPair = command(settings, QStringLiteral("autoPair"),
                              tr("Auto Pair Brackets and Quotes"), QString(), [] {});
  QAction* syntax = command(settings, QStringLiteral("syntaxHighlight"), tr("Syntax Highlighting"),
                            QString(), [] {});
  syntax->setCheckable(true);
  syntax->setChecked(QSettings().value(QStringLiteral("editor/syntaxHighlight"), true).toBool());
  editor->setSyntaxHighlight(syntax->isChecked());
  connect(syntax, &QAction::toggled, this, [editor](bool enabled) {
    editor->setSyntaxHighlight(enabled);
    QSettings().setValue(QStringLiteral("editor/syntaxHighlight"), enabled);
  });
  auto* codeLineNumbers = command(settings, QStringLiteral("codeLineNumbers"),
                                  tr("Code Block Line Numbers"), QString(), [] {});
  const auto lineNumberSetting = QStringLiteral("editor/codeLineNumbers");
  codeLineNumbers->setCheckable(true);
  codeLineNumbers->setChecked(QSettings().value(lineNumberSetting, false).toBool());
  editor->setCodeLineNumbers(codeLineNumbers->isChecked());
  connect(codeLineNumbers, &QAction::toggled, this, [editor, lineNumberSetting](bool enabled) {
    editor->setCodeLineNumbers(enabled);
    QSettings().setValue(lineNumberSetting, enabled);
  });
  const auto codeIndentSetting = QStringLiteral("editor/codeIndentSize");
  editor->setCodeIndentSize(
      QSettings().value(codeIndentSetting, editor::kDefaultCodeIndentSize).toInt());
  command(settings, QStringLiteral("codeIndentSize"), tr("Code Indent Size"), QString(),
          [this, editor, codeIndentSetting] {
            const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
            const auto restore = qScopeGuard([editor, focusCommit] {
              editor->setParagraphSourceFocusCommitEnabled(focusCommit);
            });
            bool ok = false;
            const int columns = QInputDialog::getInt(
                this, tr("Code Indent Size"), tr("Columns per indentation level"),
                editor->codeIndentSize(), 1, editor::kMaxCodeIndentSize, 1, &ok);
            if (!ok) return;
            editor->setCodeIndentSize(columns);
            QSettings().setValue(codeIndentSetting, columns);
          });
  command(settings, QStringLiteral("defaultCodeLanguage"), tr("Default Code Language"), QString(),
          [this] { configureCodeFences(); });
  const auto autoIndentSetting = QStringLiteral("editor/autoIndentOnBacktab");
  auto* autoIndent = command(settings, QStringLiteral("autoIndentOnBacktab"),
                             tr("Shift+Tab Auto Indents Code"), QString(), [] {});
  autoIndent->setCheckable(true);
  autoIndent->setChecked(QSettings().value(autoIndentSetting, false).toBool());
  editor->setAutoIndentOnBacktab(autoIndent->isChecked());
  connect(autoIndent, &QAction::toggled, this, [editor, autoIndentSetting](bool enabled) {
    editor->setAutoIndentOnBacktab(enabled);
    QSettings().setValue(autoIndentSetting, enabled);
  });
  editor->setDefaultCodeLanguageResolver([](ui::Editor::CodeFenceCreation creation) {
    const auto preferences = platform::codeFencePreferences();
    const bool enabled = creation == ui::Editor::CodeFenceCreation::Typed
                             ? preferences.applyToTypedFences
                             : preferences.applyToCommands;
    if (!enabled) return QString();
    return preferences.useLastLanguage ? platform::lastCodeLanguage() : preferences.language;
  });
  connect(editor, &ui::Editor::codeLanguageChosen, this,
          [](const QString& language) { platform::setLastCodeLanguage(language); });
  autoPair->setCheckable(true);
  autoPair->setChecked(QSettings().value(QStringLiteral("editor/autoPair"), true).toBool());
  editor->setAutoPair(autoPair->isChecked());
  connect(autoPair, &QAction::toggled, this, [editor](bool enabled) {
    editor->setAutoPair(enabled);
    QSettings().setValue(QStringLiteral("editor/autoPair"), enabled);
  });
  QAction* autoSave =
      command(settings, QStringLiteral("autoSave"), tr("Auto Save"), QString(), [] {});
  autoSave->setCheckable(true);
  autoSave->setChecked(m_autoSaveEnabled);
  connect(autoSave, &QAction::toggled, this, [this](bool enabled) {
    m_autoSaveEnabled = enabled;
    QSettings().setValue(QStringLiteral("editor/autoSave"), enabled);
  });
  auto* htmlOutline = settings->addAction(tr("Include Outline in HTML Export"));
  htmlOutline->setObjectName(QStringLiteral("htmlExportOutline"));
  htmlOutline->setCheckable(true);
  htmlOutline->setChecked(QSettings().value(htmlOutlineSetting, false).toBool());
  connect(htmlOutline, &QAction::triggered, this,
          [](bool enabled) { QSettings().setValue(htmlOutlineSetting, enabled); });
  connect(settings, &QMenu::aboutToShow, htmlOutline, [htmlOutline] {
    htmlOutline->setChecked(QSettings().value(htmlOutlineSetting).toBool());
  });
}

void MainWindow::createLaunchMenu(QMenu* settings) {
  using Mode = platform::LaunchMode;
  auto* menu = settings->addMenu(tr("On Launch"));
  menu->setObjectName(QStringLiteral("onLaunchMenu"));
  auto* group = new QActionGroup(menu);
  const auto add = [menu, group](Mode mode, const QString& id, const QString& label) {
    auto* action = menu->addAction(label);
    action->setCheckable(true);
    action->setData(int(mode));
    action->setObjectName(id);
    group->addAction(action);
  };
  add(Mode::NewDocument, QStringLiteral("launchNewDocument"), tr("Open a Blank Document"));
  add(Mode::LastFolder, QStringLiteral("launchLastFolder"), tr("Restore Last Folder"));
  add(Mode::LastFileAndFolder, QStringLiteral("launchLastFileAndFolder"),
      tr("Restore Last File and Folder"));
  add(Mode::CustomFolder, QStringLiteral("launchCustomFolder"), tr("Open a Custom Folder…"));
  const auto sync = [group] {
    const int mode = int(platform::launchOptions().mode);
    for (auto* action : group->actions()) action->setChecked(action->data().toInt() == mode);
  };
  sync();
  connect(menu, &QMenu::aboutToShow, group, sync);
  connect(group, &QActionGroup::triggered, this, [this, sync](QAction* action) {
    auto options = platform::launchOptions();
    const auto mode = Mode(action->data().toInt());
    if (mode == Mode::CustomFolder) {
      const auto initial =
          QFileInfo(options.customFolder).isDir() ? options.customFolder : m_tree->rootPath();
      const auto folder = chooseDirectory(tr("Choose Startup Folder"), initial);
      if (folder.isEmpty()) {
        sync();
        return;
      }
      options.customFolder = folder;
    }
    options.mode = mode;
    platform::setLaunchOptions(options);
  });
}

QString MainWindow::chooseDirectory(const QString& title, const QString& initial) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  return QFileDialog::getExistingDirectory(this, title,
                                           initial.isEmpty() ? QDir::homePath() : initial);
}

void MainWindow::createExportMenu(QMenu* settings) {
  using Mode = platform::ExportFolderMode;
  auto* menu = settings->addMenu(tr("Export Settings"));
  menu->setObjectName(QStringLiteral("exportSettingsMenu"));
  auto* group = new QActionGroup(menu);
  const auto add = [menu, group](Mode mode, const QString& id, const QString& label) {
    auto* action = menu->addAction(label);
    action->setCheckable(true);
    action->setData(int(mode));
    action->setObjectName(id);
    group->addAction(action);
  };
  add(Mode::DocumentFolder, QStringLiteral("exportFolderDocument"), tr("Use Document Folder"));
  add(Mode::LastExportFolder, QStringLiteral("exportFolderLast"), tr("Use Last Export Folder"));
  add(Mode::CustomFolder, QStringLiteral("exportFolderCustom"), tr("Choose Custom Export Folder…"));
  menu->addSeparator();
  auto* reveal = menu->addAction(tr("Open Folder After Export"));
  reveal->setObjectName(QStringLiteral("openFolderAfterExport"));
  reveal->setCheckable(true);
  const auto sync = [group, reveal] {
    const auto preferences = platform::exportPreferences();
    for (auto* action : group->actions())
      action->setChecked(action->data().toInt() == int(preferences.folderMode));
    reveal->setChecked(preferences.openFolderOnSuccess);
  };
  sync();
  connect(menu, &QMenu::aboutToShow, group, sync);
  connect(group, &QActionGroup::triggered, this, [this, sync](QAction* action) {
    auto preferences = platform::exportPreferences();
    const auto mode = Mode(action->data().toInt());
    if (mode == Mode::CustomFolder) {
      const auto initial = QFileInfo(preferences.customFolder).isDir()
                               ? preferences.customFolder
                               : platform::exportDirectory(m_currentPath);
      const auto folder = chooseDirectory(tr("Choose Export Folder"), initial);
      if (folder.isEmpty()) {
        sync();
        return;
      }
      preferences.customFolder = folder;
    }
    preferences.folderMode = mode;
    platform::setExportPreferences(preferences);
  });
  connect(reveal, &QAction::triggered, this, [](bool enabled) {
    auto preferences = platform::exportPreferences();
    preferences.openFolderOnSuccess = enabled;
    platform::setExportPreferences(preferences);
  });
  menu->addSeparator();
  auto* htmlContent = menu->addAction(tr("Additional HTML Content…"));
  htmlContent->setObjectName(QStringLiteral("htmlExportContent"));
  connect(htmlContent, &QAction::triggered, this, &MainWindow::configureHtmlExportContent);
}

void MainWindow::configureHtmlExportContent() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });

  QSettings settings;
  const auto saved = settings.value(htmlContentSetting).toMap();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("htmlExportContentDialog"));
  dialog.setWindowTitle(tr("Additional HTML Content"));
  dialog.resize(580, 410);
  QVBoxLayout layout(&dialog);
  QPlainTextEdit head, body;
  const auto add = [&](QPlainTextEdit& field, const QString& label, const QString& key,
                       const QString& objectName) {
    auto* caption = new QLabel(label, &dialog);
    caption->setTextFormat(Qt::PlainText);
    caption->setBuddy(&field);
    field.setAccessibleName(label);
    field.setObjectName(objectName);
    field.setLineWrapMode(QPlainTextEdit::NoWrap);
    field.setTabChangesFocus(true);
    field.setPlainText(saved.value(key).toString());
    layout.addWidget(caption);
    layout.addWidget(&field, 1);
  };
  add(head, tr("Append to <head>"), QStringLiteral("head"), QStringLiteral("htmlExtraHead"));
  add(body, tr("Append to <body>"), QStringLiteral("body"), QStringLiteral("htmlExtraBody"));

  QDialogButtonBox buttons(QDialogButtonBox::Save | QDialogButtonBox::Cancel);
  buttons.button(QDialogButtonBox::Save)->setText(tr("Save"));
  buttons.button(QDialogButtonBox::Cancel)->setText(tr("Cancel"));
  layout.addWidget(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;
  settings.setValue(htmlContentSetting, QVariantMap{{QStringLiteral("head"), head.toPlainText()},
                                                    {QStringLiteral("body"), body.toPlainText()}});
}

bool MainWindow::maybeSaveBeforeNavigate() {
  if (!m_dirty || m_benchMode) return true;
  if (!m_currentPath.isEmpty() && m_autoSaveEnabled) {
    m_autoSave->flushNow();
    autoSaveNow();
    if (!m_dirty) return true;
  }
  const auto answer = QMessageBox::warning(
      this, tr("Unsaved Document"), tr("Save your changes before continuing?"),
      QMessageBox::Save | QMessageBox::Discard | QMessageBox::Cancel, QMessageBox::Cancel);
  if (answer == QMessageBox::Cancel) return false;
  if (answer == QMessageBox::Save) {
    save();
    return !m_dirty;
  }
  m_autoSave->cancelPending();
  ui::removeDraft(m_currentPath);
  m_dirty = false;
  return true;
}

void MainWindow::newDocument() {
  if (!maybeSaveBeforeNavigate()) return;
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit(false);
  if (!m_benchMode) platform::updateClosedFiles(m_currentPath);
  m_currentPath.clear();
  resetPreviousExport();
  m_view->rendered()->setBaseUrl(QUrl());
  m_view->loadMarkdownIntoBoth(QString(), nullptr);
  m_document = m_view->active();
  m_sourceMode = false;
  m_view->showActiveIn(m_editor);
  m_sourceAction->setText(tr("Source View"));
  m_savedMarkdown.clear();
  markClean();
  m_outlinePanel->setDocument(m_document, true);
  if (m_findBar->isVisible()) refreshMatches();
  m_editor->setFocus();
}

void MainWindow::openQuickly() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = m_searchFocusHeld ? m_searchFocusPrevious
                                             : editor->setParagraphSourceFocusCommitEnabled(false);
  editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restoreFocus = qScopeGuard([this, editor, focusCommit] {
    editor->setParagraphSourceFocusCommitEnabled(m_searchFocusHeld ? false : focusCommit);
  });
  QString root = m_tree->rootPath();
  if (root.isEmpty())
    root = m_currentPath.isEmpty() ? QDir::currentPath() : QFileInfo(m_currentPath).absolutePath();
  ui::QuickOpenDialog dialog(root, platform::recentFiles(), this,
                             m_folderLocationsMenu->locations());
  if (dialog.exec() == QDialog::Accepted) {
    if (dialog.selectedIsFolder())
      openFolderPath(dialog.selectedPath());
    else
      openFile(dialog.selectedPath());
  }
}

void MainWindow::insertTableDialog() {
  QDialog dialog(this);
  dialog.setWindowTitle(tr("Insert Table"));
  QFormLayout layout(&dialog);
  QSpinBox rows, columns;
  rows.setRange(2, 100);
  rows.setValue(3);
  columns.setRange(1, 20);
  columns.setValue(3);
  layout.addRow(tr("Rows (including header)"), &rows);
  layout.addRow(tr("Columns"), &columns);
  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted)
    static_cast<ui::Editor*>(m_editor)->createTable(rows.value(), columns.value());
}

void MainWindow::configureCodeFences() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  const auto preferences = platform::codeFencePreferences();
  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("codeFenceSettings"));
  dialog.setWindowTitle(tr("Default Code Language"));
  QFormLayout layout(&dialog);
  QComboBox language;
  language.setObjectName(QStringLiteral("codeFenceLanguage"));
  language.setEditable(true);
  language.setInsertPolicy(QComboBox::NoInsert);
  language.addItem(QString());
  language.addItems(editor::codeLanguages());
  language.lineEdit()->setMaxLength(64);
  language.lineEdit()->setValidator(
      new QRegularExpressionValidator(editor::codeLanguagePattern(), &language));
  if (editor::codeLanguagePattern().match(preferences.language).hasMatch())
    language.setEditText(preferences.language);
  layout.addRow(tr("Language identifier (empty for plain text)"), &language);

  QCheckBox last(tr("Use last selected language"));
  last.setObjectName(QStringLiteral("codeFenceUseLast"));
  last.setChecked(preferences.useLastLanguage);
  language.setDisabled(last.isChecked());
  connect(&last, &QCheckBox::toggled, &language, &QComboBox::setDisabled);
  layout.addRow(&last);
  QCheckBox commands(tr("Apply to menu and shortcut"));
  commands.setObjectName(QStringLiteral("codeFenceApplyCommands"));
  commands.setChecked(preferences.applyToCommands);
  layout.addRow(&commands);
  QCheckBox typed(tr("Apply to typed fences"));
  typed.setObjectName(QStringLiteral("codeFenceApplyTyped"));
  typed.setChecked(preferences.applyToTypedFences);
  layout.addRow(&typed);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() == QDialog::Accepted && language.lineEdit()->hasAcceptableInput())
    platform::setCodeFencePreferences(
        {language.currentText(), last.isChecked(), commands.isChecked(), typed.isChecked()});
}

void MainWindow::configureWritingArea() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  const auto widths = platform::writingAreaWidths();
  using Width = platform::WritingWidth;

  QDialog dialog(this);
  dialog.setObjectName(QStringLiteral("writingAreaWidthDialog"));
  dialog.setWindowTitle(tr("Writing Area Width"));
  QFormLayout layout(&dialog);
  QComboBox rendered, source;
  rendered.setObjectName(QStringLiteral("renderedWritingWidth"));
  source.setObjectName(QStringLiteral("sourceWritingWidth"));
  const auto populate = [this](QComboBox& combo, Width value) {
    combo.addItem(tr("Compact (%1 px)").arg(int(Width::Compact)), int(Width::Compact));
    combo.addItem(tr("Standard (%1 px)").arg(int(Width::Standard)), int(Width::Standard));
    combo.addItem(tr("Wide (%1 px)").arg(int(Width::Wide)), int(Width::Wide));
    combo.addItem(tr("Fill Window"), int(Width::Fill));
    combo.setCurrentIndex(combo.findData(int(value)));
  };
  populate(rendered, widths.rendered);
  populate(source, widths.source);
  layout.addRow(tr("Rendered View"), &rendered);
  layout.addRow(tr("Source View"), &source);

  QDialogButtonBox buttons(QDialogButtonBox::Ok | QDialogButtonBox::Cancel);
  layout.addRow(&buttons);
  connect(&buttons, &QDialogButtonBox::accepted, &dialog, &QDialog::accept);
  connect(&buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
  if (dialog.exec() != QDialog::Accepted) return;

  const platform::WritingAreaWidths chosen{Width(rendered.currentData().toInt()),
                                           Width(source.currentData().toInt())};
  platform::setWritingAreaWidths(chosen);
  for (auto* widget : QApplication::topLevelWidgets())
    if (auto* window = qobject_cast<MainWindow*>(widget))
      static_cast<ui::Editor*>(window->m_editor)->setWritingAreaWidths(chosen);
}

void MainWindow::insertLinkDialog() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  editor->finishParagraphSourceEdit();
  auto cursor = m_sourceMode ? QTextCursor() : editor::linkSelection(editor->textCursor());
  const bool existing = !cursor.isNull();
  if (!existing) cursor = editor->textCursor();
  if (cursor.selectedText().contains(QChar::ParagraphSeparator)) {
    statusBar()->showMessage(tr("Select text in one paragraph"), 3000);
    return;
  }
  ui::LinkDialog dialog(
      cursor.selectedText(), existing ? cursor.charFormat().anchorHref() : QString(),
      m_sourceMode ? QList<ui::HeadingLink>() : ui::documentHeadingLinks(m_document),
      m_view->rendered()->baseUrl(), this);
  if (dialog.exec() != QDialog::Accepted) return;
  if (m_sourceMode) {
    cursor.beginEditBlock();
    cursor.insertText(QStringLiteral("[%1](%2)").arg(dialog.label(), dialog.destination()));
    cursor.endEditBlock();
    editor->setTextCursor(cursor);
  } else if (editor::replaceLink(cursor, dialog.destination(), dialog.label())) {
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    editor->setCurrentCharFormat(QTextCharFormat());
  }
}

void MainWindow::followLink(const QString& target) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const auto destination = ui::resolveLink(target, m_view->rendered()->baseUrl());
  if (destination.kind == ui::LinkDestination::Invalid) {
    statusBar()->showMessage(tr("Cannot open link: %1").arg(target), 4000);
    return;
  }
  if (destination.kind == ui::LinkDestination::Heading) {
    if (!editor->jumpToHeading(destination.fragment))
      statusBar()->showMessage(tr("Heading not found: %1").arg(destination.fragment), 4000);
    return;
  }
  if (destination.kind == ui::LinkDestination::Markdown) {
    if (QFileInfo(destination.path) != QFileInfo(m_currentPath)) openFile(destination.path);
    if (QFileInfo(destination.path) == QFileInfo(m_currentPath) &&
        !destination.fragment.isEmpty() && !editor->jumpToHeading(destination.fragment))
      statusBar()->showMessage(tr("Heading not found: %1").arg(destination.fragment), 4000);
    return;
  }
  if (!QDesktopServices::openUrl(destination.url))
    statusBar()->showMessage(tr("Cannot open link: %1").arg(target), 4000);
}

void MainWindow::replaceOne() {
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
  QTextCursor cursor = m_editor->textCursor();
  m_search.setCaseSensitive(m_findBar->caseSensitive());
  m_search.setRegex(m_findBar->regex());
  if (m_search.replaceNext(cursor, m_findBar->pattern(), m_findBar->replacement()))
    m_editor->setTextCursor(cursor);
  refreshMatches();
}

void MainWindow::replaceAll() {
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
  m_search.setCaseSensitive(m_findBar->caseSensitive());
  m_search.setRegex(m_findBar->regex());
  const int count = m_search.replaceAll(m_document, m_findBar->pattern(), m_findBar->replacement());
  refreshMatches();
  statusBar()->showMessage(tr("Replaced %1 matches").arg(count), 3000);
}

void MainWindow::showDocumentStatistics() {
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
  const QString text = m_editor->textCursor().hasSelection() ? m_editor->textCursor().selectedText()
                                                             : m_document->toPlainText();
  int words = 0, visible = 0;
  bool inWord = false;
  const auto characters = text.toUcs4();
  for (const auto ch : characters) {
    if (!QChar::isSpace(ch)) ++visible;
    const bool cjk = (ch >= 0x3400 && ch <= 0x9fff) || (ch >= 0x20000 && ch <= 0x3134f) ||
                     (ch >= 0x3040 && ch <= 0x30ff) || (ch >= 0xac00 && ch <= 0xd7af);
    if (cjk) {
      ++words;
      inWord = false;
    } else if (QChar::isLetterOrNumber(ch)) {
      if (!inWord) ++words;
      inWord = true;
    } else if (ch != '\'' && ch != '-')
      inWord = false;
  }
  const QString message = tr("Words: %1").arg(words) + QLatin1Char('\n') +
                          tr("Characters: %1").arg(characters.size()) + QLatin1Char('\n') +
                          tr("Characters without spaces: %1").arg(visible) + QLatin1Char('\n') +
                          tr("Reading time: %1 min").arg((words + 199) / 200);
  QMessageBox::information(this, tr("Document Statistics"), message);
}

void MainWindow::openDialog() {
  const QString path = QFileDialog::getOpenFileName(this, tr("Open Markdown"), QString(),
                                                    QStringLiteral("Markdown (*.md)"));
  if (!path.isEmpty()) openFile(path);
}

bool MainWindow::openStartupTarget(const platform::StartupTarget& target) {
  const bool opened = target.file.isEmpty() ||
                      (target.importHtml ? importHtmlFile(target.file) : openFile(target.file));
  // Open the document before restoring a workspace that may be elsewhere.
  return (target.folder.isEmpty() || openFolderPath(target.folder)) && opened;
}

bool MainWindow::openFile(const QString& path) { return openFileReplacingCurrent(path, true); }

bool MainWindow::openFileReplacingCurrent(const QString& path, bool rememberCurrent) {
  if (!QFileInfo(path).isFile()) {
    statusBar()->showMessage(tr("Cannot open file"));
    return false;
  }
  if (!maybeSaveBeforeNavigate()) return false;
  QFile file(path);
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(tr("Cannot open file"));
    return false;
  }

  const QString source = QString::fromUtf8(file.readAll());
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit(false);
  m_view->rendered()->setBaseUrl(
      QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QLatin1Char('/')));
  QString error;
  // A new file is a new baseline for both views, so both undo stacks start empty.
  if (!m_view->loadMarkdownIntoBoth(source, &error)) {
    statusBar()->showMessage(error);
    return false;
  }

  m_document = m_view->active();
  m_view->showActiveIn(m_editor);
  m_editor->moveCursor(QTextCursor::Start);
  m_sourceMode = m_view->isSourceMode();
  if (!m_benchMode) platform::updateClosedFiles(rememberCurrent ? m_currentPath : QString(), path);
  if (QFileInfo(m_currentPath) != QFileInfo(path)) resetPreviousExport();
  m_currentPath = path;
  m_savedMarkdown = source;
  m_sourceAction->setText(tr("Source View"));
  markClean();
  if (m_findBar->isVisible()) refreshMatches();
  if (!m_benchMode) platform::addRecentFile(path);
  rebuildRecentMenu();

  if (m_sidebarDock->isVisible() || !m_tree->rootPath().isEmpty()) {
    const QString relative =
        QDir(m_tree->rootPath()).relativeFilePath(QFileInfo(path).absoluteFilePath());
    if (m_tree->rootPath().isEmpty() || relative == QLatin1String("..") ||
        relative.startsWith(QLatin1String("../")) || QDir::isAbsolutePath(relative))
      m_tree->setRoot(QFileInfo(path).absolutePath());
    m_tree->selectPath(path);
  }
  m_outlinePanel->setDocument(m_document, true);
  m_sidebarRootLabel->setText(!m_tree->rootPath().isEmpty() ? QDir(m_tree->rootPath()).dirName()
                                                            : QFileInfo(path).dir().dirName());
  if (m_sidebarDock->isVisible()) {
    m_tree->setWatchEnabled(true);
  }
  m_workspaceSearch->setRoot(m_tree->rootPath().isEmpty() ? QFileInfo(path).absolutePath()
                                                          : m_tree->rootPath());
  const auto folder =
      m_tree->rootPath().isEmpty() ? QFileInfo(path).absolutePath() : m_tree->rootPath();
  m_folderLocationsMenu->setRoot(folder);
  if (!m_benchMode) m_folderLocationsMenu->recordFolder(folder);
  return true;
}

bool MainWindow::adoptHtmlImport(const editor::HtmlImportResult& result) {
  if (!result.ok()) {
    statusBar()->showMessage(result.error);
    return false;
  }
  if (!maybeSaveBeforeNavigate()) return false;
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit(false);
  QString error;
  if (!m_view->loadMarkdownIntoBoth(result.markdown, &error)) {
    statusBar()->showMessage(error);
    return false;
  }
  m_autoSave->cancelPending();
  if (!m_benchMode) platform::updateClosedFiles(m_currentPath);
  m_currentPath.clear();
  resetPreviousExport();
  m_view->rendered()->setBaseUrl(QUrl());
  m_document = m_view->active();
  m_sourceMode = false;
  m_view->showActiveIn(m_editor);
  m_savedMarkdown.clear();
  m_dirty = true;
  if (!m_benchMode) m_autoSave->notifyChanged();
  m_editor->moveCursor(QTextCursor::Start);
  updateTitleAndStatus();
  m_outlinePanel->setDocument(m_document, true);
  if (m_findBar->isVisible()) refreshMatches();
  m_editor->setFocus();
  statusBar()->showMessage(tr("Imported HTML into an unsaved Markdown document"));
  return true;
}
bool MainWindow::importHtmlFile(const QString& path) {
  return adoptHtmlImport(editor::importHtmlFile(path));
}
void MainWindow::importHtmlDialog() {
  const auto path = QFileDialog::getOpenFileName(this, tr("Import HTML"), {},
                                                 QStringLiteral("HTML (*.html *.htm)"));
  if (path.isEmpty()) return;
  const auto result = editor::importHtmlFile(path);
  if (!result.ok()) {
    statusBar()->showMessage(result.error);
    return;
  }
  ui::HtmlImportDialog preview(result, this);
  if (preview.exec() == QDialog::Accepted) adoptHtmlImport(result);
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
  if (!m_benchMode) platform::addRecentFile(m_currentPath);
  rebuildRecentMenu();
}

void MainWindow::saveAs() {
  const QString path = QFileDialog::getSaveFileName(this, tr("Save Markdown"), m_currentPath,
                                                    QStringLiteral("Markdown (*.md)"));
  if (path.isEmpty()) return;
  saveDocumentAs(path);
}

bool MainWindow::saveDocumentAs(const QString& path) {
  if (path.isEmpty()) return false;
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
  const auto relocated = platform::relocateImages(currentMarkdown(), m_currentPath, path);
  if (!relocated.error.isEmpty()) {
    statusBar()->showMessage(relocated.error);
    return false;
  }
  const QString markdown = relocated.markdown;
  QString error;
  if (!platform::writeAtomic(path, markdown, &error)) {
    statusBar()->showMessage(error);
    return false;
  }
  if (!relocated.targets.isEmpty()) {
    const auto previous = m_editor->textCursor();
    QTextCursor edit(m_editor->document());
    edit.beginEditBlock();
    if (m_sourceMode) {
      edit.select(QTextCursor::Document);
      edit.insertText(markdown);
    } else {
      const QUrl base = QUrl::fromLocalFile(QFileInfo(path).absolutePath() + '/');
      struct ImageEdit {
        int position;
        QTextCharFormat format;
      };
      QList<ImageEdit> images;
      for (auto block = m_editor->document()->begin(); block.isValid(); block = block.next())
        for (auto it = block.begin(); !it.atEnd(); ++it) {
          const auto fragment = it.fragment();
          auto format = fragment.charFormat();
          const auto target = format.property(editor::kImageProperty).toString();
          if (!relocated.targets.contains(target)) continue;
          const QString replacement = relocated.targets.value(target);
          format.setProperty(editor::kImageProperty, replacement);
          if (format.isImageFormat()) {
            const QString local = render::localImagePath(replacement, base);
            const QUrl url = QUrl::fromLocalFile(local);
            m_editor->document()->addResource(QTextDocument::ImageResource, url,
                                              render::imagePreview(local));
            auto image = format.toImageFormat();
            image.setName(url.toString());
            format = image;
          }
          images.append({fragment.position(), format});
        }
      for (const auto& image : images) {
        edit.setPosition(image.position);
        edit.movePosition(QTextCursor::NextCharacter, QTextCursor::KeepAnchor);
        edit.setCharFormat(image.format);
      }
    }
    edit.endEditBlock();
    if (!m_sourceMode)
      m_editor->setTextCursor(previous);
    else {
      auto cursor = m_editor->textCursor();
      cursor.setPosition(qMin(previous.position(), markdown.size()));
      m_editor->setTextCursor(cursor);
    }
  }
  ui::removeDraft(m_currentPath);
  if (!m_benchMode) platform::updateClosedFiles({}, path);
  if (!m_currentPath.isEmpty() && QFileInfo(m_currentPath) != QFileInfo(path))
    resetPreviousExport();
  m_currentPath = path;
  m_savedMarkdown = markdown;
  m_view->rendered()->setBaseUrl(
      QUrl::fromLocalFile(QFileInfo(path).absolutePath() + QLatin1Char('/')));
  markClean();
  if (!m_benchMode) platform::addRecentFile(path);
  rebuildRecentMenu();
  return true;
}

void MainWindow::exportHtml() { exportHtmlDocument(true); }

QString MainWindow::suggestedExportPath(const QString& suffix) const {
  const auto name =
      m_currentPath.isEmpty() ? QStringLiteral("untitled") : QFileInfo(m_currentPath).baseName();
  return QDir(platform::exportDirectory(m_currentPath)).filePath(name + suffix);
}

QString MainWindow::chooseExportFile(const QString& title, const QString& suggested,
                                     const QString& filter, const QString& suffix,
                                     const QString& objectName) {
  const QFileInfo info(suggested);
  QFileDialog dialog(this, title, info.absolutePath(), filter);
  dialog.setObjectName(objectName);
  dialog.setAcceptMode(QFileDialog::AcceptSave);
  dialog.setDefaultSuffix(suffix);
  dialog.selectFile(info.fileName());
  if (dialog.exec() != QDialog::Accepted) return {};
  return dialog.selectedFiles().value(0);
}

QString MainWindow::completeExportPath(const QString& path, const ExportReplacement* replacement) {
  if (!replacement) return path;
  switch (platform::replaceExport(replacement->destination, path)) {
    case platform::ExportReplacementResult::Success:
      return replacement->destination.path;
    case platform::ExportReplacementResult::Changed:
      statusBar()->showMessage(tr("The previous export has changed or is unavailable."), 5000);
      break;
    case platform::ExportReplacementResult::WriteFailed:
      statusBar()->showMessage(tr("Could not replace the previous export."), 5000);
      break;
  }
  return {};
}

void MainWindow::finishExport(const QString& path, const QString& status, ExportReplay repeat) {
  m_previousExport = std::move(repeat);
  m_previousExportFile = platform::ExportedFile::capture(path);
  m_repeatExportAction->setEnabled(true);
  m_overwriteExportAction->setEnabled(!m_previousExportFile.digest.isEmpty());
  m_overwriteExportAction->setStatusTip(m_previousExportFile.path);
  platform::rememberExportDirectory(path);
  statusBar()->showMessage(status);
  if (!platform::exportPreferences().openFolderOnSuccess) return;
  const auto folder = platform::containingDirectoryUrl(path);
  if (folder.isEmpty() || !QDesktopServices::openUrl(folder))
    statusBar()->showMessage(tr("Export saved, but the folder could not be opened"), 5000);
}

void MainWindow::resetPreviousExport() {
  m_previousExport = {};
  m_previousExportFile = {};
  m_repeatExportAction->setEnabled(false);
  m_overwriteExportAction->setEnabled(false);
  m_overwriteExportAction->setStatusTip({});
}

void MainWindow::repeatExport() { runPreviousExport(nullptr); }

void MainWindow::overwritePreviousExport() {
  if (!m_previousExport) return;
  const auto previous = m_previousExportFile;
  if (!previous.unchanged()) {
    statusBar()->showMessage(tr("The previous export has changed or is unavailable."), 5000);
    return;
  }
  if (!m_currentPath.isEmpty() && QFileInfo(previous.path) == QFileInfo(m_currentPath)) {
    statusBar()->showMessage(tr("The previous export is the current Markdown document."), 5000);
    return;
  }
  const QFileInfo output(previous.path);
  QTemporaryDir staging(output.dir().filePath(QStringLiteral(".typist-export-XXXXXX")));
  if (!staging.isValid()) {
    statusBar()->showMessage(tr("Could not replace the previous export."), 5000);
    return;
  }
  const ExportReplacement replacement{previous, staging.filePath(output.fileName())};
  runPreviousExport(&replacement);
}

void MainWindow::runPreviousExport(const ExportReplacement* replacement) {
  // A successful export replaces the stored callback while this copy runs.
  const auto repeat = m_previousExport;
  if (!repeat) return;
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  repeat(replacement);
}

void MainWindow::exportHtmlDocument(bool styled) {
  const auto theme = ui::resolveTheme(themePreference(), systemPrefersDark());
  QSettings settings;
  const auto extra = settings.value(htmlContentSetting).toMap();
  exportHtmlDocument({styled, theme == ui::Theme::Dark,
                      settings.value(htmlOutlineSetting, false).toBool(),
                      m_outlinePanel->collapsible(), extra.value(QStringLiteral("head")).toString(),
                      extra.value(QStringLiteral("body")).toString()});
}

void MainWindow::exportHtmlDocument(const HtmlExportSettings& options,
                                    const ExportReplacement* replacement) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  const QString suffix = options.styled ? QStringLiteral(".html") : QStringLiteral("-plain.html");
  const QString path =
      replacement ? replacement->stagedPath
                  : chooseExportFile(
                        options.styled ? tr("Export HTML") : tr("Export HTML (without styles)"),
                        suggestedExportPath(suffix), QStringLiteral("HTML (*.html)"),
                        QStringLiteral("html"));
  if (path.isEmpty()) return;

  QList<render::HtmlHeading> headings;
  const render::HtmlRenderOptions renderOptions{options.dark, options.styled};
  QString body = render::embedLocalImages(
      render::html(currentMarkdown(), renderOptions, options.includeOutline ? &headings : nullptr),
      m_view->rendered()->baseUrl());
  if (!headings.isEmpty()) {
    body = QStringLiteral("<div class=\"export-layout\">\n") +
           render::htmlOutline(headings, tr("Outline"), options.collapsible) +
           QStringLiteral("<main class=\"export-content\">\n") + body +
           QStringLiteral("</main>\n</div>\n");
  }
  const auto theme = options.dark ? ui::Theme::Dark : ui::Theme::Light;
  const auto css =
      options.styled ? ui::htmlStyle(theme, !headings.isEmpty()).toStdString() : std::string();
  const std::string page =
      core::wrapHtmlDocument(body.toStdString(), QFileInfo(path).baseName().toStdString(), css,
                             options.extraHead.toStdString(), options.extraBody.toStdString());
  QString error;
  if (!typist::platform::writeAtomic(path, QString::fromStdString(page), &error)) {
    statusBar()->showMessage(error);
    return;
  }
  const auto savedPath = completeExportPath(path, replacement);
  if (savedPath.isEmpty()) return;
  finishExport(savedPath, tr("Exported HTML"), [this, options](const ExportReplacement* target) {
    exportHtmlDocument(options, target);
  });
}

void MainWindow::exportImage() { exportImageDocument(); }

void MainWindow::exportImageDocument(const platform::ImageExportOptions* previous,
                                     const ExportReplacement* replacement) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restore = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  QSettings settings;
  const auto key = QStringLiteral("export/image");
  using Theme = platform::ImageExportOptions::Theme;
  platform::ImageExportOptions options;
  if (previous) {
    options = *previous;
  } else {
    const auto saved = settings.value(key).toMap();
    ui::ImageExportDialog setup(
        {saved.value(QStringLiteral("width"), options.width).toInt(),
         saved.value(QStringLiteral("fontPixels"), options.fontPixels).toInt(),
         saved.value(QStringLiteral("scale"), options.scale).toInt(),
         Theme(saved.value(QStringLiteral("theme"), int(options.theme)).toInt())},
        this);
    if (setup.exec() != QDialog::Accepted) return;
    options = setup.options();
  }

  const QFileInfo file(m_currentPath);
  const auto directory = platform::exportDirectory(m_currentPath);
  const auto name = m_currentPath.isEmpty() ? QStringLiteral("untitled") : file.completeBaseName();
  const auto path = replacement
                        ? replacement->stagedPath
                        : chooseExportFile(tr("Export Image"),
                                           QDir(directory).filePath(name + QStringLiteral(".png")),
                                           tr("PNG image (*.png)"), QStringLiteral("png"),
                                           QStringLiteral("imageDestination"));
  if (path.isEmpty()) return;
  if (QFileInfo(path).suffix().compare(QStringLiteral("png"), Qt::CaseInsensitive) != 0) {
    statusBar()->showMessage(tr("Choose a filename ending in .png."));
    return;
  }

  const bool dark = options.theme == Theme::Dark ||
                    (options.theme == Theme::Current &&
                     ui::resolveTheme(themePreference(), systemPrefersDark()) == ui::Theme::Dark);
  QTextDocument document;
  document.setBaseUrl(m_view->rendered()->baseUrl());
  document.setProperty("typistDark", dark);
  QString error;
  if (!typist::editor::loadMarkdown(currentMarkdown(), &document, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  switch (platform::exportImage(path, document, options, dark)) {
    case platform::ImageExportError::InvalidOptions:
      statusBar()->showMessage(tr("Invalid image export settings."));
      return;
    case platform::ImageExportError::ContentTooWide:
      statusBar()->showMessage(
          tr("Content is too wide. Increase the image width or reduce the font size."));
      return;
    case platform::ImageExportError::ImageTooLarge:
      statusBar()->showMessage(
          tr("The image is too large. Lower the resolution scale or export a shorter document."));
      return;
    case platform::ImageExportError::Render:
      statusBar()->showMessage(tr("Could not render the image."));
      return;
    case platform::ImageExportError::Write:
      statusBar()->showMessage(tr("Could not save the PNG image."));
      return;
    case platform::ImageExportError::None:
      break;
  }
  const auto savedPath = completeExportPath(path, replacement);
  if (savedPath.isEmpty()) return;
  settings.setValue(key, QVariantMap{{QStringLiteral("width"), options.width},
                                     {QStringLiteral("fontPixels"), options.fontPixels},
                                     {QStringLiteral("scale"), options.scale},
                                     {QStringLiteral("theme"), int(options.theme)}});
  finishExport(
      savedPath, tr("Exported PNG image"),
      [this, options](const ExportReplacement* target) { exportImageDocument(&options, target); });
}

void MainWindow::exportPdf() { exportPdfDocument(); }

void MainWindow::exportPdfDocument(const platform::PdfPageOptions* previous,
                                   const ExportReplacement* replacement) {
  const QString suggested = suggestedExportPath(QStringLiteral(".pdf"));
  // PDF always reflects Markdown semantics, so it prints a detached rendered document
  // instead of whatever the editor view is currently showing.
  QTextDocument rendered;
  rendered.setBaseUrl(m_view->rendered()->baseUrl());
  QString error;
  if (!typist::editor::loadMarkdown(currentMarkdown(), &rendered, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  const auto title =
      m_currentPath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentPath).completeBaseName();
  auto options = previous ? *previous : platform::pdfOptions(title);
  options.title = title;
  if (!previous) {
    ui::PdfExportDialog setup(&rendered, options, this);
    if (setup.exec() != QDialog::Accepted) return;
    options = setup.options();
  }
  const auto path =
      replacement ? replacement->stagedPath
                  : chooseExportFile(tr("Export PDF"), suggested, QStringLiteral("PDF (*.pdf)"),
                                     QStringLiteral("pdf"), QStringLiteral("pdfDestination"));
  if (path.isEmpty()) return;

  if (!typist::platform::exportPdf(path, &rendered, options, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  const auto savedPath = completeExportPath(path, replacement);
  if (savedPath.isEmpty()) return;
  platform::setPdfOptions(options);
  finishExport(savedPath, tr("Exported PDF"), [this, options](const ExportReplacement* target) {
    exportPdfDocument(&options, target);
  });
}

void MainWindow::exportDocx() { exportDocxDocument(nullptr); }

void MainWindow::exportDocxDocument(const ExportReplacement* replacement) {
  const QString path =
      replacement ? replacement->stagedPath
                  : QFileDialog::getSaveFileName(this, tr("Export DOCX"),
                                                 suggestedExportPath(QStringLiteral(".docx")),
                                                 QStringLiteral("Word document (*.docx)"));
  if (path.isEmpty()) return;

  // DOCX reflects Markdown semantics directly, so it renders from the source text.
  QString error;
  if (!typist::platform::exportDocx(path, currentMarkdown(), &error,
                                    m_currentPath.isEmpty()
                                        ? QDir::currentPath()
                                        : QFileInfo(m_currentPath).absolutePath())) {
    statusBar()->showMessage(error);
    return;
  }
  const auto savedPath = completeExportPath(path, replacement);
  if (savedPath.isEmpty()) return;
  finishExport(savedPath, tr("Exported DOCX"),
               [this](const ExportReplacement* target) { exportDocxDocument(target); });
}

void MainWindow::exportEpub() { exportEpubDocument(); }

void MainWindow::exportEpubDocument(const platform::EpubOptions* previous,
                                    const ExportReplacement* replacement) {
  QSettings settings;
  const auto title =
      m_currentPath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentPath).completeBaseName();
  platform::EpubOptions options;
  if (previous) {
    options = *previous;
  } else {
    ui::EpubDialog metadata(
        title, settings.value(QStringLiteral("export/epubAuthor")).toString(),
        settings.value(QStringLiteral("export/epubLanguage"), QLocale().bcp47Name()).toString(),
        this);
    metadata.setSplitChapters(
        settings.value(QStringLiteral("export/epubSplitChapters"), false).toBool());
    if (metadata.exec() != QDialog::Accepted) return;
    options = {metadata.bookTitle(), metadata.author(), metadata.language(), metadata.coverImage(),
               metadata.splitChapters()};
  }
  const auto directory = platform::exportDirectory(m_currentPath);
  const auto name = m_currentPath.isEmpty() ? QStringLiteral("untitled")
                                            : QFileInfo(m_currentPath).completeBaseName();
  const auto path =
      replacement ? replacement->stagedPath
                  : chooseExportFile(tr("Export EPUB"),
                                     QDir(directory).filePath(name + QStringLiteral(".epub")),
                                     QStringLiteral("EPUB book (*.epub)"), QStringLiteral("epub"),
                                     QStringLiteral("epubDestination"));
  if (path.isEmpty()) return;
  QString error;
  QStringList notices;
  if (!platform::exportEpub(
          path, currentMarkdown(), options, &error,
          m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).absolutePath(),
          &notices)) {
    QMessageBox::warning(this, tr("Export EPUB"), error);
    return;
  }
  const auto savedPath = completeExportPath(path, replacement);
  if (savedPath.isEmpty()) return;
  settings.setValue(QStringLiteral("export/epubAuthor"), options.author);
  settings.setValue(QStringLiteral("export/epubLanguage"), options.language);
  settings.setValue(QStringLiteral("export/epubSplitChapters"), options.splitChapters);
  if (!notices.isEmpty()) {
    QMessageBox message(QMessageBox::Information, tr("Export EPUB"),
                        tr("EPUB exported. Some resources or links were kept as text."),
                        QMessageBox::Ok, this);
    message.setTextFormat(Qt::PlainText);
    message.setDetailedText(notices.join('\n'));
    message.exec();
  }
  finishExport(savedPath, tr("Exported EPUB"), [this, options](const ExportReplacement* target) {
    exportEpubDocument(&options, target);
  });
}

void MainWindow::autoSaveNow() {
  if (!m_autoSaveEnabled || !m_dirty || m_currentPath.isEmpty()) return;

  const QString markdown = currentMarkdown();
  QString error;
  if (!typist::platform::writeAtomic(m_currentPath, markdown, &error)) {
    statusBar()->showMessage(error);
    return;
  }

  m_savedMarkdown = markdown;
  markClean();
  statusBar()->showMessage(tr("Autosaved"), 1600);
}

void MainWindow::undo() {
  if (static_cast<ui::Editor*>(m_editor)->undoParagraphSource()) return;
  m_document->undo();
  m_dirty = currentMarkdown() != m_savedMarkdown;
  if (m_findBar->isVisible()) refreshMatches();
  updateTitleAndStatus();
}

void MainWindow::redo() {
  if (static_cast<ui::Editor*>(m_editor)->undoParagraphSource(true)) return;
  m_document->redo();
  m_dirty = currentMarkdown() != m_savedMarkdown;
  updateTitleAndStatus();
}

void MainWindow::toggleSourceView() {
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
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
  m_outlinePanel->setDocument(m_sourceMode ? nullptr : m_document);
  if (m_findBar->isVisible()) refreshMatches();
  updateTitleAndStatus();
}

void MainWindow::toggleFindBar() {
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit();
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
  m_search.setRegex(m_findBar->regex());
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
  static_cast<ui::Editor*>(m_editor)->setSearchHighlights(selections);
}

int MainWindow::highlightCountForBench(const QString& pattern) const {
  typist::ui::SearchController search = m_search;
  return search.allMatches(m_document, pattern).size();
}

qint64 MainWindow::exportDocxForBench(const QString& path) const {
  return typist::platform::docxSizeForBench(path, currentMarkdown());
}
qint64 MainWindow::exportPdfForBench(const QString& path,
                                     const platform::PdfPageOptions& options) const {
  QTextDocument document;
  document.setBaseUrl(m_view->rendered()->baseUrl());
  QString error;
  if (!editor::loadMarkdown(currentMarkdown(), &document, &error) ||
      !platform::exportPdf(path, &document, options, &error))
    return -1;
  return QFileInfo(path).size();
}
qint64 MainWindow::exportEpubForBench(const QString& path,
                                      const platform::EpubOptions& options) const {
  QString error;
  if (!platform::exportEpub(
          path, currentMarkdown(), options, &error,
          m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).absolutePath()))
    return -1;
  return QFileInfo(path).size();
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
  m_outlinePanel->setDocument(m_document, true);
  m_currentPath.clear();
  resetPreviousExport();
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
  if (event->mimeData()->hasUrls() || event->mimeData()->hasImage()) event->acceptProposedAction();
}

void MainWindow::dropEvent(QDropEvent* event) {
  if (pasteImages(event->mimeData())) {
    event->acceptProposedAction();
    return;
  }
  if (openDroppedFiles(event->mimeData()->urls())) event->acceptProposedAction();
}

void MainWindow::toggleSidebar() {
  if (m_sidebarDock->isVisible()) {
    m_tree->setWatchEnabled(false);
    m_sidebarDock->hide();
    return;
  }
  if (m_tree->rootPath().isEmpty() && !m_currentPath.isEmpty())
    m_tree->setRoot(QFileInfo(m_currentPath).absolutePath());
  if (!m_tree->rootPath().isEmpty())
    m_sidebarRootLabel->setText(QDir(m_tree->rootPath()).dirName());
  m_folderLocationsMenu->setRoot(m_tree->rootPath());
  m_sidebarDock->show();
  resizeDocks({m_sidebarDock}, {240}, Qt::Horizontal);
  m_tree->setWatchEnabled(true);
  m_tree->selectPath(m_currentPath);
}

void MainWindow::openFolder() {
  const QString dir = QFileDialog::getExistingDirectory(this, tr("Open Folder"));
  if (!dir.isEmpty()) openFolderPath(dir);
}

bool MainWindow::openFolderPath(const QString& path) {
  const auto dir = QFileInfo(path).canonicalFilePath();
  if (path.isEmpty() || dir.isEmpty() || !QFileInfo(dir).isDir()) {
    statusBar()->showMessage(tr("Cannot open folder"), 4000);
    return false;
  }
  // Leaving the search page releases its persistent focus guard first.
  m_sidebarTabs->setCurrentIndex(0);
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restoreFocus = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  m_tree->setRoot(dir);
  m_tree->selectPath(m_currentPath);
  m_workspaceSearch->setRoot(dir);
  m_sidebarRootLabel->setText(QDir(dir).dirName());
  m_folderLocationsMenu->setRoot(dir);
  if (!m_benchMode) m_folderLocationsMenu->recordFolder(dir);
  m_sidebarDock->show();
  m_tree->setWatchEnabled(true);
  statusBar()->showMessage(tr("Workspace: %1").arg(dir), 4000);
  return true;
}

void MainWindow::openTreeEntry(const QString& path) { openFile(path); }

void MainWindow::openNewWindow(const QString& path) {
  if (!path.isEmpty() && !QFileInfo::exists(path)) {
    statusBar()->showMessage(tr("The file or folder is no longer available."), 4000);
    return;
  }
  const QString argument = path.isEmpty() ? QStringLiteral("--new") : path;
  if (!QProcess::startDetached(QCoreApplication::applicationFilePath(), {argument}))
    statusBar()->showMessage(tr("Cannot open a new window."), 4000);
}

qint64 MainWindow::searchFolderForBench(const QString& rootDir, const QString& pattern,
                                        int* hitsOut) {
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

void MainWindow::updateSearchFocusGuard() {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool hold = m_sidebarDock->isVisible() && m_sidebarTabs->currentIndex() == 2;
  if (hold && !m_searchFocusHeld) {
    m_searchFocusPrevious = editor->setParagraphSourceFocusCommitEnabled(false);
    m_searchFocusHeld = true;
  } else if (!hold && m_searchFocusHeld) {
    m_searchFocusHeld = false;
    editor->setParagraphSourceFocusCommitEnabled(m_searchFocusPrevious);
  }
}

void MainWindow::searchInFolder() {
  const QString root = m_tree->rootPath().isEmpty()
                           ? (m_currentPath.isEmpty() ? QDir::currentPath()
                                                      : QFileInfo(m_currentPath).absolutePath())
                           : m_tree->rootPath();
  if (m_tree->rootPath().isEmpty()) m_tree->setRoot(root);
  m_folderLocationsMenu->setRoot(root);
  if (!m_benchMode) m_folderLocationsMenu->recordFolder(root);
  if (!m_sidebarDock->isVisible()) toggleSidebar();
  m_sidebarTabs->setCurrentIndex(2);
  resizeDocks({m_sidebarDock}, {320}, Qt::Horizontal);
  m_workspaceSearch->setRoot(root);
  m_workspaceSearch->openFocused();
}

void MainWindow::openSearchHit(const platform::SearchHit& hit) {
  const auto target = hit;
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
  const auto restoreFocus = qScopeGuard(
      [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
  if (!openFile(target.path)) return;
  // Results describe the saved Markdown, so verify the line before moving the caret.
  const auto block = m_view->source()->findBlockByNumber(target.line - 1);
  if (!block.isValid() || block.text() != target.sourceLine) {
    statusBar()->showMessage(tr("Search result changed. Search again."), 5000);
    return;
  }
  if (!m_sourceMode) toggleSourceView();
  if (!m_sourceMode) return;
  QTextCursor cursor(m_document->findBlockByNumber(target.line - 1));
  cursor.setPosition(cursor.block().position() +
                     qBound(0, target.column, int(cursor.block().text().size())));
  cursor.setPosition(qMin(cursor.position() + target.length,
                          cursor.block().position() + int(cursor.block().text().size())),
                     QTextCursor::KeepAnchor);
  editor->setTextCursor(cursor);
  editor->ensureCursorVisible();
  editor->setFocus();
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
  if (!maybeSaveBeforeNavigate()) {
    event->ignore();
    return;
  }
  // A scan still in flight must not outlive the window that owns its results.
  m_workspaceSearch->stop();
  // Closing is the last chance to persist edits; the debounce window may still be open.
  if (!m_benchMode) m_autoSave->flushNow();
  if (!m_benchMode) {
    QSettings settings;
    settings.setValue(QStringLiteral("window/geometry"), saveGeometry());
    const auto file =
        m_currentPath.isEmpty() ? QString() : QFileInfo(m_currentPath).absoluteFilePath();
    auto folder = m_tree->rootPath();
    if (folder.isEmpty() && !file.isEmpty()) folder = QFileInfo(file).absolutePath();
    platform::updateClosedFiles(file);
    platform::setLastClosedTarget({file, folder});
  }
  QMainWindow::closeEvent(event);
}

void MainWindow::changeEvent(QEvent* event) {
  if (event->type() == QEvent::ApplicationPaletteChange && m_themeAction) applyTheme();
  QMainWindow::changeEvent(event);
}

void MainWindow::applyTheme() {
  const ui::Theme theme = ui::resolveTheme(themePreference(), systemPrefersDark());
  setStyleSheet(ui::editorStyleSheet(theme));
  applyThemePalette(themedPalette(theme));
  static_cast<ui::Editor*>(m_editor)->setDarkTheme(theme == ui::Theme::Dark);
  const QColor iconColor = theme == ui::Theme::Dark ? QColor("#aab0b6") : QColor("#858585");
  m_sourceAction->setIcon(navigationIcon(true, iconColor));
  m_sidebarAction->setIcon(navigationIcon(false, iconColor));
  if (m_themeAction != nullptr) {
    const QString name = m_themePreference == QStringLiteral("dark")    ? tr("Dark")
                         : m_themePreference == QStringLiteral("light") ? tr("Light")
                                                                        : tr("Follow system");
    m_themeAction->setText(tr("Theme: %1").arg(name));
  }
  for (QAction* action : findChildren<QAction*>())
    if (action->objectName().startsWith(QStringLiteral("appearance_")))
      action->setChecked(action->data().toString() == m_themePreference);
}

void MainWindow::setLanguage(const QString& preference) {
  qApp->removeTranslator(m_translator);
  m_translator->setLanguage(preference);
  qApp->installTranslator(m_translator);
  platform::setLanguagePreference(preference);
  retranslateUi();
}

void MainWindow::retranslateUi() {
  for (QAction* action : findChildren<QAction*>()) {
    const QByteArray source = action->property("translationSource").toString().toUtf8();
    if (!source.isEmpty()) {
      const QByteArray pattern = action->property("translationTemplate").toString().toUtf8();
      action->setText(
          pattern.isEmpty()
              ? tr(source.constData())
              : tr(pattern.constData()).arg(action->property("translationNumber").toInt()));
      action->setToolTip(action->text());
    }
  }
  m_sidebarDock->setWindowTitle(tr("Files"));
  m_sidebarTabs->setTabText(0, tr("Files"));
  m_sidebarTabs->setTabText(1, tr("Outline"));
  m_sidebarTabs->setTabText(2, tr("Search"));
  if (m_currentPath.isEmpty() && m_tree->rootPath().isEmpty())
    m_sidebarRootLabel->setText(tr("Workspace"));
  m_fileOptionsButton->setToolTip(tr("File Options"));
  m_fileOptionsButton->setAccessibleName(tr("File Options"));
  m_folderLocationsButton->setToolTip(tr("Recent and Pinned Folders"));
  m_folderLocationsButton->setAccessibleName(tr("Recent and Pinned Folders"));
  m_tree->setAccessibleName(tr("Files"));
  m_editor->setAccessibleName(tr("Markdown editor"));
  m_editor->setPlaceholderText(tr("Start writing, or open a Markdown file..."));
  if (m_detailsLabel) m_detailsLabel->setToolTip(tr("Document Statistics"));
  m_findBar->retranslateUi();
  m_sourceAction->setText(m_sourceMode ? tr("Rendered View") : tr("Source View"));
  rebuildRecentMenu();
  applyTheme();
  updateTitleAndStatus();
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
  return m_view->isSourceMode() ? m_editor->toPlainText()
                                : static_cast<ui::Editor*>(m_editor)->markdownWithParagraphDraft();
}

void MainWindow::markClean() {
  m_dirty = false;
  if (!m_benchMode) typist::ui::removeDraft(m_currentPath);
  updateTitleAndStatus();
}

void MainWindow::updateTitleAndStatus() {
  const QString base =
      m_currentPath.isEmpty() ? tr("Untitled") : QFileInfo(m_currentPath).fileName();
  setWindowTitle(base + (m_dirty ? QStringLiteral(" *") : QString()) + QStringLiteral(" — Typist"));
  if (m_detailsLabel)
    m_detailsLabel->setText(tr("%1 characters").arg(qMax(0, m_document->characterCount() - 1)));
  m_sourceAction->setChecked(m_sourceMode);
  statusBar()->clearMessage();
}

void MainWindow::rebuildRecentMenu() {
  if (!m_recentMenu) return;
  m_recentMenu->clear();
  for (const QString& path : typist::platform::recentFiles()) {
    if (!QFileInfo(path).isFile()) {
      if (!m_benchMode) platform::removeRecentFile(path);
      continue;
    }
    QAction* entry =
        m_recentMenu->addAction(QFileInfo(path).fileName().replace('&', QStringLiteral("&&")));
    entry->setData(path);
    entry->setToolTip(QDir::toNativeSeparators(path));
  }
  if (m_recentMenu->isEmpty()) {
    QAction* placeholder = m_recentMenu->addAction(tr("No recent files"));
    placeholder->setEnabled(false);
  }
  m_recentMenu->addSeparator();
  auto* clear = m_recentMenu->addAction(tr("Clear Recent Items"));
  clear->setObjectName(QStringLiteral("clearRecentItems"));
  connect(clear, &QAction::triggered, this, &MainWindow::clearRecentHistory);
}

void MainWindow::openRecentEntry(QAction* action) {
  if (!action || !action->isEnabled() || action->data().isNull()) return;
  openFile(action->data().toString());
}

void MainWindow::reopenClosedFile() {
  const QFileInfo current(m_currentPath);
  for (const auto& path : platform::closedFiles()) {
    if (QFileInfo(path) == current) continue;
    auto* editor = static_cast<ui::Editor*>(m_editor);
    const bool focusCommit = editor->setParagraphSourceFocusCommitEnabled(false);
    const auto restore = qScopeGuard(
        [editor, focusCommit] { editor->setParagraphSourceFocusCommitEnabled(focusCommit); });
    // The outgoing document stays in Recent Files while we walk back through the stack.
    openFileReplacingCurrent(path, false);
    return;
  }
  statusBar()->showMessage(tr("No closed files to reopen"), 4000);
}

void MainWindow::clearRecentHistory() {
  platform::clearFileHistory();
  platform::FolderLocationsStore().clearRecent();
  rebuildRecentMenu();
  statusBar()->showMessage(tr("Cleared recent items"), 4000);
}

void MainWindow::checkCurrentFileConflict() {
  if (m_currentPath.isEmpty()) return;
  QFile file(m_currentPath);
  if (!file.open(QIODevice::ReadOnly)) return;
  const QString disk = QString::fromUtf8(file.readAll());
  file.close();
  const bool diskDiffers = disk != m_savedMarkdown;
  if (typist::ui::decideConflict(m_dirty, diskDiffers, !m_benchMode) ==
      typist::ui::ConflictChoice::ReloadDisk) {
    if (m_dirty) {
      const QMessageBox::StandardButton answer =
          QMessageBox::question(this, tr("File changed on disk"),
                                tr("The file changed on disk while you have unsaved edits. Reload "
                                   "the disk version and discard your edits?"),
                                QMessageBox::Yes | QMessageBox::No, QMessageBox::No);
      if (answer != QMessageBox::Yes) {
        statusBar()->showMessage(tr("Keeping your unsaved edits"));
        return;
      }
    }
    openFile(m_currentPath);
    statusBar()->showMessage(tr("Reloaded file changed on disk"));
    return;
  }
  if (m_dirty && diskDiffers)
    statusBar()->showMessage(tr("File changed on disk; keeping your unsaved edits"));
}

void MainWindow::writeRecoveryDraft() {
  if (m_benchMode || !m_dirty) return;
  typist::ui::writeDraft(m_currentPath, currentMarkdown());
}

void MainWindow::toggleOutline() {
  if (m_sidebarDock->isVisible() && m_sidebarTabs->currentIndex() == 1) {
    m_sidebarDock->hide();
    return;
  }
  m_sidebarTabs->setCurrentIndex(1);
  m_sidebarDock->show();
  resizeDocks({m_sidebarDock}, {240}, Qt::Horizontal);
}

void MainWindow::pasteAsMarkdown() {
  const auto* source = QApplication::clipboard()->mimeData();
  if (source && source->hasText() &&
      static_cast<ui::Editor*>(m_editor)->pasteTableText(source->text()))
    return;
  if (pasteImages(QApplication::clipboard()->mimeData())) return;
  if (static_cast<ui::Editor*>(m_editor)->paragraphSourceActive() ||
      (!m_sourceMode &&
       m_editor->textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty))) {
    static_cast<ui::Editor*>(m_editor)->paste();
    return;
  }
  const QString text = QApplication::clipboard()->text();
  if (text.trimmed().isEmpty()) {
    statusBar()->showMessage(tr("Nothing to paste"));
    return;
  }
  if (m_sourceMode) {
    // The source view is plain text, so the Markdown markers stay verbatim.
    m_editor->paste();
    statusBar()->showMessage(tr("Pasted Markdown as text"));
    return;
  }
  QTextCursor cursor = m_editor->textCursor();
  QString error;
  if (!typist::editor::insertMarkdownAtCursor(text, cursor, &error)) {
    statusBar()->showMessage(error.isEmpty() ? tr("Cannot paste Markdown") : error);
    return;
  }
  m_editor->setTextCursor(cursor);
  statusBar()->showMessage(tr("Pasted Markdown"));
}

void MainWindow::pasteHtmlAsMarkdown() {
  const auto* mime = QApplication::clipboard()->mimeData();
  if (!mime || !mime->hasHtml()) {
    statusBar()->showMessage(tr("The clipboard does not contain HTML"));
    return;
  }
  auto* writingEditor = static_cast<ui::Editor*>(m_editor);
  if (mime->hasText() && writingEditor->pasteTableText(mime->text())) return;
  const bool literal = writingEditor->paragraphSourceActive() ||
                       (!m_sourceMode && m_editor->textCursor().blockFormat().hasProperty(
                                             editor::kCodeBlockProperty));
  if (literal && mime->hasText()) {
    writingEditor->insertPlainText(mime->text());
    return;
  }
  const auto imported = editor::importHtml(mime->html());
  if (!imported.ok()) {
    statusBar()->showMessage(imported.error);
    return;
  }
  if (m_sourceMode || literal) {
    writingEditor->insertPlainText(imported.markdown);
  } else {
    auto cursor = m_editor->textCursor();
    QString error;
    if (!editor::insertMarkdownAtCursor(imported.markdown, cursor, &error)) {
      statusBar()->showMessage(error);
      return;
    }
    m_editor->setTextCursor(cursor);
  }
  statusBar()->showMessage(tr("Pasted HTML as Markdown; web page styling is simplified"), 6000);
}

void MainWindow::insertImageLink(const QString& path) {
  QMimeData source;
  source.setUrls({QUrl::fromLocalFile(path)});
  pasteImages(&source);
}
bool MainWindow::pasteImages(const QMimeData* source) {
  if (!source) return false;
  QStringList files;
  for (const auto& url : source->urls()) {
    const auto path = platform::firstImagePath({url});
    if (!path.isEmpty()) files.append(path);
  }
  if (files.isEmpty() && !source->hasImage()) return false;
  if (!m_sourceMode &&
      m_editor->textCursor().blockFormat().hasProperty(editor::kCodeBlockProperty)) {
    if (source->hasText()) return false;  // Code paste keeps its literal text behavior.
    statusBar()->showMessage(tr("Place the cursor outside a code block to insert an image"));
    return true;
  }
  if (files.size() > 20) {
    statusBar()->showMessage(tr("Insert up to 20 images at a time"));
    return true;
  }
  QStringList markdown;
  if (!files.isEmpty()) {
    for (const auto& file : files) {
      const auto stored =
          platform::importImage(file, m_currentPath, platform::copyImagesToAssets());
      if (!stored.ok()) {
        statusBar()->showMessage(stored.error);
        return true;
      }
      markdown.append(platform::imageMarkdown(stored.target, QFileInfo(file).fileName()));
    }
  } else {
    const auto stored =
        platform::storeImage(qvariant_cast<QImage>(source->imageData()), m_currentPath);
    if (!stored.ok()) {
      statusBar()->showMessage(stored.error);
      return true;
    }
    markdown.append(platform::imageMarkdown(stored.target, tr("Pasted image")));
  }
  insertImageMarkdown(markdown);
  return true;
}
bool MainWindow::insertImageMarkdown(const QStringList& images) {
  auto* editor = static_cast<ui::Editor*>(m_editor);
  const QString markdown = images.join(QStringLiteral("\n\n"));
  if (m_sourceMode || editor->paragraphSourceActive()) {
    editor->insertPlainText(markdown);
    statusBar()->showMessage(tr("Inserted %1 images").arg(images.size()));
    return true;
  }
  QTextCursor cursor = m_editor->textCursor();
  QString error;
  if (!typist::editor::insertMarkdownAtCursor(markdown, cursor, &error)) {
    statusBar()->showMessage(tr("Cannot insert image link"));
    return false;
  }
  m_editor->setTextCursor(cursor);
  statusBar()->showMessage(tr("Inserted %1 images").arg(images.size()));
  m_editor->setCurrentCharFormat(QTextCharFormat());
  return true;
}

bool MainWindow::eventFilter(QObject* watched, QEvent* event) {
  if (watched == m_detailsLabel && event->type() == QEvent::MouseButtonRelease) {
    showDocumentStatistics();
    return true;
  }
  if (m_editor != nullptr && watched == m_editor->viewport() &&
      event->type() == QEvent::MouseButtonRelease) {
    toggleTaskAtClick(static_cast<QMouseEvent*>(event));
  }
  return QMainWindow::eventFilter(watched, event);
}

void MainWindow::toggleTaskAtClick(const QMouseEvent* event) {
  if (m_sourceMode || m_benchMode || event->button() != Qt::LeftButton) return;
  QTextDocument* document = m_editor->document();
  const int blockNumber = m_editor->cursorForPosition(event->pos()).blockNumber();
  bool checked = false;
  if (!typist::editor::taskCheckboxState(document, blockNumber, &checked)) return;
  // The checkbox glyph is the first character of the item; clicks on it toggle,
  // clicks on the item text stay ordinary edits.
  QTextBlock block = document->findBlockByNumber(blockNumber);
  QTextCursor probe(block);
  probe.setPosition(block.position() + 1);
  if (event->pos().x() > m_editor->cursorRect(probe).x() + 2.0) return;
  if (!typist::editor::toggleTaskCheckbox(document, blockNumber)) return;
  statusBar()->showMessage(tr("Task toggled"));
}

void MainWindow::checkForUpdates() {
#ifdef TYPIST_HAS_NETWORK
  if (m_network == nullptr) {
    m_network = new QNetworkAccessManager(this);
    connect(m_network, &QNetworkAccessManager::finished, this, &MainWindow::handleUpdateReply);
  }
  QNetworkRequest request{QUrl{typist::ui::updateFeedUrl()}};
  request.setTransferTimeout(5000);
  statusBar()->showMessage(tr("Checking for updates"));
  m_network->get(request);
#else
  statusBar()->showMessage(tr("Update check unavailable in this build"));
#endif
}

#ifdef TYPIST_HAS_NETWORK
void MainWindow::handleUpdateReply(QNetworkReply* reply) {
  QByteArray body;
  if (reply != nullptr && reply->error() == QNetworkReply::NoError) body = reply->readAll();
  if (reply != nullptr) reply->deleteLater();
  typist::ui::UpdateFeed feed;
  QString reason;
  if (!typist::ui::parseUpdateFeed(body, &feed, &reason)) {
    statusBar()->showMessage(reason.isEmpty()
                                 ? tr("Update check failed")
                                 : QStringLiteral("Update check failed: %1").arg(reason));
    return;
  }
  const QString current = QStringLiteral(TYPIST_VERSION);
  if (typist::ui::versionIsNewer(feed.version, current)) {
    statusBar()->showMessage(tr("Version %1 is available").arg(feed.version));
  } else {
    statusBar()->showMessage(tr("You are on the latest version"));
  }
}
#endif

void MainWindow::recoverLatestDraft() {
  const QStringList drafts = typist::ui::draftPaths();
  if (drafts.isEmpty()) {
    statusBar()->showMessage(tr("No drafts to recover"));
    return;
  }

  QFile file(drafts.first());
  if (!file.open(QIODevice::ReadOnly)) {
    statusBar()->showMessage(tr("Cannot open draft"));
    return;
  }
  const QString text = QString::fromUtf8(file.readAll());
  file.close();

  if (!maybeSaveBeforeNavigate()) return;

  QString error;
  static_cast<ui::Editor*>(m_editor)->finishParagraphSourceEdit(false);
  if (!m_view->loadMarkdownIntoBoth(text, &error)) {
    statusBar()->showMessage(error);
    return;
  }
  m_document = m_view->active();
  m_view->showActiveIn(m_editor);
  m_sourceMode = m_view->isSourceMode();
  // Recovery lands in an untitled buffer: the writer decides where it is saved.
  m_outlinePanel->setDocument(m_document, true);
  if (!m_benchMode) platform::updateClosedFiles(m_currentPath);
  m_currentPath.clear();
  resetPreviousExport();
  m_savedMarkdown = text;
  m_dirty = true;
  file.remove();
  updateTitleAndStatus();
  statusBar()->showMessage(tr("Recovered draft into an unsaved buffer"));
}

}  // namespace typist
