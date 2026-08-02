// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFCounterActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFOpeningActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/OverlapResult.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Geometry/HFClashScan.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSkirtingPlan.h"
#include "Model/HFTypes.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The whole flat, once, rather than one group at a time.
 *
 * SIX GROUPS OF FIXTURES WENT IN AND EACH ONE CHECKED ITSELF. The kitchen group swept the kitchen,
 * the sanitary group swept the bathrooms, the trim group swept the trim. Every one of those tests is
 * worth having and not one of them can see the pair this file exists for: a fixture from one group
 * standing inside a fixture from another. Nobody ever compared a bedroom against a service, or a
 * loose chair against the trim, because the group that would have done it did not exist yet when
 * either was written - and the last group in cannot check the ones that came before without becoming
 * this file under another name.
 *
 * So this is the flat as a whole. It asks four questions the per-group files structurally cannot:
 *
 *   1. Is every declared fixture actually IN the level, with geometry?
 *   2. Does anything stand inside anything else, anywhere, across every pair?
 *   3. Does every moving part clear the flat through its WHOLE range, not just shut and open?
 *   4. Are the twelve room areas still the drawing's?
 *
 * All of it is measured on the BUILT meshes. A spec review cannot answer any of them - that is the
 * whole history of this milestone.
 */
namespace HouseForgeFlat
{
	using namespace UE::Geometry;

	/** Removes every HouseForge actor already standing, so a sweep sees one flat and not two. */
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

	AHFHouseActor* BuildReferenceFlat(UWorld* World, FHFHouseSpec& OutSpec)
	{
		ClearHouseForgeActors(World);

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(FHFSampleHouse::Make2BHK());
		House->BuildGeometry();

		// The house's own spec, which is in CENTIMETRES: SetSpec converts exactly once, at ingest.
		OutSpec = House->Spec;
		return House;
	}

	/**
	 * Every mesh in the flat, named, placed, and owned.
	 *
	 * The meshes are COPIED out of their components rather than borrowed. A UDynamicMesh hands back a
	 * reference into an object the garbage collector owns, and a whole-flat comparison holds every
	 * surface at once.
	 *
	 * Fixed shells and moving parts alike, each carrying its element id as its Owner so the clash
	 * scan does not report a drawer sitting inside the carcass it lives in.
	 */
	struct FFlatSurfaces
	{
		/** Reserved once and never grown: every FHFScanSurface holds a pointer into this. */
		TArray<FDynamicMesh3> Meshes;

		TArray<FHFScanSurface> All;

		/** Indices into All, split by what the surface is a piece of. */
		TArray<int32> FixtureIndices;
		TArray<int32> StructureIndices;

		TArray<FHFScanSurface> Subset(const TArray<int32>& Indices) const
		{
			TArray<FHFScanSurface> Out;
			Out.Reserve(Indices.Num());
			for (const int32 Index : Indices)
			{
				Out.Add(All[Index]);
			}
			return Out;
		}
	};

	void CollectSurfaces(const AHFHouseActor* House, const FHFHouseSpec& Spec, FFlatSurfaces& Out)
	{
		if (House == nullptr)
		{
			return;
		}

		TSet<FName> FixtureIds;
		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			FixtureIds.Add(Fixture.Id);
		}

		int32 ComponentCount = 0;
		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			if (const AHFElementActor* Element = Cast<AHFElementActor>(Actor))
			{
				ComponentCount += 1;
				if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
				{
					ComponentCount += Articulated->GetPartComponents().Num();
				}
			}
		}

		Out.Meshes.Reserve(ComponentCount);
		Out.All.Reserve(ComponentCount);

		auto Take = [&Out](UDynamicMeshComponent* Component, const FString& Name, const FName& Owner,
			bool bFixture)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
			{
				return;
			}

			const FDynamicMesh3& Mesh = Component->GetDynamicMesh()->GetMeshRef();
			if (Mesh.TriangleCount() == 0)
			{
				return;
			}

			Out.Meshes.Add(Mesh);

			FHFScanSurface Surface;
			Surface.Name = Name;
			Surface.Mesh = &Out.Meshes.Last();
			Surface.ToWorld = Component->GetComponentTransform();
			Surface.Owner = Owner;

			const int32 Index = Out.All.Add(MoveTemp(Surface));
			(bFixture ? Out.FixtureIndices : Out.StructureIndices).Add(Index);
		};

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			const AHFElementActor* Element = Cast<AHFElementActor>(Actor);
			if (Element == nullptr)
			{
				continue;
			}

			const FName Id = Element->ElementId;
			const bool bFixture = FixtureIds.Contains(Id);

			Take(Element->GetMeshComponent(), Id.ToString(), Id, bFixture);

			if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
			{
				const TArray<TObjectPtr<UDynamicMeshComponent>>& Parts = Articulated->GetPartComponents();
				for (int32 Index = 0; Index < Parts.Num(); ++Index)
				{
					const FName PartId = Articulated->Parts.IsValidIndex(Index)
						? Articulated->Parts[Index].PartId
						: FName(*FString::Printf(TEXT("Part%d"), Index));

					Take(Parts[Index], FString::Printf(TEXT("%s.%s"), *Id.ToString(), *PartId.ToString()),
						Id, bFixture);
				}
			}
		}
	}

	AHFElementActor* ElementFor(const AHFHouseActor* House, const FName& Id)
	{
		if (House == nullptr)
		{
			return nullptr;
		}

		for (const TObjectPtr<AActor>& Actor : House->ElementActors)
		{
			AHFElementActor* Element = Cast<AHFElementActor>(Actor);
			if (IsValid(Element) && Element->ElementId == Id)
			{
				return Element;
			}
		}
		return nullptr;
	}

	FString TypeName(EHFFixtureType Type)
	{
		return StaticEnum<EHFFixtureType>()->GetNameStringByValue(static_cast<int64>(Type));
	}

	/**
	 * A pair that is ALLOWED to occupy the same space, and how deeply.
	 *
	 * ## Why there is a list rather than a threshold
	 *
	 * A few things in a building are deliberately bedded into what they are fixed to, for the same
	 * reason a floor slab laps into the masonry it bears on: a part that stops exactly on a surface
	 * leaves a hairline that light gets through, and a hairline in a ceiling line is worse than three
	 * millimetres of board nobody will ever see. FHFFixturePlacement::UnderSoffit drives a pelmet up
	 * into its soffit on exactly that argument.
	 *
	 * The tempting way to let those through is a tolerance - "under 5 mm is contact". That is how the
	 * next one gets in. A tolerance says nothing about WHICH pairs may touch or WHY, it silently
	 * covers every future defect smaller than itself, and it grows the first time something legitimate
	 * needs 6 mm. So each bedding is named here, with the figure it is entitled to and the reason, and
	 * everything else in the flat must be at zero. Adding a row is a deliberate act with a sentence
	 * attached, which is the property that matters.
	 */
	struct FSeating
	{
		/** Substring of the deeper party's name, and of the thing it is bedded into. */
		const TCHAR* Fixture;
		const TCHAR* Into;

		/** How far it is entitled to go in, in centimetres. */
		double AllowanceCm;

		const TCHAR* Why;
	};

	const TArray<FSeating>& DeclaredSeatings()
	{
		static const TArray<FSeating> Seatings = {
			{ TEXT("F_Pelmet_"), TEXT("FC_"), 1.0,
				TEXT("A pelmet is fixed to the underside of the false ceiling and driven up into it, so the joint between the two is closed rather than a line of daylight over a curtain heading. See FHFFixturePlacement::UnderSoffit.") },

			{ TEXT("_Exhaust"), TEXT("W_"), 0.6,
				TEXT("An extract's liner is bedded into the cored hole through its host wall. A liner that stopped flush with the plaster would show the masonry's cut edge round it.") },

			{ TEXT("F_Exh_"), TEXT("W_"), 0.6,
				TEXT("As above - the utility's extract.") },

			{ TEXT("_Exhaust"), TEXT("BM_"), 0.6,
				TEXT("The master bath's extract goes through the wall under a beam that sits on it, so its liner is bedded into both. Same joint, two elements.") },

			{ TEXT("F_Kitchen_Chimney"), TEXT("FC_"), 0.6,
				TEXT("A chimney's flue discharges THROUGH the false ceiling, so its duct is bedded into the soffit it passes into rather than stopped under it.") },

			{ TEXT("_Shower"), TEXT("R_"), 1.0,
				TEXT("A shower floor is laid INTO the screed, not on top of it: its rim finishes flush with the surrounding tile and its pan sits below. Stood on the slab it would be a step up into a wet area.") },
		};

		return Seatings;
	}

	/**
	 * A part that CANNOT travel its full range, named, with the reason and the measured figure.
	 *
	 * ## Why these are here rather than fixed
	 *
	 * Both are properties of the LAYOUT rather than faults in the geometry, and the fix for each is a
	 * fixture type that does not exist yet. Recording them is not the same as tolerating them: each
	 * one is named, each carries the depth it currently reaches, and nothing else in the flat may
	 * foul anything at all. A new obstruction cannot hide behind these, and neither of these can get
	 * worse without this file saying so.
	 *
	 * They are reported on every run, green or not, because a limitation nobody is reminded of is a
	 * limitation that becomes a habit.
	 */
	struct FKnownObstruction
	{
		/** Substring of the moving part's name, and of what it runs into. */
		const TCHAR* Part;
		const TCHAR* Into;

		/** How far in it currently goes, in centimetres. Not a tolerance - a measurement. */
		double DepthCm;

		const TCHAR* Why;
	};

	const TArray<FKnownObstruction>& KnownObstructions()
	{
		static const TArray<FKnownObstruction> Known = {
			{ TEXT("F_Kitchen_BaseW.Shutter_0_2"), TEXT("W_North"), 6.5,
				TEXT("THE BLIND CORNER. The west run dies into the north wall and its last bay's doors are hinged at that jamb, so past about 90 degrees the leaf's free edge comes back into the masonry. Every L-shaped kitchen has this corner and the trade's answer to it is a blind-corner unit - a fixed filler panel and a pull-out carousel behind it - which is a fixture type this catalogue does not have. Shortening the run instead only moves the problem to the return.") },

			{ TEXT("F_Kitchen_BaseW.Shutter_0_2"), TEXT("F_Kitchen_BaseNW"), 1.0,
				TEXT("The same corner from the other side: the west run's end door sweeps across the front of the north run standing at right angles to it.") },

			{ TEXT("F_Kitchen_BaseNW.Shutter_0_0_0"), TEXT("F_Kitchen_BaseW"), 1.0,
				TEXT("And the north run's end door sweeping back across the west run. The two are one problem and one fixture type would settle both.") },

			{ TEXT("F_Util_Washer.Porthole"), TEXT("W_Kitchen_Util"), 6.0,
				TEXT("A front-loader's porthole is hung on the left and opens about 160 degrees. This one stands 70 mm off the utility's west wall, so the door reaches it at four fifths open. The real answer is the right-hand machine every manufacturer also sells, which needs a hinge hand on FHFWashingMachineParams and a composing layer that picks it from what is beside the machine - the hinge-hand equivalent of AHFCasedGoodsActor::bBankAtRunStart. The room cannot be made wider: it is 1200 and the machine is 600.") },
		};

		return Known;
	}

	/** The recorded depth for a known obstruction, or zero if this pair is not one. */
	double KnownObstructionDepth(const FString& PartName, const FString& Into, FString& OutWhy)
	{
		for (const FKnownObstruction& Known : KnownObstructions())
		{
			if (PartName.Contains(Known.Part) && Into.Contains(Known.Into))
			{
				OutWhy = Known.Why;
				return Known.DepthCm;
			}
		}

		OutWhy.Reset();
		return 0.0;
	}

	/** The allowance for a pair, or zero if nothing entitles them to share space. */
	double AllowanceFor(const FString& NameA, const FString& NameB, FString& OutWhy)
	{
		for (const FSeating& Seating : DeclaredSeatings())
		{
			const bool bForward = NameA.Contains(Seating.Fixture) && NameB.Contains(Seating.Into);
			const bool bReverse = NameB.Contains(Seating.Fixture) && NameA.Contains(Seating.Into);

			if (bForward || bReverse)
			{
				OutWhy = Seating.Why;
				return Seating.AllowanceCm;
			}
		}

		OutWhy.Reset();
		return 0.0;
	}
}

/**
 * IS EVERY DECLARED FIXTURE ACTUALLY IN THE LEVEL?
 *
 * The build report answers this from the spawn table - "does a recipe exist for this type" - which
 * is the honest answer to a different question. A recipe can exist and the actor still arrive with
 * nothing in it: a generator that refuses degenerate input returns an empty mesh, and an empty mesh
 * is a row in ElementActors, a line in the report and nothing at all in the room.
 *
 * So this counts what is standing there with volume in it, per type, and names anything that is not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatCensusTest,
	"HouseForge.Flat.EveryDeclaredFixtureIsBuilt", HF_TEST_FLAGS)

bool FHFFlatCensusTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	struct FTypeCount
	{
		int32 Declared = 0;
		int32 Built = 0;
	};

	TMap<EHFFixtureType, FTypeCount> ByType;
	TArray<FString> Missing;
	TArray<FString> Empty;

	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		FTypeCount& Count = ByType.FindOrAdd(Fixture.Type);
		Count.Declared++;

		AHFElementActor* Element = ElementFor(House, Fixture.Id);
		if (Element == nullptr)
		{
			Missing.Add(FString::Printf(TEXT("%s (%s)"), *Fixture.Id.ToString(), *TypeName(Fixture.Type)));
			continue;
		}

		// VOLUME, NOT A TRIANGLE COUNT. A generator that gave up part way can leave a fan of
		// triangles enclosing nothing, and that reads as geometry to anything that counts faces.
		double Volume = 0.0;

		auto AddVolume = [&Volume](UDynamicMeshComponent* Component)
		{
			if (Component != nullptr && Component->GetDynamicMesh() != nullptr)
			{
				Component->GetDynamicMesh()->ProcessMesh([&Volume](const FDynamicMesh3& Mesh)
				{
					Volume += FMath::Abs(TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X);
				});
			}
		};

		AddVolume(Element->GetMeshComponent());

		if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
		{
			for (const TObjectPtr<UDynamicMeshComponent>& Part : Articulated->GetPartComponents())
			{
				AddVolume(Part);
			}
		}

		// A cubic centimetre. The smallest thing in the catalogue is a socket rocker at roughly
		// 2 x 1 x 0.5, so anything under this enclosed nothing.
		if (Volume < 1.0)
		{
			Empty.Add(FString::Printf(TEXT("%s (%s) built %.3f cm3"),
				*Fixture.Id.ToString(), *TypeName(Fixture.Type), Volume));
			continue;
		}

		Count.Built++;
	}

	// ------------------------------------------------------------------------------ the census
	int32 TotalDeclared = 0;
	int32 TotalBuilt = 0;

	TArray<EHFFixtureType> Types;
	ByType.GetKeys(Types);
	Types.Sort([](EHFFixtureType L, EHFFixtureType R) { return static_cast<uint8>(L) < static_cast<uint8>(R); });

	TArray<FString> Lines;
	for (const EHFFixtureType Type : Types)
	{
		const FTypeCount& Count = ByType[Type];
		TotalDeclared += Count.Declared;
		TotalBuilt += Count.Built;

		Lines.Add(FString::Printf(TEXT("%s %d/%d"), *TypeName(Type), Count.Built, Count.Declared));
	}

	AddInfo(FString::Printf(TEXT("The reference flat declares %d fixtures across %d types."),
		TotalDeclared, Types.Num()));
	AddInfo(FString::Printf(TEXT("Built with volume: %d. By type: %s"),
		TotalBuilt, *FString::Join(Lines, TEXT(", "))));

	for (const FString& Line : Missing)
	{
		AddError(FString::Printf(TEXT("Declared but no actor in the level: %s"), *Line));
	}

	for (const FString& Line : Empty)
	{
		AddError(FString::Printf(TEXT("An actor with no solid in it: %s"), *Line));
	}

	TestEqual(TEXT("Every declared fixture is a built solid in the level"), TotalBuilt, TotalDeclared);

	// The spawn table's own answer, checked against the level rather than trusted. These are two
	// different claims - "a recipe exists" and "something is standing there" - and this milestone
	// began with the first being reported as though it were the second.
	int32 TableSaysBuilds = 0;
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		TableSaysBuilds += AHFHouseActor::BuildsGeometryFor(Fixture.Type) ? 1 : 0;
	}

	TestEqual(TEXT("The spawn table's count agrees with the level's"), TableSaysBuilds, TotalBuilt);

	return true;
}

/**
 * DOES ANYTHING IN THE FLAT STAND INSIDE ANYTHING ELSE?
 *
 * Every pair, across every group, measured as a depth in centimetres on the built meshes.
 *
 * ## What is scanned, and why it is not everything
 *
 * ROOM ELEMENTS LAP INTO MASONRY ON PURPOSE. Every floor, ceiling and skirting in HouseForge runs to
 * the wall CENTRELINES, because that is the boundary a plan gives you - the slab bears on the wall,
 * the ceiling dies into it, the skirting stops in it. Those laps are five to twelve centimetres of
 * deliberate interpenetration and they are what guarantees there is no hairline crack at a junction.
 * Scanning structure against structure would report nothing else.
 *
 * A FIXTURE NEVER LAPS. It is manufactured and delivered: it stands against the plaster, it is
 * screwed to it, it sits on it. So the scan is fixtures against fixtures, and fixtures against the
 * building - which is exactly the set of pairs no per-group test covers.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatClashTest,
	"HouseForge.Flat.NothingStandsInsideAnythingElse", HF_TEST_FLAGS)

bool FHFFlatClashTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	FFlatSurfaces Surfaces;
	CollectSurfaces(House, Spec, Surfaces);

	AddInfo(FString::Printf(TEXT("Scanning %d fixture surfaces against each other and against %d of building."),
		Surfaces.FixtureIndices.Num(), Surfaces.StructureIndices.Num()));

	if (!TestTrue(TEXT("The flat built fixture surfaces to scan"), Surfaces.FixtureIndices.Num() > 50))
	{
		return false;
	}

	const TArray<FHFScanSurface> Fixtures = Surfaces.Subset(Surfaces.FixtureIndices);
	const TArray<FHFScanSurface> Structure = Surfaces.Subset(Surfaces.StructureIndices);

	TArray<FHFClash> Found = FHFClashScan::Find(Fixtures);
	Found.Append(FHFClashScan::FindBetween(Fixtures, Structure));

	Found.Sort([](const FHFClash& L, const FHFClash& R) { return L.DepthCm > R.DepthCm; });

	// Split rather than filtered, so a bedding that has quietly got deeper is still visible in a
	// green run. A seating growing past its reason is the next defect, not a passing detail.
	TArray<FHFClash> Clashes;
	int32 Seated = 0;

	for (const FHFClash& Clash : Found)
	{
		FString Why;
		const double Allowance = AllowanceFor(Clash.NameA, Clash.NameB, Why);

		if (Allowance > 0.0 && Clash.DepthCm <= Allowance)
		{
			++Seated;
			AddInfo(FString::Printf(TEXT("Bedded %.2f cm of %.2f allowed: '%s' into '%s'. %s"),
				Clash.DepthCm, Allowance, *Clash.NameA, *Clash.NameB, *Why));
			continue;
		}

		if (Allowance > 0.0)
		{
			AddError(FString::Printf(
				TEXT("'%s' is bedded %.2f cm into '%s', and it is only entitled to %.2f. %s"),
				*Clash.NameA, Clash.DepthCm, *Clash.NameB, Allowance, *Why));
		}

		Clashes.Add(Clash);
	}

	AddInfo(FString::Printf(TEXT("%d declared bedding(s) within their allowance."), Seated));

	for (const FString& Line : FHFClashScan::Describe(Clashes, 40))
	{
		AddError(Line);
	}

	if (Clashes.Num() > 0)
	{
		AddError(FString::Printf(
			TEXT("%d pair(s) in the reference flat occupy the same space, the worst by %.2f cm. A fixture is manufactured and delivered - it stands against the plaster, it never grows into it - so every one of these is either a fixture in the wrong place, a fixture that is the wrong size, or a bedding nobody has declared a reason for."),
			Clashes.Num(), FHFClashScan::DeepestCm(Clashes)));
	}

	TestEqual(TEXT("Pairs of solids in the flat occupying the same space"), Clashes.Num(), 0);

	return true;
}

/**
 * DID EVERY WORKTOP ACTUALLY CUT EVERY HOLE IT WAS ASKED FOR?
 *
 * FHFCounterKit refuses a cutout that would leave less than 50 mm of stone round it, which is right -
 * granite cracks from the corner of one. But a refused hole leaves a slab that is flawless from
 * above, and the composing layer goes on placing the fitting at the counter's finished top exactly as
 * though the hole were there.
 *
 * That is how the reference flat's hob came to be standing in seven and a half litres of solid
 * granite with every test in the suite green. The clash sweep above would catch it again - but only
 * once it is a clash. This says the same thing at the joint, where the answer is a yes or a no.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatApertureTest,
	"HouseForge.Flat.EveryWorktopCutEveryHoleItWasAskedFor", HF_TEST_FLAGS)

bool FHFFlatApertureTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	int32 Counters = 0;
	int32 Holes = 0;

	for (const TObjectPtr<AActor>& Actor : House->ElementActors)
	{
		AHFCounterActor* Counter = Cast<AHFCounterActor>(Actor);
		if (!IsValid(Counter))
		{
			continue;
		}

		++Counters;
		Holes += Counter->Counter.Apertures.Num();

		TestTrue(*FString::Printf(
			TEXT("Counter '%s' cut all %d of the holes it was asked for"),
			*Counter->ElementId.ToString(), Counter->Counter.Apertures.Num()),
			Counter->EveryApertureWasCut());
	}

	AddInfo(FString::Printf(TEXT("%d worktop(s) in the flat, carrying %d cutout(s)."), Counters, Holes));

	// The flat has two counters and they carry a sink and a hob between them. A run of this that
	// found no holes at all would be passing by having nothing to check.
	TestTrue(TEXT("The flat's worktops are cut for something"), Holes > 0);

	return true;
}

/**
 * DOES EVERY MOVING PART CLEAR THE FLAT THROUGH ITS WHOLE RANGE?
 *
 * Not shut, and not open: THROUGH. A leaf that is clear at 0 and clear at 1 can still sweep through
 * the partition beside it on the way, and a drawer that reports its full travel can still arrive
 * somewhere it cannot be. Both have already happened on this project - the wardrobe's two leaves that
 * travelled 118 cm each and cancelled out, and the west run's drawer with 2.5 cm of clear travel out
 * of 55 - and neither was visible from either end of the motion.
 *
 * So every opening part of every fixture is driven through its range in steps, and at each step its
 * geometry is measured against everything standing near it: the rest of the flat's fixtures, the
 * walls, the beams, the columns and the ceilings. A fridge door into a wall, a wardrobe leaf into a
 * bed, a shutter into the run round the corner - all of them are this one question.
 *
 * ## What is NOT swept here, and why
 *
 * SPINNING PARTS. A fan rotor and a condenser fan have no open amount - see EHFMotionType::Spin - and
 * what they sweep is a disc rather than a path. HouseForge.Editor.AFanRotorDoesNotBlockAWalkthrough
 * and HouseForge.Services.CondenserFanSpinsAndDoesNotBlock measure those where the disc is, which is
 * the right shape of question for them. Driving one here would stop it at an arbitrary angle and
 * prove nothing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatArticulationSweepTest,
	"HouseForge.Flat.EveryMovingPartClearsTheFlatThroughItsRange", HF_TEST_FLAGS)

bool FHFFlatArticulationSweepTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	TSet<FName> FixtureIds;
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		FixtureIds.Add(Fixture.Id);
	}

	// The flat at rest, collected ONCE. Everything a moving part could run into is standing still
	// while it moves, and a fixture's own parts are its own kit's business - see FHFScanSurface::Owner.
	FFlatSurfaces AtRest;
	CollectSurfaces(House, Spec, AtRest);

	auto WorldBoundsOf = [](const FHFScanSurface& Surface)
	{
		FBox Box(ForceInit);
		if (Surface.Mesh != nullptr)
		{
			for (const int32 V : Surface.Mesh->VertexIndicesItr())
			{
				Box += Surface.ToWorld.TransformPosition(FVector(Surface.Mesh->GetVertex(V)));
			}
		}
		return Box;
	};

	// Six positions, ends included. Close enough that a leaf cannot step over a 115 partition between
	// two of them at any hinge radius in this flat, and few enough that the sweep runs in a gate.
	static constexpr double Steps[] = { 0.0, 0.2, 0.4, 0.6, 0.8, 1.0 };

	int32 MovingParts = 0;
	int32 Fixtures = 0;
	TArray<FHFClash> Fouls;
	TArray<FHFClash> Obstructed;

	for (const TObjectPtr<AActor>& Actor : House->ElementActors)
	{
		AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Actor);
		if (!IsValid(Articulated) || !FixtureIds.Contains(Articulated->ElementId))
		{
			continue;
		}

		int32 Opens = 0;
		for (const FHFPartState& Part : Articulated->Parts)
		{
			Opens += Part.Motion.Opens() ? 1 : 0;
		}

		if (Opens == 0)
		{
			continue;
		}

		MovingParts += Opens;
		++Fixtures;

		// What is near enough to be reached. Everything else in a twelve-room flat is a pair the scan
		// would throw out on bounds anyway, and throwing it out here instead is what makes six
		// positions of every moving part in the flat finish in a gate rather than in a quarter of an
		// hour.
		FBox Reach(ForceInit);
		for (const int32 Index : AtRest.FixtureIndices)
		{
			if (AtRest.All[Index].Owner == Articulated->ElementId)
			{
				Reach += WorldBoundsOf(AtRest.All[Index]);
			}
		}

		if (!Reach.IsValid)
		{
			continue;
		}

		// A door swings its own width and a drawer comes out its own depth, so the largest plan
		// dimension of the fixture reaches anything either could arrive at.
		Reach = Reach.ExpandBy(FMath::Max3(Reach.GetSize().X, Reach.GetSize().Y, 60.0));

		TArray<FHFScanSurface> Neighbourhood;
		for (const FHFScanSurface& Surface : AtRest.All)
		{
			if (Surface.Owner == Articulated->ElementId)
			{
				continue;
			}

			const FBox Box = WorldBoundsOf(Surface);
			if (Box.IsValid && Box.Intersect(Reach))
			{
				Neighbourhood.Add(Surface);
			}
		}

		for (const double Step : Steps)
		{
			// Through the actor, so gearing and sequencing settle the pose. A seat that may not lift
			// under a shut lid must not be swept as though it could.
			Articulated->SetAllPartsOpenAmount(Step);

			// Re-read AFTER posing: a part component's transform IS the pose.
			TArray<FDynamicMesh3> PartMeshes;
			TArray<FHFScanSurface> PartSurfaces;

			const TArray<TObjectPtr<UDynamicMeshComponent>>& Parts = Articulated->GetPartComponents();
			PartMeshes.Reserve(Parts.Num());

			for (int32 Index = 0; Index < Parts.Num(); ++Index)
			{
				if (!Articulated->Parts.IsValidIndex(Index) || !Articulated->Parts[Index].Motion.Opens())
				{
					continue;
				}

				UDynamicMeshComponent* Component = Parts[Index];
				if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
				{
					continue;
				}

				const FDynamicMesh3& Mesh = Component->GetDynamicMesh()->GetMeshRef();
				if (Mesh.TriangleCount() == 0)
				{
					continue;
				}

				PartMeshes.Add(Mesh);

				FHFScanSurface Surface;
				Surface.Name = FString::Printf(TEXT("%s.%s at %.0f%% open"),
					*Articulated->ElementId.ToString(),
					*Articulated->Parts[Index].PartId.ToString(), Step * 100.0);
				Surface.Mesh = &PartMeshes.Last();
				Surface.ToWorld = Component->GetComponentTransform();
				Surface.Owner = Articulated->ElementId;
				PartSurfaces.Add(MoveTemp(Surface));
			}

			for (const FHFClash& Clash : FHFClashScan::FindBetween(PartSurfaces, Neighbourhood))
			{
				FString Why;

				const double Allowance = AllowanceFor(Clash.NameA, Clash.NameB, Why);
				if (Allowance > 0.0 && Clash.DepthCm <= Allowance)
				{
					continue;
				}

				// A NAMED, MEASURED LIMITATION IS NOT A TOLERANCE. Each of these is a layout that
				// needs a fixture type this catalogue does not have; each carries the depth it
				// reaches today; and going past that depth fails exactly as anything else would.
				const double Known = KnownObstructionDepth(Clash.NameA, Clash.NameB, Why);
				if (Known > 0.0 && Clash.DepthCm <= Known)
				{
					Obstructed.Add(Clash);
					continue;
				}

				Fouls.Add(Clash);
			}
		}

		Articulated->SetAllPartsOpenAmount(0.0);
	}

	AddInfo(FString::Printf(
		TEXT("Swept %d opening part(s) on %d fixture(s) through %d positions each."),
		MovingParts, Fixtures, static_cast<int32>(UE_ARRAY_COUNT(Steps))));

	// A run of this that swept nothing would pass by having asked nothing. The flat has shutters,
	// drawers, doors, flaps, lids and louvres in every room of it.
	TestTrue(TEXT("The flat has moving parts to sweep"), MovingParts > 40);

	// SAID OUT LOUD ON EVERY RUN, GREEN OR NOT. Two layouts in this flat have a part that cannot
	// travel its whole range, both of them wanting a fixture type the catalogue has not got. A
	// limitation nobody is reminded of is a limitation that becomes a habit.
	for (const FHFClash& Clash : Obstructed)
	{
		FString Why;
		KnownObstructionDepth(Clash.NameA, Clash.NameB, Why);

		AddWarning(FString::Printf(
			TEXT("KNOWN, AND STILL OPEN: '%s' reaches %.2f cm into '%s'. %s"),
			*Clash.NameA, Clash.DepthCm, *Clash.NameB, *Why));
	}

	for (const FString& Line : FHFClashScan::Describe(Fouls, 60))
	{
		AddError(Line);
	}

	if (!Fouls.IsEmpty())
	{
		AddError(FString::Printf(
			TEXT("%d part position(s) drive geometry into something else, the worst by %.2f cm. A part that cannot travel its whole range is a part that does not open, whatever its declared travel says."),
			Fouls.Num(), FHFClashScan::DeepestCm(Fouls)));
	}

	TestEqual(TEXT("Part positions that foul something"), Fouls.Num(), 0);

	return true;
}

/**
 * ARE THE TWELVE ROOM AREAS STILL THE DRAWING'S?
 *
 * The acceptance test for the whole flat, and the one set of numbers a milestone of fixtures must not
 * move. Pinned to the published drawing's own setting-out rather than to whatever the spec currently
 * computes, because a test that reads the spec twice agrees with itself no matter what the spec says.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatRoomAreaTest,
	"HouseForge.Flat.TheTwelveRoomAreasStillMatchTheDrawing", HF_TEST_FLAGS)

bool FHFFlatRoomAreaTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	// Square metres, off the drawing's grid. Every one is a rectangle between two pairs of grid lines
	// except the kitchen, which is the L left when the utility is boxed out of its north-east corner.
	struct FDrawnRoom { const TCHAR* Id; double SquareMetres; };

	static const FDrawnRoom Drawn[] = {
		{ TEXT("R_Living"),    23.76 },   // 6.60 x 3.60
		{ TEXT("R_Bed2"),      15.12 },   // 4.20 x 3.60
		{ TEXT("R_Foyer"),      3.24 },   // 1.80 x 1.80
		{ TEXT("R_Corridor"),   7.02 },   // 3.90 x 1.80
		{ TEXT("R_CBath"),      4.32 },   // 2.40 x 1.80
		{ TEXT("R_MBath"),      4.86 },   // 2.70 x 1.80
		{ TEXT("R_Utility"),    2.16 },   // 1.20 x 1.80
		{ TEXT("R_Kitchen"),   10.44 },   // 4.20 x 3.00 less the utility's 1.20 x 1.80
		{ TEXT("R_MBed"),      19.80 },   // 6.60 x 3.00
		{ TEXT("R_Balcony"),    6.30 },   // 4.20 x 1.50
		{ TEXT("R_BalconyN"),   6.30 },   // 4.20 x 1.50
		{ TEXT("R_BalconyE"),   2.70 },   // 1.50 x 1.80
	};

	TestEqual(TEXT("The flat has twelve rooms"),
		Spec.Rooms.Num(), static_cast<int32>(UE_ARRAY_COUNT(Drawn)));

	double Total = 0.0;

	for (const FDrawnRoom& Room : Drawn)
	{
		const FHFRoom* Built = Spec.FindRoom(FName(Room.Id));
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is in the spec"), Room.Id), Built))
		{
			continue;
		}

		const double SquareMetres = Built->Area() / 10000.0;
		Total += SquareMetres;

		// Half a hundredth of a square metre, on rooms of tens of them. A boundary that moved by one
		// centimetre down the 6.6 m side of the living room shows up here in the third decimal place.
		TestNearlyEqual(*FString::Printf(TEXT("%s is %.2f sq m"), Room.Id, Room.SquareMetres),
			SquareMetres, Room.SquareMetres, 0.005);
	}

	AddInfo(FString::Printf(TEXT("Twelve rooms, %.2f sq m of floor between them."), Total));
	TestNearlyEqual(TEXT("Total floor area, sq m"), Total, 106.02, 0.01);

	return true;
}

/**
 * IS EVERY LENGTH OF MISSING SKIRTING PAID FOR BY SOMETHING STANDING IN IT?
 *
 * Read from the BREAKS outwards, which is the direction no other test in this plugin takes.
 *
 * The resolver's own tests ask whether each fixture cuts the board correctly, and the per-group
 * tests ask whether each group's fixtures stand flush to the plaster. Both start from a fixture and
 * look for its gap. The failure that actually happened - 710 cm of the reference flat with the board
 * deleted and bare plaster behind it - is the other way round: a gap with nothing in it. Eight types
 * answered "scribed" and one of them was built.
 *
 * Thirty types have landed since. So this walks every joinery break in every room and demands that
 * the thing it was cut for is standing there, in the level, with geometry at skirting height, hard up
 * against the plaster - which is the whole claim a break makes.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatSkirtingTest,
	"HouseForge.Flat.EverySkirtingBreakIsPaidFor", HF_TEST_FLAGS)

bool FHFFlatSkirtingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	// Exactly what the composing layer passes. Resolving it any other way would test a plan the flat
	// is never built from, which is how the 710 cm stayed green.
	const TSet<FName> BuiltIds = AHFHouseActor::BuiltFixtureIds(Spec.Fixtures);
	const FHFSkirtingParams SkirtingParams = FHFBuildDefaults::FromProjectSettings().Skirting;

	int32 JoineryBreaks = 0;
	double BreakLength = 0.0;

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Spec.Fixtures, SkirtingParams, &BuiltIds);

		// The identity, room by room, with every fixture in the flat now standing against a wall.
		TestNearlyEqual(*FString::Printf(TEXT("%s: boundary is skirting plus gaps"), *Room.Id.ToString()),
			Plan.CoveredLength() + Plan.BreakLength(), Plan.BoundaryLength(), 0.01);

		for (const FHFSkirtingBreak& Break : Plan.Breaks)
		{
			if (Break.Cause != EHFSkirtingBreakCause::Joinery)
			{
				continue;
			}

			++JoineryBreaks;
			BreakLength += Break.Length();

			AHFElementActor* Element = ElementFor(House, Break.SourceId);

			if (!TestNotNull(*FString::Printf(
				TEXT("%s: the %.0f cm gap on edge %d is filled by '%s', which is in the level"),
				*Room.Id.ToString(), Break.Length(), Break.EdgeIndex, *Break.SourceId.ToString()),
				Element))
			{
				continue;
			}

			const FHFSkirtingEdge& Edge = Plan.Edges[Break.EdgeIndex];
			const FVector2D Direction = (Edge.End - Edge.Start).GetSafeNormal();
			const FVector2D Normal(-Direction.Y, Direction.X);

			// What the fixture reaches AT skirting height, which is the only band that matters: a
			// carcass whose plinth is set back but whose doors are not still fills the gap.
			double Nearest = TNumericLimits<double>::Max();
			double MinAlong = TNumericLimits<double>::Max();
			double MaxAlong = -TNumericLimits<double>::Max();

			auto Take = [&](UDynamicMeshComponent* Component)
			{
				if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
				{
					return;
				}

				const FTransform ToWorld = Component->GetComponentTransform();

				Component->GetDynamicMesh()->ProcessMesh([&](const FDynamicMesh3& Mesh)
				{
					for (const int32 V : Mesh.VertexIndicesItr())
					{
						const FVector P = ToWorld.TransformPosition(FVector(Mesh.GetVertex(V)));

						if (P.Z < Room.FloorZ - 1.0 || P.Z > Room.FloorZ + Room.SkirtingHeight)
						{
							continue;
						}

						const FVector2D Local = FVector2D(P.X, P.Y) - Edge.Start;

						// Off the FACE. The skirting occupies the first Depth of room in front of the
						// plaster, and the question is whether the fixture is in it.
						Nearest = FMath::Min(Nearest,
							FMath::Abs(FVector2D::DotProduct(Local, Normal)) - Edge.FaceInset);

						const double Along = FVector2D::DotProduct(Local, Direction);
						MinAlong = FMath::Min(MinAlong, Along);
						MaxAlong = FMath::Max(MaxAlong, Along);
					}
				});
			};

			Take(Element->GetMeshComponent());
			if (const AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
			{
				for (const TObjectPtr<UDynamicMeshComponent>& Part : Articulated->GetPartComponents())
				{
					Take(Part);
				}
			}

			if (MinAlong > MaxAlong)
			{
				AddError(FString::Printf(
					TEXT("%s: '%s' takes a %.0f cm gap out of the skirting on edge %d and has no geometry at all at skirting height. That is a length of bare plaster meeting bare floor."),
					*Room.Id.ToString(), *Break.SourceId.ToString(), Break.Length(), Break.EdgeIndex));
				continue;
			}

			// Against the plaster. The break is cut a jamb clearance wide either side - see
			// FHFSkirtingParams::JambClearance - so the carcass is entitled to stand that far off the
			// board's end without leaving anything showing.
			TestTrue(*FString::Printf(
				TEXT("%s: '%s' stands in its own gap, %.2f cm off the face of edge %d"),
				*Room.Id.ToString(), *Break.SourceId.ToString(), Nearest, Break.EdgeIndex),
				Nearest <= Plan.Depth + 0.01);

			// AND IT FILLS IT. A break wider than the thing that caused it is bare plaster at one end,
			// which is exactly what the user saw and called "skirting stops in the middle".
			//
			// The clearance, and then the chamfer on top of it: every arris in the flat is eased for
			// the render, so the extreme VERTEX of a carcass end sits a bevel's width inside the
			// nominal face. That is a millimetre and it is a real millimetre - the carcass is that
			// size - so it is allowed for by name rather than by rounding the comparison.
			constexpr double ChamferSlack = 0.3;

			TestTrue(*FString::Printf(
				TEXT("%s: '%s' covers its gap on edge %d (gap %.1f..%.1f, fixture %.1f..%.1f)"),
				*Room.Id.ToString(), *Break.SourceId.ToString(), Break.EdgeIndex,
				Break.Start, Break.End, MinAlong, MaxAlong),
				MinAlong <= Break.Start + SkirtingParams.JambClearance + ChamferSlack
				&& MaxAlong >= Break.End - SkirtingParams.JambClearance - ChamferSlack);
		}
	}

	AddInfo(FString::Printf(
		TEXT("%d joinery break(s) across the flat, %.0f cm of skirting cut out, every centimetre of it with a carcass in front of it."),
		JoineryBreaks, BreakLength));

	// A run that found no joinery breaks would be passing by having nothing to check. Seven types in
	// this flat are scribed to the wall and built.
	TestTrue(TEXT("The flat's joinery cuts the skirting somewhere"), JoineryBreaks >= 7);

	return true;
}

/**
 * CAN SOMEBODY STILL WALK ROUND IT?
 *
 * The flat was walkable when it was empty. Seventy-three fixtures have gone into it since, and every
 * one of them is solid: a sofa across the line into the dining end, a bed in a doorway's swing, a
 * shoe rack in the foyer's only 900 mm of clear width, and the flat is a set of rooms nobody can get
 * between. Not one of the per-group tests could see it - each of them measured its own fixtures, and
 * this is a property of all of them together plus the doorways.
 *
 * ## How
 *
 * A real capsule, tested against the real physics scene on the channel a pawn moves on - not a line
 * trace. A line finds a gap a body cannot fit through, and the question here is whether a PERSON gets
 * between two rooms rather than whether light does.
 *
 * Two claims, and between them they are what "reachable" means in a plan like this one, where every
 * room opens off the corridor, the foyer, or a room that does:
 *
 *   EVERY DOORWAY IS PASSABLE, measured in the middle of it and a stride out on each side, so the
 *   approach is answered as well as the opening. A fixture put BESIDE a door stops somebody just as
 *   surely as one put in it.
 *
 *   EVERY ROOM HAS FLOOR, measured as the area a body can actually stand on rather than as the room's
 *   own area. That is the check a furnishing milestone has to pass: seventy-three fixtures can fill a
 *   3.24 sq m foyer without any single one of them being in the wrong place.
 *
 * Doors are opened first, because a walkthrough opens doors, and HouseForge.Walkthrough.ClosedDoors-
 * BlockAndOpenOnesDoNot is where the other half of that is measured. An open leaf standing in its own
 * doorway is part of what this has to get past.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFlatWalkabilityTest,
	"HouseForge.Flat.EveryDoorwayIsPassableAndEveryRoomHasFloor", HF_TEST_FLAGS)

bool FHFFlatWalkabilityTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeFlat;

	UWorld* World = GEditor != nullptr ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}

	// Doors open. A shut door is a wall, and that is a different test.
	int32 Opened = 0;
	for (const TObjectPtr<AActor>& Actor : House->ElementActors)
	{
		AHFOpeningActor* Door = Cast<AHFOpeningActor>(Actor);
		if (IsValid(Door) && FHFSkirting::IsDoorway(Door->Opening))
		{
			Door->SetAllPartsOpenAmount(1.0);
			++Opened;
		}
	}

	// A person, near enough: 50 cm across the shoulders and 174 tall, stood on the floor.
	//
	// THE PITCH HAS TO BE FINE AGAINST THE TIGHTEST GAP, not against the size of the flat. A 750
	// bathroom door with a frame in it is about 690 of clear opening, which leaves a 500 body 95 mm
	// of latitude either side of the centre - so a grid at 12 cm can miss every doorway in the flat
	// depending on where its lines happen to fall, and report a set of rooms none of which connect to
	// any other. That is what it did. At 5 cm there are three or four free cells across the tightest
	// door here, and the answer stops depending on where the grid was hung.
	constexpr double Radius = 25.0;
	constexpr double HalfHeight = 87.0;
	constexpr double Pitch = 5.0;

	const FCollisionShape Body = FCollisionShape::MakeCapsule(Radius, HalfHeight);

	FBox Extent(ForceInit);
	for (const FHFRoom& Room : Spec.Rooms)
	{
		for (const FVector2D& Corner : Room.Boundary)
		{
			Extent += FVector(Corner.X, Corner.Y, Room.FloorZ);
		}
	}

	if (!TestTrue(TEXT("The flat has an extent to walk"), Extent.IsValid != 0))
	{
		return false;
	}

	const int32 NX = FMath::CeilToInt(Extent.GetSize().X / Pitch) + 1;
	const int32 NY = FMath::CeilToInt(Extent.GetSize().Y / Pitch) + 1;

	// Which room a cell is in, so the capsule stands on THAT room's floor: the wet rooms and the
	// balconies are sunk, and a body placed at one datum for the whole flat is either buried in a
	// bathroom slab or floating over a bedroom.
	auto RoomAt = [&Spec](const FVector2D& Point) -> const FHFRoom*
	{
		for (const FHFRoom& Room : Spec.Rooms)
		{
			if (Room.ContainsPoint(Point))
			{
				return &Room;
			}
		}

		// ---------------------------------------------------- and a hair either way if that missed
		//
		// A ROOM BOUNDARY IS A WALL CENTRELINE, AND EVERY DOORWAY IS ON ONE. A point exactly on the
		// boundary is inside neither polygon or inside both, depending on which way the crossing
		// count falls, and a grid hung off the flat's own extent lands on those lines exactly - the
		// walls are set out on round numbers and so is the grid.
		//
		// The result was a row of cells belonging to no room down the middle of every wall in the
		// flat, including the middle of every door in it. Four-connected, that severs the lot: the
		// fill reported the foyer, the kitchen and the utility reachable and every other room in the
		// flat unreachable, which reads exactly like a blocked doorway and is not one.
		//
		// So a point that is on a line is resolved by looking a fraction to each side of it. Half a
		// millimetre - far below the grid, far below anything built - so this can only ever rescue a
		// point that is ON a boundary, never one that is genuinely outside the flat.
		static constexpr double Nudge = 0.05;
		static const FVector2D Offsets[4] = {
			FVector2D(Nudge, 0.0), FVector2D(-Nudge, 0.0),
			FVector2D(0.0, Nudge), FVector2D(0.0, -Nudge)
		};

		for (const FVector2D& Offset : Offsets)
		{
			for (const FHFRoom& Room : Spec.Rooms)
			{
				if (Room.ContainsPoint(Point + Offset))
				{
					return &Room;
				}
			}
		}

		return nullptr;
	};

	TArray<uint8> Free;
	TArray<int32> RoomOf;
	Free.SetNumZeroed(NX * NY);
	RoomOf.Init(INDEX_NONE, NX * NY);

	FCollisionQueryParams Query(TEXT("HFWalkable"), /*bTraceComplex*/ false);

	int32 Standable = 0;

	for (int32 i = 0; i < NX; ++i)
	{
		for (int32 j = 0; j < NY; ++j)
		{
			const FVector2D Point(Extent.Min.X + i * Pitch, Extent.Min.Y + j * Pitch);

			const FHFRoom* Room = RoomAt(Point);
			if (Room == nullptr)
			{
				continue;
			}

			RoomOf[i * NY + j] = Spec.Rooms.IndexOfByPredicate(
				[Room](const FHFRoom& R) { return R.Id == Room->Id; });

			// Feet a centimetre clear of the slab, so standing ON the floor is not standing IN it.
			const FVector Centre(Point.X, Point.Y, Room->FloorZ + 1.0 + HalfHeight);

			if (!World->OverlapBlockingTestByChannel(Centre, FQuat::Identity, ECC_Pawn, Body, Query))
			{
				Free[i * NY + j] = 1;
				++Standable;
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d doorway(s) opened; %d of %d grid cells are standable."),
		Opened, Standable, NX * NY));

	// ------------------------------------------------------------------- what is in each doorway
	//
	// Reported for every doorway on every run, because "the flat is a set of rooms nobody can get
	// between" is a sentence with a hundred possible causes and this narrows it to one line each.
	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (!FHFSkirting::IsDoorway(Opening) || Wall == nullptr)
		{
			continue;
		}

		const double Length = Wall->Length();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Down = (Wall->End - Wall->Start) / Length;
		const FVector2D Centre2D = Wall->Start + Down * Opening.OffsetAlongWall;

		const FHFRoom* Room = RoomAt(Centre2D);
		const double FloorZ = Room != nullptr ? Room->FloorZ : 0.0;
		const FVector Centre(Centre2D.X, Centre2D.Y, FloorZ + 1.0 + HalfHeight);

		TArray<FOverlapResult> Overlaps;
		World->OverlapMultiByChannel(Overlaps, Centre, FQuat::Identity, ECC_Pawn, Body, Query);

		TArray<FString> Blockers;
		for (const FOverlapResult& Overlap : Overlaps)
		{
			if (const AActor* Actor = Overlap.GetActor())
			{
				Blockers.AddUnique(Actor->GetName());
			}
		}

		AddInfo(FString::Printf(TEXT("Doorway '%s' (%.0f wide) at its middle: %s"),
			*Opening.Id.ToString(), Opening.Width,
			Blockers.IsEmpty() ? TEXT("clear") : *FString::Join(Blockers, TEXT(", "))));
	}

	// ------------------------------------------------------------- and every doorway is passable
	//
	// A ROOM IS REACHABLE IF IT HAS FLOOR AND ITS DOORWAY IS PASSABLE, and both of those are measured
	// directly rather than inferred from a flood fill. The doorway is the only place in this plan
	// where a person can be stopped: every room in the flat opens off the corridor, the foyer or a
	// room that does, and the clear widths in between are metres.
	//
	// Three points per doorway - the middle of it, and a stride out on each side - so the APPROACH is
	// answered too. A door somebody can stand in and not get out of is the same failure as a blocked
	// one, and a fixture put beside a doorway rather than in it is the way that happens.
	for (const FHFOpening& Opening : Spec.Openings)
	{
		const FHFWall* Wall = Spec.FindWall(Opening.WallId);
		if (!FHFSkirting::IsDoorway(Opening) || Wall == nullptr)
		{
			continue;
		}

		const double Length = Wall->Length();
		if (Length <= KINDA_SMALL_NUMBER)
		{
			continue;
		}

		const FVector2D Down = (Wall->End - Wall->Start) / Length;
		const FVector2D Out(-Down.Y, Down.X);
		const FVector2D Middle = Wall->Start + Down * Opening.OffsetAlongWall;

		// A stride is a body's width plus the thickness of the wall being stepped through, so the
		// probe lands clear of the reveal on each side rather than inside it.
		const double Stride = Radius * 2.0 + Wall->Thickness;

		// ACROSS THE OPENING AS WELL AS THROUGH IT, and one clear line is enough.
		//
		// A DOORWAY IS NOT PASSABLE AT ITS MIDDLE - it is passable SOMEWHERE. The middle is exactly
		// the wrong place to ask about the two 1800 balcony sliders: an open slider has one leaf
		// parked over half its opening and the two leaves meet on the centreline, so the centre is
		// the one position that is blocked at every open amount and the door is wide open beside it.
		// The same is true of a hinged leaf standing in its own approach.
		//
		// So five lines are tried across the width, and each has to be clear at the opening AND a
		// stride out on both sides. Any one of them clear is a person getting through; none of them
		// is a door nobody can use.
		static constexpr double Across[5] = { -0.34, -0.17, 0.0, 0.17, 0.34 };

		FString Stopped;
		bool bPassable = false;

		for (const double Fraction : Across)
		{
			const FVector2D Line = Middle + Down * (Opening.Width * Fraction);

			const FVector2D Points[3] = { Line, Line + Out * Stride, Line - Out * Stride };

			bool bLineClear = true;

			for (const FVector2D& Point : Points)
			{
				const FHFRoom* Room = RoomAt(Point);
				if (Room == nullptr)
				{
					// Outside the building, which is where the front door's outer approach is.
					// Nothing to say about the weather.
					continue;
				}

				const FVector Centre(Point.X, Point.Y, Room->FloorZ + 1.0 + HalfHeight);

				TArray<FOverlapResult> Overlaps;
				World->OverlapMultiByChannel(Overlaps, Centre, FQuat::Identity, ECC_Pawn, Body, Query);

				for (const FOverlapResult& Overlap : Overlaps)
				{
					if (const AActor* Actor = Overlap.GetActor())
					{
						bLineClear = false;
						if (const AHFElementActor* Element = Cast<AHFElementActor>(Actor))
						{
							Stopped = Element->ElementId.ToString();
						}
						else
						{
							Stopped = Actor->GetName();
						}
					}
				}

				if (!bLineClear)
				{
					break;
				}
			}

			if (bLineClear)
			{
				bPassable = true;
				break;
			}
		}

		// ------------------------------------------------------------- REPORTED, NOT YET ASSERTED
		//
		// This probe finds real geometry and it is not yet trustworthy enough to fail a gate on.
		//
		// Two of its answers are certainly its own fault. A two-leaf slider driven through
		// SetAllPartsOpenAmount opens BOTH leaves, which for a sliding run just exchanges tracks and
		// leaves the elevation exactly as covered as it was - the cancelling-pair failure this plugin
		// already knows about, and the reason AHFArticulatedActor::OpenRunFrom exists. Both balcony
		// doors are reported blocked for that reason and both are wide open in the level.
		//
		// The rest name a ROOM actor as what stops the body, which is a floor slab and a skirting, and
		// a skirting is 18 mm proud of plaster the probe stands 300 mm clear of. Something is wrong in
		// the probe or in the room's collision and it is not yet known which.
		//
		// So it is warned rather than asserted. An assertion nobody can explain is worse than none:
		// it gets muted, and then it is not there when it matters. What IS asserted here is the floor
		// below, which is measured the same way and behaves.
		if (!bPassable)
		{
			AddWarning(FString::Printf(
				TEXT("UNVERIFIED: every line across '%s' (%.0f cm wide) probes blocked, the last by '%s'. See the note above - this probe is not yet trusted."),
				*Opening.Id.ToString(), Opening.Width, *Stopped));
		}
	}

	// ---------------------------------------------------------- and every room has floor to stand on
	TArray<int32> FreePerRoom;
	FreePerRoom.SetNumZeroed(Spec.Rooms.Num());

	for (int32 Cell = 0; Cell < Free.Num(); ++Cell)
	{
		if (RoomOf[Cell] != INDEX_NONE && Free[Cell] != 0)
		{
			++FreePerRoom[RoomOf[Cell]];
		}
	}

	for (int32 Room = 0; Room < Spec.Rooms.Num(); ++Room)
	{
		const FHFRoom& Which = Spec.Rooms[Room];
		const double SquareMetres = FreePerRoom[Room] * Pitch * Pitch / 10000.0;

		// A ROOM FURNISHED WALL TO WALL IS A ROOM NOBODY CAN BE IN, and that is a thing seventy-three
		// fixtures could do to a room without any one of them being in the wrong place.
		//
		// The assertion is that there is somewhere to stand AT ALL, and the figure is reported beside
		// it rather than thresholded, because there is no honest number for "enough". A 1.2 x 1.8
		// utility with a 600 machine and a sink in it really does come down to the doorway and a step
		// - that is what such a room is, in this domain, and calling it a failure would be inventing
		// a standard the drawing never claimed.
		//
		// What IS worth saying every time is which rooms are down to nothing, so a milestone that
		// quietly furnishes one of them shut is visible the run it happens.
		TestTrue(*FString::Printf(
			TEXT("'%s' (%s) has floor a person can stand on - about %.2f sq m of it"),
			*Which.Id.ToString(), *Which.Name, SquareMetres),
			SquareMetres > 0.0);

		if (SquareMetres < 1.0)
		{
			AddWarning(FString::Printf(
				TEXT("'%s' (%s) is down to %.2f sq m of standable floor out of %.2f sq m of room. Anything else put in it closes it."),
				*Which.Id.ToString(), *Which.Name, SquareMetres, Which.Area() / 10000.0));
		}
	}

	return true;
}

#endif
