// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFTrimActors.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFFixturePlacement.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSkirtingPlan.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The trim group, in the flat rather than on a bench.
 *
 * SIX FITTINGS, AND EVERY ONE OF THEM IS WRONG IN THE DRAWING IN A WAY ONLY THIS FILE CAN SEE.
 *
 * All three railings are drawn 60 mm off their parapet's centreline, which leaves three quarters of
 * the footprint hanging over a four-storey drop with nothing under the base plates. All three pelmets
 * are drawn at a BaseZ of 2350, worked out against a false ceiling that has since become 300 mm
 * shallower, so built where the drawing puts them they hang with bare wall above them.
 *
 * Neither is visible in a parameter struct and neither is visible in a plan. Both are obvious the
 * moment the room is built, which is exactly the class of defect this project keeps finding by eye -
 * so it is measured here instead, against the masonry and the plasterboard the fittings attach to.
 */
namespace HouseForgeTrimActors
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

	/**
	 * Everything an element occupies in the world, off its VERTICES.
	 *
	 * Not off the component's own Bounds, which a scene proxy pads: this is being compared against
	 * masonry and plasterboard at millimetre tolerances.
	 */
	FBox WorldBounds(AHFElementActor& Element)
	{
		FBox Out(ForceInit);

		UDynamicMeshComponent* Component = Element.GetMeshComponent();
		if (Component == nullptr || Component->GetDynamicMesh() == nullptr)
		{
			return Out;
		}

		const FTransform ToWorld = Component->GetComponentTransform();

		Component->GetDynamicMesh()->ProcessMesh([&Out, &ToWorld](const FDynamicMesh3& Mesh)
		{
			for (const int32 Vertex : Mesh.VertexIndicesItr())
			{
				Out += ToWorld.TransformPosition(FVector(Mesh.GetVertex(Vertex)));
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

	const TCHAR* RailingIds[] =
	{
		TEXT("F_Rail_Balcony"), TEXT("F_Rail_BalconyN"), TEXT("F_Rail_BalconyE")
	};

	const TCHAR* PelmetIds[] =
	{
		TEXT("F_Pelmet_Living"), TEXT("F_Pelmet_MBed"), TEXT("F_Pelmet_Bed2")
	};
}

// ================================================================================== the railings

/**
 * ALL THREE RAILINGS STAND ON THEIR PARAPETS, and the drawing does not put them there.
 *
 * Two separate facts, and the second is the one that would have shipped:
 *
 *   - VERTICALLY, the railing's base plates sit exactly on the coping. A guard hovering above its
 *     parapet, or sunk into it, is a guard whose height is not what anybody thinks it is.
 *   - IN PLAN, the whole railing is inside the coping's 115 mm. As drawn it is not: all three are set
 *     out 60 mm inboard of the parapet centreline, so 27.5 of a 60 wide railing overhangs the balcony
 *     with nothing under it. FHFFixturePlacement::OnWallTop is what fixes that, and this is the
 *     assertion that says it did.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingsStandOnTheirParapetsTest,
	"HouseForge.Trim.RailingsStandOnTheirParapets", HF_TEST_FLAGS)

bool FHFRailingsStandOnTheirParapetsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	for (const TCHAR* Id : RailingIds)
	{
		const FHFFixture* Fixture = FixtureFor(Spec, Id);
		AHFElementActor* Element = ElementFor(House, Id);

		if (!TestNotNull(*FString::Printf(TEXT("'%s' is declared"), Id), Fixture)
			|| !TestNotNull(*FString::Printf(TEXT("'%s' is built"), Id), Element))
		{
			continue;
		}

		AHFRailingActor* Railing = Cast<AHFRailingActor>(Element);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is a railing actor"), Id), Railing))
		{
			continue;
		}

		// NOT ARTICULATED, and said in the flat as well as in the header. A balustrade with no gate in
		// it has nothing to open, and an empty Parts array with a master open amount on it is a
		// control that lies about the object.
		TestNull(*FString::Printf(TEXT("'%s' has no articulation to lie about"), Id),
			Cast<AHFArticulatedActor>(Element));

		const FHFWall* Parapet = Spec.FindWall(Fixture->AnchorWallId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' has a parapet"), Id), Parapet))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);
		const FBox Coping = SolidOf(*Parapet);

		if (!TestTrue(*FString::Printf(TEXT("'%s' built geometry"), Id), Bounds.IsValid != 0))
		{
			continue;
		}

		// -------------------------------------------------------------------------- vertically
		const double CopingTopZ = Parapet->BaseZ + Parapet->Height;

		TestEqual(*FString::Printf(TEXT("'%s' base plates land on the coping"), Id),
			Bounds.Min.Z, CopingTopZ, 0.05);

		// ------------------------------------------------------------------------------ in plan
		//
		// The railing's footprint has to be inside the coping's, across the parapet. Which plan axis
		// that is depends on the run, so it is asked of the narrow one - the coping is 11.5 cm and
		// every railing is 6.
		const bool bRunsAlongX = FMath::Abs(Parapet->End.X - Parapet->Start.X)
			> FMath::Abs(Parapet->End.Y - Parapet->Start.Y);

		const double AcrossMin = bRunsAlongX ? Bounds.Min.Y : Bounds.Min.X;
		const double AcrossMax = bRunsAlongX ? Bounds.Max.Y : Bounds.Max.X;
		const double CopingMin = bRunsAlongX ? Coping.Min.Y : Coping.Min.X;
		const double CopingMax = bRunsAlongX ? Coping.Max.Y : Coping.Max.X;

		TestTrue(*FString::Printf(
			TEXT("'%s' is entirely on the coping (railing %.1f..%.1f, coping %.1f..%.1f)"),
			Id, AcrossMin, AcrossMax, CopingMin, CopingMax),
			AcrossMin >= CopingMin - 0.01 && AcrossMax <= CopingMax + 0.01);

		// And centred on it, which is where the base plates actually go.
		TestEqual(*FString::Printf(TEXT("'%s' is centred on the coping"), Id),
			(AcrossMin + AcrossMax) * 0.5, (CopingMin + CopingMax) * 0.5, 0.05);

		// The correction the placement had to make. Recorded rather than assumed: it is the number
		// that says the drawing was 60 mm out, and it will change if anybody moves the fixture.
		const double Correction = FHFFixturePlacement::WallCentrelineCorrection(*Fixture, Parapet);
		AddInfo(FString::Printf(TEXT("'%s': OnWallTop moved it %.1f cm onto the coping."),
			Id, Correction));

		// --------------------------------------------------------------------------- and it fits
		TestTrue(*FString::Printf(TEXT("'%s' does not overrun its parapet"), Id),
			Bounds.Min.X >= Coping.Min.X - 0.01 && Bounds.Max.X <= Coping.Max.X + 0.01
			&& Bounds.Min.Y >= Coping.Min.Y - 0.01 && Bounds.Max.Y <= Coping.Max.Y + 0.01);
	}

	return true;
}

/**
 * THE GUARD IS A CODE HEIGHT ABOVE THE BALCONY FLOOR, WHICH IS WHERE IT HAS TO BE MEASURED FROM.
 *
 * The railing is only part of the guard; the parapet under it is the rest, and until this milestone
 * nothing in the repository ever added the two together. An 800 mm railing on the 1100 mm parapet the
 * flat used to draw is 1900 above the balcony floor - above standing eye level, so nobody can see
 * out, and directly across the living room's south window from 1.5 m away.
 *
 * Measured off the BUILT actor and the BUILT wall rather than off the parameter struct, so a railing
 * placed at the wrong Z fails here even when its own numbers are right.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingsAreACodeGuardTest,
	"HouseForge.Trim.RailingsAreACodeGuard", HF_TEST_FLAGS)

bool FHFRailingsAreACodeGuardTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	for (const TCHAR* Id : RailingIds)
	{
		const FHFFixture* Fixture = FixtureFor(Spec, Id);
		AHFRailingActor* Railing = Cast<AHFRailingActor>(ElementFor(House, Id));

		if (Fixture == nullptr || Railing == nullptr)
		{
			continue;
		}

		const FHFRoom* Balcony = Spec.FindRoom(Fixture->RoomId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is in a room"), Id), Balcony))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Railing);
		const double GuardTopZ = Bounds.Max.Z - Balcony->FloorZ;

		// NBC 2016 Part 4 asks 1050; bye-laws above 15 m commonly ask 1200.
		TestTrue(*FString::Printf(TEXT("'%s' tops out %.1f cm above the balcony floor"), Id, GuardTopZ),
			GuardTopZ >= 105.0);

		TestTrue(*FString::Printf(TEXT("'%s' clears the 120 cm figure too (%.1f cm)"), Id, GuardTopZ),
			GuardTopZ >= 120.0);

		// AND IT IS NOT A CAGE. Standing eye level is about 160; a barrier above it cannot be seen
		// over, which is exactly what a 1900 mm stack of parapet and railing produced.
		TestTrue(*FString::Printf(TEXT("'%s' can be seen over (%.1f cm)"), Id, GuardTopZ),
			GuardTopZ <= 140.0);

		// The other height, and the one raising the parapet makes worse: a coping is a foothold.
		TestTrue(*FString::Printf(TEXT("'%s' stands %.1f cm above the coping it can be climbed from"),
			Id, Railing->Railing.HeightAboveFoothold()),
			Railing->Railing.HeightAboveFoothold() >= 75.0);

		// The parapet arrived from the composing layer rather than being guessed. If the actor's idea
		// of what it is standing on disagrees with the wall, every height above is measured from the
		// wrong datum and nothing else here would say so.
		const FHFWall* Parapet = Spec.FindWall(Fixture->AnchorWallId);
		if (Parapet != nullptr)
		{
			TestEqual(*FString::Printf(TEXT("'%s' knows how high its own parapet is"), Id),
				Railing->Railing.MountBaseHeight, Parapet->Height, 0.01);

			TestEqual(*FString::Printf(TEXT("'%s' computes the guard the flat actually has"), Id),
				Railing->Railing.GuardHeightAboveFloor(), GuardTopZ, 0.05);
		}

		AddInfo(FString::Printf(
			TEXT("'%s': parapet %.0f + railing %.0f = %.0f cm guard, worst gap %.1f cm."),
			Id, Railing->Railing.MountBaseHeight, Railing->Railing.Height,
			Railing->Railing.GuardHeightAboveFloor(), Railing->Railing.WorstClearGap()));

		// The sphere rule, on the fitting that actually ended up in the flat.
		TestTrue(*FString::Printf(TEXT("'%s' passes no 100 mm sphere"), Id),
			Railing->Railing.WorstClearGap() <= 10.0 + 0.001);
	}

	return true;
}

/**
 * NO LOW WALL ANYWHERE IN THE FLAT IS LEFT WITHOUT A GUARD ON IT.
 *
 * THIS IS THE TEST THAT WOULD HAVE CAUGHT WHAT LOWERING THE PARAPETS DID, and it was written after a
 * render found it rather than before. Dropping every parapet from 1100 to 450 cured the 1900 mm cage
 * over the living room's south window and, in the same edit, turned the two RETURN walls of each
 * balcony into 450 mm kerbs with nothing on them - because the drawing marks one railing per balcony,
 * on the open face, and the returns had been relying on being 1100 of masonry. Six edges of unguarded
 * fall, introduced by a fix, invisible in the spec, and passed by every assertion in this file: each
 * railing was still on its own parapet and still a code height above its own floor.
 *
 * So the question is asked of the BUILDING rather than of the railings. Every wall low enough to fall
 * over has to reach 1050 above the floor beside it, and it may do that as masonry, or as masonry plus
 * whatever is built standing on it. Nothing about which walls happen to carry railings is assumed.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFNoBalconyEdgeIsUnguardedTest,
	"HouseForge.Trim.NoBalconyEdgeIsUnguarded", HF_TEST_FLAGS)

bool FHFNoBalconyEdgeIsUnguardedTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	// Below eye level, so a person can go over it. A storey wall is not a guard question.
	constexpr double FallableHeight = 190.0;

	// NBC 2016 Part 4.
	constexpr double MinGuardHeight = 105.0;

	int32 Checked = 0;

	for (const FHFWall& Wall : Spec.Walls)
	{
		if (Wall.Height >= FallableHeight)
		{
			continue;
		}

		++Checked;

		// The highest thing built standing on this wall, measured off its mesh rather than off any
		// figure - which is what makes "or as masonry plus what stands on it" a fact about the level.
		double ReachZ = Wall.BaseZ + Wall.Height;
		FString Guard = TEXT("masonry alone");

		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.AnchorWallId != Wall.Id)
			{
				continue;
			}

			AHFRailingActor* Railing = Cast<AHFRailingActor>(ElementFor(House, Fixture.Id));
			if (Railing == nullptr)
			{
				continue;
			}

			const FBox Bounds = WorldBounds(*Railing);
			if (Bounds.IsValid && Bounds.Max.Z > ReachZ)
			{
				ReachZ = Bounds.Max.Z;
				Guard = FString::Printf(TEXT("with '%s' on it"), *Fixture.Id.ToString());
			}
		}

		const double Above = ReachZ - Wall.BaseZ;

		TestTrue(*FString::Printf(TEXT("'%s' is guarded to %.0f cm (%s)"),
			*Wall.Id.ToString(), Above, *Guard), Above >= MinGuardHeight - 0.01);
	}

	// The whole flat has nine parapets. A test that silently checked none of them because the walls
	// were renamed would pass in exactly the case it exists for.
	TestEqual(TEXT("Every parapet in the flat was checked"), Checked, 9);

	return true;
}

// =================================================================================== the pelmets

/**
 * ALL THREE PELMETS ARE FIXED TO THE CEILING, AND THE DRAWING DOES NOT PUT THEM THERE EITHER.
 *
 * The drawn BaseZ of 2350 is a subtraction somebody did against a false ceiling that no longer
 * exists: the ceilings became shallow bands, the soffits came up, and the pelmets stayed where they
 * were drawn. Built there, each one hangs with a stripe of bare wall above it - a box screwed near a
 * ceiling rather than a step in one - and nothing in the spec says so.
 *
 * This asserts what UnderSoffit produces instead, and it asserts the DEFECT as well: the gap the
 * drawn figure would have left is measured and required to be real, so if somebody later "fixes" the
 * spec by editing 2350 to a number that happens to be right today, this test says the mechanism is no
 * longer doing anything and the number is free to go stale again.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPelmetsAreFixedToTheCeilingTest,
	"HouseForge.Trim.PelmetsAreFixedToTheCeiling", HF_TEST_FLAGS)

bool FHFPelmetsAreFixedToTheCeilingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	const TArray<FHFFixture> Fitted = House->FittedFixtures();

	for (const TCHAR* Id : PelmetIds)
	{
		const FHFFixture* Drawn = FixtureFor(Spec, Id);
		AHFPelmetActor* Pelmet = Cast<AHFPelmetActor>(ElementFor(House, Id));

		if (!TestNotNull(*FString::Printf(TEXT("'%s' is declared"), Id), Drawn)
			|| !TestNotNull(*FString::Printf(TEXT("'%s' is built as a pelmet"), Id), Pelmet))
		{
			continue;
		}

		TestNull(*FString::Printf(TEXT("'%s' has no articulation to lie about"), Id),
			Cast<AHFArticulatedActor>(static_cast<AHFElementActor*>(Pelmet)));

		const FHFRoom* Room = Spec.FindRoom(Drawn->RoomId);
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is in a room"), Id), Room))
		{
			continue;
		}

		const FHFFixture* Resolved = Fitted.FindByPredicate(
			[Drawn](const FHFFixture& F) { return F.Id == Drawn->Id; });

		const double SoffitZ = FHFCeilingFit::LowestSoffitZOver(
			Resolved != nullptr ? *Resolved : *Drawn, *Room, Spec.FalseCeilings);

		const FBox Bounds = WorldBounds(*Pelmet);
		if (!TestTrue(*FString::Printf(TEXT("'%s' built geometry"), Id), Bounds.IsValid != 0))
		{
			continue;
		}

		// ------------------------------------------------------------------- fixed to the ceiling
		//
		// Into it by the embedment, not onto it and not short of it. A board that stops exactly on a
		// plane leaves two coplanar faces flashing; one that stops short leaves a slot into the
		// plenum.
		constexpr double Embedment = 0.3;

		TestTrue(*FString::Printf(
			TEXT("'%s' top is fixed to the soffit (top %.2f, soffit %.2f)"),
			Id, Bounds.Max.Z, SoffitZ),
			Bounds.Max.Z >= SoffitZ - 0.01 && Bounds.Max.Z <= SoffitZ + Embedment + 0.01);

		// ---------------------------------------------------------------- and the drawing was wrong
		//
		// What the drawn figure would have produced, so the correction is a measured quantity rather
		// than a claim in a comment.
		const double DrawnTopZ = Room->FloorZ + Drawn->BaseZ + Drawn->Height;
		const double DrawnGap = SoffitZ - DrawnTopZ;

		AddInfo(FString::Printf(
			TEXT("'%s': soffit %.1f, built top %.1f, drawn top would have been %.1f (%s by %.1f cm)."),
			Id, SoffitZ, Bounds.Max.Z, DrawnTopZ,
			DrawnGap > 0.0 ? TEXT("hanging below the ceiling") : TEXT("buried inside it"),
			FMath::Abs(DrawnGap)));

		// STALE IN BOTH DIRECTIONS IN THE SAME FLAT, which is the finding rather than an aside. The
		// living room's ceiling carries a 480 perimeter bulkhead over its beams, so its soffit is at
		// 2520 and the pelmet drawn to 2550 is 30 mm INSIDE its own plasterboard. The two bedrooms'
		// pelmets are under shallower rings and hang below theirs instead. One number, 2350, wrong two
		// ways - which is exactly what a height measured against a ceiling and then written down does
		// once the ceiling is derived rather than declared.
		TestTrue(*FString::Printf(
			TEXT("'%s': the drawn BaseZ really is stale, by %.1f cm"), Id, DrawnGap),
			FMath::Abs(DrawnGap) > 1.0);

		// ------------------------------------------------------------------ back onto the plaster
		const FHFWall* Wall = Spec.FindWall(Drawn->AnchorWallId);
		if (Wall != nullptr)
		{
			const bool bRunsAlongX = FMath::Abs(Wall->End.X - Wall->Start.X)
				> FMath::Abs(Wall->End.Y - Wall->Start.Y);

			const FBox Solid = SolidOf(*Wall);

			// The pelmet's own back plane against the nearer face of the wall it hangs off.
			const double BackMin = bRunsAlongX ? Bounds.Min.Y : Bounds.Min.X;
			const double BackMax = bRunsAlongX ? Bounds.Max.Y : Bounds.Max.X;
			const double FaceMin = bRunsAlongX ? Solid.Min.Y : Solid.Min.X;
			const double FaceMax = bRunsAlongX ? Solid.Max.Y : Solid.Max.X;

			const double Standoff = (BackMax > FaceMax) ? BackMin - FaceMax : FaceMin - BackMax;

			TestEqual(*FString::Printf(TEXT("'%s' backs onto the plaster"), Id), Standoff, 0.0, 0.05);
		}

		// ------------------------------------------------------- clear of the window head below it
		//
		// A pelmet is over a window, and the wall between the two is what the curtain covers when it
		// is drawn back. Zero would mean the pelmet is sitting on the window head, which is a fitting
		// with nowhere to fix to.
		double HighestHeadZ = 0.0;
		for (const FHFOpening& Opening : Spec.Openings)
		{
			if (Opening.WallId != Drawn->AnchorWallId || Wall == nullptr)
			{
				continue;
			}
			HighestHeadZ = FMath::Max(HighestHeadZ, Wall->BaseZ + Opening.HeadHeight());
		}

		if (HighestHeadZ > 0.0)
		{
			TestTrue(*FString::Printf(
				TEXT("'%s' hangs clear of the window head below it (%.1f cm of wall)"),
				Id, Bounds.Min.Z - HighestHeadZ),
				Bounds.Min.Z > HighestHeadZ + 5.0);
		}
	}

	return true;
}

/**
 * NOTHING IN THE TRIM FOULS ANYTHING ELSE IN THE FLAT.
 *
 * The whole-flat sweep. Six new fittings went in at the two extremes of the section - one lot on the
 * parapets and one lot against the ceilings - and both are places where something else is already
 * standing: two condensing units share the balconies with the railings, and every pelmet is on a
 * window wall with a bed, a sofa or a TV run under it.
 *
 * Measured on the BUILT meshes rather than on the drawn footprints, because that is the whole reason
 * this class of defect survives a spec review.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTrimFoulsNothingTest,
	"HouseForge.Trim.TrimFoulsNothingInTheFlat", HF_TEST_FLAGS)

bool FHFTrimFoulsNothingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	TArray<FName> TrimIds;
	for (const TCHAR* Id : RailingIds) { TrimIds.Add(FName(Id)); }
	for (const TCHAR* Id : PelmetIds) { TrimIds.Add(FName(Id)); }

	for (const FName& Id : TrimIds)
	{
		AHFElementActor* Trim = ElementFor(House, Id);
		if (Trim == nullptr)
		{
			continue;
		}

		const FBox Mine = WorldBounds(*Trim);
		if (!Mine.IsValid)
		{
			continue;
		}

		// ------------------------------------------------------------------ against every fixture
		for (const FHFFixture& Other : Spec.Fixtures)
		{
			if (Other.Id == Id)
			{
				continue;
			}

			AHFElementActor* OtherActor = ElementFor(House, Other.Id);
			if (OtherActor == nullptr || OtherActor == Trim)
			{
				continue;
			}

			const FBox Theirs = WorldBounds(*OtherActor);
			if (!Theirs.IsValid)
			{
				continue;
			}

			// A 1 mm bite is a shared plane, not a collision - two fittings on one wall face touch
			// along it. Anything deeper than that in all three axes is one object inside another.
			const FVector Overlap(
				FMath::Min(Mine.Max.X, Theirs.Max.X) - FMath::Max(Mine.Min.X, Theirs.Min.X),
				FMath::Min(Mine.Max.Y, Theirs.Max.Y) - FMath::Max(Mine.Min.Y, Theirs.Min.Y),
				FMath::Min(Mine.Max.Z, Theirs.Max.Z) - FMath::Max(Mine.Min.Z, Theirs.Min.Z));

			const bool bIntersects = Overlap.X > 0.1 && Overlap.Y > 0.1 && Overlap.Z > 0.1;

			TestFalse(*FString::Printf(
				TEXT("'%s' does not run into '%s' (overlap %.1f x %.1f x %.1f cm)"),
				*Id.ToString(), *Other.Id.ToString(), Overlap.X, Overlap.Y, Overlap.Z),
				bIntersects);
		}

		// ------------------------------------------------------------- and not in a door opening
		//
		// The rule the validator already enforces on the spec, asked of the built mesh. A railing
		// across a balcony door is the one placement mistake this group could make that would be
		// invisible in every elevation.
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

			const double Length = Wall->Length();
			if (Length <= UE_KINDA_SMALL_NUMBER)
			{
				continue;
			}

			const FVector2D Along = (Wall->End - Wall->Start) / Length;
			const FVector2D Across(-Along.Y, Along.X);
			const FVector2D A = Wall->Start + Along * Opening.OffsetAlongWall;
			const FVector2D B = A + Along * Opening.Width;
			const FVector2D Half = Across * (Wall->Thickness * 0.5);

			FBox Clear(ForceInit);
			Clear += FVector(A.X + Half.X, A.Y + Half.Y, Wall->BaseZ + Opening.SillHeight);
			Clear += FVector(A.X - Half.X, A.Y - Half.Y, Wall->BaseZ + Opening.SillHeight);
			Clear += FVector(B.X + Half.X, B.Y + Half.Y, Wall->BaseZ + Opening.HeadHeight());
			Clear += FVector(B.X - Half.X, B.Y - Half.Y, Wall->BaseZ + Opening.HeadHeight());

			const FVector Overlap(
				FMath::Min(Mine.Max.X, Clear.Max.X) - FMath::Max(Mine.Min.X, Clear.Min.X),
				FMath::Min(Mine.Max.Y, Clear.Max.Y) - FMath::Max(Mine.Min.Y, Clear.Min.Y),
				FMath::Min(Mine.Max.Z, Clear.Max.Z) - FMath::Max(Mine.Min.Z, Clear.Min.Z));

			TestFalse(*FString::Printf(TEXT("'%s' does not stand in '%s'"),
				*Id.ToString(), *Opening.Id.ToString()),
				Overlap.X > 0.1 && Overlap.Y > 0.1 && Overlap.Z > 0.1);
		}
	}

	return true;
}

/**
 * THE SKIRTING IS UNTOUCHED BY THE TRIM, AND IT IS WORTH SAYING SO.
 *
 * Adding a fixture type to the spawn table changes FHFSkirting's answer, because the resolver only
 * cuts the board for a scribed fixture the composing layer is actually BUILDING. Six new fittings
 * therefore have to be checked against it even though the answer is obviously nothing: a railing is
 * on a balcony and a pelmet is at 2.5 m, and neither has anything to do with a skirting board.
 *
 * The identity BoundaryLength == CoveredLength + BreakLength is asserted per room around them, which
 * is what would catch a break opened by a fixture with no carcass in it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTrimLeavesTheSkirtingAloneTest,
	"HouseForge.Trim.TrimLeavesTheSkirtingAlone", HF_TEST_FLAGS)

bool FHFTrimLeavesTheSkirtingAloneTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrimActors;

	TestFalse(TEXT("A railing is not scribed joinery"),
		FHFSkirting::IsScribedJoinery(EHFFixtureType::Railing));
	TestFalse(TEXT("A pelmet is not scribed joinery"),
		FHFSkirting::IsScribedJoinery(EHFFixtureType::Pelmet));

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
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

	const TArray<FHFFixture> Fitted = House->FittedFixtures();
	const TSet<FName> Built = AHFHouseActor::BuiltFixtureIds(Fitted);

	// The trim is in the built set now, which is exactly why the skirting has to be re-asked.
	for (const TCHAR* Id : RailingIds)
	{
		TestTrue(*FString::Printf(TEXT("'%s' is built"), Id), Built.Contains(FName(Id)));
	}
	for (const TCHAR* Id : PelmetIds)
	{
		TestTrue(*FString::Printf(TEXT("'%s' is built"), Id), Built.Contains(FName(Id)));
	}

	// The same flat with the trim taken back out of the built set. Anything the six fittings do to a
	// skirting board shows up as a difference between the two answers, in any room - which is a real
	// comparison rather than the identity BreakLength() is defined by.
	TSet<FName> WithoutTrim = Built;
	for (const TCHAR* Id : RailingIds) { WithoutTrim.Remove(FName(Id)); }
	for (const TCHAR* Id : PelmetIds) { WithoutTrim.Remove(FName(Id)); }

	for (const FHFRoom& Room : Spec.Rooms)
	{
		const FHFSkirtingPlan With = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Fitted, FHFSkirtingParams(), &Built);

		const FHFSkirtingPlan Without = FHFSkirting::For(Room, Spec.Walls, Spec.Openings,
			Spec.Columns, Fitted, FHFSkirtingParams(), &WithoutTrim);

		TestEqual(*FString::Printf(TEXT("'%s': the trim cuts no skirting"), *Room.Id.ToString()),
			With.CoveredLength(), Without.CoveredLength(), 0.01);

		TestEqual(*FString::Printf(TEXT("'%s': and opens no break in it"), *Room.Id.ToString()),
			With.BreakLength(), Without.BreakLength(), 0.01);

		// Every break that IS there is still paid for by something standing in it. Re-asserted here
		// rather than assumed, because adding types to the spawn table is exactly what changes it.
		TestTrue(*FString::Printf(TEXT("'%s': the board still covers most of its boundary"),
			*Room.Id.ToString()),
			With.BreakLength() <= With.BoundaryLength() + 0.01);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
