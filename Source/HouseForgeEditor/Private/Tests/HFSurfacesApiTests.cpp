// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMeshAttributeSet.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "HFEditorSubsystem.h"
#include "Materials/HFMaterialLibrary.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Misc/AutomationTest.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"
#include "UDynamicMesh.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	UWorld* EditorWorld()
	{
		return GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	}

	UHFEditorSubsystem* Subsystem()
	{
		return GEditor ? GEditor->GetEditorSubsystem<UHFEditorSubsystem>() : nullptr;
	}

	FString RoleName(EHFSurfaceRole Role)
	{
		return StaticEnum<EHFSurfaceRole>()->GetNameStringByValue(static_cast<int64>(Role));
	}

	/**
	 * PUTS THE SHIPPED LIBRARY BACK, whatever the test did to it.
	 *
	 * These tests write real finishes onto the real asset, because a test that writes to a copy
	 * proves nothing about the path the panel uses. The asset is plugin content committed to this
	 * repository, so leaving a test value in it would be a change to shipped material, and a later
	 * test asserting the shipped figures would fail on whichever order the suite happened to run in.
	 */
	struct FScopedLibraryRestore
	{
		FScopedLibraryRestore()
		{
			if (UHFMaterialLibrary* Library = UHFMaterialLibrary::Get())
			{
				Saved = Library->Finishes;
				bHeld = true;
			}
		}

		~FScopedLibraryRestore()
		{
			if (!bHeld)
			{
				return;
			}

			if (UHFMaterialLibrary* Library = UHFMaterialLibrary::Get())
			{
				Library->Finishes = Saved;

				// Pushed, not merely restored. The material instances are the library compiled for
				// the renderer, so a library put back without a push would leave every instance
				// holding the test's values while the library said otherwise - the exact split
				// between record and render that PushFinish exists to prevent.
				Library->PushAllFinishes(EHFMaterialPush::Commit);
			}
		}

		TMap<EHFSurfaceRole, FHFSurfaceFinish> Saved;
		bool bHeld = false;
	};

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

	/**
	 * ENOUGH OF EVERY MESH IN THE LEVEL TO CATCH A CHANGE TO ANY OF IT.
	 *
	 * Vertex positions and triangle indices go into one order-sensitive hash, and the surface-role
	 * polygroups go into a SET beside it rather than into the same hash. The set is the point: a
	 * material pass that renumbered polygroups while leaving every vertex where it was would pass a
	 * position-only comparison, and the flat would go on rendering in the wrong finishes with the
	 * mechanism for fixing it gone.
	 */
	struct FLevelFingerprint
	{
		uint32 GeometryHash = 0;
		int32 TriangleCount = 0;
		int32 ComponentCount = 0;
		TSet<int32> PolygroupIds;
		TSet<int32> MaterialIds;

		bool operator==(const FLevelFingerprint& Other) const
		{
			return GeometryHash == Other.GeometryHash
				&& TriangleCount == Other.TriangleCount
				&& ComponentCount == Other.ComponentCount
				&& PolygroupIds.Difference(Other.PolygroupIds).IsEmpty()
				&& Other.PolygroupIds.Difference(PolygroupIds).IsEmpty()
				&& MaterialIds.Difference(Other.MaterialIds).IsEmpty()
				&& Other.MaterialIds.Difference(MaterialIds).IsEmpty();
		}
	};

	FLevelFingerprint FingerprintLevel(UWorld* World)
	{
		FLevelFingerprint Print;

		TArray<UDynamicMeshComponent*> Components;
		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			It->GetComponents<UDynamicMeshComponent>(Components);
			for (UDynamicMeshComponent* Component : Components)
			{
				UDynamicMesh* Wrapper = Component ? Component->GetDynamicMesh() : nullptr;
				if (Wrapper == nullptr)
				{
					continue;
				}

				++Print.ComponentCount;
				Wrapper->ProcessMesh([&Print](const FDynamicMesh3& Mesh)
				{
					const FDynamicMeshMaterialAttribute* MaterialIds =
						Mesh.HasAttributes() ? Mesh.Attributes()->GetMaterialID() : nullptr;

					for (const int32 TriangleId : Mesh.TriangleIndicesItr())
					{
						++Print.TriangleCount;

						const FIndex3i Tri = Mesh.GetTriangle(TriangleId);
						for (int32 Corner = 0; Corner < 3; ++Corner)
						{
							const FVector3d P = Mesh.GetVertex(Tri[Corner]);
							Print.GeometryHash = HashCombine(Print.GeometryHash, GetTypeHash(P));
						}

						Print.PolygroupIds.Add(Mesh.GetTriangleGroup(TriangleId));
						Print.MaterialIds.Add(MaterialIds ? MaterialIds->GetValue(TriangleId) : 0);
					}
				});
			}
		}

		return Print;
	}
}

/**
 * EVERY ROLE HAS A ROW, INCLUDING THE ONES COVERING NOTHING.
 *
 * This is the panel's list source, so "the panel lists every role" is this assertion and not a
 * widget one. Counted from NumSurfaceRoles rather than from a literal: the count has been 16, 17
 * and now 18, and a panel that lists a hard-coded sixteen silently drops whichever roles were
 * added last - LightSource and Mirror, the two most recently argued for.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSurfaceUsageCoversEveryRoleTest,
	"HouseForge.Editor.Surfaces.UsageCoversEveryRole", HF_TEST_FLAGS)

bool FHFSurfaceUsageCoversEveryRoleTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	const TArray<FHFSurfaceUsage> Usage = Editor->GetSurfaceUsage();

	TestEqual(TEXT("There is one usage row per surface role"),
		Usage.Num(), FHFMeshOps::NumSurfaceRoles());

	for (int32 Index = 0; Index < Usage.Num(); ++Index)
	{
		TestEqual(FString::Printf(TEXT("Row %d is the role at that index (%s)"),
			Index, *RoleName(static_cast<EHFSurfaceRole>(Index))),
			static_cast<int32>(Usage[Index].Role), Index);
	}

	return true;
}

/**
 * THE PANEL IS USABLE WITH NO HOUSE IN THE LEVEL.
 *
 * Finishes are assets, not level state, so an empty level must not disable anything - it must only
 * report that nothing is covered. The failure this guards against is a panel that looks broken in
 * exactly the situation somebody opens it in first.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSurfacesWithNoHouseTest,
	"HouseForge.Editor.Surfaces.NoHouseIsNotAFailure", HF_TEST_FLAGS)

bool FHFSurfacesWithNoHouseTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is an editor world"), World)
		|| !TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	ClearHouseForgeActors(World);

	const TArray<FHFSurfaceUsage> Usage = Editor->GetSurfaceUsage();
	TestEqual(TEXT("Every role still has a row with no house in the level"),
		Usage.Num(), FHFMeshOps::NumSurfaceRoles());

	int32 TotalElements = 0;
	for (const FHFSurfaceUsage& Row : Usage)
	{
		TotalElements += Row.ElementCount;
	}
	TestEqual(TEXT("No role covers anything with no house in the level"), TotalElements, 0);

	// Reading and writing a finish must still work: an empty level is the state the panel is most
	// likely to be opened in, and a library edit made there is what the next generated house picks up.
	FHFSurfaceFinish Finish;
	TestTrue(TEXT("A finish is readable with no house in the level"),
		Editor->GetSurfaceFinish(EHFSurfaceRole::WallPaint, Finish).bSuccess);

	int32 Components = INDEX_NONE;
	const FHFOperationResult Reapply = Editor->ReapplyMaterialsToLevel(Components);
	TestTrue(TEXT("Re-applying materials to an empty level succeeds rather than erroring"),
		Reapply.bSuccess);
	TestEqual(TEXT("Re-applying to an empty level touched no components"), Components, 0);
	TestTrue(TEXT("The message says finishes are still editable"),
		Reapply.Message.Contains(TEXT("still editable")));

	return true;
}

/**
 * THE USAGE FIGURES ARE MEASURED OFF THE BUILT FLAT, NOT OFF THE SPEC.
 *
 * The number that answers "will changing this do anything I can see". Asserted against an
 * independent walk of the same meshes, so a tally that quietly counted a component twice or dropped
 * the moving parts of every articulated fixture would show up as a mismatch rather than as a
 * plausible-looking figure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSurfaceUsageMeasuresTheFlatTest,
	"HouseForge.Editor.Surfaces.UsageMeasuresTheBuiltFlat", HF_TEST_FLAGS)

bool FHFSurfaceUsageMeasuresTheFlatTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is an editor world"), World)
		|| !TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat was built"), House))
	{
		return false;
	}

	const TArray<FHFSurfaceUsage> Usage = Editor->GetSurfaceUsage();
	const FLevelFingerprint Print = FingerprintLevel(World);

	int32 TotalTriangles = 0;
	double TotalArea = 0.0;
	for (const FHFSurfaceUsage& Row : Usage)
	{
		TotalTriangles += Row.TriangleCount;
		TotalArea += Row.AreaSquareMetres;
	}

	TestEqual(TEXT("Every triangle in the flat is counted against exactly one role"),
		TotalTriangles, Print.TriangleCount);
	TestTrue(TEXT("The flat has triangles to count"), TotalTriangles > 0);
	TestTrue(TEXT("The flat carries a positive surface area"), TotalArea > 0.0);

	// The two roles that cover most of any flat. If either reported nothing, the tally is reading
	// the wrong attribute and every figure in the panel would be zero while the flat looks correct.
	const FHFSurfaceUsage& Walls = Usage[static_cast<int32>(EHFSurfaceRole::WallPaint)];
	const FHFSurfaceUsage& Floors = Usage[static_cast<int32>(EHFSurfaceRole::FloorFinish)];

	TestTrue(TEXT("Wall paint covers at least one element"), Walls.ElementCount > 0);
	TestTrue(TEXT("Wall paint covers a positive area"), Walls.AreaSquareMetres > 0.0);
	TestTrue(TEXT("Floor finish covers at least one element"), Floors.ElementCount > 0);

	// A 2BHK flat's floor is tens of square metres, not thousands and not a fraction. A loose bound
	// rather than a figure, because the assertion worth making is that the cm2-to-m2 conversion is
	// the right way round - an inverted one lands four orders of magnitude out and this catches it,
	// where a tighter bound would only pin the sample spec in place.
	TestTrue(FString::Printf(TEXT("Floor area reads as square metres, not another unit (got %.2f)"),
		Floors.AreaSquareMetres), Floors.AreaSquareMetres > 10.0 && Floors.AreaSquareMetres < 2000.0);

	ClearHouseForgeActors(World);
	return true;
}

/**
 * SETTING A FINISH REACHES BOTH THE RECORD AND THE RENDERER.
 *
 * Two assertions rather than one, because the two halves fail separately and both failures look
 * like nothing happening: a library write that never pushed leaves the level unchanged, and a push
 * that never wrote the library is undone by the next thing that pushes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSetSurfaceFinishTest,
	"HouseForge.Editor.Surfaces.SettingAFinishUpdatesLibraryAndInstance", HF_TEST_FLAGS)

bool FHFSetSurfaceFinishTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	const FScopedLibraryRestore Restore;

	constexpr EHFSurfaceRole Role = EHFSurfaceRole::WallPaint;

	FHFSurfaceFinish Finish;
	if (!TestTrue(TEXT("The wall paint finish is readable"),
		Editor->GetSurfaceFinish(Role, Finish).bSuccess))
	{
		return false;
	}

	// Values nothing else in the table uses, so a read-back that happened to match cannot be a
	// coincidence, and a tiling figure that is not the default so the millimetre path is exercised.
	Finish.Roughness = 0.123f;
	Finish.BaseColor = FLinearColor(0.11f, 0.22f, 0.33f, 1.0f);
	Finish.TilingMM = 375.0f;

	const FHFOperationResult Set = Editor->SetSurfaceFinish(Role, Finish, EHFMaterialPush::Commit);
	TestTrue(FString::Printf(TEXT("Setting the wall paint finish succeeded: %s"), *Set.Message),
		Set.bSuccess);

	FHFSurfaceFinish ReadBack;
	TestTrue(TEXT("The finish reads back"), Editor->GetSurfaceFinish(Role, ReadBack).bSuccess);
	TestEqual(TEXT("The library kept the roughness"), ReadBack.Roughness, 0.123f);
	TestEqual(TEXT("The library kept the tile size"), ReadBack.TilingMM, 375.0f);

	// The renderer's half. Asked through the resolved value rather than through the override list,
	// so this measures what the surface will actually shade with.
	UMaterialInterface* Material = UHFMaterialLibrary::Get()->ResolveMaterial(Role);
	if (!TestNotNull(TEXT("The wall paint role resolves to a material"), Material))
	{
		return false;
	}

	float InstanceRoughness = -1.0f;
	TestTrue(TEXT("The material instance carries a Roughness parameter"),
		Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("Roughness")), InstanceRoughness));
	TestEqual(TEXT("The material instance took the new roughness"), InstanceRoughness, 0.123f);

	float InstanceTiling = -1.0f;
	TestTrue(TEXT("The material instance carries a TilingMM parameter"),
		Material->GetScalarParameterValue(FMaterialParameterInfo(TEXT("TilingMM")), InstanceTiling));
	TestEqual(TEXT("Tiling reached the renderer in millimetres, unconverted"), InstanceTiling, 375.0f);

	return true;
}

/**
 * RE-MATERIALLING MUST NOT REGENERATE GEOMETRY, AND MUST NOT RENUMBER POLYGROUPS.
 *
 * The load-bearing one. The material panel targets faces by surface-role polygroup, so a material
 * pass that renumbered them would silently destroy the thing it exists to serve - the flat would go
 * on rendering, in the wrong finishes, with no way left to correct it. Polygroups are therefore
 * compared as a SET rather than left to the position hash, which would not notice.
 *
 * Both push tiers are exercised, and a hand-edited element is standing in the level throughout: the
 * claim is that re-materialling cannot destroy a hand edit, and the only way to measure it is to
 * have one to destroy.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSetFinishLeavesGeometryAloneTest,
	"HouseForge.Editor.Surfaces.SettingAFinishDoesNotTouchGeometry", HF_TEST_FLAGS)

bool FHFSetFinishLeavesGeometryAloneTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is an editor world"), World)
		|| !TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	const FScopedLibraryRestore Restore;

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat was built"), House))
	{
		return false;
	}

	// One element flagged as hand-edited, so the claim is measured against the case it is about.
	AHFElementActor* Edited = nullptr;
	for (TActorIterator<AHFElementActor> It(World); It; ++It)
	{
		Edited = *It;
		break;
	}
	if (!TestNotNull(TEXT("There is an element to hand-edit"), Edited))
	{
		return false;
	}
	Edited->bArtistEdited = true;

	const FLevelFingerprint Before = FingerprintLevel(World);
	TestTrue(TEXT("The flat has geometry to compare"), Before.TriangleCount > 0);
	TestTrue(TEXT("The flat carries surface-role polygroups"), Before.PolygroupIds.Num() > 1);

	FHFSurfaceFinish Finish;
	Editor->GetSurfaceFinish(EHFSurfaceRole::FloorFinish, Finish);

	// A drag, then the release. Every role, not just one, because the panel can reach all of them
	// and a regeneration triggered by any single role would be enough to lose the hand edit.
	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		const EHFSurfaceRole Role = static_cast<EHFSurfaceRole>(Index);

		FHFSurfaceFinish RoleFinish;
		Editor->GetSurfaceFinish(Role, RoleFinish);
		RoleFinish.Roughness = 0.4f;
		RoleFinish.TilingMM = 512.0f;

		Editor->SetSurfaceFinish(Role, RoleFinish, EHFMaterialPush::Interactive);
		Editor->SetSurfaceFinish(Role, RoleFinish, EHFMaterialPush::Commit);
	}

	int32 Components = 0;
	Editor->ReapplyMaterialsToLevel(Components);
	TestTrue(TEXT("Re-applying reached the level's components"), Components > 0);

	const FLevelFingerprint After = FingerprintLevel(World);

	TestEqual(TEXT("No triangle was added or removed"), After.TriangleCount, Before.TriangleCount);
	TestEqual(TEXT("No component was added or removed"), After.ComponentCount, Before.ComponentCount);
	TestEqual(TEXT("No vertex moved"), After.GeometryHash, Before.GeometryHash);
	TestEqual(TEXT("The set of surface-role polygroups is unchanged"),
		After.PolygroupIds.Num(), Before.PolygroupIds.Num());
	TestTrue(TEXT("Not one surface-role polygroup was renumbered"),
		After.PolygroupIds.Difference(Before.PolygroupIds).IsEmpty()
		&& Before.PolygroupIds.Difference(After.PolygroupIds).IsEmpty());
	TestTrue(TEXT("The material ids the renderer indexes slots with are unchanged"),
		After.MaterialIds.Difference(Before.MaterialIds).IsEmpty()
		&& Before.MaterialIds.Difference(After.MaterialIds).IsEmpty());
	TestTrue(TEXT("The whole level fingerprint is unchanged"), After == Before);

	TestTrue(TEXT("The hand-edited element is still flagged as hand-edited"), Edited->bArtistEdited);

	ClearHouseForgeActors(World);
	return true;
}

/**
 * A HAND-EDITED ELEMENT TAKES A FINISH CHANGE LIKE ANY OTHER.
 *
 * The reassuring half of the previous test, and worth its own name. bArtistEdited opts an element
 * out of REGENERATION; it does not opt it out of being re-materialled, and it must not, or the one
 * element somebody has spent time on is the one stuck with the old finish and no control to fix it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFHandEditedTakesFinishTest,
	"HouseForge.Editor.Surfaces.HandEditedElementsStillTakeAFinish", HF_TEST_FLAGS)

bool FHFHandEditedTakesFinishTest::RunTest(const FString& Parameters)
{
	UWorld* World = EditorWorld();
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("There is an editor world"), World)
		|| !TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	const FScopedLibraryRestore Restore;

	AHFHouseActor* House = BuildReferenceFlat(World);
	if (!TestNotNull(TEXT("The reference flat was built"), House))
	{
		return false;
	}

	AHFElementActor* Edited = nullptr;
	for (TActorIterator<AHFElementActor> It(World); It; ++It)
	{
		Edited = *It;
		break;
	}
	if (!TestNotNull(TEXT("There is an element to hand-edit"), Edited))
	{
		return false;
	}

	Edited->bArtistEdited = true;

	int32 Components = 0;
	TestTrue(TEXT("Re-applying materials succeeds with a hand-edited element in the level"),
		Editor->ReapplyMaterialsToLevel(Components).bSuccess);

	UDynamicMeshComponent* Mesh = Edited->GetMeshComponent();
	if (!TestNotNull(TEXT("The hand-edited element has a mesh component"), Mesh))
	{
		return false;
	}

	TestEqual(TEXT("The hand-edited element carries one material slot per surface role"),
		Mesh->GetNumMaterials(), FHFMeshOps::NumSurfaceRoles());

	// Slot index IS role index, uniformly, on every component. Checked on the hand-edited one
	// because that is the element a well-meaning skip would have left holding a stale slot table.
	const UHFMaterialLibrary* Library = Editor->GetMaterialLibrary();
	if (!TestNotNull(TEXT("There is a material library"), Library))
	{
		return false;
	}

	for (int32 Index = 0; Index < FHFMeshOps::NumSurfaceRoles(); ++Index)
	{
		const EHFSurfaceRole Role = static_cast<EHFSurfaceRole>(Index);
		TestEqual(FString::Printf(TEXT("Slot %d on the hand-edited element is the %s material"),
			Index, *RoleName(Role)),
			Mesh->GetMaterial(Index), Library->ResolveMaterial(Role));
	}

	// And it is still hand-edited afterwards. Nothing in the material path may clear the flag.
	TestTrue(TEXT("The element is still flagged as hand-edited"), Edited->bArtistEdited);

	ClearHouseForgeActors(World);
	return true;
}

/**
 * RESET PUTS THE SHIPPED FINISH BACK, AND SAYS SO.
 *
 * The escape hatch for anyone who has changed six numbers and wants the plugin's opinion again.
 * Compared against DefaultFinishForRole rather than against a remembered value, so it measures
 * "back to what ships" rather than "back to what it was a moment ago".
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFResetSurfaceFinishTest,
	"HouseForge.Editor.Surfaces.ResetPutsTheShippedFinishBack", HF_TEST_FLAGS)

bool FHFResetSurfaceFinishTest::RunTest(const FString& Parameters)
{
	UHFEditorSubsystem* Editor = Subsystem();
	if (!TestNotNull(TEXT("The HouseForge editor subsystem is available"), Editor))
	{
		return false;
	}

	const FScopedLibraryRestore Restore;

	constexpr EHFSurfaceRole Role = EHFSurfaceRole::CounterStone;
	const FHFSurfaceFinish& Shipped = UHFMaterialLibrary::DefaultFinishForRole(Role);

	FHFSurfaceFinish Wrecked;
	Editor->GetSurfaceFinish(Role, Wrecked);
	Wrecked.Roughness = 0.999f;
	Wrecked.TilingMM = 47.0f;
	Wrecked.Description = TEXT("wrecked by a test");
	Editor->SetSurfaceFinish(Role, Wrecked, EHFMaterialPush::Commit);

	FHFSurfaceFinish Confirm;
	Editor->GetSurfaceFinish(Role, Confirm);
	TestEqual(TEXT("The finish really was changed first"), Confirm.Roughness, 0.999f);

	TestTrue(TEXT("Reset succeeded"), Editor->ResetSurfaceFinish(Role).bSuccess);

	FHFSurfaceFinish AfterReset;
	Editor->GetSurfaceFinish(Role, AfterReset);
	TestEqual(TEXT("Roughness is back to the shipped figure"), AfterReset.Roughness, Shipped.Roughness);
	TestEqual(TEXT("Tiling is back to the shipped figure"), AfterReset.TilingMM, Shipped.TilingMM);
	TestEqual(TEXT("The description is back to the shipped one"),
		AfterReset.Description, Shipped.Description);
	TestTrue(TEXT("A reset role still says what it is"), !AfterReset.Description.IsEmpty());

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
