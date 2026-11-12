#include "DocxWriter.h"
#include "Editor.h"
#include "EpubDialog.h"
#include "FileTree.h"
#include "FindBar.h"
#include "FolderLocations.h"
#include "FolderLocationsMenu.h"
#include "HtmlImportDialog.h"
#include "HtmlImporter.h"
#include "ImageExportDialog.h"
#include "LinkDialog.h"
#include "LinkNavigation.h"
#include "MainWindow.h"
#include "PdfExportDialog.h"
#include "PdfWriter.h"
#include "QuickOpenDialog.h"
#include "Settings.h"
#include "SpecialContent.h"
#include "TableEditing.h"
#include "Toc.h"
#include "WorkspaceSearchPanel.h"
#include "editor_document.h"
#include "markdown.h"

#include <QAction>
#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QComboBox>
#include <QContextMenuEvent>
#include <QDesktopServices>
#include <QDialogButtonBox>
#include <QDir>
#include <QDockWidget>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileDialog>
#include <QInputDialog>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QMap>
#include <QMenu>
#include <QMenuBar>
#include <QMessageBox>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QRegularExpression>
#include <QScopeGuard>
#include <QScopedPointer>
#include <QScrollBar>
#include <QSettings>
#include <QSpinBox>
#include <QStatusBar>
#include <QTabWidget>
#include <QTemporaryDir>
#include <QTextBlock>
#include <QTextEdit>
#include <QTextList>
#include <QTextTable>
#include <QTimer>
#include <QToolBar>
#include <QToolButton>
#include <QTreeWidget>
#include <QXmlStreamReader>
#include <QtTest>

namespace {
struct FolderHistoryGuard {
  QSettings settings{QSettings::defaultFormat(), QSettings::UserScope, QStringLiteral("Typist"),
                     QStringLiteral("Typist")};
  QVariant recent = settings.value(QStringLiteral("folders/recent"));
  QVariant pinned = settings.value(QStringLiteral("folders/pinned"));
  FolderHistoryGuard() {
    settings.remove(QStringLiteral("folders/recent"));
    settings.remove(QStringLiteral("folders/pinned"));
    settings.sync();
  }
  ~FolderHistoryGuard() {
    for (const auto& pair : {qMakePair(QStringLiteral("folders/recent"), recent),
                             qMakePair(QStringLiteral("folders/pinned"), pinned)}) {
      if (pair.second.isValid())
        settings.setValue(pair.first, pair.second);
      else
        settings.remove(pair.first);
    }
    settings.sync();
  }
};
struct FileHistoryGuard {
  QSettings settings;
  QMap<QString, QVariant> previous;
  FileHistoryGuard() {
    for (const auto& key : {QStringLiteral("recentFiles"), QStringLiteral("history/closedFiles"),
                            QStringLiteral("startup/lastClosed"), QStringLiteral("startup/options"),
                            QStringLiteral("window/geometry"), QStringLiteral("editor/autoSave")})
      previous.insert(key, settings.value(key));
    typist::platform::clearFileHistory();
  }
  ~FileHistoryGuard() {
    for (auto it = previous.cbegin(); it != previous.cend(); ++it) {
      if (it.value().isValid())
        settings.setValue(it.key(), it.value());
      else
        settings.remove(it.key());
    }
    settings.sync();
  }
};
QAction* actionBySource(QWidget& widget, const QString& source) {
  for (auto* action : widget.findChildren<QAction*>())
    if (action->property("translationSource").toString() == source) return action;
  return nullptr;
}
}  // namespace

class LinkReceiver : public QObject {
  Q_OBJECT
 public:
  QUrl last;
 public slots:
  void capture(const QUrl& url) { last = url; }
};

class WindowTests : public QObject {
  Q_OBJECT
 private slots:
  void initTestCase() {
    QApplication::setOrganizationName(QStringLiteral("Typist"));
    QApplication::setApplicationName(QStringLiteral("Typist"));
    QApplication::setStyle(QStringLiteral("Fusion"));
    QApplication::setWindowIcon(QIcon(QStringLiteral(":/packaging/linux/typist.png")));
    QSettings::setDefaultFormat(QSettings::IniFormat);
    QSettings::setPath(QSettings::IniFormat, QSettings::UserScope, m_settings.path());
  }

  void closedHistoryMigratesFromStartupAndSkipsUnavailableFiles() {
    using namespace typist::platform;
    const FileHistoryGuard settings;
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QStringList paths;
    for (int i = 0; i < 11; ++i) {
      QFile file(directory.filePath(QStringLiteral("note-%1.md").arg(i)));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.close();
      paths.append(QFileInfo(file).canonicalFilePath());
    }
    setLastClosedTarget({paths.first(), directory.path()});
    QCOMPARE(closedFiles(), QStringList{paths.first()});
    updateClosedFiles(paths.first());
    setLastClosedTarget({});
    QCOMPARE(closedFiles(), QStringList{paths.first()});
    for (const auto& path : paths.mid(1)) updateClosedFiles(path);
    QCOMPARE(closedFiles().size(), 8);
    QCOMPARE(closedFiles().first(), paths.last());
    QCOMPARE(closedFiles().last(), paths.at(3));
    updateClosedFiles(paths.at(4));
    QCOMPARE(closedFiles().first(), paths.at(4));
    QCOMPARE(closedFiles().count(paths.at(4)), 1);
    QVERIFY(QFile::remove(paths.at(4)));
    QCOMPARE(closedFiles().first(), paths.last());
    updateClosedFiles({}, paths.last());
    QVERIFY(!closedFiles().contains(paths.last()));
    const auto before = closedFiles();
    typist::MainWindow bench(nullptr, true);
    QVERIFY(bench.openFile(before.first()));
    QVERIFY(bench.close());
    QCOMPARE(closedFiles(), before);
    QVERIFY(recentFiles().isEmpty());
  }

  void reopenClosedFileKeepsDraftOnCancelAndWalksBackThroughHistory() {
    using namespace typist::platform;
    const FileHistoryGuard history;
    const FolderHistoryGuard folders;
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto write = [&](const QString& name) {
      QFile file(directory.filePath(name + QStringLiteral(".md")));
      if (!file.open(QIODevice::WriteOnly) || file.write(name.toUtf8()) < 0) return QString();
      file.close();
      return QFileInfo(file).canonicalFilePath();
    };
    const auto first = write(QStringLiteral("First"));
    const auto second = write(QStringLiteral("Second"));
    const auto current = write(QStringLiteral("Current"));
    QVERIFY(!first.isEmpty() && !second.isEmpty() && !current.isEmpty());

    typist::MainWindow window;
    QVERIFY(window.openFile(first));
    QVERIFY(window.openFile(second));
    QVERIFY(window.openFile(current));
    window.show();
    auto* reopen = window.findChild<QAction*>(QStringLiteral("reopenClosedFile"));
    auto* fresh = window.findChild<QAction*>(QStringLiteral("newDocument"));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(reopen && fresh && editor);
    QCOMPARE(closedFiles(), (QStringList{second, first}));
    editor->moveCursor(QTextCursor::Start);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(input);
    input->setPlainText(QStringLiteral("Pending **draft**."));
    const auto choose = [&](QMessageBox::StandardButton answer) {
      QTimer::singleShot(0, &window, [answer] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
          box->button(answer)->click();
      });
      reopen->trigger();
    };
    choose(QMessageBox::Cancel);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(input->toPlainText(), QStringLiteral("Pending **draft**."));
    QCOMPARE(closedFiles(), (QStringList{second, first}));
    choose(QMessageBox::Discard);
    QVERIFY(!editor->paragraphSourceActive());
    QCOMPARE(editor->toPlainText(), QStringLiteral("Second"));
    QCOMPARE(closedFiles(), QStringList{first});
    reopen->trigger();
    QCOMPARE(editor->toPlainText(), QStringLiteral("First"));
    QVERIFY(closedFiles().isEmpty());
    QVERIFY(recentFiles().contains(current));

    fresh->trigger();
    QVERIFY(editor->toPlainText().isEmpty());
    QCOMPARE(closedFiles(), QStringList{first});
    reopen->trigger();
    QCOMPARE(editor->toPlainText(), QStringLiteral("First"));
    QVERIFY(closedFiles().isEmpty());
    QVERIFY(window.close());
    QCOMPARE(closedFiles(), QStringList{first});
  }

  void clearRecentItemsKeepsPinnedFoldersAndCurrentDocument() {
    using namespace typist::platform;
    const FileHistoryGuard history;
    const FolderHistoryGuard folders;
    const auto language = languagePreference();
    const auto restoreLanguage = qScopeGuard([&] { setLanguagePreference(language); });
    setLanguagePreference(QStringLiteral("zh"));
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto folder = QFileInfo(directory.path()).canonicalFilePath();
    const auto path = directory.filePath(QStringLiteral("Note & file.md"));
    QFile document(path);
    QVERIFY(document.open(QIODevice::WriteOnly));
    const QByteArray contents("Saved.");
    QCOMPARE(document.write(contents), contents.size());
    document.close();
    const auto file = QFileInfo(document).canonicalFilePath();
    FolderLocationsStore store;
    store.record(folder);
    QVERIFY(store.setPinned(folder, true));
    setLaunchOptions({LaunchMode::CustomFolder, folder});
    setLastClosedTarget({file, folder});

    typist::MainWindow window;
    QVERIFY(window.openFile(file));
    updateClosedFiles(file);
    auto* menu = window.findChild<QMenu*>(QStringLiteral("recentFilesMenu"));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(menu && editor);
    auto* clear = menu->findChild<QAction*>(QStringLiteral("clearRecentItems"));
    QVERIFY(clear);
    QCOMPARE(clear->text(), QStringLiteral("清除最近记录"));
    bool hasEntry = false;
    for (auto* action : menu->actions()) {
      if (action->data().toString() != file) continue;
      hasEntry = true;
      QCOMPARE(action->text(), QStringLiteral("Note && file.md"));
      QCOMPARE(action->toolTip(), QDir::toNativeSeparators(file));
    }
    QVERIFY(hasEntry);
    editor->insertPlainText(QStringLiteral("Pending "));
    const auto before = editor->toPlainText();
    QVERIFY(editor->document()->isUndoAvailable());
    clear->trigger();
    QVERIFY(recentFiles().isEmpty());
    QVERIFY(closedFiles().isEmpty());
    QVERIFY(lastClosedTarget().file.isEmpty() && lastClosedTarget().folder.isEmpty());
    QVERIFY(store.read().recent.isEmpty());
    QCOMPARE(store.read().pinned, QStringList{folder});
    QCOMPARE(launchOptions().mode, LaunchMode::CustomFolder);
    QCOMPARE(launchOptions().customFolder, folder);
    QCOMPARE(editor->toPlainText(), before);
    QVERIFY(editor->document()->isUndoAvailable());
    QVERIFY(document.open(QIODevice::ReadOnly));
    QCOMPARE(document.readAll(), contents);
    window.setBenchMode(true);
  }

  void startupOptionsResolveExistingPathsAndKeepBenchmarksIsolated() {
    using namespace typist::platform;
    QSettings settings;
    const auto optionsKey = QStringLiteral("startup/options");
    const auto savedOptions = settings.value(optionsKey);
    const auto savedTarget = lastClosedTarget();
    const auto restore = qScopeGuard([&] {
      if (savedOptions.isValid())
        settings.setValue(optionsKey, savedOptions);
      else
        settings.remove(optionsKey);
      setLastClosedTarget(savedTarget);
    });
    settings.remove(optionsKey);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto folder = directory.filePath(QStringLiteral("工作 区"));
    QVERIFY(QDir().mkpath(folder));
    const auto file = directory.filePath(QStringLiteral("记录 #1.md"));
    const auto html = directory.filePath(QStringLiteral("import.HTML"));
    for (const auto& path : {file, html}) {
      QFile input(path);
      QVERIFY(input.open(QIODevice::WriteOnly));
      QVERIFY(input.write("Input") > 0);
    }
    setLastClosedTarget({file, folder});
    QCOMPARE(launchOptions().mode, LaunchMode::NewDocument);
    QVERIFY(startupTarget({}).file.isEmpty() && startupTarget({}).folder.isEmpty());

    setLaunchOptions({LaunchMode::LastFolder, {}});
    QCOMPARE(startupTarget({}).folder, folder);
    QVERIFY(startupTarget({}).file.isEmpty());
    setLaunchOptions({LaunchMode::LastFileAndFolder, {}});
    QCOMPARE(startupTarget({}).file, file);
    QCOMPARE(startupTarget({}).folder, folder);
    QVERIFY(startupTarget({QStringLiteral("--new")}).file.isEmpty());
    QVERIFY(startupTarget({QStringLiteral("--new")}).folder.isEmpty());
    const auto explicitFile = startupTarget({QStringLiteral("--new"), html});
    QCOMPARE(explicitFile.file, html);
    QVERIFY(explicitFile.folder.isEmpty() && explicitFile.importHtml);
    QCOMPARE(startupTarget({folder}).folder, folder);
    QVERIFY(startupTarget({folder}).file.isEmpty());
    const auto missing = directory.filePath(QStringLiteral("missing.md"));
    QVERIFY(startupTarget({missing}).file.isEmpty() && startupTarget({missing}).folder.isEmpty());
    QVERIFY(startupTarget({}, true).file.isEmpty() && startupTarget({}, true).folder.isEmpty());
    const auto benchInput = startupTarget({file}, true, html);
    QCOMPARE(benchInput.file, html);
    QVERIFY(benchInput.importHtml);
    QCOMPARE(startupTarget({file}, false, html).file, file);

    setLaunchOptions({LaunchMode::CustomFolder, folder});
    QCOMPARE(startupTarget({}).folder, folder);
    QVERIFY(startupTarget({}).file.isEmpty());
    const auto reopened = startupTarget({QStringLiteral("--reopen-file")});
    QCOMPARE(reopened.file, file);
    QCOMPARE(reopened.folder, folder);
    QVERIFY(QFile::remove(file));
    const auto missingFile = startupTarget({QStringLiteral("--reopen-file")});
    QVERIFY(missingFile.file.isEmpty());
    QCOMPARE(missingFile.folder, folder);
    QVERIFY(QDir().rmdir(folder));
    QVERIFY(startupTarget({}).folder.isEmpty());
  }

  void launchMenuPersistsChoicesAndRestoresOnlyAfterAcceptedClose() {
    using namespace typist::platform;
    QSettings settings;
    const auto priorOptions = launchOptions();
    const auto priorTarget = lastClosedTarget();
    const auto autoSave = QStringLiteral("editor/autoSave");
    const auto geometry = QStringLiteral("window/geometry");
    const auto priorAutoSave = settings.value(autoSave);
    const auto priorGeometry = settings.value(geometry);
    const auto priorLanguage = languagePreference();
    const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] {
      setLaunchOptions(priorOptions);
      setLastClosedTarget(priorTarget);
      setLanguagePreference(priorLanguage);
      for (const auto& entry :
           {qMakePair(autoSave, priorAutoSave), qMakePair(geometry, priorGeometry)}) {
        if (entry.second.isValid())
          settings.setValue(entry.first, entry.second);
        else
          settings.remove(entry.first);
      }
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
    });
    const FolderHistoryGuard folders;
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    setLanguagePreference(QStringLiteral("en"));
    setLaunchOptions({});
    setLastClosedTarget({});
    settings.setValue(autoSave, false);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto workspace = directory.filePath(QStringLiteral("workspace"));
    const auto customFolder = directory.filePath(QStringLiteral("custom startup"));
    QVERIFY(QDir().mkpath(workspace));
    QVERIFY(QDir().mkpath(customFolder));
    const auto canonicalWorkspace = QFileInfo(workspace).canonicalFilePath();
    const auto path = directory.filePath(QStringLiteral("saved.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write("Saved body.\n"), 12);
    file.close();

    typist::MainWindow window;
    QVERIFY(window.openFile(path));
    QVERIFY(window.openFolderPath(workspace));
    window.show();
    auto* empty = window.findChild<QAction*>(QStringLiteral("launchNewDocument"));
    auto* last = window.findChild<QAction*>(QStringLiteral("launchLastFileAndFolder"));
    auto* custom = window.findChild<QAction*>(QStringLiteral("launchCustomFolder"));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(empty && last && custom && editor && empty->isChecked());
    QCOMPARE(last->text(), QStringLiteral("Restore Last File and Folder"));
    last->trigger();
    QCOMPARE(launchOptions().mode, LaunchMode::LastFileAndFolder);
    const auto chooseFolder = [&](bool accept) {
      bool found = false;
      QTimer automation, safety;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        automation.stop();
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        found = input && dialog->fileMode() == QFileDialog::Directory;
        if (accept && found) {
          input->setText(customFolder);
          QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
        } else {
          dialog->reject();
        }
      });
      safety.setSingleShot(true);
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = QApplication::activeModalWidget()) modal->close();
      });
      automation.start();
      safety.start(3000);
      custom->trigger();
      return found;
    };
    QVERIFY(chooseFolder(false));
    QVERIFY(last->isChecked());
    QCOMPARE(launchOptions().mode, LaunchMode::LastFileAndFolder);
    QVERIFY(chooseFolder(true));
    QVERIFY(custom->isChecked());
    QCOMPARE(launchOptions().mode, LaunchMode::CustomFolder);
    QCOMPARE(QFileInfo(launchOptions().customFolder).canonicalFilePath(),
             QFileInfo(customFolder).canonicalFilePath());
    last->trigger();
    auto* chinese = window.findChild<QAction*>(QStringLiteral("language_zh"));
    QVERIFY(chinese);
    chinese->trigger();
    QCOMPARE(last->text(), QStringLiteral("恢复上次的文件和文件夹"));

    editor->insertPlainText(QStringLiteral("Unsaved "));
    const auto closeWith = [&](QMessageBox::StandardButton choice) {
      QTimer::singleShot(0, &window, [choice] {
        if (auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget()))
          box->button(choice)->click();
      });
      return window.close();
    };
    QVERIFY(!closeWith(QMessageBox::Cancel));
    QVERIFY(lastClosedTarget().file.isEmpty() && lastClosedTarget().folder.isEmpty());
    QVERIFY(editor->toPlainText().contains(QStringLiteral("Unsaved")));
    QVERIFY(closeWith(QMessageBox::Discard));
    QCOMPARE(lastClosedTarget().file, path);
    QCOMPARE(lastClosedTarget().folder, canonicalWorkspace);

    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    QVERIFY(reopened.openStartupTarget(startupTarget({})));
    reopened.show();
    const auto* restoredTree = reopened.findChild<typist::ui::FileTree*>();
    const auto* restoredEditor =
        reopened.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(restoredTree && restoredEditor);
    QCOMPARE(restoredTree->rootPath(), canonicalWorkspace);
    QVERIFY(restoredTree->isVisible());
    QCOMPARE(restoredEditor->toPlainText(), QStringLiteral("Saved body."));
    QVERIFY(reopened.close());
    QCOMPARE(lastClosedTarget().file, path);
  }

  void quickOpenPreservesDraftOnCancelAndWorkspaceOnOpen() {
    QTemporaryDir directory;
    const auto original = directory.filePath(QStringLiteral("original.md"));
    const auto target = directory.filePath(QStringLiteral("nested/target.md"));
    QDir().mkpath(QFileInfo(target).absolutePath());
    for (const auto& path : {original, target}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(path == target ? "# Target\n" : "Original paragraph.\n");
    }
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.show();
    window.openFile(original);
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    auto* tree = window.findChild<typist::ui::FileTree*>();
    tree->setRoot(directory.path());
    window.findChild<QDockWidget*>(QStringLiteral("navigationSidebar"))->show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("Unsaved **draft**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    typist::platform::addRecentFile(target);
    QTimer safety;
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
    });
    safety.start(3000);
    auto* quickOpen = window.findChild<QAction*>(QStringLiteral("openQuickly"));
    QVERIFY(quickOpen);
    bool dismissed = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto* picker =
              qobject_cast<typist::ui::QuickOpenDialog*>(QApplication::activeModalWidget())) {
        dismissed = true;
        QTest::keyClick(picker->findChild<QLineEdit*>(), Qt::Key_Escape);
      }
    });
    quickOpen->trigger();
    QCoreApplication::processEvents();
    QVERIFY(dismissed);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **draft**."));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    for (bool discard : {false, true}) {
      bool chosen = false, prompted = false;
      QTimer automate;
      connect(&automate, &QTimer::timeout, &window, [&] {
        if (auto* picker =
                qobject_cast<typist::ui::QuickOpenDialog*>(QApplication::activeModalWidget())) {
          if (chosen) return;
          chosen = true;
          auto* query = picker->findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
          query->setText(QStringLiteral("target"));
          QTest::keyClick(query, Qt::Key_Return);
        } else if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
          prompted = true;
          prompt->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
        }
      });
      automate.start(10);
      quickOpen->trigger();
      QCoreApplication::processEvents();
      QVERIFY(chosen && prompted);
      if (!discard) {
        QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
        QCOMPARE(editor->document()->availableUndoSteps(), steps);
        QVERIFY(editor->paragraphSourceActive());
        QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **draft**."));
      }
    }
    QVERIFY(editor->toPlainText().contains(QStringLiteral("Target")));
    QVERIFY(!editor->paragraphSourceActive());
    QCOMPARE(tree->rootPath(), directory.path());
    QFile file(original);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("Original paragraph.\n"));
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }

  void fileBrowserPreferencesLanguagesAndDocumentStayIntact() {
    const auto previous = typist::platform::fileBrowserOptions();
    const auto restore = qScopeGuard([&] { typist::platform::setFileBrowserOptions(previous); });
    typist::platform::setFileBrowserOptions({});
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    const auto root = directory.filePath(QStringLiteral("Writing workspace"));
    QDir().mkpath(root + QStringLiteral("/Guides"));
    for (const auto& name :
         {QStringLiteral("1-introduction.md"), QStringLiteral("2-plan.md"),
          QStringLiteral("10-review.md"), QStringLiteral("Guides/写作指南.MD")}) {
      QFile file(QDir(root).filePath(name));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("# Writing\n\nA document for the file browser.\n");
    }
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    window.openFile(QDir(root).filePath(QStringLiteral("Guides/写作指南.MD")));
    auto* tree = window.findChild<typist::ui::FileTree*>();
    tree->setRoot(root);
    tree->selectPath(QDir(root).filePath(QStringLiteral("Guides/写作指南.MD")));
    auto* dock = window.findChild<QDockWidget*>(QStringLiteral("navigationSidebar"));
    dock->show();
    window.findChild<QLabel*>(QStringLiteral("workspaceName"))->setText(QDir(root).dirName());
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral(" unsaved editing"));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    auto* button = window.findChild<QToolButton*>(QStringLiteral("fileOptions"));
    QVERIFY(button && button->menu());
    auto* menu = button->menu();
    menu->findChild<QAction*>(QStringLiteral("browserList"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("browserNatural"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("browserDescending"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("browserGroupByFolder"))->trigger();
    menu->findChild<QAction*>(QStringLiteral("browserCreated"))->trigger();
    QCOMPARE(tree->options().view, typist::platform::FileBrowserView::List);
    QCOMPARE(typist::platform::fileBrowserOptions().sort,
             typist::platform::FileBrowserSort::Created);
    QVERIFY(typist::platform::fileBrowserOptions().groupByFolder);
    QVERIFY(typist::platform::fileBrowserOptions().descending);
    QCOMPARE(tree->selectedPath(), QDir(root).filePath(QStringLiteral("Guides/写作指南.MD")));
    const auto output = qEnvironmentVariable("TYPIST_FILE_BROWSER_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      QCoreApplication::processEvents();
      QCOMPARE(button->toolTip(), language == QLatin1String("en") ? QStringLiteral("File Options")
                                                                  : QStringLiteral("文件选项"));
      QCOMPARE(menu->findChild<QAction*>(QStringLiteral("browserList"))->text(),
               language == QLatin1String("en") ? QStringLiteral("File List")
                                               : QStringLiteral("文件列表"));
      QCOMPARE(menu->findChild<QAction*>(QStringLiteral("browserModified"))->text(),
               language == QLatin1String("en") ? QStringLiteral("Modified Date")
                                               : QStringLiteral("修改时间"));
      QCOMPARE(menu->findChild<QAction*>(QStringLiteral("browserCreated"))->text(),
               language == QLatin1String("en") ? QStringLiteral("Created Date")
                                               : QStringLiteral("创建时间"));
      QCOMPARE(menu->findChild<QAction*>(QStringLiteral("browserGroupByFolder"))->text(),
               language == QLatin1String("en") ? QStringLiteral("Group by Folder")
                                               : QStringLiteral("按文件夹分组"));
      QCOMPARE(tree->topLevelItem(0)->text(0), language == QLatin1String("en")
                                                   ? QStringLiteral("Workspace Root")
                                                   : QStringLiteral("工作区根目录"));
      QVERIFY(menu->findChild<QAction*>(QStringLiteral("browserGroupByFolder"))->isChecked());
      QVERIFY(menu->findChild<QAction*>(QStringLiteral("browserCreated"))->isChecked());
      QVERIFY(menu->findChild<QAction*>(QStringLiteral("browserList"))->isChecked());
      QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
      QCOMPARE(editor->document()->availableUndoSteps(), steps);
      if (!output.isEmpty()) {
        window.resize(1120, 760);
        QTest::qWait(80);
        QVERIFY(window.grab().save(output + QStringLiteral("/browser-") + language +
                                   QStringLiteral(".png")));
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        QTest::qWait(60);
        QVERIFY(menu->grab().save(output + QStringLiteral("/menu-") + language +
                                  QStringLiteral(".png")));
        menu->close();
      }
    }
    // Toggling the sidebar must keep the root chosen by Open Folder.
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSidebar", Qt::DirectConnection));
    QVERIFY(!dock->isVisible());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSidebar", Qt::DirectConnection));
    QCOMPARE(tree->rootPath(), root);
    QCOMPARE(window.findChild<QLabel*>(QStringLiteral("workspaceName"))->text(),
             QStringLiteral("Writing workspace"));
    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    QCOMPARE(reopened.findChild<typist::ui::FileTree*>()->options().view,
             typist::platform::FileBrowserView::List);
    QCOMPARE(reopened.findChild<typist::ui::FileTree*>()->options().sort,
             typist::platform::FileBrowserSort::Created);
    QVERIFY(reopened.findChild<typist::ui::FileTree*>()->options().groupByFolder);
    QVERIFY(reopened.findChild<typist::ui::FileTree*>()->options().descending);
    window.findChild<QAction*>(QStringLiteral("fileTree"))->trigger();
    QCOMPARE(tree->options().view, typist::platform::FileBrowserView::Tree);
    window.findChild<QAction*>(QStringLiteral("fileList"))->trigger();
    QCOMPARE(tree->options().view, typist::platform::FileBrowserView::List);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
  }

  void fileActionsBilingualThemesKeepUnsavedDocument() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("写作.md"));
    const auto original = QStringLiteral("# Writing\n\nSaved paragraph.\n").toUtf8();
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    QCOMPARE(file.write(original), qint64(original.size()));
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.openFile(path));
    QVERIFY(window.openFolderPath(directory.path()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    auto cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    cursor.insertText(QStringLiteral(" Unsaved edits."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    const auto base = editor->document()->baseUrl();
    auto* tree = window.findChild<typist::ui::FileTree*>();
    const auto output = qEnvironmentVariable("TYPIST_FILE_ACTIONS_OUTPUT");
    QVERIFY(tree);
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      const bool chinese = language == QLatin1String("zh");
      qInfo("File actions: %s menu", qPrintable(language));
      auto* languageAction = window.findChild<QAction*>(QStringLiteral("language_") + language);
      auto* themeAction = window.findChild<QAction*>(chinese ? QStringLiteral("appearance_dark")
                                                             : QStringLiteral("appearance_light"));
      QVERIFY(languageAction);
      QVERIFY(themeAction);
      languageAction->trigger();
      themeAction->trigger();
      tree->selectPath(path);
      QScopedPointer<QMenu> menu(tree->createContextMenu());
      for (const auto* id :
           {"fileDuplicate", "fileNewFolder", "fileCopyRelativePath", "fileCopyPath"})
        QVERIFY2(menu->findChild<QAction*>(QString::fromLatin1(id)), id);
      QCOMPARE(menu->findChild<QAction*>("fileDuplicate")->text(),
               chinese ? QStringLiteral("创建副本") : QStringLiteral("Duplicate"));
      QCOMPARE(menu->findChild<QAction*>("fileNewFolder")->text(),
               chinese ? QStringLiteral("新建文件夹") : QStringLiteral("New Folder"));
      QCOMPARE(menu->findChild<QAction*>("fileCopyRelativePath")->text(),
               chinese ? QStringLiteral("复制相对路径") : QStringLiteral("Copy Relative Path"));
      if (!output.isEmpty()) {
        window.resize(1120, 760);
        menu->popup(tree->mapToGlobal(QPoint(30, 50)));
        QTest::qWait(60);
        QVERIFY(window.grab().save(output + QStringLiteral("/window-") + language +
                                   QStringLiteral(".png")));
        QVERIFY(menu->grab().save(output + QStringLiteral("/menu-") + language +
                                  QStringLiteral(".png")));
        menu->hide();
      }
      QTimer safety;
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
          modal->reject();
      });
      safety.start(3000);
      bool prompted = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget())) {
          prompted = true;
          auto* buttons = dialog->findChild<QDialogButtonBox*>(QStringLiteral("fileNameButtons"));
          QVERIFY(buttons);
          auto* accept = dialog->findChild<QPushButton*>(QStringLiteral("fileNameAccept"));
          auto* cancel = dialog->findChild<QPushButton*>(QStringLiteral("fileNameCancel"));
          QVERIFY(accept);
          QVERIFY(cancel);
          QCOMPARE(accept->text(), chinese ? QStringLiteral("确定") : QStringLiteral("OK"));
          QCOMPARE(cancel->text(), chinese ? QStringLiteral("取消") : QStringLiteral("Cancel"));
          auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameInput"));
          QVERIFY(input);
          input->setText(language + QStringLiteral("-notes"));
          if (!output.isEmpty())
            QVERIFY(dialog->grab().save(output + QStringLiteral("/folder-dialog-") + language +
                                        QStringLiteral(".png")));
          accept->click();
        }
      });
      qInfo("File actions: %s create folder", qPrintable(language));
      menu->findChild<QAction*>("fileNewFolder")->trigger();
      QVERIFY(prompted);
      QVERIFY(QFileInfo(directory.filePath(language + QStringLiteral("-notes"))).isDir());
      menu->findChild<QAction*>("fileCopyPath")->trigger();
      QCOMPARE(QFileInfo(QApplication::clipboard()->text()).canonicalFilePath(),
               QFileInfo(path).canonicalFilePath());
      qInfo("File actions: %s duplicate", qPrintable(language));
      menu->findChild<QAction*>("fileDuplicate")->trigger();
      QFile copy(tree->selectedPath());
      QVERIFY(copy.open(QIODevice::ReadOnly));
      QCOMPARE(copy.readAll(), original);
      QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
      QCOMPARE(editor->document()->availableUndoSteps(), steps);
      QCOMPARE(editor->document()->baseUrl(), base);
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), original);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
  }

  void workspaceSearchUsesRootAndProtectsDraftBeforeSourceJump() {
    QTemporaryDir directory;
    const auto original = directory.filePath(QStringLiteral("notes/original.md"));
    const auto target = directory.filePath(QStringLiteral("guides/target.md"));
    QDir().mkpath(QFileInfo(original).absolutePath());
    QDir().mkpath(QFileInfo(target).absolutePath());
    for (const auto& path : {original, target}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write(path == original ? "Original paragraph.\n"
                                  : "# Target\n\nNeedle first.\n\nneedle second.\n");
    }
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.show();
    QVERIFY(window.openFile(original));
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    auto* fileTree = window.findChild<typist::ui::FileTree*>();
    fileTree->setRoot(directory.path());
    auto* panel = window.findChild<typist::ui::WorkspaceSearchPanel*>();
    auto* query = panel->findChild<QLineEdit*>(QStringLiteral("workspaceSearchQuery"));
    auto* tree = panel->findChild<QTreeWidget*>(QStringLiteral("workspaceSearchResults"));
    auto* status = panel->findChild<QLabel*>(QStringLiteral("workspaceSearchStatus"));
    actionBySource(window, QStringLiteral("Search in Folder"))->trigger();
    QCOMPARE(panel->rootPath(), directory.path());
    query->setText(QStringLiteral("needle"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 2")));
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 2);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("Unsaved **search draft**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchRegex"))->setChecked(true);
    panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchWords"))->setChecked(true);
    query->setText(QStringLiteral("["));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Invalid regular expression.")));
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **search draft**."));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    query->setText(QStringLiteral("needle"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines: 2")));
    QTimer safety;
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
    });
    safety.start(3000);
    for (bool discard : {false, true}) {
      bool prompted = false;
      QTimer::singleShot(0, &window, [&] {
        if (auto* prompt = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
          prompted = true;
          prompt->button(discard ? QMessageBox::Discard : QMessageBox::Cancel)->click();
        }
      });
      tree->setCurrentItem(tree->topLevelItem(0)->child(1));
      QTest::keyClick(tree, Qt::Key_Return);
      QVERIFY(prompted);
      QCoreApplication::processEvents();
      if (!discard) {
        QVERIFY(editor->paragraphSourceActive());
        QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **search draft**."));
        QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
        QCOMPARE(editor->document()->availableUndoSteps(), steps);
      }
    }
    QCOMPARE(editor->textCursor().blockNumber(), 4);
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("needle"));
    QCOMPARE(fileTree->rootPath(), directory.path());
    QFile unchanged(original);
    QVERIFY(unchanged.open(QIODevice::ReadOnly));
    QCOMPARE(unchanged.readAll(), QByteArray("Original paragraph.\n"));
    QFile changed(target);
    QVERIFY(changed.open(QIODevice::WriteOnly));
    changed.write("# Target\n\nNeedle first.\n\nChanged result.\n");
    changed.close();
    QTest::keyClick(tree, Qt::Key_Return);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Search result changed")));
    const auto loaded = editor->toPlainText();
    QVERIFY(QFile::remove(target));
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(editor->toPlainText(), loaded);
    QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Cannot open")));
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }

  void workspaceRegexSelectsActualSpanAndZeroWidth() {
    QTemporaryDir directory;
    const auto target = directory.filePath(QStringLiteral("note.md"));
    const auto text = QStringLiteral("# Heading\r\n\r\n😀 Note42 note7\r\n\r\n");
    QFile file(target);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(text.toUtf8());
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.openFile(target));
    actionBySource(window, QStringLiteral("Search in Folder"))->trigger();
    auto* panel = window.findChild<typist::ui::WorkspaceSearchPanel*>();
    auto* query = panel->findChild<QLineEdit*>(QStringLiteral("workspaceSearchQuery"));
    auto* results = panel->findChild<QTreeWidget*>(QStringLiteral("workspaceSearchResults"));
    auto* status = panel->findChild<QLabel*>(QStringLiteral("workspaceSearchStatus"));
    panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchRegex"))->setChecked(true);
    panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchCase"))->setChecked(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    for (const auto& pattern :
         {QStringLiteral("note\\d+"), QStringLiteral("(?=Note)"), QStringLiteral("^$")}) {
      query->setText(pattern);
      QTest::keyClick(query, Qt::Key_Return);
      QTRY_VERIFY(status->text().startsWith(QStringLiteral("Matching lines:")));
      QCOMPARE(results->topLevelItemCount(), 1);
      results->setCurrentItem(results->topLevelItem(0)->child(0));
      QTest::keyClick(results, Qt::Key_Return);
      const auto cursor = editor->textCursor();
      if (pattern == QLatin1String("note\\d+")) {
        QCOMPARE(cursor.blockNumber(), 2);
        QCOMPARE(cursor.selectedText(), QStringLiteral("note7"));
        QCOMPARE(cursor.selectionStart() - cursor.block().position(), 10);
      } else if (pattern == QLatin1String("(?=Note)")) {
        QCOMPARE(cursor.blockNumber(), 2);
        QCOMPARE(cursor.positionInBlock(), 3);
        QVERIFY(!cursor.hasSelection());
      } else {
        QCOMPARE(cursor.blockNumber(), 1);
        QCOMPARE(cursor.positionInBlock(), 0);
        QVERIFY(!cursor.hasSelection());
      }
    }
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), text.toUtf8());
  }

  void workspaceSearchBilingualThemesAndSavedScope() {
    QTemporaryDir directory;
    const auto target = directory.filePath(QStringLiteral("写作计划.md"));
    QFile file(target);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("# Writing\n\n计划：整理资料\n\n计划：检查排版\n").toUtf8());
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    window.openFile(target);
    actionBySource(window, QStringLiteral("Search in Folder"))->trigger();
    auto* panel = window.findChild<typist::ui::WorkspaceSearchPanel*>();
    auto* query = panel->findChild<QLineEdit*>(QStringLiteral("workspaceSearchQuery"));
    auto* results = panel->findChild<QTreeWidget*>(QStringLiteral("workspaceSearchResults"));
    auto* regex = panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchRegex"));
    auto* words = panel->findChild<QToolButton*>(QStringLiteral("workspaceSearchWords"));
    regex->setChecked(true);
    words->setChecked(true);
    query->setText(QStringLiteral("计划：.+"));
    QTest::keyClick(query, Qt::Key_Return);
    QTRY_COMPARE(results->topLevelItemCount(), 1);
    QCOMPARE(results->topLevelItem(0)->childCount(), 2);
    const auto output = qEnvironmentVariable("TYPIST_WORKSPACE_SEARCH_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      QCoreApplication::processEvents();
      QCOMPARE(query->placeholderText(), language == QLatin1String("en")
                                             ? QStringLiteral("Search in workspace…")
                                             : QStringLiteral("在工作区中搜索…"));
      QCOMPARE(panel->findChild<QPushButton*>(QStringLiteral("workspaceSearchStart"))->text(),
               language == QLatin1String("en") ? QStringLiteral("Search") : QStringLiteral("搜索"));
      QCOMPARE(window.findChild<QTabWidget*>(QStringLiteral("sidebarTabs"))->tabText(2),
               language == QLatin1String("en") ? QStringLiteral("Search") : QStringLiteral("搜索"));
      QCOMPARE(regex->accessibleName(), language == QLatin1String("en")
                                            ? QStringLiteral("Regular expression")
                                            : QStringLiteral("正则表达式"));
      QCOMPARE(words->accessibleName(), language == QLatin1String("en")
                                            ? QStringLiteral("Match whole word")
                                            : QStringLiteral("全字匹配"));
      QVERIFY(regex->isChecked());
      QVERIFY(words->isChecked());
      if (!output.isEmpty()) {
        window.resize(1120, 760);
        QTest::qWait(60);
        QVERIFY(window.grab().save(output + QStringLiteral("/search-") + language +
                                   QStringLiteral(".png")));
      }
      auto* status = panel->findChild<QLabel*>(QStringLiteral("workspaceSearchStatus"));
      query->setText(QStringLiteral("["));
      QTest::keyClick(query, Qt::Key_Return);
      const auto invalid = language == QLatin1String("en")
                               ? QStringLiteral("Invalid regular expression. Check the pattern.")
                               : QStringLiteral("正则表达式无效，请检查表达式。");
      QTRY_COMPARE(status->text(), invalid);
      if (!output.isEmpty())
        QVERIFY(window.grab().save(output + QStringLiteral("/invalid-") + language +
                                   QStringLiteral(".png")));
      query->setText(QStringLiteral("计划：.+"));
      QTest::keyClick(query, Qt::Key_Return);
      QTRY_COMPARE(results->topLevelItemCount(), 1);
    }
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
  }

  void folderSwitchAndQuickOpenKeepDocumentAndDraft() {
    FolderHistoryGuard history;
    QTemporaryDir directory;
    const auto a = directory.filePath(QStringLiteral("Original workspace"));
    const auto b = directory.filePath(QStringLiteral("Other workspace"));
    const auto c = directory.filePath(QStringLiteral("Pinned workspace"));
    for (const auto& folder : {a, b, c}) QVERIFY(QDir().mkpath(folder));
    const auto input = QDir(a).filePath(QStringLiteral("original.md"));
    QFile file(input);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Original paragraph.\n");
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.show();
    QVERIFY(window.openFile(input));
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    const auto originalBase = editor->document()->baseUrl();
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("Persistent **workspace draft**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    QVERIFY(window.openFolderPath(b));
    QCoreApplication::processEvents();
    auto* tree = window.findChild<typist::ui::FileTree*>();
    QCOMPARE(tree->rootPath(), QFileInfo(b).canonicalFilePath());
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Persistent **workspace draft**."));
    QCOMPARE(editor->document()->baseUrl(), originalBase);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    typist::platform::FolderLocationsStore store;
    QVERIFY(store.setPinned(c, true));
    actionBySource(window, QStringLiteral("Search in Folder"))->trigger();
    bool picked = false;
    QTimer safety;
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget())) modal->reject();
    });
    safety.start(3000);
    QTimer::singleShot(0, &window, [&] {
      if (auto* picker =
              qobject_cast<typist::ui::QuickOpenDialog*>(QApplication::activeModalWidget())) {
        picked = true;
        auto* query = picker->findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
        query->setText(QStringLiteral("Pinned workspace"));
        QTest::keyClick(query, Qt::Key_Return);
      }
    });
    window.findChild<QAction*>(QStringLiteral("openQuickly"))->trigger();
    QVERIFY(picked);
    QCoreApplication::processEvents();
    QCOMPARE(tree->rootPath(), QFileInfo(c).canonicalFilePath());
    QCOMPARE(window.findChild<typist::ui::WorkspaceSearchPanel*>()->rootPath(), tree->rootPath());
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Persistent **workspace draft**."));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    QVERIFY(!window.openFolderPath(directory.filePath(QStringLiteral("missing"))));
    QCOMPARE(tree->rootPath(), QFileInfo(c).canonicalFilePath());
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("Original paragraph.\n"));
    file.close();
    // Normal focus-out editing must be restored after leaving the search page via Ctrl+P.
    QVERIFY(editor->setParagraphSourceFocusCommitEnabled(true));
    window.activateWindow();
    draft->setFocus();
    QTRY_VERIFY(draft->hasFocus());
    tree->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(tree->hasFocus());
    QTRY_VERIFY(!editor->paragraphSourceActive());
    QVERIFY(editor->toPlainText().contains(QStringLiteral("workspace draft")));
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }

  void folderLocationsBilingualThemeMenus() {
    FolderHistoryGuard history;
    QTemporaryDir directory;
    const auto a = directory.filePath(QStringLiteral("Writing workspace"));
    const auto b = directory.filePath(QStringLiteral("中文资料"));
    QVERIFY(QDir().mkpath(a));
    QVERIFY(QDir().mkpath(b));
    typist::platform::FolderLocationsStore store;
    store.record(a);
    store.record(b);
    QVERIFY(store.setPinned(a, true));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.openFolderPath(a));
    auto* button = window.findChild<QToolButton*>(QStringLiteral("folderLocations"));
    auto* menu = window.findChild<typist::ui::FolderLocationsMenu*>();
    QVERIFY(button && menu);
    const auto output = qEnvironmentVariable("TYPIST_FOLDER_LOCATIONS_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      QCoreApplication::processEvents();
      menu->refresh();
      QCOMPARE(button->toolTip(), language == QLatin1String("en")
                                      ? QStringLiteral("Recent and Pinned Folders")
                                      : QStringLiteral("最近与固定文件夹"));
      QCOMPARE(menu->findChild<QAction*>(QStringLiteral("pinCurrentFolder"))->text(),
               language == QLatin1String("en") ? QStringLiteral("Unpin Current Folder")
                                               : QStringLiteral("取消固定当前文件夹"));
      if (!output.isEmpty()) {
        window.resize(1120, 760);
        QTest::qWait(50);
        QVERIFY(window.grab().save(output + QStringLiteral("/workspace-") + language +
                                   QStringLiteral(".png")));
        menu->popup(button->mapToGlobal(QPoint(0, button->height())));
        QTest::qWait(50);
        QVERIFY(menu->grab().save(output + QStringLiteral("/locations-") + language +
                                  QStringLiteral(".png")));
        menu->close();
        typist::ui::QuickOpenDialog picker(a, {}, &window, store.read());
        picker.show();
        QTest::qWait(60);
        QVERIFY(picker.grab().save(output + QStringLiteral("/picker-") + language +
                                   QStringLiteral(".png")));
      }
    }
    menu->refresh();
    menu->findChild<QAction*>(QStringLiteral("folderLocation_1"))->trigger();
    QCOMPARE(window.findChild<typist::ui::FileTree*>()->rootPath(),
             QFileInfo(b).canonicalFilePath());
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
  }

  void quickOpenBilingualThemes() {
    QTemporaryDir directory;
    for (const auto& name : {QStringLiteral("Writing guide.md"), QStringLiteral("写作计划.md"),
                             QStringLiteral("Reference.md")}) {
      QFile file(directory.filePath(name));
      QVERIFY(file.open(QIODevice::WriteOnly));
      file.write("# Note\n");
    }
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    const auto output = qEnvironmentVariable("TYPIST_QUICK_OPEN_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      typist::ui::QuickOpenDialog dialog(
          directory.path(), {directory.filePath(QStringLiteral("写作计划.md"))}, &window);
      dialog.show();
      auto* status = dialog.findChild<QLabel*>(QStringLiteral("quickOpenStatus"));
      QTRY_VERIFY(status->text().contains(QStringLiteral("3")));
      QCOMPARE(dialog.windowTitle(), language == QLatin1String("en")
                                         ? QStringLiteral("Open Quickly")
                                         : QStringLiteral("快速打开"));
      QCOMPARE(dialog.palette().color(QPalette::Window).lightness() < 128,
               language == QLatin1String("zh"));
      auto* query = dialog.findChild<QLineEdit*>(QStringLiteral("quickOpenQuery"));
      QCOMPARE(query->placeholderText(), language == QLatin1String("en")
                                             ? QStringLiteral("Search files or folders…")
                                             : QStringLiteral("搜索文件或文件夹…"));
      if (!output.isEmpty())
        QVERIFY(dialog.grab().save(output + QStringLiteral("/quick-open-") + language +
                                   QStringLiteral(".png")));
    }
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
  }

  void windowLanguageThemeAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.show();
    QTest::qWait(30);
    QVERIFY(window.width() >= 760);
    QVERIFY(window.height() >= 540);
    QVERIFY(!window.windowIcon().isNull());
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QTextCursor cursor = editor->textCursor();
    cursor.insertText(QStringLiteral("中文 and English"));
    const QString before = editor->toPlainText();
    QVERIFY(editor->document()->isUndoAvailable());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(window.menuBar()->actions().first()->text(), QStringLiteral("文件"));
    QCOMPARE(editor->toPlainText(), before);
    QVERIFY(editor->document()->isUndoAvailable());
    QCOMPARE(typist::platform::languagePreference(), QStringLiteral("zh"));
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QVERIFY(window.menuBar()->palette().color(QPalette::WindowText).lightness() > 160);
    QVERIFY(window.menuBar()->palette().color(QPalette::ButtonText).lightness() > 160);
    QVERIFY(window.styleSheet().contains(QStringLiteral("QMenuBar::item:selected")));
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QCOMPARE(window.menuBar()->actions().first()->text(), QStringLiteral("File"));
    editor->undo();
    QCOMPARE(editor->toPlainText(), QString());
  }

  void typographyDoesNotChangeMarkdownSemantics() {
    const QString source = QStringLiteral(
        "# A **bold** heading\n\nA paragraph.\n\n"
        "## A second heading\n\n- plain item\n- **bold** item\n\n"
        "| Plain | **Bold** |\n|---|---|\n| one | two |\n");
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
    const auto original = typist::core::parseMarkdown(source.toStdString());
    const auto saved =
        typist::core::parseMarkdown(typist::editor::serializeDocument(&document).toStdString());
    QVERIFY(typist::core::astEqual(original.blocks, saved.blocks));
    QTextBlock heading = document.begin();
    QCOMPARE(heading.blockFormat().headingLevel(), 1);
    QVERIFY(heading.begin().fragment().charFormat().fontPointSize() >= 24);
    QVERIFY(heading.next().blockFormat().bottomMargin() >= 10);
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next())
      if (block.textList()) QCOMPARE(block.blockFormat().headingLevel(), 0);
    QCOMPARE(typist::ui::outlineFromDocument(&document).size(), 2);
  }

  void wideWindowOpensReadmeAndResizes() {
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1280, 900);
    window.show();
    QTest::qWait(30);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QTest::qWait(50);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    QVERIFY(editor && editor->toPlainText().startsWith(QStringLiteral("Typist")));
    const QString original = typist::editor::serializeDocument(editor->document());
    QVERIFY(window.findChildren<QToolBar*>().isEmpty());
    auto* sidebarButton = window.findChild<QToolButton*>(QStringLiteral("sidebarToggle"));
    auto* sourceButton = window.findChild<QToolButton*>(QStringLiteral("sourceToggle"));
    auto* tabs = window.findChild<QTabWidget*>(QStringLiteral("sidebarTabs"));
    QVERIFY(sidebarButton && sourceButton && tabs);
    QTest::mouseClick(sidebarButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(tabs->isVisible());
    tabs->setCurrentIndex(1);
    for (const QSize size :
         {QSize(960, 700), QSize(1440, 900), QSize(800, 650), QSize(1180, 820)}) {
      window.resize(size);
      QTest::qWait(20);
      editor->repaint();
      QVERIFY2(editor->viewport()->width() >= qMin(800, size.width() - 320),
               qPrintable(QStringLiteral("window=%1 editor=%2 viewport=%3")
                              .arg(window.width())
                              .arg(editor->width())
                              .arg(editor->viewport()->width())));
    }
    editor->verticalScrollBar()->setValue(editor->verticalScrollBar()->maximum());
    QTest::qWait(20);
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QTest::mouseClick(sourceButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(editor->toPlainText().startsWith(QStringLiteral("# Typist")));
    QTest::mouseClick(sourceButton, Qt::LeftButton);
    QTest::qWait(20);
    QVERIFY(editor->toPlainText().startsWith(QStringLiteral("Typist")));
  }

  void writingAreaWidthsApplyIndependentlyAndPersist() {
    using Width = typist::platform::WritingWidth;
    const auto key = QStringLiteral("editor/writingAreaWidths");
    QSettings settings;
    const auto prior = settings.value(key);
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      if (prior.isValid())
        settings.setValue(key, prior);
      else
        settings.remove(key);
    });
    settings.setValue(key, QVariantMap{{QStringLiteral("rendered"), QStringLiteral("invalid")},
                                       {QStringLiteral("source"), 8000}});
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QCOMPARE(typist::platform::writingAreaWidths().rendered, Width::Standard);
    QCOMPARE(typist::platform::writingAreaWidths().source, Width::Standard);

    typist::MainWindow other;
    other.setBenchMode(true);
    other.resize(1350, 700);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1350, 700);
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* action = window.findChild<QAction*>(QStringLiteral("writingAreaWidth"));
    auto* sourceToggle = window.findChild<QToolButton*>(QStringLiteral("sourceToggle"));
    QVERIFY(editor && action && sourceToggle);
    QTRY_VERIFY(editor->viewport()->width() > 800);
    QCOMPARE(action->text(), QStringLiteral("Writing Area Width"));
    const auto configure = [&](Width rendered, Width source, bool accept, const QString& title) {
      bool shown = false;
      QTimer::singleShot(0, &window, [&] {
        auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        auto* r = dialog->findChild<QComboBox*>(QStringLiteral("renderedWritingWidth"));
        auto* s = dialog->findChild<QComboBox*>(QStringLiteral("sourceWritingWidth"));
        if (r && s && dialog->windowTitle() == title) {
          r->setCurrentIndex(r->findData(int(rendered)));
          s->setCurrentIndex(s->findData(int(source)));
          shown = r->currentIndex() >= 0 && s->currentIndex() >= 0;
        }
        if (accept && shown)
          dialog->accept();
        else
          dialog->reject();
      });
      action->trigger();
      return shown;
    };

    QTest::keyClicks(editor, "Paragraph with a draft.");
    auto selection = editor->textCursor();
    selection.movePosition(QTextCursor::StartOfBlock, QTextCursor::KeepAnchor);
    editor->setTextCursor(selection);
    const auto original = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    QVERIFY(configure(Width::Compact, Width::Wide, true, QStringLiteral("Writing Area Width")));
    QTRY_VERIFY(editor->viewport()->width() > 600 && editor->viewport()->width() <= 640);
    QCOMPARE(editor->textCursor().position(), selection.position());
    QCOMPARE(editor->textCursor().anchor(), selection.anchor());
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    other.show();
    auto* otherEditor = other.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(otherEditor);
    QTRY_VERIFY(otherEditor->viewport()->width() > 600 && otherEditor->viewport()->width() <= 640);
    window.raise();

    selection.clearSelection();
    editor->setTextCursor(selection);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Unsaved **draft**."));
    QVERIFY(configure(Width::Fill, Width::Compact, false, QStringLiteral("Writing Area Width")));
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **draft**."));
    QCOMPARE(typist::platform::writingAreaWidths().rendered, Width::Compact);
    QCOMPARE(typist::platform::writingAreaWidths().source, Width::Wide);
    QVERIFY(configure(Width::Wide, Width::Wide, true, QStringLiteral("Writing Area Width")));
    QTRY_VERIFY(editor->viewport()->width() > 1080 && editor->viewport()->width() <= 1120);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Unsaved **draft**."));
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    editor->finishParagraphSourceEdit(false);

    QTest::mouseClick(sourceToggle, Qt::LeftButton);
    QTRY_VERIFY(editor->viewport()->width() > 1080 && editor->viewport()->width() <= 1120);
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(action->text(), QStringLiteral("写作区宽度"));
    QVERIFY(configure(Width::Compact, Width::Fill, true, QStringLiteral("写作区宽度")));
    QTRY_VERIFY(editor->viewport()->width() > 1180);
    window.resize(710, 600);
    QTRY_VERIFY(window.width() < 1000);
    QTRY_VERIFY(editor->viewport()->width() <= editor->width() &&
                editor->viewport()->width() >= editor->width() - 80);

    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    reopened.resize(1350, 700);
    reopened.show();
    auto* reopenedEditor = reopened.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(reopenedEditor);
    QTRY_VERIFY(reopenedEditor->viewport()->width() > 600 &&
                reopenedEditor->viewport()->width() <= 640);
    QVERIFY(QMetaObject::invokeMethod(&reopened, "toggleSourceView", Qt::DirectConnection));
    QTRY_VERIFY(reopenedEditor->viewport()->width() > 1180);
  }

  void writingAreaReflowKeepsTheReadingPosition() {
    using Width = typist::platform::WritingWidth;
    typist::ui::Editor editor;
    editor.resize(1200, 560);
    QString text;
    for (int i = 0; i < 80; ++i)
      text += QString::number(i) + QStringLiteral(" a long line of text").repeated(14) + '\n';
    editor.setPlainText(text);
    editor.show();
    QTRY_VERIFY(editor.verticalScrollBar()->maximum() > 0);
    auto* scroll = editor.verticalScrollBar();
    scroll->setValue(scroll->maximum() / 2);
    const auto anchor = editor.cursorForPosition(QPoint(0, 0));
    const int oldY = editor.cursorRect(anchor).top();
    QVERIFY(anchor.blockNumber() > 8);
    QVERIFY(editor.cursorRect().bottom() < 0);
    editor.setWritingAreaWidths({Width::Compact, Width::Wide});
    QTRY_VERIFY(qAbs(editor.cursorRect(anchor).top() - oldY) <= editor.fontMetrics().height());
    QVERIFY(editor.cursorRect().bottom() < 0);
    QCOMPARE(editor.toPlainText(), text);
    QVERIFY(!editor.document()->isUndoAvailable());
  }

  void editingActionsReplaceAndOutline() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1100, 800);
    window.show();
    auto* editor = static_cast<typist::ui::Editor*>(
        window.findChild<QTextEdit*>(QStringLiteral("writingEditor")));
    QVERIFY(editor);
    window.activateWindow();
    editor->setFocus();
    QTest::qWait(30);
    QTest::keyClicks(editor, "## Title");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "cat cat");
    QTest::keyClick(editor, Qt::Key_H, Qt::ControlModifier);
    auto* pattern = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    auto* replacement = window.findChild<QLineEdit*>(QStringLiteral("replaceText"));
    QVERIFY(pattern && replacement && replacement->isVisible());
    pattern->setText(QStringLiteral("cat"));
    replacement->setText(QStringLiteral("dog"));
    window.findChild<QPushButton*>(QStringLiteral("replaceAll"))->click();
    QVERIFY(editor->toPlainText().contains(QStringLiteral("dog dog")));
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("typewriterMode"))->trigger();
    QVERIFY(editor->focusMode());
    QVERIFY(editor->typewriterMode());
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("typewriterMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("heading_2"))->text(),
             QStringLiteral("2 级标题"));
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("format_bold"))->text(),
             QStringLiteral("加粗"));
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QCOMPARE(editor->toPlainText(), QString());
    QVERIFY(!editor->document()->isUndoAvailable());
  }

  void outlineFiltersHierarchyAndNavigates() {
    QSettings settings(QStringLiteral("Typist"), QStringLiteral("Typist"));
    const auto key = QStringLiteral("outline/collapsible");
    const auto prior = settings.value(key);
    const auto restore = qScopeGuard([&] {
      if (prior.isValid())
        settings.setValue(key, prior);
      else
        settings.remove(key);
      settings.sync();
    });
    settings.remove(key);
    settings.sync();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("outline.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(QStringLiteral("Preface\n\n# Plan\n\n### 第一阶段\n\n#### **Review** today\n\n"
                              "## Archive\n\n# Other\n\n## Review today\n\nMore notes.\n")
                   .toUtf8());
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1100, 760);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("outlineHeadings"));
    auto* filter = window.findChild<QLineEdit*>(QStringLiteral("outlineFilter"));
    auto* flat = window.findChild<QAction*>(QStringLiteral("outlineFlat"));
    auto* hierarchy = window.findChild<QAction*>(QStringLiteral("outlineCollapsible"));
    auto* reveal = window.findChild<QAction*>(QStringLiteral("outlineReveal"));
    QVERIFY(editor && tree && filter && flat && hierarchy && reveal);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleOutline", Qt::DirectConnection));
    QTRY_VERIFY(tree->isVisible());
    QVERIFY(flat->isChecked());
    QCOMPARE(tree->topLevelItemCount(), 6);
    hierarchy->trigger();
    QCOMPARE(tree->topLevelItemCount(), 2);
    QCOMPARE(tree->topLevelItem(0)->child(0)->text(0), QStringLiteral("第一阶段"));
    QCOMPARE(tree->topLevelItem(0)->child(0)->child(0)->text(0), QStringLiteral("Review today"));
    tree->topLevelItem(0)->setExpanded(false);

    filter->setText(QStringLiteral("rEvIeW"));
    QVERIFY(tree->topLevelItem(0)->isExpanded());
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
    QTest::keyClick(filter, Qt::Key_Return);
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 4);
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("Review today"));
    const auto second = tree->topLevelItem(1)->child(0);
    tree->scrollToItem(second);
    QTRY_VERIFY(!tree->visualItemRect(second).isEmpty());
    QTest::mouseClick(tree->viewport(), Qt::LeftButton, Qt::NoModifier,
                      tree->visualItemRect(second).center());
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 2);
    QTest::keyClick(filter, Qt::Key_Down);
    QCOMPARE(tree->currentItem(), tree->topLevelItem(0)->child(0)->child(0));
    QTest::keyClick(tree, Qt::Key_Return);
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 4);
    filter->clear();
    QVERIFY(!tree->topLevelItem(0)->isExpanded());
    QVERIFY(tree->topLevelItem(0)->font(0).bold());
    const int position = editor->textCursor().position();
    reveal->trigger();
    QVERIFY(tree->topLevelItem(0)->isExpanded());
    QCOMPARE(tree->currentItem(), tree->topLevelItem(0)->child(0)->child(0));
    QCOMPARE(editor->textCursor().position(), position);
    const auto output = qEnvironmentVariable("TYPIST_OUTLINE_OUTPUT");
    if (!output.isEmpty()) {
      QTest::qWait(60);
      QVERIFY(window.grab().save(output + QStringLiteral("/outline-hierarchy.png")));
    }

    tree->topLevelItem(0)->setExpanded(false);
    editor->moveCursor(QTextCursor::Start);
    editor->insertPlainText(QStringLiteral("Updated "));
    editor->setTextCursor(editor->document()->find(QStringLiteral("today")));
    editor->insertPlainText(QStringLiteral("draft"));
    QTRY_COMPARE(tree->topLevelItem(0)->child(0)->child(0)->text(0),
                 QStringLiteral("Review draft"));
    QVERIFY(!tree->topLevelItem(0)->isExpanded());
    flat->trigger();
    QCOMPARE(tree->topLevelItemCount(), 6);
    hierarchy->trigger();
    QVERIFY(!tree->topLevelItem(0)->isExpanded());
    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    QVERIFY(reopened.findChild<QAction*>(QStringLiteral("outlineCollapsible"))->isChecked());
  }

  void outlineFollowsScrollingAndKeepsEditsThroughLanguageChanges() {
    const auto language = typist::platform::languagePreference();
    const auto theme = typist::platform::themePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      typist::platform::setThemePreference(theme);
    });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("chapters.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# First chapter\n\nA short introduction.\n\n# Last chapter\n\n");
    for (int i = 1; i <= 70; ++i)
      file.write(QStringLiteral("Paragraph %1 of the final section.\n\n").arg(i).toUtf8());
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1050, 660);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("outlineHeadings"));
    auto* filter = window.findChild<QLineEdit*>(QStringLiteral("outlineFilter"));
    auto* empty = window.findChild<QLabel*>(QStringLiteral("outlineEmpty"));
    auto* reveal = window.findChild<QAction*>(QStringLiteral("outlineReveal"));
    QVERIFY(editor && tree && filter && empty && reveal);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleOutline", Qt::DirectConnection));
    auto* scrollbar = editor->verticalScrollBar();
    QTRY_VERIFY(scrollbar->maximum() > 0);
    editor->setTextCursor(editor->document()->find(QStringLiteral("short introduction")));
    editor->insertPlainText(QStringLiteral("revised introduction"));
    QTRY_VERIFY(tree->topLevelItem(0)->font(0).bold());
    const int position = editor->textCursor().position();
    const auto markdown = typist::editor::serializeDocument(editor->document());
    const auto undo = editor->document()->availableUndoSteps();
    scrollbar->setValue(scrollbar->maximum());
    QTRY_VERIFY(tree->topLevelItem(1)->font(0).bold());
    QVERIFY(!tree->topLevelItem(0)->font(0).bold());
    QCOMPARE(editor->textCursor().position(), position);
    filter->setText(QStringLiteral("no result"));
    QVERIFY(empty->isVisible());
    QCOMPARE(empty->text(), QStringLiteral("No matching headings"));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTRY_COMPARE(empty->text(), QStringLiteral("没有匹配的标题"));
    QCOMPARE(filter->placeholderText(), QStringLiteral("筛选标题…"));
    QCOMPARE(reveal->text(), QStringLiteral("定位当前标题"));
    reveal->trigger();
    QCOMPARE(filter->text(), QString());
    QCOMPARE(tree->currentItem(), tree->topLevelItem(1));
    QCOMPARE(editor->textCursor().position(), position);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), markdown);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);

    const auto output = qEnvironmentVariable("TYPIST_OUTLINE_OUTPUT");
    if (!output.isEmpty()) {
      QTest::qWait(60);
      QVERIFY(window.grab().save(output + QStringLiteral("/outline-zh-dark.png")));
      window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
      window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
      filter->setText(QStringLiteral("chapter"));
      QTest::qWait(60);
      QVERIFY(window.grab().save(output + QStringLiteral("/outline-en-light.png")));
      filter->clear();
      window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    }

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!filter->isEnabled());
    QVERIFY(empty->isVisible());
    QCOMPARE(empty->text(), QStringLiteral("请切换到渲染视图使用大纲"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral("\n\n## Newly added heading\n"));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(filter->isEnabled());
    filter->setText(QStringLiteral("newly added"));
    QVERIFY(tree->isVisible());
    QTest::keyClick(filter, Qt::Key_Return);
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("Newly added heading"));
  }

  void codeToolsTargetTheClickedFenceAndSelectAllStaysInTheEditor() {
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] { typist::platform::setLanguagePreference(language); });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("code-tools.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Before\n\n```cpp\nfirst\n```\n\nMiddle\n\n```cpp\n\tother \n```\n\nTail.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1040, 640);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* selectAll = window.findChild<QAction*>(QStringLiteral("selectAll"));
    QVERIFY(editor && selectAll);
    const auto original = typist::editor::serializeDocument(editor->document());
    // Deliver synthetic shortcuts to the Qt window even if the OS has another foreground app.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&window);
    QT_WARNING_POP
    editor->setTextCursor(editor->document()->find(QStringLiteral("first")));
    auto caret = editor->textCursor();
    caret.clearSelection();
    editor->setTextCursor(caret);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    selectAll->trigger();
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("first"));
    QTest::keyClick(editor, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().selectionStart(), 0);
    QCOMPARE(editor->textCursor().selectionEnd(), editor->document()->characterCount() - 1);

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));
    auto* find = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    QVERIFY(find);
    find->setText(QStringLiteral("find this"));
    find->setCursorPosition(find->text().size());
    QTRY_VERIFY(find->hasFocus());
    QTest::keyClick(find, Qt::Key_A, Qt::ControlModifier);
    QCOMPARE(find->selectedText(), find->text());
    QCOMPARE(editor->textCursor().selectionStart(), 0);
    QCOMPARE(editor->textCursor().selectionEnd(), editor->document()->characterCount() - 1);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));

    editor->setTextCursor(caret);
    const auto other = editor->document()->find(QStringLiteral("other"));
    QVERIFY(!other.isNull());
    const auto activateTool = [&](const QString& id, const QString& expectedTitle) {
      const QPoint point = editor->cursorRect(other).center();
      if (!editor->viewport()->rect().contains(point)) return false;
      bool activated = false;
      QTimer::singleShot(0, &window, [&] {
        auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
        if (!popup) return;
        auto* tools = popup->findChild<QMenu*>(QStringLiteral("codeToolsContextMenu"));
        if (tools && tools->title() == expectedTitle) {
          auto* action = tools->findChild<QAction*>(id);
          if (action && action->isEnabled()) {
            action->trigger();
            activated = true;
          }
        }
        popup->close();
      });
      QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                              editor->viewport()->mapToGlobal(point));
      QApplication::sendEvent(editor->viewport(), &event);
      return activated;
    };
    QVERIFY(activateTool(QStringLiteral("codeContextCopy"), QStringLiteral("Code Tools")));
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("\tother \n"));
    QCOMPARE(editor->textCursor().position(), caret.position());
    QVERIFY(activateTool(QStringLiteral("codeContextSelect"), QStringLiteral("Code Tools")));
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("\tother "));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QVERIFY(activateTool(QStringLiteral("codeContextExit"), QStringLiteral("代码工具")));
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("Tail."));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    QVERIFY(activateTool(QStringLiteral("codeContextAutoIndent"), QStringLiteral("代码工具")));
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("other "));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
  }

  void codeAutoIndentMenuAndBacktabPreference() {
    QSettings settings;
    const auto key = QStringLiteral("editor/autoIndentOnBacktab");
    const auto prior = settings.value(key);
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      if (prior.isValid())
        settings.setValue(key, prior);
      else
        settings.remove(key);
    });
    settings.remove(key);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("indent.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("```json\n{\n\"items\": []\n}\n```\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* setting = window.findChild<QAction*>(QStringLiteral("autoIndentOnBacktab"));
    auto* indent = window.findChild<QAction*>(QStringLiteral("autoIndentCode"));
    QVERIFY(editor && setting && indent);
    editor->setCodeIndentSize(2);
    QCOMPARE(indent->text(), QStringLiteral("Auto Indent Code"));
    QVERIFY(!setting->isChecked());
    const auto item = editor->document()->find(QStringLiteral("items")).block();
    editor->setTextCursor(QTextCursor(item));
    indent->trigger();
    QCOMPARE(item.text(), QStringLiteral("  \"items\": []"));
    QTest::keyClick(editor, Qt::Key_Backtab);
    QCOMPARE(item.text(), QStringLiteral("\"items\": []"));
    setting->trigger();
    QVERIFY(settings.value(key).toBool());
    QTest::keyClick(editor, Qt::Key_Backtab);
    QCOMPARE(item.text(), QStringLiteral("  \"items\": []"));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(indent->text(), QStringLiteral("自动缩进代码"));
    QCOMPARE(setting->text(), QStringLiteral("Shift+Tab 自动缩进代码"));
    typist::MainWindow reopened;
    auto* persisted = reopened.findChild<QAction*>(QStringLiteral("autoIndentOnBacktab"));
    QVERIFY(persisted && persisted->isChecked());
  }

  void codeIndentSizeSettingAppliesImmediatelyAndPersists() {
    QSettings settings;
    const auto key = QStringLiteral("editor/codeIndentSize");
    const auto prior = settings.value(key);
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      if (prior.isValid())
        settings.setValue(key, prior);
      else
        settings.remove(key);
    });
    settings.remove(key);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* setting = window.findChild<QAction*>(QStringLiteral("codeIndentSize"));
    auto* fence = window.findChild<QAction*>(QStringLiteral("paragraph_code"));
    QVERIFY(editor && setting && fence);
    QCOMPARE(editor->codeIndentSize(), 4);
    QCOMPARE(setting->text(), QStringLiteral("Code Indent Size"));
    const auto configure = [&](int size, bool accept) {
      bool shown = false;
      QTimer::singleShot(0, &window, [&] {
        auto* modal = QApplication::activeModalWidget();
        if (auto* dialog = qobject_cast<QInputDialog*>(modal)) {
          shown = true;
          dialog->setIntValue(size);
          if (accept)
            dialog->accept();
          else
            dialog->reject();
        } else if (modal) {
          modal->close();
        }
      });
      setting->trigger();
      return shown;
    };
    QVERIFY(configure(2, true));
    QCOMPARE(editor->codeIndentSize(), 2);
    QCOMPARE(settings.value(key).toInt(), 2);
    fence->trigger();
    QTest::keyClick(editor, Qt::Key_Tab);
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("  "));
    QVERIFY(configure(8, false));
    QCOMPARE(editor->codeIndentSize(), 2);
    QCOMPARE(settings.value(key).toInt(), 2);

    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    auto* reopenedEditor = reopened.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(reopenedEditor);
    QCOMPARE(reopenedEditor->codeIndentSize(), 2);
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(setting->text(), QStringLiteral("代码缩进宽度"));
  }

  void defaultCodeLanguageSettingsRememberModeAndExplicitLanguage() {
    QSettings settings(QStringLiteral("Typist"), QStringLiteral("Typist"));
    const auto key = QStringLiteral("editor/codeFencePreferences");
    const auto lastKey = QStringLiteral("editor/lastCodeLanguage");
    const auto prior = settings.value(key);
    const auto priorLast = settings.value(lastKey);
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      for (const auto& pair : {qMakePair(key, prior), qMakePair(lastKey, priorLast)}) {
        if (pair.second.isValid())
          settings.setValue(pair.first, pair.second);
        else
          settings.remove(pair.first);
      }
    });
    settings.remove(key);
    settings.remove(lastKey);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* action = window.findChild<QAction*>(QStringLiteral("defaultCodeLanguage"));
    auto* code = window.findChild<QAction*>(QStringLiteral("paragraph_code"));
    auto* fresh = window.findChild<QAction*>(QStringLiteral("newDocument"));
    QVERIFY(editor && action && code && fresh);
    QCOMPARE(action->text(), QStringLiteral("Default Code Language"));
    const auto configure = [&](bool useLast, bool forCommands, bool forTyped, bool accept) {
      bool seen = false;
      QTimer::singleShot(0, &window, [&] {
        auto* dialog = window.findChild<QDialog*>(QStringLiteral("codeFenceSettings"));
        if (!dialog) {
          if (auto* modal = QApplication::activeModalWidget()) modal->close();
          return;
        }
        const auto close = qScopeGuard([&] { accept ? dialog->accept() : dialog->reject(); });
        auto* input = dialog->findChild<QComboBox*>(QStringLiteral("codeFenceLanguage"));
        auto* last = dialog->findChild<QCheckBox*>(QStringLiteral("codeFenceUseLast"));
        auto* commands = dialog->findChild<QCheckBox*>(QStringLiteral("codeFenceApplyCommands"));
        auto* typed = dialog->findChild<QCheckBox*>(QStringLiteral("codeFenceApplyTyped"));
        if (!input || !last || !commands || !typed) return;
        input->setEditText(QStringLiteral("python"));
        last->setChecked(useLast);
        commands->setChecked(forCommands);
        typed->setChecked(forTyped);
        seen = input->isEnabled() == !useLast;
      });
      action->trigger();
      return seen;
    };
    QVERIFY(configure(false, true, false, true));
    code->trigger();
    QCOMPARE(
        editor->textCursor().blockFormat().property(typist::editor::kCodeInfoProperty).toString(),
        QStringLiteral("python"));
    QVERIFY(typist::platform::lastCodeLanguage().isEmpty());
    fresh->trigger();
    QTest::keyClicks(editor, "```");
    QTest::keyClick(editor, Qt::Key_Return);
    QVERIFY(editor->textCursor().blockFormat().hasProperty(typist::editor::kCodeBlockProperty));
    QVERIFY(editor->textCursor()
                .blockFormat()
                .property(typist::editor::kCodeInfoProperty)
                .toString()
                .isEmpty());
    QVERIFY(editor->setCodeLanguage(QStringLiteral("rust")));
    QCOMPARE(typist::platform::lastCodeLanguage(), QStringLiteral("rust"));

    QVERIFY(configure(true, false, true, false));
    QVERIFY(!typist::platform::codeFencePreferences().useLastLanguage);
    QVERIFY(configure(true, false, true, true));
    QVERIFY(typist::platform::codeFencePreferences().useLastLanguage);
    QVERIFY(!typist::platform::codeFencePreferences().applyToCommands);
    fresh->trigger();
    code->trigger();
    QVERIFY(editor->textCursor()
                .blockFormat()
                .property(typist::editor::kCodeInfoProperty)
                .toString()
                .isEmpty());
    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    auto* reopenedEditor = reopened.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(reopenedEditor);
    QTest::keyClicks(reopenedEditor, "```");
    QTest::keyClick(reopenedEditor, Qt::Key_Return);
    QCOMPARE(reopenedEditor->textCursor()
                 .blockFormat()
                 .property(typist::editor::kCodeInfoProperty)
                 .toString(),
             QStringLiteral("rust"));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(action->text(), QStringLiteral("默认代码语言"));
  }

  void codeLineNumberSettingPersistsAndTranslates() {
    const auto language = typist::platform::languagePreference();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QSettings settings;
    const auto key = QStringLiteral("editor/codeLineNumbers");
    const auto prior = settings.value(key);
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      if (prior.isValid())
        settings.setValue(key, prior);
      else
        settings.remove(key);
    });
    settings.remove(key);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* action = window.findChild<QAction*>(QStringLiteral("codeLineNumbers"));
    QVERIFY(editor && action);
    QCOMPARE(action->text(), QStringLiteral("Code Block Line Numbers"));
    QVERIFY(action->isCheckable() && !action->isChecked());
    action->trigger();
    QVERIFY(settings.value(key).toBool());
    auto* gutter = editor->findChild<QWidget*>(QStringLiteral("codeLineNumberGutter"));
    QVERIFY(gutter && gutter->isVisible());

    typist::MainWindow reopened;
    reopened.setBenchMode(true);
    auto* reopenedAction = reopened.findChild<QAction*>(QStringLiteral("codeLineNumbers"));
    QVERIFY(reopenedAction && reopenedAction->isChecked());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(action->text(), QStringLiteral("代码块行号"));
    action->trigger();
    QVERIFY(!settings.value(key).toBool());
    QVERIFY(gutter->isHidden());
  }

  void underlineShortcutWorksAcrossEditorViews() {
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] { typist::platform::setLanguagePreference(language); });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("underline.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Some text to underline.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1040, 640);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* underline = window.findChild<QAction*>(QStringLiteral("format_underline"));
    QVERIFY(editor && underline);
    QCOMPARE(underline->text(), QStringLiteral("Underline"));
    // Set the Qt activation state for synthetic keys when the OS keeps another
    // process in the foreground.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&window);
    QT_WARNING_POP
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    editor->setTextCursor(editor->document()->find(QStringLiteral("Some text")));
    QTest::keyClick(editor, Qt::Key_U, Qt::ControlModifier);
    QCOMPARE(typist::editor::inlineMarkdown(editor->textCursor().block()),
             QStringLiteral("<u>Some text</u> to underline."));
    editor->copySelection(typist::ui::Editor::CopyFormat::WithoutTheme);
    QVERIFY(
        QApplication::clipboard()->mimeData()->html().contains(QStringLiteral("<u>Some text</u>")));

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->setTextCursor(editor->document()->find(QStringLiteral("to underline")));
    const auto source = editor->toPlainText();
    QTest::keyClick(editor, Qt::Key_U, Qt::ControlModifier);
    QVERIFY(editor->toPlainText().contains(QStringLiteral("<u>to underline</u>")));
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("to underline"));
    QTest::keyClick(editor, Qt::Key_U, Qt::ControlModifier);
    QCOMPARE(editor->toPlainText(), source);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));

    editor->moveCursor(QTextCursor::Start);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    const auto originalDraft = draft->toPlainText();
    draft->setTextCursor(draft->document()->find(QStringLiteral("to underline")));
    QTRY_VERIFY(draft->hasFocus());
    QTest::keyClick(draft, Qt::Key_U, Qt::ControlModifier);
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(draft->toPlainText().contains(QStringLiteral("<u>to underline</u>")));
    QVERIFY(QMetaObject::invokeMethod(&window, "undo", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), originalDraft);
    editor->finishParagraphSourceEdit(false);
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(underline->text(), QStringLiteral("下划线"));
  }

  void headingShortcutsUpdateTheOutlineAndRespectEditorMode() {
    const auto language = typist::platform::languagePreference();
    QSettings settings(QStringLiteral("Typist"), QStringLiteral("Typist"));
    const auto outlineKey = QStringLiteral("outline/collapsible");
    const auto outlinePreference = settings.value(outlineKey);
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      if (outlinePreference.isValid())
        settings.setValue(outlineKey, outlinePreference);
      else
        settings.remove(outlineKey);
    });
    settings.setValue(outlineKey, true);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("headings.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# Parent\n\n## Child\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1040, 640);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* tree = window.findChild<QTreeWidget*>(QStringLiteral("outlineHeadings"));
    auto* promote = window.findChild<QAction*>(QStringLiteral("promoteHeading"));
    auto* demote = window.findChild<QAction*>(QStringLiteral("demoteHeading"));
    QVERIFY(editor && tree && promote && demote);
    QCOMPARE(promote->text(), QStringLiteral("Increase Heading Level"));
    QCOMPARE(demote->text(), QStringLiteral("Decrease Heading Level"));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleOutline", Qt::DirectConnection));
    QTRY_VERIFY(tree->isVisible());
    QCOMPARE(tree->topLevelItemCount(), 1);
    QCOMPARE(tree->topLevelItem(0)->childCount(), 1);
    // QTest sends keys in-process. Set the Qt activation state even when the OS
    // leaves another process in the foreground.
    QT_WARNING_PUSH
    QT_WARNING_DISABLE_DEPRECATED
    QApplication::setActiveWindow(&window);
    QT_WARNING_POP
    editor->setTextCursor(editor->document()->find(QStringLiteral("Child")));
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Equal, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 1);
    QTRY_COMPARE(tree->topLevelItemCount(), 2);
    QTest::keyClick(editor, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 2);
    QTRY_COMPARE(tree->topLevelItemCount(), 1);

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));
    auto* find = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    QVERIFY(find);
    QTRY_VERIFY(find->hasFocus());
    QTest::keyClick(find, Qt::Key_Equal, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().blockFormat().headingLevel(), 2);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));

    auto cursor = editor->textCursor();
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft && !promote->isEnabled() && !demote->isEnabled());
    draft->setPlainText(QStringLiteral("### Unapplied edit"));
    QTest::keyClick(draft, Qt::Key_Equal, Qt::ControlModifier);
    QCOMPARE(draft->toPlainText(), QStringLiteral("### Unapplied edit"));
    QVERIFY(editor->paragraphSourceActive());
    editor->finishParagraphSourceEdit(false);
    QVERIFY(promote->isEnabled() && demote->isEnabled());

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!promote->isEnabled() && !demote->isEnabled());
    const auto source = editor->toPlainText();
    QTest::keyClick(editor, Qt::Key_Minus, Qt::ControlModifier);
    QCOMPARE(editor->toPlainText(), source);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(promote->isEnabled() && demote->isEnabled());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(promote->text(), QStringLiteral("提升标题级别"));
    QCOMPARE(demote->text(), QStringLiteral("降低标题级别"));
  }

  void rangeShortcutsNavigateAndKeepParagraphDraft() {
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] { typist::platform::setLanguagePreference(language); });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("ranges.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Start **bold phrase**. Another sentence.\n\n");
    for (int i = 0; i < 70; ++i) file.write("Scrolling content.\n\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1040, 640);
    window.show();
    window.activateWindow();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* selectMenu = window.findChild<QMenu*>(QStringLiteral("selectRangeMenu"));
    auto* jumpMenu = window.findChild<QMenu*>(QStringLiteral("jumpMenu"));
    QVERIFY(editor && selectMenu && jumpMenu);
    QCOMPARE(selectMenu->title(), QStringLiteral("Select Range"));
    QCOMPARE(jumpMenu->title(), QStringLiteral("Jump"));
    const auto trigger = [&](const char* name) {
      auto* action = window.findChild<QAction*>(QLatin1String(name));
      if (action) action->trigger();
      return action != nullptr;
    };
    auto cursor = editor->document()->find(QStringLiteral("bold"));
    QVERIFY(!cursor.isNull());
    cursor.setPosition(cursor.selectionStart() + 1);
    editor->setTextCursor(cursor);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("bold"));
    QTest::keyClick(editor, Qt::Key_E, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("bold phrase"));
    QTest::keyClick(editor, Qt::Key_L, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().selectedText(), QStringLiteral("Start bold phrase."));

    const auto selected = editor->textCursor();
    cursor.setPosition(selected.selectionEnd());
    cursor.setPosition(selected.selectionStart(), QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    auto* scroll = editor->verticalScrollBar();
    QTRY_VERIFY(scroll->maximum() > 0);
    scroll->setValue(scroll->maximum());
    QTest::keyClick(editor, Qt::Key_J, Qt::ControlModifier);
    QTRY_VERIFY(scroll->value() < scroll->maximum());
    QCOMPARE(editor->textCursor().anchor(), cursor.anchor());
    QCOMPARE(editor->textCursor().position(), cursor.position());
    QVERIFY(trigger("jumpLineEnd"));
    QVERIFY(editor->textCursor().atBlockEnd());
    QVERIFY(!editor->textCursor().hasSelection());
    QVERIFY(trigger("jumpLineStart"));
    QVERIFY(editor->textCursor().atBlockStart());
    QVERIFY(trigger("jumpBottom"));
    QVERIFY(editor->textCursor().atEnd());
    QVERIFY(trigger("jumpTop"));
    QVERIFY(editor->textCursor().atStart());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));
    auto* find = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    QVERIFY(find && find->isVisible());
    find->setFocus();
    QTRY_VERIFY(find->hasFocus());
    QTest::keyClick(find, Qt::Key_L, Qt::ControlModifier);
    QVERIFY(find->hasFocus());
    QVERIFY(!editor->textCursor().hasSelection());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));

    const auto original = typist::editor::serializeDocument(editor->document());
    const auto undo = editor->document()->availableUndoSteps();
    editor->setTextCursor(editor->document()->find(QStringLiteral("Start")));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Draft **one**.\n第二行内容。"));
    draft->moveCursor(QTextCursor::End);
    QTRY_VERIFY(draft->hasFocus());
    QTest::keyClick(draft, Qt::Key_L, Qt::ControlModifier);
    QCOMPARE(draft->textCursor().selectedText(), QStringLiteral("第二行内容。"));
    QVERIFY(trigger("jumpTop"));
    QVERIFY(draft->textCursor().atStart());
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Draft **one**.\n第二行内容。"));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    QVERIFY(trigger("language_zh"));
    QCOMPARE(selectMenu->title(), QStringLiteral("选择范围"));
    QCOMPARE(jumpMenu->title(), QStringLiteral("跳转"));
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("selectSentence"))->text(),
             QStringLiteral("选择行／句子"));
    editor->finishParagraphSourceEdit(false);
  }

  void deleteRangeShortcutUsesTheActiveEditorAndKeepsTheDraftOpen() {
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] { typist::platform::setLanguagePreference(language); });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("deletion.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("First sentence. Delete this sentence! Final sentence.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1040, 640);
    window.show();
    window.activateWindow();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* menu = window.findChild<QMenu*>(QStringLiteral("deleteRangeMenu"));
    auto* sentence = window.findChild<QAction*>(QStringLiteral("deleteSentence"));
    QVERIFY(editor && menu && sentence);
    QCOMPARE(menu->title(), QStringLiteral("Delete Range"));
    const auto original = typist::editor::serializeDocument(editor->document());
    auto cursor = editor->document()->find(QStringLiteral("Delete"));
    QVERIFY(!cursor.isNull());
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    editor->setFocus();
    QTRY_VERIFY(editor->hasFocus());
    QTest::keyClick(editor, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(editor->toPlainText().contains(QStringLiteral("First sentence. Final sentence.")));
    QVERIFY(QMetaObject::invokeMethod(&window, "undo", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));
    auto* find = window.findChild<QLineEdit*>(QStringLiteral("findPattern"));
    QVERIFY(find && find->isVisible());
    find->setFocus();
    QTRY_VERIFY(find->hasFocus());
    QTest::keyClick(find, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(find->hasFocus());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleFindBar", Qt::DirectConnection));

    editor->setTextCursor(editor->document()->find(QStringLiteral("First")));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Keep **draft**.\n删除此行\nEnd."));
    draft->setTextCursor(draft->document()->find(QStringLiteral("删除")));
    draft->moveCursor(QTextCursor::EndOfBlock);
    QTRY_VERIFY(draft->hasFocus());
    QTest::keyClick(draft, Qt::Key_Backspace, Qt::ControlModifier | Qt::ShiftModifier);
    QCOMPARE(draft->toPlainText(), QStringLiteral("Keep **draft**.\nEnd."));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(QMetaObject::invokeMethod(&window, "undo", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Keep **draft**.\n删除此行\nEnd."));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(menu->title(), QStringLiteral("删除范围"));
    QCOMPARE(sentence->text(), QStringLiteral("删除行／句子"));
    editor->finishParagraphSourceEdit(false);
  }

  void clipboardFormatsUseTheSelectionAcrossViewsAndLanguages() {
    const auto language = typist::platform::languagePreference();
    const auto theme = typist::platform::themePreference();
    const auto restore = qScopeGuard([&] {
      typist::platform::setLanguagePreference(language);
      typist::platform::setThemePreference(theme);
    });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("clipboard.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(
        "# Clipboard\n\nA **bold** [link](notes/guide.md).\n\n"
        "```cpp\nint x = 3 < 4;\n```\n\nOutside selection.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1080, 720);
    window.show();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* htmlCode = window.findChild<QAction*>(QStringLiteral("copyHtmlCode"));
    auto* semantic = window.findChild<QAction*>(QStringLiteral("copyWithoutTheme"));
    auto* plain = window.findChild<QAction*>(QStringLiteral("copyPlainText"));
    auto* markdown = window.findChild<QAction*>(QStringLiteral("copyMarkdown"));
    QVERIFY(editor && htmlCode && semantic && plain && markdown);
    auto* clipboard = QApplication::clipboard();
    clipboard->setText(QStringLiteral("Existing clipboard"));
    htmlCode->trigger();
    semantic->trigger();
    plain->trigger();
    QCOMPARE(clipboard->text(), QStringLiteral("Existing clipboard"));
    QTextCursor range(editor->document());
    const int end = editor->document()->find(QStringLiteral("Outside selection")).selectionStart();
    QVERIFY(end > 0);
    range.setPosition(end, QTextCursor::KeepAnchor);
    editor->setTextCursor(range);
    const auto original = typist::editor::serializeDocument(editor->document());
    const int undo = editor->document()->availableUndoSteps();
    htmlCode->trigger();
    const auto html = clipboard->text();
    QVERIFY(html.contains(QStringLiteral("<h1>Clipboard</h1>")));
    QVERIFY(html.contains(QStringLiteral("<strong>bold</strong>")));
    QVERIFY(html.contains(QStringLiteral("href=\"notes/guide.md\"")));
    QVERIFY(html.contains(QStringLiteral("class=\"language-cpp\"")));
    QVERIFY(html.contains(QStringLiteral("3 &lt; 4")));
    QVERIFY(!html.contains(QStringLiteral("Outside selection")));
    QVERIFY(!clipboard->mimeData()->hasHtml());
    semantic->trigger();
    QCOMPARE(clipboard->mimeData()->html(), html);
    QVERIFY(clipboard->text().contains(QStringLiteral("A bold link.")));
    QVERIFY(!html.contains(QStringLiteral("style=")));
    plain->trigger();
    QVERIFY(clipboard->text().contains(QStringLiteral("int x = 3 < 4;")));
    QVERIFY(!clipboard->mimeData()->hasHtml());
    markdown->trigger();
    QVERIFY(clipboard->text().contains(QStringLiteral("**bold**")));
    QCOMPARE(editor->textCursor().selectionEnd(), end);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);

    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QCOMPARE(htmlCode->text(), QStringLiteral("复制为 HTML 源码"));
    QCOMPARE(semantic->text(), QStringLiteral("复制无主题样式"));
    QCOMPARE(plain->text(), QStringLiteral("复制为纯文本"));
    editor->setTextCursor(editor->document()->find(QStringLiteral("old")));
    editor->ensureCursorVisible();
    bool activated = false;
    QTimer::singleShot(0, &window, [&] {
      auto* popup = qobject_cast<QMenu*>(QApplication::activePopupWidget());
      if (!popup) return;
      auto* copyAs = popup->findChild<QMenu*>(QStringLiteral("copyAsContextMenu"));
      if (copyAs && copyAs->title() == QStringLiteral("复制为")) {
        for (auto* action : copyAs->actions()) {
          if (action->text() == QStringLiteral("复制为 HTML 源码")) {
            action->trigger();
            activated = true;
          }
        }
      }
      popup->close();
    });
    const QPoint point = editor->cursorRect().center();
    QContextMenuEvent event(QContextMenuEvent::Mouse, point,
                            editor->viewport()->mapToGlobal(point));
    QApplication::sendEvent(editor->viewport(), &event);
    QVERIFY(activated);
    QVERIFY(clipboard->text().contains(QStringLiteral("<strong>old</strong>")));
    QVERIFY(!clipboard->text().contains(QStringLiteral("bold")));

    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->setTextCursor(editor->document()->find(QStringLiteral("**bold**")));
    plain->trigger();
    QCOMPARE(clipboard->text(), QStringLiteral("**bold**"));
    htmlCode->trigger();
    QVERIFY(clipboard->text().contains(QStringLiteral("<strong>bold</strong>")));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->setTextCursor(editor->document()->find(QStringLiteral("bold")));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Draft **待确认**"));
    draft->selectAll();
    semantic->trigger();
    QVERIFY(clipboard->mimeData()->html().contains(QStringLiteral("<strong>待确认</strong>")));
    QCOMPARE(clipboard->text(), QStringLiteral("Draft **待确认**"));
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    editor->finishParagraphSourceEdit(false);
  }

  void cancelAndDiscardNeverOverwriteDocument() {
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/original.md");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("original\n");
    file.close();
    typist::MainWindow window;
    window.show();
    window.openFile(path);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral(" edited"));
    bool cancelClicked = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (dialog) {
        cancelClicked = true;
        dialog->button(QMessageBox::Cancel)->click();
      }
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(cancelClicked);
    QVERIFY(editor->toPlainText().contains(QStringLiteral("edited")));
    bool discardClicked = false;
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (dialog) {
        discardClicked = true;
        dialog->button(QMessageBox::Discard)->click();
      }
    });
    window.close();
    QVERIFY(discardClicked);
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArrayLiteral("original\n"));
    file.close();
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }

  void reopeningCurrentFileFlushesBeforeReading() {
    QTemporaryDir directory;
    const QString path = directory.path() + QStringLiteral("/reopen.md");
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("before\n");
    file.close();
    typist::MainWindow window;
    window.show();
    window.openFile(path);
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(true);
    auto* editor = window.findChild<QTextEdit*>(QStringLiteral("writingEditor"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(QStringLiteral(" after"));
    window.openFile(path);
    QCOMPARE(editor->toPlainText(), QStringLiteral("before after"));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("before after"));
    window.close();
  }

  void docxHasValidRunsAndReadableStyles() {
    const QString path = m_settings.path() + QStringLiteral("/layout.docx");
    QString error;
    QVERIFY(typist::platform::exportDocx(
        path,
        QStringLiteral("# A *styled* heading\n\n[Link](https://example.com)\n\n"
                       "- [x] Done\n\n> A **quote**\n\n```cpp\nfirst\nsecond\n```\n"),
        &error));
    QFile file(path);
    QVERIFY(file.open(QIODevice::ReadOnly));
    const QByteArray zip = file.readAll();
    const int start = zip.indexOf("<w:document ");
    const int end = zip.indexOf("</w:document>", start);
    QVERIFY(start > 0 && end > start);
    QXmlStreamReader xml(zip.mid(start, end + 13 - start));
    QStringList stack;
    bool sawBreak = false;
    while (!xml.atEnd()) {
      xml.readNext();
      if (xml.isStartElement()) {
        if (xml.name() == QStringLiteral("br")) {
          QCOMPARE(stack.last(), QStringLiteral("r"));
          sawBreak = true;
        }
        stack.append(xml.name().toString());
      } else if (xml.isEndElement()) {
        stack.removeLast();
      } else if (xml.isCharacters() && !xml.isWhitespace()) {
        QCOMPARE(stack.last(), QStringLiteral("t"));
      }
    }
    QVERIFY2(!xml.hasError(), qPrintable(xml.errorString()));
    QVERIFY(sawBreak);
    QVERIFY(zip.contains("w:eastAsia=\"Microsoft YaHei\""));
    QVERIFY(zip.contains("w:line=\"360\""));
    QVERIFY(zip.contains("<w:keepNext/>"));
    QVERIFY(zip.contains("<w:widowControl/>"));
    QVERIFY(!zip.contains("&lt;w:r"));
  }

  void editingFeaturesVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_VISUAL_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_VISUAL_OUTPUT to retain UI specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("zh"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    QSettings().setValue(QStringLiteral("editor/focusMode"), false);
    QSettings().setValue(QStringLiteral("editor/typewriterMode"), false);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.show();
    window.activateWindow();
    auto* editor = static_cast<typist::ui::Editor*>(
        window.findChild<QTextEdit*>(QStringLiteral("writingEditor")));
    editor->setFocus();
    QTest::qWait(20);
    QTest::keyClicks(editor, "# Writing that works");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Write **bold ideas**, *quiet details*, and `inline code` directly.");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "## One continuous workflow");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "- [x] Markdown renders while you type");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Tables, replacements and undo work together");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "> Keep your focus on the next sentence.");
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClick(editor, Qt::Key_Return);
    QTest::keyClicks(editor, "Search a word, replace it, or enter focus mode with F8.");
    editor->moveCursor(QTextCursor::Start);
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-light-zh.png")));
    auto* paragraphMenu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("heading_2"))->parent());
    QVERIFY(paragraphMenu);
    paragraphMenu->ensurePolished();
    paragraphMenu->adjustSize();
    QVERIFY(paragraphMenu->grab().save(out + QStringLiteral("/paragraph-menu-zh.png")));
    window.findChild<QAction*>(QStringLiteral("replace"))->trigger();
    window.findChild<QLineEdit*>(QStringLiteral("findPattern"))->setText(QStringLiteral("word"));
    window.findChild<QLineEdit*>(QStringLiteral("replaceText"))->setText(QStringLiteral("idea"));
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-replace-zh.png")));
    window.findChild<typist::ui::FindBar*>()->closeBar();
    window.findChild<QAction*>(QStringLiteral("focusMode"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    editor->setFocus();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/editing-focus-dark.png")));
    window.findChild<QAction*>(QStringLiteral("focusMode"))->setChecked(false);
  }

  void exportVisualFixtures() {
    const QString out = qEnvironmentVariable("TYPIST_VISUAL_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_VISUAL_OUTPUT to retain export specimens.");
    QDir().mkpath(out);
    const QString source = QStringLiteral(
        "# A better place to write\n\nTypist · 专注写作，让想法自然成形。\n\n"
        "## Thoughtful typography / 清晰的排版\n\n"
        "Readable paragraphs, **strong ideas**, *small details*, and `inline code` make a document "
        "easy to follow. "
        "中文和 English 在同一段落中保持清晰、舒适的阅读节奏。\n\n"
        "- A comfortable line length and generous spacing\n"
        "- 中文界面与英文界面即时切换，保留正在编辑的内容\n"
        "- Export the same document to PDF and Word\n\n"
        "> Good writing starts with a little space to think.\n\n"
        "### A small checklist\n\n- [x] Find your focus\n- [ ] Write the next paragraph\n\n"
        "| Feature | Experience |\n| --- | --- |\n| Writing | Clean, comfortable and focused |\n"
        "| 中文支持 | 简体中文界面与中英混排 |\n| Export | PDF and DOCX with clear hierarchy |\n\n"
        "```cpp\n// Start with one idea.\n\nint main() {\n  return 0;\n}\n```\n");
    QFile input(out + QStringLiteral("/writing-sample.md"));
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write(source.toUtf8());
    input.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 820);
    window.openFile(input.fileName());
    window.show();
    QTest::qWait(50);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-dark-zh.png")));
    window.findChild<QToolButton*>(QStringLiteral("sidebarToggle"))->click();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-sidebar-dark.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/interface-sidebar-light.png")));
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QTest::qWait(40);
    QVERIFY(window.grab().save(out + QStringLiteral("/readme-wide-light.png")));
    QTextDocument document;
    QString error;
    QVERIFY(typist::editor::loadMarkdown(source, &document, &error));
    typist::platform::PdfPageOptions options;
    options.header = QStringLiteral("Typist / Writing sample");
    options.footer = QStringLiteral("#");
    QVERIFY2(typist::platform::exportPdf(out + QStringLiteral("/writing-sample.pdf"), &document,
                                         options, &error),
             qPrintable(error));
    QVERIFY2(
        typist::platform::exportDocx(out + QStringLiteral("/writing-sample.docx"), source, &error),
        qPrintable(error));
    QFile readme(QStringLiteral(TYPIST_SOURCE_DIR "/README.md"));
    QVERIFY(readme.open(QIODevice::ReadOnly));
    const QString longSource = QString::fromUtf8(readme.readAll());
    QVERIFY(typist::editor::loadMarkdown(longSource, &document, &error));
    QVERIFY(typist::platform::exportPdf(out + QStringLiteral("/readme.pdf"), &document, options,
                                        &error));
    QVERIFY(typist::platform::exportDocx(out + QStringLiteral("/readme.docx"), longSource, &error));
  }
  void advancedContentVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_ADVANCED_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_ADVANCED_OUTPUT to retain advanced UI specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 1000);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/advanced.md"));
    window.show();
    QTest::qWait(40);
    QVERIFY(window.grab().save(out + QStringLiteral("/advanced-window-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/advanced-window-dark-zh.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTimer::singleShot(10, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      if (dialog) {
        dialog->grab().save(out + QStringLiteral("/advanced-math-dialog-zh.png"));
        dialog->reject();
      }
    });
    window.findChild<QAction*>(QStringLiteral("insert_math"))->trigger();
  }

  void paragraphSourceSaveAutosaveAndViewSwitch() {
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("paragraph.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Original **word**.\n\nTail.\n");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.show();
    QTest::qWait(10);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(editor->startParagraphSourceEdit());
    editor->finishParagraphSourceEdit(false);
    QVERIFY(!window.windowTitle().contains(QLatin1Char('*')));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(input);
    auto* title = editor->findChild<QLabel*>(QStringLiteral("paragraphSourceTitle"));
    QVERIFY(title);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(5);
    QCOMPARE(title->text(), QStringLiteral("Paragraph Markdown"));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(5);
    QCOMPARE(title->text(), QStringLiteral("段落 Markdown"));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(!window.windowTitle().contains(QLatin1Char('*')));
    input->setPlainText(QStringLiteral("Saved **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(!editor->document()->isUndoAvailable());
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Original **word**")));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("Saved **snapshot**"));
    file.close();
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(true);
    input->setPlainText(QStringLiteral("Autosaved **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "autoSaveNow", Qt::DirectConnection));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QVERIFY(file.readAll().contains("Autosaved **snapshot**"));
    file.close();
    input->setPlainText(QStringLiteral("Final **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(editor->toPlainText().contains(QStringLiteral("Final **snapshot**")));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Final **snapshot**")));
  }
  void paragraphDraftSurvivesCancelledNavigation() {
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("protected.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Protected paragraph.\n");
    file.close();
    typist::MainWindow window;
    window.openFile(path);
    window.show();
    QTest::qWait(10);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* input = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    input->setPlainText(QStringLiteral("Pending **draft**."));
    QTimer::singleShot(10, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->button(QMessageBox::Cancel)->click();
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(input->toPlainText(), QStringLiteral("Pending **draft**."));
    QTimer::singleShot(10, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      if (box) box->button(QMessageBox::Discard)->click();
    });
    window.findChild<QAction*>(QStringLiteral("newDocument"))->trigger();
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(editor->toPlainText().isEmpty());
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("Protected paragraph.\n"));
    file.close();
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void syntaxAndParagraphVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_SYNTAX_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_SYNTAX_OUTPUT to retain code and paragraph specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    QSettings().setValue(QStringLiteral("editor/syntaxHighlight"), true);
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 950);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/syntax.md"));
    window.show();
    QTest::qWait(40);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(window.grab().save(out + QStringLiteral("/syntax-window-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/syntax-window-dark-zh.png")));
    editor->setTextCursor(QTextCursor(editor->document()->findBlockByNumber(1)));
    QVERIFY(editor->startParagraphSourceEdit());
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/paragraph-source-dark-zh.png")));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    QTest::qWait(20);
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(window.grab().save(out + QStringLiteral("/paragraph-source-light-zh.png")));
    editor->finishParagraphSourceEdit(false);
  }
  void codeCommandsPasteLanguageAndSave() {
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QTemporaryDir directory;
    const QString path = directory.filePath(QStringLiteral("code.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("```cpp\nint value;\n```\n\nBody.\n");
    file.close();
    typist::MainWindow window;
    window.openFile(path);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->setFocus();
    editor->moveCursor(QTextCursor::Start);
    QTest::qWait(20);
    window.findChild<QAction*>(QStringLiteral("copyCode"))->trigger();
    QCOMPARE(QApplication::clipboard()->text(), QStringLiteral("int value;\n"));
    QVERIFY(!editor->document()->isUndoAvailable());
    auto* combo = editor->findChild<QComboBox*>(QStringLiteral("codeBlockLanguage"));
    QVERIFY(combo && combo->isVisible());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(10);
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("copyCodeBlock"))->text(),
             QStringLiteral("已复制"));
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("exitCodeBlock"))->text(),
             QStringLiteral("退出代码块"));
    combo->setFocus();
    combo->lineEdit()->selectAll();
    QTest::keyClicks(combo->lineEdit(), "python");
    QTest::keyClick(combo->lineEdit(), Qt::Key_Return);
    QCOMPARE(editor->document()->blockCount(), 2);
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .startsWith("```python\nint value;\n```"));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(10);
    QCOMPARE(editor->findChild<QPushButton*>(QStringLiteral("exitCodeBlock"))->text(),
             QStringLiteral("Exit code"));
    QApplication::clipboard()->setText(QStringLiteral("# Heading\n**literal**\n"));
    auto* paste = actionBySource(window, QStringLiteral("Paste Markdown"));
    QVERIFY(paste);
    paste->trigger();
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains("# Heading\n**literal**\nint value;"));
    for (auto block = editor->document()->begin(); block.next().isValid(); block = block.next())
      QCOMPARE(block.blockFormat().headingLevel(), 0);
    const auto snapshot = typist::editor::serializeDocument(editor->document());
    const auto save = actionBySource(window, QStringLiteral("Save"));
    QVERIFY(save);
    save->trigger();
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), snapshot.toUtf8());
    file.close();
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("Body."));
    QVERIFY(!editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), snapshot);
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void codeToolbarVisuals() {
    const QString out = qEnvironmentVariable("TYPIST_CODE_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_CODE_OUTPUT to retain code toolbar specimens.");
    QDir().mkpath(out);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 850);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/code-writing.md"));
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto block = editor->document()->begin();
    while (block.isValid() && !block.blockFormat().hasProperty(typist::editor::kCodeBlockProperty))
      block = block.next();
    QVERIFY(block.isValid());
    editor->setTextCursor(QTextCursor(block));
    editor->setFocus();
    QTest::qWait(30);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-dark-zh.png")));
    window.resize(800, 650);
    QTest::qWait(20);
    QVERIFY(window.grab().save(out + QStringLiteral("/code-toolbar-compact-zh.png")));
  }
  void linksNavigateFilesWithSaveProtectionAndHeadingJump() {
    QTemporaryDir directory;
    const auto original = directory.filePath("original.md");
    const auto target = directory.filePath(QStringLiteral("中文 note.md"));
    QFile file(original);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# Original\n\nKeep this.\n");
    file.close();
    QFile next(target);
    QVERIFY(next.open(QIODevice::WriteOnly));
    next.write(QStringLiteral("# Next file\n\n## 中文 章节\n\nDestination.\n").toUtf8());
    next.close();
    typist::MainWindow window;
    window.openFile(original);
    window.findChild<QAction*>(QStringLiteral("autoSave"))->setChecked(false);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->moveCursor(QTextCursor::End);
    editor->insertPlainText(" unsaved");
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto undo = editor->document()->availableUndoSteps();
    const auto caret = editor->textCursor();
    bool cancelled = false;
    QTimer::singleShot(0, &window, [&] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      cancelled = true;
      box->button(QMessageBox::Cancel)->click();
    });
    window.followLink(QStringLiteral("中文%20note.md#中文-章节"));
    QVERIFY(cancelled);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    QCOMPARE(editor->textCursor().position(), caret.position());
    QCOMPARE(editor->textCursor().anchor(), caret.anchor());
    QTimer::singleShot(0, &window, [] {
      auto* box = qobject_cast<QMessageBox*>(QApplication::activeModalWidget());
      QVERIFY(box);
      box->button(QMessageBox::Discard)->click();
    });
    window.followLink(QStringLiteral("中文%20note.md#中文-章节"));
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("中文 章节"));
    QVERIFY(editor->toPlainText().contains("Destination."));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), QByteArray("# Original\n\nKeep this.\n"));
    file.close();
    window.followLink("#missing");
    QVERIFY(window.statusBar()->currentMessage().contains("missing"));
    LinkReceiver receiver;
    QDesktopServices::setUrlHandler("https", &receiver, "capture");
    window.followLink("https://example.test/path");
    QCOMPARE(receiver.last, QUrl("https://example.test/path"));
    QDesktopServices::unsetUrlHandler("https");
    window.setBenchMode(true);
    QSettings().setValue(QStringLiteral("editor/autoSave"), true);
  }
  void linkDialogLanguagePreservesRichTextAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(typist::editor::loadMarkdown("[**Styled** and *italic*](https://old.test) tail\n",
                                         editor->document(), nullptr));
    editor->setTextCursor(editor->document()->find("Styled"));
    const auto before = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<typist::ui::LinkDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      QCOMPARE(dialog->windowTitle(), QStringLiteral("编辑链接"));
      QCOMPARE(dialog->findChild<QPushButton*>(QStringLiteral("cancelLink"))->text(),
               QStringLiteral("取消"));
      QCOMPARE(dialog->findChild<QLineEdit*>("linkLabel")->text(),
               QStringLiteral("Styled and italic"));
      dialog->findChild<QLineEdit*>("linkDestination")->setText("https://new.test/a b");
      dialog->accept();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    const auto saved = typist::editor::serializeDocument(editor->document());
    QVERIFY2(saved.contains("https://new.test/a%20b"), qPrintable(saved));
    const auto parsed = typist::core::parseMarkdown(saved.toStdString());
    bool bold = false, italic = false;
    for (const auto& run : parsed.blocks.front().runs) {
      bold |= run.bold;
      italic |= run.italic;
    }
    QVERIFY(bold && italic);
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QTimer::singleShot(0, &window, [] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      dialog->reject();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
  }
  void sourceViewLinkDialogKeepsMarkdownEditingAndUndo() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText("raw text");
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->setTextCursor(editor->document()->find("raw text"));
    const auto before = editor->toPlainText();
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<typist::ui::LinkDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      dialog->findChild<QLineEdit*>("linkLabel")->setText("Source **label**");
      dialog->findChild<QLineEdit*>("linkDestination")->setText("https://example.test/a b");
      dialog->accept();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
    QVERIFY(editor->toPlainText().contains("[Source **label**](https://example.test/a%20b)"));
    editor->undo();
    QCOMPARE(editor->toPlainText(), before);
    editor->redo();
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(editor->toPlainText().contains("Source label"));
    const auto linked = editor->document()->find("Source label");
    QCOMPARE(linked.charFormat().anchorHref(), QStringLiteral("https://example.test/a%20b"));
  }
  void linkWorkflowVisuals() {
    const auto output = QString::fromUtf8(qgetenv("TYPIST_LINK_VISUAL_DIR"));
    if (output.isEmpty()) QSKIP("Set TYPIST_LINK_VISUAL_DIR to capture link specimens");
    QVERIFY(QDir().mkpath(output));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/link-writing.md"));
    window.resize(1180, 880);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTest::qWait(30);
    QVERIFY(window.grab().save(output + "/links-light.png"));
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QFile html(output + "/links.html");
    QVERIFY(html.open(QIODevice::WriteOnly));
    html.write(typist::core::wrapHtmlDocument(typist::render::html(markdown).toStdString(), "Links")
                   .c_str());
    html.close();
    QString error;
    QVERIFY2(typist::platform::exportDocx(output + "/links.docx", markdown, &error),
             qPrintable(error));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(20);
    QVERIFY(window.grab().save(output + "/links-dark-zh.png"));
    editor->setTextCursor(editor->document()->find("linked note"));
    QTimer::singleShot(0, &window, [&] {
      auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget());
      QVERIFY(dialog);
      QTimer::singleShot(1000, dialog, &QDialog::reject);
      auto* headings = dialog->findChild<QComboBox*>(QStringLiteral("linkHeading"));
      QVERIFY(headings);
      const auto sample = headings->grab().toImage();
      QVERIFY(sample.pixelColor(sample.width() / 2, sample.height() / 2).lightness() < 128);
      QVERIFY(dialog->grab().save(output + "/link-dialog-dark-zh.png"));
      dialog->reject();
    });
    window.findChild<QAction*>(QStringLiteral("format_link"))->trigger();
  }
  void listStatusLanguageSaveAndSource() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("list.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("- [x] **Done**\n- [ ] Next\n\nTAIL\n");
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.resize(1080, 800);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    auto cursor = editor->textCursor();
    cursor.setPosition(0);
    cursor.setPosition(editor->document()->findBlockByNumber(1).position() + 2,
                       QTextCursor::KeepAnchor);
    editor->setTextCursor(cursor);
    const auto original = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("task_done"))->text(),
             QStringLiteral("标记为已完成"));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    window.findChild<QAction*>(QStringLiteral("task_done"))->trigger();
    const auto saved = typist::editor::serializeDocument(editor->document());
    QVERIFY(saved.contains("- [x] Next"));
    QVERIFY(saved.contains("**Done**"));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), saved.toUtf8());
    file.close();
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!editor->changeTaskStatus(typist::editor::TaskStatus::Todo));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), saved);
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QCOMPARE(window.findChild<QAction*>(QStringLiteral("task_done"))->text(),
             QStringLiteral("Mark as Complete"));
    editor->document()->setModified(false);
  }
  void listWorkflowVisuals() {
    const auto output = QString::fromUtf8(qgetenv("TYPIST_LIST_VISUAL_DIR"));
    if (output.isEmpty()) QSKIP("Set TYPIST_LIST_VISUAL_DIR to capture list specimens");
    QVERIFY(QDir().mkpath(output));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/list-writing.md"));
    window.resize(1180, 940);
    window.show();
    window.activateWindow();
    QTest::qWait(50);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(window.grab().save(output + "/list-light.png"));
    QString error;
    QVERIFY2(typist::platform::exportPdf(output + "/list.pdf", editor->document(), &error),
             qPrintable(error));
    QVERIFY2(
        typist::platform::exportDocx(output + "/list.docx",
                                     typist::editor::serializeDocument(editor->document()), &error),
        qPrintable(error));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(30);
    QVERIFY(window.grab().save(output + "/list-dark-zh.png"));
    auto* menu =
        qobject_cast<QMenu*>(window.findChild<QAction*>(QStringLiteral("task_done"))->parent());
    QVERIFY(menu);
    menu->popup(window.mapToGlobal(QPoint(200, 75)));
    QTest::qWait(30);
    QVERIFY(menu->grab().save(output + "/list-task-menu-zh.png"));
    menu->close();
  }
  void tableToolbarLanguagePasteSaveAndSource() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("table.md"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Before\n\n| Name | Value |\n|---|---:|\n| one | 1 |\n| two | 2 |\n\nTAIL\n");
    file.close();
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(path);
    window.resize(1000, 750);
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTextTable* table = nullptr;
    for (auto block = editor->document()->begin(); block.isValid() && !table; block = block.next())
      table = QTextCursor(block).currentTable();
    QVERIFY(table);
    editor->setTextCursor(table->cellAt(1, 0).firstCursorPosition());
    editor->setFocus();
    QTest::qWait(10);
    auto* bar = editor->findChild<QFrame*>(QStringLiteral("tableBar"));
    QVERIFY(bar && bar->isVisible());
    auto* rows = bar->findChild<QToolButton*>(QStringLiteral("tableRowsMenu"));
    QVERIFY(rows);
    QCOMPARE(rows->text(), QStringLiteral("Rows"));
    const auto original = typist::editor::serializeDocument(editor->document());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(10);
    QCOMPARE(rows->text(), QStringLiteral("行"));
    QCOMPARE(bar->findChild<QAction*>(QStringLiteral("tableBarCopyMarkdown"))->text(),
             QStringLiteral("复制为 Markdown"));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    QApplication::clipboard()->setText(QStringLiteral("**literal**\t10\n中文\t20\n"));
    actionBySource(window, QStringLiteral("Paste Markdown"))->trigger();
    QCOMPARE(table->rows(), 3);
    QCOMPARE(table->columns(), 2);
    const auto snapshot = typist::editor::serializeDocument(editor->document());
    QVERIFY(snapshot.contains("\\*\\*literal\\*\\*"));
    QVERIFY(QMetaObject::invokeMethod(&window, "save", Qt::DirectConnection));
    QVERIFY(file.open(QIODevice::ReadOnly));
    QCOMPARE(file.readAll(), snapshot.toUtf8());
    file.close();
    window.findChild<QAction*>(QStringLiteral("table_copy_markdown"))->trigger();
    QVERIFY(QApplication::clipboard()->text().startsWith("| Name"));
    QTest::keyClick(editor, Qt::Key_Return, Qt::ControlModifier);
    QVERIFY(!editor->textCursor().currentTable());
    QVERIFY(!editor->paragraphSourceActive());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QVERIFY(!bar->isVisible());
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), snapshot);
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    QTest::qWait(5);
    QCOMPARE(rows->text(), QStringLiteral("Rows"));
  }
  void tableWorkflowVisuals() {
    const auto out = qEnvironmentVariable("TYPIST_TABLE_OUTPUT");
    if (out.isEmpty()) QSKIP("Set TYPIST_TABLE_OUTPUT to retain table writing specimens.");
    QVERIFY(QDir().mkpath(out));
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.resize(1180, 850);
    window.openFile(QStringLiteral(TYPIST_SOURCE_DIR "/tests/corpus/table-writing.md"));
    window.show();
    window.activateWindow();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QTextTable* table = nullptr;
    for (auto block = editor->document()->begin(); block.isValid() && !table; block = block.next())
      table = QTextCursor(block).currentTable();
    QVERIFY(table);
    editor->setTextCursor(table->cellAt(1, 1).firstCursorPosition());
    editor->setFocus();
    QTest::qWait(25);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-light.png")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_dark"))->trigger();
    QTest::qWait(15);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-dark-zh.png")));
    auto* rows = editor->findChild<QToolButton*>(QStringLiteral("tableRowsMenu"));
    rows->menu()->popup(rows->mapToGlobal(QPoint(0, rows->height())));
    QTRY_VERIFY(rows->menu()->isVisible());
    QTest::qWait(40);
    QVERIFY(rows->menu()->grab().save(out + QStringLiteral("/table-row-menu-dark-zh.png")));
    QVERIFY(window.grab().save(out + QStringLiteral("/table-menu-dark-zh.png")));
    rows->menu()->hide();
    window.resize(800, 650);
    QTest::qWait(15);
    QVERIFY(window.grab().save(out + QStringLiteral("/table-compact-zh.png")));
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QString error;
    QFile html(out + QStringLiteral("/table.html"));
    QVERIFY(html.open(QIODevice::WriteOnly));
    html.write(QByteArray::fromStdString(typist::core::wrapHtmlDocument(
        typist::render::html(markdown).toStdString(), "Table writing")));
    html.close();
    QVERIFY2(
        typist::platform::exportPdf(out + QStringLiteral("/table.pdf"), editor->document(), &error),
        qPrintable(error));
    QVERIFY2(typist::platform::exportDocx(out + QStringLiteral("/table.docx"), markdown, &error),
             qPrintable(error));
  }

  void epubMetadataLanguageThemeAndCancel() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("dark"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    typist::ui::EpubDialog dialog(QStringLiteral("A book"), QStringLiteral("An author"),
                                  QStringLiteral("en"), &window);
    dialog.show();
    auto* title = dialog.findChild<QLineEdit*>(QStringLiteral("epubTitle"));
    auto* language = dialog.findChild<QLineEdit*>(QStringLiteral("epubLanguage"));
    auto* apply = dialog.findChild<QPushButton*>(QStringLiteral("confirmEpub"));
    auto* cancel = dialog.findChild<QPushButton*>(QStringLiteral("cancelEpub"));
    QVERIFY(title && language && apply && cancel);
    QVERIFY(dialog.palette().color(QPalette::Window).lightness() < 100);
    QVERIFY(dialog.palette().color(QPalette::Text).lightness() > 160);
    title->clear();
    QVERIFY(!apply->isEnabled());
    title->setText(QStringLiteral("  我的书 & Ideas  "));
    language->setText(QStringLiteral("invalid language"));
    QVERIFY(!apply->isEnabled());
    language->setText(QStringLiteral("zh-CN"));
    QVERIFY(apply->isEnabled());
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QTest::qWait(5);
    QCOMPARE(dialog.windowTitle(), QStringLiteral("导出 EPUB"));
    QCOMPARE(apply->text(), QStringLiteral("导出"));
    QCOMPARE(cancel->text(), QStringLiteral("取消"));
    QCOMPARE(dialog.bookTitle(), QStringLiteral("我的书 & Ideas"));
    QCOMPARE(dialog.author(), QStringLiteral("An author"));
    QCOMPARE(dialog.language(), QStringLiteral("zh-CN"));
    const auto output = qEnvironmentVariable("TYPIST_EPUB_OUTPUT");
    if (!output.isEmpty())
      QVERIFY(dialog.grab().save(output + QStringLiteral("/metadata-dark-zh.png")));
    cancel->click();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QTimer::singleShot(0, &window, [&] {
      auto* active = window.findChild<typist::ui::EpubDialog*>(QStringLiteral("epubDialog"));
      // The first dialog is hidden; the menu opens a new, visible metadata dialog.
      for (auto* child : window.findChildren<typist::ui::EpubDialog*>())
        if (child->isVisible()) active = child;
      if (active) active->reject();
    });
    auto* action = actionBySource(window, QStringLiteral("Export EPUB"));
    QVERIFY(action);
    action->trigger();
    window.findChild<QAction*>(QStringLiteral("language_en"))->trigger();
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    typist::ui::EpubDialog light(QStringLiteral("A book"), {}, QStringLiteral("en"), &window);
    light.show();
    QCOMPARE(light.windowTitle(), QStringLiteral("Export EPUB"));
    QVERIFY(light.palette().color(QPalette::Window).lightness() > 200);
    if (!output.isEmpty())
      QVERIFY(light.grab().save(output + QStringLiteral("/metadata-light-en.png")));
  }
  void epubExportUsesDraftAndSourceWithoutEditing() {
    QTemporaryDir directory;
    const auto inputPath = directory.filePath(QStringLiteral("draft.md"));
    QFile input(inputPath);
    QVERIFY(input.open(QIODevice::WriteOnly));
    input.write("Original **word**.\n\nTail.\n");
    input.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(inputPath);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Draft **snapshot**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto steps = editor->document()->availableUndoSteps();
    const auto path = directory.filePath(QStringLiteral("book.epub"));
    const typist::platform::EpubOptions options{QStringLiteral("A book"), {}, QStringLiteral("en")};
    QVERIFY(window.exportEpubForBench(path, options) > 400);
    QFile output(path);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("Draft <strong>snapshot</strong>"));
    output.close();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), steps);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Draft **snapshot**."));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->textCursor().insertText(QStringLiteral("Source "));
    const auto source = editor->toPlainText();
    const auto sourceSteps = editor->document()->availableUndoSteps();
    QVERIFY(window.exportEpubForBench(path, options) > 400);
    QVERIFY(output.open(QIODevice::ReadOnly));
    QVERIFY(output.readAll().contains("Source "));
    output.close();
    QCOMPARE(editor->toPlainText(), source);
    QCOMPARE(editor->document()->availableUndoSteps(), sourceSteps);
    QVERIFY(input.open(QIODevice::ReadOnly));
    QCOMPARE(input.readAll(), QByteArray("Original **word**.\n\nTail.\n"));
  }

  void epubMenuExportsWithDefaultSuffixAndRemembersMetadata() {
    QTemporaryDir directory;
    QImage cover(100, 160, QImage::Format_RGB32);
    cover.fill(QColor("#315f99"));
    const auto coverPath = directory.filePath(QStringLiteral("cover.png"));
    QVERIFY(cover.save(coverPath));
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(typist::editor::loadMarkdown(QStringLiteral("# First\n\nOne.\n\n# Second\n\nTwo."),
                                         editor->document(), nullptr));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool metadataAccepted = false, destinationAccepted = false;
    QTimer automation;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* dialog = qobject_cast<typist::ui::EpubDialog*>(QApplication::activeModalWidget())) {
        dialog->findChild<QLineEdit*>(QStringLiteral("epubTitle"))
            ->setText(QStringLiteral("Menu book"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubAuthor"))
            ->setText(QStringLiteral("Writer"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubLanguage"))
            ->setText(QStringLiteral("en"));
        dialog->findChild<QLineEdit*>(QStringLiteral("epubCover"))->setText(coverPath);
        dialog->findChild<QCheckBox*>(QStringLiteral("epubSplitChapters"))->setChecked(true);
        metadataAccepted = true;
        dialog->accept();
      } else if (auto* destination =
                     qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        auto* name = destination->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (!name) return;
        name->setText(directory.filePath(QStringLiteral("menu-book")));
        destinationAccepted = true;
        QMetaObject::invokeMethod(destination, "accept", Qt::DirectConnection);
      }
    });
    // Ensure a failed automation closes any modal dialog rather than hanging CI.
    QTimer safety;
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    safety.start(3000);
    automation.start();
    const bool invoked = QMetaObject::invokeMethod(&window, "exportEpub", Qt::DirectConnection);
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(invoked && metadataAccepted && destinationAccepted);
    QFile file(directory.filePath(QStringLiteral("menu-book.epub")));
    QVERIFY(file.open(QIODevice::ReadOnly));
    const auto bytes = file.readAll();
    QVERIFY(bytes.contains("<dc:title>Menu book</dc:title>"));
    QVERIFY(bytes.contains("<dc:creator>Writer</dc:creator>"));
    QVERIFY(bytes.contains("OEBPS/cover.xhtml"));
    QVERIFY(bytes.contains("OEBPS/chapter-2.xhtml"));
    QCOMPARE(QSettings().value(QStringLiteral("export/epubAuthor")).toString(),
             QStringLiteral("Writer"));
    QCOMPARE(QSettings().value(QStringLiteral("export/epubLanguage")).toString(),
             QStringLiteral("en"));
    QVERIFY(QSettings().value(QStringLiteral("export/epubSplitChapters")).toBool());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    bool remembered = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto* dialog = qobject_cast<typist::ui::EpubDialog*>(QApplication::activeModalWidget())) {
        remembered = dialog->splitChapters() && dialog->coverImage().isEmpty();
        dialog->reject();
      }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "exportEpub", Qt::DirectConnection));
    QVERIFY(remembered);
  }
  void epubCoverBrowseValidationAndBilingualPreview() {
    QTemporaryDir directory;
    const auto coverPath = directory.filePath(QStringLiteral("封面.png"));
    QImage cover(600, 900, QImage::Format_RGB32);
    cover.fill(QColor("#315f99"));
    {
      QPainter painter(&cover);
      painter.setPen(Qt::white);
      QFont font = painter.font();
      font.setPointSize(32);
      painter.setFont(font);
      painter.drawText(cover.rect().adjusted(30, 30, -30, -30), Qt::AlignCenter,
                       QStringLiteral("Writing\nChapters"));
    }
    QVERIFY(cover.save(coverPath));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    const auto output = qEnvironmentVariable("TYPIST_CHAPTERS_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      typist::ui::EpubDialog dialog(QStringLiteral("Writing / 章节"), QStringLiteral("Author"),
                                    QStringLiteral("zh-CN"), &window);
      dialog.show();
      auto* input = dialog.findChild<QLineEdit*>(QStringLiteral("epubCover"));
      auto* confirm = dialog.findChild<QPushButton*>(QStringLiteral("confirmEpub"));
      input->setText(directory.filePath(QStringLiteral("missing.png")));
      QVERIFY(!confirm->isEnabled());
      input->setText(coverPath);
      QVERIFY(confirm->isEnabled());
      dialog.setSplitChapters(true);
      QVERIFY(dialog.splitChapters());
      QVERIFY(dialog.findChild<QLabel*>(QStringLiteral("epubCoverPreview"))->isVisible());
      QCOMPARE(dialog.findChild<QCheckBox*>(QStringLiteral("epubSplitChapters"))->text(),
               language == QLatin1String("en")
                   ? QStringLiteral("Split chapters at level-one headings")
                   : QStringLiteral("按一级标题拆分章节"));
      if (!output.isEmpty())
        QVERIFY(dialog.grab().save(output + QStringLiteral("/cover-dialog-") + language +
                                   QStringLiteral(".png")));
      dialog.findChild<QPushButton*>(QStringLiteral("clearEpubCover"))->click();
      QVERIFY(input->text().isEmpty());
      QVERIFY(confirm->isEnabled());
      const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
      QTimer automation, safety;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &dialog, [&] {
        if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget()))
          if (auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
            name->setText(coverPath);
            QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
          }
      });
      connect(&safety, &QTimer::timeout, &dialog, [] {
        if (auto* modal = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
          modal->reject();
      });
      automation.start();
      safety.start(3000);
      dialog.findChild<QPushButton*>(QStringLiteral("browseEpubCover"))->click();
      automation.stop();
      safety.stop();
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
      QCOMPARE(dialog.coverImage(), coverPath);
      QVERIFY(confirm->isEnabled());
      dialog.findChild<QPushButton*>(QStringLiteral("cancelEpub"))->click();
    }
  }

  void htmlImportNewBufferSaveAndPreview() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("source.html"));
    QFile source(path);
    const QByteArray bytes =
        "<html><meta charset='utf-8'><h1>Imported title</h1><p>A <b>bold</b> "
        "paragraph.</p><pre>one\n  "
        "two</pre><table><tr><th>Name</th></tr><tr><td>Value</td></tr></table></html>";
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write(bytes);
    source.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QVERIFY(window.importHtmlFile(path));
    QVERIFY(window.windowTitle().contains('*'));
    QVERIFY(!window.windowTitle().contains(QStringLiteral("source.html")));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(editor);
    const auto markdown = typist::editor::serializeDocument(editor->document());
    QVERIFY(markdown.contains(QStringLiteral("# Imported title")));
    QVERIFY(markdown.contains(QStringLiteral("**bold**")));
    QVERIFY(!editor->document()->isUndoAvailable());
    const auto parsed = typist::core::parseMarkdown(markdown.toStdString());
    const auto target = directory.filePath(QStringLiteral("saved.md"));
    QVERIFY(window.saveDocumentAs(target));
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), bytes);
    source.close();
    QFile saved(target);
    QVERIFY(saved.open(QIODevice::ReadOnly));
    QVERIFY(typist::core::astEqual(
        parsed.blocks, typist::core::parseMarkdown(saved.readAll().toStdString()).blocks));
    QVERIFY(window.exportEpubForBench(directory.filePath(QStringLiteral("imported.epub")),
                                      {QStringLiteral("Imported"), {}, QStringLiteral("en")}) >
            400);
    QVERIFY(window.exportDocxForBench(directory.filePath(QStringLiteral("imported.docx"))) > 400);
    const auto output = qEnvironmentVariable("TYPIST_HTML_OUTPUT");
    for (const auto& preference : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + preference)->trigger();
      window
          .findChild<QAction*>(preference == QStringLiteral("en")
                                   ? QStringLiteral("appearance_light")
                                   : QStringLiteral("appearance_dark"))
          ->trigger();
      const auto result = typist::editor::importHtmlFile(path);
      typist::ui::HtmlImportDialog preview(result, &window);
      preview.show();
      QTest::qWait(10);
      QVERIFY(preview.findChild<QTextEdit*>(QStringLiteral("htmlImportPreview"))->isReadOnly());
      QCOMPARE(preview.windowTitle(), preference == QStringLiteral("en")
                                          ? QStringLiteral("Import HTML")
                                          : QStringLiteral("导入 HTML"));
      QCOMPARE(
          preview.findChild<QPushButton*>(QStringLiteral("confirmHtmlImport"))->text(),
          preference == QStringLiteral("en") ? QStringLiteral("Import") : QStringLiteral("导入"));
      if (!output.isEmpty())
        QVERIFY(preview.grab().save(output + QStringLiteral("/preview-") + preference +
                                    QStringLiteral(".png")));
      preview.findChild<QPushButton*>(QStringLiteral("cancelHtmlImport"))->click();
      QCOMPARE(preview.result(), int(QDialog::Rejected));
      QCOMPARE(typist::editor::serializeDocument(editor->document()), markdown);
    }
  }
  void htmlImportCancellationAndErrorsKeepDraft() {
    QTemporaryDir directory;
    QFile source(directory.filePath(QStringLiteral("source.html")));
    QVERIFY(source.open(QIODevice::WriteOnly));
    source.write("<h1>Replacement</h1>");
    source.close();
    QSettings().setValue(QStringLiteral("editor/autoSave"), false);
    typist::MainWindow window;
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original **draft**."));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("Pending **snapshot**."));
    bool cancelClicked = false;
    QTimer::singleShot(0, &window, [&] {
      if (auto* message = qobject_cast<QMessageBox*>(QApplication::activeModalWidget())) {
        cancelClicked = true;
        message->button(QMessageBox::Cancel)->click();
      }
    });
    QVERIFY(!window.importHtmlFile(source.fileName()));
    QVERIFY(cancelClicked);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(draft->toPlainText(), QStringLiteral("Pending **snapshot**."));
    QVERIFY(!window.importHtmlFile(directory.filePath(QStringLiteral("missing.html"))));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Pending **snapshot**."));
    window.setBenchMode(true);
  }
  void htmlClipboardPasteUndoAndLiteralContexts() {
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Before TAIL"));
    auto cursor = editor->textCursor();
    cursor.setPosition(7);
    editor->setTextCursor(cursor);
    const auto before = typist::editor::serializeDocument(editor->document());
    auto* mime = new QMimeData;
    mime->setHtml(QStringLiteral("<h2>Pasted</h2><p><b>Bold</b> and <i>italic</i>.</p>"));
    mime->setText(QStringLiteral("Pasted\nBold and italic."));
    QApplication::clipboard()->setMimeData(mime);
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    const auto pasted = typist::editor::serializeDocument(editor->document());
    QVERIFY(pasted.contains(QStringLiteral("## Pasted")));
    QVERIFY(pasted.contains(QStringLiteral("**Bold**")));
    QVERIFY(pasted.contains(QStringLiteral("TAIL")));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    editor->redo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), pasted);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    cursor = editor->textCursor();
    cursor.movePosition(QTextCursor::End);
    editor->setTextCursor(cursor);
    const auto sourceBefore = editor->toPlainText();
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QVERIFY(editor->toPlainText().mid(sourceBefore.size()).contains(QStringLiteral("## Pasted")));
    editor->undo();
    QCOMPARE(editor->toPlainText(), sourceBefore);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    cursor = editor->textCursor();
    cursor.setPosition(0);
    editor->setTextCursor(cursor);
    editor->formatParagraph(typist::editor::ParagraphStyle::Code);
    const auto codeBefore = typist::editor::serializeDocument(editor->document());
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QVERIFY(!editor->textCursor().blockFormat().headingLevel());
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("Pasted\nBold and italic.")));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), codeBefore);
    QApplication::clipboard()->setText(QStringLiteral("No HTML"));
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QCOMPARE(typist::editor::serializeDocument(editor->document()), codeBefore);
  }
  void htmlPasteIntoParagraphDraftIsLiteral() {
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original paragraph"));
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->selectAll();
    auto* mime = new QMimeData;
    mime->setHtml(QStringLiteral("<script>Nothing readable</script>"));
    mime->setText(QStringLiteral("Literal <tag> & **Markdown**"));
    QApplication::clipboard()->setMimeData(mime);
    QVERIFY(QMetaObject::invokeMethod(&window, "pasteHtmlAsMarkdown", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Literal <tag> & **Markdown**"));
    QVERIFY(QMetaObject::invokeMethod(&window, "undo", Qt::DirectConnection));
    QCOMPARE(draft->toPlainText(), QStringLiteral("Original paragraph"));
  }
  void htmlImportMenuPreviewCancelAndAccept() {
    QTemporaryDir directory;
    const auto path = directory.filePath(QStringLiteral("menu.html"));
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("<h1>Menu import</h1><p>A paragraph.</p>");
    file.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Keep this buffer"));
    const auto before = typist::editor::serializeDocument(editor->document());
    const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool selected = false, reviewed = false, accepting = false;
    QTimer automation, safety;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        // selectFile() deliberately leaves a focused filename field unchanged on
        // some Qt versions. Enter the absolute path in that field like a user would.
        auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (!name) return;
        name->setText(path);
        selected = true;
        QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
      } else if (auto* preview = qobject_cast<typist::ui::HtmlImportDialog*>(
                     QApplication::activeModalWidget())) {
        reviewed = true;
        if (accepting)
          preview->accept();
        else
          preview->reject();
      }
    });
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    automation.start();
    safety.start(3000);
    auto* action = window.findChild<QAction*>(QStringLiteral("importHtml"));
    const bool actionFound = action;
    if (action) action->trigger();
    const bool cancelledUnchanged = typist::editor::serializeDocument(editor->document()) == before;
    const bool cancelledReviewed = selected && reviewed;
    selected = reviewed = false;
    accepting = true;
    if (action) action->trigger();
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(actionFound && cancelledReviewed && selected && reviewed && cancelledUnchanged);
    QVERIFY(typist::editor::serializeDocument(editor->document())
                .contains(QStringLiteral("# Menu import")));
  }

  void pdfSetupPreviewThemeLanguageAndCancel() {
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("light"));
    typist::MainWindow window;
    window.setBenchMode(true);
    window.show();
    QTextDocument document;
    QString source = QStringLiteral("# Export preview\n\n");
    for (int i = 0; i < 100; ++i)
      source += QStringLiteral("Paragraph %1 / 一段可阅读的中文与英文。\n\n").arg(i + 1);
    QVERIFY(typist::editor::loadMarkdown(source, &document, nullptr));
    const auto before = typist::editor::serializeDocument(&document);
    typist::platform::PdfPageOptions initial;
    initial.title = QStringLiteral("Document");
    initial.header = QStringLiteral("{title}");
    initial.footer = QStringLiteral("Page #");
    const auto output = qEnvironmentVariable("TYPIST_PDF_OUTPUT");
    for (const auto& language : {QStringLiteral("en"), QStringLiteral("zh")}) {
      window.findChild<QAction*>(QStringLiteral("language_") + language)->trigger();
      window
          .findChild<QAction*>(language == QLatin1String("en") ? QStringLiteral("appearance_light")
                                                               : QStringLiteral("appearance_dark"))
          ->trigger();
      typist::ui::PdfExportDialog dialog(&document, initial, &window);
      dialog.show();
      auto* exportButton = dialog.findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
      QTRY_VERIFY(exportButton->isEnabled());
      auto* next = dialog.findChild<QPushButton*>(QStringLiteral("pdfNextPage"));
      auto* previous = dialog.findChild<QPushButton*>(QStringLiteral("pdfPreviousPage"));
      QVERIFY(next->isEnabled());
      QVERIFY(!previous->isEnabled());
      next->click();
      QVERIFY(previous->isEnabled());
      previous->click();
      auto* orientation = dialog.findChild<QComboBox*>(QStringLiteral("pdfOrientation"));
      orientation->setCurrentIndex(1);
      dialog.findChild<QComboBox*>(QStringLiteral("pdfPaper"))->setCurrentIndex(1);
      dialog.findChild<QDoubleSpinBox*>(QStringLiteral("pdfMargin"))->setValue(12.5);
      dialog.findChild<QLineEdit*>(QStringLiteral("pdfHeader"))
          ->setText(QStringLiteral("Custom #"));
      QTRY_VERIFY(exportButton->isEnabled());
      const auto options = dialog.options();
      QVERIFY(options.landscape);
      QCOMPARE(options.paper, QStringLiteral("letter"));
      QCOMPARE(options.marginMm, 12.5);
      QCOMPARE(options.header, QStringLiteral("Custom #"));
      QCOMPARE(dialog.windowTitle(), language == QLatin1String("en") ? QStringLiteral("Export PDF")
                                                                     : QStringLiteral("导出 PDF"));
      QCOMPARE(exportButton->text(),
               language == QLatin1String("en") ? QStringLiteral("Export") : QStringLiteral("导出"));
      auto* paper = dialog.findChild<QComboBox*>(QStringLiteral("pdfPaper"));
      QVERIFY(paper);
      QCOMPARE(
          paper->itemText(paper->findData(QStringLiteral("custom"))),
          language == QLatin1String("en") ? QStringLiteral("Custom") : QStringLiteral("自定义"));
      auto* sectionBreaks =
          dialog.findChild<QCheckBox*>(QStringLiteral("pdfBreakBetweenTopHeadings"));
      QVERIFY(sectionBreaks);
      QCOMPARE(sectionBreaks->text(), language == QLatin1String("en")
                                          ? QStringLiteral("Break between level 1 headings")
                                          : QStringLiteral("一级标题之间分页"));
      if (!output.isEmpty())
        QVERIFY(dialog.grab().save(output + QStringLiteral("/setup-") + language +
                                   QStringLiteral(".png")));
      dialog.findChild<QPushButton*>(QStringLiteral("cancelPdfExport"))->click();
      QCOMPARE(dialog.result(), int(QDialog::Rejected));
      QCOMPARE(typist::editor::serializeDocument(&document), before);
    }
  }
  void pageBreakMenuInsertsAnUndoableMarkdownBlock() {
    const auto language = typist::platform::languagePreference();
    const auto restore = qScopeGuard([&] { typist::platform::setLanguagePreference(language); });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::MainWindow window;
    window.setBenchMode(true);
    QTemporaryDir directory;
    QFile file(directory.filePath(QStringLiteral("chapters.md")));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("Before after.\n");
    file.close();
    QVERIFY(window.openFile(file.fileName()));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* insert = window.findChild<QAction*>(QStringLiteral("insert_page_break"));
    QVERIFY(editor && insert);
    QCOMPARE(insert->text(), QStringLiteral("Insert Page Break"));
    const auto original = typist::editor::serializeDocument(editor->document());
    auto cursor = editor->document()->find(QStringLiteral("Before "));
    QVERIFY(!cursor.isNull());
    cursor.clearSelection();
    editor->setTextCursor(cursor);
    insert->trigger();
    const auto marker = QString::fromLatin1(typist::core::kPageBreakMarkdown);
    const auto inserted = typist::editor::serializeDocument(editor->document());
    QCOMPARE(inserted, QStringLiteral("Before \n\n") + marker + QStringLiteral("\n\nafter.\n\n"));
    QCOMPARE(editor->textCursor().block().text(), QStringLiteral("after."));
    editor->undo();
    QCOMPARE(typist::editor::serializeDocument(editor->document()), original);
    QVERIFY(!editor->document()->isUndoAvailable());
    editor->redo();
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    QCOMPARE(editor->toPlainText().count(marker), 1);
    editor->moveCursor(QTextCursor::End);
    insert->trigger();
    QCOMPARE(editor->toPlainText().count(marker), 2);
    editor->undo();
    QCOMPARE(editor->toPlainText().count(marker), 1);
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    auto block = editor->document()->begin().next();
    QVERIFY(typist::render::isPageBreak(block));
    QTextCursor selection(block);
    selection.movePosition(QTextCursor::EndOfBlock, QTextCursor::KeepAnchor);
    QCOMPARE(typist::editor::selectedMarkdown(selection), marker);
    editor->setTextCursor(QTextCursor(block));
    QTest::keyClick(editor, Qt::Key_Delete);
    const auto deleted = typist::editor::serializeDocument(editor->document());
    QVERIFY(!deleted.contains(marker));
    QVERIFY(deleted.contains(QStringLiteral("Before")) &&
            deleted.contains(QStringLiteral("after.")));
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(insert->text(), QStringLiteral("插入分页符"));
  }

  void pdfDialogSectionBreaksRefreshPreview() {
    QTextDocument document;
    QVERIFY(typist::editor::loadMarkdown(
        QStringLiteral("# One\n\nFirst chapter.\n\n# Two\n\nSecond chapter[^note].\n\n"
                       "[^note]: A note.\n"),
        &document, nullptr));
    typist::ui::PdfExportDialog dialog(&document, {});
    dialog.show();
    auto* sectionBreaks =
        dialog.findChild<QCheckBox*>(QStringLiteral("pdfBreakBetweenTopHeadings"));
    auto* exportButton = dialog.findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
    auto* previous = dialog.findChild<QPushButton*>(QStringLiteral("pdfPreviousPage"));
    auto* next = dialog.findChild<QPushButton*>(QStringLiteral("pdfNextPage"));
    QVERIFY(sectionBreaks && exportButton && previous && next);
    QTRY_VERIFY(exportButton->isEnabled());
    QVERIFY(!sectionBreaks->isChecked() && !next->isEnabled());
    sectionBreaks->setChecked(true);
    QTRY_VERIFY(next->isEnabled());
    QVERIFY(dialog.options().breakBetweenTopHeadings);
    next->click();
    QVERIFY(previous->isEnabled() && next->isEnabled());
    next->click();
    QVERIFY(!next->isEnabled());
    sectionBreaks->setChecked(false);
    QTRY_VERIFY(exportButton->isEnabled());
    QVERIFY(!previous->isEnabled() && !next->isEnabled());
    QVERIFY(!dialog.options().breakBetweenTopHeadings);
  }

  void pdfCustomPaperControlsRevalidatePreview() {
    QTextDocument document;
    document.setPlainText(QStringLiteral("Preview content"));
    typist::ui::PdfExportDialog dialog(&document, {});
    dialog.show();
    auto* paper = dialog.findChild<QComboBox*>(QStringLiteral("pdfPaper"));
    auto* width = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("pdfPaperWidth"));
    auto* height = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("pdfPaperHeight"));
    auto* margin = dialog.findChild<QDoubleSpinBox*>(QStringLiteral("pdfMargin"));
    auto* error = dialog.findChild<QLabel*>(QStringLiteral("pdfPreviewError"));
    auto* exportButton = dialog.findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
    QVERIFY(paper && width && height && margin && error && exportButton);
    QTRY_VERIFY(exportButton->isEnabled());
    QVERIFY(!width->isVisible() && !height->isVisible());
    paper->setCurrentIndex(paper->findData(QStringLiteral("custom")));
    QVERIFY(width->isVisible() && height->isVisible());
    width->setValue(50);
    height->setValue(50);
    margin->setValue(30);
    QTRY_VERIFY(!error->text().isEmpty());
    QVERIFY(!exportButton->isEnabled());
    width->setValue(173.5);
    height->setValue(244.2);
    QTRY_VERIFY(exportButton->isEnabled());
    QVERIFY(error->text().isEmpty());
    QCOMPARE(dialog.options().customSizeMm, QSizeF(173.5, 244.2));
    paper->setCurrentIndex(paper->findData(QStringLiteral("a5")));
    QVERIFY(!width->isVisible() && !height->isVisible());
    QTRY_VERIFY(exportButton->isEnabled());
    QCOMPARE(dialog.options().paper, QStringLiteral("a5"));
  }

  void repeatExportUsesSuccessfulOptionsAndCurrentDraft_data() {
    QTest::addColumn<QString>("command");
    QTest::addColumn<QString>("extension");
    QTest::addColumn<bool>("setupRequired");
    QTest::newRow("HTML") << QStringLiteral("Export HTML") << QStringLiteral("html") << false;
    QTest::newRow("plain HTML") << QStringLiteral("Export HTML (without styles)")
                                << QStringLiteral("html") << false;
    QTest::newRow("PNG") << QStringLiteral("Export Image") << QStringLiteral("png") << true;
    QTest::newRow("PDF") << QStringLiteral("Export PDF") << QStringLiteral("pdf") << true;
    QTest::newRow("DOCX") << QStringLiteral("Export DOCX") << QStringLiteral("docx") << false;
    QTest::newRow("EPUB") << QStringLiteral("Export EPUB") << QStringLiteral("epub") << true;
  }

  void repeatExportUsesSuccessfulOptionsAndCurrentDraft() {
    QFETCH(QString, command);
    QFETCH(QString, extension);
    QFETCH(bool, setupRequired);
    QSettings settings;
    QMap<QString, QVariant> oldValues;
    for (const auto& key :
         {QStringLiteral("export/preferences"), QStringLiteral("export/lastDirectory"),
          QStringLiteral("export/htmlOutline"), QStringLiteral("export/image"),
          QStringLiteral("pdfPaper"), QStringLiteral("pdfMarginMm"),
          QStringLiteral("pdf/customSizeMm"), QStringLiteral("pdf/landscape"),
          QStringLiteral("pdf/repeatTableHeader"), QStringLiteral("pdf/breakBetweenTopHeadings"),
          QStringLiteral("pdf/header"), QStringLiteral("pdf/footer"),
          QStringLiteral("export/epubAuthor"), QStringLiteral("export/epubLanguage"),
          QStringLiteral("export/epubSplitChapters")})
      oldValues.insert(key, settings.value(key));
    const auto language = typist::platform::languagePreference();
    const auto theme = typist::platform::themePreference();
    const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] {
      for (auto it = oldValues.cbegin(); it != oldValues.cend(); ++it) {
        if (it.value().isValid())
          settings.setValue(it.key(), it.value());
        else
          settings.remove(it.key());
      }
      typist::platform::setLanguagePreference(language);
      typist::platform::setThemePreference(theme);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
    });
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    typist::platform::setThemePreference(QStringLiteral("dark"));
    typist::platform::setExportPreferences({});
    settings.setValue(QStringLiteral("export/htmlOutline"), true);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto source = directory.filePath(QStringLiteral("source.md"));
    const auto other = directory.filePath(QStringLiteral("other.md"));
    for (const auto& path : {source, other}) {
      QFile file(path);
      QVERIFY(file.open(QIODevice::WriteOnly));
      QVERIFY(file.write("# First\n\nSome text.\n\n# Second\n\nOld paragraph.\n") > 0);
    }
    const auto coverPath = directory.filePath(QStringLiteral("cover.png"));
    QImage cover(64, 100, QImage::Format_RGB32);
    cover.fill(QColor("#326a83"));
    QVERIFY(cover.save(coverPath));
    typist::MainWindow window(nullptr, true);
    QVERIFY(window.openFile(source));
    window.show();
    auto* original = actionBySource(window, command);
    auto* repeat = window.findChild<QAction*>(QStringLiteral("repeatExport"));
    auto* overwrite = window.findChild<QAction*>(QStringLiteral("overwritePreviousExport"));
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    QVERIFY(original && repeat && overwrite && editor);
    QVERIFY(!repeat->isEnabled() && !overwrite->isEnabled());

    const auto run = [&](QAction* action, bool expectSetup, const QString& path) {
      bool setupSeen = false, matched = false;
      QTimer automation, safety;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &window, [&] {
        auto* modal = QApplication::activeModalWidget();
        if (!modal) return;
        if (auto* file = qobject_cast<QFileDialog*>(modal)) {
          automation.stop();
          auto* name = file->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
          matched = name && file->windowTitle() == command && setupSeen == expectSetup;
          if (!matched || path.isEmpty()) {
            file->reject();
            return;
          }
          name->setText(path);
          QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
          matched = file->result() == QDialog::Accepted;
        } else if (!expectSetup) {
          modal->close();
        } else if (auto* image = qobject_cast<typist::ui::ImageExportDialog*>(modal)) {
          setupSeen = true;
          image->findChild<QSpinBox*>(QStringLiteral("imageWidth"))->setValue(450);
          image->findChild<QComboBox*>(QStringLiteral("imageScale"))->setCurrentIndex(1);
          image->findChild<QComboBox*>(QStringLiteral("imageTheme"))->setCurrentIndex(2);
          image->accept();
        } else if (auto* pdf = qobject_cast<typist::ui::PdfExportDialog*>(modal)) {
          setupSeen = true;
          auto* paper = pdf->findChild<QComboBox*>(QStringLiteral("pdfPaper"));
          paper->setCurrentIndex(paper->findData(QStringLiteral("a5")));
          pdf->accept();
        } else if (auto* epub = qobject_cast<typist::ui::EpubDialog*>(modal)) {
          setupSeen = true;
          epub->findChild<QLineEdit*>(QStringLiteral("epubTitle"))
              ->setText(QStringLiteral("Replay Book"));
          epub->findChild<QLineEdit*>(QStringLiteral("epubAuthor"))
              ->setText(QStringLiteral("Replay Author"));
          epub->findChild<QLineEdit*>(QStringLiteral("epubLanguage"))
              ->setText(QStringLiteral("en"));
          epub->findChild<QLineEdit*>(QStringLiteral("epubCover"))->setText(coverPath);
          epub->findChild<QCheckBox*>(QStringLiteral("epubSplitChapters"))->setChecked(true);
          epub->accept();
        }
      });
      safety.setSingleShot(true);
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = QApplication::activeModalWidget()) modal->close();
      });
      automation.start();
      safety.start(3000);
      action->trigger();
      return matched;
    };

    const auto initialPath = directory.filePath(QStringLiteral("initial.") + extension);
    const auto repeatedPath = directory.filePath(QStringLiteral("repeat.") + extension);
    QVERIFY(run(original, setupRequired, initialPath));
    QFile initial(initialPath);
    QVERIFY(initial.open(QIODevice::ReadOnly));
    const auto initialBytes = initial.readAll();
    QVERIFY(!initialBytes.isEmpty());
    initial.close();
    QVERIFY(repeat->isEnabled() && overwrite->isEnabled());

    // Change global defaults to distinguish the replay from a new export.
    settings.setValue(QStringLiteral("export/htmlOutline"), false);
    settings.setValue(QStringLiteral("export/image"), QVariantMap{{QStringLiteral("width"), 640}});
    typist::platform::setPdfPaper(QStringLiteral("letter"));
    settings.setValue(QStringLiteral("export/epubAuthor"), QStringLiteral("Different author"));
    window.findChild<QAction*>(QStringLiteral("appearance_light"))->trigger();
    editor->moveCursor(QTextCursor::End);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(
        QStringLiteral("Pending **new contents**. ") +
        QStringLiteral("A longer paragraph wraps across more lines. ").repeated(14));
    const auto before = typist::editor::serializeDocument(editor->document());
    QVERIFY(run(repeat, false, {}));
    QVERIFY(repeat->isEnabled() && editor->paragraphSourceActive());
    if (extension == QLatin1String("png")) {
      QVERIFY(run(repeat, false, directory.filePath(QStringLiteral("rejected.jpg"))));
      QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("rejected.jpg"))));
      QVERIFY(repeat->isEnabled());
    }
    QVERIFY(run(repeat, false, repeatedPath));
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QFile repeated(repeatedPath);
    QVERIFY(repeated.open(QIODevice::ReadOnly));
    const auto bytes = repeated.readAll();
    QVERIFY(!bytes.isEmpty());
    if (extension == QLatin1String("html")) {
      QVERIFY(bytes.contains("<nav class=\"export-outline"));
      QVERIFY(bytes.contains("new contents"));
      QCOMPARE(bytes.contains("<style>"), command == QLatin1String("Export HTML"));
      if (command == QLatin1String("Export HTML")) QVERIFY(bytes.contains("#1f2328"));
    } else if (extension == QLatin1String("png")) {
      const auto picture = QImage::fromData(bytes);
      QCOMPARE(picture.width(), 900);
      QCOMPARE(picture.pixelColor(0, 0), QColor("#1f2328"));
      QVERIFY(picture.height() > QImage::fromData(initialBytes).height());
    } else if (extension == QLatin1String("pdf")) {
      static const QRegularExpression mediaBox(QStringLiteral(R"(/MediaBox\s*\[[^\]]+\])"));
      const auto firstBox = mediaBox.match(QString::fromLatin1(initialBytes)).captured();
      QVERIFY(!firstBox.isEmpty());
      QCOMPARE(mediaBox.match(QString::fromLatin1(bytes)).captured(), firstBox);
    } else if (extension == QLatin1String("epub")) {
      QVERIFY(bytes.contains("<dc:title>Replay Book</dc:title>"));
      QVERIFY(bytes.contains("<dc:creator>Replay Author</dc:creator>"));
      QVERIFY(bytes.contains("OEBPS/cover.xhtml"));
      QVERIFY(bytes.contains("OEBPS/chapter-2.xhtml"));
      QVERIFY(bytes.contains("new contents"));
    } else {
      QVERIFY(bytes.contains("new contents"));
    }
    repeated.close();

    const auto runOverwrite = [&](bool expectWarning = false) {
      bool modalSeen = false;
      bool warningSeen = false;
      QTimer rejectDialogs;
      connect(&rejectDialogs, &QTimer::timeout, &window, [&] {
        if (auto* modal = QApplication::activeModalWidget()) {
          modalSeen = true;
          if (const auto* message = qobject_cast<QMessageBox*>(modal))
            warningSeen = message->icon() == QMessageBox::Warning;
          modal->close();
        }
      });
      rejectDialogs.start(10);
      overwrite->trigger();
      return expectWarning ? warningSeen : !modalSeen;
    };
    draft->setPlainText(QStringLiteral("Updated overwrite contents. ") +
                        QStringLiteral("More lines for the replacement file. ").repeated(55));
    QVERIFY(runOverwrite());
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QVERIFY(repeated.open(QIODevice::ReadOnly));
    const auto overwritten = repeated.readAll();
    repeated.close();
    QVERIFY(!overwritten.isEmpty());
    QVERIFY(overwritten != bytes);
    if (extension == QLatin1String("png")) {
      const auto picture = QImage::fromData(overwritten);
      QCOMPARE(picture.width(), 900);
      QCOMPARE(picture.pixelColor(0, 0), QColor("#1f2328"));
      QVERIFY(picture.height() > QImage::fromData(bytes).height());
    } else if (extension == QLatin1String("pdf")) {
      static const QRegularExpression mediaBox(QStringLiteral(R"(/MediaBox\s*\[[^\]]+\])"));
      QCOMPARE(mediaBox.match(QString::fromLatin1(overwritten)).captured(),
               mediaBox.match(QString::fromLatin1(bytes)).captured());
    } else {
      QVERIFY(overwritten.contains("Updated overwrite contents"));
    }
    if (extension == QLatin1String("epub")) {
      QVERIFY(overwritten.contains("<dc:title>Replay Book</dc:title>"));
      QVERIFY(overwritten.contains("OEBPS/cover.xhtml"));
      QVERIFY(QFile::remove(coverPath));
      QVERIFY(runOverwrite(true));
      QVERIFY(repeated.open(QIODevice::ReadOnly));
      QCOMPARE(repeated.readAll(), overwritten);
      repeated.close();
    } else if (command == QLatin1String("Export HTML")) {
      QVERIFY(overwritten.contains("<title>repeat</title>"));
      QVERIFY(overwritten.contains("<nav class=\"export-outline"));
      auto modified = overwritten;
      modified[0] = '!';
      QVERIFY(modified != overwritten);
      QVERIFY(repeated.open(QIODevice::WriteOnly | QIODevice::Truncate));
      QCOMPARE(repeated.write(modified), modified.size());
      repeated.close();
      QVERIFY(runOverwrite());
      QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("has changed")));
      QVERIFY(repeated.open(QIODevice::ReadOnly));
      QCOMPARE(repeated.readAll(), modified);
      repeated.close();
      QVERIFY(repeated.remove());
      QVERIFY(runOverwrite());
      QVERIFY(!repeated.exists());
      QVERIFY(repeat->isEnabled());
      QVERIFY(run(repeat, false, repeatedPath));
      QVERIFY(runOverwrite());
      QVERIFY(window.statusBar()->currentMessage().contains(QStringLiteral("Exported HTML")));
    }
    QVERIFY(QDir(directory.path())
                .entryList({QStringLiteral(".typist-export-*")}, QDir::Dirs | QDir::Hidden)
                .isEmpty());
    QVERIFY(initial.open(QIODevice::ReadOnly));
    QCOMPARE(initial.readAll(), initialBytes);
    QVERIFY(window.openFile(source));
    QVERIFY(repeat->isEnabled() && overwrite->isEnabled());
    QVERIFY(window.openFile(other));
    QVERIFY(!repeat->isEnabled() && !overwrite->isEnabled());
  }

  void exportPreferencesApplyToAllSaveDialogsAndOnlyRememberSuccessfulWrites() {
    using namespace typist::platform;
    QSettings settings;
    const auto preferencesKey = QStringLiteral("export/preferences");
    const auto lastKey = QStringLiteral("export/lastDirectory");
    const auto previousPreferences = settings.value(preferencesKey);
    const auto previousDirectory = settings.value(lastKey);
    const auto language = languagePreference();
    const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] {
      for (const auto& entry : {qMakePair(preferencesKey, previousPreferences),
                                qMakePair(lastKey, previousDirectory)}) {
        if (entry.second.isValid())
          settings.setValue(entry.first, entry.second);
        else
          settings.remove(entry.first);
      }
      setLanguagePreference(language);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
    });
    settings.remove(preferencesKey);
    settings.remove(lastKey);
    setLanguagePreference(QStringLiteral("en"));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    const auto documentFolder = directory.filePath(QStringLiteral("documents"));
    const auto customFolder = directory.filePath(QStringLiteral("导出 目录"));
    const auto actualFolder = directory.filePath(QStringLiteral("actual output"));
    for (const auto& folder : {documentFolder, customFolder, actualFolder})
      QVERIFY(QDir().mkpath(folder));
    QFile document(QDir(documentFolder).filePath(QStringLiteral("note.md")));
    QVERIFY(document.open(QIODevice::WriteOnly));
    QVERIFY(document.write("# Export specimen\n\nA short paragraph.\n") > 0);
    document.close();
    const auto canonical = [](const QString& path) { return QFileInfo(path).canonicalFilePath(); };
    QCOMPARE(exportDirectory({}), QDir::homePath());
    QCOMPARE(canonical(exportDirectory(document.fileName())), canonical(documentFolder));

    typist::MainWindow window(nullptr, true);
    QVERIFY(window.openFile(document.fileName()));
    window.show();
    auto* last = window.findChild<QAction*>(QStringLiteral("exportFolderLast"));
    auto* custom = window.findChild<QAction*>(QStringLiteral("exportFolderCustom"));
    auto* fromDocument = window.findChild<QAction*>(QStringLiteral("exportFolderDocument"));
    auto* reveal = window.findChild<QAction*>(QStringLiteral("openFolderAfterExport"));
    QVERIFY(last && custom && fromDocument && reveal);
    QVERIFY(fromDocument->isChecked() && !reveal->isChecked());
    last->trigger();
    QCOMPARE(exportPreferences().folderMode, ExportFolderMode::LastExportFolder);
    QCOMPARE(canonical(exportDirectory(document.fileName())), canonical(documentFolder));

    const auto drive = [&](QAction* action, const QString& expectedFolder, const QString& path,
                           bool chooseFolder = false, bool blockWrite = false) {
      if (!action) return false;
      bool verified = false;
      QTimer automation, safety;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &window, [&] {
        auto* modal = QApplication::activeModalWidget();
        if (auto* file = qobject_cast<QFileDialog*>(modal)) {
          automation.stop();
          auto* input = file->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
          verified = input &&
                     canonical(file->directory().absolutePath()) == canonical(expectedFolder) &&
                     (chooseFolder ? file->fileMode() == QFileDialog::Directory
                                   : file->acceptMode() == QFileDialog::AcceptSave);
          if (!verified || path.isEmpty()) {
            file->reject();
            return;
          }
          input->setText(path);
          QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
          verified = file->result() == QDialog::Accepted;
          if (verified && blockWrite) verified = QDir().mkpath(path);
        } else if (qobject_cast<typist::ui::PdfExportDialog*>(modal) ||
                   qobject_cast<typist::ui::ImageExportDialog*>(modal) ||
                   qobject_cast<typist::ui::EpubDialog*>(modal)) {
          qobject_cast<QDialog*>(modal)->accept();
        }
      });
      safety.setSingleShot(true);
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = QApplication::activeModalWidget()) modal->close();
      });
      automation.start();
      safety.start(3000);
      action->trigger();
      return verified;
    };
    QVERIFY(drive(custom, documentFolder, {}, true));
    QVERIFY(last->isChecked());
    QCOMPARE(exportPreferences().folderMode, ExportFolderMode::LastExportFolder);
    QVERIFY(drive(custom, documentFolder, customFolder, true));
    QVERIFY(custom->isChecked());
    QCOMPARE(canonical(exportDirectory(document.fileName())), canonical(customFolder));

    LinkReceiver receiver;
    QDesktopServices::setUrlHandler(QStringLiteral("file"), &receiver, "capture");
    const auto resetHandler =
        qScopeGuard([] { QDesktopServices::unsetUrlHandler(QStringLiteral("file")); });
    reveal->trigger();
    QVERIFY(exportPreferences().openFolderOnSuccess);
    for (const auto& name : {"Export HTML", "Export HTML (without styles)", "Export Image",
                             "Export PDF", "Export DOCX", "Export EPUB"}) {
      QVERIFY2(drive(actionBySource(window, QString::fromLatin1(name)), customFolder, {}), name);
    }
    QVERIFY(receiver.last.isEmpty());
    QVERIFY(!settings.contains(lastKey));

    auto* html = window.findChild<QAction*>(QStringLiteral("exportHtml"));
    const auto output = QDir(actualFolder).filePath(QStringLiteral("report.html"));
    QVERIFY(drive(html, customFolder, output));
    QVERIFY(QFileInfo(output).isFile());
    QCOMPARE(canonical(receiver.last.toLocalFile()), canonical(actualFolder));
    QCOMPARE(canonical(settings.value(lastKey).toString()), canonical(actualFolder));
    last->trigger();
    QCOMPARE(canonical(exportDirectory(document.fileName())), canonical(actualFolder));
    receiver.last = QUrl();
    const auto blocked = QDir(customFolder).filePath(QStringLiteral("blocked.html"));
    QVERIFY(drive(html, actualFolder, blocked, false, true));
    QVERIFY(QFileInfo(blocked).isDir());
    QVERIFY(receiver.last.isEmpty());
    QCOMPARE(canonical(settings.value(lastKey).toString()), canonical(actualFolder));

    auto* chinese = window.findChild<QAction*>(QStringLiteral("language_zh"));
    QVERIFY(chinese);
    chinese->trigger();
    QCOMPARE(reveal->text(), QStringLiteral("导出后打开文件夹"));
    QCOMPARE(last->text(), QStringLiteral("使用上次导出的文件夹"));
    setExportPreferences({ExportFolderMode::CustomFolder,
                          directory.filePath(QStringLiteral("missing folder")), false});
    QCOMPARE(canonical(exportDirectory(document.fileName())), canonical(documentFolder));
  }

  void imageExportSavesOptionsOnlyAfterWritingAndKeepsDraft() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    const auto key = QStringLiteral("export/image");
    const auto previous = settings.value(key);
    const auto language = typist::platform::languagePreference();
    const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] {
      if (previous.isValid())
        settings.setValue(key, previous);
      else
        settings.remove(key);
      typist::platform::setLanguagePreference(language);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
    });
    settings.remove(key);
    typist::platform::setLanguagePreference(QStringLiteral("en"));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QFile source(directory.filePath(QStringLiteral("export.test.md")));
    const QByteArray markdown("Original paragraph.\n\n## A heading\n\nFinal paragraph.\n");
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(markdown), markdown.size());
    source.close();
    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(source.fileName());
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* action = window.findChild<QAction*>(QStringLiteral("exportImage"));
    QVERIFY(editor && action);
    QCOMPARE(action->text(), QStringLiteral("Export Image"));
    editor->moveCursor(QTextCursor::Start);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Pending **draft**. ").repeated(30));
    const auto before = typist::editor::serializeDocument(editor->document());

    enum class Outcome { CancelSettings, CancelDestination, Save };
    const auto exportTo = [&](Outcome outcome, bool expectSaved, bool chinese = false) {
      bool handledSettings = false;
      bool handledDestination = false;
      QTimer automation;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &window, [&] {
        auto* modal = QApplication::activeModalWidget();
        if (auto* setup = qobject_cast<typist::ui::ImageExportDialog*>(modal)) {
          const auto initial = setup->options();
          handledSettings = initial.width == (expectSaved ? 720 : 640) &&
                            initial.fontPixels == (expectSaved ? 20 : 24) &&
                            initial.scale == (expectSaved ? 2 : 1) &&
                            setup->windowTitle() == (chinese ? QStringLiteral("导出图片")
                                                             : QStringLiteral("Export Image"));
          if (outcome == Outcome::CancelSettings) {
            setup->reject();
            return;
          }
          setup->findChild<QSpinBox*>(QStringLiteral("imageWidth"))->setValue(720);
          setup->findChild<QSpinBox*>(QStringLiteral("imageFontSize"))->setValue(20);
          setup->findChild<QComboBox*>(QStringLiteral("imageScale"))->setCurrentIndex(1);
          setup->findChild<QComboBox*>(QStringLiteral("imageTheme"))->setCurrentIndex(2);
          setup->accept();
        } else if (auto* file = qobject_cast<QFileDialog*>(modal)) {
          auto* input = file->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
          handledDestination = input && input->text() == QStringLiteral("export.test.png");
          if (outcome == Outcome::CancelDestination || !input) {
            file->reject();
            return;
          }
          input->setText(directory.filePath(QStringLiteral("note")));
          QMetaObject::invokeMethod(file, "accept", Qt::DirectConnection);
        }
      });
      QTimer safety;
      safety.setSingleShot(true);
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = QApplication::activeModalWidget()) modal->close();
      });
      automation.start();
      safety.start(3000);
      action->trigger();
      return handledSettings && handledDestination == (outcome != Outcome::CancelSettings);
    };
    QVERIFY(exportTo(Outcome::CancelSettings, false));
    QVERIFY(!settings.contains(key));
    QVERIFY(exportTo(Outcome::CancelDestination, false));
    QVERIFY(!settings.contains(key));
    const auto output = directory.filePath(QStringLiteral("note.png"));
    QVERIFY(!QFileInfo::exists(output));
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(exportTo(Outcome::Save, false));
    const QImage image(output);
    QVERIFY(!image.isNull());
    QCOMPARE(image.width(), 1440);
    QCOMPARE(image.pixelColor(0, 0), QColor("#1f2328"));
    QVERIFY(image.height() > 500);  // Includes the long pending paragraph.
    QCOMPARE(settings.value(key).toMap().value(QStringLiteral("width")).toInt(), 720);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), markdown);
    window.findChild<QAction*>(QStringLiteral("language_zh"))->trigger();
    QCOMPARE(action->text(), QStringLiteral("导出图片"));
    QVERIFY(exportTo(Outcome::CancelSettings, true, true));
  }

  void htmlExportUsesOutlinePreferenceAndKeepsPendingDraft() {
    QTemporaryDir directory;
    QVERIFY(directory.isValid());
    QSettings settings;
    const auto key = QStringLiteral("export/htmlOutline");
    const auto previous = settings.value(key);
    const bool collapsible = typist::platform::outlineCollapsible();
    const auto theme = typist::platform::themePreference();
    const bool native = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    const auto restore = qScopeGuard([&] {
      if (previous.isValid())
        settings.setValue(key, previous);
      else
        settings.remove(key);
      typist::platform::setOutlineCollapsible(collapsible);
      typist::platform::setThemePreference(theme);
      QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, native);
    });
    settings.remove(key);
    typist::platform::setOutlineCollapsible(false);
    typist::platform::setThemePreference(QStringLiteral("light"));
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);

    QFile source(directory.filePath(QStringLiteral("sample.md")));
    const QByteArray markdown(
        "Original paragraph.\n\n# Guide\n\n[TOC]\n\n## Installation\n\n"
        "Install Typist to write and export Markdown.\n\n"
        "### Windows\n\n![Sample](sample.png)\n\n"
        "```cpp\nint count = 1;\n```\n\n# Guide\n\nAnother section.\n");
    QVERIFY(source.open(QIODevice::WriteOnly));
    QCOMPARE(source.write(markdown), markdown.size());
    source.close();
    QImage image(48, 32, QImage::Format_RGB32);
    image.fill(QColor(QStringLiteral("#315e95")));
    QVERIFY(image.save(directory.filePath(QStringLiteral("sample.png"))));

    typist::MainWindow window;
    window.setBenchMode(true);
    window.openFile(source.fileName());
    window.show();
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    auto* styled = window.findChild<QAction*>(QStringLiteral("exportHtml"));
    auto* plain = window.findChild<QAction*>(QStringLiteral("exportPlainHtml"));
    auto* include = window.findChild<QAction*>(QStringLiteral("htmlExportOutline"));
    auto* hierarchy = window.findChild<QAction*>(QStringLiteral("outlineCollapsible"));
    QVERIFY(editor && styled && plain && include && hierarchy);
    QVERIFY(include->isCheckable() && !include->isChecked());
    QCOMPARE(styled->shortcut(), QKeySequence(QStringLiteral("Ctrl+Shift+E")));

    const auto exportTo = [&](QAction* action, const QString& name, bool cancel = false) {
      bool handled = false;
      QTimer automation;
      automation.setInterval(10);
      connect(&automation, &QTimer::timeout, &window, [&] {
        auto* dialog = qobject_cast<QFileDialog*>(QApplication::activeModalWidget());
        if (!dialog) return;
        automation.stop();
        auto* input = dialog->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"));
        if (cancel || !input) {
          handled = cancel;
          dialog->reject();
          return;
        }
        input->setText(directory.filePath(name));
        handled = QMetaObject::invokeMethod(dialog, "accept", Qt::DirectConnection);
      });
      QTimer safety;
      safety.setSingleShot(true);
      connect(&safety, &QTimer::timeout, &window, [] {
        if (auto* modal = QApplication::activeModalWidget()) modal->close();
      });
      automation.start();
      safety.start(3000);
      action->trigger();
      return handled;
    };
    const auto read = [&](const QString& name) {
      QFile file(directory.filePath(name + QStringLiteral(".html")));
      return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
    };
    QVERIFY(exportTo(styled, QStringLiteral("default")));
    const auto defaultHtml = read(QStringLiteral("default"));
    QVERIFY(defaultHtml.contains("<style>"));
    QVERIFY(defaultHtml.contains("class=\"table-of-contents\""));
    QVERIFY(!defaultHtml.contains("class=\"export-outline"));

    include->trigger();
    QVERIFY(settings.value(key).toBool());
    QVERIFY(exportTo(styled, QStringLiteral("flat")));
    const auto flat = read(QStringLiteral("flat"));
    QVERIFY(flat.contains("<nav class=\"export-outline flat\""));
    QVERIFY(flat.contains("class=\"table-of-contents\""));
    QVERIFY(flat.contains("data:image/png;base64,"));
    QVERIFY(!flat.contains("<details"));

    hierarchy->trigger();
    QVERIFY(hierarchy->isChecked());
    editor->moveCursor(QTextCursor::Start);
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    QVERIFY(draft);
    draft->setPlainText(QStringLiteral("Pending **draft**."));
    const auto before = typist::editor::serializeDocument(editor->document());
    QVERIFY(exportTo(plain, QStringLiteral("cancelled"), true));
    QVERIFY(read(QStringLiteral("cancelled")).isEmpty());
    QVERIFY(editor->paragraphSourceActive());
    QVERIFY(exportTo(plain, QStringLiteral("plain")));
    const auto plainHtml = read(QStringLiteral("plain"));
    QVERIFY(plainHtml.contains("<details open>"));
    QVERIFY(plainHtml.contains("href=\"#guide-1\""));
    QVERIFY(plainHtml.contains("class=\"table-of-contents\""));
    QVERIFY(plainHtml.contains("data:image/png;base64,"));
    QVERIFY(plainHtml.contains("Pending <strong>draft</strong>."));
    QVERIFY(!plainHtml.contains("<style>") && !plainHtml.contains(" style="));
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QVERIFY(source.open(QIODevice::ReadOnly));
    QCOMPARE(source.readAll(), markdown);

    const QString specimens = qEnvironmentVariable("TYPIST_HTML_OUTPUT");
    if (!specimens.isEmpty()) {
      QVERIFY(QDir().mkpath(specimens));
      QVERIFY(exportTo(styled, QStringLiteral("collapsible")));
      for (const auto& name : {QStringLiteral("flat.html"), QStringLiteral("plain.html"),
                               QStringLiteral("collapsible.html")})
        QVERIFY(QFile::copy(directory.filePath(name), QDir(specimens).filePath(name)));
    }
  }

  void pdfMenuPersistsOnlyAfterSuccessfulSave() {
    QTemporaryDir directory;
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("A document to export."));
    const auto before = typist::editor::serializeDocument(editor->document());
    const auto old = typist::platform::pdfOptions(QStringLiteral("Old"));
    const auto restoreOptions = qScopeGuard([&] { typist::platform::setPdfOptions(old); });
    QTimer::singleShot(0, &window, [&] {
      if (auto* dialog =
              qobject_cast<typist::ui::PdfExportDialog*>(QApplication::activeModalWidget())) {
        dialog->findChild<QComboBox*>(QStringLiteral("pdfPaper"))->setCurrentIndex(1);
        dialog->findChild<QCheckBox*>(QStringLiteral("pdfBreakBetweenTopHeadings"))
            ->setChecked(!old.breakBetweenTopHeadings);
        dialog->reject();
      }
    });
    QVERIFY(QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection));
    QCOMPARE(typist::platform::pdfOptions(QStringLiteral("Old")).paper, old.paper);
    QCOMPARE(typist::platform::pdfOptions(QStringLiteral("Old")).breakBetweenTopHeadings,
             old.breakBetweenTopHeadings);
    const bool previous = QApplication::testAttribute(Qt::AA_DontUseNativeDialogs);
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, true);
    bool configured = false, setupAccepted = false, fileAccepted = false, cancelDestination = true;
    QTimer automation, safety;
    automation.setInterval(10);
    connect(&automation, &QTimer::timeout, &window, [&] {
      if (auto* dialog =
              qobject_cast<typist::ui::PdfExportDialog*>(QApplication::activeModalWidget())) {
        if (!configured) {
          auto* paper = dialog->findChild<QComboBox*>(QStringLiteral("pdfPaper"));
          paper->setCurrentIndex(paper->findData(QStringLiteral("custom")));
          dialog->findChild<QDoubleSpinBox*>(QStringLiteral("pdfPaperWidth"))->setValue(173.5);
          dialog->findChild<QDoubleSpinBox*>(QStringLiteral("pdfPaperHeight"))->setValue(244.2);
          dialog->findChild<QComboBox*>(QStringLiteral("pdfOrientation"))->setCurrentIndex(1);
          dialog->findChild<QDoubleSpinBox*>(QStringLiteral("pdfMargin"))->setValue(12.5);
          dialog->findChild<QLineEdit*>(QStringLiteral("pdfHeader"))
              ->setText(QStringLiteral("Header #"));
          dialog->findChild<QCheckBox*>(QStringLiteral("pdfRepeatHeader"))->setChecked(false);
          dialog->findChild<QCheckBox*>(QStringLiteral("pdfBreakBetweenTopHeadings"))
              ->setChecked(!old.breakBetweenTopHeadings);
          configured = true;
        }
        auto* button = dialog->findChild<QPushButton*>(QStringLiteral("confirmPdfExport"));
        if (button->isEnabled()) {
          setupAccepted = true;
          button->click();
        }
      } else if (auto* selector = qobject_cast<QFileDialog*>(QApplication::activeModalWidget())) {
        if (cancelDestination) {
          fileAccepted = true;
          selector->reject();
          return;
        }
        if (auto* name = selector->findChild<QLineEdit*>(QStringLiteral("fileNameEdit"))) {
          name->setText(directory.filePath(QStringLiteral("menu-pdf")));
          fileAccepted = true;
          QMetaObject::invokeMethod(selector, "accept", Qt::DirectConnection);
        }
      }
    });
    connect(&safety, &QTimer::timeout, &window, [] {
      if (auto* dialog = qobject_cast<QDialog*>(QApplication::activeModalWidget()))
        dialog->reject();
    });
    automation.start();
    safety.start(3000);
    const bool cancelled = QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection) &&
                           setupAccepted && fileAccepted;
    const auto afterCancel = typist::platform::pdfOptions(QStringLiteral("Old"));
    cancelDestination = false;
    configured = setupAccepted = fileAccepted = false;
    const bool invoked = QMetaObject::invokeMethod(&window, "exportPdf", Qt::DirectConnection);
    automation.stop();
    safety.stop();
    QApplication::setAttribute(Qt::AA_DontUseNativeDialogs, previous);
    QVERIFY(invoked && setupAccepted && fileAccepted);
    QVERIFY(cancelled);
    QCOMPARE(afterCancel.header, old.header);
    QCOMPARE(afterCancel.paper, old.paper);
    QCOMPARE(afterCancel.customSizeMm, old.customSizeMm);
    QCOMPARE(afterCancel.breakBetweenTopHeadings, old.breakBetweenTopHeadings);
    QFile pdf(directory.filePath(QStringLiteral("menu-pdf.pdf")));
    QVERIFY(pdf.open(QIODevice::ReadOnly));
    QVERIFY(pdf.readAll().startsWith("%PDF-"));
    const auto saved = typist::platform::pdfOptions(QStringLiteral("New"));
    QVERIFY(saved.landscape);
    QCOMPARE(saved.paper, QStringLiteral("custom"));
    QCOMPARE(saved.customSizeMm, QSizeF(173.5, 244.2));
    QCOMPARE(saved.marginMm, 12.5);
    QCOMPARE(saved.header, QStringLiteral("Header #"));
    QVERIFY(!saved.repeatTableHeader);
    QCOMPARE(saved.breakBetweenTopHeadings, !old.breakBetweenTopHeadings);
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
  }
  void pdfUsesParagraphSnapshotWithoutApplying() {
    QTemporaryDir directory;
    typist::MainWindow window;
    window.setBenchMode(true);
    auto* editor = window.findChild<typist::ui::Editor*>(QStringLiteral("writingEditor"));
    editor->insertPlainText(QStringLiteral("Original paragraph"));
    const auto before = typist::editor::serializeDocument(editor->document());
    QVERIFY(editor->startParagraphSourceEdit());
    auto* draft = editor->findChild<QPlainTextEdit*>(QStringLiteral("paragraphSourceInput"));
    draft->setPlainText(QStringLiteral("# Draft export snapshot"));
    const auto undo = editor->document()->availableUndoSteps();
    QVERIFY(window.exportPdfForBench(directory.filePath(QStringLiteral("draft.pdf")), {}) > 400);
    QVERIFY(editor->paragraphSourceActive());
    QCOMPARE(typist::editor::serializeDocument(editor->document()), before);
    QCOMPARE(editor->document()->availableUndoSteps(), undo);
    QCOMPARE(draft->toPlainText(), QStringLiteral("# Draft export snapshot"));
    QVERIFY(QMetaObject::invokeMethod(&window, "toggleSourceView", Qt::DirectConnection));
    editor->insertPlainText(QStringLiteral("Source "));
    const auto source = editor->toPlainText();
    QVERIFY(window.exportPdfForBench(directory.filePath(QStringLiteral("source.pdf")), {}) > 400);
    QCOMPARE(editor->toPlainText(), source);
  }

 private:
  QTemporaryDir m_settings;
};

QTEST_MAIN(WindowTests)
#include "window_tests.moc"
