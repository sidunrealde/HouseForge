// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFCasedGoodsActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFFurnitureActors.h"
#include "Actors/HFHouseActor.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSkirtingPlan.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The bedroom group, in the flat rather than on a bench.
 *
 * EVERY DEFECT THIS PROJECT HAS FOUND WAS FOUND BY LOOKING AT THE BUILT FLAT while the kit tests
 * passed, and this group produced four more of exactly that kind: two TV units set out from a wall's
 * centreline instead of its face, a shoe rack 32.5 mm inside the masonry down its whole length, a
 * headboard 185 mm off the wall behind it, and a socket buried in the back of the console it feeds.
 * Not one of them is visible in a parameter struct, and not one of them would have been caught by
 * anything that builds a fixture on its own and measures it.
 *
 * So these tests build the reference flat and ask the questions a person asks standing in the room:
 * is it in the wall, is it in the doorway, is it in something else, and does the skirting still
 * work round it.
 */
namespace HouseForgeBedroom
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
		// Taking FHFSampleHouse::Make2BHK() again here would hand back millimetres and every
		// comparison below would be out by a factor of ten while still looking like a number.
		OutSpec = House->Spec;
		return House;
	}

	/** The element actor the house built for a spec id, or null. */
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

	/**
	 * Everything an element occupies in the world, moving parts included, in their CLOSED pose.
	 *
	 * Off the vertices rather than off the component's own Bounds, because a scene proxy's bounds are
	 * padded and this is being compared against masonry at millimetre tolerances.
	 */
	FBox WorldBounds(AHFElementActor& Element)
	{
		FBox Out(ForceInit);

		auto Accumulate = [&Out](UDynamicMeshComponent* Component)
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

	/** The same, restricted to a band of world Z - what an element occupies at skirting height. */
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

	/** How far two boxes interpenetrate on their least-overlapping axis. Zero or less means clear. */
	double Interpenetration(const FBox& A, const FBox& B)
	{
		const FVector Overlap(
			FMath::Min(A.Max.X, B.Max.X) - FMath::Max(A.Min.X, B.Min.X),
			FMath::Min(A.Max.Y, B.Max.Y) - FMath::Max(A.Min.Y, B.Min.Y),
			FMath::Min(A.Max.Z, B.Max.Z) - FMath::Max(A.Min.Z, B.Min.Z));

		return FMath::Min3(Overlap.X, Overlap.Y, Overlap.Z);
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

	/** The eight fixtures this group builds, and what each one is. */
	struct FBedroomFixture
	{
		const TCHAR* Id;
		const TCHAR* What;

		/** True for a run scribed to the plaster, whose skirting is cut out and which stands flush. */
		bool bScribed;
	};

	const TArray<FBedroomFixture>& BedroomFixtures()
	{
		static const TArray<FBedroomFixture> Fixtures = {
			{ TEXT("F_MBed_Bed"),     TEXT("the king bed"),         false },
			{ TEXT("F_Bed2_Bed"),     TEXT("the queen bed"),        false },
			{ TEXT("F_MBed_Night1"),  TEXT("the west nightstand"),  false },
			{ TEXT("F_MBed_Night2"),  TEXT("the east nightstand"),  false },
			{ TEXT("F_TVUnit_W"),     TEXT("the TV storage column"), true },
			{ TEXT("F_TVUnit_E"),     TEXT("the TV console"),        true },
			{ TEXT("F_Bed2_Study"),   TEXT("the study table"),      false },
			{ TEXT("F_ShoeRack"),     TEXT("the shoe rack"),         true },
		};

		return Fixtures;
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedroomBuildsTest,
	"HouseForge.Editor.TheBedroomFixturesAreReallyInTheFlat", HF_TEST_FLAGS)

bool FHFBedroomBuildsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeBedroom;

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

	for (const FBedroomFixture& Wanted : BedroomFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (!TestNotNull(*FString::Printf(TEXT("%s was built"), Wanted.What), Element))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		TestTrue(*FString::Printf(TEXT("%s has geometry in it"), Wanted.What),
			Bounds.IsValid != 0 && Bounds.GetSize().GetMin() > 1.0);

		// AND IT IS WHERE THE DRAWING PUT IT. A fixture built at the origin is a fixture that looks
		// perfect in every screenshot of itself and is standing in the foyer.
		const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
			[&Wanted](const FHFFixture& F) { return F.Id == FName(Wanted.Id); });

		if (Fixture == nullptr)
		{
			AddError(FString::Printf(TEXT("The flat has no fixture '%s'."), Wanted.Id));
			continue;
		}

		const FVector2D Centre(Bounds.GetCenter().X, Bounds.GetCenter().Y);

		// Generous, because the built object is not the drawn box in plan either - a drawer front
		// hangs in front of the carcass and a mattress oversails its frame - but nothing like loose
		// enough to accept a fixture in the wrong room.
		TestTrue(*FString::Printf(TEXT("%s stands where it was drawn"), Wanted.What),
			FVector2D::Distance(Centre, Fixture->Position) < 20.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedroomClearanceTest,
	"HouseForge.Editor.NoBedroomFixtureStandsInTheBuilding", HF_TEST_FLAGS)

bool FHFBedroomClearanceTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeBedroom;

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

	// Contact is not interpenetration. Fitted joinery is SCRIBED to the plaster - its back is meant
	// to touch the wall face - so what is being caught here is a run standing inside the masonry, and
	// the shoe rack was 3.25 cm inside it.
	constexpr double ContactTolerance = 0.5;

	TArray<TPair<const TCHAR*, FBox>> Built;

	for (const FBedroomFixture& Wanted : BedroomFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		if (Element == nullptr)
		{
			AddError(FString::Printf(TEXT("%s was not built at all."), Wanted.What));
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);
		Built.Emplace(Wanted.What, Bounds);

		// ------------------------------------------------------------------------ not in a wall

		for (const FHFWall& Wall : Spec.Walls)
		{
			const double Into = Interpenetration(Bounds, SolidOf(Wall));

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(
					TEXT("%s stands %.1f cm inside wall '%s'. Fitted joinery is scribed to the plaster, not built into it - check the fixture is set out from the wall's FACE rather than its centreline."),
					Wanted.What, Into, *Wall.Id.ToString()));
			}
		}

		// --------------------------------------------------------------- and not in a door opening
		//
		// A doorway is somewhere somebody walks. The validator asks this of the drawn boxes; this asks
		// it of the geometry that was really built, which is not the same question - a drawer front
		// stands 20 mm in front of the box it belongs to, and a mattress oversails its frame.

		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.Kind != EHFOpeningKind::Door && Opening.Kind != EHFOpeningKind::SlidingDoor)
			{
				continue;
			}

			const FHFWall* Wall = Spec.FindWall(Opening.WallId);
			if (Wall == nullptr)
			{
				continue;
			}

			const double Into = Interpenetration(Bounds, ClearOpeningOf(Opening, *Wall));

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(
					TEXT("%s stands %.1f cm inside the clear opening of '%s'. Nobody walks through furniture."),
					Wanted.What, Into, *Opening.Id.ToString()));
			}
		}
	}

	// ------------------------------------------------------------------- and not in each other
	//
	// Bounds rather than meshes, which is the conservative direction: two boxes that do not overlap
	// certainly do not collide. It is enough to catch the thing that actually happens, which is a
	// nightstand set out beside a bed on a figure that was right before the bed moved.

	for (int32 A = 0; A < Built.Num(); ++A)
	{
		for (int32 B = A + 1; B < Built.Num(); ++B)
		{
			const double Into = Interpenetration(Built[A].Value, Built[B].Value);

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(TEXT("%s and %s occupy the same %.1f cm of the flat."),
					Built[A].Key, Built[B].Key, Into));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d bedroom fixtures checked against %d walls, %d openings and each other."),
		Built.Num(), Spec.Walls.Num(), Spec.Openings.Num()));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedroomSkirtingTest,
	"HouseForge.Editor.TheSkirtingStillWorksRoundTheBedroomFixtures", HF_TEST_FLAGS)

bool FHFBedroomSkirtingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeBedroom;

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

	const FHFSkirtingParams SkirtingParams = FHFBuildDefaults::FromProjectSettings().Skirting;

	AddInfo(FString::Printf(TEXT("Skirting section is %.1f cm deep."), SkirtingParams.Depth));

	// THE TWO HALVES OF THE SAME DECISION, and each of them looks right without the other.
	//
	// A SCRIBED run - a TV unit, a shoe rack - has the board CUT OUT for it, so it must stand hard
	// against the plaster. Left 60 mm off the wall, as both TV units were, the cut leaves a slot of
	// bare plaster meeting bare floor behind 2100 mm of joinery, in the one place in a living room
	// the eye is aimed at all evening.
	//
	// An UNSCRIBED one - a bed, a nightstand, a desk - has the board running on BEHIND it, so it must
	// stand clear of the board instead. Pushed flat against the plaster it is 18 mm inside the
	// skirting, permanently, in every room, and nothing that measures the fixture on its own can see
	// it. That is the whole reason FHFDeskParams::SupportSetback exists.

	for (const FBedroomFixture& Wanted : BedroomFixtures())
	{
		const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
			[&Wanted](const FHFFixture& F) { return F.Id == FName(Wanted.Id); });

		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (Fixture == nullptr || Element == nullptr || Fixture->AnchorWallId.IsNone())
		{
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Fixture->AnchorWallId);
		const FHFRoom* Room = Spec.FindRoom(Fixture->RoomId);
		if (Wall == nullptr || Room == nullptr)
		{
			continue;
		}

		// The wall's face on this fixture's side, as a signed distance along the wall's own normal.
		const FVector2D Along = (Wall->End - Wall->Start).GetSafeNormal();
		const FVector2D Normal(-Along.Y, Along.X);

		const double CentreOffset = FVector2D::DotProduct(Wall->Start, Normal);
		const double FixtureSide =
			FMath::Sign(FVector2D::DotProduct(Fixture->Position, Normal) - CentreOffset);

		const double FaceOffset = CentreOffset + FixtureSide * Wall->Thickness * 0.5;

		// What the fixture reaches AT OR BELOW SKIRTING HEIGHT, which is the only band where any of
		// this matters: everything above the board's top passes over it freely.
		//
		// Below, and not "within a band". These solids are boxes and a box has vertices only at its
		// extremes, so a plinth running from the floor to 8 cm has nothing at all between 1 and 5.4 -
		// a band that narrow found geometry on the desk and on nothing else, and reported six
		// fixtures clear by saying nothing about them.
		const FBox Band = WorldBoundsInBand(*Element, Room->FloorZ - 1.0,
			Room->FloorZ + FMath::Max(Room->SkirtingHeight, 10.0));

		if (Band.IsValid == 0)
		{
			// Nothing at skirting height at all - a wall-hung unit. Not this test's business.
			continue;
		}

		// How far the fixture's nearest geometry is from the wall face, positive when clear of it.
		const FVector2D Corners[4] = {
			FVector2D(Band.Min.X, Band.Min.Y), FVector2D(Band.Max.X, Band.Min.Y),
			FVector2D(Band.Min.X, Band.Max.Y), FVector2D(Band.Max.X, Band.Max.Y) };

		double Nearest = TNumericLimits<double>::Max();
		for (const FVector2D& Corner : Corners)
		{
			Nearest = FMath::Min(Nearest,
				FixtureSide * (FVector2D::DotProduct(Corner, Normal) - FaceOffset));
		}

		AddInfo(FString::Printf(TEXT("%s: %.2f cm from the face of '%s' at skirting height."),
			Wanted.What, Nearest, *Wall->Id.ToString()));

		if (Wanted.bScribed)
		{
			// Flush. The board is cut for it, so anything more than a scribe joint is a visible slot
			// with no skirting in it.
			TestTrue(*FString::Printf(
				TEXT("%s is scribed to the wall, and its skirting break is paid for"), Wanted.What),
				Nearest < 1.0);
			TestTrue(*FString::Printf(TEXT("%s is not built into the wall either"), Wanted.What),
				Nearest > -0.5);
		}
		else
		{
			// Clear of the board that runs behind it. The tolerance is the section's own depth: it
			// either stands in front of the skirting or it stands in it.
			TestTrue(*FString::Printf(
				TEXT("%s is not scribed, so it stands clear of the board running behind it"), Wanted.What),
				Nearest >= SkirtingParams.Depth - 0.05);
		}
	}

	// And the room plans themselves still balance, with the new runs cutting into them.
	const TArray<FHFFixture> Fitted = House->FittedFixtures();
	const TSet<FName> BuiltIds = AHFHouseActor::BuiltFixtureIds(Fitted);

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan Plan = FHFSkirting::For(Room, Spec.Walls, Spec.Openings, Spec.Columns,
			Fitted, SkirtingParams, &BuiltIds);

		TestNearlyEqual(*FString::Printf(TEXT("%s: boundary is still skirting plus gaps"),
			*Room.Id.ToString()), Plan.CoveredLength() + Plan.BreakLength(), Plan.BoundaryLength(), 0.01);

		for (const FHFSkirtingBreak& Break : Plan.Breaks)
		{
			if (Break.Cause == EHFSkirtingBreakCause::Joinery)
			{
				TestTrue(*FString::Printf(TEXT("%s: the gap for '%s' has a carcass standing in it"),
					*Room.Id.ToString(), *Break.SourceId.ToString()), BuiltIds.Contains(Break.SourceId));
			}
		}
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
