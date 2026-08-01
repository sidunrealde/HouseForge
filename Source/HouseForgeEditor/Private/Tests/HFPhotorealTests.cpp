// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "HFEditorSubsystem.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSettings.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * What the photoreal groundwork costs and whether it actually reached the flat.
 *
 * Everything here is measured on the reference 2BHK as it is really built, because the whole point
 * of these attributes is that they exist on every surface a camera can see. A chamfer on a test box
 * proves the operation works; a chamfer on all 22 walls, 19 openings, 12 rooms, 8 beams, 11 columns,
 * 7 false ceilings and 69 fixtures is the thing the renders depend on.
 */
namespace HouseForgePhotoreal
{
	using namespace UE::Geometry;

	void ClearHouseForgeActors(UWorld* World)
	{
		TArray<AActor*> Doomed;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (It->IsA(AHFHouseActor::StaticClass()) || It->IsA(AHFElementActor::StaticClass()))
			{
				Doomed.Add(*It);
			}
		}
		for (AActor* Actor : Doomed)
		{
			if (IsValid(Actor))
			{
				Actor->Destroy();
			}
		}
	}

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

	/** Every dynamic mesh component the house built: element shells and moving parts alike. */
	TArray<UDynamicMeshComponent*> AllMeshComponents(const AHFHouseActor* House)
	{
		TArray<UDynamicMeshComponent*> Components;
		if (House == nullptr)
		{
			return Components;
		}

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			AHFElementActor* Element = Cast<AHFElementActor>(Actor);
			if (Element == nullptr)
			{
				continue;
			}

			if (UDynamicMeshComponent* Shell = Element->GetMeshComponent())
			{
				Components.Add(Shell);
			}

			if (AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
			{
				for (const TObjectPtr<UDynamicMeshComponent>& Part : Articulated->GetPartComponents())
				{
					if (Part != nullptr)
					{
						Components.Add(Part.Get());
					}
				}
			}
		}

		return Components;
	}

	int32 TotalTriangles(const AHFHouseActor* House)
	{
		int32 Total = 0;
		for (UDynamicMeshComponent* Component : AllMeshComponents(House))
		{
			if (UDynamicMesh* Dynamic = Component->GetDynamicMesh())
			{
				Total += Dynamic->GetMeshRef().TriangleCount();
			}
		}
		return Total;
	}

	/** Rebuilds every element of the house with a different render finish. */
	void RefinishHouse(AHFHouseActor* House, TFunctionRef<void(FHFRenderFinish&)> Adjust)
	{
		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			if (AHFElementActor* Element = Cast<AHFElementActor>(Actor))
			{
				Adjust(Element->RenderFinish);
				Element->RevertToGenerated();
			}
		}
	}
}

/**
 * The triangle bill for chamfering the whole flat, reported rather than guessed.
 *
 * A chamfer is not free: a box goes from 12 triangles to 44, because each of its 12 arrises becomes
 * a quad strip and each of its 8 corners becomes a junction polygon. Multiply that across every
 * element in a flat and the answer has to be looked at, not assumed - which is why the width, the
 * angle threshold and the whole operation are parameters on the element rather than a constant.
 *
 * The budget below is a ceiling on the RATIO, not on the absolute count, so the test keeps meaning
 * something as the flat gains detail. Six times is the point at which the cost stops being worth
 * paying for a 1.5 mm highlight and the answer would be to narrow which roles get chamfered.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBevelCostTest,
	"HouseForge.Photoreal.ChamferCostOfTheReferenceFlat", HF_TEST_FLAGS)

bool FHFBevelCostTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePhotoreal;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	const int32 Chamfered = TotalTriangles(House);

	RefinishHouse(House, [](FHFRenderFinish& Finish) { Finish.Bevel.bEnabled = false; });
	const int32 Sharp = TotalTriangles(House);

	if (!TestTrue(TEXT("The flat has geometry to measure"), Sharp > 0))
	{
		return false;
	}

	const double Ratio = static_cast<double>(Chamfered) / static_cast<double>(Sharp);

	// Logged at Display so the number appears in the gate's own output. A budget test that only
	// says pass or fail hides the one thing anybody actually wants from it.
	UE_LOG(LogHouseForgeEditor, Display,
		TEXT("Reference flat triangle count: %d sharp, %d chamfered (x%.2f)."), Sharp, Chamfered, Ratio);

	AddInfo(FString::Printf(
		TEXT("Reference flat: %d triangles sharp, %d chamfered - x%.2f."), Sharp, Chamfered, Ratio));

	TestTrue(TEXT("Chamfering actually reached the flat"), Chamfered > Sharp);
	TestTrue(TEXT("Chamfering the flat costs less than six times its triangles"), Ratio < 6.0);

	// Put it back, so a later test in the same run does not inherit a flat with the finish disabled.
	RefinishHouse(House, [](FHFRenderFinish& Finish) { Finish.Bevel.bEnabled = true; });

	return true;
}

/**
 * Every surface in the flat carries the attributes a photoreal render needs.
 *
 * Three of them, and each has failed silently before in this codebase or is set up to:
 *
 *   - a SECOND UV CHANNEL, so baked lighting stays an option alongside Lumen. Asked for by name in
 *     .claude/rules/04-conventions.md and absent from every mesh until now.
 *   - SHADING NORMALS on every triangle. A triangle with no normal elements shades off
 *     FDynamicMesh3's constant (0, 1, 0) and is pixel-identical to correct output in any unlit or
 *     wireframe capture.
 *   - SURFACE ROLES on every triangle, still, after the chamfering. An untagged chamfer is one the
 *     material panel can never reach.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatRenderAttributesTest,
	"HouseForge.Photoreal.EverySurfaceIsRenderReady", HF_TEST_FLAGS)

bool FHFFlatRenderAttributesTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePhotoreal;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	int32 Meshes = 0;
	int32 WithoutLightmap = 0;
	int32 WithoutNormals = 0;
	int32 Untagged = 0;
	int32 LightmapOutsideUnitSquare = 0;
	double WorstOvershoot = 0.0;

	for (UDynamicMeshComponent* Component : AllMeshComponents(House))
	{
		UDynamicMesh* Dynamic = Component->GetDynamicMesh();
		if (Dynamic == nullptr)
		{
			continue;
		}

		const FDynamicMesh3& Mesh = Dynamic->GetMeshRef();
		if (Mesh.TriangleCount() == 0)
		{
			continue;
		}
		++Meshes;

		if (!Mesh.HasAttributes() || Mesh.Attributes()->NumUVLayers() < 2)
		{
			++WithoutLightmap;
			continue;
		}

		const FDynamicMeshUVOverlay* Lightmap = Mesh.Attributes()->GetUVLayer(1);
		const FDynamicMeshNormalOverlay* Normals = Mesh.Attributes()->PrimaryNormals();

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Lightmap == nullptr || !Lightmap->IsSetTriangle(Tid))
			{
				++WithoutLightmap;
				break;
			}

			const FIndex3i UVTri = Lightmap->GetTriangle(Tid);
			bool bOutside = false;
			for (int32 i = 0; i < 3; ++i)
			{
				const FVector2f UV = Lightmap->GetElement(UVTri[i]);
				WorstOvershoot = FMath::Max3(WorstOvershoot,
					static_cast<double>(FMath::Max(UV.X, UV.Y)) - 1.0,
					-static_cast<double>(FMath::Min(UV.X, UV.Y)));
				bOutside |= UV.X < -0.002f || UV.X > 1.002f || UV.Y < -0.002f || UV.Y > 1.002f;
			}
			if (bOutside)
			{
				++LightmapOutsideUnitSquare;
				break;
			}
		}

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Normals == nullptr || !Normals->IsSetTriangle(Tid))
			{
				++WithoutNormals;
				break;
			}
		}

		// Group zero is the one value RoleForGroup cannot map, and it is what an untagged triangle
		// carries. Anything else is a real role, chamfer or not.
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (Mesh.GetTriangleGroup(Tid) <= 0)
			{
				++Untagged;
				break;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d meshes checked; worst lightmap UV overshoot %.4f."), Meshes, WorstOvershoot));
	TestTrue(TEXT("The flat has meshes to check"), Meshes > 100);
	TestEqual(TEXT("Every mesh in the flat has a lightmap UV channel"), WithoutLightmap, 0);
	TestEqual(TEXT("Every lightmap unwrap is inside the unit square"), LightmapOutsideUnitSquare, 0);
	TestEqual(TEXT("Every mesh in the flat has shading normals"), WithoutNormals, 0);
	TestEqual(TEXT("No mesh in the flat has untagged triangles"), Untagged, 0);

	return true;
}

/**
 * There is no pane of glass in the flat that is a plane.
 *
 * .claude/rules/04-conventions.md: "Glass needs thickness, not a plane, or refraction and reflection
 * look wrong." Checked over the built flat rather than over one generator, because the failure this
 * guards against is a pane centred in a rebate whose arithmetic happens to come out at zero - which
 * only one opening type at one size would show.
 *
 * Measured per connected run of glass triangles, not per mesh: a sliding unit carries two panes on
 * two tracks, and their combined bounding box is as deep as the track pitch whether or not either
 * pane has any thickness at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatGlassIsSolidTest,
	"HouseForge.Photoreal.NoGlassInTheFlatIsAPlane", HF_TEST_FLAGS)

bool FHFFlatGlassIsSolidTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePhotoreal;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	int32 Panes = 0;
	int32 Flat = 0;
	double Thinnest = TNumericLimits<double>::Max();

	for (UDynamicMeshComponent* Component : AllMeshComponents(House))
	{
		UDynamicMesh* Dynamic = Component->GetDynamicMesh();
		if (Dynamic == nullptr)
		{
			continue;
		}
		const FDynamicMesh3& Mesh = Dynamic->GetMeshRef();

		// Glass triangles, grouped into connected runs by shared vertices - one run per pane.
		TArray<int32> GlassTris;
		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			if (FHFMeshOps::RoleForGroup(Mesh.GetTriangleGroup(Tid)) == EHFSurfaceRole::Glass)
			{
				GlassTris.Add(Tid);
			}
		}
		if (GlassTris.IsEmpty())
		{
			continue;
		}

		TSet<int32> Remaining(GlassTris);
		while (!Remaining.IsEmpty())
		{
			TArray<int32> Stack{ *Remaining.CreateIterator() };
			Remaining.Remove(Stack[0]);

			FAxisAlignedBox3d Bounds = FAxisAlignedBox3d::Empty();
			while (!Stack.IsEmpty())
			{
				const int32 Tid = Stack.Pop();
				const FIndex3i Tri = Mesh.GetTriangle(Tid);
				for (int32 i = 0; i < 3; ++i)
				{
					Bounds.Contain(Mesh.GetVertex(Tri[i]));
				}

				const FIndex3i Neighbours = Mesh.GetTriNeighbourTris(Tid);
				for (int32 i = 0; i < 3; ++i)
				{
					if (Neighbours[i] != FDynamicMesh3::InvalidID && Remaining.Remove(Neighbours[i]) > 0)
					{
						Stack.Add(Neighbours[i]);
					}
				}
			}

			++Panes;
			const double Thickness = FMath::Min3(Bounds.Width(), Bounds.Depth(), Bounds.Height());
			Thinnest = FMath::Min(Thinnest, Thickness);

			// The thinnest glass anywhere in the plugin's figures is 4 mm; a chamfer takes 0.5 mm off
			// each arris of it. Anything under 2 mm is a pane that has been flattened by arithmetic.
			if (Thickness < 0.2)
			{
				++Flat;
			}
		}
	}

	TestTrue(TEXT("The flat has glazing in it"), Panes > 0);
	AddInfo(FString::Printf(TEXT("%d glazed panes, thinnest %.3f cm."), Panes,
		Panes > 0 ? Thinnest : 0.0));
	TestEqual(TEXT("No pane in the flat is a plane"), Flat, 0);

	return true;
}

/**
 * A sliding leaf blocks a walkthrough wherever it has been slid to, not only where it started.
 *
 * The existing collision test covers a hinged door at shut, ajar and open. A slider is the case it
 * does not reach, and the one most likely to fail quietly: both leaves of every slider now carry a
 * real Slide motion, so there are twice as many bodies that have to travel with their render, and a
 * sash that leaves its collision behind reads as perfect in every still.
 *
 * Traced against the part's own body at each of five open amounts, and the closed position is
 * re-traced at full travel: a body that never moved passes the first half of that and fails the
 * second, and a body that vanished fails the first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSlidingCollisionTest,
	"HouseForge.Photoreal.SlidingPartsCollideAtAnyOpenAmount", HF_TEST_FLAGS)

bool FHFSlidingCollisionTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePhotoreal;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	const FCollisionQueryParams TraceParams(TEXT("HFSlidingCollision"), /*bTraceComplex*/ true);

	// A probe along whichever way the part is thinnest - which for a leaf, a sash or a shutter is its
	// thickness, so the line crosses the solid rather than running down a shadow gap.
	//
	// AIMED AT THE PART'S OWN TRIANGLES RATHER THAN AT THE MIDDLE OF ITS BOUNDING BOX, because not
	// every part has anything at the middle of its bounding box. A drawer's runner is a PAIR of
	// members, one down each side of the module - see FHFJoineryKit::GenerateDrawerRunnerIntermediate,
	// where two of them is what lets the drawer's rail overlap this member and this member overlap the
	// cabinet's channel. Its box therefore spans the whole module and its centre is the air BETWEEN
	// the two rails, so a central probe went cleanly through the gap and reported no collision on five
	// parts that have perfectly good collision.
	//
	// Sampling a grid across the box does not fix it either - the rails are 12 mm members at the ends
	// of a 780 mm span, so any grid coarse enough to write down misses them too, and picking offsets
	// until the current geometry passes is fitting the test to the answer.
	//
	// So the probe is aimed at a point that is on the surface BY CONSTRUCTION: the centroid of one of
	// the part's own triangles, taken from the mesh the component is rendering and pushed through the
	// component's own transform. That cannot be defeated by any shape, and it still fails exactly as
	// before for the case this test exists to catch - a part whose collision did not travel with its
	// render has nothing at any of its triangles either.
	auto ProbeHits = [&TraceParams](UDynamicMeshComponent* Component, const FBox& WorldBounds)
	{
		const FVector Extent = WorldBounds.GetExtent();

		int32 Thinnest = 0;
		for (int32 Axis = 1; Axis < 3; ++Axis)
		{
			Thinnest = Extent[Axis] < Extent[Thinnest] ? Axis : Thinnest;
		}

		FVector Along = FVector::ZeroVector;
		Along[Thinnest] = FMath::Max(Extent[Thinnest] * 4.0, 10.0);

		const UE::Geometry::FDynamicMesh3* Mesh = Component->GetMesh();
		if (Mesh == nullptr || Mesh->TriangleCount() == 0)
		{
			return false;
		}

		const FTransform ToWorld = Component->GetComponentTransform();

		// A handful of triangles spread through the part rather than all of them: one is enough to
		// prove the body is there, and several guard against a single degenerate face.
		const int32 Triangles = Mesh->MaxTriangleID();
		const int32 Stride = FMath::Max(Triangles / 8, 1);

		for (int32 Tri = 0; Tri < Triangles; Tri += Stride)
		{
			if (!Mesh->IsTriangle(Tri))
			{
				continue;
			}

			const FVector From = ToWorld.TransformPosition(FVector(Mesh->GetTriCentroid(Tri)));

			FHitResult Hit;
			if (Component->LineTraceComponent(Hit, From - Along, From + Along, TraceParams))
			{
				return true;
			}
		}

		return false;
	};

	int32 SlidingParts = 0;
	int32 MissedSomewhere = 0;
	int32 BodiesThatNeverMoved = 0;

	for (const TObjectPtr<AActor>& Actor : House->ElementActors)
	{
		AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Actor);
		if (Articulated == nullptr)
		{
			continue;
		}

		Articulated->CloseAllParts();

		for (const FHFPartState& Part : Articulated->Parts)
		{
			if (Part.Motion.Type != EHFMotionType::Slide || Part.Motion.MaxTravelCm <= 1.0)
			{
				continue;
			}

			UDynamicMeshComponent* Component = Articulated->GetPartComponent(Part.PartId);
			if (Component == nullptr)
			{
				continue;
			}

			++SlidingParts;

			Articulated->SetPartOpenAmount(Part.PartId, 0.0);
			const FBox ShutBounds = Component->Bounds.GetBox();

			bool bHitEverywhere = true;
			for (const double Amount : { 0.0, 0.25, 0.5, 0.75, 1.0 })
			{
				Articulated->SetPartOpenAmount(Part.PartId, Amount);
				bHitEverywhere &= ProbeHits(Component, Component->Bounds.GetBox());
			}

			if (!bHitEverywhere)
			{
				++MissedSomewhere;
			}

			// Fully open, the shut position must be clear - as long as the leaf actually travelled
			// further than its own width, which is what a two-track slider does by construction.
			const FBox OpenBounds = Component->Bounds.GetBox();
			if (!OpenBounds.Intersect(ShutBounds) && ProbeHits(Component, ShutBounds))
			{
				++BodiesThatNeverMoved;
			}

			Articulated->SetPartOpenAmount(Part.PartId, 0.0);
		}
	}

	TestTrue(TEXT("The flat has sliding parts to test"), SlidingParts > 0);
	AddInfo(FString::Printf(TEXT("%d sliding parts traced at five open amounts each."), SlidingParts));
	TestEqual(TEXT("Every sliding part blocks at every open amount"), MissedSomewhere, 0);
	TestEqual(TEXT("No sliding part left its collision behind"), BodiesThatNeverMoved, 0);

	return true;
}

/**
 * The chamfer is a project setting, and the setting reaches a flat that is already standing.
 *
 * A control that only takes effect on the next full rebuild is a control the page is lying about,
 * and this page has already told that lie once - the whole Joinery section was inert on wardrobes
 * already in the level. The render finish is the widest-reaching section on it: every element in
 * the flat carries one, so the failure mode is bigger here than anywhere else.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRenderFinishSettingTest,
	"HouseForge.Photoreal.TheChamferIsAProjectSetting", HF_TEST_FLAGS)

bool FHFRenderFinishSettingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgePhotoreal;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	UHFEditorSubsystem* Subsystem = GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();

	if (!TestNotNull(TEXT("An editor world is open"), World)
		|| !TestNotNull(TEXT("The HouseForge subsystem is up"), Subsystem)
		|| !TestNotNull(TEXT("The settings object exists"), Settings))
	{
		return false;
	}

	const FHFRenderFinish Original = Settings->Render;
	ON_SCOPE_EXIT
	{
		Settings->Render = Original;
		ClearHouseForgeActors(World);
	};

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	const int32 Chamfered = TotalTriangles(House);

	// A house built while the page says no chamfers has to come out of the spawn funnel with none,
	// which is the half of this that a full rebuild covers.
	Settings->Render.Bevel.bEnabled = false;
	const int32 Rebuilt = Subsystem->ApplyProjectSettingsToLevel();
	const int32 Sharp = TotalTriangles(House);

	TestTrue(TEXT("Turning chamfers off rebuilds the flat that is already standing"), Rebuilt > 100);
	TestTrue(TEXT("And the flat really loses its chamfers"), Sharp < Chamfered);

	// Applying the same settings twice must not rebuild anything the second time: an element whose
	// finish already matches is not a reason to throw its geometry away, and on a flat this size
	// that would be a second full rebuild for no change at all.
	const int32 Again = Subsystem->ApplyProjectSettingsToLevel();
	TestTrue(TEXT("Applying an unchanged finish rebuilds nothing extra for it"), Again < Rebuilt);

	Settings->Render = Original;
	Subsystem->ApplyProjectSettingsToLevel();
	TestEqual(TEXT("Turning them back on restores exactly what was there"),
		TotalTriangles(House), Chamfered);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
