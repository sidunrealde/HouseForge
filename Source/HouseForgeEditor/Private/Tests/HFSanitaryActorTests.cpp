// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFArticulatedActor.h"
#include "Actors/HFElementActors.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFSanitaryActors.h"
#include "Components/DynamicMeshComponent.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Editor.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "Misc/AutomationTest.h"
#include "Model/HFFixturePlacement.h"
#include "Model/HFSampleHouse.h"
#include "UDynamicMesh.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * The sanitary group, in the flat rather than on a bench.
 *
 * EVERY DEFECT THIS PROJECT HAS FOUND WAS FOUND BY LOOKING AT THE BUILT FLAT while the kit tests
 * passed, and this group produced three more of exactly that kind. Two of them are what this file
 * exists to stop coming back:
 *
 *   - both geysers were drawn with 107.5 mm of their back INSIDE W_Mid_Upper, and both mirrors hung
 *     47.5 mm off the plaster in front of it. The same defect with opposite signs, and neither is
 *     visible anywhere in a parameter struct.
 *   - both basins' tap levers ran 20 mm into the wall behind them, because a monobloc's lever points
 *     backwards and a basin's back is on the plaster.
 *
 * The third was not measurable at all: the cistern of both WCs was FLOATING, because the pan stops
 * where the cistern begins and nothing in a closed, correctly-sized, correctly-placed solid says
 * what holds it up. That one took a render, and the last test here is what it turned into.
 */
namespace HouseForgeSanitary
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
	 * ## Why one box each is not enough
	 *
	 * Because sanitaryware is not box-shaped, and the difference is not academic. A wall-hung basin's
	 * envelope reaches the plaster at the BACK of its bowl and reaches its highest at the TOP of its
	 * tap, which stands well forward of that - so its single box covers a volume the china never
	 * occupies, exactly where the mirror above it hangs. One box each says they interpenetrate by
	 * 30 mm; they are 40 mm apart.
	 *
	 * Slicing does not make the test weaker. A real clash is a real clash in whichever slice it
	 * happens in, and a fitting that leans over another still overlaps it somewhere.
	 */
	double SlicedInterpenetration(AHFElementActor& A, AHFElementActor& B)
	{
		const FBox WholeA = WorldBounds(A);
		const FBox WholeB = WorldBounds(B);

		if (Interpenetration(WholeA, WholeB) <= 0.0)
		{
			return Interpenetration(WholeA, WholeB);
		}

		// Five centimetres: below the height of anything in this group that leans, and above the
		// vertex spacing of the coarsest surface in it.
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

			// The plan overlap only: the two are in the same slice by construction, so a vertical
			// measurement here would just report the slice's own height.
			const FVector2D Plan(
				FMath::Min(BandA.Max.X, BandB.Max.X) - FMath::Max(BandA.Min.X, BandB.Min.X),
				FMath::Min(BandA.Max.Y, BandB.Max.Y) - FMath::Max(BandA.Min.Y, BandB.Min.Y));

			Worst = FMath::Max(Worst, FMath::Min(Plan.X, Plan.Y));
		}

		return Worst;
	}

	/** The thirteen fixtures this group builds, and what each one is. */
	struct FSanitaryFixture
	{
		const TCHAR* Id;
		const TCHAR* What;

		/** True for a fitting screwed to plaster, whose BACK must land on the finished wall face. */
		bool bOnTheWall;
	};

	const TArray<FSanitaryFixture>& SanitaryFixtures()
	{
		static const TArray<FSanitaryFixture> Fixtures = {
			{ TEXT("F_CBath_WC"),      TEXT("the common bath's WC"),         true  },
			{ TEXT("F_CBath_Basin"),   TEXT("the wall-hung basin"),          true  },
			{ TEXT("F_CBath_Shower"),  TEXT("the common bath's shower"),     true  },
			{ TEXT("F_CBath_Geyser"),  TEXT("the common bath's geyser"),     true  },
			{ TEXT("F_CBath_Mirror"),  TEXT("the common bath's mirror"),     true  },
			{ TEXT("F_CBath_Towel"),   TEXT("the common bath's towel rail"), true  },

			{ TEXT("F_MBath_WC"),      TEXT("the master bath's WC"),         true  },
			{ TEXT("F_MBath_Vanity"),  TEXT("the vanity"),                   false },
			{ TEXT("F_MBath_Basin"),   TEXT("the counter basin"),            false },
			{ TEXT("F_MBath_Shower"),  TEXT("the master bath's shower"),     true  },
			{ TEXT("F_MBath_Geyser"),  TEXT("the master bath's geyser"),     true  },
			{ TEXT("F_MBath_Mirror"),  TEXT("the master bath's mirror"),     true  },
			{ TEXT("F_MBath_Towel"),   TEXT("the master bath's towel rail"), true  },
		};

		return Fixtures;
	}
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSanitaryBuildsTest,
	"HouseForge.Editor.TheSanitaryFixturesAreReallyInTheFlat", HF_TEST_FLAGS)

bool FHFSanitaryBuildsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeSanitary;

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

	for (const FSanitaryFixture& Wanted : SanitaryFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));

		if (!TestNotNull(*FString::Printf(TEXT("%s is built"), Wanted.What), Element))
		{
			continue;
		}

		const FBox Bounds = WorldBounds(*Element);

		TestTrue(*FString::Printf(TEXT("%s has geometry"), Wanted.What),
			Bounds.IsValid && Bounds.GetVolume() > 1.0);

		const FHFFixture* Fixture = FixtureFor(Spec, FName(Wanted.Id));
		if (Fixture == nullptr)
		{
			AddError(FString::Printf(TEXT("%s is not in the spec at all"), Wanted.What));
			continue;
		}

		// IT STAYS INSIDE ITS OWN PLAN FOOTPRINT. Height is a different question - a WC's cistern and
		// a basin's tap both stand well above the drawn box on purpose - but nothing in this group is
		// allowed to grow sideways, because the plan is what every clearance in the room was set out
		// against.
		const double Largest = FMath::Max(Fixture->Footprint.X, Fixture->Footprint.Y);

		TestTrue(*FString::Printf(TEXT("%s stays within its drawn plan (%.1f x %.1f against %.1f)"),
			Wanted.What, Bounds.Max.X - Bounds.Min.X, Bounds.Max.Y - Bounds.Min.Y, Largest),
			Bounds.Max.X - Bounds.Min.X <= Largest + 6.0
				&& Bounds.Max.Y - Bounds.Min.Y <= Largest + 6.0);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSanitaryOnTheWallTest,
	"HouseForge.Editor.BathroomFittingsLandOnTheWallFace", HF_TEST_FLAGS)

bool FHFSanitaryOnTheWallTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeSanitary;

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
	// Measured against EVERY wall in the flat rather than against the one each fitting names: a
	// fitting buried in a different wall is the same failure, and would pass a check that only looked
	// at its own.

	for (const FSanitaryFixture& Wanted : SanitaryFixtures())
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

			// A centimetre of slack: a fitting sits ON the plaster, and a render finish that chamfers
			// its back arris leaves a facet a hair inside the plane it was flush with.
			TestTrue(*FString::Printf(TEXT("%s is not buried in '%s' (%.1f cm in)"),
				Wanted.What, *Wall.Id.ToString(), Into), Into <= 1.0);
		}
	}

	// --------------------------------------------------------------------- and none of it is floating
	//
	// The other direction, which is what the geysers and the mirrors actually were. A fitting drawn
	// with its back short of the plaster hangs in mid-air, and the gap is invisible from every view
	// except along the wall - which is not a view anybody takes of a bathroom.

	for (const FSanitaryFixture& Wanted : SanitaryFixtures())
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

		// How far the DRAWING would have put it out, which is the number the placement rule corrects.
		const double Correction = FHFFixturePlacement::WallFaceCorrection(*Fixture, Anchor);

		const FBox Bounds = WorldBounds(*Element);
		const FBox WallSolid = SolidOf(*Anchor);

		// The gap between the fitting's back and the plaster, on the axis the wall runs across.
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

		AddInfo(FString::Printf(
			TEXT("%s: the drawing put it %.1f cm off the face; built, its back is %.2f cm from it."),
			Wanted.What, Correction, Gap));

		TestTrue(*FString::Printf(TEXT("%s touches the plaster it is screwed to"), Wanted.What),
			Gap <= 1.0);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSanitaryClearanceTest,
	"HouseForge.Editor.NothingInTheBathroomsFoulsAnythingElse", HF_TEST_FLAGS)

bool FHFSanitaryClearanceTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeSanitary;

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

	// ------------------------------------------------------------------------ nothing in a doorway
	//
	// A bathroom's fittings standing in its own doorway is a failure this flat has already had once,
	// in both bathrooms, and it was fixed by moving them rather than by hoping.

	for (const FSanitaryFixture& Wanted : SanitaryFixtures())
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
			if (Wall == nullptr || Opening.Kind != EHFOpeningKind::Door)
			{
				continue;
			}

			const double Into = Interpenetration(Bounds, ClearOpeningOf(Opening, *Wall));

			TestTrue(*FString::Printf(TEXT("%s is not standing in '%s' (%.1f cm in)"),
				Wanted.What, *Opening.Id.ToString(), Into), Into <= 0.0);
		}
	}

	// ------------------------------------------------------------- and nothing inside anything else
	//
	// EVERY PAIR, not the pairs somebody thought of. One exception, and it is a real assembly rather
	// than a clash: a counter basin STANDS ON its vanity and is meant to be inside its box.

	const TArray<FSanitaryFixture>& Fixtures = SanitaryFixtures();

	for (int32 A = 0; A < Fixtures.Num(); ++A)
	{
		AHFElementActor* First = ElementFor(House, FName(Fixtures[A].Id));
		if (First == nullptr)
		{
			continue;
		}

		for (int32 B = A + 1; B < Fixtures.Num(); ++B)
		{
			AHFElementActor* Second = ElementFor(House, FName(Fixtures[B].Id));
			if (Second == nullptr)
			{
				continue;
			}

			// The vanity and the basin standing on it are ONE fitting in two actors.
			const bool bSameAssembly =
				FCString::Strcmp(Fixtures[A].Id, TEXT("F_MBath_Vanity")) == 0
					&& FCString::Strcmp(Fixtures[B].Id, TEXT("F_MBath_Basin")) == 0;

			if (bSameAssembly)
			{
				continue;
			}

			const double Into = SlicedInterpenetration(*First, *Second);

			if (Into > -2.0)
			{
				AddInfo(FString::Printf(TEXT("%s and %s come within %.2f cm of each other."),
					Fixtures[A].What, Fixtures[B].What, -Into));
			}

			TestTrue(*FString::Printf(TEXT("%s does not foul %s"), Fixtures[A].What, Fixtures[B].What),
				Into <= 0.0);
		}
	}

	// ------------------------------------------------------------------- and it clears the ceiling
	//
	// The geysers are the only things in this group anywhere near a soffit, and they are why
	// FHFCeilingFit has a rule for the type. Asserted against the room's own finished soffit rather
	// than a number, so it stays true if somebody deepens a bathroom ceiling.

	for (const FSanitaryFixture& Wanted : SanitaryFixtures())
	{
		AHFElementActor* Element = ElementFor(House, FName(Wanted.Id));
		const FHFFixture* Fixture = FixtureFor(Spec, FName(Wanted.Id));

		const FHFRoom* Room = Fixture != nullptr ? Spec.FindRoom(Fixture->RoomId) : nullptr;
		if (Element == nullptr || Room == nullptr)
		{
			continue;
		}

		double Drop = 0.0;
		for (const FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
		{
			if (Ceiling.RoomId == Room->Id)
			{
				Drop = FMath::Max(Drop, Ceiling.Drop);
			}
		}

		const double SoffitZ = Room->FloorZ + Room->CeilingHeight - Drop;
		const FBox Bounds = WorldBounds(*Element);

		TestTrue(*FString::Printf(TEXT("%s is below the finished soffit at %.0f (it tops out at %.1f)"),
			Wanted.What, SoffitZ, Bounds.Max.Z), Bounds.Max.Z <= SoffitZ);
	}

	return true;
}

// ---------------------------------------------------------------------------------------------

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSanitaryStandsOnSomethingTest,
	"HouseForge.Editor.NothingInTheBathroomsIsFloating", HF_TEST_FLAGS)

bool FHFSanitaryStandsOnSomethingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeSanitary;

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

	// ------------------------------------------------------- THE ONE THAT ONLY A RENDER EVER SHOWED
	//
	// A close-coupled cistern is bolted to the back of the pan, and the back of the pan is solid china
	// from the floor to the shelf it sits on. Built without that shelf the cistern is a box hanging in
	// mid-air behind the bowl - which is what both WCs were: correctly sized, correctly placed,
	// watertight, inside the drawn box, with a flush plate that moved, and floating.
	//
	// There is no assertion about a closed solid that says "and something holds this up". So the test
	// is a COLUMN PROBE: at the middle of the cistern in plan, there must be ceramic all the way from
	// the floor to the shelf. That is the question a person answers by looking, written down.

	const TCHAR* WCs[2] = { TEXT("F_CBath_WC"), TEXT("F_MBath_WC") };

	for (const TCHAR* Id : WCs)
	{
		AHFWCActor* WC = Cast<AHFWCActor>(ElementFor(House, FName(Id)));
		if (!TestNotNull(*FString::Printf(TEXT("'%s' is a WC"), Id), WC))
		{
			continue;
		}

		UDynamicMeshComponent* Shell = WC->GetMeshComponent();
		if (Shell == nullptr || Shell->GetDynamicMesh() == nullptr)
		{
			AddError(FString::Printf(TEXT("'%s' has no shell mesh"), Id));
			continue;
		}

		const FHFWCParams& P = WC->WC;

		// The cistern's own centre, in the WC's local frame - which is the frame the shell is in.
		const FVector2D Probe(0.0, P.Projection * 0.5 - P.CisternDepth * 0.5);

		int32 Filled = 0;
		constexpr int32 Samples = 12;

		Shell->GetDynamicMesh()->ProcessMesh([&Filled, &Probe, &P](const FDynamicMesh3& Mesh)
		{
			for (int32 Sample = 0; Sample < Samples; ++Sample)
			{
				const FVector3d Point(Probe.X, Probe.Y,
					P.RimZ() * (Sample + 0.5) / static_cast<double>(Samples));

				// A ray straight up from the point: an odd number of crossings means it started INSIDE
				// the solid. Exact for a closed mesh, and it needs no physics body.
				int32 Crossings = 0;

				for (const int32 Tri : Mesh.TriangleIndicesItr())
				{
					FVector3d A, B, C;
					Mesh.GetTriVertices(Tri, A, B, C);

					const double D = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
					if (FMath::Abs(D) < 1e-9)
					{
						continue;
					}

					const double U = ((B.Y - C.Y) * (Point.X - C.X) + (C.X - B.X) * (Point.Y - C.Y)) / D;
					const double V = ((C.Y - A.Y) * (Point.X - C.X) + (A.X - C.X) * (Point.Y - C.Y)) / D;
					const double W = 1.0 - U - V;

					if (U < 0.0 || V < 0.0 || W < 0.0)
					{
						continue;
					}

					if (U * A.Z + V * B.Z + W * C.Z > Point.Z)
					{
						++Crossings;
					}
				}

				if (Crossings % 2 == 1)
				{
					++Filled;
				}
			}
		});

		AddInfo(FString::Printf(TEXT("'%s': %d of %d samples under the cistern are solid china."),
			Id, Filled, Samples));

		TestEqual(*FString::Printf(TEXT("'%s' has ceramic all the way under its cistern"), Id),
			Filled, Samples);
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
