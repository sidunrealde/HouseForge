// Copyright Siddartha G. All Rights Reserved.

#include "HouseForgeEditor.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFElementActors.h"
#include "Actors/HFFanActor.h"
#include "Actors/HFHouseActor.h"
#include "Actors/HFWardrobeActor.h"
#include "Components/DynamicMeshComponent.h"
#include "Editor.h"
#include "EngineUtils.h"
#include "Engine/World.h"
#include "Misc/AutomationTest.h"
#include "Misc/ScopeExit.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// CHANGING THE CEILING CHANGES EVERYTHING UNDER IT.
//
// The user's words: "when I change between the ceilings the exhaust fans fans walls and other
// things should adjust accordingly right now they are clipping."
//
// The ceiling fan already followed its ceiling, because the failure it causes is spectacular - the
// whole rotor inside the plasterboard - and it was fixed the moment somebody looked. Nothing else
// did. An extract fixed high on a wall, a geyser over a shower, a curtain pelmet, the top of a
// wardrobe: all of them were set out against the structural slab, and all of them were left inside
// the ceiling when the depth model changed.
//
// So these tests take a house that is standing in a level, change the design of its ceiling the way
// a user does, and look at what happened to everything else in the room. HFCeilingFitTests measures
// the resolver; this measures the SEAM between it and the level - the actors, their transforms, and
// the meshes they actually built.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeCeilingDependents
{
	void ClearHouseForgeActors(UWorld* World)
	{
		for (TActorIterator<AHFHouseActor> It(World); It; ++It)
		{
			It->ClearGeometry();
			It->Destroy();
		}

		for (TActorIterator<AHFElementActor> It(World); It; ++It)
		{
			It->Destroy();
		}
	}

	/**
	 * A room with one of everything that answers to the ceiling over it.
	 *
	 * A beam on the south edge, because that is what puts a perimeter bulkhead ring on the ceiling -
	 * and the ring is the deep part, hanging along exactly the walls the fittings are screwed to.
	 *
	 * The four fittings are the four RULES rather than four objects: a fan on a rod, an extract that
	 * slides down, a wardrobe that is cut shorter, and a pelmet that is neither built as an actor nor
	 * exempt from the arithmetic. Each is drawn at a height a drawing would state and none of them
	 * mentions the ceiling.
	 */
	AHFHouseActor* SpawnDependentsHouse(UWorld* World, EHFCeilingTemplate Template)
	{
		ClearHouseForgeActors(World);

		FHFHouseSpec Spec;
		Spec.Name = TEXT("Ceiling Dependents");
		Spec.Units = EHFUnits::Centimeters;
		Spec.UnitsSource = TEXT("test");

		FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
		Room.Id = TEXT("R1");
		Room.Type = EHFRoomType::Bedroom;
		Room.CeilingHeight = 300.0;
		Room.Boundary = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 400), FVector2D(0, 400) };

		auto AddWall = [&Spec](const FName& Id, const FVector2D& Start, const FVector2D& End)
		{
			FHFWall& Wall = Spec.Walls.AddDefaulted_GetRef();
			Wall.Id = Id;
			Wall.Start = Start;
			Wall.End = End;
			Wall.Thickness = 11.5;
			Wall.Height = 300.0;
		};

		// The two runs on the room's short sides stop short of the corners, so no two walls share a
		// footprint. Not how a building is set out - a real junction is one run built through and the
		// other butted to its face, and AHFHouseActor resolves that properly - but a boolean at a
		// corner where two equal walls meet exactly end to end is a coin toss the mesh library loses,
		// and a fixture that logs a z-fight warning on every build is a fixture that hides whatever
		// the test is actually about.
		AddWall(TEXT("W_South"), FVector2D(0, 0), FVector2D(500, 0));
		AddWall(TEXT("W_East"), FVector2D(500, 20), FVector2D(500, 380));
		AddWall(TEXT("W_North"), FVector2D(500, 400), FVector2D(0, 400));
		AddWall(TEXT("W_West"), FVector2D(0, 380), FVector2D(0, 20));

		// A 23 beam over an 11.5 partition stands 5.75 proud of the plaster on both faces, so it shows
		// in the room and the ceiling grows a ring to bury it. The reference flat's own section.
		FHFBeam& Beam = Spec.Beams.AddDefaulted_GetRef();
		Beam.Id = TEXT("BM1");
		Beam.Start = FVector2D(0.0, 0.0);
		Beam.End = FVector2D(500.0, 0.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;

		FHFFalseCeiling& Ceiling = Spec.FalseCeilings.AddDefaulted_GetRef();
		Ceiling.Id = TEXT("FC1");
		Ceiling.RoomId = TEXT("R1");
		Ceiling.Template = Template;

		// Dead centre, where a band leaves the room open to the slab and a tray does not.
		FHFFixture& Fan = Spec.Fixtures.AddDefaulted_GetRef();
		Fan.Id = TEXT("FAN1");
		Fan.RoomId = TEXT("R1");
		Fan.Type = EHFFixtureType::CeilingFan;
		Fan.Position = FVector2D(250.0, 200.0);
		Fan.Footprint = FVector2D(120.0, 120.0);
		Fan.Height = 30.0;

		// High on the north wall, at the height the reference flat's bathroom extracts are drawn at:
		// 230 to 255, which is 30 mm inside a soffit at 252.
		FHFFixture& Extract = Spec.Fixtures.AddDefaulted_GetRef();
		Extract.Id = TEXT("EXH1");
		Extract.RoomId = TEXT("R1");
		Extract.Type = EHFFixtureType::ExhaustFan;
		Extract.Position = FVector2D(250.0, 394.0);
		Extract.Footprint = FVector2D(25.0, 10.0);
		Extract.BaseZ = 230.0;
		Extract.Height = 25.0;
		Extract.AnchorWallId = TEXT("W_North");

		// Against the east wall, tall enough to be worth cutting and with a TOP-HUNG loft on it -
		// the one moving part in this flat whose leading edge travels UPWARD when it opens.
		FHFFixture& Wardrobe = Spec.Fixtures.AddDefaulted_GetRef();
		Wardrobe.Id = TEXT("WR1");
		Wardrobe.RoomId = TEXT("R1");
		Wardrobe.Type = EHFFixtureType::Wardrobe;
		Wardrobe.Position = FVector2D(470.0, 200.0);
		Wardrobe.Footprint = FVector2D(180.0, 60.0);
		Wardrobe.Height = 240.0;
		Wardrobe.RotationDegrees = 90.0;
		Wardrobe.AnchorWallId = TEXT("W_East");
		Wardrobe.Params.ShutterCount = 3;
		Wardrobe.Params.ShelfCount = 4;
		Wardrobe.Params.bHasLoft = true;
		Wardrobe.Params.LoftHeight = 45.0;
		Wardrobe.Params.LoftShutterMotion = EHFShutterMotion::TopHung;
		Wardrobe.Params.PlinthHeight = 10.0;

		// A pelmet over the south wall: the fitting the user named, and the one with no actor of its
		// own. It is here because a resolver that only worked for things that had already been built
		// would have missed all three of the flat's pelmets.
		FHFFixture& Pelmet = Spec.Fixtures.AddDefaulted_GetRef();
		Pelmet.Id = TEXT("PEL1");
		Pelmet.RoomId = TEXT("R1");
		Pelmet.Type = EHFFixtureType::Pelmet;
		Pelmet.Position = FVector2D(250.0, 9.0);
		Pelmet.Footprint = FVector2D(190.0, 18.0);
		Pelmet.BaseZ = 235.0;
		Pelmet.Height = 20.0;
		Pelmet.AnchorWallId = TEXT("W_South");

		AHFHouseActor* House = World->SpawnActor<AHFHouseActor>();
		if (House == nullptr)
		{
			return nullptr;
		}

		House->SetSpec(Spec);
		House->BuildGeometry();
		return House;
	}

	template <typename T>
	T* FindElement(AHFHouseActor* House)
	{
		for (AActor* Element : House->ElementActors)
		{
			if (T* Typed = Cast<T>(Element))
			{
				return Typed;
			}
		}
		return nullptr;
	}

	const FHFFixture* FindFitted(const TArray<FHFFixture>& Fixtures, const FName& Id)
	{
		return Fixtures.FindByPredicate([&Id](const FHFFixture& F) { return F.Id == Id; });
	}

	/** World bounds of everything an actor renders, moving parts included. */
	FBox WorldBoundsOf(AActor* Actor)
	{
		FBox Bounds(ForceInit);

		TArray<UDynamicMeshComponent*> Components;
		Actor->GetComponents(Components);

		for (UDynamicMeshComponent* Component : Components)
		{
			if (Component == nullptr || Component->GetDynamicMesh() == nullptr
				|| Component->GetDynamicMesh()->GetMeshRef().TriangleCount() == 0)
			{
				continue;
			}

			Component->UpdateBounds();
			Bounds += Component->Bounds.GetBox();
		}

		return Bounds;
	}

	FString NameOf(EHFCeilingTemplate Template)
	{
		return StaticEnum<EHFCeilingTemplate>()->GetNameStringByValue(static_cast<int64>(Template));
	}
}

/**
 * CHANGING THE CEILING'S DESIGN RE-HANGS EVERYTHING UNDER IT, ON A HOUSE ALREADY BUILT.
 *
 * Not a fresh build from a new spec, which would prove very little: the failure the user reported is
 * about switching designs on a flat that is standing in the level, and the only path that does that
 * is AHFHouseActor::ApplyProjectSettingsToCeilings. It used to re-seed the ceilings and the ceiling
 * fans and nothing else at all.
 *
 * A plain band leaves the middle of the room open to the slab, so a fan under one needs no extra rod.
 * A stepped tray panels that middle in at 100 below the slab, so the same fan's rod has to grow by
 * exactly that. The rod is the assertion because it is CUMULATIVE - ApplyCeilingAbove adds to the
 * project's figure by design - so a re-seed that adjusted rather than rebuilt would hang the fan a
 * ceiling lower every time the user changed their mind.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingTemplateRehangsDependentsTest,
	"HouseForge.Editor.ChangingACeilingTemplateRehangsWhatIsUnderIt", HF_TEST_FLAGS)

bool FHFCeilingTemplateRehangsDependentsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCeilingDependents;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = SpawnDependentsHouse(World, EHFCeilingTemplate::PlainBand);
	if (!TestNotNull(TEXT("A house builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	AHFCeilingActor* Ceiling = FindElement<AHFCeilingActor>(House);
	AHFWardrobeActor* Wardrobe = FindElement<AHFWardrobeActor>(House);

	AHFFanActor* CeilingFan = nullptr;
	AHFFanActor* Extract = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFFanActor* Fan = Cast<AHFFanActor>(Element))
		{
			(Fan->ElementId == FName(TEXT("FAN1")) ? CeilingFan : Extract) = Fan;
		}
	}

	if (!TestNotNull(TEXT("The ceiling was built"), Ceiling)
		|| !TestNotNull(TEXT("The ceiling fan was built"), CeilingFan)
		|| !TestNotNull(TEXT("The extract was built"), Extract)
		|| !TestNotNull(TEXT("The wardrobe was built"), Wardrobe))
	{
		return false;
	}

	// ------------------------------------------------------------------------ a shallow plain band
	const double BandDrop = Ceiling->Ceiling.Drop;
	const double RodUnderBand = CeilingFan->Fan.DropLength;
	const double ExtractZUnderBand = Extract->GetActorLocation().Z;

	TestTrue(TEXT("A plain band is shallow"), BandDrop > 0.0 && BandDrop < 30.0);
	TestTrue(TEXT("The band grew a ring to bury the beam"), Ceiling->Ceiling.HasPerimeterBulkhead());

	AddInfo(FString::Printf(TEXT("PlainBand: drop %.1f, ring %.1f, fan rod %.1f, extract axis %.1f."),
		BandDrop, Ceiling->Ceiling.PerimeterBulkheadDrop, RodUnderBand, ExtractZUnderBand));

	// The fan is dead centre, where a band leaves the room open to the slab, so its rod is exactly
	// the project's figure and nothing more.
	{
		const TArray<FHFFixture> Fitted = House->FittedFixtures();
		const FHFFixture* FittedExtract = FindFitted(Fitted, TEXT("EXH1"));

		if (TestNotNull(TEXT("The extract is in the fitted list"), FittedExtract))
		{
			TestTrue(TEXT("The extract had to come down to clear the ring over it"),
				FittedExtract->BaseZ < 230.0 - 0.01);
		}
	}

	// -------------------------------------------------- the user changes their mind: a stepped tray
	//
	// The one thing a user does. Everything else here follows from it.
	House->Spec.FalseCeilings[0].Template = EHFCeilingTemplate::SteppedTray;

	const int32 Rebuilt = House->ApplyProjectSettingsToCeilings();
	AddInfo(FString::Printf(TEXT("Switching to SteppedTray rebuilt %d elements."), Rebuilt));

	TestTrue(TEXT("Changing the design rebuilt more than the ceiling itself"), Rebuilt > 1);

	Ceiling = FindElement<AHFCeilingActor>(House);
	if (!TestNotNull(TEXT("The ceiling survives"), Ceiling))
	{
		return false;
	}

	TestTrue(TEXT("A stepped tray is a different ceiling from a plain band"),
		!FMath::IsNearlyEqual(Ceiling->Ceiling.Drop, BandDrop, 0.01)
			|| Ceiling->Ceiling.InnerDrop > 0.0);

	// ------------------------------------------------------------------------------- the fan's rod
	//
	// A tray panels the middle of the room in, so the fan that hung from bare slab now hangs through
	// a soffit - and the rod has to grow by exactly the depth of what was put over it.
	const double InnerDrop = (Ceiling->Ceiling.InnerDrop > 0.0)
		? Ceiling->Ceiling.InnerDrop
		: Ceiling->Ceiling.Drop * 0.5;

	AddInfo(FString::Printf(TEXT("SteppedTray: drop %.1f, inner %.1f, fan rod %.1f (was %.1f)."),
		Ceiling->Ceiling.Drop, InnerDrop, CeilingFan->Fan.DropLength, RodUnderBand));

	TestTrue(TEXT("The fan's rod grew when the ceiling closed in over it"),
		CeilingFan->Fan.DropLength > RodUnderBand + 0.01);

	TestEqual(TEXT("It grew by exactly the drop of what is now over it"),
		CeilingFan->Fan.DropLength - RodUnderBand, InnerDrop, 0.01);

	// The canopy goes with it, or the hole cut for the rod is on show from underneath.
	TestEqual(TEXT("The canopy came down to the new soffit"),
		CeilingFan->Fan.CanopyDrop, InnerDrop, 0.01);

	// ---------------------------------------------------------------- and it is not cumulative
	//
	// Applying the same design again must produce the same fan. ApplyCeilingAbove ADDS, so a re-seed
	// that adjusted instead of rebuilding would lower the fan every time the page was touched.
	const double RodAfterOnce = CeilingFan->Fan.DropLength;
	House->ApplyProjectSettingsToCeilings();

	TestEqual(TEXT("Re-applying the same design does not lower the fan again"),
		CeilingFan->Fan.DropLength, RodAfterOnce, 0.001);

	// -------------------------------------------------------------------------- and the extract
	{
		const TArray<FHFFixture> Fitted = House->FittedFixtures();
		const FHFFixture* FittedExtract = FindFitted(Fitted, TEXT("EXH1"));

		if (TestNotNull(TEXT("The extract is still in the fitted list"), FittedExtract))
		{
			const FHFRoom* Room = House->Spec.FindRoom(TEXT("R1"));
			const double SoffitZ =
				FHFCeilingFit::LowestSoffitZOver(*FittedExtract, *Room, House->Spec.FalseCeilings);

			TestTrue(TEXT("The extract still clears the ceiling after the design changed"),
				Room->FloorZ + FittedExtract->BaseZ + FittedExtract->Height <= SoffitZ + 0.01);

			// The actor moved with it, rather than the fitted list being a number nobody read.
			TestEqual(TEXT("The extract actor is where the fitted list puts it"),
				Extract->GetActorLocation().Z,
				Room->FloorZ + FittedExtract->BaseZ + FittedExtract->Height * 0.5, 0.01);
		}
	}

	// ---------------------------------------------------- the hole in the wall went with the fan
	//
	// The case covers exactly the spot where the hole is, so a duct left at the drawn height is a
	// bare square opening in a finished wall with the fan sitting below it.
	{
		AHFWallActor* North = nullptr;
		for (AActor* Element : House->ElementActors)
		{
			AHFWallActor* Candidate = Cast<AHFWallActor>(Element);
			if (Candidate != nullptr && Candidate->ElementId == FName(TEXT("W_North")))
			{
				North = Candidate;
				break;
			}
		}

		if (TestNotNull(TEXT("The extract's wall was built"), North))
		{
			const FHFOpening* Duct = North->Openings.FindByPredicate(
				[](const FHFOpening& O) { return O.Kind == EHFOpeningKind::Ventilator; });

			if (TestNotNull(TEXT("The wall carries a duct for the extract"), Duct))
			{
				const double DuctCentreZ = North->Wall.BaseZ + Duct->SillHeight + Duct->Height * 0.5;

				TestEqual(TEXT("The duct is cored at the height the fan ended up turning at"),
					DuctCentreZ, Extract->GetActorLocation().Z, 0.1);
			}
		}
	}

	return true;
}

/**
 * NOTHING TOUCHES THE CEILING, UNDER ANY DESIGN, AT ANY OPEN AMOUNT.
 *
 * The whole-room assertion, measured on the MESHES that were actually built rather than on the
 * figures they were built from. A resolver that returned the right numbers while an actor placed
 * itself from something else would pass every test in HFCeilingFitTests and still show a fan case
 * disappearing into the plasterboard.
 *
 * AT ANY OPEN AMOUNT, because a moving part can reach where its closed self does not. The wardrobe
 * here carries a TOP-HUNG loft flap - the one thing in this flat whose leading edge travels UPWARD as
 * it opens - so a carcass cut to fit under the soffit with a flap that swings through it would be a
 * wardrobe that is correct until somebody opens it.
 *
 * The ceiling fan is excepted from the whole-actor test and asked about its ROTOR instead: its rod is
 * meant to pass through the soffit, which is why the ceiling cuts a hole for it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFNothingTouchesTheCeilingTest,
	"HouseForge.Editor.NothingTouchesTheCeilingAtAnyOpenAmount", HF_TEST_FLAGS)

bool FHFNothingTouchesTheCeilingTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCeilingDependents;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	const EHFCeilingTemplate Templates[] =
	{
		EHFCeilingTemplate::PlainBand,
		EHFCeilingTemplate::Cove,
		EHFCeilingTemplate::SteppedTray,
		EHFCeilingTemplate::FramedPanel
	};

	for (EHFCeilingTemplate Template : Templates)
	{
		AHFHouseActor* House = SpawnDependentsHouse(World, Template);
		if (!TestNotNull(*FString::Printf(TEXT("[%s] a house builds"), *NameOf(Template)), House))
		{
			continue;
		}

		const FHFRoom* Room = House->Spec.FindRoom(TEXT("R1"));
		if (Room == nullptr)
		{
			continue;
		}

		const TArray<FHFFixture> Fitted = House->FittedFixtures();

		// Closed, then wide open. Both, because the two are different shapes and only one of them is
		// what a still of a finished flat usually shows.
		for (double OpenAmount : { 0.0, 1.0 })
		{
			for (AActor* Element : House->ElementActors)
			{
				if (AHFArticulatedActor* Articulated = Cast<AHFArticulatedActor>(Element))
				{
					Articulated->SetMasterOpenAmount(OpenAmount);
				}
			}

			for (const FHFFixture& Fixture : Fitted)
			{
				const EHFCeilingFitRule Rule = FHFCeilingFit::RuleFor(Fixture.Type);
				if (Rule == EHFCeilingFitRule::Ignores)
				{
					continue;
				}

				AHFElementActor* Actor = nullptr;
				for (AActor* Element : House->ElementActors)
				{
					AHFElementActor* Typed = Cast<AHFElementActor>(Element);
					if (Typed != nullptr && Typed->ElementId == Fixture.Id)
					{
						Actor = Typed;
						break;
					}
				}

				if (Actor == nullptr)
				{
					// A fixture type with no generator yet - the pelmet. Its resolved box is what
					// HFCeilingFitTests measures; there is no mesh here to look at.
					continue;
				}

				const double SoffitZ =
					FHFCeilingFit::LowestSoffitZOver(Fixture, *Room, House->Spec.FalseCeilings);

				// A fan on a rod is SUPPOSED to reach through the soffit - that is what the hole is
				// cut for - so the question is asked of the part that must not: the rotor.
				FBox Bounds(ForceInit);
				if (Rule == EHFCeilingFitRule::HangsOnARod)
				{
					AHFFanActor* Fan = Cast<AHFFanActor>(Actor);
					UDynamicMeshComponent* Rotor = Fan != nullptr
						? Fan->GetPartComponent(AHFFanActor::RotorPartId())
						: nullptr;

					if (Rotor == nullptr)
					{
						continue;
					}

					Rotor->UpdateBounds();
					Bounds = Rotor->Bounds.GetBox();
				}
				else
				{
					Bounds = WorldBoundsOf(Actor);
				}

				if (!Bounds.IsValid)
				{
					continue;
				}

				TestTrue(*FString::Printf(
					TEXT("[%s] '%s' clears the soffit at open %.0f (reaches %.1f, soffit %.1f)"),
					*NameOf(Template), *Fixture.Id.ToString(), OpenAmount, Bounds.Max.Z, SoffitZ),
					Bounds.Max.Z <= SoffitZ + 0.01);
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d elements checked closed and open."),
			*NameOf(Template), House->ElementActors.Num()));

		ClearHouseForgeActors(World);
	}

	return true;
}

/**
 * A HAND-EDITED ELEMENT IS NOT RE-HUNG BY A CEILING CHANGE.
 *
 * bArtistEdited is the whole of what makes these dynamic meshes safe to sculpt - see
 * .claude/rules/04-conventions.md - and every new mechanism that reaches into a built level is a new
 * way to walk over somebody's modelling. This one reaches further than any before it: one settings
 * change now re-seeds ceilings, both kinds of fan, wardrobes and the walls that carry a duct.
 *
 * Asserted on the PARAMETERS as well as the transform, because a re-seed that left the mesh alone but
 * rewrote the struct behind it would change what Revert To Generated produced - a loss that only
 * shows up long after it happened, which is exactly the kind this flag exists to prevent.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingChangeRespectsHandEditsTest,
	"HouseForge.Editor.ACeilingChangeLeavesHandEditedElementsAlone", HF_TEST_FLAGS)

bool FHFCeilingChangeRespectsHandEditsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeCeilingDependents;

	UWorld* World = GEditor ? GEditor->GetEditorWorldContext().World() : nullptr;
	if (!TestNotNull(TEXT("An editor world is open"), World))
	{
		return false;
	}

	AHFHouseActor* House = SpawnDependentsHouse(World, EHFCeilingTemplate::PlainBand);
	if (!TestNotNull(TEXT("A house builds"), House))
	{
		return false;
	}
	ON_SCOPE_EXIT{ ClearHouseForgeActors(World); };

	AHFFanActor* Extract = nullptr;
	AHFFanActor* CeilingFan = nullptr;
	for (AActor* Element : House->ElementActors)
	{
		if (AHFFanActor* Fan = Cast<AHFFanActor>(Element))
		{
			(Fan->ElementId == FName(TEXT("FAN1")) ? CeilingFan : Extract) = Fan;
		}
	}

	AHFCeilingActor* Ceiling = FindElement<AHFCeilingActor>(House);

	if (!TestNotNull(TEXT("The extract was built"), Extract)
		|| !TestNotNull(TEXT("The ceiling fan was built"), CeilingFan)
		|| !TestNotNull(TEXT("The ceiling was built"), Ceiling))
	{
		return false;
	}

	// Somebody has modelled on the extract and on the ceiling itself.
	Extract->bArtistEdited = true;
	Ceiling->bArtistEdited = true;

	const FTransform ExtractBefore = Extract->GetActorTransform();
	const FHFFanParams ExtractParamsBefore = Extract->Fan;
	const FHFFalseCeiling CeilingBefore = Ceiling->Ceiling;
	const double UntouchedFanRodBefore = CeilingFan->Fan.DropLength;

	// The user changes the design of the ceiling. The hand-edited elements must not move; the one
	// nobody touched must.
	House->Spec.FalseCeilings[0].Template = EHFCeilingTemplate::SteppedTray;
	House->ApplyProjectSettingsToCeilings();

	TestTrue(TEXT("The hand-edited extract did not move"),
		Extract->GetActorTransform().Equals(ExtractBefore, 1e-4));

	TestEqual(TEXT("Its rod length was not re-seeded"),
		Extract->Fan.DropLength, ExtractParamsBefore.DropLength, 1e-6);
	TestEqual(TEXT("Its sweep was not re-seeded"),
		Extract->Fan.SweepDiameter, ExtractParamsBefore.SweepDiameter, 1e-6);
	TestTrue(TEXT("It is still marked as hand-edited"), Extract->bArtistEdited);

	TestEqual(TEXT("The hand-edited ceiling kept its drop"),
		Ceiling->Ceiling.Drop, CeilingBefore.Drop, 1e-6);
	TestEqual(TEXT("It kept its band width too"),
		Ceiling->Ceiling.BandWidth, CeilingBefore.BandWidth, 1e-6);

	// And the mechanism is not simply inert: the fan nobody touched moved, so the test above is
	// about the flag rather than about nothing having happened.
	TestTrue(TEXT("The element that was not hand-edited did follow the change"),
		!FMath::IsNearlyEqual(CeilingFan->Fan.DropLength, UntouchedFanRodBefore, 0.01));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
