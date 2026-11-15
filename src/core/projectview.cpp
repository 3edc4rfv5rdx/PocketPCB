#include "projectview.h"

#include <QFileInfo>
#include <QJsonArray>

namespace ppcb {

namespace {

const char VIEW_FILE_NAME[] = ".ppview";
const char KEY_PAGES[] = "pages";
const char KEY_CENTER[] = "center";
const char KEY_SCALE[] = "scale";

} // namespace

const char BOARD_VIEW_KEY[] = "board";

bool operator==(const PageView &a, const PageView &b)
{
    return a.center == b.center && a.pixelsPerMm == b.pixelsPerMm;
}

QString projectViewPath(const QString &projectPath)
{
    return QFileInfo(projectPath).absolutePath() + QLatin1Char('/')
           + QLatin1String(VIEW_FILE_NAME);
}

QJsonObject projectViewToJson(const ProjectView &view)
{
    QJsonObject root;
    writeHeader(root, FILE_TYPE_VIEW);
    QJsonObject pages;
    for (auto it = view.constBegin(); it != view.constEnd(); ++it) {
        QJsonObject page;
        page.insert(QLatin1String(KEY_CENTER),
                    QJsonArray({it.value().center.x(), it.value().center.y()}));
        page.insert(QLatin1String(KEY_SCALE), it.value().pixelsPerMm);
        pages.insert(it.key(), page);
    }
    root.insert(QLatin1String(KEY_PAGES), pages);
    return root;
}

ProjectView projectViewFromJson(const QJsonObject &root, JsonReader &reader)
{
    ProjectView view;
    reader.header(root, FILE_TYPE_VIEW);
    if (!reader.ok())
        return view;
    const QJsonObject pages = root.value(QLatin1String(KEY_PAGES)).toObject();
    for (auto it = pages.constBegin(); it != pages.constEnd(); ++it) {
        const QJsonObject page = it.value().toObject();
        const QJsonArray center = page.value(QLatin1String(KEY_CENTER)).toArray();
        const QJsonValue scale = page.value(QLatin1String(KEY_SCALE));
        if (center.size() != 2 || !center.at(0).isDouble() || !center.at(1).isDouble()
            || !scale.isDouble() || !(scale.toDouble() > 0))
            continue;
        view.insert(it.key(), PageView{QPointF(center.at(0).toDouble(), center.at(1).toDouble()),
                                       scale.toDouble()});
    }
    return view;
}

ProjectView readProjectView(const QString &projectPath)
{
    const QString path = projectViewPath(projectPath);
    if (!QFileInfo::exists(path))
        return ProjectView();
    QJsonObject root;
    QString error;
    if (!JsonFile::read(path, root, error))
        return ProjectView();
    JsonReader reader;
    const ProjectView view = projectViewFromJson(root, reader);
    return reader.ok() ? view : ProjectView();
}

bool writeProjectView(const QString &projectPath, const ProjectView &view, QString &error)
{
    return JsonFile::write(projectViewPath(projectPath), projectViewToJson(view), error);
}

} // namespace ppcb
