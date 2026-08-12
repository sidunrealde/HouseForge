// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "HFEditorSubsystem.h"
#include "Input/DragAndDrop.h"
#include "Misc/AutomationTest.h"
#include "UI/SHFDrawingsPanel.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** A real external file drag, the way the OS delivers one to Slate. */
	TSharedPtr<FDragDropOperation> FileDrag(const TArray<FString>& Files)
	{
		return FExternalDragOperation::NewFiles(Files);
	}
}

/**
 * WHAT THE PLUGIN WILL READ, IN ONE PLACE.
 *
 * This predicate is consulted by three callers - the import, the file dialog's filter, and the
 * panel's drop target. Pinning the accepted set here is what stops one of them quietly drifting:
 * a format that becomes droppable but not importable reads to the user as a broken drop rather
 * than as an unsupported format.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFReadableDrawingTest,
	"HouseForge.Drawings.WhatCountsAsAReadableDrawing", HF_TEST_FLAGS)

bool FHFReadableDrawingTest::RunTest(const FString& Parameters)
{
	// The four the importer actually handles. PDF is here because ImportDrawings rasterises it to
	// one PNG per page - from the caller's side it is readable, and it is how AutoCAD sets arrive.
	for (const TCHAR* Path : { TEXT("plan.png"), TEXT("plan.jpg"), TEXT("plan.jpeg"), TEXT("set.pdf") })
	{
		TestTrue(FString::Printf(TEXT("'%s' is readable"), Path),
			UHFEditorSubsystem::IsReadableDrawing(Path));
	}

	// Case comes off the filesystem however the user's tools left it, so a sheet exported as .PNG
	// by one program and .png by another must both import.
	for (const TCHAR* Path : { TEXT("PLAN.PNG"), TEXT("Plan.JpEg"), TEXT("SET.PDF") })
	{
		TestTrue(FString::Printf(TEXT("'%s' is readable whatever its case"), Path),
			UHFEditorSubsystem::IsReadableDrawing(Path));
	}

	// .dwg is the one that matters: it is what an architect will reach for first, and HouseForge
	// cannot read it. Refusing it at the drop is the difference between a "no" cursor and a dialog.
	for (const TCHAR* Path : { TEXT("plan.dwg"), TEXT("plan.dxf"), TEXT("notes.txt"),
		TEXT("sheet.tiff"), TEXT("archive.zip"), TEXT("noextension") })
	{
		TestFalse(FString::Printf(TEXT("'%s' is not readable"), Path),
			UHFEditorSubsystem::IsReadableDrawing(Path));
	}

	return true;
}

/**
 * A DRAG IS ACCEPTED IF *ANY* FILE IN IT IS READABLE - NOT IF EVERY FILE IS.
 *
 * This is the assertion with teeth. The natural-looking implementation checks every file, and it
 * passes every hand test a developer runs, because a developer drags exactly the sheets they mean.
 * An artist drags the whole issued folder, which carries a title-block .dwg, a .txt of revisions,
 * and a Thumbs.db - and an every-file rule refuses the entire drop with no explanation.
 *
 * ImportDrawings already reports what it skipped, so accepting the mixed drop loses nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDropAcceptanceTest,
	"HouseForge.Drawings.AMixedFolderIsStillAcceptedForDropping", HF_TEST_FLAGS)

bool FHFDropAcceptanceTest::RunTest(const FString& Parameters)
{
	TestTrue(TEXT("A folder of sheets is accepted"),
		SHFDrawingsPanel::CanAcceptDrag(FileDrag({ TEXT("a.png"), TEXT("b.png") })));

	// The case the every-file rule gets wrong, and the reason this test exists.
	TestTrue(TEXT("A real issued folder - sheets plus the junk that ships with them - is accepted"),
		SHFDrawingsPanel::CanAcceptDrag(FileDrag({
			TEXT("A-101 Plan.pdf"), TEXT("titleblock.dwg"), TEXT("revisions.txt"), TEXT("Thumbs.db") })));

	TestTrue(TEXT("One readable file among many unreadable ones is still a yes"),
		SHFDrawingsPanel::CanAcceptDrag(FileDrag({
			TEXT("a.dwg"), TEXT("b.dwg"), TEXT("c.dwg"), TEXT("plan.png") })));

	// And the other half: nothing readable must be refused, or the "no" cursor never appears and
	// the artist learns nothing until a dialog tells them the drop did nothing.
	TestFalse(TEXT("A folder with no readable drawing is refused"),
		SHFDrawingsPanel::CanAcceptDrag(FileDrag({ TEXT("plan.dwg"), TEXT("notes.txt") })));

	// Not an afterthought: an every-file loop over an empty list returns true by vacuous truth, so
	// the wrong implementation accepts a drag carrying nothing at all. Falsified - this assertion
	// fails alongside the two above when the rule is inverted.
	TestFalse(TEXT("An empty file list is refused"),
		SHFDrawingsPanel::CanAcceptDrag(FileDrag({})));

	// A text drag - dragging selected words out of another application - is not a file drag at all.
	TestFalse(TEXT("A text drag is refused"),
		SHFDrawingsPanel::CanAcceptDrag(FExternalDragOperation::NewText(TEXT("plan.png"))));

	TestFalse(TEXT("A null operation is refused"),
		SHFDrawingsPanel::CanAcceptDrag(nullptr));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
