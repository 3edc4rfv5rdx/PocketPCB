#include "document.h"

#include <QDir>
#include <QFile>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace ppcb;

namespace {

const char GARBAGE[] = "not a project file";

// The document's content with another title on the first sheet.
Design withSheetTitle(const Document &document, const QString &title)
{
    Design design = document.design();
    design.schematic.sheets[0].title = title;
    return design;
}

// The document's content with one more track on the board.
Design withTrack(const Document &document)
{
    Design design = document.design();
    Track track;
    track.id = newObjectId();
    track.width = 500000;
    track.to = Point{NM_PER_MM, 0};
    design.board.tracks.append(track);
    return design;
}

Design withAuthor(const Document &document, const QString &author)
{
    Design design = document.design();
    design.project.titleBlock.author = author;
    return design;
}

bool writeGarbage(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(GARBAGE) == qint64(sizeof(GARBAGE) - 1);
}

bool isGarbage(const QString &path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) && file.readAll() == QByteArray(GARBAGE);
}

} // namespace

class TestDocument : public QObject
{
    Q_OBJECT

private slots:
    void startsAsANewProject()
    {
        Document document;
        QVERIFY(document.projectPath().isEmpty());
        QVERIFY(!document.isModified());
        QCOMPARE(document.design().schematic.sheets.size(), 1);
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("untitled.ppsch"));
        QVERIFY(!document.undoStack()->canUndo());

        document.startNew(QStringLiteral("amp"));
        QCOMPARE(document.design().project.boardFile, QStringLiteral("amp.ppbrd"));
        QVERIFY(!document.isModified());
    }

    void undoBringsBackTheSameContent()
    {
        Document document;
        const Design original = document.design();

        const Design first = withSheetTitle(document, QStringLiteral("Вход"));
        document.change(QStringLiteral("Rename sheet"), first);
        QVERIFY(document.design() == first);
        const Design second = withTrack(document);
        document.change(QStringLiteral("Add track"), second);
        QVERIFY(document.design() == second);
        QCOMPARE(document.undoStack()->count(), 2);
        QCOMPARE(document.undoStack()->undoText(), QStringLiteral("Add track"));

        // One history for the schematic and the board.
        document.undoStack()->undo();
        QVERIFY(document.design() == first);
        QCOMPARE(document.undoStack()->redoText(), QStringLiteral("Add track"));
        document.undoStack()->undo();
        QVERIFY(document.design() == original);
        QVERIFY(!document.undoStack()->canUndo());

        document.undoStack()->redo();
        QVERIFY(document.design() == first);
        document.undoStack()->redo();
        QVERIFY(document.design() == second);
        QVERIFY(!document.undoStack()->canRedo());
    }

    void aChangeAfterUndoDropsTheUndoneSteps()
    {
        Document document;
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        document.change(QStringLiteral("B"), withSheetTitle(document, QStringLiteral("B")));
        document.undoStack()->undo();
        document.change(QStringLiteral("C"), withSheetTitle(document, QStringLiteral("C")));
        QCOMPARE(document.undoStack()->count(), 2);
        QVERIFY(!document.undoStack()->canRedo());
        document.undoStack()->undo();
        QCOMPARE(document.design().schematic.sheets.at(0).title, QStringLiteral("A"));
    }

    void aChangeThatChangesNothingIsNoStep()
    {
        Document document;
        QSignalSpy changed(&document, &Document::designChanged);
        document.change(QStringLiteral("Nothing"), document.design());
        QCOMPARE(document.undoStack()->count(), 0);
        QCOMPARE(changed.count(), 0);
        QVERIFY(!document.isModified());

        // Other file names are not a change either: they are not the caller's.
        Design renamed = document.design();
        renamed.project.schematicFile = QStringLiteral("other.ppsch");
        renamed.project.boardFile = QStringLiteral("other.ppbrd");
        document.change(QStringLiteral("Rename files"), renamed);
        QCOMPARE(document.undoStack()->count(), 0);
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("untitled.ppsch"));

        // And they stay out of a real change.
        renamed.project.titleBlock.author = QStringLiteral("Автор");
        document.change(QStringLiteral("Set author"), renamed);
        QCOMPARE(document.undoStack()->count(), 1);
        QCOMPARE(document.design().project.titleBlock.author, QStringLiteral("Автор"));
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("untitled.ppsch"));
        QCOMPARE(document.design().project.boardFile, QStringLiteral("untitled.ppbrd"));
    }

    void signalsFollowTheContent()
    {
        Document document;
        QSignalSpy changed(&document, &Document::designChanged);
        QSignalSpy modified(&document, &Document::modifiedChanged);

        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QCOMPARE(changed.count(), 1);
        QCOMPARE(modified.count(), 1);
        QCOMPARE(modified.at(0).at(0).toBool(), true);
        QVERIFY(document.isModified());

        // A second change: still modified, no second signal.
        document.change(QStringLiteral("B"), withSheetTitle(document, QStringLiteral("B")));
        QCOMPARE(changed.count(), 2);
        QCOMPARE(modified.count(), 1);

        // Back at what was started with: not modified again.
        document.undoStack()->undo();
        document.undoStack()->undo();
        QCOMPARE(changed.count(), 4);
        QCOMPARE(modified.count(), 2);
        QCOMPARE(modified.at(1).at(0).toBool(), false);
        QVERIFY(!document.isModified());

        // The same content reached by another way is not modified either.
        document.undoStack()->redo();
        QVERIFY(document.isModified());
        document.change(QStringLiteral("Back"), withSheetTitle(document, QString()));
        QVERIFY(!document.isModified());
    }

    void historyIsLimited()
    {
        Document document;
        for (int i = 0; i < Document::UNDO_LIMIT + 5; ++i)
            document.change(QStringLiteral("Step"), withSheetTitle(document, QString::number(i)));
        QCOMPARE(document.undoStack()->count(), int(Document::UNDO_LIMIT));
        // Still modified after undoing everything that is left.
        while (document.undoStack()->canUndo())
            document.undoStack()->undo();
        QCOMPARE(document.design().schematic.sheets.at(0).title, QStringLiteral("4"));
        QVERIFY(document.isModified());
    }

    void saveNeedsAFile()
    {
        Document document;
        QString error;
        QVERIFY(!document.save(error));
        QVERIFY(!error.isEmpty());
    }

    void saveAsNamesTheFilesAfterTheProject()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.v2.ppcb"));

        Document document;
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QSignalSpy modified(&document, &Document::modifiedChanged);
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        QCOMPARE(document.projectPath(), path);
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("amp.v2.ppsch"));
        QCOMPARE(document.design().project.boardFile, QStringLiteral("amp.v2.ppbrd"));
        QVERIFY(!document.isModified());
        QCOMPARE(modified.count(), 1);
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("amp.v2.ppsch"))));
        QVERIFY(QFile::exists(dir.filePath(QStringLiteral("amp.v2.ppbrd"))));

        Design onDisk;
        QVERIFY2(DesignFiles::read(path, onDisk, error), qPrintable(error));
        QVERIFY(onDisk == document.design());

        // The history survives, and Undo does not bring the old names back.
        QVERIFY(document.undoStack()->canUndo());
        document.undoStack()->undo();
        QCOMPARE(document.design().schematic.sheets.at(0).title, QString());
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("amp.v2.ppsch"));
        QCOMPARE(document.design().project.boardFile, QStringLiteral("amp.v2.ppbrd"));
        QVERIFY(document.isModified());

        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY2(DesignFiles::read(path, onDisk, error), qPrintable(error));
        QVERIFY(onDisk == document.design());
        document.undoStack()->redo();
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("amp.v2.ppsch"));
    }

    void saveWritesOnlyWhatChanged()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString schematicPath = dir.filePath(QStringLiteral("amp.ppsch"));
        const QString boardPath = dir.filePath(QStringLiteral("amp.ppbrd"));

        Document document;
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));

        // Files the document must not touch are replaced by something it
        // would never write.
        QVERIFY(writeGarbage(boardPath));
        QVERIFY(writeGarbage(path));
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(!document.isModified());
        QVERIFY(isGarbage(boardPath));
        QVERIFY(isGarbage(path));
        Schematic schematic;
        QVERIFY2(SchematicFile::read(schematicPath, schematic, error), qPrintable(error));
        QVERIFY(schematic == document.design().schematic);

        // The board alone.
        QVERIFY(writeGarbage(schematicPath));
        document.change(QStringLiteral("Add track"), withTrack(document));
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(isGarbage(schematicPath));
        QVERIFY(isGarbage(path));
        Board board;
        QVERIFY2(BoardFile::read(boardPath, board, error), qPrintable(error));
        QVERIFY(board == document.design().board);

        // The project alone.
        QVERIFY(writeGarbage(boardPath));
        document.change(QStringLiteral("Set author"), withAuthor(document, QStringLiteral("Автор")));
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(isGarbage(schematicPath));
        QVERIFY(isGarbage(boardPath));
        Project project;
        QVERIFY2(ProjectFile::read(path, project, error), qPrintable(error));
        QVERIFY(project == document.design().project);

        // Nothing changed: nothing is written.
        QVERIFY(writeGarbage(path));
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(isGarbage(path));

        // Changed and undone: the files already hold this content.
        document.change(QStringLiteral("B"), withSheetTitle(document, QStringLiteral("B")));
        document.undoStack()->undo();
        QVERIFY(!document.isModified());
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(isGarbage(schematicPath));
    }

    void saveWritesAMissingFileAgain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString boardPath = dir.filePath(QStringLiteral("amp.ppbrd"));

        Document document;
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        QVERIFY(QFile::remove(boardPath));
        QVERIFY(QFile::remove(path));
        QVERIFY2(document.save(error), qPrintable(error));

        Design onDisk;
        QVERIFY2(DesignFiles::read(path, onDisk, error), qPrintable(error));
        QVERIFY(onDisk == document.design());
    }

    void failedSaveAsLeavesTheDocumentAtTheOldPlace()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString schematicPath = dir.filePath(QStringLiteral("amp.ppsch"));

        Document document;
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));

        // A folder that is not there: not even the first file is written.
        QVERIFY(!document.saveAs(dir.filePath(QStringLiteral("nowhere/other.ppcb")), error));
        QVERIFY(!error.isEmpty());
        QCOMPARE(document.projectPath(), path);
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("amp.ppsch"));
        QCOMPARE(document.design().project.boardFile, QStringLiteral("amp.ppbrd"));
        QVERIFY(document.isModified());

        // A folder where a folder sits in the place of the project file: the
        // schematic and the board get written, the project does not.
        const QString busy = dir.filePath(QStringLiteral("busy"));
        QVERIFY(QDir().mkpath(busy + QStringLiteral("/other.ppcb")));
        QVERIFY(!document.saveAs(busy + QStringLiteral("/other.ppcb"), error));
        QVERIFY(QFile::exists(busy + QStringLiteral("/other.ppsch")));
        QCOMPARE(document.projectPath(), path);
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("amp.ppsch"));
        // What reached the other place does not count as saved at this one.
        QVERIFY(document.isModified());
        QVERIFY2(document.save(error), qPrintable(error));
        Schematic schematic;
        QVERIFY2(SchematicFile::read(schematicPath, schematic, error), qPrintable(error));
        QCOMPARE(schematic.sheets.at(0).title, QStringLiteral("A"));

        // A project file without a name has nothing to name the others after.
        QVERIFY(!document.saveAs(dir.filePath(QStringLiteral(".ppcb")), error));
        QCOMPARE(document.projectPath(), path);
    }

    void openReplacesContentAndHistory()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        Design stored = newDesign(QStringLiteral("amp"));
        stored.schematic.sheets[0].title = QStringLiteral("Stored");
        QString error;
        QVERIFY2(DesignFiles::write(path, stored, error), qPrintable(error));

        Document document;
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QSignalSpy changed(&document, &Document::designChanged);
        QSignalSpy modified(&document, &Document::modifiedChanged);
        QVERIFY2(document.open(path, error), qPrintable(error));
        QVERIFY(document.design() == stored);
        QCOMPARE(document.projectPath(), path);
        QVERIFY(!document.isModified());
        QVERIFY(!document.undoStack()->canUndo());
        QCOMPARE(changed.count(), 1);
        QCOMPARE(modified.count(), 1);
        QCOMPARE(modified.at(0).at(0).toBool(), false);

        // startNew() forgets the file.
        document.startNew(QStringLiteral("next"));
        QVERIFY(document.projectPath().isEmpty());
        QVERIFY(!document.isModified());
    }

    void failedOpenChangesNothing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        Document document;
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        const Design before = document.design();
        QSignalSpy changed(&document, &Document::designChanged);

        QString error;
        QVERIFY(!document.open(dir.filePath(QStringLiteral("missing.ppcb")), error));
        QVERIFY(!error.isEmpty());
        QVERIFY(document.design() == before);
        QVERIFY(document.projectPath().isEmpty());
        QVERIFY(document.isModified());
        QVERIFY(document.undoStack()->canUndo());
        QCOMPARE(changed.count(), 0);
    }
};

QTEST_GUILESS_MAIN(TestDocument)
#include "test_document.moc"
