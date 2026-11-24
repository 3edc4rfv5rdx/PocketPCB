#include "document.h"
#include "libraryfile.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
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

    // An empty date of the title block is the day of the last save; a new
    // project has none, a project opened has that of its newest file.
    void titleBlockShowsTheDateOfTheLastSave()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString today = QDate::currentDate().toString(Qt::ISODate);

        Document document;
        QVERIFY(!document.savedDate().isValid());
        QVERIFY(document.titleBlockShown().date.isEmpty());
        QSignalSpy dated(&document, &Document::savedDateChanged);
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        QCOMPARE(document.savedDate(), QDate::currentDate());
        QCOMPARE(dated.count(), 1);
        QCOMPARE(document.titleBlockShown().date, today);

        Design design = document.design();
        design.project.titleBlock.date = QStringLiteral("1 May");
        document.change(QStringLiteral("date"), design);
        QCOMPARE(document.titleBlockShown().date, QStringLiteral("1 May"));

        Document opened;
        QVERIFY2(opened.open(path, error), qPrintable(error));
        QCOMPARE(opened.savedDate(), QDate::currentDate());
        QCOMPARE(opened.titleBlockShown().date, today);
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

    void saveWritesTheLibraryAndNeverRemovesItsFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString first = dir.filePath(QStringLiteral("library/symbols/first.json"));
        const QString second = dir.filePath(QStringLiteral("library/symbols/second.json"));

        Document document;
        QString error;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        // An empty library makes no folder.
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("library"))));

        Design design = document.design();
        design.library.symbols.insert(QStringLiteral("first"), Symbol());
        design.library.symbols.insert(QStringLiteral("second"), Symbol());
        document.change(QStringLiteral("Add symbols"), design);
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(QFile::exists(first));
        QVERIFY(QFile::exists(second));

        // Only what changed is written again.
        QVERIFY(writeGarbage(first));
        Symbol changed;
        changed.showPinNames = !changed.showPinNames;
        design = document.design();
        design.library.symbols.insert(QStringLiteral("second"), changed);
        document.change(QStringLiteral("Change symbol"), design);
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(isGarbage(first));
        LibraryItem item;
        QVERIFY2(LibraryFile::read(second, item, error), qPrintable(error));
        QVERIFY(item.symbol == changed);

        // Dropped from the library, kept on disk, and the project is saved.
        document.undoStack()->undo();
        document.undoStack()->undo();
        QVERIFY(document.design().library.symbols.isEmpty());
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(!document.isModified());
        QVERIFY(isGarbage(first));
        QVERIFY(QFile::exists(second));
    }

    void saveKeepsBackupCopies()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString copy1 = dir.filePath(QStringLiteral("backup/amp.ppsch.1.bak"));
        const QString copy2 = dir.filePath(QStringLiteral("backup/amp.ppsch.2.bak"));
        const QString copy3 = dir.filePath(QStringLiteral("backup/amp.ppsch.3.bak"));

        Document document;
        document.setBackupCount(2);
        QString error;
        // Nothing was there before: nothing to keep.
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("backup"))));

        // Each save keeps the schematic as it was; the board did not change.
        const auto saveTitle = [&](const QString &title) {
            document.change(title, withSheetTitle(document, title));
            QString saveError;
            QVERIFY2(document.save(saveError), qPrintable(saveError));
        };
        const auto titleIn = [](const QString &file) {
            Schematic schematic;
            QString readError;
            if (!SchematicFile::read(file, schematic, readError))
                return readError;
            return schematic.sheets.at(0).title;
        };
        saveTitle(QStringLiteral("A"));
        QCOMPARE(titleIn(copy1), QString());
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("backup/amp.ppbrd.1.bak"))));
        saveTitle(QStringLiteral("B"));
        QCOMPARE(titleIn(copy1), QStringLiteral("A"));
        QCOMPARE(titleIn(copy2), QString());
        saveTitle(QStringLiteral("C"));
        QCOMPARE(titleIn(copy1), QStringLiteral("B"));
        QCOMPARE(titleIn(copy2), QStringLiteral("A"));
        QVERIFY(!QFileInfo::exists(copy3));

        // Fewer kept: the extra copies go at the next save.
        document.setBackupCount(1);
        saveTitle(QStringLiteral("D"));
        QCOMPARE(titleIn(copy1), QStringLiteral("C"));
        QVERIFY(!QFileInfo::exists(copy2));

        // None kept: the copies there stay as they are.
        document.setBackupCount(0);
        saveTitle(QStringLiteral("E"));
        QCOMPARE(titleIn(copy1), QStringLiteral("C"));
    }

    void autosaveKeepsACopyUntilSaved()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString copy = dir.filePath(QStringLiteral("autosave/amp.ppcb"));

        // A project without a file has no place for a copy.
        Document document;
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QString error;
        QVERIFY2(document.autosave(error), qPrintable(error));

        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        // Nothing changed: nothing written.
        QVERIFY2(document.autosave(error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(copy));
        QVERIFY(!Document::hasAutosave(path));

        // A change goes to the copy, not to the project's own files.
        QVERIFY(writeGarbage(dir.filePath(QStringLiteral("amp.ppsch"))));
        document.change(QStringLiteral("B"), withSheetTitle(document, QStringLiteral("B")));
        QVERIFY2(document.autosave(error), qPrintable(error));
        QVERIFY(isGarbage(dir.filePath(QStringLiteral("amp.ppsch"))));
        QVERIFY(Document::hasAutosave(path));
        Design copied;
        QVERIFY2(DesignFiles::read(copy, copied, error), qPrintable(error));
        QVERIFY(copied == document.design());
        QVERIFY(document.isModified());

        // An up-to-date copy is not written again.
        QVERIFY(writeGarbage(copy));
        QVERIFY2(document.autosave(error), qPrintable(error));
        QVERIFY(isGarbage(copy));

        // Saving drops it.
        QVERIFY2(document.save(error), qPrintable(error));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("autosave"))));
        QVERIFY(!Document::hasAutosave(path));
    }

    void autosaveIsRestored()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        QString error;
        {
            // A session that ends without saving.
            Document crashed;
            QVERIFY2(crashed.saveAs(path, error), qPrintable(error));
            crashed.change(QStringLiteral("A"), withSheetTitle(crashed, QStringLiteral("Lost")));
            QVERIFY2(crashed.autosave(error), qPrintable(error));
        }
        QVERIFY(Document::hasAutosave(path));

        Document document;
        QVERIFY2(document.open(path, error), qPrintable(error));
        QCOMPARE(document.design().schematic.sheets.at(0).title, QString());
        QVERIFY2(document.restoreAutosave(error), qPrintable(error));
        QCOMPARE(document.design().schematic.sheets.at(0).title, QStringLiteral("Lost"));
        QVERIFY(document.isModified());
        QVERIFY(!document.undoStack()->canUndo());

        // Dropped, it is not offered again.
        document.dropAutosave();
        QVERIFY(!Document::hasAutosave(path));
        QVERIFY(!document.restoreAutosave(error));
        QVERIFY(!error.isEmpty());
    }

    void droppingTheAutosaveKeepsWhatItDidNotWrite()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QString foreign = dir.filePath(QStringLiteral("autosave/notes.txt"));
        QString error;
        Document document;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("autosave")));
        QVERIFY(writeGarbage(foreign));

        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QVERIFY2(document.autosave(error), qPrintable(error));
        QVERIFY(Document::hasAutosave(path));
        QVERIFY2(document.save(error), qPrintable(error));

        QVERIFY(!Document::hasAutosave(path));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("autosave/amp.ppsch"))));
        QVERIFY(!QFileInfo::exists(dir.filePath(QStringLiteral("autosave/amp.ppbrd"))));
        QVERIFY(isGarbage(foreign));
    }

    void droppingTheAutosaveKeepsForeignLibraryFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath(QStringLiteral("amp.ppcb"));
        const QDir library(dir.filePath(QStringLiteral("autosave/library")));
        QString error;
        Document document;
        QVERIFY2(document.saveAs(path, error), qPrintable(error));
        // Not library files at all, and one that is but no autosave wrote.
        QStringList foreign;
        for (const QString &kind :
             {QStringLiteral("symbols"), QStringLiteral("footprints"), QStringLiteral("parts")}) {
            QVERIFY(library.mkpath(kind));
            foreign.append(library.filePath(kind + QStringLiteral("/keep.json")));
            QVERIFY(writeGarbage(foreign.last()));
        }
        LibraryItem other;
        other.kind = LibraryKind::Symbol;
        other.symbol.showPinNames = !other.symbol.showPinNames;
        const QString otherPath = library.filePath(QStringLiteral("symbols/other.json"));
        QVERIFY2(LibraryFile::write(otherPath, other, error), qPrintable(error));

        Design design = document.design();
        design.library.symbols.insert(QStringLiteral("own"), Symbol());
        document.change(QStringLiteral("Add symbol"), design);
        QVERIFY2(document.autosave(error), qPrintable(error));
        const QString own = library.filePath(QStringLiteral("symbols/own.json"));
        QVERIFY(QFile::exists(own));
        QVERIFY2(document.save(error), qPrintable(error));

        QVERIFY(!Document::hasAutosave(path));
        QVERIFY(!QFile::exists(own));
        for (const QString &file : std::as_const(foreign))
            QVERIFY2(isGarbage(file), qPrintable(file));
        LibraryItem item;
        QVERIFY2(LibraryFile::read(otherPath, item, error), qPrintable(error));
        QVERIFY(item.symbol == other.symbol);
    }

    void droppingOneAutosaveKeepsAnotherProjects()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString pathA = dir.filePath(QStringLiteral("a.ppcb"));
        const QString pathB = dir.filePath(QStringLiteral("b.ppcb"));
        QString error;
        Document a;
        Document b;
        QVERIFY2(a.saveAs(pathA, error), qPrintable(error));
        QVERIFY2(b.saveAs(pathB, error), qPrintable(error));
        a.change(QStringLiteral("A"), withSheetTitle(a, QStringLiteral("A")));
        b.change(QStringLiteral("B"), withSheetTitle(b, QStringLiteral("B")));
        QVERIFY2(a.autosave(error), qPrintable(error));
        QVERIFY2(b.autosave(error), qPrintable(error));

        a.dropAutosave();
        QVERIFY(!Document::hasAutosave(pathA));
        QVERIFY(Document::hasAutosave(pathB));
        Design copied;
        QVERIFY2(DesignFiles::read(DesignFiles::autosaveProjectPath(pathB), copied, error),
                 qPrintable(error));
        QVERIFY(copied == b.design());
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

    void projectNamesAreChecked()
    {
        for (const char *good : {"amp", "amp v2", "amp.v2", "усилитель", "con1", "a"})
            QVERIFY2(Document::nameProblem(QString::fromUtf8(good)).isEmpty(), good);
        for (const char *bad : {"", "a/b", "a\\b", "a:b", "a*b", "a?b", "a\"b", "a<b", "a>b", "a|b",
                                "a\tb", ".amp", ".", "..", " amp", "amp ", "amp.", "con", "Con.amp",
                                "nul", "COM1", "lpt9"})
            QVERIFY2(!Document::nameProblem(QString::fromUtf8(bad)).isEmpty(), bad);
    }

    void newProjectFolderProblems()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        // Nothing there, and an empty folder: fine.
        QVERIFY(Document::newFolderProblem(dir.path(), QStringLiteral("amp")).isEmpty());
        QVERIFY(QDir(dir.path()).mkdir(QStringLiteral("empty")));
        QVERIFY(Document::newFolderProblem(dir.path(), QStringLiteral("empty")).isEmpty());
        // A parent that is not there yet is made on saving.
        QVERIFY(Document::newFolderProblem(dir.filePath(QStringLiteral("a/b")),
                                           QStringLiteral("amp")).isEmpty());

        QVERIFY(!Document::newFolderProblem(dir.path(), QStringLiteral("a/b")).isEmpty());
        QVERIFY(!Document::newFolderProblem(QString(), QStringLiteral("amp")).isEmpty());
        QVERIFY(!Document::newFolderProblem(QStringLiteral("relative"), QStringLiteral("amp"))
                     .isEmpty());
        // A file in the place of the folder.
        QVERIFY(writeGarbage(dir.filePath(QStringLiteral("file"))));
        QVERIFY(!Document::newFolderProblem(dir.path(), QStringLiteral("file")).isEmpty());
        // A folder with anything in it, a hidden file too.
        QVERIFY(QDir(dir.path()).mkpath(QStringLiteral("hidden")));
        QVERIFY(writeGarbage(dir.filePath(QStringLiteral("hidden/.keep"))));
        QVERIFY(!Document::newFolderProblem(dir.path(), QStringLiteral("hidden")).isEmpty());
    }

    void saveAsNewFolderMakesTheProjectFolder()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString parent = dir.filePath(QStringLiteral("projects"));
        const QString folder = parent + QStringLiteral("/amp");

        Document document;
        QString error;
        QVERIFY2(document.saveAsNewFolder(parent, QStringLiteral("amp"), error), qPrintable(error));
        QCOMPARE(document.projectPath(), folder + QStringLiteral("/amp.ppcb"));
        QCOMPARE(document.projectPath(),
                 Document::projectPathInFolder(parent, QStringLiteral("amp")));
        QCOMPARE(QDir(folder).entryList(QDir::Files, QDir::Name),
                 QStringList({QStringLiteral("amp.ppbrd"), QStringLiteral("amp.ppcb"),
                              QStringLiteral("amp.ppsch")}));
        QVERIFY(!document.isModified());

        // The folder is there now: the same place is refused and nothing in
        // it is touched.
        QVERIFY(writeGarbage(folder + QStringLiteral("/amp.ppsch")));
        document.change(QStringLiteral("A"), withSheetTitle(document, QStringLiteral("A")));
        QVERIFY(!document.saveAsNewFolder(parent, QStringLiteral("amp"), error));
        QVERIFY(!error.isEmpty());
        QVERIFY(isGarbage(folder + QStringLiteral("/amp.ppsch")));
        QCOMPARE(document.projectPath(), folder + QStringLiteral("/amp.ppcb"));
        QVERIFY(document.isModified());

        // A bad name makes no folder.
        QVERIFY(!document.saveAsNewFolder(parent, QStringLiteral("a|b"), error));
        QVERIFY(!QFileInfo::exists(parent + QStringLiteral("/a|b")));

        // An empty folder of that name is taken.
        QVERIFY(QDir(parent).mkdir(QStringLiteral("v2")));
        QVERIFY2(document.saveAsNewFolder(parent, QStringLiteral("v2"), error), qPrintable(error));
        QCOMPARE(document.projectPath(), parent + QStringLiteral("/v2/v2.ppcb"));
        QCOMPARE(document.design().project.schematicFile, QStringLiteral("v2.ppsch"));
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
