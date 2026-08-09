// Copyright Siddartha G. All Rights Reserved.

//
// The reversible bake.
//
// This feature can silently lose an artist's work, so the tests are written failure-mode first. The
// happy path - "a bake produces an asset" - is the least interesting thing here and is asserted
// almost in passing. What matters is everything around it:
//
//   * the dynamic mesh is still there, vertex for vertex, after a bake     DynamicMeshSurvivesBake
//   * and after an unbake                                                  UnbakeRestoresLiveMesh
//   * a bake is not a hand edit                                            BakeDoesNotSetArtistEdited
//   * a bake of a sculpted element bakes the SCULPT                        ArtistEditedBakesSculptedForm
//   * sculpting a baked element brings the live mesh back                  HandEditWhileBakedUnbakes
//   * a Modeling Tool still targets the live mesh                          ToolTargetStaysOnTheLiveMesh
//   * exactly one representation collides                                  CollisionFollowsVisibility
//   * a missing asset never leaves a hole in the flat                      MissingAssetFallsBackToDynamic
//   * a fixture's parts stay separate and still move                       ArticulatedBakeKeepsPartsSeparate
//   * surface roles survive, or the material panel loses the element       SurfaceRolesReachTheBakedElement
//   * a house rebuild does not destroy a baked element                     HouseRebuildPreservesBakedElements
//
// ToolTargetStaysOnTheLiveMesh is the one to keep. HouseForge.Bake.Probe.ToolTargetSelection
// MEASURED that a hidden - and even an unregistered - UStaticMeshComponent still wins a Modeling
// Tool over the live dynamic mesh, so an artist who thinks they are sculpting a wall would be
// editing a derived asset that the next re-bake throws away. The fix is that ApplyRenderMode clears
// the baked component's UStaticMesh while Dynamic, and this test is the only thing standing between
// that fix and somebody "tidying" it back into a SetVisibility call.
//

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFBakeTypes.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Bake/HFBakeService.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "HAL/FileManager.h"
#include "MeshDescription.h"
#include "Misc/AutomationTest.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Model/HFSampleHouse.h"
#include "StaticMeshAttributes.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolContextInterfaces.h"
#include "ToolTargetManager.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"
#include "ToolTargets/VolumeComponentToolTarget.h"
#include "UObject/Package.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFBakeTest
{
	/**
	 * Every vertex of a component's mesh, as a value that can be compared exactly.
	 *
	 * Positions AND the surface-role polygroups, kept as separate fields rather than folded into one
	 * hash. Renumbering the polygroups while leaving every vertex in place passes a position-only
	 * comparison and leaves the flat rendering the wrong finishes with the mechanism for fixing it
	 * gone - which is the same trap HouseForge.Editor.Surfaces.SettingAFinishDoesNotTouchGeometry
	 * exists to avoid.
	 */
	struct FMeshPrint
	{
		int32 Vertices = 0;
		int32 Triangles = 0;
		FVector3d Centroid = FVector3d::Zero();
		double PositionSum = 0.0;
		TSet<int32> Groups;

		bool operator==(const FMeshPrint& Other) const
		{
			return Vertices == Other.Vertices
				&& Triangles == Other.Triangles
				&& Centroid.Equals(Other.Centroid, 1e-9)
				&& FMath::IsNearlyEqual(PositionSum, Other.PositionSum, 1e-6)
				&& Groups.Difference(Other.Groups).IsEmpty()
				&& Other.Groups.Difference(Groups).IsEmpty();
		}
	};

	FMeshPrint Print(UDynamicMeshComponent* Component)
	{
		FMeshPrint Result;
		if (!IsValid(Component))
		{
			return Result;
		}

		Component->GetDynamicMesh()->ProcessMesh([&Result](const FDynamicMesh3& Mesh)
		{
			Result.Vertices = Mesh.VertexCount();
			Result.Triangles = Mesh.TriangleCount();

			FVector3d Sum = FVector3d::Zero();
			for (const int32 Vid : Mesh.VertexIndicesItr())
			{
				const FVector3d P = Mesh.GetVertex(Vid);
				Sum += P;

				// Weighted by index so a permutation of the same points is not the same print - a
				// mesh rebuilt to the identical shape is still a rebuild, and this feature's whole
				// claim is that no rebuild happened at all.
				Result.PositionSum += (P.X * 3.0 + P.Y * 5.0 + P.Z * 7.0) * static_cast<double>(Vid + 1);
			}

			if (Mesh.VertexCount() > 0)
			{
				Result.Centroid = Sum / static_cast<double>(Mesh.VertexCount());
			}

			if (Mesh.HasTriangleGroups())
			{
				for (const int32 Tid : Mesh.TriangleIndicesItr())
				{
					Result.Groups.Add(Mesh.GetTriangleGroup(Tid));
				}
			}
		});

		return Result;
	}

	/** A wall standing alone in the editor world. */
	AHFWallActor* SpawnWall(UWorld* World, const TCHAR* Id)
	{
		AHFWallActor* Actor = World->SpawnActor<AHFWallActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->ElementId = FName(Id);
		Actor->Wall.Id = FName(Id);
		Actor->Wall.Start = FVector2D(0.0, 0.0);
		Actor->Wall.End = FVector2D(400.0, 0.0);
		Actor->Wall.Thickness = 20.0;
		Actor->Wall.Height = 300.0;
		Actor->Regenerate();
		return Actor;
	}

	/** A door, which is the one articulated element the plugin ships today: a leaf that moves. */
	AHFOpeningActor* SpawnDoor(UWorld* World, const TCHAR* Id)
	{
		AHFOpeningActor* Actor = World->SpawnActor<AHFOpeningActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->ElementId = FName(Id);
		Actor->HostWall.Id = TEXT("W_Bake");
		Actor->HostWall.Start = FVector2D(0.0, 0.0);
		Actor->HostWall.End = FVector2D(400.0, 0.0);
		Actor->HostWall.Thickness = 20.0;
		Actor->HostWall.Height = 300.0;

		Actor->Opening.Id = FName(Id);
		Actor->Opening.WallId = TEXT("W_Bake");
		Actor->Opening.OffsetAlongWall = 200.0;
		Actor->Opening.Width = 90.0;
		Actor->Opening.Height = 210.0;
		Actor->Opening.Kind = EHFOpeningKind::Door;
		Actor->Opening.Swing = EHFSwing::InwardLeft;

		Actor->Regenerate();
		return Actor;
	}

	/** Nudges a mesh by hand, exactly as a modelling tool would - through the public commit path. */
	void SculptFirstVertex(UDynamicMeshComponent* Component, const FVector3d& Offset)
	{
		Component->GetDynamicMesh()->EditMesh([&Offset](FDynamicMesh3& Mesh)
		{
			for (const int32 Vid : Mesh.VertexIndicesItr())
			{
				Mesh.SetVertex(Vid, Mesh.GetVertex(Vid) + Offset);
				return;
			}
		});
	}

	/** Leaves no dirty packages behind, so the automation run does not try to save test assets. */
	void ForgetAssets(AHFElementActor* Element)
	{
		if (!IsValid(Element))
		{
			return;
		}

		for (const FHFBakedPart& Part : Element->BakedParts)
		{
			if (Part.BakedMesh != nullptr && Part.BakedMesh->GetOutermost() != nullptr)
			{
				Part.BakedMesh->GetOutermost()->SetDirtyFlag(false);
			}
		}
	}

	/** The target set every single-selection mesh editing tool asks for. */
	const FToolTargetTypeRequirements& MeshEditingRequirements()
	{
		static FToolTargetTypeRequirements Requirements({
			UMaterialProvider::StaticClass(),
			UDynamicMeshCommitter::StaticClass(),
			UDynamicMeshProvider::StaticClass(),
			UPrimitiveComponentBackedTarget::StaticClass()
			});
		return Requirements;
	}

	/** Loaded with exactly the factories UModelingToolsEditorMode::Enter loads, in its order. */
	UToolTargetManager* MakeModelingModeTargetManager()
	{
		UToolTargetManager* Manager = NewObject<UToolTargetManager>();
		Manager->Initialize();
		Manager->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(Manager));
		Manager->AddTargetFactory(NewObject<UVolumeComponentToolTargetFactory>(Manager));
		Manager->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(Manager));
		return Manager;
	}
}

// =========================================================================== the central claim

/**
 * The one sentence the whole feature rests on: "Dynamic meshes are kept."
 *
 * Vertex for vertex, group for group. If this ever fails, baking has become a replacement and rule
 * 04 has been broken - and the loss would be invisible until somebody tried to unbake.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeDynamicSurvivesTest,
	"HouseForge.Bake.DynamicMeshSurvivesBake", HF_TEST_FLAGS)

bool FHFBakeDynamicSurvivesTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWallActor* Wall = HFBakeTest::SpawnWall(World, TEXT("W_Survive"));
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	const HFBakeTest::FMeshPrint Before = HFBakeTest::Print(Wall->GetMeshComponent());
	TestTrue(TEXT("The wall generated some geometry to begin with"), Before.Triangles > 0);

	FHFBakeReport Report;
	const bool bBaked = FHFBakeService::BakeElement(Wall, Report);
	AddInfo(Report.Summary());

	if (!TestTrue(TEXT("The wall bakes"), bBaked))
	{
		return false;
	}

	const HFBakeTest::FMeshPrint After = HFBakeTest::Print(Wall->GetMeshComponent());

	TestTrue(TEXT("The dynamic mesh is identical after a bake - same vertices, same positions, same surface roles"),
		Before == After);
	TestEqual(TEXT("The element is showing baked geometry"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));
	TestTrue(TEXT("The dynamic mesh component still exists and still holds the mesh"),
		IsValid(Wall->GetMeshComponent()) && After.Triangles > 0);

	return true;
}

/**
 * And back again. Unbake is a pointer assignment; it cannot lose anything, and this says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeUnbakeRestoresTest,
	"HouseForge.Bake.UnbakeRestoresLiveMesh", HF_TEST_FLAGS)

bool FHFBakeUnbakeRestoresTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Unbake")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	const HFBakeTest::FMeshPrint Before = HFBakeTest::Print(Wall->GetMeshComponent());

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	FHFBakeService::UnbakeElement(Wall, Report);

	TestEqual(TEXT("The element is back on its live mesh"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));
	TestTrue(TEXT("The live mesh is exactly what it was before the bake"),
		Before == HFBakeTest::Print(Wall->GetMeshComponent()));
	TestTrue(TEXT("The dynamic component is visible again"),
		Wall->GetMeshComponent()->IsVisible());
	TestTrue(TEXT("The dynamic component is editable again, so the Modeling Tools can see it"),
		Wall->GetMeshComponent()->IsEditable());

	// THE ASSET IS KEPT. Re-baking after an accidental unbake has to cost nothing, which is most of
	// why the switch needs no confirmation dialog.
	TestTrue(TEXT("Unbaking keeps the baked asset, so re-baking is free"), Wall->HasAllBakedAssets());
	TestFalse(TEXT("Unbaking a current bake does not make it stale"), Wall->IsBakeStale());

	if (TestTrue(TEXT("There is a baked component to inspect"), !Wall->BakedParts.IsEmpty()))
	{
		UStaticMeshComponent* Baked = Wall->BakedParts[0].Component;
		if (TestNotNull(TEXT("The baked component still exists"), Baked))
		{
			TestFalse(TEXT("The baked component is hidden"), Baked->IsVisible());
			TestNull(TEXT("The baked component holds no UStaticMesh while Dynamic - the tool target fix"),
				Baked->GetStaticMesh().Get());
		}
	}

	return true;
}

// ================================================================= the dangerous one, guarded

/**
 * A Modeling Tool must target the LIVE mesh while the element is showing its live mesh.
 *
 * HouseForge.Bake.Probe.ToolTargetSelection measured that hiding the baked component does nothing
 * and that UNREGISTERING it does nothing either, because ToolBuilderUtil::FindAllComponents walks
 * AActor::OwnedComponents with no registration test and
 * UStaticMeshComponentToolTargetFactory::CanBuildTarget asks only whether the component holds a
 * writable UStaticMesh. The static factory is also registered first, so it wins every tie.
 *
 * So an artist starting Sculpt on an unbaked-but-previously-baked wall would have been editing the
 * derived asset, and the next re-bake would have thrown that work away without a word. The fix is
 * that ApplyRenderMode clears SetStaticMesh(nullptr) while Dynamic. This asserts the fix through the
 * real UToolTargetManager, with a real asset, rather than trusting the code to keep doing it.
 *
 * The candidate COUNT is asserted as well as the winner: USingleSelectionMeshEditingToolBuilder
 * requires exactly one, so two live candidates make PolyEdit, Sculpt, Displace and Remesh refuse to
 * start at all.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeToolTargetTest,
	"HouseForge.Bake.ToolTargetStaysOnTheLiveMeshWhileDynamic", HF_TEST_FLAGS)

bool FHFBakeToolTargetTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_ToolTarget")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}
	if (!TestTrue(TEXT("The bake produced a real asset to be dangerous with"), Wall->HasAllBakedAssets()))
	{
		return false;
	}

	UToolTargetManager* Manager = HFBakeTest::MakeModelingModeTargetManager();

	auto Ask = [Manager, World, Wall](FString& OutTargetClass) -> int32
	{
		FToolBuilderState State;
		State.World = World;
		State.SelectedActors.Add(Wall);

		OutTargetClass = TEXT("<none>");
		if (UToolTarget* Target = Manager->BuildFirstSelectedTargetable(State, HFBakeTest::MeshEditingRequirements()))
		{
			OutTargetClass = Target->GetClass()->GetName();
		}
		return Manager->CountSelectedAndTargetable(State, HFBakeTest::MeshEditingRequirements());
	};

	// Baked: the tools correctly target the baked asset, and that is fine - the switch is visibly on,
	// the dynamic mesh is untouched, and the next re-bake discards anything done to the asset.
	FString BakedTarget;
	const int32 BakedCount = Ask(BakedTarget);
	AddInfo(FString::Printf(TEXT("Baked mode: %d candidate(s), target=%s"), BakedCount, *BakedTarget));
	TestEqual(TEXT("In Baked mode there is exactly one candidate, so single-selection tools can start"),
		BakedCount, 1);

	// Dynamic: the tools MUST come back to the live mesh.
	FHFBakeService::UnbakeElement(Wall, Report);

	FString DynamicTarget;
	const int32 DynamicCount = Ask(DynamicTarget);
	AddInfo(FString::Printf(TEXT("Dynamic mode: %d candidate(s), target=%s"), DynamicCount, *DynamicTarget));

	TestEqual(TEXT("While Dynamic, a Modeling Tool edits the live dynamic mesh and not the baked asset"),
		DynamicTarget, FString(TEXT("DynamicMeshComponentToolTarget")));
	TestEqual(TEXT("While Dynamic there is exactly one candidate, so PolyEdit and Sculpt can start"),
		DynamicCount, 1);

	return true;
}

// ============================================================================== hand editing

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeNotAnEditTest,
	"HouseForge.Bake.BakeDoesNotSetArtistEdited", HF_TEST_FLAGS)

bool FHFBakeNotAnEditTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_NotAnEdit")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	FHFBakeService::BakeElement(Wall, Report);
	FHFBakeService::UnbakeElement(Wall, Report);
	FHFBakeService::BakeElement(Wall, Report);

	// A bake that set the flag would silently stop the element regenerating from its parameters
	// forever - the element would look fine and would quietly stop following the spec.
	TestFalse(TEXT("Baking, unbaking and re-baking never marks the element as hand-edited"),
		Wall->bArtistEdited);

	return true;
}

/**
 * BAKE BAKES WHAT IS ON SCREEN. Never Regenerate() first.
 *
 * A sculpted wall bakes its sculpt and unbakes back to that same sculpt. Regenerating before baking
 * to make the asset "correct" would throw the modelling work away, which is exactly the silent
 * unrecoverable loss rule 04 forbids.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSculptedFormTest,
	"HouseForge.Bake.ArtistEditedBakesSculptedForm", HF_TEST_FLAGS)

bool FHFBakeSculptedFormTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Sculpted")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	HFBakeTest::SculptFirstVertex(Wall->GetMeshComponent(), FVector3d(0.0, 0.0, 37.0));
	TestTrue(TEXT("The sculpt was detected as a hand edit"), Wall->bArtistEdited);

	const HFBakeTest::FMeshPrint Sculpted = HFBakeTest::Print(Wall->GetMeshComponent());

	FHFBakeReport Report;
	if (!TestTrue(TEXT("A hand-edited wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	TestTrue(TEXT("The sculpt is still there after the bake"),
		Sculpted == HFBakeTest::Print(Wall->GetMeshComponent()));

	FHFBakeService::UnbakeElement(Wall, Report);

	TestTrue(TEXT("Unbaking gives back the sculpted form, not the generated one"),
		Sculpted == HFBakeTest::Print(Wall->GetMeshComponent()));
	TestTrue(TEXT("The element is still marked hand-edited"), Wall->bArtistEdited);

	// The bounds of the baked asset are the SCULPTED bounds. If the bake had regenerated first they
	// would be the generated ones, and the assertion above would still have passed because the
	// component was never written - so this is the half that catches a regenerate-then-bake.
	if (TestTrue(TEXT("There is a baked asset"), Wall->HasAllBakedAssets()))
	{
		const FBox AssetBounds = Wall->BakedParts[0].BakedMesh->GetBoundingBox();
		FAxisAlignedBox3d LiveBounds;
		Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&LiveBounds](const FDynamicMesh3& Mesh)
		{
			LiveBounds = Mesh.GetBounds();
		});

		TestTrue(TEXT("The baked asset holds the sculpted geometry, so the bake did not regenerate first"),
			AssetBounds.GetSize().Equals(FVector(LiveBounds.Max - LiveBounds.Min), 0.5));
	}

	return true;
}

/**
 * Sculpting a baked element brings the live mesh back.
 *
 * Without this an artist sculpts something they cannot see, watches nothing change, and undoes work
 * that in fact applied perfectly.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeHandEditUnbakesTest,
	"HouseForge.Bake.HandEditWhileBakedUnbakes", HF_TEST_FLAGS)

bool FHFBakeHandEditUnbakesTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_EditWhileBaked")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	// Off, so the element stays baked and stale rather than being re-baked out from under the test.
	Wall->bAutoRebakeOnRegenerate = false;

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	HFBakeTest::SculptFirstVertex(Wall->GetMeshComponent(), FVector3d(0.0, 0.0, 11.0));

	TestEqual(TEXT("A hand edit while baked switches the element back to its live mesh"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));
	TestTrue(TEXT("The edit registered as a hand edit"), Wall->bArtistEdited);
	TestTrue(TEXT("The now-outdated baked asset is kept rather than deleted"), Wall->HasAllBakedAssets());
	TestTrue(TEXT("And it is reported as stale"), Wall->IsBakeStale());

	// Off, the element must NOT silently switch away - somebody turned the guard off on purpose.
	Wall->bUnbakeOnHandEdit = false;
	Wall->SetRenderMode(EHFRenderMode::Baked);
	HFBakeTest::SculptFirstVertex(Wall->GetMeshComponent(), FVector3d(0.0, 0.0, 3.0));
	TestEqual(TEXT("With the guard off, a hand edit leaves the element baked"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));

	return true;
}

// ================================================================================= collision

/**
 * Exactly one representation collides, in either mode.
 *
 * Both on double-traces every wall in the flat and leaves complex-as-simple dynamic collision under
 * a mesh the user believes is the only thing there; neither on drops a walkthrough through the
 * floor. This is also where the bake's collision has to be complex-as-simple rather than a hull, or
 * a walkthrough passes through an open door - rule 04 names that case specifically.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeCollisionTest,
	"HouseForge.Bake.CollisionFollowsVisibility", HF_TEST_FLAGS)

bool FHFBakeCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Collision")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	UDynamicMeshComponent* Dynamic = Wall->GetMeshComponent();
	const ECollisionEnabled::Type Declared = Dynamic->GetCollisionEnabled();
	TestNotEqual(TEXT("A generated wall collides to begin with"),
		static_cast<int32>(Declared), static_cast<int32>(ECollisionEnabled::NoCollision));

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	UStaticMeshComponent* Baked = Wall->BakedParts.IsEmpty() ? nullptr : Wall->BakedParts[0].Component;
	if (!TestNotNull(TEXT("There is a baked component"), Baked))
	{
		return false;
	}

	TestEqual(TEXT("Baked: the live mesh stops colliding"),
		static_cast<int32>(Dynamic->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));
	TestNotEqual(TEXT("Baked: the baked mesh collides"),
		static_cast<int32>(Baked->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));

	// Complex-as-simple on the asset. A hull here is a walkthrough walking through an open doorway.
	UStaticMesh* Asset = Wall->BakedParts[0].BakedMesh;
	if (TestNotNull(TEXT("The baked asset exists"), Asset) && TestNotNull(TEXT("It has a body setup"), Asset->GetBodySetup()))
	{
		TestEqual(TEXT("Baked collision is complex-as-simple, so it matches the visual mesh"),
			static_cast<int32>(Asset->GetBodySetup()->CollisionTraceFlag),
			static_cast<int32>(ECollisionTraceFlag::CTF_UseComplexAsSimple));
	}

	FHFBakeService::UnbakeElement(Wall, Report);

	TestEqual(TEXT("Dynamic: the live mesh collides again, with the setting it declared"),
		static_cast<int32>(Dynamic->GetCollisionEnabled()), static_cast<int32>(Declared));
	TestEqual(TEXT("Dynamic: the baked mesh stops colliding"),
		static_cast<int32>(Baked->GetCollisionEnabled()), static_cast<int32>(ECollisionEnabled::NoCollision));

	return true;
}

/**
 * A RE-BAKE MUST NOT ERODE THE COLLISION UNBAKE WILL LATER RESTORE.
 *
 * CollisionFollowsVisibility above bakes once and unbakes once, and passes. This is the same
 * property asked of the loop people actually work in: bake the flat, correct a misread, let it
 * re-bake, switch back to live meshes to model something.
 *
 * The trap is that FHFBakedPart::SourceCollisionEnabled is a SNAPSHOT of the dynamic component,
 * and a re-bake reads it while the element is already baked - at which point the bake itself has
 * set that component to NoCollision. Snapshotting then records "this part blocks nothing" as the
 * value to restore, and the next unbake hands it back faithfully. The wall is then visible, live,
 * editable and completely passable, in both modes, with nothing logged.
 *
 * It is the worst shape of bug this feature can have: no error, no missing asset, no visual
 * difference at all, and a walkthrough that falls out of the flat.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeRebakeCollisionTest,
	"HouseForge.Bake.RebakingKeepsTheCollisionUnbakeRestores", HF_TEST_FLAGS)

bool FHFBakeRebakeCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_RebakeCollision")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	UDynamicMeshComponent* Dynamic = Wall->GetMeshComponent();
	const ECollisionEnabled::Type Declared = Dynamic->GetCollisionEnabled();
	TestNotEqual(TEXT("A generated wall blocks something to begin with"),
		static_cast<int32>(Declared), static_cast<int32>(ECollisionEnabled::NoCollision));

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	// Two parameter changes, so the erosion has a second chance to happen even if the first
	// re-bake caught the live value by luck of ordering.
	Wall->Wall.Height = 260.0;
	Wall->Regenerate();
	Wall->Wall.Height = 250.0;
	Wall->Regenerate();

	TestEqual(TEXT("The element is still baked after two parameter changes"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));

	AddInfo(FString::Printf(TEXT("Declared collision %d; recorded after two re-bakes %d."),
		static_cast<int32>(Declared),
		static_cast<int32>(Wall->BakedParts[0].SourceCollisionEnabled.GetValue())));

	// Asserted on the RECORD as well as on the restored value, because the record is the thing that
	// is wrong and the restored value is only how it is noticed.
	TestEqual(TEXT("The record of what the live mesh blocks survives a re-bake"),
		static_cast<int32>(Wall->BakedParts[0].SourceCollisionEnabled.GetValue()),
		static_cast<int32>(Declared));

	FHFBakeService::UnbakeElement(Wall, Report);

	TestEqual(TEXT("After re-baking, unbaking still gives the live mesh back its collision"),
		static_cast<int32>(Dynamic->GetCollisionEnabled()), static_cast<int32>(Declared));

	return true;
}

/**
 * The same erosion, on a part that deliberately blocks LESS than a wall.
 *
 * A door leaf is a full blocker; the point of doing it on the articulated actor as well is that
 * AHFArticulatedActor::ApplyPartCollision re-declares each part's collision on every regeneration,
 * so the part path has a second writer the root path does not. A rotor is QueryOnly on purpose -
 * see EHFPartCollision::TraceOnly - and neither NoCollision nor QueryAndPhysics is an acceptable
 * thing for a bake to leave behind.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeRebakePartCollisionTest,
	"HouseForge.Bake.RebakingKeepsEachPartsOwnCollision", HF_TEST_FLAGS)

bool FHFBakeRebakePartCollisionTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFOpeningActor* Door = World ? HFBakeTest::SpawnDoor(World, TEXT("D_RebakeCollision")) : nullptr;
	if (!TestNotNull(TEXT("A door spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Door); if (IsValid(Door)) { Door->Destroy(); } };

	TArray<UDynamicMeshComponent*> Sources;
	Door->GetBakeSourceComponents(Sources);
	if (!TestTrue(TEXT("The door has a leaf as well as a shell"), Sources.Num() > 1))
	{
		return false;
	}

	TArray<ECollisionEnabled::Type> Declared;
	for (UDynamicMeshComponent* Source : Sources)
	{
		Declared.Add(Source->GetCollisionEnabled());
	}

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The door bakes"), FHFBakeService::BakeElement(Door, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	Door->Opening.Width = 85.0;
	Door->Regenerate();

	FHFBakeService::UnbakeElement(Door, Report);

	Door->GetBakeSourceComponents(Sources);
	if (!TestEqual(TEXT("The door still has the same parts"), Sources.Num(), Declared.Num()))
	{
		return false;
	}

	for (int32 Index = 0; Index < Sources.Num(); ++Index)
	{
		TestEqual(FString::Printf(
			TEXT("Part %d gets back exactly the collision its generator declared, after a re-bake"), Index),
			static_cast<int32>(Sources[Index]->GetCollisionEnabled()),
			static_cast<int32>(Declared[Index]));
	}

	return true;
}

// ================================================================================= staleness

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeStaleTest,
	"HouseForge.Bake.MeshRevisionMarksBakeStale", HF_TEST_FLAGS)

bool FHFBakeStaleTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Stale")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	Wall->bAutoRebakeOnRegenerate = false;

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}
	TestFalse(TEXT("A freshly baked element is not stale"), Wall->IsBakeStale());

	const int32 RevisionBefore = Wall->MeshRevision;
	Wall->Wall.Height = 280.0;
	Wall->Regenerate();

	TestTrue(TEXT("Regenerating bumps the mesh revision"), Wall->MeshRevision > RevisionBefore);
	TestTrue(TEXT("Which makes the bake stale"), Wall->IsBakeStale());

	// And RebakeStale is what fixes it, without touching anything that is current.
	FHFBakeService::RebakeStale(World, Report);
	TestFalse(TEXT("RebakeStale brings it back up to date"), Wall->IsBakeStale());
	TestEqual(TEXT("And leaves it baked"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));

	return true;
}

/**
 * A parameter change on a baked element re-bakes it, by default.
 *
 * Off, the viewport draws the previous plan's geometry while the spec says something else - and
 * CaptureTopDown, the tool Claude uses to check its own work, photographs the lie.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeAutoRebakeTest,
	"HouseForge.Bake.RegenerateWhileBakedRebakes", HF_TEST_FLAGS)

bool FHFBakeAutoRebakeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_AutoRebake")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	const FBox BeforeBounds = Wall->BakedParts[0].BakedMesh->GetBoundingBox();

	Wall->Wall.Height = 240.0;
	Wall->Regenerate();

	TestEqual(TEXT("The element is still baked after a parameter change"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));
	TestFalse(TEXT("And it is not stale, because it re-baked itself"), Wall->IsBakeStale());

	const FBox AfterBounds = Wall->BakedParts[0].BakedMesh->GetBoundingBox();
	AddInfo(FString::Printf(TEXT("Baked asset height before %.1f, after %.1f"),
		BeforeBounds.GetSize().Z, AfterBounds.GetSize().Z));

	TestTrue(TEXT("The baked asset now holds the NEW geometry, so the viewport is not lying"),
		AfterBounds.GetSize().Z < BeforeBounds.GetSize().Z - 1.0);

	return true;
}

// ============================================================================ missing assets

/**
 * A missing asset never leaves a hole in the flat.
 *
 * Force-delete the asset, reopen the level, and the element has to come back drawing its live mesh
 * with a reason recorded - not invisible, and not a crash.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeMissingAssetTest,
	"HouseForge.Bake.MissingAssetFallsBackToDynamic", HF_TEST_FLAGS)

bool FHFBakeMissingAssetTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Missing")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	const FSoftObjectPath Path = Wall->BakedParts[0].BakedAssetPath;
	TestTrue(TEXT("The bake recorded where the asset lives, separately from the pointer"), Path.IsValid());

	// Exactly what a force-delete leaves behind: a null pointer, and a path that still names it.
	Wall->BakedParts[0].BakedMesh = nullptr;
	Wall->ReconcileBakeState();

	TestEqual(TEXT("An element whose asset went missing comes back on its live mesh"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));
	TestTrue(TEXT("And says so, rather than failing silently"), Wall->bBakeAssetMissing);
	TestTrue(TEXT("The live mesh is intact, so nothing is invisible"),
		HFBakeTest::Print(Wall->GetMeshComponent()).Triangles > 0);
	TestTrue(TEXT("The path is kept so the report can name what went missing"),
		Wall->BakedParts[0].BakedAssetPath == Path);

	// And a re-bake puts it right without any manual repair.
	TestTrue(TEXT("Re-baking restores it"), FHFBakeService::BakeElement(Wall, Report));
	TestFalse(TEXT("The missing flag clears"), Wall->bBakeAssetMissing);

	return true;
}

// ================================================================================= surfaces

/**
 * The material panel must still be able to reach a baked element by surface role.
 *
 * A bake that lost the roles would make every baked element unreachable from the panel - and since
 * a whole flat must be baked to be visible to Lumen at all, that would mean the flat could be lit
 * or materialled but not both.
 *
 * Asserted through the MATERIAL SECTIONS rather than the polygroups. Both survive - measured - but
 * sections are what the renderer actually uses, and the index arithmetic is free: sections come out
 * dense from 0, and a non-empty section's index IS the role index. Note what is NOT asserted:
 * section count equal to role count. Sections are dense and include empties.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSurfaceRolesTest,
	"HouseForge.Bake.SurfaceRolesReachTheBakedElement", HF_TEST_FLAGS)

bool FHFBakeSurfaceRolesTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Roles")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	TSet<EHFSurfaceRole> RolesBefore;
	Wall->GetMeshComponent()->GetDynamicMesh()->ProcessMesh([&RolesBefore](const FDynamicMesh3& Mesh)
	{
		RolesBefore = FHFMeshOps::RolesPresent(Mesh);
	});
	TestTrue(TEXT("The live wall carries at least one surface role"), RolesBefore.Num() > 0);

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	UStaticMesh* Asset = Wall->BakedParts[0].BakedMesh;
	if (!TestNotNull(TEXT("The bake produced an asset"), Asset))
	{
		return false;
	}

	TestEqual(TEXT("The baked asset has one material slot per surface role, so slot index means role index"),
		Asset->GetStaticMaterials().Num(), FHFMeshOps::NumSurfaceRoles());

	const FMeshDescription* Description = Asset->GetMeshDescription(0);
	if (!TestNotNull(TEXT("The committed mesh description reads back"), Description))
	{
		return false;
	}

	TSet<int32> NonEmptySections;
	FString SectionList;
	for (const FPolygonGroupID GroupID : Description->PolygonGroups().GetElementIDs())
	{
		const int32 Triangles = Description->GetPolygonGroupPolygons(GroupID).Num();
		if (Triangles > 0)
		{
			NonEmptySections.Add(GroupID.GetValue());
			SectionList += FString::Printf(TEXT("%d(role %d, %d tris) "), GroupID.GetValue(),
				static_cast<int32>(FHFMeshOps::RoleForMaterialId(GroupID.GetValue())), Triangles);
		}
	}
	AddInfo(FString::Printf(TEXT("Roles in: %d. Non-empty sections out: %s"), RolesBefore.Num(), *SectionList));

	for (const EHFSurfaceRole Role : RolesBefore)
	{
		TestTrue(FString::Printf(TEXT("Role %d survives the bake as a section at its own index"),
			static_cast<int32>(Role)),
			NonEmptySections.Contains(FHFMeshOps::MaterialIdForRole(Role)));
	}

	// And the component through which a finish change is written has the slots to write into.
	UStaticMeshComponent* Baked = Wall->BakedParts[0].Component;
	if (TestNotNull(TEXT("There is a baked component"), Baked))
	{
		TestEqual(TEXT("The baked component carries a material override per role, so the panel can reach it"),
			Baked->GetNumMaterials(), FHFMeshOps::NumSurfaceRoles());
	}

	return true;
}

// =============================================================================== articulation

/**
 * "A bake must not weld a chest of drawers into a block." - .claude/rules/04-conventions.md
 *
 * Tested on the one articulated element that exists today: a door, whose leaf swings. One baked part
 * per source component, each parented to the dynamic component it stands in for, and the leaf still
 * moves after the bake - because the baked component inherits that part's pose for free.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeArticulationTest,
	"HouseForge.Bake.ArticulatedBakeKeepsPartsSeparate", HF_TEST_FLAGS)

bool FHFBakeArticulationTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFOpeningActor* Door = World ? HFBakeTest::SpawnDoor(World, TEXT("D_Bake")) : nullptr;
	if (!TestNotNull(TEXT("A door spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Door); if (IsValid(Door)) { Door->Destroy(); } };

	if (!TestTrue(TEXT("The door has a moving part to keep moving"), Door->NumParts() > 0))
	{
		return false;
	}

	TArray<UDynamicMeshComponent*> Sources;
	Door->GetBakeSourceComponents(Sources);
	AddInfo(FString::Printf(TEXT("The door presents %d bake source component(s) for %d moving part(s)."),
		Sources.Num(), Door->NumParts()));

	TestEqual(TEXT("The bake sources are the shell plus one per moving part"),
		Sources.Num(), Door->NumParts() + 1);

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The door bakes"), FHFBakeService::BakeElement(Door, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	TestEqual(TEXT("One baked part per source component - the fixture is not welded into a block"),
		Door->BakedParts.Num(), Sources.Num());

	// Distinct assets. One asset shared between the frame and the leaf would be a welded door with
	// extra steps.
	TSet<UStaticMesh*> Distinct;
	for (const FHFBakedPart& Part : Door->BakedParts)
	{
		if (Part.BakedMesh != nullptr)
		{
			Distinct.Add(Part.BakedMesh);
		}
	}
	TestEqual(TEXT("Every part got its own asset"), Distinct.Num(), Door->BakedParts.Num());

	// AND IT STILL OPENS. The baked component is parented to the dynamic part component, so posing
	// the part moves the baked geometry with it.
	const FName LeafId = Door->Parts[0].PartId;
	UStaticMeshComponent* BakedLeaf = Door->BakedParts.Num() > 1 ? Door->BakedParts[1].Component : nullptr;
	if (!TestNotNull(TEXT("The leaf has a baked component"), BakedLeaf))
	{
		return false;
	}

	const FTransform ShutWorld = BakedLeaf->GetComponentTransform();
	TestTrue(TEXT("The leaf accepts an open amount"), Door->SetPartOpenAmount(LeafId, 1.0));
	const FTransform OpenWorld = BakedLeaf->GetComponentTransform();

	AddInfo(FString::Printf(TEXT("Baked leaf moved %.1f cm and turned %.1f degrees when opened."),
		FVector::Dist(ShutWorld.GetLocation(), OpenWorld.GetLocation()),
		FMath::Abs(OpenWorld.Rotator().Yaw - ShutWorld.Rotator().Yaw)));

	TestTrue(TEXT("The BAKED leaf moves when the door opens - the articulation survives the bake"),
		!OpenWorld.Equals(ShutWorld, 0.01f));

	return true;
}

// ============================================================================= assets on disk

/**
 * A re-bake updates the same asset rather than minting a second one beside it, and the provenance
 * stamp survives the update.
 *
 * The stamp is what makes orphan deletion safe. Measured: re-creating an asset over an existing one
 * of the same name re-runs its constructor and RESETS the AssetUserData array, so a re-create would
 * silently un-stamp it. That is why the repeat path writes a mesh description instead.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeRebakeSameAssetTest,
	"HouseForge.Bake.RebakeReusesTheSameStampedAsset", HF_TEST_FLAGS)

bool FHFBakeRebakeSameAssetTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Rebake")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	UStaticMesh* First = Wall->BakedParts[0].BakedMesh;
	const FString FirstPath = First->GetPathName();
	const FGuid OwnerGuid = Wall->BakeOwnerGuid;
	TestTrue(TEXT("The element has an owner guid"), OwnerGuid.IsValid());

	const UHFBakedMeshUserData* Stamp =
		Cast<UHFBakedMeshUserData>(First->GetAssetUserDataOfClass(UHFBakedMeshUserData::StaticClass()));
	if (!TestNotNull(TEXT("The baked asset carries its provenance stamp"), Stamp))
	{
		return false;
	}
	TestEqual(TEXT("The stamp names the element"), Stamp->ElementId, Wall->ElementId);
	TestEqual(TEXT("And the level, which is what makes orphan deletion safe"),
		Stamp->LevelPackageName, World->GetOutermost()->GetFName());
	TestEqual(TEXT("And the owner"), Stamp->OwnerGuid, OwnerGuid);

	Wall->Wall.Height = 260.0;
	Wall->Regenerate();

	UStaticMesh* Second = Wall->BakedParts[0].BakedMesh;
	if (!TestNotNull(TEXT("The re-bake produced an asset"), Second))
	{
		return false;
	}

	TestEqual(TEXT("A re-bake lands at the same asset path, so nothing is duplicated"),
		Second->GetPathName(), FirstPath);
	TestEqual(TEXT("The owner guid is unchanged, so undo and redo cannot mint a duplicate"),
		Wall->BakeOwnerGuid, OwnerGuid);

	const UHFBakedMeshUserData* AfterStamp =
		Cast<UHFBakedMeshUserData>(Second->GetAssetUserDataOfClass(UHFBakedMeshUserData::StaticClass()));
	if (TestNotNull(TEXT("The stamp survives a re-bake"), AfterStamp))
	{
		TestEqual(TEXT("And is re-applied with the new revision"),
			AfterStamp->SourceMeshRevision, Wall->MeshRevision);
	}

	return true;
}

// =========================================================================== house-wide bake

/**
 * A house rebuild must not destroy a baked element underneath its own asset.
 *
 * It would orphan one asset per element, silently, on every rebuild of a fully baked flat - and the
 * flat would come back Dynamic, which since the Lumen measurement means invisible to Lumen and
 * rendering BRIGHTER than the truth. A preserved-because-baked element still takes the new
 * parameters, unlike a hand-edited one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeHouseRebuildTest,
	"HouseForge.Editor.Bake.HouseRebuildPreservesBakedElements", HF_TEST_FLAGS)

bool FHFBakeHouseRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->Destroy(); } };

	// A small hand-made spec rather than the sample flat: this test is about the rebuild predicate,
	// and 155 elements would make it slow without making it any more conclusive.
	FHFHouseSpec Spec;
	Spec.Name = TEXT("BakeRebuild");
	Spec.Units = EHFUnits::Centimeters;

	FHFWall& WallA = Spec.Walls.AddDefaulted_GetRef();
	WallA.Id = TEXT("W1");
	WallA.Start = FVector2D(0.0, 0.0);
	WallA.End = FVector2D(400.0, 0.0);
	WallA.Thickness = 20.0;
	WallA.Height = 300.0;

	FHFWall& WallB = Spec.Walls.AddDefaulted_GetRef();
	WallB.Id = TEXT("W2");
	WallB.Start = FVector2D(400.0, 0.0);
	WallB.End = FVector2D(400.0, 300.0);
	WallB.Thickness = 20.0;
	WallB.Height = 300.0;

	House->SetSpec(Spec);

	AHFWallActor* Baked = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFWallActor* Typed = Cast<AHFWallActor>(Element))
		{
			if (Typed->ElementId == FName(TEXT("W1")))
			{
				Baked = Typed;
				break;
			}
		}
	}
	if (!TestNotNull(TEXT("The house built a wall W1"), Baked))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Baked); };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("W1 bakes"), FHFBakeService::BakeElement(Baked, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	const FSoftObjectPath AssetPath = Baked->BakedParts[0].BakedAssetPath;
	const FGuid OwnerGuid = Baked->BakeOwnerGuid;

	// The spec changes and the house rebuilds - the everyday loop after a misread is corrected.
	Spec.Walls[0].Height = 275.0;
	House->SetSpec(Spec);

	TestTrue(TEXT("The baked element actor survives a rebuild rather than being destroyed"),
		IsValid(Baked));
	if (!IsValid(Baked))
	{
		return false;
	}

	TestTrue(TEXT("It is still in the house's element list"),
		House->ElementActors.Contains(Baked));
	TestEqual(TEXT("It kept its asset, so nothing was orphaned"),
		Baked->BakedParts[0].BakedAssetPath, AssetPath);
	TestEqual(TEXT("And its owner guid"), Baked->BakeOwnerGuid, OwnerGuid);
	TestEqual(TEXT("It is still showing baked geometry, so the flat stays visible to Lumen"),
		static_cast<int32>(Baked->RenderMode), static_cast<int32>(EHFRenderMode::Baked));

	// AND IT TOOK THE NEW PARAMETERS. A baked element is preserved for its asset, not because it is
	// precious: unlike a hand-edited one it must still follow the spec.
	TestEqual(TEXT("The preserved element took the new wall height from the spec"),
		Baked->Wall.Height, 275.0);
	TestFalse(TEXT("And re-baked itself, so the baked geometry matches the new spec"),
		Baked->IsBakeStale());

	return true;
}

/**
 * THE FOURTH COMBINATION: hand-edited AND baked, through a house rebuild.
 *
 * bArtistEdited and RenderMode are orthogonal, and the rebuild sorts elements into two buckets by
 * asking about the first - Preserved for hand-edited, PreservedForBake for the rest. An element
 * that is both lands in the first bucket, and the question this asks is whether it keeps the
 * SECOND property on the way through.
 *
 * Both losses are silent and both are bad in different ways. Lose the sculpt and an afternoon of
 * modelling is gone. Keep the sculpt but lose the bake and the element drops out of the Lumen
 * scene while every neighbour stays in it - so the flat renders with one wall letting sky through,
 * brighter and wronger, which is the failure mode the Lumen measurement showed is hardest to see.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeEditedAndBakedRebuildTest,
	"HouseForge.Editor.Bake.HouseRebuildKeepsAnElementThatIsBothEditedAndBaked", HF_TEST_FLAGS)

bool FHFBakeEditedAndBakedRebuildTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
	if (!TestNotNull(TEXT("A house spawns"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->Destroy(); } };

	FHFHouseSpec Spec;
	Spec.Name = TEXT("BakeEditedRebuild");
	Spec.Units = EHFUnits::Centimeters;

	FHFWall& Wall = Spec.Walls.AddDefaulted_GetRef();
	Wall.Id = TEXT("W1");
	Wall.Start = FVector2D(0.0, 0.0);
	Wall.End = FVector2D(400.0, 0.0);
	Wall.Thickness = 20.0;
	Wall.Height = 300.0;

	House->SetSpec(Spec);

	AHFWallActor* Element = nullptr;
	for (AActor* Actor : House->ElementActors)
	{
		if (AHFWallActor* Typed = Cast<AHFWallActor>(Actor))
		{
			Element = Typed;
			break;
		}
	}
	if (!TestNotNull(TEXT("The house built a wall"), Element))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Element); };

	// Sculpted first, then baked - so the asset holds the sculpted form, which is the order that
	// makes the bake worth preserving at all.
	HFBakeTest::SculptFirstVertex(Element->GetMeshComponent(), FVector3d(0.0, 0.0, 23.0));
	if (!TestTrue(TEXT("The sculpt registered as a hand edit"), Element->bArtistEdited))
	{
		return false;
	}

	FHFBakeReport Report;
	if (!TestTrue(TEXT("A hand-edited element bakes"), FHFBakeService::BakeElement(Element, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	const HFBakeTest::FMeshPrint Sculpted = HFBakeTest::Print(Element->GetMeshComponent());
	const FSoftObjectPath AssetPath = Element->BakedParts[0].BakedAssetPath;

	// The everyday loop: the spec is corrected and the house rebuilds underneath it.
	Spec.Walls[0].Height = 270.0;
	House->SetSpec(Spec);

	if (!TestTrue(TEXT("The element survives the rebuild"), IsValid(Element)))
	{
		return false;
	}

	TestTrue(TEXT("It is still marked hand-edited"), Element->bArtistEdited);
	TestTrue(TEXT("Its sculpt is untouched - a rebuild never regenerates over modelling work"),
		Sculpted == HFBakeTest::Print(Element->GetMeshComponent()));
	TestNotEqual(TEXT("And it kept the sculpted height rather than taking the spec's"),
		Element->Wall.Height, 270.0);

	// The half that is easy to lose: the bake.
	TestEqual(TEXT("It is still showing baked geometry, so it stays in the Lumen scene with its neighbours"),
		static_cast<int32>(Element->RenderMode), static_cast<int32>(EHFRenderMode::Baked));
	TestTrue(TEXT("It still owns its asset, so nothing was orphaned"), Element->HasAllBakedAssets());
	TestEqual(TEXT("And it is the same asset"), Element->BakedParts[0].BakedAssetPath, AssetPath);
	TestFalse(TEXT("Which is not stale, because nothing regenerated"), Element->IsBakeStale());

	// And it can still be switched back, which is the whole promise.
	FHFBakeService::UnbakeElement(Element, Report);
	TestTrue(TEXT("Unbaking after a rebuild still gives back the sculpted form exactly"),
		Sculpted == HFBakeTest::Print(Element->GetMeshComponent()));

	return true;
}

/**
 * Bulk bake and unbake over a whole level, and the tally that reports it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeBulkTest,
	"HouseForge.Editor.Bake.BulkBakeAndUnbakeTheWholeHouse", HF_TEST_FLAGS)

bool FHFBakeBulkTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	TArray<AHFWallActor*> Walls;
	ON_SCOPE_EXIT
	{
		for (AHFWallActor* Wall : Walls)
		{
			HFBakeTest::ForgetAssets(Wall);
			if (IsValid(Wall)) { Wall->Destroy(); }
		}
	};

	for (int32 Index = 0; Index < 3; ++Index)
	{
		AHFWallActor* Wall = HFBakeTest::SpawnWall(World, *FString::Printf(TEXT("W_Bulk%d"), Index));
		if (Wall != nullptr)
		{
			Walls.Add(Wall);
		}
	}
	if (!TestEqual(TEXT("Three walls spawn"), Walls.Num(), 3))
	{
		return false;
	}

	// Selection scope: the three walls, and nothing else in the level.
	TArray<AHFElementActor*> Selection;
	for (AHFWallActor* Wall : Walls)
	{
		Selection.Add(Wall);
	}

	FHFBakeReport Report;
	FHFBakeService::SetRenderModeMany(Selection, /*bBaked*/ true, Report);
	AddInfo(Report.Summary());

	TestEqual(TEXT("Every element in the selection baked"), Report.ElementsBaked, 3);
	TestEqual(TEXT("Nothing failed"), Report.ElementsFailed, 0);

	for (AHFWallActor* Wall : Walls)
	{
		TestEqual(TEXT("Each wall is showing baked geometry"),
			static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Baked));
	}

	int32 BakedCount = 0;
	int32 Total = 0;
	int32 Stale = 0;
	FHFBakeService::CountBakeState(World, BakedCount, Total, Stale);
	AddInfo(FString::Printf(TEXT("Level tally: %d of %d baked, %d stale."), BakedCount, Total, Stale));
	TestTrue(TEXT("The tally counts at least the three walls as baked"), BakedCount >= 3);
	TestEqual(TEXT("None of them are stale"), Stale, 0);

	// Baking again over an already-current bake must not rewrite the assets - that is the difference
	// between "Bake all" taking two seconds and taking two minutes on a real flat.
	UStaticMesh* FirstAsset = Walls[0]->BakedParts[0].BakedMesh;
	FHFBakeReport Second;
	FHFBakeService::SetRenderModeMany(Selection, /*bBaked*/ true, Second);
	TestTrue(TEXT("Re-baking an already-current element keeps the same asset"),
		Walls[0]->BakedParts[0].BakedMesh == FirstAsset);

	FHFBakeService::SetRenderModeMany(Selection, /*bBaked*/ false, Report);

	for (AHFWallActor* Wall : Walls)
	{
		TestEqual(TEXT("Each wall is back on its live mesh"),
			static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));
		TestTrue(TEXT("And kept its asset, so switching back is free"), Wall->HasAllBakedAssets());
	}

	return true;
}

// ================================================================================ durability

/**
 * A BAKE REACHES DISK, or it is a promise for one session only.
 *
 * A baked element holds a hard reference to its UStaticMesh, and saving the LEVEL does not save
 * the asset packages the level references. In the interactive editor the Save Content dialog
 * covers that gap by listing dependencies. There is no dialog on the path this plugin is driven
 * down: a model bakes over MCP and saves the level, with nobody at a prompt.
 *
 * Unsaved, the next editor start finds no asset, ReconcileBakeState falls the element back to
 * Dynamic, and the flat silently leaves the Lumen scene - which by the measurement behind this
 * milestone renders BRIGHTER than the truth rather than obviously broken. The morning's render
 * looks bright, cheerful and wrong.
 *
 * This is the one test that opts saving back ON during automation, and it removes the file it
 * wrote afterwards so the gate leaves nothing in the user's Content folder.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSavesToDiskTest,
	"HouseForge.Editor.Bake.BakedAssetsAreWrittenToDisk", HF_TEST_FLAGS)

bool FHFBakeSavesToDiskTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Durable")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}

	FString WrittenFile;
	ON_SCOPE_EXIT
	{
		// The gate must not deposit user output. Removed whether the test passed or failed.
		if (!WrittenFile.IsEmpty())
		{
			IFileManager::Get().Delete(*WrittenFile, /*RequireExists*/ false, /*EvenReadOnly*/ true);

			// And the folder the save created, but ONLY if it is empty - non-recursive on purpose.
			// This path is where a user's real baked assets live, so the one thing this cleanup must
			// never do is take a directory that has anything in it.
			IFileManager::Get().DeleteDirectory(*FPaths::GetPath(WrittenFile),
				/*RequireExists*/ false, /*Tree*/ false);
		}
		HFBakeTest::ForgetAssets(Wall);
		if (IsValid(Wall)) { Wall->Destroy(); }
	};

	FHFBakeReport Report;
	{
		// Deliberately opting back in to the thing automation otherwise switches off.
		FHFBakeSaveScope AllowSave(true);
		if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
		{
			AddError(Report.Summary());
			return false;
		}
	}

	AddInfo(Report.Summary());

	if (!TestTrue(TEXT("The bake produced an asset"), Wall->HasAllBakedAssets()))
	{
		return false;
	}

	TestTrue(TEXT("The bake reports having written at least one package"), Report.PackagesSaved > 0);

	UPackage* Package = Wall->BakedParts[0].BakedMesh->GetOutermost();
	if (!TestNotNull(TEXT("The asset has a package"), Package))
	{
		return false;
	}

	WrittenFile = FPackageName::LongPackageNameToFilename(
		Package->GetName(), FPackageName::GetAssetPackageExtension());

	AddInfo(FString::Printf(TEXT("Expected on disk at %s"), *WrittenFile));

	// THE ASSERTION THAT MATTERS: the file exists. A dirty flag cleared in memory would satisfy
	// every other check here and still be gone in the morning.
	TestTrue(TEXT("The baked asset is on disk, so the bake survives closing the editor"),
		FPaths::FileExists(WrittenFile));
	TestFalse(TEXT("And its package is no longer dirty"), Package->IsDirty());

	return true;
}

/**
 * And the suite as a whole does NOT write, which is the other half of the same decision.
 *
 * Rule 01: what lands in the project's Content folder is the user's output. A validation gate that
 * deposited a fresh set of test assets on every pass would be generating user output as a side
 * effect of testing, and the drift would be indistinguishable from a real bake.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeAutomationDoesNotWriteTest,
	"HouseForge.Editor.Bake.AutomationDoesNotLeaveAssetsBehind", HF_TEST_FLAGS)

bool FHFBakeAutomationDoesNotWriteTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_NotOnDisk")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	// No scope: exactly what every other test in this file gets.
	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	TestEqual(TEXT("An ordinary automation bake writes no packages at all"), Report.PackagesSaved, 0);

	const FString File = FPackageName::LongPackageNameToFilename(
		Wall->BakedParts[0].BakedMesh->GetOutermost()->GetName(),
		FPackageName::GetAssetPackageExtension());

	TestFalse(TEXT("So nothing was left in the project's Content folder"), FPaths::FileExists(File));

	return true;
}

// ================================================================================== orphans

/**
 * An orphan scan sees only the open level.
 *
 * An asset stamped for a different level belongs to a level that is not open, whose elements cannot
 * be asked whether they still want it - so however unreferenced it looks from here, it is never a
 * deletion candidate. An unstamped asset was not made by HouseForge and is likewise left alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeOrphanScopeTest,
	"HouseForge.Editor.Bake.OrphanScanIgnoresOtherLevels", HF_TEST_FLAGS)

bool FHFBakeOrphanScopeTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Orphan")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	UStaticMesh* Asset = Wall->BakedParts[0].BakedMesh;
	const FSoftObjectPath Path = Wall->BakedParts[0].BakedAssetPath;
	ON_SCOPE_EXIT
	{
		if (IsValid(Asset) && Asset->GetOutermost()) { Asset->GetOutermost()->SetDirtyFlag(false); }
		if (IsValid(Wall)) { Wall->Destroy(); }
	};

	// While the element still claims it, it is not an orphan.
	TArray<FAssetData> Orphans;
	FHFBakeService::FindOrphans(World, Orphans);
	bool bClaimedAppears = false;
	for (const FAssetData& Data : Orphans)
	{
		bClaimedAppears |= (Data.GetSoftObjectPath() == Path);
	}
	TestFalse(TEXT("An asset an element still claims is never reported as an orphan"), bClaimedAppears);

	// Re-stamp it for a level that is not open. It is now unclaimed by anything in this level, and it
	// must STILL not be offered for deletion.
	UHFBakedMeshUserData* Stamp =
		Cast<UHFBakedMeshUserData>(Asset->GetAssetUserDataOfClass(UHFBakedMeshUserData::StaticClass()));
	if (!TestNotNull(TEXT("The asset carries a stamp to re-write"), Stamp))
	{
		return false;
	}

	Wall->BakedParts.Reset();
	Stamp->LevelPackageName = FName(TEXT("/Game/Maps/SomeOtherLevel"));

	Orphans.Reset();
	FHFBakeService::FindOrphans(World, Orphans);

	bool bForeignAppears = false;
	for (const FAssetData& Data : Orphans)
	{
		bForeignAppears |= (Data.GetSoftObjectPath() == Path);
	}
	AddInfo(FString::Printf(TEXT("Orphan scan returned %d candidate(s) for this level."), Orphans.Num()));
	TestFalse(TEXT("An asset stamped for another level is never an orphan candidate here"), bForeignAppears);

	// Stamp it back to this level and it becomes one, which proves the scan is discriminating rather
	// than simply returning nothing.
	Stamp->LevelPackageName = World->GetOutermost()->GetFName();
	Orphans.Reset();
	FHFBakeService::FindOrphans(World, Orphans);

	bool bOwnAppears = false;
	for (const FAssetData& Data : Orphans)
	{
		bOwnAppears |= (Data.GetSoftObjectPath() == Path);
	}
	TestTrue(TEXT("An unclaimed asset stamped for THIS level is reported, so the scan discriminates"),
		bOwnAppears);

	return true;
}

// ================================================================================ small print

/**
 * Flipping the render switch in the details panel must not rebuild geometry.
 *
 * RenderMode is declared on AHFElementActor, and the catch-all in PostEditChangeProperty regenerates
 * for any property that is. Without the interception, ticking the box would regenerate the element,
 * bump the revision, and mark the bake it was just asked to show stale.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeSwitchDoesNotRegenerateTest,
	"HouseForge.Bake.RenderModeSwitchDoesNotRegenerate", HF_TEST_FLAGS)

bool FHFBakeSwitchDoesNotRegenerateTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFWallActor* Wall = World ? HFBakeTest::SpawnWall(World, TEXT("W_Switch")) : nullptr;
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The wall bakes"), FHFBakeService::BakeElement(Wall, Report)))
	{
		return false;
	}

	const int32 RevisionAfterBake = Wall->MeshRevision;

	// Exactly what the details panel does when the dropdown changes.
	Wall->RenderMode = EHFRenderMode::Dynamic;
	FPropertyChangedEvent Event(
		AHFElementActor::StaticClass()->FindPropertyByName(
			GET_MEMBER_NAME_CHECKED(AHFElementActor, RenderMode)));
	Wall->PostEditChangeProperty(Event);

	TestEqual(TEXT("The switch took effect"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));
	TestEqual(TEXT("Flipping the switch did not regenerate the element"),
		Wall->MeshRevision, RevisionAfterBake);
	TestFalse(TEXT("So the bake it was just asked about is not marked stale by asking"),
		Wall->IsBakeStale());

	return true;
}

/**
 * A part that stops being generated takes its baked component with it, and its asset is REPORTED
 * rather than deleted.
 *
 * Left attached to a destroyed parent, a baked drawer front would draw in world space at the actor
 * origin - a drawer front lying in the middle of the room. Deleting the asset instead would be a
 * regeneration path silently destroying a user's asset, which is not something this plugin does.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeDroppedPartTest,
	"HouseForge.Bake.DroppedPartDropsItsBakedComponent", HF_TEST_FLAGS)

bool FHFBakeDroppedPartTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	AHFOpeningActor* Door = World ? HFBakeTest::SpawnDoor(World, TEXT("D_Drop")) : nullptr;
	if (!TestNotNull(TEXT("A door spawns"), Door))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Door); if (IsValid(Door)) { Door->Destroy(); } };

	Door->bAutoRebakeOnRegenerate = false;

	FHFBakeReport Report;
	if (!TestTrue(TEXT("The door bakes"), FHFBakeService::BakeElement(Door, Report)))
	{
		AddError(Report.Summary());
		return false;
	}

	const int32 PartsWhenDoor = Door->BakedParts.Num();
	TestTrue(TEXT("A door bakes more than one part"), PartsWhenDoor > 1);

	// An archway has no leaf, so the leaf part disappears.
	Door->Opening.Kind = EHFOpeningKind::Archway;
	Door->Regenerate();

	TArray<UDynamicMeshComponent*> Sources;
	Door->GetBakeSourceComponents(Sources);
	AddInfo(FString::Printf(TEXT("Door baked %d part(s); as an archway it has %d source(s) and %d baked part(s)."),
		PartsWhenDoor, Sources.Num(), Door->BakedParts.Num()));

	TestEqual(TEXT("The baked part list follows the source components down"),
		Door->BakedParts.Num(), Sources.Num());
	TestTrue(TEXT("Which is fewer than it was"), Door->BakedParts.Num() < PartsWhenDoor);

	// No baked component may be left dangling on a destroyed parent.
	for (const FHFBakedPart& Part : Door->BakedParts)
	{
		if (Part.Component != nullptr)
		{
			TestNotNull(TEXT("Every surviving baked component still has a parent to hang on"),
				Part.Component->GetAttachParent());
		}
	}

	return true;
}

/**
 * A degenerate element does not fail a bulk bake.
 *
 * An element with no triangles bakes correctly by producing nothing. Without this one wall whose
 * openings have eaten all of it turns "Bake all" over a whole flat red, for an element nobody can
 * see in either mode.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBakeEmptyElementTest,
	"HouseForge.Bake.EmptyElementDoesNotFailTheBake", HF_TEST_FLAGS)

bool FHFBakeEmptyElementTest::RunTest(const FString& Parameters)
{
	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWallActor* Wall = World->SpawnActor<AHFWallActor>();
	if (!TestNotNull(TEXT("A wall spawns"), Wall))
	{
		return false;
	}
	ON_SCOPE_EXIT{ HFBakeTest::ForgetAssets(Wall); if (IsValid(Wall)) { Wall->Destroy(); } };

	// Zero length, so the generator produces nothing at all.
	Wall->ElementId = TEXT("W_Empty");
	Wall->Wall.Id = TEXT("W_Empty");
	Wall->Wall.Start = FVector2D(0.0, 0.0);
	Wall->Wall.End = FVector2D(0.0, 0.0);
	Wall->Wall.Thickness = 20.0;
	Wall->Wall.Height = 300.0;
	Wall->Regenerate();

	const int32 Triangles = HFBakeTest::Print(Wall->GetMeshComponent()).Triangles;
	AddInfo(FString::Printf(TEXT("The degenerate wall has %d triangle(s)."), Triangles));
	if (!TestEqual(TEXT("The degenerate wall really is empty"), Triangles, 0))
	{
		return false;
	}

	FHFBakeReport Report;
	FHFBakeService::BakeElement(Wall, Report);
	AddInfo(Report.Summary());

	// The important half: it does not report a FAILURE, and it does not claim to be baked either -
	// a switch with nothing on either side of it is not a switch.
	TestEqual(TEXT("An empty element is not a bake failure"), Report.ElementsFailed, 0);
	TestFalse(TEXT("And it does not claim to have baked geometry"), Wall->HasAllBakedAssets());
	TestEqual(TEXT("So it stays on its live mesh"),
		static_cast<int32>(Wall->RenderMode), static_cast<int32>(EHFRenderMode::Dynamic));

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
