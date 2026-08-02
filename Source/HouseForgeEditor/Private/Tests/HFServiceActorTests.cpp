// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFServiceActors.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFFixturePlacement.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSkirtingPlan.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The services group, in the flat rather than on a bench.
 *
 * TWENTY-ONE FITTINGS AND NOT ONE OF THEM IS INDIVIDUALLY IMPORTANT, which is exactly what makes this
 * file necessary. Thirteen of them are sockets and switch plates: a plate that stands 60 mm off the
 * plaster instead of 20 is not one mistake, it is the same mistake at eye level in every room of the
 * flat at once, and there is nothing in any parameter struct that would show it. The bench tests
 * measure a plate against its own drawn box; only this measures it against the wall it is screwed to.
 *
 * The other two failures this group could produce are both about the FLOOR: a refrigerator and a
 * washing machine are not scribed joinery, so the skirting board runs on behind them and both have to
 * stand in front of it - and a fridge door that swings 110 degrees has to have somewhere to swing.
 */
namespace HouseForgeServices
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

	const FHFFixture* FixtureFor(const FHFHouseSpec& Spec, const FName& Id)
	{
		for (const FHFFixture& Candidate : Spec.Fixtures)
		{
			if (Candidate.Id == Id)
			{
				return &Candidate;
			}
		}
		return nullptr;
	}

	/** Accumulates a component's vertices, in world space, into a box. */
	void AccumulateComponent(UDynamicMeshComponent* Component, FBox& Out)
	{
		if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
		{
			return;
		}

		const FTransform ToWorld = Component->GetComponentTransform();

		Component->GetDynamicMesh()->ProcessMesh([&Out, &ToWorld](const FDynamicMesh3& Mesh)
		{
			for (const int32 Vertex : Mesh.VertexIndicesItr())
			{
				Out += ToWorld.TransformPosition(FVector(Mesh.GetVertex(Vertex)));
			}
		});
	}

	/**
	 * Everything an element occupies in the world, moving parts included, in their CLOSED pose.
	 *
	 * Off the vertices rather than off the component's own Bounds, because a scene proxy's bounds are
	 * padded and this is being compared against masonry at millimetre tolerances.
	 */
	FBox WorldBounds(AHFElementActor& Element)
	{
		FBox Out(ForceInit);

		AccumulateComponent(Element.GetMeshComponent(), Out);

		if (AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(&Element))
		{
			for (const TObjectPtr<UDynamicMeshComponent>& Part : Articulated->GetPartComponents())
			{
				AccumulateComponent(Part.Get(), Out);
			}
		}

		return Out;
	}

	/** Only the FIXED geometry of an element - what it occupies with nothing opened. */
	FBox ShellBounds(AHFElementActor& Element)
	{
		FBox Out(ForceInit);
		AccumulateComponent(Element.GetMeshComponent(), Out);
		return Out;
	}

	/** The solid a wall occupies: its centreline swept by half its thickness, over its own height. */
	FBox SolidOf(const FHFWall& Wall)
	{
		const FVector2D Along = (Wall.End - Wall.Start).GetSafeNormal();
		const FVector2D Across(-Along.Y, Along.X);
		const FVector2D Half = Across * (Wall.Thickness * 0.5);

		FBox Out(ForceInit);
		Out += FVector(Wall.Start.X + Half.X, Wall.Start.Y + Half.Y, Wall.BaseZ);
		Out += FVector(Wall.Start.X - Half.X, Wall.Start.Y - Half.Y, Wall.BaseZ);
		Out += FVector(Wall.End.X + Half.X, Wall.End.Y + Half.Y, Wall.BaseZ + Wall.Height);
		Out += FVector(Wall.End.X - Half.X, Wall.End.Y - Half.Y, Wall.BaseZ + Wall.Height);

		return Out;
	}

	/** The clear opening a door leaves in its wall: full wall thickness, sill to head. */
	FBox ClearOpeningOf(const FHFOpening& Opening, const FHFWall& Wall)
	{
		const double Length = Wall.Length();
		if (Length <= UE_KINDA_SMALL_NUMBER)
		{
			return FBox(ForceInit);
		}

		const FVector2D Along = (Wall.End - Wall.Start) / Length;
		const FVector2D Across(-Along.Y, Along.X);

		const FVector2D A = Wall.Start + Along * Opening.OffsetAlongWall;
		const FVector2D B = A + Along * Opening.Width;
		const FVector2D Half = Across * (Wall.Thickness * 0.5);

		FBox Out(ForceInit);
		Out += FVector(A.X + Half.X, A.Y + Half.Y, Wall.BaseZ + Opening.SillHeight);
		Out += FVector(A.X - Half.X, A.Y - Half.Y, Wall.BaseZ + Opening.SillHeight);
		Out += FVector(B.X + Half.X, B.Y + Half.Y, Wall.BaseZ + Opening.SillHeight + Opening.Height);
		Out += FVector(B.X - Half.X, B.Y - Half.Y, Wall.BaseZ + Opening.SillHeight + Opening.Height);

		return Out;
	}

	/** How far two boxes interpenetrate on their least-overlapping axis. Zero or less means clear. */
	double Interpenetration(const FBox& A, const FBox& B)
	{
		if (!A.IsValid || !B.IsValid)
		{
			return -1.0;
		}

		const FVector Overlap(
			FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X),
			FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z));

		return FMath::Min3(Overlap.X, Overlap.Y, Overlap.Z);
	}

	/** What an element occupies within one horizontal slice of the world. */
	FBox WorldBoundsInBand(AHFElementActor& Element, double Z0, double Z1)
	{
		FBox Out(ForceInit);

		auto Accumulate = [&Out, Z0, Z1](UDynamicMeshComponent* Component)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
			{
				return;
			}

			const FTransform ToWorld = Component->GetComponentTransform();

			Component->GetDynamicMesh()->ProcessMesh([&Out, &ToWorld, Z0, Z1](const FDynamicMesh3& Mesh)
			{
				for (const int32 Vertex : Mesh.VertexIndicesItr())
				{
					const FVector P = ToWorld.TransformPosition(FVector(Mesh.GetVertex(Vertex)));
					if (P.Z >= Z0 && P.Z <= Z1)
					{
						Out += P;
					}
				}
			});
		};

		Accumulate(Element.GetMeshComponent());

		if (AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(&Element))
		{
			for (const TObjectPtr<UDynamicMeshComponent>& Part : Articulated->GetPartComponents())
			{
				Accumulate(Part.Get());
			}
		}

		return Out;
	}

	/**
	 * The deepest two elements actually interpenetrate, measured in horizontal slices.
	 *
	 * One box each is not enough, for the same reason it is not enough for sanitaryware: a split AC
	 * head reaches the plaster at the back of its casing and reaches its widest at the front of its
	 * bulge, so a single box covers a wedge of room the moulding never occupies. Slicing does not make
	 * the test weaker - a real clash is a real clash in whichever slice it happens in.
	 */
	double SlicedInterpenetration(AHFElementActor& A, AHFElementActor& B)
	{
		const FBox WholeA = WorldBounds(A);
		const FBox WholeB = WorldBounds(B);

		if (Interpenetration(WholeA, WholeB) <= 0.0)
		{
			return Interpenetration(WholeA, WholeB);
		}

		constexpr double Slice = 5.0;

		const double From = FMath::Max(WholeA.Min.Z, WholeB.Min.Z);
		const double To = FMath::Min(WholeA.Max.Z, WholeB.Max.Z);

		double Worst = -1.0;

		for (double Z = From; Z < To; Z += Slice)
		{
			const double Top = FMath::Min(Z + Slice, To);

			const FBox BandA = WorldBoundsInBand(A, Z, Top);
			const FBox BandB = WorldBoundsInBand(B, Z, Top);

			if (!BandA.IsValid || !BandB.IsValid)
			{
				continue;
			}

			const FVector2D Plan(
				FMath::Min(BandA.Max.X, BandB.Max.X) - FMath::Max(BandA.Min.X, BandB.Min.X),
				FMath::Min(BandA.Max.Y, BandB.Max.Y) - FMath::Max(BandA.Min.Y, BandB.Min.Y));

			Worst = FMath::Max(Worst, FMath::Min(Plan.X, Plan.Y));
		}

		return Worst;
	}

	/** One service fixture the reference flat declares, and what it should have come out as. */
	struct FServiceFixture
	{
		const TCHAR* Id;
		const TCHAR* What;
		UClass* Class;

		/** True for a fitting screwed to the plaster, which must land ON the finished face. */
		bool bOnTheWall;
	};

	TArray<FServiceFixture> ServiceFixtures()
	{
		return {
			// The eight sockets. Three heights: 300 general purpose, 1000 for the machine point,
			// 1100 for the two counter-height pairs and the television.
			{ TEXT("F_Soc_Living_TV"), TEXT("The living room's TV point"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_Living_1"), TEXT("The living room's socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_MBed_1"), TEXT("The master bedroom's first socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_MBed_2"), TEXT("The master bedroom's second socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_Bed2_1"), TEXT("Bedroom 2's socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_Kit_1"), TEXT("The kitchen's west counter socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_Kit_2"), TEXT("The kitchen's east counter socket"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Soc_Util"), TEXT("The utility's machine point"),
				AHFAccessoryPlateActor::StaticClass(), true },

			// The five switch plates, all at 1200 beside their room's own door jamb.
			{ TEXT("F_Sw_Living"), TEXT("The living room's 8 gang plate"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Sw_MBed"), TEXT("The master bedroom's 6 gang plate"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Sw_Bed2"), TEXT("Bedroom 2's 6 gang plate"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Sw_Kitchen"), TEXT("The kitchen's 6 gang plate"),
				AHFAccessoryPlateActor::StaticClass(), true },
			{ TEXT("F_Sw_Foyer"), TEXT("The foyer's 4 gang plate"),
				AHFAccessoryPlateActor::StaticClass(), true },

			{ TEXT("F_DB"), TEXT("The consumer unit"),
				AHFDistributionBoardActor::StaticClass(), true },

			// The three split heads, at 2200 with a false ceiling over each of them.
			{ TEXT("F_AC_Living"), TEXT("The living room's AC head"),
				AHFSplitACActor::StaticClass(), true },
			{ TEXT("F_AC_MBed"), TEXT("The master bedroom's AC head"),
				AHFSplitACActor::StaticClass(), true },
			{ TEXT("F_AC_Bed2"), TEXT("Bedroom 2's AC head"),
				AHFSplitACActor::StaticClass(), true },

			// AND THE FOUR THAT STAND ON THE FLOOR, which are deliberately NOT pulled to the face.
			// An appliance is pushed up near a wall, not screwed to it, and all four need air behind
			// them - see AHFCondenserActor.
			{ TEXT("F_ACOut_Living"), TEXT("The living room's condensing unit"),
				AHFCondenserActor::StaticClass(), false },
			{ TEXT("F_ACOut_MBed"), TEXT("The master bedroom's condensing unit"),
				AHFCondenserActor::StaticClass(), false },
			{ TEXT("F_Kitchen_Fridge"), TEXT("The refrigerator"),
				AHFRefrigeratorActor::StaticClass(), false },
			{ TEXT("F_Util_Washer"), TEXT("The washing machine"),
				AHFWashingMachineActor::StaticClass(), false }
		};
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesInTheFlatTest,
	"HouseForge.Editor.TheServicesAreReallyInTheFlat", HF_TEST_FLAGS)

bool FHFServicesInTheFlatTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	int32 Built = 0;

	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (!TestNotNull(*FString::Printf(TEXT("%s is in the level"), Wanted.What), Element))
		{
			continue;
		}

		if (!TestTrue(*FString::Printf(TEXT("%s is the actor it should be"), Wanted.What),
			Element->IsA(Wanted.Class)))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		TestTrue(*FString::Printf(TEXT("%s has geometry"), Wanted.What),
			Bounds.IsValid && Bounds.GetVolume() > 0.0);

		++Built;
	}

	TestEqual(TEXT("All twenty-one service fixtures are built"), Built, 21);

	// AND THE TYPES ARE ON THE SPAWN TABLE, which is what the skirting resolver and the build report
	// both read. A type that builds but is not on the table cuts no skirting; one on the table that
	// does not build cuts a hole with nothing standing in it. See AHFHouseActor::BuildsGeometryFor.
	for (const EHFFixtureType Type : {
		EHFFixtureType::PowerSocket, EHFFixtureType::SwitchPlate, EHFFixtureType::DistributionBoard,
		EHFFixtureType::ACIndoorUnit, EHFFixtureType::ACOutdoorUnit,
		EHFFixtureType::Refrigerator, EHFFixtureType::WashingMachine })
	{
		TestTrue(TEXT("Every service type is on the spawn table"),
			AHFHouseActor::BuildsGeometryFor(Type));
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesOnTheWallTest,
	"HouseForge.Editor.EveryWallFittingLandsOnThePlaster", HF_TEST_FLAGS)

bool FHFServicesOnTheWallTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// ------------------------------------------------------------------------ nothing in the masonry
	//
	// Against EVERY wall in the flat rather than against the one each fitting names: a plate buried in
	// a different wall is the same failure and would pass a check that only looked at its own.

	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		if (Element == nullptr)
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		for (const FHFWall& Wall : Spec.Walls)
		{
			const double Into = Interpenetration(Bounds, SolidOf(Wall));

			// A centimetre of slack: a fitting sits ON the plaster, and the render finish chamfers its
			// back arris to a facet a hair inside the plane it was flush with.
			TestTrue(*FString::Printf(TEXT("%s is not buried in '%s' (%.1f cm in)"),
				Wanted.What, *Wall.Id.ToString(), Into), Into <= 1.0);
		}
	}

	// --------------------------------------------------------------- and the thirteen are on the face
	//
	// THE ONE MEASUREMENT THIS WHOLE GROUP TURNS ON. A socket that hangs 47 mm off the plaster and one
	// that sits on it are indistinguishable in plan, in the spec and in every bench test - and there
	// are thirteen of them at eye level. The same defect with the opposite sign put both geysers a
	// quarter of the way inside a partition.

	int32 OnTheFace = 0;

	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		if (!Wanted.bOnTheWall)
		{
			continue;
		}

		const FHFFixture* Fixture = FixtureFor(Spec, FName(Wanted.Id));
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (Fixture == nullptr || Element == nullptr)
		{
			continue;
		}

		const FHFWall* Anchor = Spec.FindWall(Fixture->AnchorWallId);
		if (!TestNotNull(*FString::Printf(TEXT("%s names the wall it is fixed to"), Wanted.What), Anchor))
		{
			continue;
		}

		const double Correction = FHFFixturePlacement::WallFaceCorrection(*Fixture, Anchor);

		const FBox Bounds = WorldBounds(*Element);
		const FBox WallSolid = SolidOf(*Anchor);

		const bool bRunsEastWest =
			FMath::Abs(Anchor->End.Y - Anchor->Start.Y) < FMath::Abs(Anchor->End.X - Anchor->Start.X);

		const bool bBeyond = bRunsEastWest
			? Fixture->Position.Y > Anchor->Start.Y
			: Fixture->Position.X > Anchor->Start.X;

		const double FittingBack = bRunsEastWest
			? (bBeyond ? Bounds.Min.Y : Bounds.Max.Y)
			: (bBeyond ? Bounds.Min.X : Bounds.Max.X);

		const double WallFace = bRunsEastWest
			? (bBeyond ? WallSolid.Max.Y : WallSolid.Min.Y)
			: (bBeyond ? WallSolid.Max.X : WallSolid.Min.X);

		const double Gap = FMath::Abs(FittingBack - WallFace);

		// AND THE OTHER SIDE OF IT: HOW FAR PROUD. The drawn depth is the whole build-up, so a plate
		// that came out standing further off the wall than the drawing says would be wrong in thirteen
		// places at once - and it is the dimension nothing else in the flat constrains.
		const double FittingFront = bRunsEastWest
			? (bBeyond ? Bounds.Max.Y : Bounds.Min.Y)
			: (bBeyond ? Bounds.Max.X : Bounds.Min.X);

		const double Proud = FMath::Abs(FittingFront - WallFace);

		AddInfo(FString::Printf(
			TEXT("%s: the drawing put it %.1f cm off the face; built, its back is %.2f cm from it and it stands %.2f cm proud against a drawn %.2f."),
			Wanted.What, Correction, Gap, Proud, Fixture->Footprint.Y));

		TestTrue(*FString::Printf(TEXT("%s touches the plaster it is screwed to (%.2f cm)"),
			Wanted.What, Gap), Gap <= 1.0);

		TestTrue(*FString::Printf(
			TEXT("%s stands off the plaster by its drawn depth (%.2f cm against %.2f)"),
			Wanted.What, Proud, Fixture->Footprint.Y),
			Proud <= Fixture->Footprint.Y + 1.0);

		++OnTheFace;
	}

	TestEqual(TEXT("All seventeen wall-fixed services were measured"), OnTheFace, 17);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesClearanceTest,
	"HouseForge.Editor.NoServiceFixtureStandsInTheBuilding", HF_TEST_FLAGS)

bool FHFServicesClearanceTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// ------------------------------------------------------------------- not standing in a doorway
	//
	// A fitting in a door opening is caught by a validator rule and the reference flat is clean; this
	// is the same question asked of what was actually BUILT, which is not the same object. A split AC
	// head is 220 mm deep and sits at 2200 - above every door head in the flat but not above the
	// balcony sliders, and a plate beside a jamb is a hand's width from the leaf's swing.

	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		if (Element == nullptr)
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		for (const FHFOpening& Opening : Spec.Openings)
		{
			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr)
			{
				continue;
			}

			const double Into = Interpenetration(Bounds, ClearOpeningOf(Opening, *Wall));

			TestTrue(*FString::Printf(TEXT("%s does not stand in '%s' (%.1f cm in)"),
				Wanted.What, *Opening.Id.ToString(), Into), Into <= 0.0);
		}
	}

	// ------------------------------------------------------------------------ nor in the structure

	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		if (Element == nullptr)
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		for (const FHFColumn& Column : Spec.Columns)
		{
			const double Radians = FMath::DegreesToRadians(Column.RotationDegrees);
			const double C = FMath::Abs(FMath::Cos(Radians));
			const double S = FMath::Abs(FMath::Sin(Radians));

			const FVector2D Half(
				(Column.Size.X * C + Column.Size.Y * S) * 0.5,
				(Column.Size.X * S + Column.Size.Y * C) * 0.5);

			FBox Solid(ForceInit);
			Solid += FVector(Column.Position.X - Half.X, Column.Position.Y - Half.Y, 0.0);
			Solid += FVector(Column.Position.X + Half.X, Column.Position.Y + Half.Y, Column.Height);

			const double Into = Interpenetration(Bounds, Solid);

			TestTrue(*FString::Printf(TEXT("%s does not stand in column '%s' (%.1f cm in)"),
				Wanted.What, *Column.Id.ToString(), Into), Into <= 0.5);
		}

		for (const FHFBeam& Beam : Spec.Beams)
		{
			const FVector2D Along = (Beam.End - Beam.Start).GetSafeNormal();
			const FVector2D Across(-Along.Y, Along.X);
			const FVector2D Half = Across * (Beam.Width * 0.5);

			FBox Solid(ForceInit);
			Solid += FVector(Beam.Start.X + Half.X, Beam.Start.Y + Half.Y,
				Beam.SoffitZ - Beam.Depth);
			Solid += FVector(Beam.Start.X - Half.X, Beam.Start.Y - Half.Y, Beam.SoffitZ);
			Solid += FVector(Beam.End.X + Half.X, Beam.End.Y + Half.Y, Beam.SoffitZ - Beam.Depth);
			Solid += FVector(Beam.End.X - Half.X, Beam.End.Y - Half.Y, Beam.SoffitZ);

			const double Into = Interpenetration(Bounds, Solid);

			TestTrue(*FString::Printf(TEXT("%s does not run into beam '%s' (%.1f cm in)"),
				Wanted.What, *Beam.Id.ToString(), Into), Into <= 0.5);
		}
	}

	// ----------------------------------------------------------------- nor into any other fixture
	//
	// Against everything the house builds, not just against each other. The AC head over the master
	// bedroom's wardrobe run and the fridge beside the kitchen's base units are both cases where two
	// perfectly correct fittings can be in the same place.

	TArray<AHFElementActor*> Services;
	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		if (AHFElementActor* Element = ElementFor(House, FName(Wanted.Id)))
		{
			Services.Add(Element);
		}
	}

	for (int32 Index = 0; Index < Services.Num(); ++Index)
	{
		AHFElementActor* Service = Services[Index];

		for (const FHFFixture& Other : Spec.Fixtures)
		{
			if (Other.Id == Service->ElementId)
			{
				continue;
			}

			AHFElementActor* OtherElement = ElementFor(House, Other.Id);
			if (OtherElement == nullptr)
			{
				continue;
			}

			const double Into = SlicedInterpenetration(*Service, *OtherElement);

			TestTrue(*FString::Printf(TEXT("'%s' does not foul '%s' (%.1f cm in)"),
				*Service->ElementId.ToString(), *Other.Id.ToString(), Into), Into <= 1.0);
		}
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesCeilingTest,
	"HouseForge.Editor.TheACHeadsClearTheirCeilings", HF_TEST_FLAGS)

bool FHFServicesCeilingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// THE CLIPPING THE USER REPORTED FOR THE FANS, ASKED OF THE THREE AC HEADS. A split head hangs at
	// 2200 with its top at 2500 in three rooms that all have a false ceiling, and the ceiling's depth
	// is a project setting rather than a fact of the drawing - so "it clears today" is not the same
	// statement as "it answers to the ceiling".

	const TArray<FHFFixture> Fitted = House->FittedFixtures();

	int32 Measured = 0;

	for (const FHFFixture& Fixture : Fitted)
	{
		if (Fixture.Type != EHFFixtureType::ACIndoorUnit)
		{
			continue;
		}

		const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
		if (Room == nullptr)
		{
			continue;
		}

		AHFElementActor* Element = ElementFor(House, Fixture.Id);
		if (Element == nullptr)
		{
			continue;
		}

		const double SoffitZ = FHFCeilingFit::LowestSoffitZOver(Fixture, *Room, Spec.FalseCeilings);
		const FBox Bounds = WorldBounds(*Element);

		AddInfo(FString::Printf(
			TEXT("'%s' tops out at %.1f under a soffit at %.1f - %.1f cm clear."),
			*Fixture.Id.ToString(), Bounds.Max.Z, SoffitZ, SoffitZ - Bounds.Max.Z));

		TestTrue(*FString::Printf(TEXT("'%s' clears the finished soffit over it (%.1f cm)"),
			*Fixture.Id.ToString(), SoffitZ - Bounds.Max.Z), Bounds.Max.Z <= SoffitZ);

		// AND ITS BUILT HEIGHT IS THE HEIGHT THE FIT WAS GIVEN. The mechanism takes a built envelope
		// rather than the drawn box, and a casing that quietly grew past its drawn height would leave
		// the difference inside the plasterboard - which is precisely what the extract's bezel did.
		TestEqual(*FString::Printf(TEXT("'%s' is built the height the fit was told about"),
			*Fixture.Id.ToString()),
			Bounds.Max.Z - Bounds.Min.Z, AHFSplitACActor::ParamsFor(Fixture).BuiltHeight(), 0.05);

		++Measured;
	}

	TestEqual(TEXT("All three AC heads were measured"), Measured, 3);

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesSkirtingTest,
	"HouseForge.Editor.TheSkirtingRunsBehindTheAppliances", HF_TEST_FLAGS)

bool FHFServicesSkirtingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// NEITHER APPLIANCE IS SCRIBED JOINERY, so neither may cut a break in a board. A break with nothing
	// standing in it is a length of missing skirting: bare plaster meeting bare floor for the width of
	// a unit, which is the failure FHFSkirting::IsScribedJoinery exists on both sides of.
	TestFalse(TEXT("A refrigerator does not cut the skirting"),
		FHFSkirting::IsScribedJoinery(EHFFixtureType::Refrigerator));
	TestFalse(TEXT("A washing machine does not cut the skirting"),
		FHFSkirting::IsScribedJoinery(EHFFixtureType::WashingMachine));
	TestFalse(TEXT("A condensing unit does not cut the skirting"),
		FHFSkirting::IsScribedJoinery(EHFFixtureType::ACOutdoorUnit));

	// AND THEREFORE BOTH HAVE TO STAND IN FRONT OF THE BOARD. The washing machine is drawn with its
	// back EXACTLY on W_North's plaster and the refrigerator 2.5 mm off W_Mid_Upper's, so without the
	// setback both would be permanently inside an 18 mm skirting board - invisible in plan, invisible
	// in every test of the appliance alone, and precisely the defect a study table was found with.

	const double SkirtingDepth = FHFBuildDefaults::FromProjectSettings().Skirting.Depth;

	struct FStanding
	{
		const TCHAR* Id;
		const TCHAR* What;
	};

	for (const FStanding& Standing : { FStanding{ TEXT("F_Kitchen_Fridge"), TEXT("The refrigerator") },
		FStanding{ TEXT("F_Util_Washer"), TEXT("The washing machine") } })
	{
		const FHFFixture* Fixture = FixtureFor(Spec, FName(Standing.Id));
		AHFElementActor* Element = ElementFor(House, FName(Standing.Id));

		if (Fixture == nullptr || Element == nullptr)
		{
			AddError(FString::Printf(TEXT("%s is missing."), Standing.What));
			continue;
		}

		const FHFWall* Anchor = Spec.FindWall(Fixture->AnchorWallId);
		if (!TestNotNull(*FString::Printf(TEXT("%s names its wall"), Standing.What), Anchor))
		{
			continue;
		}

		// Only the FIXED geometry: an open door is not what stands against a skirting board.
		const FBox Bounds = ShellBounds(*Element);
		const FBox WallSolid = SolidOf(*Anchor);

		const bool bRunsEastWest =
			FMath::Abs(Anchor->End.Y - Anchor->Start.Y) < FMath::Abs(Anchor->End.X - Anchor->Start.X);

		const bool bBeyond = bRunsEastWest
			? Fixture->Position.Y > Anchor->Start.Y
			: Fixture->Position.X > Anchor->Start.X;

		const double ApplianceBack = bRunsEastWest
			? (bBeyond ? Bounds.Min.Y : Bounds.Max.Y)
			: (bBeyond ? Bounds.Min.X : Bounds.Max.X);

		const double WallFace = bRunsEastWest
			? (bBeyond ? WallSolid.Max.Y : WallSolid.Min.Y)
			: (bBeyond ? WallSolid.Max.X : WallSolid.Min.X);

		const double Gap = FMath::Abs(ApplianceBack - WallFace);

		AddInfo(FString::Printf(
			TEXT("%s stands %.2f cm off the plaster, against an %.1f cm skirting board."),
			Standing.What, Gap, SkirtingDepth));

		TestTrue(*FString::Printf(
			TEXT("%s stands clear of the skirting board (%.2f cm off a %.1f cm board)"),
			Standing.What, Gap, SkirtingDepth), Gap >= SkirtingDepth);

		// AND NOT SO FAR CLEAR THAT IT IS IN THE MIDDLE OF THE ROOM. The setback is a couple of
		// centimetres; anything more is an appliance somebody has pulled out.
		TestTrue(*FString::Printf(TEXT("%s is still against its wall (%.2f cm off)"),
			Standing.What, Gap), Gap <= SkirtingDepth + 3.0);
	}

	// ------------------------------------------------------- and the board did not change under them
	//
	// SEVEN TYPES JOINED THE BUILT SET THIS MILESTONE and the skirting resolver reads that set. A
	// scribed type entering it cuts a new break in a board; a type that should not cut and does takes
	// a length of skirting away with nothing standing in the hole.
	//
	// Asked as a difference rather than as an absolute figure: the plan is resolved with the services
	// in the built set and again with them taken out of it, and the two must be identical in every
	// room. That is exactly the statement "none of these cuts the skirting", and unlike a covered
	// percentage it cannot drift as the flat gains furniture.

	const FHFSkirtingParams SkirtingParams = FHFBuildDefaults::FromProjectSettings().Skirting;
	const TArray<FHFFixture> Fitted = House->FittedFixtures();

	const TSet<FName> WithServices = AHFHouseActor::BuiltFixtureIds(Fitted);

	TSet<FName> WithoutServices = WithServices;
	for (const FServiceFixture& Wanted : ServiceFixtures())
	{
		WithoutServices.Remove(FName(Wanted.Id));
	}

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan With = FHFSkirting::For(Room, Spec.Walls, Spec.Openings, Spec.Columns,
			Fitted, SkirtingParams, &WithServices);
		const FHFSkirtingPlan Without = FHFSkirting::For(Room, Spec.Walls, Spec.Openings, Spec.Columns,
			Fitted, SkirtingParams, &WithoutServices);

		TestEqual(*FString::Printf(
			TEXT("'%s' has exactly as much skirting with the services as without them"),
			*Room.Id.ToString()),
			With.CoveredLength(), Without.CoveredLength(), 0.01);

		// And there is still a board there at all. A room whose skirting had collapsed entirely would
		// pass the comparison above by being equally broken both ways.
		//
		// A THIRD, NOT A HALF, and the foyer is why: it is 38% covered, and correctly so. It is a
		// 1800 x 1800 lobby with the front door in one wall, the kitchen doorway in another and the
		// common bathroom's in a third, plus a shoe rack scribed across the fourth - there is very
		// little of its boundary that is not an opening or a carcass. Setting the bar where the
		// best-behaved room sits would be a test that only says the flat has not changed.
		TestTrue(*FString::Printf(TEXT("'%s' still has a skirting board (%.0f%% of its boundary)"),
			*Room.Id.ToString(), 100.0 * With.CoveredLength() / FMath::Max(With.BoundaryLength(), 1.0)),
			With.CoveredLength() > With.BoundaryLength() * 0.3);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFFridgeDoorTest,
	"HouseForge.Editor.TheFridgeDoorsCanActuallyBeOpened", HF_TEST_FLAGS)

bool FHFFridgeDoorTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeServices;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	FHFHouseSpec Spec;
	AHFHouseActor* House = BuildReferenceFlat(World, Spec);
	if (!TestNotNull(TEXT("The reference flat builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ if (IsValid(House)) { House->ClearGeometry(); House->Destroy(); } };

	// A DOOR NEEDS SOMEWHERE TO SWING, and only this layer can see whether it has one. A refrigerator
	// whose leaves sweep their whole 110 degrees into a run of base units satisfies every assertion in
	// the kit tests and cannot be opened - which is the wardrobe's cancelling-leaf failure in a
	// different fitting, and the same failure the drawer-obstruction probe exists for.

	AHFArticulatedActor* Fridge =
		Cast<AHFArticulatedActor>(ElementFor(House, TEXT("F_Kitchen_Fridge")));

	if (!TestNotNull(TEXT("The refrigerator is in the flat"), Fridge))
	{
		return false;
	}

	const FBox Shut = WorldBounds(*Fridge);

	Fridge->SetAllPartsOpenAmount(1.0);

	const FBox Open = WorldBounds(*Fridge);

	// The doors really come out into the room, and by a door's own width rather than by a token.
	const FVector Grew = Open.GetSize() - Shut.GetSize();

	AddInfo(FString::Printf(TEXT("Opened, the refrigerator's envelope grows by (%.1f, %.1f, %.1f) cm."),
		Grew.X, Grew.Y, Grew.Z));

	TestTrue(FString::Printf(TEXT("The leaves swing out into the kitchen (%.1f cm)"),
		FMath::Max(Grew.X, Grew.Y)),
		FMath::Max(Grew.X, Grew.Y) > 50.0);

	// ------------------------------------------------------------- AND THERE IS ROOM FOR THEM TO DO IT
	//
	// Against everything else the flat builds on the kitchen floor. The open leaves are 700 mm of
	// swinging door in a room with a run of base units, a counter and a hob in it.

	for (const FHFFixture& Other : Spec.Fixtures)
	{
		if (Other.Id == Fridge->ElementId || Other.RoomId != TEXT("R_Kitchen"))
		{
			continue;
		}

		AHFElementActor* OtherElement = ElementFor(House, Other.Id);
		if (OtherElement == nullptr)
		{
			continue;
		}

		const double Into = SlicedInterpenetration(*Fridge, *OtherElement);

		TestTrue(*FString::Printf(TEXT("The open refrigerator does not foul '%s' (%.1f cm in)"),
			*Other.Id.ToString(), Into), Into <= 1.0);
	}

	// And not into the masonry either. A leaf that swings through a wall is the commonest way for a
	// door to be hung on the wrong stile, and it looks perfectly correct shut.
	for (const FHFWall& Wall : Spec.Walls)
	{
		const double Into = Interpenetration(Open, SolidOf(Wall));

		TestTrue(*FString::Printf(TEXT("The open refrigerator does not swing into '%s' (%.1f cm in)"),
			*Wall.Id.ToString(), Into), Into <= 1.0);
	}

	Fridge->SetAllPartsOpenAmount(0.0);

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
