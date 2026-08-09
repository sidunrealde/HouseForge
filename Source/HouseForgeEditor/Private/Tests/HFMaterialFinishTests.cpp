// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Capture/HFSceneCapture.h"
#include "Capture/HFViewingLight.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/World.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFRenderFinish.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/Material.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// Proving that a millimetre is a millimetre.
//
// Everything upstream of the sampler is asserted without a renderer, by
// HouseForge.Materials.TilingMillimetresMatchTheUnwrap: that UV0 really is world-scale on generator
// output, and that the material's UVWorldSizeCm agrees with the unwrap's TexelSizeCm. What that
// cannot reach is the last link - whether the graph actually turns those numbers into a pattern of
// the size it claims - and that link is only observable in pixels.
//
// So this file renders a floor and MEASURES it. The gate runs -nullrhi and cannot, which is stated
// as a warning rather than passed over in silence: a test that quietly asserts nothing is worse than
// one that says what it did not do.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	/** A floor big enough to hold several tiles with room to spare around the measured window. */
	AHFRoomActor* SpawnFloor(UWorld* World)
	{
		AHFRoomActor* Actor = World->SpawnActor<AHFRoomActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->Room.Id = TEXT("R_Tiling");
		Actor->Room.Boundary = { FVector2D(0, 0), FVector2D(600, 0), FVector2D(600, 600), FVector2D(0, 600) };
		Actor->Room.FloorZ = 0.0;
		Actor->Room.CeilingHeight = 300.0;
		Actor->Room.SkirtingHeight = 10.0;
		Actor->bGenerateCeilingSlab = false;
		Actor->Regenerate();
		return Actor;
	}

	/**
	 * A high-contrast tile material off the real master, so the measurement reads the graph under
	 * test and not the shipped floor's own subtlety.
	 *
	 * Deliberately NOT the MI_HF_FloorFinish asset. Editing that would dirty a committed asset from a
	 * test, and its 2 mm joint in a soft beige is tuned to be believable rather than measurable. What
	 * is under test is the ARITHMETIC - millimetres in, world size out - so the pattern is turned up
	 * until it is unambiguous and every other source of variation is turned off.
	 */
	UMaterialInstanceDynamic* MakeMeasurableTile(UObject* Outer, double TilingMM)
	{
		UMaterialInterface* Shipped = UHFMaterialLibrary::Get()->ResolveMaterial(EHFSurfaceRole::FloorFinish);
		UMaterial* Master = Shipped ? Shipped->GetMaterial() : nullptr;
		if (Master == nullptr)
		{
			return nullptr;
		}

		UMaterialInstanceDynamic* Instance = UMaterialInstanceDynamic::Create(Master, Outer);
		if (Instance == nullptr)
		{
			return nullptr;
		}

		Instance->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor::White);
		Instance->SetVectorParameterValue(TEXT("GroutColor"), FLinearColor::Black);

		Instance->SetScalarParameterValue(TEXT("TilingMM"), static_cast<float>(TilingMM));
		// A 20 mm joint rather than a real 2 mm one: at the resolution this renders at, a two
		// millimetre line is under a pixel wide and would be measured as its own antialiasing.
		Instance->SetScalarParameterValue(TEXT("GroutWidthMM"), 20.0f);
		Instance->SetScalarParameterValue(TEXT("TilingRotationDegrees"), 0.0f);
		Instance->SetScalarParameterValue(TEXT("Roughness"), 0.9f);
		Instance->SetScalarParameterValue(TEXT("GroutRoughness"), 0.9f);
		Instance->SetScalarParameterValue(TEXT("Metallic"), 0.0f);
		Instance->SetScalarParameterValue(TEXT("CoatWeight"), 0.0f);

		// Everything that would put variation on top of the grid, silenced: what is being measured is
		// the position of a joint, and macro drift and shade batching are noise against that signal.
		Instance->SetScalarParameterValue(TEXT("MacroRoughnessAmount"), 0.0f);
		Instance->SetScalarParameterValue(TEXT("MacroAlbedoAmount"), 0.0f);
		Instance->SetScalarParameterValue(TEXT("TileShadeVariation"), 0.0f);
		Instance->SetScalarParameterValue(TEXT("DetailBumpStrength"), 0.0f);

		return Instance;
	}

	/**
	 * The average brightness of each image column, over a band across the middle of the frame.
	 *
	 * Averaging down the column rather than reading one scanline is what makes this robust: the
	 * camera looks straight down a world axis and the joints run exactly along image columns, so
	 * every row carries the same signal and averaging them cancels the renderer's noise without
	 * moving a single edge.
	 */
	TArray<double> ColumnProfile(const TArray<FColor>& Pixels, FIntPoint Size)
	{
		TArray<double> Profile;
		Profile.Init(0.0, Size.X);

		const int32 First = Size.Y * 2 / 5;
		const int32 Last = Size.Y * 3 / 5;
		const int32 Rows = FMath::Max(Last - First, 1);

		for (int32 Y = First; Y < Last; ++Y)
		{
			for (int32 X = 0; X < Size.X; ++X)
			{
				const FColor& C = Pixels[Y * Size.X + X];
				Profile[X] += (C.R + C.G + C.B) / 3.0;
			}
		}

		for (double& Value : Profile)
		{
			Value /= Rows;
		}
		return Profile;
	}

	/**
	 * The centre of every dark band in a profile, in pixels.
	 *
	 * THE THRESHOLD IS HALFWAY BETWEEN THE TILE AND THE JOINT, and taking it anywhere else is what
	 * this got wrong first time. The tile level is the profile's MEDIAN - most columns are tile, so
	 * the median is the field whatever the joints do - and the joint level is its minimum. Sitting
	 * the cut midway between them is the only placement that does not care how deep a joint happens
	 * to read.
	 *
	 * Measured, not assumed: with a cut taken 35% up from the minimum instead, one joint in the frame
	 * dipped to 183.7 against a threshold of 182.4 and went undetected while its neighbours dipped to
	 * 175.0 - a 1.3-level miss that turned a perfectly correct 60.00 cm period into a measured
	 * 79.77 cm, because two gaps had silently become one. The joints were all there; only the cut was
	 * wrong. A band's depth varies by a few levels with how its edges land on the pixel grid, and no
	 * absolute cut survives that.
	 *
	 * Bands touching either end of the frame are dropped: a joint the frame cuts in half reports its
	 * centre half a band inwards, which is a real bias on a real measurement.
	 */
	TArray<double> DarkBandCentres(const TArray<double>& Profile, double& OutContrast)
	{
		TArray<double> Centres;
		if (Profile.Num() < 8)
		{
			OutContrast = 0.0;
			return Centres;
		}

		TArray<double> Sorted = Profile;
		Sorted.Sort();
		const double Field = Sorted[Sorted.Num() / 2];
		const double Trough = Sorted[0];

		OutContrast = Field - Trough;
		if (OutContrast <= 0.0)
		{
			return Centres;
		}

		const double Threshold = Field - OutContrast * 0.5;

		int32 RunStart = INDEX_NONE;
		for (int32 X = 0; X <= Profile.Num(); ++X)
		{
			const bool bDark = X < Profile.Num() && Profile[X] < Threshold;
			if (bDark && RunStart == INDEX_NONE)
			{
				RunStart = X;
			}
			else if (!bDark && RunStart != INDEX_NONE)
			{
				const int32 RunEnd = X - 1;
				if (RunStart > 0 && RunEnd < Profile.Num() - 1)
				{
					Centres.Add((RunStart + RunEnd) * 0.5);
				}
				RunStart = INDEX_NONE;
			}
		}
		return Centres;
	}

	/** Every vertex position, triangle group and UV element - the whole of what a mesh IS. */
	struct FMeshFingerprint
	{
		int32 Vertices = 0;
		int32 Triangles = 0;
		TArray<FVector3d> Positions;
		TArray<int32> Groups;
		TArray<FVector2f> UVs;

		bool operator==(const FMeshFingerprint& Other) const
		{
			return Vertices == Other.Vertices && Triangles == Other.Triangles
				&& Positions == Other.Positions && Groups == Other.Groups && UVs == Other.UVs;
		}
	};

	FMeshFingerprint Fingerprint(const UDynamicMeshComponent* Component)
	{
		FMeshFingerprint Print;
		Component->ProcessMesh([&Print](const FDynamicMesh3& Mesh)
		{
			Print.Vertices = Mesh.VertexCount();
			Print.Triangles = Mesh.TriangleCount();

			for (const int32 Vid : Mesh.VertexIndicesItr())
			{
				Print.Positions.Add(Mesh.GetVertex(Vid));
			}
			for (const int32 Tid : Mesh.TriangleIndicesItr())
			{
				Print.Groups.Add(Mesh.GetTriangleGroup(Tid));
			}

			const FDynamicMeshUVOverlay* UVs = Mesh.HasAttributes() ? Mesh.Attributes()->PrimaryUV() : nullptr;
			if (UVs != nullptr)
			{
				for (const int32 Eid : UVs->ElementIndicesItr())
				{
					Print.UVs.Add(UVs->GetElement(Eid));
				}
			}
		});
		return Print;
	}
}

/**
 * THE MILLIMETRE PROMISE, MEASURED IN PIXELS.
 *
 * A tiling of 600 mm has to produce a feature that is 600 mm across in the world, and the only proof
 * of that is to render a known surface and measure it. Everything short of that - the parameter
 * value, the repeat arithmetic, the world-scale unwrap - is asserted elsewhere and all of it can be
 * individually correct while the graph still multiplies by ten somewhere.
 *
 * Measured two ways, because either alone is weaker than it looks:
 *
 *  - ABSOLUTELY, against the camera's own scale. This is the claim: 600 mm means 600 mm.
 *  - RELATIVELY, by halving the tiling and checking the period halves. This catches the case where a
 *    constant error and a camera error cancel, which an absolute check on one value cannot.
 *
 * Under -nullrhi there is no renderer and no pixels, which is how the gate runs. That is reported as
 * a warning naming what was not measured, rather than returning true and looking like a pass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTilingIsInMillimetresTest,
	"HouseForge.Materials.TilingIsInMillimetres", HF_TEST_FLAGS)

bool FHFTilingIsInMillimetresTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FString WhyNot;
	if (!FHFSceneCapture::CanRender(WhyNot))
	{
		// HF_UNMEASURED is a sentinel the gate greps for, not decoration. hf-validate.ps1 runs a
		// second, renderer-enabled stage precisely so this branch is not the only one anybody ever
		// executes, and it FAILS if this token appears in that stage's report. A test that quietly
		// asserts nothing in the only invocation anybody runs is the same class of defect as a
		// capture that renders the wrong material without saying so.
		AddWarning(FString::Printf(
			TEXT("HF_UNMEASURED: tiling was NOT measured in pixels: %s Everything up to the sampler ")
			TEXT("is covered by HouseForge.Materials.TilingMillimetresMatchTheUnwrap; the pattern ")
			TEXT("itself needs a renderer, so run this suite without -nullrhi to assert it."), *WhyNot));
		return true;
	}

	AHFRoomActor* Floor = SpawnFloor(World);
	if (!TestNotNull(TEXT("A floor spawns"), Floor))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Floor)) { Floor->Destroy(); } };

	FHFViewingLight::EnsureIn(World);

	UDynamicMeshComponent* Component = Floor->GetMeshComponent();
	if (!TestNotNull(TEXT("The floor has a mesh component"), Component))
	{
		return false;
	}

	// A window over the middle of the floor, well inside the skirting, looking straight down a world
	// axis so the joints land along image columns.
	//
	// 250 cm rather than a round 240: at 240 the joints of a 600 mm tile land exactly on the frame
	// edges, so the outermost two are cut in half by the frame and report their centres half a band
	// inwards. Choosing a width that is NOT a whole number of tiles puts every joint in open frame,
	// and incidentally proves the pattern is anchored to the world rather than to the camera.
	const double OrthoWidthCm = 250.0;
	const int32 Pixels = 1024;
	const double CmPerPixel = OrthoWidthCm / Pixels;

	FHFCaptureRequest Request;
	Request.Location = FVector(300.0, 300.0, 400.0);
	Request.Rotation = FRotator(-90.0, 0.0, 0.0);
	Request.bOrthographic = true;
	Request.OrthoWidth = OrthoWidthCm;
	Request.Width = Pixels;
	Request.Height = Pixels;
	Request.ShowOnly = { Floor };
	Request.bShowSky = false;

	const int32 FloorSlot = FHFMeshOps::MaterialIdForRole(EHFSurfaceRole::FloorFinish);

	// Measures the world period of the joint pattern at a given tiling, in centimetres.
	const auto MeasurePeriodCm = [&](double TilingMM, double& OutPeriodCm) -> bool
	{
		UMaterialInstanceDynamic* Tile = MakeMeasurableTile(Component, TilingMM);
		if (!TestNotNull(TEXT("A measurable tile material is created off the master"), Tile))
		{
			return false;
		}
		Component->SetMaterial(FloorSlot, Tile);

		TArray<FColor> Image;
		FIntPoint Size = FIntPoint::ZeroValue;
		FString Error;
		if (!TestTrue(*FString::Printf(TEXT("A floor at %.0f mm renders: %s"), TilingMM, *Error),
			FHFSceneCapture::RenderToPixels(World, Request, Image, Size, Error)))
		{
			return false;
		}

		double Contrast = 0.0;
		const TArray<double> Centres = DarkBandCentres(ColumnProfile(Image, Size), Contrast);

		// A flat frame means the grid never reached the pixels at all - a material that failed to
		// draw its joint would otherwise be measured as "no error" rather than as no pattern.
		if (!TestTrue(*FString::Printf(
			TEXT("The %.0f mm grid is actually visible (contrast %.1f of 255)"), TilingMM, Contrast),
			Contrast > 12.0))
		{
			return false;
		}

		if (!TestTrue(*FString::Printf(
			TEXT("At %.0f mm there are several joints across %.0f cm to measure between (found %d)"),
			TilingMM, OrthoWidthCm, Centres.Num()), Centres.Num() >= 3))
		{
			return false;
		}

		// Averaged over every gap rather than taken from the first: the endpoints of the frame can
		// clip a joint, and the average of the interior spacings is the period.
		double Total = 0.0;
		for (int32 i = 1; i < Centres.Num(); ++i)
		{
			Total += Centres[i] - Centres[i - 1];
		}
		OutPeriodCm = (Total / (Centres.Num() - 1)) * CmPerPixel;

		// Evenly spaced, or it is not a tiling. A pattern that drifts across the frame would still
		// produce a plausible average while being visibly wrong.
		for (int32 i = 1; i < Centres.Num(); ++i)
		{
			const double GapCm = (Centres[i] - Centres[i - 1]) * CmPerPixel;
			TestTrue(*FString::Printf(
				TEXT("Joint %d sits one full tile from the last (%.2f cm against %.2f cm)"),
				i, GapCm, OutPeriodCm), FMath::Abs(GapCm - OutPeriodCm) < 0.5);
		}
		return true;
	};

	// ---- the claim: 600 mm of tiling is 600 mm of floor -----------------------------------------
	double SixHundredCm = 0.0;
	if (MeasurePeriodCm(600.0, SixHundredCm))
	{
		AddInfo(FString::Printf(TEXT("600 mm tiling measured %.2f cm on the floor."), SixHundredCm));

		// Two millimetres either way, which is under a pixel at this scale. A joint is an antialiased
		// band eight pixels wide, so its centre is good to a fraction of a pixel and no better -
		// tightening this further would be asserting the estimator rather than the material. It is
		// still two orders of magnitude finer than any unit mistake could be: a centimetre-for-
		// millimetre slip would read 6 cm and an inch-for-centimetre one 152 cm.
		TestTrue(*FString::Printf(
			TEXT("A tiling of 600 mm measures 600 mm in world space (measured %.2f cm)"), SixHundredCm),
			FMath::Abs(SixHundredCm - 60.0) < 0.2);
	}

	// ---- and it scales, so a constant error cannot hide in a camera error ------------------------
	double ThreeHundredCm = 0.0;
	if (MeasurePeriodCm(300.0, ThreeHundredCm))
	{
		AddInfo(FString::Printf(TEXT("300 mm tiling measured %.2f cm on the floor."), ThreeHundredCm));

		TestTrue(*FString::Printf(
			TEXT("A tiling of 300 mm measures 300 mm in world space (measured %.2f cm)"), ThreeHundredCm),
			FMath::Abs(ThreeHundredCm - 30.0) < 0.2);

		if (SixHundredCm > 0.0)
		{
			TestTrue(*FString::Printf(TEXT("Halving the tiling halves the tile (%.2f cm then %.2f cm)"),
				SixHundredCm, ThreeHundredCm),
				FMath::Abs(SixHundredCm / ThreeHundredCm - 2.0) < 0.02);
		}
	}

	return true;
}

/**
 * RE-MATERIALLING MUST NOT REGENERATE GEOMETRY.
 *
 * Changing a colour must not destroy a hand edit, and the reason it cannot is structural rather
 * than careful: every path from a material to a surface goes through the COMPONENT's slot table, and
 * nothing in UHFMaterialLibrary or in a material instance can reach a vertex. This asserts that
 * structure holds, on the case where getting it wrong is unrecoverable - an element an artist has
 * modelled on top of.
 *
 * Compared as a fingerprint of the whole mesh rather than as a triangle count, because the failure
 * being guarded against is a REGENERATION, which would produce the same counts from the same
 * parameters and differ only in that the artist's edits were gone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFReMaterialisingKeepsGeometryTest,
	"HouseForge.Materials.ReMaterialisingDoesNotTouchGeometry", HF_TEST_FLAGS)

bool FHFReMaterialisingKeepsGeometryTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFRoomActor* Room = SpawnFloor(World);
	if (!TestNotNull(TEXT("A room spawns"), Room))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Room)) { Room->Destroy(); } };

	UDynamicMeshComponent* Component = Room->GetMeshComponent();
	if (!TestNotNull(TEXT("The room has a mesh component"), Component))
	{
		return false;
	}

	// AN ELEMENT SOMEBODY HAS MODELLED ON. The mesh is deliberately made to differ from what the
	// generator would produce, so a regeneration is detectable rather than idempotent.
	Room->bArtistEdited = true;
	Component->EditMesh([](FDynamicMesh3& Mesh)
	{
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(Vid);
			Mesh.SetVertex(Vid, P + FVector3d(0.0, 0.0, 0.137));
			break;
		}
	});

	const FMeshFingerprint Before = Fingerprint(Component);
	if (!TestTrue(TEXT("There is a mesh to protect"), Before.Triangles > 0))
	{
		return false;
	}

	// ---- 1: assigning the role materials again ---------------------------------------------------
	UHFMaterialLibrary::Get()->ApplyTo(Component);
	TestTrue(TEXT("Assigning role materials leaves the mesh exactly as it was"),
		Fingerprint(Component) == Before);

	// ---- 2: changing what a surface looks like ---------------------------------------------------
	//
	// Every kind of parameter the panel will drive: a colour, a finish scalar, and the tiling itself
	// - the one most likely to be mistaken for something geometric, since it is measured in
	// millimetres of the real world.
	UMaterialInstanceDynamic* Recoloured = MakeMeasurableTile(Component, 400.0);
	if (TestNotNull(TEXT("A material instance is created"), Recoloured))
	{
		Recoloured->SetVectorParameterValue(TEXT("BaseColor"), FLinearColor(0.9f, 0.2f, 0.1f));
		Recoloured->SetScalarParameterValue(TEXT("Roughness"), 0.15f);
		Recoloured->SetScalarParameterValue(TEXT("TilingMM"), 250.0f);
		Recoloured->SetScalarParameterValue(TEXT("GroutWidthMM"), 6.0f);

		Component->SetMaterial(FHFMeshOps::MaterialIdForRole(EHFSurfaceRole::FloorFinish), Recoloured);

		TestTrue(TEXT("Changing colour, roughness and tiling leaves the mesh exactly as it was"),
			Fingerprint(Component) == Before);
	}

	// ---- 3: and the hand edit is still there -----------------------------------------------------
	TestTrue(TEXT("The element is still flagged as hand-edited"), Room->bArtistEdited);
	TestTrue(TEXT("A re-materialled element still opts out of regeneration"),
		Room->ShouldPreserveOnRebuild());

	return true;
}

/**
 * THE SAME PROMISE, THROUGH THE MECHANISM THAT WILL ACTUALLY BE USED.
 *
 * The test above proves a component's slot table cannot reach a vertex. This proves the LIBRARY
 * cannot either, which is a different claim and now the load-bearing one: from this milestone on,
 * changing a finish means editing UHFMaterialLibrary and pushing it, not swapping a material on a
 * component. A push writes a shared asset that 155 components already point at - so it is exactly
 * the operation that touches everything at once, and exactly the one worth proving touches no
 * geometry at all.
 *
 * THE POLYGROUPS ARE ASSERTED EXPLICITLY, not merely covered by the fingerprint. Every triangle
 * carries a surface-role polygroup and the material panel targets faces by role; a material pass
 * that renumbered them would silently undo the thing it exists to serve, and it would look like a
 * success - the flat would still render, in the wrong finishes, with no way left to fix it. So the
 * group set is compared as a set of role ids rather than as an opaque array.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFinishPushKeepsArtistEditsTest,
	"HouseForge.Materials.ChangingAFinishLeavesArtistEditsAlone", HF_TEST_FLAGS)

bool FHFFinishPushKeepsArtistEditsTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFRoomActor* Room = SpawnFloor(World);
	if (!TestNotNull(TEXT("A room spawns"), Room))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(Room)) { Room->Destroy(); } };

	UDynamicMeshComponent* Component = Room->GetMeshComponent();
	if (!TestNotNull(TEXT("The room has a mesh component"), Component))
	{
		return false;
	}

	// Hand-edited, and made to differ from generator output so a regeneration would be visible
	// rather than idempotent.
	Room->bArtistEdited = true;
	Component->EditMesh([](FDynamicMesh3& Mesh)
	{
		for (const int32 Vid : Mesh.VertexIndicesItr())
		{
			Mesh.SetVertex(Vid, Mesh.GetVertex(Vid) + FVector3d(0.0, 0.0, 0.211));
			break;
		}
	});

	const FMeshFingerprint Before = Fingerprint(Component);
	if (!TestTrue(TEXT("There is a mesh to protect"), Before.Triangles > 0))
	{
		return false;
	}

	// The roles this element is made of, as ids, before anything is re-materialled.
	TSet<int32> RolesBefore(Before.Groups);
	TestTrue(TEXT("The element carries surface-role polygroups to begin with"), RolesBefore.Num() > 0);

	const int32 SlotsBefore = Component->GetNumMaterials();

	// ---- a library edit, pushed --------------------------------------------------------------
	//
	// A library of this test's own, so nothing outside it sees the edit; and every role pushed, not
	// only the floor, because the failure being guarded against would not be selective.
	UHFMaterialLibrary* Edited = NewObject<UHFMaterialLibrary>();
	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		FHFSurfaceFinish& Finish = Edited->Finishes.FindOrAdd(static_cast<EHFSurfaceRole>(Index));
		Finish.BaseColor = FLinearColor(0.9f, 0.1f, 0.4f, 1.0f);
		Finish.Roughness = 0.19f;
		Finish.TilingMM = 137.0f;
		Finish.GroutWidthMM = 9.0f;
	}

	ON_SCOPE_EXIT
	{
		// The instances are shared assets, so put the shipped finishes back before leaving.
		UHFMaterialLibrary* Pristine = NewObject<UHFMaterialLibrary>();
		Pristine->PushAllFinishes(EHFMaterialPush::Commit);
	};

	Edited->PushAllFinishes(EHFMaterialPush::Interactive);
	TestTrue(TEXT("A dragged finish leaves the mesh exactly as it was"),
		Fingerprint(Component) == Before);

	Edited->PushAllFinishes(EHFMaterialPush::Commit);
	TestTrue(TEXT("A committed finish leaves the mesh exactly as it was"),
		Fingerprint(Component) == Before);

	Edited->ApplyTo(Component);
	TestTrue(TEXT("Re-assigning the whole set leaves the mesh exactly as it was"),
		Fingerprint(Component) == Before);

	// ---- and specifically: the roles are still the roles ---------------------------------------
	const TSet<int32> RolesAfter(Fingerprint(Component).Groups);
	TestTrue(TEXT("Every surface-role polygroup survived the material pass, with its own id"),
		RolesBefore.Difference(RolesAfter).IsEmpty() && RolesAfter.Difference(RolesBefore).IsEmpty());

	TestEqual(TEXT("The slot table is still one slot per role"),
		Component->GetNumMaterials(), SlotsBefore);
	TestEqual(TEXT("...which is one slot per role"), SlotsBefore, FHFMeshOps::NumSurfaceRoles());

	// ---- and the hand edit is untouched ---------------------------------------------------------
	TestTrue(TEXT("The element is still flagged as hand-edited"), Room->bArtistEdited);
	TestTrue(TEXT("A re-finished element still opts out of regeneration"),
		Room->ShouldPreserveOnRebuild());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
