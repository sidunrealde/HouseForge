// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFFanActor.h"
#include "Misc/AutomationTest.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFCeilingTemplates.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFTypes.h"

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * NOTHING IN A ROOM MAY BE LEFT INSIDE THE CEILING OVER IT.
 *
 * The flat's ceilings used to drop a uniform 500 everywhere, and everything else in it was set out
 * against the structural slab. Those two facts only agreed by arithmetic accident, and when the depth
 * model changed - a shallow band with a deep ring round the edge - the accident stopped holding.
 * Seven fittings ended up 30 mm inside their own finished ceilings: both bathroom geysers, both
 * bathroom extracts, and all three curtain pelmets. Every one of them validated, every mesh was
 * watertight, and the only evidence was the picture.
 *
 * So these tests ask the question a photograph asks. In CENTIMETRES, which is what the resolver and
 * the generators work in.
 */
namespace
{
	/** A 500 x 400 room under a 300 slab. */
	FHFRoom MakeFitRoom()
	{
		FHFRoom Room;
		Room.Id = TEXT("R_Fit");
		Room.Boundary = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 400), FVector2D(0, 400) };
		Room.FloorZ = 0.0;
		Room.CeilingHeight = 300.0;
		return Room;
	}

	/** The reference flat's beam section: 23 wide, 45 deep, on the room's south edge. */
	FHFBeam MakeFitBeam()
	{
		FHFBeam Beam;
		Beam.Id = TEXT("BM_Fit");
		Beam.Start = FVector2D(0.0, 0.0);
		Beam.End = FVector2D(500.0, 0.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;
		return Beam;
	}

	FHFFalseCeiling MakeFitCeiling(EHFCeilingTemplate Template, const FHFRoom& Room, const FHFBeam* Beam)
	{
		FHFFalseCeiling Ceiling;
		Ceiling.Id = TEXT("FC_Fit");
		Ceiling.RoomId = Room.Id;
		Ceiling.Template = Template;

		// The beam, when there is one, runs along boundary edge 0 - the south wall the fittings
		// under test are fixed to, which is what makes the ring land over them.
		TArray<const FHFBeam*> PerEdge;
		PerEdge.SetNumZeroed(Room.Boundary.Num());
		if (Beam != nullptr && PerEdge.Num() > 0)
		{
			PerEdge[0] = Beam;
		}

		FHFCeilingTemplates::Apply(Ceiling, Room, PerEdge, FHFCeilingDefaults(), 1.0);
		return Ceiling;
	}

	/** A fitting on the room's south wall, at the height and size a drawing would state. */
	FHFFixture MakeWallFixture(const FName& Id, EHFFixtureType Type, double BaseZ, double Height,
		const FVector2D& Footprint = FVector2D(50.0, 20.0))
	{
		FHFFixture Fixture;
		Fixture.Id = Id;
		Fixture.RoomId = TEXT("R_Fit");
		Fixture.Type = Type;
		Fixture.Position = FVector2D(250.0, 10.0);
		Fixture.Footprint = Footprint;
		Fixture.BaseZ = BaseZ;
		Fixture.Height = Height;
		Fixture.AnchorWallId = TEXT("W_South");
		return Fixture;
	}

	FString NameOfTemplate(EHFCeilingTemplate Template)
	{
		return StaticEnum<EHFCeilingTemplate>()->GetNameStringByValue(static_cast<int64>(Template));
	}

	FString NameOfAction(EHFCeilingFitAction Action)
	{
		return StaticEnum<EHFCeilingFitAction>()->GetNameStringByValue(static_cast<int64>(Action));
	}

	/**
	 * What the composing layer would tell the resolver about how big these fittings come out.
	 *
	 * The same question AHFHouseActor::ResolveFixtures asks, and asked the same way. An extract's
	 * bezel is sized to lap the corners of the chase cored behind it, so a fan drawn 250 stands 316
	 * tall - and a test that measured the drawn box would go green while 33 mm of real bezel sat in
	 * the plasterboard, which is what happened.
	 */
	TMap<FName, double> BuiltHeightsFor(const FHFHouseSpec& Spec)
	{
		TMap<FName, double> Out;
		for (const FHFFixture& Fixture : Spec.Fixtures)
		{
			if (Fixture.Type == EHFFixtureType::ExhaustFan)
			{
				Out.Add(Fixture.Id, AHFFanActor::ParamsFor(Fixture).CaseHalfWidth() * 2.0);
			}
		}
		return Out;
	}

	/** How far the built thing reaches above the box the drawing states, at the top. */
	double OverhangOf(const TMap<FName, double>& BuiltHeights, const FHFFixture& Fixture)
	{
		const double* Built = BuiltHeights.Find(Fixture.Id);
		return (Built != nullptr) ? FMath::Max(*Built - Fixture.Height, 0.0) * 0.5 : 0.0;
	}
}

/**
 * The soffit is asked about over the whole FOOTPRINT, not over the middle of it.
 *
 * The level a ceiling hangs at is piecewise constant over zones that are insets of the room boundary,
 * and every one of those zones is measured in tens of centimetres while the fittings are measured in
 * metres. A 2.2 m pelmet on a wall crosses a 30 cm ring and a 45 cm band without its centre ever
 * leaving one of them, so a point query at the fixture's position answers about a level the fitting
 * mostly is not under. The lowest is the one that matters, because that is the one it hits.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFitSpansTheFootprintTest,
	"HouseForge.Ceiling.FitAsksAboutTheWholeFootprint", HF_TEST_FLAGS)

bool FHFCeilingFitSpansTheFootprintTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeFitRoom();
	const FHFBeam Beam = MakeFitBeam();
	const FHFFalseCeiling Ceiling = MakeFitCeiling(EHFCeilingTemplate::Cove, Room, &Beam);

	if (!TestTrue(TEXT("The cove carries a perimeter ring to bury the beam"), Ceiling.HasPerimeterBulkhead()))
	{
		return false;
	}

	const double RingSoffitZ = Room.CeilingHeight - Ceiling.PerimeterBulkheadDrop;
	const double BandSoffitZ = Room.CeilingHeight - Ceiling.Drop;

	// A fitting standing well clear of the ring in plan, but reaching into it at one end. Its CENTRE
	// is under the shallow band; the deep ring is over its near edge only.
	FHFFixture Straddler;
	Straddler.Id = TEXT("F_Straddler");
	Straddler.RoomId = Room.Id;
	Straddler.Type = EHFFixtureType::Pelmet;
	Straddler.Footprint = FVector2D(100.0, 80.0);
	Straddler.Position = FVector2D(250.0, Ceiling.PerimeterBulkheadWidth);
	Straddler.BaseZ = 0.0;
	Straddler.Height = 10.0;

	const double Lowest = FHFCeilingFit::LowestSoffitZOver(Straddler, Room, { Ceiling });

	AddInfo(FString::Printf(TEXT("Ring soffit %.1f, band soffit %.1f, lowest over the footprint %.1f."),
		RingSoffitZ, BandSoffitZ, Lowest));

	TestEqual(TEXT("The lowest soffit over the footprint is the ring's, not the band's over its centre"),
		Lowest, RingSoffitZ, 0.01);

	// And a fitting genuinely clear of the ring gets the shallower answer, or the test above would
	// pass just as well against a function that always returned the deepest thing in the room.
	FHFFixture Inboard = Straddler;
	Inboard.Id = TEXT("F_Inboard");
	Inboard.Position = FVector2D(250.0, 200.0);

	TestEqual(TEXT("A fitting clear of the ring is measured against what is actually over it"),
		FHFCeilingFit::LowestSoffitZOver(Inboard, Room, { Ceiling }),
		Room.CeilingHeight, 0.01);

	return true;
}

/**
 * Each kind of thing gives in the way that kind of thing gives.
 *
 * A bought extract cannot be made shorter and a wardrobe cannot be lifted off the floor, so one rule
 * for everything would be wrong for almost everything. Asserted on the three that do something and on
 * the outcome when nothing can be done, because a resolver that silently fudged the last case would
 * be hiding a design fault rather than reporting it.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFitRulesTest,
	"HouseForge.Ceiling.FitGivesInTheRightWay", HF_TEST_FLAGS)

bool FHFCeilingFitRulesTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeFitRoom();

	// A full drop at 48, which is what the flat's wet areas and its perimeter rings are.
	FHFFalseCeiling Ceiling;
	Ceiling.Id = TEXT("FC_Full");
	Ceiling.RoomId = Room.Id;
	Ceiling.Style = EHFCeilingStyle::FullDrop;
	Ceiling.Drop = 48.0;

	const double SoffitZ = Room.CeilingHeight - Ceiling.Drop;
	constexpr double Clearance = 1.0;

	// ------------------------------------------------------------------------------------ lowers
	{
		// The reference flat's bathroom extract: 230 to 255, under a 252 soffit.
		const FHFFixture Extract = MakeWallFixture(TEXT("F_Exhaust"), EHFFixtureType::ExhaustFan, 230.0, 25.0);
		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Extract, Room, { Ceiling }, Clearance);

		TestEqual(TEXT("An extract keeps its size"), Result.Height, Extract.Height, 0.001);
		TestEqual(TEXT("An extract slides down"), Result.Action, EHFCeilingFitAction::Lowered);
		TestEqual(TEXT("Its head lands exactly the clearance below the soffit"),
			Result.BaseZ + Result.Height, SoffitZ - Clearance, 0.001);
		TestTrue(TEXT("It moved downward, never up"), Result.BaseZ < Extract.BaseZ);
	}

	// -------------------------------------------------------- and the box the drawing states is not
	//                                                            always the object that gets built
	{
		// A fan drawn 250 tall whose bezel comes out 316, centred on what was drawn. Fitting the
		// drawn box leaves 33 mm of real bezel inside the plasterboard - which is exactly what the
		// first version of this resolver did, and it went green on every model-level assertion in
		// this file. It was found by rendering the room.
		const FHFFixture Extract = MakeWallFixture(TEXT("F_Bezel"), EHFFixtureType::ExhaustFan, 230.0, 25.0);
		constexpr double BuiltHeight = 31.6;
		constexpr double Overhang = (BuiltHeight - 25.0) * 0.5;

		const FHFCeilingFitResult Result =
			FHFCeilingFit::Fit(Extract, Room, { Ceiling }, Clearance, BuiltHeight);

		TestEqual(TEXT("The BUILT head lands the clearance below the soffit, not the drawn one"),
			Result.BaseZ + Result.Height + Overhang, SoffitZ - Clearance, 0.001);

		// And it really is lower than fitting the drawn box would have put it, or the assertion above
		// would pass against a resolver that ignored the figure entirely.
		TestTrue(TEXT("Allowing for the bezel puts it lower than the drawn box would have"),
			Result.BaseZ < FHFCeilingFit::Fit(Extract, Room, { Ceiling }, Clearance).BaseZ - 0.001);
	}

	// -------------------------------------------------------------------------------- shortens
	{
		// A wardrobe taller than the room now allows. A carpenter cuts the carcass; he does not dig.
		const FHFFixture Wardrobe = MakeWallFixture(TEXT("F_Wardrobe"), EHFFixtureType::Wardrobe,
			0.0, 260.0, FVector2D(240.0, 60.0));
		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Wardrobe, Room, { Ceiling }, Clearance);

		TestEqual(TEXT("A wardrobe stays on the floor"), Result.BaseZ, 0.0, 0.001);
		TestEqual(TEXT("A wardrobe is cut down"), Result.Action, EHFCeilingFitAction::Shortened);
		TestEqual(TEXT("Its top lands exactly the clearance below the soffit"),
			Result.BaseZ + Result.Height, SoffitZ - Clearance, 0.001);
	}

	// ---------------------------------------------------------------------------- hangs on a rod
	{
		// The fan's rod already solves this, and the fit must not solve it a second way: two
		// mechanisms lowering the same fan would lower it twice.
		FHFFixture Fan = MakeWallFixture(TEXT("F_Fan"), EHFFixtureType::CeilingFan, 30.0, 30.0,
			FVector2D(120.0, 120.0));
		Fan.Position = FVector2D(250.0, 200.0);
		Fan.AnchorWallId = FName();

		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Fan, Room, { Ceiling }, Clearance);

		TestEqual(TEXT("A ceiling fan is left to its rod"), Result.Rule, EHFCeilingFitRule::HangsOnARod);
		TestEqual(TEXT("Nothing is done to it here"), Result.Action, EHFCeilingFitAction::Unchanged);
		TestEqual(TEXT("Its base is untouched"), Result.BaseZ, Fan.BaseZ, 0.001);
	}

	// ------------------------------------------------------------------------ hangs from the soffit
	{
		// BaseZ on a ceiling-mounted fixture is a drop below the ceiling, and this is the rule that
		// makes "the ceiling" the finished soffit rather than the slab it is suspended from.
		FHFFixture Light = MakeWallFixture(TEXT("F_Light"), EHFFixtureType::LightFixture, 10.0, 10.0,
			FVector2D(40.0, 40.0));
		Light.Position = FVector2D(250.0, 200.0);
		Light.AnchorWallId = FName();

		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Light, Room, { Ceiling }, Clearance);

		TestEqual(TEXT("A surface fitting takes the soffit as its datum"), Result.Action,
			EHFCeilingFitAction::Rehung);
		TestEqual(TEXT("The soffit it is measured from is the finished one"), Result.SoffitZ, SoffitZ, 0.001);
		TestEqual(TEXT("Its own figures are untouched: the datum moved, not the fitting"),
			Result.BaseZ, Light.BaseZ, 0.001);
	}

	// -------------------------------------------------------------------------------- refused
	{
		// A ceiling low enough that there is not as much wall left below it as the fitting is tall.
		// There is no honest answer - lowering it would sink it into the floor - and the resolver must
		// not invent one: the fitting stays where it was drawn and says what it could not give.
		FHFFalseCeiling Crushing = Ceiling;
		Crushing.Drop = 265.0;

		const FHFFixture Geyser = MakeWallFixture(TEXT("F_Geyser"), EHFFixtureType::Geyser, 210.0, 45.0);
		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Geyser, Room, { Crushing }, Clearance);

		TestEqual(TEXT("A fitting with nowhere to go is refused, not fudged"), Result.Action,
			EHFCeilingFitAction::Refused);
		TestEqual(TEXT("It is left exactly as the drawing put it"), Result.BaseZ, Geyser.BaseZ, 0.001);
		TestEqual(TEXT("It keeps its size too"), Result.Height, Geyser.Height, 0.001);
		TestTrue(TEXT("The shortfall is stated rather than swallowed"), Result.Shortfall > 0.0);

		AddInfo(FHFCeilingFit::Describe(Geyser, Result));
	}

	// ------------------------------------------------------------------------------- ignores
	{
		// A bed under a 252 soffit needs no resolution at all, and a resolver that moved it would be
		// worse than one that did nothing.
		const FHFFixture Bed = MakeWallFixture(TEXT("F_Bed"), EHFFixtureType::Bed, 0.0, 60.0,
			FVector2D(180.0, 200.0));
		const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Bed, Room, { Ceiling }, Clearance);

		TestEqual(TEXT("Furniture is left alone"), Result.Action, EHFCeilingFitAction::Unchanged);
		TestEqual(TEXT("Its height is untouched"), Result.Height, Bed.Height, 0.001);
	}

	return true;
}

/**
 * A fitting that already clears is not moved a millimetre.
 *
 * The resolver runs over every fixture in the flat on every build, so "does nothing when nothing is
 * needed" is the property that keeps it from being a slow way to nudge the whole house. It is also
 * what makes the build log worth reading: a report that named all sixty-nine fixtures every time
 * would name none of them usefully.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFitIsIdempotentTest,
	"HouseForge.Ceiling.FitLeavesWhatAlreadyFits", HF_TEST_FLAGS)

bool FHFCeilingFitIsIdempotentTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	const TMap<FName, double> BuiltHeights = BuiltHeightsFor(Spec);

	TArray<FString> Moved;
	const TArray<FHFFixture> Once = FHFCeilingFit::FitAll(Spec, 1.0, &BuiltHeights, &Moved);

	if (!TestEqual(TEXT("Every fixture comes back, in order"), Once.Num(), Spec.Fixtures.Num()))
	{
		return false;
	}

	for (const FString& Line : Moved)
	{
		AddInfo(Line);
	}

	TestTrue(TEXT("Something in the reference flat actually needed fitting"), Moved.Num() > 0);
	TestTrue(TEXT("Most of the flat did not"), Moved.Num() < Spec.Fixtures.Num() / 2);

	// Feeding the answer back in must produce the answer again. Without that, a build that ran the
	// resolver twice - which the settings page does every time it re-seeds - would walk a pelmet down
	// the wall a centimetre at a time.
	FHFHouseSpec Refitted = Spec;
	Refitted.Fixtures = Once;

	const TArray<FHFFixture> Twice = FHFCeilingFit::FitAll(Refitted, 1.0, &BuiltHeights, nullptr);

	int32 Drifted = 0;
	for (int32 Index = 0; Index < Once.Num(); ++Index)
	{
		if (!FMath::IsNearlyEqual(Once[Index].BaseZ, Twice[Index].BaseZ, 0.001)
			|| !FMath::IsNearlyEqual(Once[Index].Height, Twice[Index].Height, 0.001))
		{
			++Drifted;
			AddError(FString::Printf(
				TEXT("'%s' moved again on the second pass: base %.3f then %.3f, height %.3f then %.3f."),
				*Once[Index].Id.ToString(), Once[Index].BaseZ, Twice[Index].BaseZ,
				Once[Index].Height, Twice[Index].Height));
		}
	}

	TestEqual(TEXT("Fitting an already-fitted flat changes nothing"), Drifted, 0);

	return true;
}

/**
 * NO FIXTURE IN THE FLAT IS LEFT INSIDE A CEILING, UNDER ANY OF THE FOUR TEMPLATES.
 *
 * The whole-flat assertion, and the one that would have caught the seven fittings the user found.
 * Every room is given each named design in turn - which is exactly what somebody choosing a ceiling
 * does - and every fixture in the flat is then measured against the soffit that ends up over it.
 *
 * Over the FOOTPRINT rather than the position, because that is where the failures were: a pelmet on
 * a wall is under the perimeter ring for its whole length, and its centre is under the ring too, so
 * a point test would have caught these - but a run of wall units under a band would not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFitWholeFlatTest,
	"HouseForge.Ceiling.NothingIsLeftInsideACeiling", HF_TEST_FLAGS)

bool FHFCeilingFitWholeFlatTest::RunTest(const FString& Parameters)
{
	const EHFCeilingTemplate Templates[] =
	{
		EHFCeilingTemplate::PlainBand,
		EHFCeilingTemplate::Cove,
		EHFCeilingTemplate::SteppedTray,
		EHFCeilingTemplate::FramedPanel
	};

	constexpr double Clearance = 1.0;

	for (EHFCeilingTemplate Template : Templates)
	{
		FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
		FHFUnits::ConvertToCentimeters(Spec);

		// EVERY room, including the wet areas that ship as hand-tuned full drops. A user picking a
		// design from the settings page is not asked which rooms are allowed to have one.
		for (FHFFalseCeiling& Ceiling : Spec.FalseCeilings)
		{
			Ceiling.Template = Template;

			// The corridor's bulkhead follows its own polygon; a named design follows the room. The
			// template is what decides the style, so nothing needs setting here but the outline.
			Ceiling.ExplicitPolygon.Reset();
		}

		FHFCeilingTemplates::Apply(Spec, FHFCeilingDefaults());

		const TMap<FName, double> BuiltHeights = BuiltHeightsFor(Spec);
		const TArray<FHFFixture> Fitted = FHFCeilingFit::FitAll(Spec, Clearance, &BuiltHeights, nullptr);

		int32 Buried = 0;
		int32 Refused = 0;

		for (int32 Index = 0; Index < Fitted.Num(); ++Index)
		{
			const FHFFixture& Fixture = Fitted[Index];

			const FHFRoom* Room = Spec.FindRoom(Fixture.RoomId);
			if (Room == nullptr || Fixture.IsCeilingMounted())
			{
				// A hanging fixture is placed from the ceiling rather than measured against it, and
				// its own mechanism - the rod, or the soffit datum - is asserted separately.
				continue;
			}

			const FHFCeilingFitResult Result = FHFCeilingFit::Fit(Spec.Fixtures[Index], *Room,
				Spec.FalseCeilings, Clearance, OverhangOf(BuiltHeights, Fixture) * 2.0 + Fixture.Height);

			if (Result.Action == EHFCeilingFitAction::Refused)
			{
				// Reported honestly rather than silently fixed, which is the correct outcome - but it
				// must stay a thing that does not happen in the reference flat.
				++Refused;
				AddError(FString::Printf(TEXT("[%s] %s"), *NameOfTemplate(Template),
					*FHFCeilingFit::Describe(Spec.Fixtures[Index], Result)));
				continue;
			}

			// The head of what is BUILT, bezel and all - not of the box the drawing states.
			const double TopZ = Room->FloorZ + Fixture.BaseZ + Fixture.Height
				+ OverhangOf(BuiltHeights, Fixture);
			const double SoffitZ = FHFCeilingFit::LowestSoffitZOver(Fixture, *Room, Spec.FalseCeilings);

			if (TopZ > SoffitZ + 0.001)
			{
				++Buried;
				AddError(FString::Printf(
					TEXT("[%s] '%s' in room '%s' reaches %.1f but the lowest soffit over it is %.1f - %.1f inside the ceiling. Its rule is %s and the fit reported %s."),
					*NameOfTemplate(Template), *Fixture.Id.ToString(), *Room->Id.ToString(),
					TopZ, SoffitZ, TopZ - SoffitZ,
					*StaticEnum<EHFCeilingFitRule>()->GetNameStringByValue(
						static_cast<int64>(FHFCeilingFit::RuleFor(Fixture.Type))),
					*NameOfAction(Result.Action)));
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d fixtures, %d left inside a ceiling, %d that do not fit at all."),
			*NameOfTemplate(Template), Fitted.Num(), Buried, Refused));

		TestEqual(FString::Printf(TEXT("[%s] nothing is left inside a ceiling"), *NameOfTemplate(Template)),
			Buried, 0);
		TestEqual(FString::Printf(TEXT("[%s] everything in the flat can be made to fit"), *NameOfTemplate(Template)),
			Refused, 0);
	}

	return true;
}

/**
 * THE SEVEN THE USER FOUND, named, so this cannot regress quietly.
 *
 * The flat as committed put both bathroom geysers, both bathroom extracts and all three curtain
 * pelmets 30 mm inside their own ceilings. A general "nothing is buried" test would go green if
 * somebody deleted the pelmets, so the fittings are named and the outcome is asserted on each.
 *
 * FOUR OF THE SEVEN NO LONGER FOUL ANYTHING, and that is the shallow ceilings arriving rather than
 * this test decaying. F_MBath_Exhaust, F_Pelmet_MBed and F_Pelmet_Bed2 are drawn at heads of 2550
 * to 2583 under soffits that used to sit at 2520 and now sit at 2750 to 2850. The premise clause
 * that used to read "this really is buried" is exactly what caught the change - it was written to -
 * so it becomes the sharper question: a fitting that fouls must be lowered, and a fitting that does
 * NOT foul must be left precisely where the drawing put it. The second half is the one that would
 * catch a resolver quietly lowering everything.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingFitReportedFittingsTest,
	"HouseForge.Ceiling.TheFittingsThatWereClippingClearNow", HF_TEST_FLAGS)

bool FHFCeilingFitReportedFittingsTest::RunTest(const FString& Parameters)
{
	const TCHAR* Reported[] =
	{
		TEXT("F_CBath_Geyser"), TEXT("F_MBath_Geyser"),
		TEXT("F_CBath_Exhaust"), TEXT("F_MBath_Exhaust"),
		TEXT("F_Pelmet_Living"), TEXT("F_Pelmet_MBed"), TEXT("F_Pelmet_Bed2")
	};

	FHFHouseSpec Spec = FHFSampleHouse::Make2BHK();
	FHFUnits::ConvertToCentimeters(Spec);

	const TMap<FName, double> BuiltHeights = BuiltHeightsFor(Spec);
	const TArray<FHFFixture> Fitted = FHFCeilingFit::FitAll(Spec, 1.0, &BuiltHeights, nullptr);

	int32 StillFouls = 0;
	int32 Clears = 0;

	for (const TCHAR* Id : Reported)
	{
		const FName Name(Id);

		const int32 Index = Spec.Fixtures.IndexOfByPredicate(
			[&Name](const FHFFixture& Fixture) { return Fixture.Id == Name; });

		if (!TestTrue(FString::Printf(TEXT("The flat still has '%s'"), Id), Index != INDEX_NONE))
		{
			continue;
		}

		const FHFFixture& Drawn = Spec.Fixtures[Index];
		const FHFRoom* Room = Spec.FindRoom(Drawn.RoomId);
		if (!TestNotNull(TEXT("It is in a room"), Room))
		{
			continue;
		}

		const double Overhang = OverhangOf(BuiltHeights, Drawn);
		const double SoffitZ = FHFCeilingFit::LowestSoffitZOver(Drawn, *Room, Spec.FalseCeilings);
		const double DrawnTopZ = Room->FloorZ + Drawn.BaseZ + Drawn.Height + Overhang;
		const double FittedTopZ = Room->FloorZ + Fitted[Index].BaseZ + Fitted[Index].Height + Overhang;

		AddInfo(FString::Printf(TEXT("%s: drawn head %.1f, soffit %.1f, built head %.1f."),
			Id, DrawnTopZ, SoffitZ, FittedTopZ));

		TestTrue(FString::Printf(TEXT("'%s' as built clears it"), Id), FittedTopZ <= SoffitZ + 0.001);

		if (DrawnTopZ > SoffitZ)
		{
			++StillFouls;

			// It had to move, so it must actually have moved.
			TestTrue(FString::Printf(TEXT("'%s' was lowered to clear its ceiling"), Id),
				FittedTopZ < DrawnTopZ - 0.001);
		}
		else
		{
			++Clears;

			// AND IT MUST NOT HAVE MOVED. A resolver that lowered everything by the clearance
			// would satisfy every other assertion here and would quietly drop three fittings in
			// the reference flat away from the height the drawing states.
			TestEqual(FString::Printf(TEXT("'%s' fits as drawn and is left where it was drawn"), Id),
				FittedTopZ, DrawnTopZ, 0.001);
		}

		// And it kept its size. An extract that fitted by being made smaller would be a different
		// extract from the one that was bought.
		TestEqual(FString::Printf(TEXT("'%s' is the same fitting, just lower"), Id),
			Fitted[Index].Height, Drawn.Height, 0.001);
	}

	AddInfo(FString::Printf(
		TEXT("Of the seven reported fittings, %d still foul their ceiling and are lowered; "
			 "%d now clear it as drawn, because the ceilings above them got shallower."),
		StillFouls, Clears));

	// The mechanism still has work to do in this flat. If every fitting cleared, this test would be
	// asserting nothing about the resolver and somebody should be told rather than left with a
	// green run.
	TestTrue(TEXT("The reference flat still exercises the fit"), StillFouls > 0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
