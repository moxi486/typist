#include "Settings.h"

#include <QFileInfo>
#include <QSettings>

namespace typist::platform {

namespace {
QSettings openSettings(QSettings::Format format = QSettings::NativeFormat) {
  return QSettings(format, QSettings::UserScope, QStringLiteral("Typist"),
                   QStringLiteral("Typist"));
}
const auto writingAreaKey = QStringLiteral("editor/writingAreaWidths");
const auto launchOptionsKey = QStringLiteral("startup/options");
const auto lastClosedKey = QStringLiteral("startup/lastClosed");

StartupTarget explicitStartupTarget(const QString& path) {
  const QFileInfo info(path);
  if (info.isFile()) {
    const auto suffix = info.suffix().toLower();
    return {info.absoluteFilePath(),
            {},
            suffix == QLatin1String("html") || suffix == QLatin1String("htm")};
  }
  if (info.isDir()) return {{}, info.absoluteFilePath()};
  return {};
}

WritingWidth readWritingWidth(const QVariant& value) {
  bool ok = false;
  const int width = value.toInt(&ok);
  if (ok) {
    for (auto choice :
         {WritingWidth::Fill, WritingWidth::Compact, WritingWidth::Standard, WritingWidth::Wide})
      if (width == int(choice)) return choice;
  }
  return WritingWidth::Standard;
}
}  // namespace

LaunchOptions launchOptions() {
  const auto values = openSettings(QSettings::defaultFormat()).value(launchOptionsKey).toMap();
  const int mode = values.value(QStringLiteral("mode")).toInt();
  return {mode >= int(LaunchMode::NewDocument) && mode <= int(LaunchMode::CustomFolder)
              ? LaunchMode(mode)
              : LaunchMode::NewDocument,
          values.value(QStringLiteral("customFolder")).toString()};
}

void setLaunchOptions(const LaunchOptions& options) {
  auto settings = openSettings(QSettings::defaultFormat());
  settings.setValue(launchOptionsKey,
                    QVariantMap{{QStringLiteral("mode"), int(options.mode)},
                                {QStringLiteral("customFolder"), options.customFolder}});
  settings.sync();
}

StartupTarget lastClosedTarget() {
  const auto values = openSettings(QSettings::defaultFormat()).value(lastClosedKey).toMap();
  return {values.value(QStringLiteral("file")).toString(),
          values.value(QStringLiteral("folder")).toString()};
}

void setLastClosedTarget(const StartupTarget& target) {
  auto settings = openSettings(QSettings::defaultFormat());
  settings.setValue(lastClosedKey, QVariantMap{{QStringLiteral("file"), target.file},
                                               {QStringLiteral("folder"), target.folder}});
  settings.sync();
}

StartupTarget startupTarget(const QStringList& arguments, bool benchmark,
                            const QString& benchmarkFile) {
  if (benchmark && !benchmarkFile.isEmpty()) return explicitStartupTarget(benchmarkFile);
  bool blank = false, reopen = false, positional = false, literal = false;
  for (const auto& argument : arguments) {
    if (!literal && argument.startsWith(QLatin1String("--"))) {
      literal = argument == QLatin1String("--");
      blank |= argument == QLatin1String("--new");
      reopen |= argument == QLatin1String("--reopen-file");
      continue;
    }
    if (argument.isEmpty()) continue;
    positional = true;
    const auto target = explicitStartupTarget(argument);
    if (!target.file.isEmpty() || !target.folder.isEmpty()) return target;
  }
  if (positional || benchmark || blank) return {};

  const auto options = launchOptions();
  const auto mode = reopen ? LaunchMode::LastFileAndFolder : options.mode;
  if (mode == LaunchMode::NewDocument) return {};
  if (mode == LaunchMode::CustomFolder)
    return {{}, QFileInfo(options.customFolder).isDir() ? options.customFolder : QString()};
  auto target = lastClosedTarget();
  if (mode == LaunchMode::LastFolder || !QFileInfo(target.file).isFile()) target.file.clear();
  if (!QFileInfo(target.folder).isDir()) target.folder.clear();
  return target;
}

WritingAreaWidths writingAreaWidths() {
  const auto widths = openSettings(QSettings::defaultFormat()).value(writingAreaKey).toMap();
  return {readWritingWidth(widths.value(QStringLiteral("rendered"))),
          readWritingWidth(widths.value(QStringLiteral("source")))};
}

void setWritingAreaWidths(const WritingAreaWidths& widths) {
  openSettings(QSettings::defaultFormat())
      .setValue(writingAreaKey, QVariantMap{{QStringLiteral("rendered"), int(widths.rendered)},
                                            {QStringLiteral("source"), int(widths.source)}});
}

FileBrowserOptions fileBrowserOptions() {
  const auto settings = openSettings();
  FileBrowserOptions options;
  if (settings.value(QStringLiteral("files/view")).toString() == QLatin1String("list"))
    options.view = FileBrowserView::List;
  const auto sort = settings.value(QStringLiteral("files/sort")).toString();
  if (sort == QLatin1String("natural")) options.sort = FileBrowserSort::Natural;
  if (sort == QLatin1String("modified")) options.sort = FileBrowserSort::Modified;
  if (sort == QLatin1String("created")) options.sort = FileBrowserSort::Created;
  options.descending = settings.value(QStringLiteral("files/descending"), false).toBool();
  options.foldersFirst = settings.value(QStringLiteral("files/foldersFirst"), false).toBool();
  options.groupByFolder = settings.value(QStringLiteral("files/groupByFolder"), false).toBool();
  return options;
}

void setFileBrowserOptions(const FileBrowserOptions& options) {
  auto settings = openSettings();
  settings.setValue(QStringLiteral("files/view"), options.view == FileBrowserView::List
                                                      ? QStringLiteral("list")
                                                      : QStringLiteral("tree"));
  settings.setValue(QStringLiteral("files/sort"),
                    options.sort == FileBrowserSort::Natural    ? QStringLiteral("natural")
                    : options.sort == FileBrowserSort::Modified ? QStringLiteral("modified")
                    : options.sort == FileBrowserSort::Created  ? QStringLiteral("created")
                                                                : QStringLiteral("name"));
  settings.setValue(QStringLiteral("files/descending"), options.descending);
  settings.setValue(QStringLiteral("files/foldersFirst"), options.foldersFirst);
  settings.setValue(QStringLiteral("files/groupByFolder"), options.groupByFolder);
  settings.sync();
}

bool outlineCollapsible() {
  return openSettings().value(QStringLiteral("outline/collapsible"), false).toBool();
}

void setOutlineCollapsible(bool enabled) {
  auto settings = openSettings();
  settings.setValue(QStringLiteral("outline/collapsible"), enabled);
  settings.sync();
}

QString themePreference(const QString& fallback) {
  return openSettings().value(QStringLiteral("theme"), fallback).toString();
}

void setThemePreference(const QString& value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("theme"), value);
  store.sync();
}

QString languagePreference() {
  const QString value =
      openSettings().value(QStringLiteral("language"), QStringLiteral("system")).toString();
  return value == QStringLiteral("zh") || value == QStringLiteral("en") ? value
                                                                        : QStringLiteral("system");
}

void setLanguagePreference(const QString& value) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("language"), value);
  store.sync();
}
bool copyImagesToAssets() {
  return openSettings().value(QStringLiteral("images/copyToAssets"), true).toBool();
}
void setCopyImagesToAssets(bool enabled) {
  QSettings store = openSettings();
  store.setValue(QStringLiteral("images/copyToAssets"), enabled);
}

CodeFencePreferences codeFencePreferences() {
  const auto values = openSettings().value(QStringLiteral("editor/codeFencePreferences")).toMap();
  return {values.value(QStringLiteral("language")).toString(),
          values.value(QStringLiteral("useLastLanguage"), false).toBool(),
          values.value(QStringLiteral("applyToCommands"), true).toBool(),
          values.value(QStringLiteral("applyToTypedFences"), true).toBool()};
}

void setCodeFencePreferences(const CodeFencePreferences& preferences) {
  const QVariantMap values{{QStringLiteral("language"), preferences.language},
                           {QStringLiteral("useLastLanguage"), preferences.useLastLanguage},
                           {QStringLiteral("applyToCommands"), preferences.applyToCommands},
                           {QStringLiteral("applyToTypedFences"), preferences.applyToTypedFences}};
  openSettings().setValue(QStringLiteral("editor/codeFencePreferences"), values);
}

QString lastCodeLanguage() {
  return openSettings().value(QStringLiteral("editor/lastCodeLanguage")).toString();
}

void setLastCodeLanguage(const QString& language) {
  openSettings().setValue(QStringLiteral("editor/lastCodeLanguage"), language);
}

QString pdfPaper(const QString& fallback) {
  const QString value = openSettings(QSettings::defaultFormat())
                            .value(QStringLiteral("pdfPaper"), fallback)
                            .toString();
  const auto id = pdfPaperId(value);
  return id == QPageSize::Custom ? QStringLiteral("custom") : QPageSize::key(id).toLower();
}

void setPdfPaper(const QString& value) {
  auto store = openSettings(QSettings::defaultFormat());
  store.setValue(QStringLiteral("pdfPaper"), value);
  store.sync();
}

double pdfMarginMm(double fallback) {
  const auto value =
      openSettings(QSettings::defaultFormat()).value(QStringLiteral("pdfMarginMm"), fallback);
  bool ok = false;
  const double margin = value.toDouble(&ok);
  return ok && margin >= 5.0 && margin <= 60.0 ? margin : fallback;
}

void setPdfMarginMm(double value) {
  auto store = openSettings(QSettings::defaultFormat());
  store.setValue(QStringLiteral("pdfMarginMm"), value);
  store.sync();
}

PdfPageOptions pdfOptions(const QString& title) {
  const auto settings = openSettings(QSettings::defaultFormat());
  PdfPageOptions options;
  options.paper = pdfPaper();
  const auto customSize = settings.value(QStringLiteral("pdf/customSizeMm")).toSizeF();
  if (validPdfCustomSize(customSize)) options.customSizeMm = customSize;
  options.marginMm = pdfMarginMm();
  options.landscape = settings.value(QStringLiteral("pdf/landscape"), false).toBool();
  options.repeatTableHeader =
      settings.value(QStringLiteral("pdf/repeatTableHeader"), true).toBool();
  options.breakBetweenTopHeadings =
      settings.value(QStringLiteral("pdf/breakBetweenTopHeadings"), false).toBool();
  options.header =
      settings.value(QStringLiteral("pdf/header"), QStringLiteral("{title}")).toString();
  options.footer =
      settings.value(QStringLiteral("pdf/footer"), QStringLiteral("Page #")).toString();
  options.title = title;
  return options;
}
void setPdfOptions(const PdfPageOptions& options) {
  setPdfPaper(options.paper);
  setPdfMarginMm(options.marginMm);
  auto settings = openSettings(QSettings::defaultFormat());
  if (validPdfCustomSize(options.customSizeMm))
    settings.setValue(QStringLiteral("pdf/customSizeMm"), options.customSizeMm);
  settings.setValue(QStringLiteral("pdf/landscape"), options.landscape);
  settings.setValue(QStringLiteral("pdf/repeatTableHeader"), options.repeatTableHeader);
  settings.setValue(QStringLiteral("pdf/breakBetweenTopHeadings"), options.breakBetweenTopHeadings);
  settings.setValue(QStringLiteral("pdf/header"), options.header);
  settings.setValue(QStringLiteral("pdf/footer"), options.footer);
}

QStringList recentFiles(int limit) {
  QSettings settings = openSettings();
  const QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  QStringList out;
  for (const QString& path : stored) {
    if (path.isEmpty() || out.contains(path)) continue;
    out.append(path);
    if (limit > 0 && out.size() >= limit) break;
  }
  return out;
}

void addRecentFile(const QString& path) {
  if (path.isEmpty()) return;
  QSettings settings = openSettings();
  QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  stored.removeOne(path);
  stored.prepend(path);
  while (stored.size() > 8) stored.removeLast();
  settings.setValue(QStringLiteral("recentFiles"), stored);
}

void removeRecentFile(const QString& path) {
  QSettings settings = openSettings();
  QStringList stored = settings.value(QStringLiteral("recentFiles")).toStringList();
  stored.removeOne(path);
  settings.setValue(QStringLiteral("recentFiles"), stored);
}

}  // namespace typist::platform
