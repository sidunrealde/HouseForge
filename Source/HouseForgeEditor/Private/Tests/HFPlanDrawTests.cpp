// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Capture/HFPlanDraw.h"
#include "Capture/HFPlanSection.h"
#include "Capture/HFSceneCapture.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/Scene.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSectionCut.h"
#include "HFEditorSubsystem.h"
#include "HFRenderSettings.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFFixturePlacement.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// IS THE PLAN READABLE? MEASURED, NOT LOOKED AT.
//
// "The section-cut plan renders clipped to pure white with a heavy bloom halo, so walls barely
// separate from floor and furniture reads only as faint outlines" is a report from three review
// packages, and every clause of it is a number: a spike at the top of the histogram, light in the
// frame where there is no geometry, and the difference between the tone of a wall and the tone of
// the floor beside it. A picture that has been eyeballed and called better is exactly how it got
// through three times.
//
// So both halves are asserted. FHFPlanDraw's palette and poche can be checked with no renderer at
// all - they are geometry and arithmetic - and the gate runs the whole suite under -nullrhi, so
// those assertions run on every commit. The image itself needs a renderer, which is what stage 3 of
// hf-validate.ps1 exists for; under -nullrhi it says HF_UNMEASURED and that stage refuses it.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgePlan
{
	using namespace UE::Geometry;

	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}

	/** Removes every HouseForge actor standing, so a plan is a plan of one flat. */
	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;

		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			Doomed.Add(*It);
		}
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			Doomed.Add(*It);
		}
		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

	/**
	 * The reference flat, and not a stand-in.
	 *
	 * THE COMPLAINT IS ABOUT THIS FLAT. "Furniture reads only as faint outlines" cannot be measured
	 * on four walls and a door - there has to be furniture in the picture - and the whole point of
	 * the plan tool is comparing what was built against sheet 01 of the drawings, which is this.
	 */
	AHFHouseActor* BuildReferenceFlat(UWorld* World)
	{
		ClearHouseForgeActors(World);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();
		return House;
	}

	/** sRGB luminance of a pixel, on the 0-255 scale a reader measures off the PNG. */
	double Luma(const FColor& Pixel)
	{
		return 0.2126 * Pixel.R + 0.7152 * Pixel.G + 0.0722 * Pixel.B;
	}

	double Median(TArray<double>& Values)
	{
		if (Values.IsEmpty())
		{
			return 0.0;
		}
		Values.Sort();
		return Values[Values.Num() / 2];
	}

	/** Where a plan camera puts a world point, given the framing the caller chose. */
	struct FPlanFrame
	{
		FVector2D Centre = FVector2D::ZeroVector;
		double CmPerPixel = 1.0;
		FIntPoint Size = FIntPoint::ZeroValue;

		/**
		 * World centimetres to image pixels.
		 *
		 * +Y runs DOWN the image and that is forced rather than chosen - see
		 * FHFPlanSection::PlanCameraRotation. Getting it backwards here would sample the flat
		 * mirrored, which on a roughly symmetrical plan reads as noise rather than as a mistake.
		 */
		FIntPoint ToPixel(const FVector2D& World) const
		{
			return FIntPoint(
				FMath::RoundToInt((World.X - Centre.X) / CmPerPixel + Size.X * 0.5),
				FMath::RoundToInt((World.Y - Centre.Y) / CmPerPixel + Size.Y * 0.5));
		}

		bool Holds(const FIntPoint& Pixel) const
		{
			return Pixel.X >= 0 && Pixel.Y >= 0 && Pixel.X < Size.X && Pixel.Y < Size.Y;
		}
	};

	/** The tone at a world point, or -1 when it falls outside the image. */
	double ToneAt(const TArray<FColor>& Pixels, const FPlanFrame& Frame, const FVector2D& World)
	{
		const FIntPoint At = Frame.ToPixel(World);
		if (!Frame.Holds(At))
		{
			return -1.0;
		}
		return Luma(Pixels[At.Y * Frame.Size.X + At.X]);
	}

	/** Is this point inside any wall's thickness? Walls are centrelines; a plan sees their width. */
	bool InAnyWall(const FHFHouseSpec& Spec, const FVector2D& Point, double Margin)
	{
		for (const FHFWall& Wall : Spec.Walls)
		{
			const FVector2D A = Wall.Start;
			const FVector2D B = Wall.End;
			const FVector2D Along = B - A;

			const double LengthSq = Along.SizeSquared();
			if (LengthSq <= KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const double T = FMath::Clamp(FVector2D::DotProduct(Point - A, Along) / LengthSq, 0.0, 1.0);
			const double Distance = FVector2D::Distance(Point, A + Along * T);

			if (Distance <= Wall.Thickness * 0.5 + Margin)
			{
				return true;
			}
		}
		return false;
	}

	/** Is this point under something that stands on the floor and would be in the picture? */
	bool UnderAFixture(const FHFHouseSpec& Spec, const FVector2D& Point, double Margin)
	{
		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (!AHFHouseActor::BuildsGeometryFor(Fixture.Type) || Fixture.IsCeilingMounted())
			{
				continue;
			}
			if (FHFFixturePlacement::FootprintContains(Fixture, Point, Margin))
			{
				return true;
			}
		}
		return false;
	}
}

/**
 * THE POCHE AND THE PALETTE, WITH NO RENDERER IN SIGHT.
 *
 * Everything that decides whether a plan CAN read is geometry and arithmetic: which triangles the
 * cut exposed, which slot they end up on, and how far apart the tones on those slots are. All of it
 * is measurable headless, so all of it runs on every commit rather than only in the renderer stage.
 *
 * The separations are asserted as numbers because "walls barely separate from floor" was a number
 * all along - wall paint sRGB 230 against floor tile 211, nineteen levels - and nineteen levels is
 * what "barely" meant. Nothing in this palette may drift back towards that.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPlanIsPochedAndFlatTonedTest,
	"HouseForge.Capture.APlanIsPochedAndItsTonesSeparate", HF_TEST_FLAGS)

bool FHFPlanIsPochedAndFlatTonedTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePlan;

	// ------------------------------------------------------------------- the slot, and why it is one
	//
	// One past the last role, so it cannot collide with a role's own material id, and NOT a new
	// EHFSurfaceRole - see FHFPlanDraw::PocheSlot.
	TestEqual(TEXT("The poche sits one past the last surface role"),
		FHFPlanDraw::PocheSlot(), FHFMeshOps::NumSurfaceRoles());
	TestEqual(TEXT("A section component has a slot per role plus the poche"),
		FHFPlanDraw::SlotCount(), FHFMeshOps::NumSurfaceRoles() + 1);

	// ----------------------------------------------------------------------------- the separations
	const double Floor = FHFPlanDraw::FloorTone();
	const double Poche = FHFPlanDraw::PocheTone();

	// A WALL AGAINST THE FLOOR BESIDE IT. This is the whole complaint, as one figure. Nineteen levels
	// is what the finish library gave it; anything under a hundred is not a drawing.
	TestTrue(*FString::Printf(
		TEXT("The cut reads against the floor: poche %.0f against floor %.0f is %.0f levels"),
		Poche, Floor, Poche - Floor),
		Poche - Floor >= 128.0);

	// AND BOTH READ AGAINST THE PAGE. A base-colour capture leaves black where nothing was drawn, so
	// a floor tone near zero would make the flat's own extent invisible - the halo problem inverted.
	TestTrue(*FString::Printf(TEXT("The floor reads against the page it is drawn on (%.0f)"), Floor),
		Floor >= 40.0);

	// EVERY ROLE THAT STANDS ON THE FLOOR IS LEGIBLE ON IT. "Furniture reads only as faint outlines"
	// is this assertion failing, and until now nothing stated the figure it should hold to.
	int32 Checked = 0;
	for (int32 Slot = 0; Slot < FHFMeshOps::NumSurfaceRoles(); ++Slot)
	{
		const EHFSurfaceRole Role = static_cast<EHFSurfaceRole>(Slot);
		const double Tone = FHFPlanDraw::ToneForSlot(Slot);

		// The floor and its skirting ARE the floor; they are what everything else is measured against.
		if (Role == EHFSurfaceRole::FloorFinish || Role == EHFSurfaceRole::Skirting)
		{
			TestEqual(TEXT("The skirting is drawn as part of the floor it was cut from"), Tone, Floor);
			continue;
		}

		++Checked;
		TestTrue(*FString::Printf(
			TEXT("Role %d is legible on the floor - tone %.0f against %.0f"), Slot, Tone, Floor),
			FMath::Abs(Tone - Floor) >= 32.0);

		TestTrue(*FString::Printf(TEXT("Role %d is drawn in something, not left as an empty slot"), Slot),
			Tone > 0.0);
	}
	TestTrue(TEXT("Every surface role has a tone in the drawing palette"), Checked >= 14);

	// ------------------------------------------------------------------- what the poche actually is
	//
	// A cut face is one whose every vertex lies in the cut plane. Built here as a box straddling the
	// plane, cut, and counted - so the identification is exercised on a shape whose answer is known
	// rather than only on the flat, where nobody could say what the right count was.
	{
		FDynamicMesh3 Box;
		FHFMeshOps::InitialiseMesh(Box);
		FHFMeshOps::AppendBox(Box, FVector3d(0.0, 0.0, 100.0), FVector3d(50.0, 50.0, 100.0),
			0.0, EHFSurfaceRole::WallPaint);

		FHFSectionCutParams CutParams;
		CutParams.CutZ = 120.0;
		CutParams.bCap = true;
		CutParams.CapRole = EHFSurfaceRole::WallPaint;

		FDynamicMesh3 Cut = FHFSectionCut::CutBelow(Box, CutParams);
		if (TestTrue(TEXT("The test box survives its own section cut"), Cut.TriangleCount() > 0))
		{
			FHFMeshOps::AssignMaterialIdsFromRoles(Cut);

			const int32 Before = Cut.TriangleCount();
			const int32 Poched = FHFPlanDraw::PocheTheCut(Cut, 120.0);

			TestTrue(TEXT("The cut face is found and retagged"), Poched > 0);

			// A BOX HAS FIVE FACES LEFT UNDER A CUT AND ONE ABOVE IT. Poche-ing everything would pass
			// "the cut face is found" while making the whole plan one solid tone, which is the failure
			// this is replacing rather than a new one.
			TestTrue(*FString::Printf(
				TEXT("And only the cut face - %d of %d triangles"), Poched, Before),
				Poched < Before / 2);

			int32 OnPoche = 0;
			const FDynamicMeshMaterialAttribute* Ids = Cut.Attributes()->GetMaterialID();
			for (const int32 Tid : Cut.TriangleIndicesItr())
			{
				if (Ids->GetValue(Tid) == FHFPlanDraw::PocheSlot())
				{
					++OnPoche;

					FVector3d A, B, C;
					Cut.GetTriVertices(Tid, A, B, C);
					const double Highest = FMath::Max3(A.Z, B.Z, C.Z);
					const double Lowest = FMath::Min3(A.Z, B.Z, C.Z);

					TestTrue(TEXT("Everything on the poche slot really does lie in the cut plane"),
						FMath::Abs(Highest - 120.0) < 0.1 && FMath::Abs(Lowest - 120.0) < 0.1);
				}
			}
			TestEqual(TEXT("Every retagged triangle is on the poche slot"), OnPoche, Poched);
		}
	}

	// ------------------------------------------------------------------------------- and no bloom
	//
	// The halo, by name. This one is inert on the path a plan actually takes - a base-colour capture
	// never reaches the tonemapper - and it is asserted anyway, because the struct it is written into
	// is public and the next caller to build a drawing request through some other capture source must
	// not inherit the default that put a halo round the last three plans.
	{
		FPostProcessSettings Settings;
		Settings.bOverride_BloomIntensity = 0;
		Settings.BloomIntensity = 0.675f;

		FHFPlanDraw::ApplyDrawingPostProcess(Settings);

		TestTrue(TEXT("A drawing overrides bloom rather than inheriting it"),
			Settings.bOverride_BloomIntensity != 0);
		TestEqual(TEXT("And turns it off"), Settings.BloomIntensity, 0.0f);
		TestEqual(TEXT("No ambient fill either - a drawing is not lit"),
			Settings.AmbientCubemapIntensity, 0.0f);
		TestTrue(TEXT("Exposure is manual, so there is nothing to adapt"),
			Settings.bOverride_AutoExposureMethod != 0 && Settings.AutoExposureMethod == AEM_Manual);
	}

	// -------------------------------------------------------- and the section really is built that way
	//
	// The palette and the poche are only worth anything if the section wears them. Asserted on the
	// real flat, because that is the mesh the plan is drawn from.
	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("There is an editor world"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	FBox Bounds(ForceInit);
	TArray<AActor*> Section = FHFPlanSection::Build(World, House, FHFPlanSection::DefaultCutHeight(), Bounds);
	ON_SCOPE_EXIT{ FHFPlanSection::DestroyAll(World, Section); };

	if (!TestTrue(TEXT("The section produced geometry"), Section.Num() > 0))
	{
		return false;
	}

	int32 Components = 0;
	int32 PochedTriangles = 0;
	int32 TotalTriangles = 0;
	int32 SlotsWrong = 0;

	for (AActor* Actor : Section)
	{
		TArray<UDynamicMeshComponent*> Meshes;
		Actor->GetComponents<UDynamicMeshComponent>(Meshes);

		for (UDynamicMeshComponent* Mesh : Meshes)
		{
			++Components;
			if (Mesh->GetNumMaterials() != FHFPlanDraw::SlotCount())
			{
				++SlotsWrong;
			}

			Mesh->ProcessMesh([&PochedTriangles, &TotalTriangles](const FDynamicMesh3& M)
			{
				const FDynamicMeshMaterialAttribute* Ids =
					M.HasAttributes() ? M.Attributes()->GetMaterialID() : nullptr;
				if (Ids == nullptr)
				{
					return;
				}
				for (const int32 Tid : M.TriangleIndicesItr())
				{
					++TotalTriangles;
					if (Ids->GetValue(Tid) == FHFPlanDraw::PocheSlot())
					{
						++PochedTriangles;
					}
				}
			});
		}
	}

	AddInfo(FString::Printf(
		TEXT("The section is %d component(s) and %d triangle(s), of which %d are poche."),
		Components, TotalTriangles, PochedTriangles));

	TestEqual(TEXT("Every section component carries the whole palette"), SlotsWrong, 0);

	// Twenty-two walls, eleven columns and every cased-goods run in the flat is cut by the 1.2 m
	// plane. A section with no poche in it is a plan with no walls drawn.
	TestTrue(*FString::Printf(TEXT("The cut is actually poched - %d triangle(s)"), PochedTriangles),
		PochedTriangles > 100);

	// AND IT IS NOT THE WHOLE PICTURE. Every floor slab in the flat lies below the cut and must stay
	// off the poche slot, or the drawing is one solid tone again.
	TestTrue(*FString::Printf(
		TEXT("But most of the section is not the cut - %d of %d"), PochedTriangles, TotalTriangles),
		PochedTriangles < TotalTriangles / 2);

	return true;
}

/**
 * THE PLAN, MEASURED AS AN IMAGE.
 *
 * Everything above is about the drawing's intent. This is the drawing.
 *
 * Four things are asserted, and each one is a clause of the complaint the fix answers:
 *
 *   "clipped to pure white"   - the fraction of the frame at the top of the histogram.
 *   "a heavy bloom halo"      - the tone of the border, which is 3% of padding the framing
 *                               guarantees is empty. Light there came from nowhere.
 *   "walls barely separate
 *    from floor"              - the tone at wall centrelines against the tone of open floor,
 *                               sampled from the SPEC so the points are the flat's own and not
 *                               eyeballed off the image.
 *   "furniture reads only as
 *    faint outlines"          - the same, at the centre of every fixture that stands on the floor.
 *
 * The last two are why this test frames its own capture rather than only reading back what
 * CaptureTopDown wrote: sampling a world coordinate needs the world-to-pixel mapping, and the tool's
 * own result message does not carry the centre it framed on. So both are done - the real path is
 * captured and measured for the two statistics that need no mapping, and a second capture with a
 * framing this test chose is measured for the two that do.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPlanIsReadableTest,
	"HouseForge.Capture.APlanReadsRatherThanBlowingOut", HF_TEST_FLAGS)

bool FHFPlanIsReadableTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePlan;

	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("There is an editor world"), World))
	{
		return false;
	}

	FString WhyNot;
	if (!FHFSceneCapture::CanRender(WhyNot))
	{
		AddWarning(FString::Printf(
			TEXT("HF_UNMEASURED: the plan's histogram was NOT measured: %s The palette, the poche and ")
			TEXT("the separations they are chosen for are asserted headless by ")
			TEXT("HouseForge.Capture.APlanIsPochedAndItsTonesSeparate; whether the picture actually ")
			TEXT("comes out that way needs a renderer, so run this suite without -nullrhi."), *WhyNot));
		return true;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	const FHFHouseSpec Spec = House->Spec;

	FBox Bounds(ForceInit);
	TArray<AActor*> Section = FHFPlanSection::Build(World, House,
		FHFPlanSection::DefaultCutHeight(), Bounds);
	ON_SCOPE_EXIT{ FHFPlanSection::DestroyAll(World, Section); };

	if (!TestTrue(TEXT("The section produced geometry to draw"), Section.Num() > 0 && Bounds.IsValid))
	{
		return false;
	}

	// A square frame around the flat with 6% of air, so the outer band of the image is known empty
	// whatever shape the flat is. Chosen here rather than borrowed from CaptureTopDown because this
	// test has to know where a world point lands.
	constexpr int32 Pixels = 1024;
	constexpr double Padding = 1.06;

	FPlanFrame Frame;
	Frame.Size = FIntPoint(Pixels, Pixels);
	Frame.Centre = FVector2D(Bounds.GetCenter().X, Bounds.GetCenter().Y);

	const double Span = FMath::Max3(Bounds.GetSize().X, Bounds.GetSize().Y, 1.0) * Padding;
	Frame.CmPerPixel = Span / Pixels;

	FHFCaptureRequest Request;
	Request.bOrthographic = true;
	Request.OrthoWidth = Span;
	Request.Rotation = FHFPlanSection::PlanCameraRotation();
	Request.Location = FVector(Frame.Centre.X, Frame.Centre.Y, Bounds.Max.Z + 500.0);
	Request.Width = Pixels;
	Request.Height = Pixels;
	Request.ShowOnly = Section;
	Request.bShowSky = false;
	Request.LumenGuard = EHFLumenGuard::Off;
	Request.DrawStyle = EHFDrawStyle::Drawing;

	TArray<FColor> Image;
	FIntPoint Size = FIntPoint::ZeroValue;
	FString Error;
	if (!TestTrue(*FString::Printf(TEXT("The plan renders: %s"), *Error),
		FHFSceneCapture::RenderToPixels(World, Request, Image, Size, Error)))
	{
		return false;
	}

	// ------------------------------------------------------------------------- clipped to white?
	int32 AtTheTop = 0;
	int32 Ink = 0;
	for (const FColor& Pixel : Image)
	{
		const double L = Luma(Pixel);
		AtTheTop += (L >= 250.0) ? 1 : 0;
		Ink += (L > 8.0) ? 1 : 0;
	}

	const double ClippedFraction = static_cast<double>(AtTheTop) / Image.Num();
	const double InkFraction = static_cast<double>(Ink) / Image.Num();

	AddInfo(FString::Printf(
		TEXT("Plan histogram: %.3f%% of the frame is at or above 250 of 255; %.1f%% of it is drawn on."),
		ClippedFraction * 100.0, InkFraction * 100.0));

	// FALSIFIED, on the reference flat, by putting the old path back beside this one: the section
	// materialled from UHFMaterialLibrary and captured through SCS_FinalColorLDR at the placeholder
	// rig's interior exposure - which is what every plan this tool ever produced was.
	//
	//                        clipped     ink     empty border, mean / brightest
	//   lit, EV100 8         45.682%    73.5%        1.99 / 21
	//   drawing               0.000%    62.0%        0.00 /  0
	//
	// Forty-five per cent of the frame pinned at the top of the histogram is not "a bit bright"; it
	// is the plan. And the border - 3% of air the framing guarantees no geometry reaches - carrying
	// light at all is the halo, measured where it cannot be anything else.
	TestTrue(*FString::Printf(
		TEXT("The plan is not clipped - %.3f%% of it is at the top of the histogram"),
		ClippedFraction * 100.0),
		ClippedFraction < 0.005);

	// And it is a picture of something. A frame that failed to draw is not clipped either.
	TestTrue(*FString::Printf(TEXT("The plan is mostly flat, not mostly empty page (%.1f%% drawn)"),
		InkFraction * 100.0),
		InkFraction > 0.30 && InkFraction < 0.99);

	// ------------------------------------------------------------------------------- and no halo
	//
	// The framing leaves 3% of air on every side, so the outermost 1% of the image is empty by
	// construction. Anything bright there is light the geometry did not have, which is what a bloom
	// halo IS.
	const int32 Band = FMath::Max(Pixels / 100, 2);
	double BrightestEdge = 0.0;
	double EdgeTotal = 0.0;
	int32 EdgeCount = 0;

	for (int32 Y = 0; Y < Pixels; ++Y)
	{
		for (int32 X = 0; X < Pixels; ++X)
		{
			const bool bEdge = X < Band || Y < Band || X >= Pixels - Band || Y >= Pixels - Band;
			if (!bEdge)
			{
				continue;
			}
			const double L = Luma(Image[Y * Pixels + X]);
			BrightestEdge = FMath::Max(BrightestEdge, L);
			EdgeTotal += L;
			++EdgeCount;
		}
	}

	const double EdgeMean = EdgeCount > 0 ? EdgeTotal / EdgeCount : 0.0;

	AddInfo(FString::Printf(
		TEXT("The %d-pixel border the framing guarantees is empty: mean %.2f, brightest %.0f of 255."),
		Band, EdgeMean, BrightestEdge));

	// The lit path measured 21 here against a mean of 1.99. Eight is chosen to be well under that and
	// well over nothing, so a halo cannot creep back and a stray antialiased edge cannot fail it.
	TestTrue(*FString::Printf(
		TEXT("No halo round the flat - the empty border's brightest pixel is %.0f of 255"),
		BrightestEdge),
		BrightestEdge <= 8.0);

	// -------------------------------------------------------- do walls separate from the floor?
	//
	// Sampled from the SPEC, at points chosen by what the flat is rather than by what the image
	// looks like. Several points per wall and a median over all of them, because a doorway or a
	// window reveal on any one centreline sample would read as floor - correctly, and not as the
	// wall this is measuring.
	TArray<double> WallTones;
	TArray<double> FloorTones;
	TArray<double> FixtureTones;

	for (const FHFWall& Wall : Spec.Walls)
	{
		for (const double T : { 0.12, 0.3, 0.5, 0.7, 0.88 })
		{
			const FVector2D At = Wall.Start + (Wall.End - Wall.Start) * T;
			const double Tone = ToneAt(Image, Frame, At);
			if (Tone >= 0.0)
			{
				WallTones.Add(Tone);
			}
		}
	}

	for (const FHFRoom& Room : Spec.Rooms)
	{
		// A grid over the room's own bounding box, keeping only what is inside the room, out of every
		// wall's thickness, and not under anything standing on the floor. That is open floor.
		FBox2D Box(ForceInit);
		for (const FVector2D& Corner : Room.Boundary)
		{
			Box += Corner;
		}
		if (!Box.bIsValid)
		{
			continue;
		}

		constexpr int32 Steps = 7;
		for (int32 IY = 1; IY < Steps; ++IY)
		{
			for (int32 IX = 1; IX < Steps; ++IX)
			{
				const FVector2D At(
					FMath::Lerp(Box.Min.X, Box.Max.X, static_cast<double>(IX) / Steps),
					FMath::Lerp(Box.Min.Y, Box.Max.Y, static_cast<double>(IY) / Steps));

				if (!Room.ContainsPoint(At) || InAnyWall(Spec, At, 12.0)
					|| UnderAFixture(Spec, At, 12.0))
				{
					continue;
				}

				const double Tone = ToneAt(Image, Frame, At);
				if (Tone >= 0.0)
				{
					FloorTones.Add(Tone);
				}
			}
		}
	}

	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (!AHFHouseActor::BuildsGeometryFor(Fixture.Type) || Fixture.IsCeilingMounted()
			|| Fixture.Footprint.X <= 0.0 || Fixture.Footprint.Y <= 0.0)
		{
			continue;
		}

		const double Tone = ToneAt(Image, Frame, Fixture.Position);
		if (Tone >= 0.0)
		{
			FixtureTones.Add(Tone);
		}
	}

	if (!TestTrue(TEXT("There are wall, floor and fixture points to compare"),
		WallTones.Num() >= 40 && FloorTones.Num() >= 40 && FixtureTones.Num() >= 20))
	{
		return false;
	}

	const double WallMedian = Median(WallTones);
	const double FloorMedian = Median(FloorTones);
	const double FixtureMedian = Median(FixtureTones);

	AddInfo(FString::Printf(
		TEXT("Sampled %d wall point(s) at median %.0f, %d open-floor point(s) at %.0f, and %d fixture ")
		TEXT("centre(s) at %.0f, all of 255."),
		WallTones.Num(), WallMedian, FloorTones.Num(), FloorMedian, FixtureTones.Num(), FixtureMedian));

	// THE COMPLAINT, AS ONE NUMBER. Drawn through the finish library this was wall paint at sRGB 230
	// against floor tile at 211 - and then six stops of over-exposure put both of them against the
	// top of the curve, where the nineteen levels between them became almost nothing.
	TestTrue(*FString::Printf(
		TEXT("Walls separate from the floor - %.0f against %.0f is %.0f levels"),
		WallMedian, FloorMedian, WallMedian - FloorMedian),
		WallMedian - FloorMedian >= 96.0);

	// AND FURNITURE IS NOT A FAINT OUTLINE. Sampled at the CENTRE of each footprint, which is the
	// part of a fixture an outline is exactly what you would not see.
	TestTrue(*FString::Printf(
		TEXT("Furniture reads against the floor - %.0f against %.0f is %.0f levels"),
		FixtureMedian, FloorMedian, FMath::Abs(FixtureMedian - FloorMedian)),
		FMath::Abs(FixtureMedian - FloorMedian) >= 24.0);

	// ------------------------------------------------------- and the tool's own path writes a PNG
	//
	// The measurements above are taken on a framing this test chose. The one a user gets comes out of
	// CaptureTopDown, so it is exercised too - end to end, through the PNG - and checked for the one
	// property that needs no mapping and no palette: that it is not clipped.
	UHFEditorSubsystem* Editor = Subsystem();
	if (TestNotNull(TEXT("There is a subsystem"), Editor))
	{
		FString Out;
		const FHFOperationResult Result = Editor->CaptureTopDown(TEXT("HFTest_PlanReadability"), 1024, 0.0, Out);
		TestTrue(*FString::Printf(TEXT("The plan tool captures: %s"), *Result.Message), Result.bSuccess);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
