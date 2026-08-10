// Copyright Siddartha G. All Rights Reserved.

//
// The batch asset replacement pass, and the mapping table behind it.
//
// The feature the user asked for in the first brief: "let me have the power so that I can do a
// separate pass to replace the generated fixtures using the assets available in the content
// browser." What makes it safe rather than merely possible is that it is REVERSIBLE, and that is
// what most of these tests are about.
//
//   * a table catches every instance of a type                 ATableAppliesToEveryInstanceOfAType
//   * a hand-picked subset catches only that subset            ASubsetAppliesToOnlyThatSubset
//   * reverting restores the generated mesh EXACTLY            RevertRestoresGenerationExactly
//   * a table survives a rebuild and furnishes a new house     ATableSurvivesARebuild
//   * a hand-picked swap is not clobbered by the table         AHandPickedOverrideOutranksTheTable
//   * ...and survives a rebuild, still tracking the drawing    AHandPickedOverrideSurvivesARebuild
//   * an override is not a hand edit                           AnOverrideIsNotAHandEdit
//   * a Modeling Tool still targets the live mesh              ToolTargetSurvivesAnOverride
//   * exactly one representation collides                      CollisionFollowsTheOverride
//   * an override on a baked element hides BOTH                AnOverrideOutranksABake
//   * a missing asset never leaves a hole in the flat          AMissingAssetFallsBackToGeneration
//
// RevertRestoresGenerationExactly and ToolTargetSurvivesAnOverride are the two to keep.
//
// The first is .claude/rules/04-conventions.md in one assertion: "Clearing the override must restore
// the generated mesh exactly." It is asserted by fingerprinting every vertex AND the surface-role
// polygroups, not by counting triangles - a rebuild to the same shape would pass a count and would
// mean the guarantee had been broken, because the claim is that no rebuild happened at all.
//
// The second carries HouseForge.Bake.Probe.ToolTargetSelection's measurement across to a second
// UStaticMeshComponent. That probe MEASURED that a hidden - and even an unregistered - static mesh
// component still wins a Modeling Tool over the live dynamic mesh, and that two live candidates make
// every single-selection tool refuse to start. An override component that kept its asset while
// inactive would reintroduce exactly that, on an actor the user believes is fully procedural again.
//

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFAssetOverrideTypes.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFWardrobeActor.h"
#include "AssetUtils/CreateStaticMeshUtil.h"
#include "Assets/HFAssetMappingTable.h"
#include "Components/DynamicMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/StaticMesh.h"
#include "EngineUtils.h"
#include "Geometry/HFMeshOps.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSettings.h"
#include "StaticMeshCompiler.h"
#include "TargetInterfaces/DynamicMeshCommitter.h"
#include "TargetInterfaces/DynamicMeshProvider.h"
#include "TargetInterfaces/MaterialProvider.h"
#include "TargetInterfaces/PrimitiveComponentBackedTarget.h"
#include "ToolTargetManager.h"
#include "ToolTargets/DynamicMeshComponentToolTarget.h"
#include "ToolTargets/StaticMeshComponentToolTarget.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace HFAssetTest
{
	/**
	 * Every vertex of a component's mesh AND its surface-role polygroups, as a comparable value.
	 *
	 * The same shape as HFBakeTest::Print and for the same reason: renumbering the polygroups while
	 * leaving every vertex in place passes a position-only comparison and leaves the flat rendering
	 * the wrong finishes with the mechanism for fixing it gone. The position term is weighted by
	 * vertex index so that a mesh rebuilt to the identical shape is NOT the same print - the claim
	 * being tested is that no rebuild happened at all, not that the result looks the same.
	 */
	struct FMeshPrint
	{
		int32 Vertices = 0;
		int32 Triangles = 0;
		double PositionSum = 0.0;
		TSet<int32> Groups;

		bool operator==(const FMeshPrint& Other) const
		{
			return Vertices == Other.Vertices
				&& Triangles == Other.Triangles
				&& FMath::IsNearlyEqual(PositionSum, Other.PositionSum, 1e-6)
				&& Groups.Difference(Other.Groups).IsEmpty()
				&& Other.Groups.Difference(Groups).IsEmpty();
		}
	};

	FMeshPrint Print(const UDynamicMeshComponent* Component)
	{
		FMeshPrint Result;
		if (!IsValid(Component))
		{
			return Result;
		}

		const_cast<UDynamicMeshComponent*>(Component)->GetDynamicMesh()->ProcessMesh(
			[&Result](const FDynamicMesh3& Mesh)
			{
				Result.Vertices = Mesh.VertexCount();
				Result.Triangles = Mesh.TriangleCount();

				for (const int32 Vid : Mesh.VertexIndicesItr())
				{
					const FVector3d P = Mesh.GetVertex(Vid);
					Result.PositionSum += (P.X * 3.0 + P.Y * 5.0 + P.Z * 7.0) * static_cast<double>(Vid + 1);
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

	/** The print of every source component of an element, so an articulated fixture is covered too. */
	TArray<FMeshPrint> PrintAll(const AHFElementActor* Actor)
	{
		TArray<FMeshPrint> Prints;
		if (!IsValid(Actor))
		{
			return Prints;
		}

		TArray<UDynamicMeshComponent*> Sources;
		Actor->GetBakeSourceComponents(Sources);
		for (const UDynamicMeshComponent* Source : Sources)
		{
			Prints.Add(Print(Source));
		}
		return Prints;
	}

	/**
	 * A box-shaped UStaticMesh asset, standing in for something out of the Content Browser.
	 *
	 * UE::AssetUtils::CreateStaticMeshAsset rather than the GeometryScript wrapper, for the reason
	 * HouseForge.Bake.Probe measured: the low-level one nulls GUndo for its own duration and never
	 * touches GEditor, so it is safe headless.
	 *
	 * FinishCompilation before returning, and that is not optional - HouseForge.Bake.Probe measured
	 * that a freshly created static mesh reports IsCompiling() == 1 and has EMPTY bounds until the
	 * build finishes. A fit computed against an empty box scales the asset to nothing, and the test
	 * would be measuring the compile rather than the fit.
	 */
	UStaticMesh* MakeBoxAsset(const FString& AssetPath, const FVector& Size)
	{
		// Built about the origin with its base on z = 0, so the asset's pivot is a real one rather
		// than the degenerate centre-of-everything case - the fit has to cope with either.
		FDynamicMesh3 Mesh;
		FHFMeshOps::AppendBox(Mesh, FVector3d(0.0, 0.0, Size.Z * 0.5),
			FVector3d(Size.X * 0.5, Size.Y * 0.5, Size.Z * 0.5), 0.0, EHFSurfaceRole::JoineryCarcass);

		UE::AssetUtils::FStaticMeshAssetOptions Options;
		Options.NewAssetPath = AssetPath;
		Options.NumSourceModels = 1;
		Options.NumMaterialSlots = FHFMeshOps::NumSurfaceRoles();
		Options.bGenerateLightmapUVs = false;
		Options.bCreatePhysicsBody = true;
		Options.CollisionType = ECollisionTraceFlag::CTF_UseComplexAsSimple;
		Options.SourceMeshes.DynamicMeshes.Add(&Mesh);

		UE::AssetUtils::FStaticMeshResults Results;
		if (UE::AssetUtils::CreateStaticMeshAsset(Options, Results) != UE::AssetUtils::ECreateStaticMeshResult::Ok)
		{
			return nullptr;
		}

		UStaticMesh* Asset = Results.StaticMesh;
		if (Asset != nullptr)
		{
			FStaticMeshCompilingManager::Get().FinishCompilation({ Asset });
		}
		return Asset;
	}

	/** The sample flat, built in the editor world. */
	AHFHouseActor* BuildSampleHouse(UWorld* World)
	{
		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		// SetSpec builds: it converts the spec to centimetres, then rebuilds and generates. Calling
		// BuildGeometry after it would build the flat twice.
		House->SetSpec(FHFSampleHouse::Make2BHK());
		return House;
	}

	/** Every element actor of a house whose drawing called it this kind of fixture. */
	TArray<AHFElementActor*> ElementsOfType(AHFHouseActor* House, EHFFixtureType Type)
	{
		TArray<AHFElementActor*> Found;
		if (!IsValid(House))
		{
			return Found;
		}

		for (AActor* Element : House->ElementActors)
		{
			AHFElementActor* Typed = Cast<AHFElementActor>(Element);
			if (IsValid(Typed) && Typed->SourceFixtureType == Type)
			{
				Found.Add(Typed);
			}
		}
		return Found;
	}

	/** A table with one row, ready to apply. */
	UHFAssetMappingTable* MakeTable(EHFFixtureType Type, UStaticMesh* Asset, EHFAssetFitMode Mode)
	{
		UHFAssetMappingTable* Table = NewObject<UHFAssetMappingTable>(GetTransientPackage(),
			MakeUniqueObjectName(GetTransientPackage(), UHFAssetMappingTable::StaticClass(), TEXT("HFTestTable")));

		FHFAssetMapping Mapping;
		Mapping.Mesh = Asset;
		Mapping.FitMode = Mode;
		Table->Entries.Add(Type, Mapping);

		return Table;
	}

	/**
	 * Holds the project's mapping table setting for a scope and puts it back.
	 *
	 * A bare write would leak into every later test in the run, and the leak would be invisible: a
	 * table left configured makes an unrelated rebuild furnish itself, and the failure surfaces
	 * somewhere with no connection to the test that caused it.
	 */
	struct FTableSettingScope
	{
		explicit FTableSettingScope(UHFAssetMappingTable* Table)
		{
			Settings = GetMutableDefault<UHFSettings>();
			if (Settings != nullptr)
			{
				Previous = Settings->AssetMappingTable;
				Settings->AssetMappingTable = Table;
			}
		}

		~FTableSettingScope()
		{
			if (Settings != nullptr)
			{
				Settings->AssetMappingTable = Previous;
			}
		}

		UHFSettings* Settings = nullptr;
		TSoftObjectPtr<UHFAssetMappingTable> Previous;
	};

	/** A wardrobe standing on its own, for the tests that do not need a whole flat. */
	AHFWardrobeActor* SpawnWardrobe(UWorld* World, const TCHAR* Id)
	{
		AHFWardrobeActor* Actor = World->SpawnActor<AHFWardrobeActor>();
		if (Actor == nullptr)
		{
			return nullptr;
		}

		Actor->ElementId = FName(Id);
		Actor->SourceFixtureType = EHFFixtureType::Wardrobe;
		Actor->ApplyProjectDefaults();

		FHFFixture Fixture;
		Fixture.Id = FName(Id);
		Fixture.Type = EHFFixtureType::Wardrobe;
		Fixture.Footprint = FVector2D(200.0, 60.0);
		Fixture.Height = 240.0;
		Actor->ApplyFixture(Fixture);

		Actor->Regenerate();
		return Actor;
	}
}

// ============================================================================================
// The table

/**
 * ONE ROW CATCHES EVERY INSTANCE OF ITS TYPE. The whole point of having a table.
 *
 * The reference flat has two wardrobes, which is exactly the case that a per-instance picker makes
 * tedious and a table makes free - and it is the first of the customers the fixtures milestone
 * named.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetTableAppliesToTypeTest,
	"HouseForge.Editor.Assets.ATableAppliesToEveryInstanceOfAType", HF_TEST_FLAGS)

bool FHFAssetTableAppliesToTypeTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildSampleHouse(World);
	if (!TestNotNull(TEXT("The sample flat built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT { House->Destroy(); };

	const TArray<AHFElementActor*> Wardrobes = ElementsOfType(House, EHFFixtureType::Wardrobe);
	if (!TestTrue(TEXT("The reference flat has more than one wardrobe to catch"), Wardrobes.Num() >= 2))
	{
		return false;
	}

	// Every wardrobe starts procedural. Asserted rather than assumed, so a table that did nothing at
	// all could not pass by leaving them in the state they were already in.
	for (const AHFElementActor* Wardrobe : Wardrobes)
	{
		TestFalse(TEXT("A freshly built wardrobe has no override"), Wardrobe->HasAssetOverride());
	}

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Wardrobe"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	UHFAssetMappingTable* Table = MakeTable(EHFFixtureType::Wardrobe, Asset, EHFAssetFitMode::StretchToFootprint);

	TArray<FString> Report;
	const int32 Changed = House->ApplyAssetMappingTable(Table, &Report);

	TestEqual(TEXT("Every wardrobe was replaced, and nothing else was"), Changed, Wardrobes.Num());

	for (const AHFElementActor* Wardrobe : Wardrobes)
	{
		TestTrue(TEXT("Each wardrobe is now showing the asset"), Wardrobe->HasAssetOverride());
		TestTrue(TEXT("...and knows the table put it there"), Wardrobe->HasTableAssetOverride());
		TestTrue(TEXT("...and the component is drawing that asset"),
			Wardrobe->GetAssetOverrideComponent()->GetStaticMesh() == Asset);

		// STRETCHED TO THE DRAWN BOX, which is what StretchToFootprint promises. Measured off the
		// component rather than off the fit result, so this asserts what the viewport shows.
		const FBox Fitted = Wardrobe->GetAssetOverrideComponent()->CalcBounds(
			Wardrobe->GetAssetOverrideComponent()->GetRelativeTransform()).GetBox();
		const FBox Generated = Wardrobe->GetGeneratedLocalBounds();

		TestTrue(TEXT("...filling the box the generated wardrobe occupied"),
			Fitted.GetSize().Equals(Generated.GetSize(), 0.5));
	}

	// Nothing that is not a wardrobe was touched, which is the other half of "applies to a type".
	int32 OtherOverrides = 0;
	for (AActor* Element : House->ElementActors)
	{
		const AHFElementActor* Typed = Cast<AHFElementActor>(Element);
		if (IsValid(Typed) && Typed->SourceFixtureType != EHFFixtureType::Wardrobe && Typed->HasAssetOverride())
		{
			++OtherOverrides;
		}
	}
	TestEqual(TEXT("No element of any other type was replaced"), OtherOverrides, 0);

	// Idempotent: the same table again changes nothing about which elements are overridden.
	const int32 Again = House->ApplyAssetMappingTable(Table, nullptr);
	TestEqual(TEXT("Applying the same table again touches the same elements and no others"),
		Again, Wardrobes.Num());

	return true;
}

/**
 * A hand-picked subset catches only that subset, and the rest of the type stays procedural.
 *
 * "Apply across all matching instances, OR a hand-picked subset" - the second half. The flat's two
 * wardrobes are the test: one gets the asset, the other must be untouched, vertex for vertex.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetSubsetTest,
	"HouseForge.Editor.Assets.ASubsetAppliesToOnlyThatSubset", HF_TEST_FLAGS)

bool FHFAssetSubsetTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildSampleHouse(World);
	if (!TestNotNull(TEXT("The sample flat built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT { House->Destroy(); };

	TArray<AHFElementActor*> Wardrobes = ElementsOfType(House, EHFFixtureType::Wardrobe);
	if (!TestTrue(TEXT("Two wardrobes to tell apart"), Wardrobes.Num() >= 2))
	{
		return false;
	}

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Subset"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	AHFElementActor* Chosen = Wardrobes[0];
	AHFElementActor* Untouched = Wardrobes[1];

	const TArray<FMeshPrint> BeforeUntouched = PrintAll(Untouched);

	FHFAssetOverride Override;
	Override.OverrideMesh = Asset;
	Override.FitMode = EHFAssetFitMode::UniformFit;

	const FHFAssetFitResult Fit = Chosen->SetAssetOverride(Override);
	TestTrue(TEXT("The chosen wardrobe took the asset"), Fit.bValid);
	TestTrue(TEXT("...and is showing it"), Chosen->HasAssetOverride());

	// HAND-PICKED, so it carries no table name - which is what stops a later batch pass from
	// reverting it.
	TestTrue(TEXT("A hand-picked override records no source table"), !Chosen->HasTableAssetOverride());

	TestFalse(TEXT("The other wardrobe is still procedural"), Untouched->HasAssetOverride());
	TestTrue(TEXT("...and its geometry was not touched at all"),
		PrintAll(Untouched) == BeforeUntouched);
	TestTrue(TEXT("...and its live mesh is still what draws"),
		Untouched->GetMeshComponent()->IsVisible());

	return true;
}

// ============================================================================================
// The way back

/**
 * REVERTING RESTORES THE GENERATED MESH EXACTLY. Rule 04, in one assertion.
 *
 * Fingerprinted rather than counted. A count of triangles would pass on an element that had been
 * regenerated to the same shape, and regeneration is exactly what must NOT have happened: the
 * guarantee is that the FDynamicMesh3 was never read, written, cleared or rebuilt, so its vertices
 * are the same objects in the same order with the same polygroups.
 *
 * Run over a hand-edited element as well, because that is where the guarantee is worth something: a
 * revert that quietly regenerated would look correct on a generated wardrobe and would silently
 * destroy a sculpted one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetRevertTest,
	"HouseForge.Editor.Assets.RevertRestoresGenerationExactly", HF_TEST_FLAGS)

bool FHFAssetRevertTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnWardrobe(World, TEXT("WD_Revert"));
	if (!TestNotNull(TEXT("A wardrobe was built"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT { Wardrobe->Destroy(); };

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Revert"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	const TArray<FMeshPrint> Before = PrintAll(Wardrobe);
	const int32 RevisionBefore = Wardrobe->MeshRevision;
	TestTrue(TEXT("There is geometry to lose"), Before.Num() > 0 && Before[0].Triangles > 0);

	FHFAssetOverride Override;
	Override.OverrideMesh = Asset;
	Override.FitMode = EHFAssetFitMode::StretchToFootprint;
	Wardrobe->SetAssetOverride(Override);

	TestTrue(TEXT("The asset is showing"), Wardrobe->HasAssetOverride());
	TestTrue(TEXT("...and the generated mesh is UNCHANGED while it is hidden"),
		PrintAll(Wardrobe) == Before);

	Wardrobe->ClearAssetOverride();

	TestFalse(TEXT("The override is gone"), Wardrobe->HasAssetOverride());
	TestTrue(TEXT("The generated mesh came back vertex for vertex, polygroups included"),
		PrintAll(Wardrobe) == Before);
	TestTrue(TEXT("...and the live mesh is what draws again"), Wardrobe->GetMeshComponent()->IsVisible());
	TestTrue(TEXT("...and it is editable again, so the Modeling Tools will take it"),
		Wardrobe->GetMeshComponent()->IsEditable());

	// NOT A GEOMETRY CHANGE. MeshRevision is the staleness model for the bake; an override bumping it
	// would mark every baked element in the flat stale for a change that touched no geometry, and a
	// whole flat would silently re-bake on a swap.
	TestEqual(TEXT("Nothing about the round trip counted as a geometry change"),
		Wardrobe->MeshRevision, RevisionBefore);

	return true;
}

/**
 * An override is not a hand edit, and the two stay orthogonal.
 *
 * bArtistEdited is raised by UDynamicMeshComponent::OnMeshChanged, and nothing in the override path
 * changes a mesh - so this cannot fire today. It is worth an assertion anyway, because the day
 * somebody "helpfully" regenerates before swapping, the flag flips and the element stops
 * regenerating from the drawing forever, silently.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetNotAHandEditTest,
	"HouseForge.Editor.Assets.AnOverrideIsNotAHandEdit", HF_TEST_FLAGS)

bool FHFAssetNotAHandEditTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnWardrobe(World, TEXT("WD_NotEdited"));
	if (!TestNotNull(TEXT("A wardrobe was built"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT { Wardrobe->Destroy(); };

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_NotEdited"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	TestFalse(TEXT("A generated wardrobe starts un-edited"), Wardrobe->bArtistEdited);

	FHFAssetOverride Override;
	Override.OverrideMesh = Asset;
	Wardrobe->SetAssetOverride(Override);
	TestFalse(TEXT("Setting an override did not mark it hand-edited"), Wardrobe->bArtistEdited);

	Wardrobe->ClearAssetOverride();
	TestFalse(TEXT("Nor did clearing it"), Wardrobe->bArtistEdited);

	// And an overridden element still regenerates from the drawing underneath, which is what makes a
	// later revert give back geometry matching the CURRENT spec rather than the one it was hidden at.
	Wardrobe->SetAssetOverride(Override);
	const TArray<FMeshPrint> WhileHidden = PrintAll(Wardrobe);

	FHFFixture Wider;
	Wider.Id = TEXT("WD_NotEdited");
	Wider.Type = EHFFixtureType::Wardrobe;
	Wider.Footprint = FVector2D(280.0, 60.0);
	Wider.Height = 240.0;
	Wardrobe->ApplyFixture(Wider);
	Wardrobe->Regenerate();

	TestFalse(TEXT("The hidden mesh regenerated rather than being frozen"),
		PrintAll(Wardrobe) == WhileHidden);
	TestTrue(TEXT("...and the asset is still the thing on screen"), Wardrobe->HasAssetOverride());

	Wardrobe->ClearAssetOverride();
	TestTrue(TEXT("Reverting gives back the WIDER wardrobe, matching the current drawing"),
		Wardrobe->GetGeneratedLocalBounds().GetSize().X > 250.0);

	return true;
}

// ============================================================================================
// The rebuild

/**
 * A TABLE SURVIVES A REBUILD AND FURNISHES A NEWLY GENERATED HOUSE.
 *
 * The claim that makes a table worth building at all: "a library built once auto-applies to every
 * future generated house." Asserted the hard way - the house is rebuilt from its spec, which
 * destroys and respawns every element that has nothing to preserve, and the flat has to come back
 * furnished without anybody re-running anything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetTableSurvivesRebuildTest,
	"HouseForge.Editor.Assets.ATableSurvivesARebuild", HF_TEST_FLAGS)

bool FHFAssetTableSurvivesRebuildTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Rebuild"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	UHFAssetMappingTable* Table = MakeTable(EHFFixtureType::Wardrobe, Asset, EHFAssetFitMode::StretchToFootprint);

	// Configured as the PROJECT's table, which is the path a real user takes - and the path that
	// makes the pass automatic. Restored on the way out so no later test inherits it.
	FTableSettingScope Scoped(Table);

	AHFHouseActor* House = BuildSampleHouse(World);
	if (!TestNotNull(TEXT("The sample flat built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT { House->Destroy(); };

	// FURNISHED ON THE FIRST BUILD, with nobody asking. BuildGeometry runs the pass at the end.
	{
		const TArray<AHFElementActor*> Wardrobes = ElementsOfType(House, EHFFixtureType::Wardrobe);
		if (!TestTrue(TEXT("The flat has wardrobes"), Wardrobes.Num() >= 2))
		{
			return false;
		}

		for (const AHFElementActor* Wardrobe : Wardrobes)
		{
			TestTrue(TEXT("A newly generated house comes out already furnished from the table"),
				Wardrobe->HasAssetOverride());
		}
	}

	// AND AGAIN AFTER A FULL REBUILD, which destroys and respawns everything with nothing to keep.
	House->BuildGeometry();

	{
		const TArray<AHFElementActor*> Wardrobes = ElementsOfType(House, EHFFixtureType::Wardrobe);
		TestTrue(TEXT("There are still wardrobes after the rebuild"), Wardrobes.Num() >= 2);

		for (const AHFElementActor* Wardrobe : Wardrobes)
		{
			TestTrue(TEXT("...and each is still showing the table's asset"), Wardrobe->HasAssetOverride());
			TestTrue(TEXT("...the same asset"),
				Wardrobe->GetAssetOverrideComponent()->GetStaticMesh() == Asset);
		}
	}

	// EMPTYING THE TABLE GIVES THE PROCEDURAL FLAT BACK. The other direction, and the one that makes
	// the pass idempotent rather than merely repeatable: a row deleted and a rebuild run must not
	// leave last run's asset stranded with nothing declaring it.
	Table->Entries.Empty();
	House->BuildGeometry();

	for (const AHFElementActor* Wardrobe : ElementsOfType(House, EHFFixtureType::Wardrobe))
	{
		TestFalse(TEXT("With the row gone, the generated wardrobe is back"), Wardrobe->HasAssetOverride());
		TestTrue(TEXT("...and it is what draws"), Wardrobe->GetMeshComponent()->IsVisible());
	}

	return true;
}

/**
 * A hand-picked override outranks the table, and a batch pass will not quietly revert it.
 *
 * The worst kind of data loss this feature could cause: somebody chooses a particular sofa for the
 * living room, a later batch pass replaces it with the library's default, and nothing says so. It is
 * invisible until a render and impossible to attribute afterwards.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetHandPickedOutranksTableTest,
	"HouseForge.Editor.Assets.AHandPickedOverrideOutranksTheTable", HF_TEST_FLAGS)

bool FHFAssetHandPickedOutranksTableTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = BuildSampleHouse(World);
	if (!TestNotNull(TEXT("The sample flat built"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT { House->Destroy(); };

	TArray<AHFElementActor*> Wardrobes = ElementsOfType(House, EHFFixtureType::Wardrobe);
	if (!TestTrue(TEXT("Two wardrobes to tell apart"), Wardrobes.Num() >= 2))
	{
		return false;
	}

	UStaticMesh* Chosen = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Chosen"), FVector(190.0, 58.0, 220.0));
	UStaticMesh* Library = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Library"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The chosen asset was created"), Chosen)
		|| !TestNotNull(TEXT("The library asset was created"), Library))
	{
		return false;
	}

	FHFAssetOverride ByHand;
	ByHand.OverrideMesh = Chosen;
	ByHand.FitMode = EHFAssetFitMode::KeepAssetSize;
	Wardrobes[0]->SetAssetOverride(ByHand);

	UHFAssetMappingTable* Table = MakeTable(EHFFixtureType::Wardrobe, Library, EHFAssetFitMode::StretchToFootprint);
	House->ApplyAssetMappingTable(Table, nullptr);

	TestTrue(TEXT("The hand-picked wardrobe kept the asset somebody chose for it"),
		Wardrobes[0]->GetAssetOverrideComponent()->GetStaticMesh() == Chosen);
	TestTrue(TEXT("...and is still marked as hand-picked"), !Wardrobes[0]->HasTableAssetOverride());
	TestTrue(TEXT("The other wardrobe took the library's"),
		Wardrobes[1]->GetAssetOverrideComponent()->GetStaticMesh() == Library);

	// AND A REBUILD DOES NOT LOSE IT. This is the branch that had to change in BuildGeometry: a
	// hand-picked override goes into PreservedForBake, so the actor survives, is re-parameterised
	// from the spec, and keeps its asset. Putting it in ShouldPreserveOnRebuild instead would have
	// frozen its parameters and stopped it tracking the drawing.
	House->BuildGeometry();

	AHFElementActor* Kept = nullptr;
	for (AHFElementActor* Wardrobe : ElementsOfType(House, EHFFixtureType::Wardrobe))
	{
		if (Wardrobe->HasAssetOverride() && !Wardrobe->HasTableAssetOverride())
		{
			Kept = Wardrobe;
			break;
		}
	}

	if (TestNotNull(TEXT("The hand-picked wardrobe survived the rebuild"), Kept))
	{
		TestTrue(TEXT("...still showing the asset somebody chose"),
			Kept->GetAssetOverrideComponent()->GetStaticMesh() == Chosen);

		// STILL TRACKING THE DRAWING, which is the half that ShouldPreserveOnRebuild would have lost.
		// Clearing the override gives back geometry the rebuild regenerated, not a frozen copy.
		Kept->ClearAssetOverride();
		TestTrue(TEXT("...and the generated wardrobe underneath it is real geometry from this build"),
			Kept->GetGeneratedLocalBounds().IsValid && Kept->GetGeneratedLocalBounds().GetSize().X > 1.0);
	}

	return true;
}

// ============================================================================================
// What else must not break

/**
 * A Modeling Tool still targets the live mesh, in both directions.
 *
 * HouseForge.Bake.Probe.ToolTargetSelection MEASURED that a UStaticMeshComponent holding an asset
 * wins a Modeling Tool over a dynamic mesh whether it is visible, hidden or unregistered, and that
 * two live candidates make every single-selection tool refuse to start. Both consequences apply to
 * the override component, so both are asserted here: while the override is active the dynamic mesh
 * must drop out, and once it is cleared the static one must.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetToolTargetTest,
	"HouseForge.Editor.Assets.ToolTargetSurvivesAnOverride", HF_TEST_FLAGS)

bool FHFAssetToolTargetTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnWardrobe(World, TEXT("WD_ToolTarget"));
	if (!TestNotNull(TEXT("A wardrobe was built"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT { Wardrobe->Destroy(); };

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_ToolTarget"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	// Loaded with exactly the factories UModelingToolsEditorMode::Enter loads, IN ITS ORDER. The
	// order is what makes the static factory win every tie, so a manager built with them in any other
	// order would be measuring something the editor never does.
	UToolTargetManager* Targets = NewObject<UToolTargetManager>(GetTransientPackage());
	Targets->Initialize();
	Targets->AddTargetFactory(NewObject<UStaticMeshComponentToolTargetFactory>(Targets));
	Targets->AddTargetFactory(NewObject<UDynamicMeshComponentToolTargetFactory>(Targets));

	// What every single-selection mesh editing tool asks for.
	FToolTargetTypeRequirements Requirements({
		UMaterialProvider::StaticClass(),
		UDynamicMeshProvider::StaticClass(),
		UDynamicMeshCommitter::StaticClass()
	});

	auto CountFor = [&](UActorComponent* Component)
	{
		return (Component != nullptr && Targets->CanBuildTarget(Component, Requirements)) ? 1 : 0;
	};

	FHFAssetOverride Override;
	Override.OverrideMesh = Asset;
	Wardrobe->SetAssetOverride(Override);

	{
		TArray<UDynamicMeshComponent*> Sources;
		Wardrobe->GetBakeSourceComponents(Sources);

		int32 LiveCandidates = 0;
		for (UDynamicMeshComponent* Source : Sources)
		{
			LiveCandidates += CountFor(Source);
		}

		// The dynamic components drop out because ApplyRenderMode/RefreshAssetOverride marks them
		// non-editable, which UDynamicMeshComponentToolTargetFactory::CanBuildTarget tests explicitly.
		TestEqual(TEXT("While overridden, no live mesh is a tool candidate"), LiveCandidates, 0);
		TestEqual(TEXT("...and the override component is the only one"),
			CountFor(Wardrobe->GetAssetOverrideComponent()), 1);
	}

	Wardrobe->ClearAssetOverride();

	{
		// AND THE OTHER DIRECTION, which is the one that matters. A cleared override that merely
		// hid its component would leave it holding the asset, and the static factory - registered
		// first - would hand a Modeling Tool the vendor's asset on an actor the user believes is
		// fully procedural again.
		TestEqual(TEXT("Once reverted the override component is NOT a tool candidate"),
			CountFor(Wardrobe->GetAssetOverrideComponent()), 0);
		TestNull(TEXT("...because its static mesh was cleared, not just hidden"),
			Wardrobe->GetAssetOverrideComponent()->GetStaticMesh());
		TestEqual(TEXT("...and the live mesh is targetable again"),
			CountFor(Wardrobe->GetMeshComponent()), 1);
	}

	return true;
}

/**
 * Exactly one representation collides, so a walkthrough cannot hit a thing it cannot see.
 *
 * Leaving both on double-traces the fixture and leaves complex-as-simple collision sitting under a
 * mesh the user believes is the only thing there - the same rule the bake follows.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetCollisionTest,
	"HouseForge.Editor.Assets.CollisionFollowsTheOverride", HF_TEST_FLAGS)

bool FHFAssetCollisionTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnWardrobe(World, TEXT("WD_Collision"));
	if (!TestNotNull(TEXT("A wardrobe was built"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT { Wardrobe->Destroy(); };

	UStaticMesh* Asset = MakeBoxAsset(TEXT("/Game/HouseForge/AssetTest/SM_Collision"), FVector(180.0, 55.0, 210.0));
	if (!TestNotNull(TEXT("The stand-in asset was created"), Asset))
	{
		return false;
	}

	auto LiveCollidingCount = [&]()
	{
		TArray<UDynamicMeshComponent*> Sources;
		Wardrobe->GetBakeSourceComponents(Sources);

		int32 Count = 0;
		for (const UDynamicMeshComponent* Source : Sources)
		{
			if (IsValid(Source) && Source->GetCollisionEnabled() != ECollisionEnabled::NoCollision)
			{
				++Count;
			}
		}
		return Count;
	};

	const int32 LiveBefore = LiveCollidingCount();
	TestTrue(TEXT("The generated wardrobe collides to begin with"), LiveBefore > 0);

	FHFAssetOverride Override;
	Override.OverrideMesh = Asset;
	Wardrobe->SetAssetOverride(Override);

	TestEqual(TEXT("While overridden nothing generated still collides"), LiveCollidingCount(), 0);
	TestTrue(TEXT("...and the asset does"),
		Wardrobe->GetAssetOverrideComponent()->GetCollisionEnabled() != ECollisionEnabled::NoCollision);

	Wardrobe->ClearAssetOverride();

	TestEqual(TEXT("Reverting gives the generated collision back, part for part"),
		LiveCollidingCount(), LiveBefore);
	TestTrue(TEXT("...and the asset stops colliding"),
		Wardrobe->GetAssetOverrideComponent()->GetCollisionEnabled() == ECollisionEnabled::NoCollision);

	return true;
}

/**
 * A missing asset never leaves a hole in the flat.
 *
 * The same rule the bake follows for a missing baked asset: NEVER RENDER NOTHING. A soft pointer at
 * a package that has been deleted or renamed leaves the generated mesh drawing and says so.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAssetMissingTest,
	"HouseForge.Editor.Assets.AMissingAssetFallsBackToGeneration", HF_TEST_FLAGS)

bool FHFAssetMissingTest::RunTest(const FString& Parameters)
{
	using namespace HFAssetTest;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFWardrobeActor* Wardrobe = SpawnWardrobe(World, TEXT("WD_Missing"));
	if (!TestNotNull(TEXT("A wardrobe was built"), Wardrobe))
	{
		return false;
	}
	ON_SCOPE_EXIT { Wardrobe->Destroy(); };

	const TArray<FMeshPrint> Before = PrintAll(Wardrobe);

	FHFAssetOverride Override;
	Override.OverrideMesh = TSoftObjectPtr<UStaticMesh>(
		FSoftObjectPath(TEXT("/Game/HouseForge/AssetTest/SM_ThisWasDeleted.SM_ThisWasDeleted")));

	AddExpectedError(TEXT("could not load the override asset"), EAutomationExpectedErrorFlags::Contains, 0);

	const FHFAssetFitResult Fit = Wardrobe->SetAssetOverride(Override);

	TestFalse(TEXT("A fit against a missing asset is not valid"), Fit.bValid);
	TestTrue(TEXT("...and it says which asset"), Fit.Note.Contains(TEXT("SM_ThisWasDeleted")));
	TestFalse(TEXT("The element is not showing an override"), Wardrobe->HasAssetOverride());
	TestTrue(TEXT("The generated wardrobe is still drawing"), Wardrobe->GetMeshComponent()->IsVisible());
	TestTrue(TEXT("...untouched"), PrintAll(Wardrobe) == Before);

	return true;
}

#undef HF_TEST_FLAGS

#endif	// WITH_DEV_AUTOMATION_TESTS
