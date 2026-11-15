#include "projectview.h"

#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

class TestProjectView : public QObject
{
    Q_OBJECT

private slots:
    void viewsSurviveTheFile()
    {
        ProjectView view;
        view.insert(QStringLiteral("a3f09c21b7e4"), PageView{QPointF(101.6, -76.25), 12.5});
        view.insert(QLatin1String(BOARD_VIEW_KEY), PageView{QPointF(0, 0), 4});

        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString project = dir.filePath(QStringLiteral("amp.ppcb"));
        QCOMPARE(projectViewPath(project), dir.filePath(QStringLiteral(".ppview")));
        // No file yet: no views.
        QVERIFY(readProjectView(project).isEmpty());

        QString error;
        QVERIFY2(writeProjectView(project, view, error), qPrintable(error));
        QVERIFY(readProjectView(project) == view);
    }

    void badPagesAreLeftOut()
    {
        QJsonObject root = projectViewToJson(
            ProjectView{{QStringLiteral("good"), PageView{QPointF(1, 2), 3}}});
        QJsonObject pages = root.value(QStringLiteral("pages")).toObject();
        pages.insert(QStringLiteral("zero"),
                     QJsonObject{{"center", QJsonArray{1, 2}}, {"scale", 0}});
        pages.insert(QStringLiteral("short"),
                     QJsonObject{{"center", QJsonArray{1}}, {"scale", 2}});
        pages.insert(QStringLiteral("text"),
                     QJsonObject{{"center", QJsonArray{1, 2}}, {"scale", "big"}});
        root.insert(QStringLiteral("pages"), pages);
        JsonReader reader;
        const ProjectView view = projectViewFromJson(root, reader);
        QVERIFY2(reader.ok(), qPrintable(reader.error()));
        QCOMPARE(view.keys(), QStringList({QStringLiteral("good")}));

        // Another kind of file is not a view.
        root.insert(QStringLiteral("type"), QStringLiteral("pocketpcb-project"));
        JsonReader other;
        QVERIFY(projectViewFromJson(root, other).isEmpty());
        QVERIFY(!other.ok());
    }
};

QTEST_APPLESS_MAIN(TestProjectView)
#include "test_projectview.moc"
