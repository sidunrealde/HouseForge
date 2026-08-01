// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMesh3.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFJoineryKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFBuildDefaults.h"
#include "Model/HFSettings.h"
#include "Model/HFSpecValidator.h"
#include "Templates/Function.h"
#include "UObject/UnrealType.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

namespace
{
	/** An opening big enough to be built as a real two-sash sliding window. */
	FHFOpening MakeSlidingWindow()
	{
		FHFOpening Opening;
		Opening.Id = TEXT("Win_Test");
		Opening.WallId = TEXT("W_Test");
		Opening.Kind = EHFOpeningKind::SlidingWindow;
		Opening.Width = 150.0;
		Opening.Height = 120.0;
		Opening.SillHeight = 90.0;
		Opening.OffsetAlongWall = 200.0;
		return Opening;
	}

	FHFWall MakeWall()
	{
		FHFWall Wall;
		Wall.Id = TEXT("W_Test");
		Wall.Start = FVector2D(0.0, 0.0);
		Wall.End = FVector2D(400.0, 0.0);
		Wall.Thickness = 20.0;
		Wall.Height = 300.0;
		return Wall;
	}

	FHFOpening MakeDoor()
	{
		FHFOpening Opening;
		Opening.Id = TEXT("D_Test");
		Opening.WallId = TEXT("W_Test");
		Opening.Kind = EHFOpeningKind::Door;
		Opening.Width = 90.0;
		Opening.Height = 210.0;
		Opening.SillHeight = 0.0;
		Opening.OffsetAlongWall = 200.0;
		Opening.Swing = EHFSwing::InwardLeft;
		return Opening;
	}

	double VolumeOf(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}
}

/**
 * The whole change is a no-op until somebody edits something.
 *
 * Every default on the settings page is the figure that used to be compiled in. If one of them
 * drifts, geometry silently changes for every project that never opened the page - and it would
 * change in a way no other test would attribute to the settings work. Asserted against the named
 * constants that still exist rather than against literals, so a constant and its setting cannot
 * diverge without this failing.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsDefaultsMatchConstantsTest,
	"HouseForge.Settings.DefaultsMatchTheConstantsTheyReplaced", HF_TEST_FLAGS)

bool FHFSettingsDefaultsMatchConstantsTest::RunTest(const FString& Parameters)
{
	const FHFBuildDefaults Defaults;

	// ------------------------------------------------------------------------------ openings
	//
	// Literals here, deliberately: these constants no longer exist anywhere else to compare against,
	// which is the point of the test. They are the figures HFGenerators.cpp carried.

	TestEqual(TEXT("Door leaf thickness"), Defaults.Opening.Door.LeafThickness, 4.0);
	TestEqual(TEXT("Door leaf frame gap"), Defaults.Opening.Door.LeafFrameGap, 0.5);

	// The chowkhat: a 100 x 62 section with a 15 mm check in it. These never were constants in
	// HFGenerators.cpp, because until the reference flat was walked no door had a frame at all.
	TestEqual(TEXT("Door frame depth"), Defaults.Opening.Door.FrameDepth, 10.0);
	TestEqual(TEXT("Door frame face"), Defaults.Opening.Door.FrameFace, 6.2);
	TestEqual(TEXT("Door frame rebate stop"), Defaults.Opening.Door.RebateStop, 1.5);
	TestEqual(TEXT("Door leaf undercut"), Defaults.Opening.Door.LeafUndercut, 1.0);

	// And the sliding door's section, which is not the window's: two 40 mm sashes plus 12 mm of
	// running clearance in a 92 mm frame, glazed at 8 mm rather than a window's 5.
	TestEqual(TEXT("Sliding door frame depth"), Defaults.Opening.SlidingDoor.FrameDepth, 9.2);
	TestEqual(TEXT("Sliding door frame face"), Defaults.Opening.SlidingDoor.FrameFace, 5.5);
	TestEqual(TEXT("Sliding door sash depth"), Defaults.Opening.SlidingDoor.SashDepth, 4.0);
	TestEqual(TEXT("Sliding door track pitch"), Defaults.Opening.SlidingDoor.TrackPitch, 4.6);
	TestEqual(TEXT("Sliding door bottom rail"), Defaults.Opening.SlidingDoor.BottomRailWidth, 10.0);
	TestEqual(TEXT("Sliding door interlock"), Defaults.Opening.SlidingDoor.InterlockOverlap, 3.0);
	TestEqual(TEXT("Sliding door glass thickness"), Defaults.Opening.SlidingDoor.GlassThickness, 0.8);
	TestEqual(TEXT("Sliding door threshold height"), Defaults.Opening.SlidingDoor.ThresholdHeight, 3.0);

	// The section closes: two sashes on that pitch occupy 86 mm and the frame that houses them is 92.
	TestTrue(TEXT("The sliding door's frame contains both its sashes"),
		Defaults.Opening.SlidingDoor.FrameDepth >=
			Defaults.Opening.SlidingDoor.TrackPitch + Defaults.Opening.SlidingDoor.SashDepth);

	TestEqual(TEXT("Sliding window frame depth"), Defaults.Opening.SlidingWindow.FrameDepth, 6.5);
	TestEqual(TEXT("Sliding window frame face"), Defaults.Opening.SlidingWindow.FrameFace, 4.5);
	TestEqual(TEXT("Sash depth"), Defaults.Opening.SlidingWindow.SashDepth, 2.7);
	TestEqual(TEXT("Sash track pitch"), Defaults.Opening.SlidingWindow.TrackPitch, 3.0);
	TestEqual(TEXT("Sash face width"), Defaults.Opening.SlidingWindow.SashFaceWidth, 4.0);
	TestEqual(TEXT("Sash interlock overlap"), Defaults.Opening.SlidingWindow.InterlockOverlap, 2.5);
	TestEqual(TEXT("Sash glass thickness"), Defaults.Opening.SlidingWindow.GlassThickness, 0.5);

	TestEqual(TEXT("Ventilator frame face"), Defaults.Opening.Ventilator.FrameFace, 3.5);
	TestEqual(TEXT("Ventilator open angle"), Defaults.Opening.Ventilator.OpenAngleDegrees, 30.0);

	TestEqual(TEXT("Fixed window frame depth"), Defaults.Opening.FixedWindow.FrameDepth, 6.0);
	TestEqual(TEXT("Fixed window frame face"), Defaults.Opening.FixedWindow.FrameFace, 5.0);
	TestEqual(TEXT("Fixed window glass thickness"), Defaults.Opening.FixedWindow.GlassThickness, 0.8);
	TestEqual(TEXT("Mullion threshold"), Defaults.Opening.FixedWindow.MullionAboveWidth, 120.0);

	// ------------------------------------------------------------------------------- joinery
	//
	// These constants DO still exist, so compare against them rather than against literals. That way
	// the test fails if either side moves, which is the only way the two can be kept honest.

	TestEqual(TEXT("Target shelf spacing"),
		Defaults.Joinery.TargetShelfSpacing, FHFJoineryKit::DefaultTargetShelfSpacing);
	TestEqual(TEXT("Minimum useful compartment"),
		Defaults.Joinery.MinUsefulCompartment, FHFJoineryKit::MinUsefulCompartment);
	TestEqual(TEXT("Minimum hanging clearance"),
		Defaults.Joinery.MinHangingClearance, FHFJoineryKit::MinHangingClearance);
	TestEqual(TEXT("Ply shelf thickness"),
		Defaults.Joinery.ShelfThicknessPly, FHFJoineryKit::PlyShelfThickness);
	TestEqual(TEXT("Glass shelf thickness"),
		Defaults.Joinery.ShelfThicknessGlass, FHFJoineryKit::GlassShelfThickness);
	TestEqual(TEXT("Ply max span"), Defaults.Joinery.MaxShelfSpanPly, FHFJoineryKit::PlyMaxSpan);
	TestEqual(TEXT("Glass max span"), Defaults.Joinery.MaxShelfSpanGlass, FHFJoineryKit::GlassMaxSpan);

	// The parameter structs' own inline defaults are the other half of the same contract: a struct
	// built by hand and a struct stamped from the defaults have to describe the same joinery.
	TestEqual(TEXT("Shutter reveal gap"),
		Defaults.Joinery.ShutterRevealGap, FHFShutterParams().RevealGap);
	TestEqual(TEXT("Shutter leaf thickness"),
		Defaults.Joinery.ShutterLeafThickness, FHFShutterParams().Thickness);
	TestEqual(TEXT("Shutter open angle"),
		Defaults.Joinery.ShutterOpenAngleDegrees, FHFShutterParams().OpenAngleDegrees);
	TestEqual(TEXT("Plinth height"), Defaults.Joinery.PlinthHeight, FHFPlinthParams().Height);
	TestEqual(TEXT("Plinth front recess"),
		Defaults.Joinery.PlinthFrontRecess, FHFPlinthParams().FrontRecess);
	TestEqual(TEXT("Cornice projection"),
		Defaults.Joinery.CorniceProjection, FHFCorniceParams().Projection);
	TestEqual(TEXT("Cornice edge bevel"), Defaults.Joinery.CorniceEdgeBevel, FHFCorniceParams().EdgeBevel);
	TestEqual(TEXT("Hanging rail drop"), Defaults.Joinery.HangingRailDrop, FHFShelfStackParams().RailDrop);

	// The one the user named. The params field and the domain floor must agree, or a shelf stack
	// built by hand would silently use a different threshold from one the settings resolved.
	TestEqual(TEXT("Shelf stack's own hanging clearance matches the domain floor"),
		FHFShelfStackParams().MinHangingClearance, FHFJoineryKit::MinHangingClearance);

	// ---------------------------------------------------------------------------- validation

	TestEqual(TEXT("Minimum headroom"), Defaults.Validation.MinHeadroomCm, 210.0);
	TestEqual(TEXT("Fixture overlap tolerance"), Defaults.Validation.FixtureOverlapToleranceRatio, 0.05);

	// And the settings object itself must resolve to exactly the same thing, or the page would
	// present one set of figures and the build would use another.
	const FHFBuildDefaults Resolved = GetDefault<UHFSettings>()->Resolve();

	TestEqual(TEXT("Resolved door leaf matches the struct default"),
		Resolved.Opening.Door.LeafThickness, Defaults.Opening.Door.LeafThickness);
	TestEqual(TEXT("Resolved hanging clearance matches the struct default"),
		Resolved.Joinery.MinHangingClearance, Defaults.Joinery.MinHangingClearance);
	TestEqual(TEXT("Resolved headroom matches the struct default"),
		Resolved.Validation.MinHeadroomCm, Defaults.Validation.MinHeadroomCm);

	return true;
}

/**
 * An overridden setting has to reach the parameter struct, and reach it by the documented route.
 *
 * The measurable consequence rather than a field comparison: a thicker leaf is more material, and a
 * bigger frame face is less glass. Asserting on volume proves the figure travelled all the way into
 * the geometry, which comparing two doubles would not.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsOverrideReachesGeometryTest,
	"HouseForge.Settings.AnOverriddenValueReachesTheGeometry", HF_TEST_FLAGS)

bool FHFSettingsOverrideReachesGeometryTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FHFOpening Door = MakeDoor();

	// A door leaf at the shipped 40 mm, and the same leaf at 60.
	const FHFBuildDefaults Shipped;

	FHFBuildDefaults Thicker;
	Thicker.Joinery = Shipped.Joinery;
	Thicker.Opening.Door.LeafThickness = 6.0;

	const double ShippedVolume = VolumeOf(FHFGenerators::GenerateDoorLeaf(Door, 1.0, Shipped.Opening.Door));
	const double ThickerVolume = VolumeOf(FHFGenerators::GenerateDoorLeaf(Door, 1.0, Thicker.Opening.Door));

	TestTrue(TEXT("Both leaves have volume"), ShippedVolume > 0.0);

	// A 60 mm leaf is exactly half as much material again as a 40 mm one of the same face size.
	TestEqual(TEXT("A 50% thicker leaf is 50% more material"),
		ThickerVolume, ShippedVolume * 1.5, ShippedVolume * 0.001);

	// The same for a sliding window's frame face, which eats into the clear opening on all four
	// sides: widen it and the sashes get less glass.
	FHFBuildDefaults FatFrame;
	FatFrame.Opening.SlidingWindow.FrameFace = 9.0;

	TArray<FHFMeshPart> ShippedParts;
	FHFGenerators::BuildOpeningParts(MakeSlidingWindow(), Wall, ShippedParts, Shipped.Opening);

	TArray<FHFMeshPart> FatParts;
	FHFGenerators::BuildOpeningParts(MakeSlidingWindow(), Wall, FatParts, FatFrame.Opening);

	if (!TestEqual(TEXT("Both windows built two sashes"), ShippedParts.Num(), 2)
		|| !TestEqual(TEXT("The wider frame also built two sashes"), FatParts.Num(), 2))
	{
		return false;
	}

	// The running sash travels half the clear opening less half the interlock. A wider frame face
	// leaves less clear opening, so the sash has less distance to run.
	const FHFMeshPart* ShippedSash = ShippedParts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.Motion.Type == EHFMotionType::Slide; });
	const FHFMeshPart* FatSash = FatParts.FindByPredicate(
		[](const FHFMeshPart& P) { return P.Motion.Type == EHFMotionType::Slide; });

	if (!TestNotNull(TEXT("The shipped window has a running sash"), ShippedSash)
		|| !TestNotNull(TEXT("The wide-framed window has a running sash"), FatSash))
	{
		return false;
	}

	TestTrue(TEXT("A wider frame face leaves the sash less travel"),
		FatSash->Motion.MaxTravelCm < ShippedSash->Motion.MaxTravelCm);

	// And the arithmetic, so this is a measurement rather than an inequality: clear width is the
	// opening less a frame face each side, the sash takes half of it, and travel is that half less
	// half the interlock.
	const FHFSlidingWindowParams& Wide = FatFrame.Opening.SlidingWindow;
	const double ExpectedTravel =
		Wide.ClearWidth(MakeSlidingWindow().Width) * 0.5 - Wide.InterlockOverlap * 0.5;

	TestEqual(TEXT("The running sash travels far edge to far edge of the clear opening"),
		FatSash->Motion.MaxTravelCm, ExpectedTravel, 0.001);

	return true;
}

/**
 * A parameter struct built by hand is not touched by settings, and a generator never consults them.
 *
 * The architectural guarantee, stated as a test. Whatever the project's settings say, a struct a
 * test filled in itself produces the geometry that struct describes - which is what makes any of
 * this testable headlessly, and is why generators may not read a settings singleton.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsDoNotLeakIntoHandBuiltParamsTest,
	"HouseForge.Settings.HandBuiltParamsAreUnaffectedBySettings", HF_TEST_FLAGS)

bool FHFSettingsDoNotLeakIntoHandBuiltParamsTest::RunTest(const FString& Parameters)
{
	const FHFWall Wall = MakeWall();
	const FHFOpening Door = MakeDoor();

	// Change the project's settings to something absurd, and leave them changed for the duration of
	// the test. Nothing below asks for them, so nothing below may notice.
	UHFSettings* Settings = GetMutableDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The settings CDO exists"), Settings))
	{
		return false;
	}

	const double SavedLeaf = Settings->Door.LeafThickness;
	const double SavedClearance = Settings->MinHangingClearance;

	ON_SCOPE_EXIT
	{
		Settings->Door.LeafThickness = SavedLeaf;
		Settings->MinHangingClearance = SavedClearance;
	};

	Settings->Door.LeafThickness = 25.0;
	Settings->MinHangingClearance = 300.0;

	// A hand-built params struct. It says 4 cm, so the leaf is 4 cm, whatever the page says.
	FHFDoorParams ByHand;
	ByHand.LeafThickness = 4.0;
	ByHand.LeafFrameGap = 0.5;

	const double ByHandVolume = VolumeOf(FHFGenerators::GenerateDoorLeaf(Door, 1.0, ByHand));

	// The leaf of a 900 door hung in a 62 mm frame with a 15 mm check: 47 mm of frame inset and
	// 5 mm of running clearance off each jamb, the same off the head, and a 10 mm undercut at the
	// floor. Spelled out rather than asked of the params, so the arithmetic is asserted and not
	// merely reproduced.
	const double Expected =
		(Door.Width - 2.0 * (6.2 - 1.5) - 2.0 * 0.5) * 4.0 *
		(Door.Height - (6.2 - 1.5) - 0.5 - 1.0);

	TestEqual(TEXT("A hand-built leaf measures what its own params say"),
		ByHandVolume, Expected, Expected * 0.001);

	// The defaulted argument is the same guarantee for a caller that passes nothing at all: it gets
	// the compiled-in figures, NOT whatever the project's settings currently hold.
	const double DefaultedVolume = VolumeOf(FHFGenerators::GenerateDoorLeaf(Door, 1.0));

	TestEqual(TEXT("A generator called with no params at all still uses the compiled-in figures"),
		DefaultedVolume, Expected, Expected * 0.001);

	// Same for the joinery: a shelf stack built by hand keeps its own hanging clearance, so the
	// 300 cm on the settings page cannot reach in and refuse its rail.
	FHFShelfStackParams Bay;
	Bay.Width = 75.0;
	Bay.Depth = 56.3;
	Bay.Height = 200.0;
	Bay.ShelfCount = 0;
	Bay.bHangingRail = true;

	const FHFShelfStackParams Sanitised = FHFJoineryKit::SanitiseShelfStack(Bay);
	TestTrue(TEXT("A hand-built bay keeps the rail its own params asked for"), Sanitised.bHangingRail);

	// And the validator: its limits are an argument, not a lookup.
	FHFValidationLimits Strict;
	Strict.MinHeadroomCm = 260.0;
	TestEqual(TEXT("Validation limits are whatever the caller passes"), Strict.MinHeadroomCm, 260.0);

	return true;
}

/**
 * Raising the hanging clearance is the case the user actually raised.
 *
 * The composed wardrobe's hanging bay clears 90 by 8 mm. Asking for full-length hanging must
 * therefore refuse the rail rather than fit one nothing hangs under - and refusing has to be
 * legible, which is why SanitiseShelfStack reports it by clearing the flag.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsHangingClearanceIsHonouredTest,
	"HouseForge.Settings.RaisingHangingClearanceRefusesAShortBay", HF_TEST_FLAGS)

bool FHFSettingsHangingClearanceIsHonouredTest::RunTest(const FString& Parameters)
{
	// One shelf in a 2 m bay leaves compartments of about 99 cm, so a rail clears the shipped 90.
	FHFShelfStackParams Bay;
	Bay.Width = 75.0;
	Bay.Depth = 56.3;
	Bay.Height = 200.0;
	Bay.ShelfCount = 1;
	Bay.bHangingRail = true;

	const FHFShelfStackParams AtDefault = FHFJoineryKit::SanitiseShelfStack(Bay);
	TestTrue(TEXT("At the shipped 90 the bay takes a rail"), AtDefault.bHangingRail);

	// Now a project that wants full-length garments to hang. The same bay is no longer good enough,
	// and saying so is the correct answer - not a rail with 99 cm under it.
	FHFJoineryDefaults FullLength;
	FullLength.MinHangingClearance = 150.0;

	FHFShelfStackParams Raised = Bay;
	FullLength.ApplyTo(Raised);

	TestEqual(TEXT("ApplyTo stamped the raised clearance onto the params"),
		Raised.MinHangingClearance, 150.0);

	const FHFShelfStackParams AtFullLength = FHFJoineryKit::SanitiseShelfStack(Raised);
	TestFalse(TEXT("At 150 the same bay honestly reports it has no room to hang"),
		AtFullLength.bHangingRail);

	// ApplyTo must not have destroyed the dimensions the composing layer worked out.
	TestEqual(TEXT("ApplyTo left the bay's width alone"), Raised.Width, Bay.Width);
	TestEqual(TEXT("ApplyTo left the bay's height alone"), Raised.Height, Bay.Height);
	TestEqual(TEXT("ApplyTo left the shelf count alone"), Raised.ShelfCount, Bay.ShelfCount);

	// And the sentinel that means "whatever this material is" must survive, or a glass shelf would
	// come out 18 mm thick.
	TestEqual(TEXT("ApplyTo leaves the shelf thickness sentinel alone"), Raised.ShelfThickness, 0.0);
	TestEqual(TEXT("ApplyTo leaves the max span sentinel alone"), Raised.MaxSpan, 0.0);

	return true;
}

/**
 * A raised headroom limit has to change what the validator says, and the defaulted argument has to
 * leave every existing caller alone.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsValidationLimitsTest,
	"HouseForge.Settings.ValidationLimitsAreOverridable", HF_TEST_FLAGS)

bool FHFSettingsValidationLimitsTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Spec;
	Spec.Name = TEXT("Headroom");
	Spec.Units = EHFUnits::Centimeters;
	Spec.UnitsSource = TEXT("test");

	FHFRoom& Room = Spec.Rooms.AddDefaulted_GetRef();
	Room.Id = TEXT("R1");
	Room.Type = EHFRoomType::Bedroom;
	Room.CeilingHeight = 300.0;
	Room.FloorZ = 0.0;
	Room.Boundary = { FVector2D(0, 0), FVector2D(400, 0), FVector2D(400, 350), FVector2D(0, 350) };

	FHFFalseCeiling& Ceiling = Spec.FalseCeilings.AddDefaulted_GetRef();
	Ceiling.Id = TEXT("FC1");
	Ceiling.RoomId = TEXT("R1");

	// 300 - 60 = 240 clear: comfortably above the shipped 210 floor.
	Ceiling.Drop = 60.0;

	TestFalse(TEXT("240 of headroom passes at the shipped limit"),
		FHFSpecValidator::Validate(Spec).Contains(TEXT("LowHeadroom")));

	// A project building to a taller slab wants more than that, and saying so must change the answer.
	FHFValidationLimits Tall;
	Tall.MinHeadroomCm = 250.0;

	TestTrue(TEXT("The same house fails a 250 headroom limit"),
		FHFSpecValidator::Validate(Spec, Tall).Contains(TEXT("LowHeadroom")));

	return true;
}

/**
 * Every figure on the page is visible, and the ones that reach nothing yet say so.
 *
 * Most of Joinery is now wired: AHFWardrobeActor composes the kit into a wardrobe and seeds itself
 * from this page, so twenty-five of these figures move geometry an artist can see. Seven do not, and
 * for a reason rather than an omission - there is no fixture with drawers in it and none that builds
 * a glass bay. Both land in milestone 9.
 *
 * There were three ways to ship those seven and only one of them is honest. Hiding them means the
 * page silently grows a section one day. Shipping them bare means a page that lies to the person
 * dragging the slider. Shipping them marked is the third, and it only stays true while something
 * holds the marking on - and, now that figures are graduating off the list one milestone at a time,
 * while something takes the marking OFF the ones that have.
 *
 * So this walks the page by reflection rather than trusting the header, and names the seven. A new
 * joinery figure with no consumer that ships unmarked fails here; so does one that acquires a
 * consumer and keeps its marking, which is the direction this milestone moved and the direction a
 * marker-presence check on its own could never catch.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsInertOnesAreMarkedTest,
	"HouseForge.Settings.InertControlsAreMarkedOnThePage", HF_TEST_FLAGS)

bool FHFSettingsInertOnesAreMarkedTest::RunTest(const FString& Parameters)
{
	const UHFSettings* Settings = GetDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The settings page is registered"), Settings))
	{
		return false;
	}

	static const FName CategoryKey(TEXT("Category"));
	static const FString Marker(TEXT("no fixture uses these yet"));

	// The seven, by name. A list rather than a count, because the two ways this can go wrong are
	// opposites and a count catches neither: a figure that gained a consumer and kept its label, and
	// a figure that never had one and shipped without.
	//
	// The drawer five have no fixture with drawers in it. A drawer inside a wardrobe is an INTERLOCK -
	// it cannot come out until the leaf in front of it is open, at a threshold somebody has to measure
	// rather than guess - so it lands with the chest and the vanity rather than being bolted on here.
	//
	// The glass two are the shelf board and span that a bay of toughened glass takes. Every shelf in a
	// wardrobe is ply; a crockery unit and a display bay behind a glazed shutter are what want glass.
	static const TSet<FString> ExpectedInert = {
		TEXT("DrawerFrontThickness"),
		TEXT("DrawerBoxSideThickness"),
		TEXT("DrawerBoxBottomThickness"),
		TEXT("DrawerRevealGap"),
		TEXT("DrawerBackClearance"),
		TEXT("ShelfThicknessGlass"),
		TEXT("MaxShelfSpanGlass")
	};

	int32 Controls = 0;
	int32 Joinery = 0;
	TSet<FString> Marked;

	// A struct property is a heading in the details panel; what an artist actually drags is the
	// leaf inside it, so the leaves are what get counted and checked.
	TFunction<void(const UStruct*, const FString&)> Walk =
		[&](const UStruct* Struct, const FString& InheritedCategory)
		{
			for (TFieldIterator<FProperty> It(Struct); It; ++It)
			{
				const FProperty* Property = *It;

				// Only what the page itself exposes. Parameter structs are shared with the
				// generators, whose own members are not all editable here.
				if (!Property->HasAnyPropertyFlags(CPF_Edit))
				{
					continue;
				}

				FString Category = Property->GetMetaData(CategoryKey);
				if (Category.IsEmpty())
				{
					Category = InheritedCategory;
				}

				if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
				{
					Walk(AsStruct->Struct, Category);
					continue;
				}

				++Controls;

				const FString Name = Property->GetName();
				const bool bIsJoinery = Category.StartsWith(TEXT("Joinery"));
				const bool bIsMarked = Category.Contains(Marker);

				Joinery += bIsJoinery ? 1 : 0;
				if (bIsMarked)
				{
					Marked.Add(Name);
				}

				if (bIsMarked && !bIsJoinery)
				{
					AddError(FString::Printf(
						TEXT("'%s' is marked as reaching no fixture, but it sits under '%s' rather than Joinery. Openings and Validation are wired end to end; marking one of those would be telling an artist a value does nothing when it does."),
						*Name, *Category));
				}

				if (bIsMarked && !ExpectedInert.Contains(Name))
				{
					AddError(FString::Printf(
						TEXT("'%s' is marked as reaching no fixture but is not one of the seven that do not. Either a fixture now uses it - in which case take the marking off, because the page is telling an artist their change does nothing when it does - or this list needs it adding."),
						*Name));
				}

				if (!bIsMarked && ExpectedInert.Contains(Name))
				{
					AddError(FString::Printf(
						TEXT("'%s' is one of the figures no fixture uses, but its category '%s' does not say so. An artist dragging it would see no difference and no explanation."),
						*Name, *Category));
				}
			}
		};

	Walk(UHFSettings::StaticClass(), FString());

	AddInfo(FString::Printf(
		TEXT("HouseForge settings page: %d controls, of which %d are joinery figures and %d of those reach no fixture yet."),
		Controls, Joinery, Marked.Num()));

	TestEqual(TEXT("Exactly the figures with no consumer are marked"), Marked.Num(), ExpectedInert.Num());
	TestTrue(TEXT("The joinery section is still on the page rather than hidden"), Joinery > 0);

	// The whole page ships, not a subset of it. A control quietly dropped between milestones is
	// exactly what this number is here to catch.
	//
	// 100 leaves: 8 door + 18 sliding door + 15 sliding window + 12 ventilator + 4 fixed window
	// under Openings, 31 under Joinery, 7 under Fans, and 5 validation limits. The struct properties
	// themselves are headings in the details panel rather than things anybody drags, so they are
	// recursed through, not counted.
	//
	// 31 under Joinery, not 32: PlinthEndRecess is gone. It set a plinth back at every end on show,
	// which put a 5 x 10 cm notch at each end of every wardrobe run in the flat with floor visible
	// under the gable above it. A toe kick is set back at the FRONT and its ends stand on the floor,
	// so the control was removed rather than defaulted to zero - a default is a value somebody can
	// put back, and the geometry is wrong at every value of it but zero.
	//
	// 5, not 3: DoorApproachDepthCm and MinClearPassageCm are what the DoorwayNotClear rule is
	// judged against, and a doorway blockage is a project's own call - how far in front of a door
	// counts as being in the way, and how narrow a gap that project will let somebody squeeze
	// through - so they belong on the page beside MinHeadroomCm rather than compiled in.
	//
	// 100 rather than 79: doors gained the six figures of the chowkhat they are hung in, which did
	// not exist while every door in the flat was a bare leaf in a bare hole; and the two sliding
	// panel figures that used to live under Doors moved into the eighteen of the glazed sliding
	// DOOR section, which is a sliding window with a threshold rather than a pair of boards.
	// 127 rather than 100: the False Ceilings section. Twenty-five numeric figures - the band and
	// the drop for each of the four named designs, the six of the cove section, the four of the
	// downlight fitting with its pitch and setback, and the three that size the ring which buries a
	// beam - plus two switches, bHasLedStrip and bRecessed, which are the two places a design says
	// "not this one" rather than giving a number.
	//
	// This section is why the flat's ceilings stopped being a uniform 500 drop. Every one of those
	// figures used to be a literal: some in FHFSampleHouse, some compiled into HFGenerators, and the
	// cove's three in a struct nothing built anything from.
	TestEqual(TEXT("The page ships every control it did"), Controls, 127);
	TestEqual(TEXT("Every joinery control is still there"), Joinery, 31);

	return true;
}

/**
 * The four shelf figures reach geometry, and reach it as a MATERIAL PAIR.
 *
 * These were the page's last dishonest controls: ShelfThicknessPly, ShelfThicknessGlass,
 * MaxShelfSpanPly and MaxShelfSpanGlass resolved through a zero sentinel that ApplyTo cannot write
 * to, so a project could set them and nothing whatsoever would change. They now travel to the point
 * of resolution as FHFShelfMaterialFigures.
 *
 * The pair is the whole point, and it is what a single "shelf thickness" setting could never have
 * done: the figure that gets used has to follow the material the bay turned out to be, so the same
 * settings object must produce an 18 mm board for a ply stack and an 8 mm one for a glass stack.
 * Both are asserted from ONE set of figures, because a test that checked them one at a time would
 * pass against a stamped-on value that ignores the material.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsShelfFiguresReachGeometryTest,
	"HouseForge.Settings.ShelfMaterialFiguresReachTheGeometry", HF_TEST_FLAGS)

bool FHFSettingsShelfFiguresReachGeometryTest::RunTest(const FString& Parameters)
{
	// A project that builds in thicker board and trusts it further than the kit does.
	FHFJoineryDefaults Project;
	Project.ShelfThicknessPly = 2.5;
	Project.ShelfThicknessGlass = 1.2;
	Project.MaxShelfSpanPly = 120.0;
	Project.MaxShelfSpanGlass = 80.0;

	const FHFShelfMaterialFigures Figures = Project.ShelfFigures();

	FHFShelfStackParams Bay;
	Bay.Width = 75.0;
	Bay.Depth = 56.3;
	Bay.Height = 200.0;
	Bay.ShelfCount = 3;

	// ------------------------------------------------------------------------------- the sentinel
	// ApplyTo still must not touch these two, or the material stops mattering at all.
	FHFShelfStackParams Stamped = Bay;
	Project.ApplyTo(Stamped);
	TestEqual(TEXT("ApplyTo still leaves the thickness sentinel for the material to resolve"),
		Stamped.ShelfThickness, 0.0);
	TestEqual(TEXT("ApplyTo still leaves the span sentinel for the material to resolve"),
		Stamped.MaxSpan, 0.0);

	// ------------------------------------------------------------------------- one pair, two ways
	FHFShelfStackParams PlyBay = Bay;
	PlyBay.ShelfMaterial = EHFShelfMaterial::Ply;

	FHFShelfStackParams GlassBay = Bay;
	GlassBay.ShelfMaterial = EHFShelfMaterial::Glass;

	const FHFShelfStackParams PlyUsed = FHFJoineryKit::SanitiseShelfStack(PlyBay, Figures);
	const FHFShelfStackParams GlassUsed = FHFJoineryKit::SanitiseShelfStack(GlassBay, Figures);

	TestEqual(TEXT("A ply stack takes the project's ply board"), PlyUsed.ShelfThickness, 2.5);
	TestEqual(TEXT("A glass stack takes the project's glass board"), GlassUsed.ShelfThickness, 1.2);
	TestEqual(TEXT("A ply stack takes the project's ply span"), PlyUsed.MaxSpan, 120.0);
	TestEqual(TEXT("A glass stack takes the project's glass span"), GlassUsed.MaxSpan, 80.0);

	// ------------------------------------------------------------------------ and it reaches mesh
	// Resolution that stopped at the sanitiser would still be a setting an artist cannot see, so the
	// figure has to be measurable in the geometry that comes out.
	const double ShippedVolume = VolumeOf(FHFJoineryKit::GenerateShelfStack(PlyBay));
	const double ProjectVolume = VolumeOf(FHFJoineryKit::GenerateShelfStack(PlyBay, Figures));

	TestTrue(TEXT("The shipped stack has volume to compare against"), ShippedVolume > 0.0);
	TestTrue(TEXT("Thicker project board makes a heavier shelf stack"), ProjectVolume > ShippedVolume);

	// A tighter span breaks the stack into more bays, which is the other figure showing up as
	// geometry rather than as a number: 75 wide is one bay at the project's 120 and two at the
	// kit's 90.
	const FHFShelfStackParams AtKitSpan = FHFJoineryKit::SanitiseShelfStack(PlyBay);
	TestEqual(TEXT("The kit's own ply span still stands when nothing is passed"), AtKitSpan.MaxSpan,
		FHFJoineryKit::PlyMaxSpan);

	// --------------------------------------------------------------- nothing passed, nothing moved
	// Every existing caller and every existing test hands over no figures at all, and must keep
	// getting exactly the compiled-in constants.
	const FHFShelfStackParams PlyDefault = FHFJoineryKit::SanitiseShelfStack(PlyBay);
	const FHFShelfStackParams GlassDefault = FHFJoineryKit::SanitiseShelfStack(GlassBay);

	TestEqual(TEXT("With no figures a ply shelf is still 18 mm"), PlyDefault.ShelfThickness,
		FHFJoineryKit::PlyShelfThickness);
	TestEqual(TEXT("With no figures a glass shelf is still 8 mm"), GlassDefault.ShelfThickness,
		FHFJoineryKit::GlassShelfThickness);
	TestEqual(TEXT("With no figures a glass shelf still spans 60"), GlassDefault.MaxSpan,
		FHFJoineryKit::GlassMaxSpan);

	return true;
}

/**
 * The other two shelving figures, which reached nothing at all.
 *
 * TargetShelfSpacing and MinUsefulCompartment were copied out of the settings into
 * FHFJoineryDefaults and then read by nobody. Unlike the four material figures they were not held
 * back by a sentinel - they had nowhere to go: FHFShelfStackParams has no field for either, because
 * neither is a property of a stack. They are the rule for deciding how many shelves a stack gets,
 * applied before there is a stack.
 *
 * Their only consumer, FHFJoineryKit::ShelfCountForClearHeight, falls back to its own compiled-in
 * constants when a caller passes zero, and every caller passed zero. Both dials on the page moved
 * nothing whatsoever, including once fixtures land.
 *
 * The assertion is therefore that CHANGING the setting changes a count. Comparing the setting to the
 * constant it was seeded from - which is what the shipped defaults test does - cannot tell a figure
 * that is wired up from one that is merely stored.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsShelfLadderReachesKitTest,
	"HouseForge.Settings.ShelfLadderFiguresReachTheKit", HF_TEST_FLAGS)

bool FHFSettingsShelfLadderReachesKitTest::RunTest(const FString& Parameters)
{
	// A 200 cm clear bay, which the shipped ladder fills with 4 shelves at 37.5 spacing.
	constexpr double ClearHeight = 200.0;

	FHFJoineryDefaults Shipped;
	const int32 ShippedCount = Shipped.ShelfCountFor(ClearHeight);

	TestEqual(TEXT("The shipped ladder still gives the count it always gave"), ShippedCount,
		FHFJoineryKit::ShelfCountForClearHeight(ClearHeight, FHFJoineryKit::DefaultTargetShelfSpacing,
			FHFJoineryKit::PlyShelfThickness, FHFJoineryKit::MinUsefulCompartment));
	TestTrue(TEXT("And it is a real ladder rather than an empty bay"), ShippedCount > 0);

	// ------------------------------------------------------------------- the spacing has to bite
	// A project that wants shallower compartments gets MORE shelves in the same height. Nothing
	// about this could have passed before: the project figure never left FHFJoineryDefaults.
	FHFJoineryDefaults Tight;
	Tight.TargetShelfSpacing = 25.0;
	const int32 TightCount = Tight.ShelfCountFor(ClearHeight);

	TestTrue(*FString::Printf(TEXT("A 25 cm target gives more shelves than 37.5 (%d vs %d)"),
		TightCount, ShippedCount), TightCount > ShippedCount);

	// And deeper compartments give fewer, so the figure is being used rather than merely perturbing
	// something in one direction.
	FHFJoineryDefaults Loose;
	Loose.TargetShelfSpacing = 60.0;
	const int32 LooseCount = Loose.ShelfCountFor(ClearHeight);

	TestTrue(*FString::Printf(TEXT("A 60 cm target gives fewer shelves than 37.5 (%d vs %d)"),
		LooseCount, ShippedCount), LooseCount < ShippedCount);

	// -------------------------------------------------------- and so does the compartment floor
	// Raising the smallest useful compartment above the target forces the ladder to drop shelves
	// rather than leave slots too shallow to fold a shirt into.
	FHFJoineryDefaults Roomy;
	Roomy.TargetShelfSpacing = 25.0;
	Roomy.MinUsefulCompartment = 45.0;

	TestTrue(*FString::Printf(TEXT("A 45 cm floor overrides a 25 cm target (%d vs %d)"),
		Roomy.ShelfCountFor(ClearHeight), TightCount),
		Roomy.ShelfCountFor(ClearHeight) < TightCount);

	// ------------------------------------------------------------------ the board thickness too
	// Thicker board eats the height the compartments came out of, so a project building in 25 mm
	// ply cannot fit as many as one building in 18.
	FHFJoineryDefaults Thick;
	Thick.ShelfThicknessPly = 6.0;
	TestTrue(TEXT("Thicker project board can only reduce the count"),
		Thick.ShelfCountFor(ClearHeight) <= ShippedCount);

	// A caller that names its own board is answered with that board rather than the project's.
	TestEqual(TEXT("An explicit thickness overrides the project's ply"),
		Shipped.ShelfCountFor(ClearHeight, 6.0), Thick.ShelfCountFor(ClearHeight));

	return true;
}

/**
 * Every number on the page says what unit it is in.
 *
 * This project converts millimetres to centimetres exactly once, at spec ingest, and has been bitten
 * at that boundary before - see .claude/rules/04-conventions.md. A settings page is the one place a
 * figure is typed in by hand rather than converted, so a control whose unit an artist has to guess
 * is how a millimetre gets into centimetre territory. "18" and "1.8" are both plausible board
 * thicknesses; only the tooltip says which this field wants.
 *
 * Enforced rather than audited, because a tooltip is exactly the kind of thing a later milestone
 * adds a control without.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSettingsUnitsAreStatedTest,
	"HouseForge.Settings.EveryControlStatesItsUnit", HF_TEST_FLAGS)

bool FHFSettingsUnitsAreStatedTest::RunTest(const FString& Parameters)
{
#if WITH_EDITORONLY_DATA
	const UHFSettings* Settings = GetDefault<UHFSettings>();
	if (!TestNotNull(TEXT("The settings page is registered"), Settings))
	{
		return false;
	}

	// What counts as naming a unit. Deliberately short: anything not on this list wants a considered
	// addition rather than a looser match, which is the whole point of the guard.
	//
	// It began as lengths, angles and ratios because that was every figure the page had. Fans added
	// two kinds it could not express: a SPEED, which is what an artist knows about a fan and is the
	// figure that turns elapsed time into revolutions, and a COUNT, which is dimensionless but is
	// still a thing a tooltip has to say plainly - "blades", not a bare number.
	static const TArray<FString> Units = {
		TEXT("centimetre"), TEXT("centimeter"), TEXT("millimetre"), TEXT("millimeter"),
		TEXT("degree"), TEXT("ratio"), TEXT("fraction"), TEXT("percent"),
		TEXT("revolution"), TEXT("count")
	};

	int32 Checked = 0;

	TFunction<void(const UStruct*)> Walk = [&](const UStruct* Struct)
	{
		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			const FProperty* Property = *It;
			if (!Property->HasAnyPropertyFlags(CPF_Edit))
			{
				continue;
			}

			if (const FStructProperty* AsStruct = CastField<FStructProperty>(Property))
			{
				Walk(AsStruct->Struct);
				continue;
			}

			// Only numbers carry units. A bool or an enum is its own explanation.
			if (!Property->IsA<FNumericProperty>())
			{
				continue;
			}

			++Checked;

			const FString ToolTip = Property->GetToolTipText().ToString();
			if (ToolTip.IsEmpty())
			{
				AddError(FString::Printf(
					TEXT("'%s' has no tooltip at all, so nothing on the page says what unit it wants."),
					*Property->GetName()));
				continue;
			}

			const bool bNamesAUnit = Units.ContainsByPredicate(
				[&ToolTip](const FString& Unit) { return ToolTip.Contains(Unit); });

			if (!bNamesAUnit)
			{
				AddError(FString::Printf(
					TEXT("'%s' does not name its unit: \"%s\". This project converts mm to cm exactly once, so a figure typed in against an unstated unit is how the two get mixed."),
					*Property->GetName(), *ToolTip.Left(120)));
			}
		}
	};

	Walk(UHFSettings::StaticClass());

	AddInfo(FString::Printf(TEXT("%d numeric controls checked, every one naming its unit."), Checked));

	// The same guard the marking test carries: a walk that silently stopped finding anything would
	// otherwise pass.
	TestEqual(TEXT("Every numeric control on the page was checked"), Checked, 125);
#endif // WITH_EDITORONLY_DATA

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
