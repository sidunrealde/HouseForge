// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFLooseFurnitureActors.h"
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
 * The loose furniture, in the flat rather than on a bench.
 *
 * EVERY DEFECT THIS PROJECT HAS FOUND WAS FOUND BY LOOKING AT THE BUILT FLAT while the kit tests
 * passed, and this group produced two more of the same kind: a three-seater set out from
 * W_Mid_Lower's centreline rather than its face, standing 192.5 mm off the plaster, and a dining
 * table 250 mm from the front of that sofa - so a four-seater whose north side nobody could sit at
 * and a pinch nobody could walk through. Neither is visible in a parameter struct.
 *
 * The one below that matters most is FHFLoosePullOutTest. Nothing in this group articulates, so
 * there is no swept transform to assert; what there is instead is a chair, which is MOVED. "Does it
 * move" is the wrong question about a chair and it is exactly the question the wardrobe's two
 * cancelling leaves passed. "Where does it end up, and what is standing there" is the right one, and
 * it is asked here of the geometry that was really built rather than of the drawn boxes.
 */
namespace HouseForgeLoose
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

	/** Everything an element occupies in the world, off its vertices rather than its proxy bounds. */
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
	FBox WorldBoundsBelow(AHFElementActor& Element, double Z0, double Z1)
	{
		FBox Out(ForceInit);

		UDynamicMeshComponent* Component = Element.GetMeshComponent();
		if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
		{
			return Out;
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

	struct FLooseFixture
	{
		const TCHAR* Id;
		const TCHAR* What;
		bool bIsChair;
	};

	/** One built fixture, kept for the pairwise check below. */
	struct FBuiltFixture
	{
		const TCHAR* What = nullptr;
		bool bIsChair = false;
		bool bIsDiningTable = false;
		FBox Bounds = FBox(ForceInit);
	};

	const TArray<FLooseFixture>& LooseFixtures()
	{
		static const TArray<FLooseFixture> Fixtures = {
			{ TEXT("F_Sofa"),        TEXT("the three-seater"), false },
			{ TEXT("F_CoffeeTable"), TEXT("the coffee table"), false },
			{ TEXT("F_DiningTable"), TEXT("the dining table"), false },
			{ TEXT("F_Chair_D1"),    TEXT("the north-west chair"), true },
			{ TEXT("F_Chair_D2"),    TEXT("the north-east chair"), true },
			{ TEXT("F_Chair_D3"),    TEXT("the west end chair"), true },
			{ TEXT("F_Chair_D4"),    TEXT("the east end chair"), true },
		};

		return Fixtures;
	}

	/** How far back somebody pulls a chair to sit down. Not the 15 cm it is tucked under by. */
	constexpr double PullOut = 35.0;

	/** Contact is not interpenetration; a scribed back is meant to touch the plaster. */
	constexpr double ContactTolerance = 0.5;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLooseBuildsTest,
	"HouseForge.Editor.TheLooseFurnitureIsReallyInTheFlat", HF_TEST_FLAGS)

bool FHFLooseBuildsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLoose;

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

	for (const FLooseFixture& Wanted : LooseFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (!TestNotNull(*FString::Printf(TEXT("%s was built"), Wanted.What), Element))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		TestTrue(*FString::Printf(TEXT("%s has geometry in it"), Wanted.What),
			Bounds.IsValid != 0 && Bounds.GetSize().GetMin() > 1.0);

		// AND IT IS WHERE THE DRAWING PUT IT. A fixture built at the origin looks perfect in every
		// screenshot of itself and is standing in the foyer.
		const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
			[&Wanted](const FHFFixture& F) { return F.Id == FName(Wanted.Id); });

		if (Fixture == nullptr)
		{
			AddError(FString::Printf(TEXT("The flat has no fixture '%s'."), Wanted.Id));
			continue;
		}

		const FVector2D Centre(Bounds.GetCenter().X, Bounds.GetCenter().Y);
		TestTrue(*FString::Printf(TEXT("%s stands where it was drawn"), Wanted.What),
			FVector2D::Distance(Centre, Fixture->Position) < 20.0);

		// The drawn height is the built height for all of these - a sofa's back, a table's top and a
		// chair's stiles are the tallest thing on each - which is what makes it right for none of them
		// to supply a built envelope to FHFCeilingFit. Worth asserting rather than assuming: a bed
		// drawn 600 stands 1050, and that was found the same way.
		TestTrue(*FString::Printf(TEXT("%s stands as tall as it was drawn (%.1f of %.1f)"),
			Wanted.What, Bounds.Max.Z - Bounds.Min.Z, Fixture->Height),
			FMath::IsNearlyEqual(Bounds.Max.Z - Bounds.Min.Z, Fixture->Height, 0.5));
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLooseClearanceTest,
	"HouseForge.Editor.NoLooseFixtureStandsInTheBuilding", HF_TEST_FLAGS)

bool FHFLooseClearanceTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLoose;

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

	TArray<FBuiltFixture> Built;

	for (const FLooseFixture& Wanted : LooseFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		if (Element == nullptr)
		{
			AddError(FString::Printf(TEXT("%s was not built at all."), Wanted.What));
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		FBuiltFixture Row;
		Row.What = Wanted.What;
		Row.bIsChair = Wanted.bIsChair;
		Row.bIsDiningTable = FCString::Strcmp(Wanted.Id, TEXT("F_DiningTable")) == 0;
		Row.Bounds = Bounds;
		Built.Add(Row);

		// ------------------------------------------------------------------------ not in a wall
		for (const FHFWall& Wall : Spec.Walls)
		{
			const double Into = Interpenetration(Bounds, SolidOf(Wall));

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(
					TEXT("%s stands %.1f cm inside wall '%s'. Loose furniture goes against the plaster, not into it - check the fixture is set out from the wall's FACE rather than its centreline."),
					Wanted.What, Into, *Wall.Id.ToString()));
			}
		}

		// --------------------------------------------------------------- and not in a door opening
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
	// EXCEPT A CHAIR AND ITS TABLE, which are meant to overlap: a chair that does not is standing
	// beside its table rather than tucked under it. The dining table is the only thing in this list a
	// chair may share space with, and that is asserted the other way round below.
	for (int32 A = 0; A < Built.Num(); ++A)
	{
		for (int32 B = A + 1; B < Built.Num(); ++B)
		{
			const bool bChairAndTable =
				(Built[A].bIsChair && Built[B].bIsDiningTable)
				|| (Built[B].bIsChair && Built[A].bIsDiningTable);

			if (bChairAndTable)
			{
				continue;
			}

			const double Into = Interpenetration(Built[A].Bounds, Built[B].Bounds);

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(TEXT("%s and %s occupy the same %.1f cm of the flat."),
					Built[A].What, Built[B].What, Into));
			}
		}
	}

	AddInfo(FString::Printf(TEXT("%d loose fixtures checked against %d walls, %d openings and each other."),
		Built.Num(), Spec.Walls.Num(), Spec.Openings.Num()));

	return true;
}

/**
 * A CHAIR PULLED OUT MUST NOT FOUL ANYTHING, asked of the geometry that was really built.
 *
 * This is what stands in for a motion assertion in a group where nothing articulates, and it is the
 * question the whole dining end was re-planned around: at the table's drawn position the north side
 * of a four-seater had 250 mm to the front of the sofa, so two of its four chairs could not be sat
 * at and there was no way past them either.
 *
 * Asked here rather than only against the spec because a built chair is not its drawn box - a seat
 * pad oversails its board and a raked stile leans - and because only the built flat has the walls,
 * the skirting and the other fixtures in it at the same time.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLoosePullOutTest,
	"HouseForge.Editor.AChairCanActuallyBePulledOut", HF_TEST_FLAGS)

bool FHFLoosePullOutTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLoose;

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

	// Everything else standing on the floor of the living room, as built.
	TArray<TPair<FName, FBox>> Neighbours;
	for (const FHFFixture& Fixture : Spec.Fixtures)
	{
		if (Fixture.RoomId != FName(TEXT("R_Living")) || Fixture.Type == EHFFixtureType::Chair)
		{
			continue;
		}

		if (AHFElementActor* Element = ElementFor(House, Fixture.Id))
		{
			Neighbours.Emplace(Fixture.Id, WorldBounds(*Element));
		}
	}

	int32 ChairsChecked = 0;

	for (const FLooseFixture& Wanted : LooseFixtures())
	{
		if (!Wanted.bIsChair)
		{
			continue;
		}

		AHFElementActor* Chair = ElementFor(House, FName(Wanted.Id));
		if (Chair == nullptr)
		{
			AddError(FString::Printf(TEXT("%s was not built at all."), Wanted.What));
			continue;
		}

		// Local +Y runs BACK on a chair, so pulling one out is a translation along its own back
		// direction. Taken off the actor's transform rather than off the drawing's yaw, because the
		// transform is what actually put it there.
		const FVector Back = Chair->GetActorTransform().TransformVectorNoScale(FVector(0.0, 1.0, 0.0));
		const FBox Pulled = WorldBounds(*Chair).ShiftBy(Back * PullOut);

		++ChairsChecked;

		double Nearest = TNumericLimits<double>::Max();
		FName NearestId;

		for (const TPair<FName, FBox>& Neighbour : Neighbours)
		{
			// The table is what the chair belongs to. It is under the top when tucked and still under
			// it when pulled back a little, and neither is a fault.
			if (Neighbour.Key == FName(TEXT("F_DiningTable")))
			{
				continue;
			}

			const double Into = Interpenetration(Pulled, Neighbour.Value);

			if (Into > -Nearest)
			{
				Nearest = -Into;
				NearestId = Neighbour.Key;
			}

			if (Into > 0.0)
			{
				AddError(FString::Printf(
					TEXT("%s pulled back %.0f cm drives %.1f cm into '%s'. Move the table, the chair or whatever is behind it."),
					Wanted.What, PullOut, Into, *Neighbour.Key.ToString()));
			}
		}

		// And not into the building either. A chair pulled out into the plaster is the same defect
		// with masonry instead of furniture.
		for (const FHFWall& Wall : Spec.Walls)
		{
			const double Into = Interpenetration(Pulled, SolidOf(Wall));

			if (Into > ContactTolerance)
			{
				AddError(FString::Printf(
					TEXT("%s pulled back %.0f cm drives %.1f cm into wall '%s'."),
					Wanted.What, PullOut, Into, *Wall.Id.ToString()));
			}
		}

		if (!NearestId.IsNone())
		{
			AddInfo(FString::Printf(TEXT("%s pulled out: %.1f cm clear of '%s', its closest neighbour."),
				Wanted.What, Nearest, *NearestId.ToString()));
		}
	}

	TestEqual(TEXT("All four chairs were swept"), ChairsChecked, 4);

	return true;
}

/**
 * The skirting runs BEHIND all of this, so none of it may stand on the board.
 *
 * Not one of these types is scribed joinery - FHFSkirting::IsScribedJoinery says so - which means
 * the board is continuous behind them and they have to stand clear of it instead. It is the
 * unscribed half of the decision the bedroom group found the other half of, and the sofa is the
 * interesting case: its back really is on the plaster, and it gets away with it because its base
 * starts at 120 and the board is 100 tall. The only thing it has below that is four legs, 90 mm in
 * from each corner.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFLooseSkirtingTest,
	"HouseForge.Editor.TheSkirtingRunsBehindTheLooseFurniture", HF_TEST_FLAGS)

bool FHFLooseSkirtingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeLoose;

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

	// NONE OF THEM CUTS THE BOARD, which is the decision everything below follows from. A sofa is not
	// made on site and is not scribed to the plaster: pull it away and the skirting is still there.
	static const EHFFixtureType LooseTypes[] = { EHFFixtureType::Sofa, EHFFixtureType::DiningTable,
		EHFFixtureType::CoffeeTable, EHFFixtureType::Chair };

	for (const EHFFixtureType Type : LooseTypes)
	{
		TestFalse(TEXT("Loose furniture does not cut the skirting"), FHFSkirting::IsScribedJoinery(Type));
	}

	const FHFRoom* Living = Spec.FindRoom(FName(TEXT("R_Living")));
	if (!TestNotNull(TEXT("The living room is in the flat"), Living))
	{
		return false;
	}

	for (const FLooseFixture& Wanted : LooseFixtures())
	{
		const FHFFixture* Fixture = Spec.Fixtures.FindByPredicate(
			[&Wanted](const FHFFixture& F) { return F.Id == FName(Wanted.Id); });

		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (Fixture == nullptr || Element == nullptr || Fixture->AnchorWallId.IsNone())
		{
			// Nothing anchored to a wall has nothing to stand on the board of. The tables and the
			// chairs are all free-standing, which is why only the sofa reaches this.
			continue;
		}

		const FHFWall* Wall = Spec.FindWall(Fixture->AnchorWallId);
		if (Wall == nullptr)
		{
			continue;
		}

		const FVector2D Along = (Wall->End - Wall->Start).GetSafeNormal();
		const FVector2D Normal(-Along.Y, Along.X);

		const double CentreOffset = FVector2D::DotProduct(Wall->Start, Normal);
		const double FixtureSide =
			FMath::Sign(FVector2D::DotProduct(Fixture->Position, Normal) - CentreOffset);
		const double FaceOffset = CentreOffset + FixtureSide * Wall->Thickness * 0.5;

		// What the fixture reaches AT OR BELOW SKIRTING HEIGHT. Everything above the board's top
		// passes over it freely, which is exactly how the sofa's back gets onto the plaster.
		const FBox Band = WorldBoundsBelow(*Element, Living->FloorZ - 1.0,
			Living->FloorZ + FMath::Max(Living->SkirtingHeight, 10.0));

		if (Band.IsValid == 0)
		{
			AddInfo(FString::Printf(TEXT("%s has nothing at skirting height."), Wanted.What));
			continue;
		}

		const FVector2D Corners[4] = {
			FVector2D(Band.Min.X, Band.Min.Y), FVector2D(Band.Max.X, Band.Min.Y),
			FVector2D(Band.Min.X, Band.Max.Y), FVector2D(Band.Max.X, Band.Max.Y) };

		double Nearest = TNumericLimits<double>::Max();
		for (const FVector2D& Corner : Corners)
		{
			Nearest = FMath::Min(Nearest,
				FixtureSide * (FVector2D::DotProduct(Corner, Normal) - FaceOffset));
		}

		AddInfo(FString::Printf(TEXT("%s: %.2f cm from the face of '%s' at skirting height (board is %.2f deep)."),
			Wanted.What, Nearest, *Wall->Id.ToString(), SkirtingParams.Depth));

		if (Nearest < SkirtingParams.Depth - 0.01)
		{
			AddError(FString::Printf(
				TEXT("%s reaches to %.2f cm off the face of '%s' at skirting height, and the board is %.2f cm deep. The skirting runs behind it, so it is standing on the board."),
				Wanted.What, Nearest, *Wall->Id.ToString(), SkirtingParams.Depth));
		}
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
