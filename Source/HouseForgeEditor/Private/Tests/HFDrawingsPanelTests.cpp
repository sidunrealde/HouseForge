// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Editor.h"
#include "HFEditorSubsystem.h"
#include "ImageCore.h"
#include "ImageUtils.h"
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

/**
 * A CROP IS THE SHEET'S OWN PIXELS, NOT A PICTURE OF THEM.
 *
 * That is the whole point of the tool and the only property worth pinning. A crop that resampled,
 * or that came back scaled to some tidy size, would add nothing: the detail it exists to recover
 * is exactly the detail resampling destroys. So this builds a sheet whose every pixel encodes its
 * own coordinate, crops a rectangle out of the middle, and checks the pixels that come back are
 * the ones that went in - which fails for a resample, an off-by-one origin, and a flip.
 *
 * The tool exists because a real generation ran into the wall it removes. Claude said "let me crop
 * the plan sheets so I can read the detail properly", had no way to, and fell back to reading the
 * elevations - and the flat came out with its furniture arranged from inference rather than from
 * the plan.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCropDrawingTest,
	"HouseForge.Drawings.ACropKeepsTheSheetsOwnPixels", HF_TEST_FLAGS)

bool FHFCropDrawingTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	if (!TestNotNull(TEXT("The editor subsystem is available"), Editor))
	{
		return false;
	}

	// A sheet where every pixel says where it is: red carries x, green carries y. Any resample
	// blends neighbours and breaks that; any origin slip shifts it by a known amount.
	constexpr int32 SheetW = 240;
	constexpr int32 SheetH = 160;

	FImage Sheet;
	Sheet.Init(SheetW, SheetH, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> Pixels = Sheet.AsBGRA8();

	for (int32 Y = 0; Y < SheetH; ++Y)
	{
		for (int32 X = 0; X < SheetW; ++X)
		{
			Pixels[static_cast<int64>(Y) * SheetW + X] =
				FColor(static_cast<uint8>(X), static_cast<uint8>(Y), 0, 255);
		}
	}

	const FString Source = FPaths::Combine(FPaths::AutomationTransientDir(), TEXT("HFCropSheet.png"));
	IFileManager::Get().MakeDirectory(*FPaths::GetPath(Source), true);

	if (!TestTrue(TEXT("The test sheet was written"),
		FImageUtils::SaveImageByExtension(*Source, Sheet)))
	{
		return false;
	}

	// The middle quarter: x 60..180, y 40..120.
	FString CropPath;
	const FHFOperationResult Result = Editor->CropDrawing(Source, 0.25f, 0.25f, 0.5f, 0.5f, CropPath);

	if (!TestTrue(FString::Printf(TEXT("The crop succeeded: %s"), *Result.Message), Result.bSuccess))
	{
		return false;
	}

	FImage Crop;
	if (!TestTrue(TEXT("The crop can be read back"), FImageUtils::LoadImage(*CropPath, Crop)))
	{
		return false;
	}

	TestEqual(TEXT("The crop is the requested width in real pixels"), Crop.SizeX, SheetW / 2);
	TestEqual(TEXT("The crop is the requested height in real pixels"), Crop.SizeY, SheetH / 2);

	// AND THE PIXELS ARE THE ORIGINALS. Corners and centre: enough to catch a flip, which samples
	// the right VALUES from the wrong places and would satisfy any size assertion.
	FImage Read;
	Crop.CopyTo(Read, ERawImageFormat::BGRA8, EGammaSpace::sRGB);
	const TArrayView64<FColor> Out = Read.AsBGRA8();

	const int32 X0 = SheetW / 4;
	const int32 Y0 = SheetH / 4;

	struct FProbe { int32 X; int32 Y; const TCHAR* What; };
	const FProbe Probes[] = {
		{ 0, 0, TEXT("top-left") },
		{ Crop.SizeX - 1, 0, TEXT("top-right") },
		{ 0, Crop.SizeY - 1, TEXT("bottom-left") },
		{ Crop.SizeX - 1, Crop.SizeY - 1, TEXT("bottom-right") },
		{ Crop.SizeX / 2, Crop.SizeY / 2, TEXT("centre") },
	};

	for (const FProbe& Probe : Probes)
	{
		const FColor Got = Out[static_cast<int64>(Probe.Y) * Crop.SizeX + Probe.X];

		TestEqual(FString::Printf(TEXT("The %s pixel's column came from x = %d"),
			Probe.What, X0 + Probe.X), static_cast<int32>(Got.R), X0 + Probe.X);
		TestEqual(FString::Printf(TEXT("The %s pixel's row came from y = %d"),
			Probe.What, Y0 + Probe.Y), static_cast<int32>(Got.G), Y0 + Probe.Y);
	}

	// A rectangle with no area is refused rather than writing an empty file nobody can read.
	FString Ignored;
	TestFalse(TEXT("A zero-width crop is refused"),
		Editor->CropDrawing(Source, 0.25f, 0.25f, 0.0f, 0.5f, Ignored).bSuccess);

	// Running off the edge is clamped, not refused: an estimate off a downscaled sheet is meant to
	// be approximate, and the part that exists is still worth having.
	FString Clamped;
	const FHFOperationResult Overrun = Editor->CropDrawing(Source, 0.8f, 0.8f, 0.5f, 0.5f, Clamped);
	TestTrue(FString::Printf(TEXT("A crop running past the edge is clamped: %s"), *Overrun.Message),
		Overrun.bSuccess);

	IFileManager::Get().Delete(*Source, false, true, true);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
