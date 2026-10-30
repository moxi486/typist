#include "FolderLocations.h"
#include "FolderLocationsMenu.h"
#include "QuickOpenDialog.h"

#include <QAction>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QLabel>
#include <QLineEdit>
#include <QListWidget>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using typist::platform::FolderLocationsStore;
namespace {
QString makeFolder(const QTemporaryDir& directory, const QString& name) {
  const auto path = directory.filePath(name);
  return QDir().mkpath(path) ? QFileInfo(path).canonicalFilePath() : QString();
}
}  // namespace

class FolderLocationsTests : public QObject {
  Q_OBJECT
 private slots:
  void recencyNormalizationAndPersistence() {
    QTemporaryDir directory;
    const auto a = makeFolder(directory, QStringLiteral("中文/A&B"));
    const auto b = makeFolder(directory, QStringLiteral("Other/Notes"));
    QVERIFY(!a.isEmpty() && !b.isEmpty());
    const auto ini = directory.filePath("settings.ini");
    FolderLocationsStore store(ini);
    QVERIFY(store.read().recent.isEmpty());
    store.record({});
    store.record(directory.filePath("missing"));
    QVERIFY(store.read().recent.isEmpty());
    store.record(a);
    store.record(b);
    store.record(a + QStringLiteral("/../A&B/."));
    QCOMPARE(store.read().recent, QStringList({a, b}));
#ifdef Q_OS_WIN
    store.record(a.toUpper());
    QCOMPARE(store.read().recent.size(), 2);
    QCOMPARE(QString::compare(store.read().recent.first(), a, Qt::CaseInsensitive), 0);
#endif
    QCOMPARE(FolderLocationsStore(ini).read().recent, store.read().recent);
#ifdef Q_OS_UNIX
    const auto alias = directory.filePath("alias");
    QVERIFY(QFile::link(a, alias));
    store.record(alias);
    QCOMPARE(store.read().recent, QStringList({a, b}));
#endif
    QSettings raw(ini, QSettings::IniFormat);
    raw.setValue("folders/recent",
                 QStringList({a, a + "/.", directory.filePath("missing"), {}, b}));
    raw.sync();
    QCOMPARE(store.read().recent, QStringList({a, b}));
  }

  void pinsSurviveEvictionForgetAndClear() {
    QTemporaryDir directory;
    FolderLocationsStore store(directory.filePath("settings.ini"));
    const auto pinned = makeFolder(directory, QStringLiteral("Pinned"));
    store.record(pinned);
    QVERIFY(store.setPinned(pinned, true));
    for (int i = 0; i < 14; ++i) store.record(makeFolder(directory, QString::number(i)));
    QCOMPARE(store.read().recent.size(), 12);
    QVERIFY(!store.read().recent.contains(pinned));
    QCOMPARE(store.read().pinned, QStringList({pinned}));
    store.record(pinned);
    store.forgetRecent(pinned);
    QVERIFY(!store.read().recent.contains(pinned));
    QCOMPARE(store.read().pinned, QStringList({pinned}));
    store.clearRecent();
    QVERIFY(store.read().recent.isEmpty());
    QCOMPARE(store.read().pinned, QStringList({pinned}));
    QVERIFY(store.setPinned(pinned, false));
    QVERIFY(store.read().pinned.isEmpty());
  }

  void pinLimitAndMissingFolders() {
    QTemporaryDir directory;
    FolderLocationsStore store(directory.filePath("settings.ini"));
    QStringList paths;
    for (int i = 0; i < 17; ++i)
      paths.append(makeFolder(directory, QStringLiteral("folder-%1").arg(i)));
    for (int i = 0; i < 16; ++i) QVERIFY(store.setPinned(paths.at(i), true));
    QVERIFY(!store.setPinned(paths.last(), true));
    QCOMPARE(store.read().pinned.size(), 16);
    QVERIFY(store.setPinned(paths.first(), true));
    QCOMPARE(store.read().pinned.first(), paths.first());
    QVERIFY(store.setPinned(paths.first(), false));
    QVERIFY(store.setPinned(paths.last(), true));
    QCOMPARE(store.read().pinned.last(), paths.last());
    QVERIFY(QDir().rmdir(paths.at(1)));
    QCOMPARE(store.read().pinned.size(), 15);
    QVERIFY(!store.setPinned(paths.at(1), true));
    FolderLocationsStore invalidSettings(directory.path());
    QVERIFY(!invalidSettings.setPinned(paths.last(), true));
  }

  void menuActionsAndDistinctPaths() {
    QTemporaryDir directory;
    const auto a = makeFolder(directory, QStringLiteral("one/A&B"));
    const auto b = makeFolder(directory, QStringLiteral("two/A&B"));
    const auto ini = directory.filePath("settings.ini");
    FolderLocationsStore store(ini);
    store.record(a);
    store.record(b);
    typist::ui::FolderLocationsMenu menu(nullptr, ini);
    menu.setRoot(a);
    menu.refresh();
    menu.findChild<QAction*>("pinCurrentFolder")->trigger();
    QCOMPARE(store.read().pinned, QStringList({a}));
    menu.refresh();
    QCOMPARE(menu.findChild<QAction*>("pinCurrentFolder")->text(),
             QStringLiteral("Unpin Current Folder"));
    auto* first = menu.findChild<QAction*>("folderLocation_0");
    auto* second = menu.findChild<QAction*>("folderLocation_1");
    QVERIFY(first && second);
    QCOMPARE(first->data().toString(), a);
    QCOMPARE(second->data().toString(), b);
    QVERIFY(first->text().contains(QStringLiteral("A&&B")));
    QVERIFY(first->toolTip() != second->toolTip());
    QSignalSpy activated(&menu, &typist::ui::FolderLocationsMenu::folderActivated);
    second->trigger();
    QCOMPARE(activated.count(), 1);
    QCOMPARE(activated.first().first().toString(), b);
    QSignalSpy browse(&menu, &typist::ui::FolderLocationsMenu::browseRequested);
    menu.findChild<QAction*>("browseFolder")->trigger();
    QCOMPARE(browse.count(), 1);
    menu.findChild<QAction*>("clearRecentFolders")->trigger();
    QVERIFY(store.read().recent.isEmpty());
    QCOMPARE(store.read().pinned, QStringList({a}));
  }

  void quickOpenSearchesFoldersAndDeduplicatesPins() {
    QTemporaryDir directory;
    const auto a = makeFolder(directory, QStringLiteral("Pinned workspace"));
    const auto b = makeFolder(directory, QStringLiteral("最近资料"));
    QFile file(directory.filePath("document.md"));
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write("# Note\n");
    file.close();
    typist::ui::QuickOpenDialog dialog(directory.path(), {}, nullptr, {{a, b}, {a}});
    auto* status = dialog.findChild<QLabel*>("quickOpenStatus");
    auto* query = dialog.findChild<QLineEdit*>("quickOpenQuery");
    auto* list = dialog.findChild<QListWidget*>("quickOpenResults");
    dialog.show();
    QTRY_VERIFY(status->text().startsWith(QStringLiteral("3 items")));
    QCOMPARE(dialog.selectedPath(), a);
    QVERIFY(dialog.selectedIsFolder());
    QVERIFY(list->item(0)->text().contains(QStringLiteral("Pinned Folder")));
    query->setText(QStringLiteral("最近"));
    QTest::keyClick(query, Qt::Key_Return);
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QCOMPARE(dialog.selectedPath(), b);
    QVERIFY(dialog.selectedIsFolder());
  }
};
QTEST_MAIN(FolderLocationsTests)
#include "folder_locations_tests.moc"
