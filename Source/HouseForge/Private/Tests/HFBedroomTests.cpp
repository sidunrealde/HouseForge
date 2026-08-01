// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFCasedGoodsActor.h"
#include "Actors/HFFurnitureActors.h"
#include "Geometry/HFBedKit.h"
#include "Geometry/HFCasedGoodsKit.h"
#include "Geometry/HFDeskKit.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The bedroom group: the bed and the desk as new kits, and the three that are recipes on the cased
// goods kit. Measured on volume, bounds, roles, clearances and swept travel in centimetres, and
// never on a triangle count - see .claude/rules and the milestone brief. A count changes the moment
// anything is chamfered and says nothing about whether the geometry is right.
//
// The motion assertions here are written against the trap the wardrobe fell into: a part that moves
// its full declared distance and opens nothing. For a tilt-out flap that means measuring the
// APERTURE the flap uncovers at the head of its compartment, in centimetres, and not merely that the
// leaf turned.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** True when every triangle in the mesh carries a polygroup that maps back to a surface role. */
	bool EveryTriangleHasARole(const FDynamicMesh3& Mesh)
	{
		if (Mesh.TriangleCount() == 0)
		{
			return false;
		}

		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			const int32 Group = Mesh.GetTriangleGroup(Tri);
			if (Group <= 0 || Group > FHFMeshOps::NumSurfaceRoles())
			{
				return false;
			}
		}
		return true;
	}

	/** Where a point given in a part's local space ends up, in the actor, at a given open amount. */
	FVector PosedPoint(const FHFMeshPart& Part, const FVector& LocalPoint, double OpenAmount)
	{
		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		return State.CurrentPose().TransformPosition(LocalPoint);
	}

	/** The whole of a part's geometry, swept to a given open amount, as a box in actor space. */
	FBox PosedBounds(const FHFMeshPart& Part, double OpenAmount)
	{
		FBox Out(ForceInit);

		FHFPartState State;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = OpenAmount;

		const FTransform Pose = State.CurrentPose();

		for (const int32 Vertex : Part.Mesh.VertexIndicesItr())
		{
			Out += Pose.TransformPosition(FVector(Part.Mesh.GetVertex(Vertex)));
		}

		return Out;
	}

	/** True when any triangle of the mesh has its centroid inside the box. */
	bool AnythingInside(const FDynamicMesh3& Mesh, const FBox& Box)
	{
		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tri, A, B, C);

			if (Box.IsInsideOrOn(FVector((A + B + C) / 3.0)))
			{
				return true;
			}
		}
		return false;
	}

	// ------------------------------------------------------ the fixtures, exactly as the flat has them

	FHFFixture MakeFixture(const TCHAR* Id, EHFFixtureType Type, const FVector2D& Footprint, double Height)
	{
		FHFFixture F;
		F.Id = Id;
		F.RoomId = TEXT("R_Test");
		F.Type = Type;
		F.Footprint = Footprint;
		F.Height = Height;
		return F;
	}

	/** F_MBed_Bed: a king bed, 1800 x 2000, mattress top at 600. In centimetres. */
	FHFBedParams MakeKingBed()
	{
		FHFBedParams P;
		P.Width = 180.0;
		P.Depth = 200.0;
		P.MattressTopZ = 60.0;
		return FHFBedKit::Sanitise(P);
	}

	/** F_Bed2_Study: 1200 x 550 at 750, two drawers, standing clear of an 18 mm skirting. */
	FHFDeskParams MakeStudyTable()
	{
		FHFDeskParams P;
		P.Width = 120.0;
		P.Depth = 55.0;
		P.Height = 75.0;
		P.DrawerCount = 2;
		P.SupportSetback = 2.0;
		return FHFDeskKit::Sanitise(P);
	}

	/** F_ShoeRack: 1200 x 350 x 900, two tilt-out tiers, three divisions, on an 80 plinth. */
	FHFCasedGoodsParams MakeShoeRack()
	{
		FHFFixture F = MakeFixture(TEXT("F_ShoeRack"), EHFFixtureType::ShoeRack,
			FVector2D(120.0, 35.0), 90.0);
		F.Params.ShutterCount = 2;
		F.Params.ShelfCount = 3;
		F.Params.PlinthHeight = 8.0;
		F.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;

		return AHFCasedGoodsActor::ParamsFor(F);
	}

	/** F_MBed_Night1: 450 x 400 x 550, two drawers on an 80 plinth. */
	FHFCasedGoodsParams MakeNightstand()
	{
		FHFFixture F = MakeFixture(TEXT("F_MBed_Night1"), EHFFixtureType::Nightstand,
			FVector2D(45.0, 40.0), 55.0);
		F.Params.DrawerCount = 2;
		F.Params.PlinthHeight = 8.0;

		return AHFCasedGoodsActor::ParamsFor(F);
	}

	/** F_TVUnit_E: the console, 1200 x 450 x 600, three drawers on an 80 plinth. */
	FHFCasedGoodsParams MakeTVConsole()
	{
		FHFFixture F = MakeFixture(TEXT("F_TVUnit_E"), EHFFixtureType::TVUnit,
			FVector2D(120.0, 45.0), 60.0);
		F.Params.DrawerCount = 3;
		F.Params.PlinthHeight = 8.0;
		F.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;

		return AHFCasedGoodsActor::ParamsFor(F);
	}

	/** F_TVUnit_W: the tall storage column, 900 x 450 x 1800, two bays of four shelves. */
	FHFCasedGoodsParams MakeTVColumn()
	{
		FHFFixture F = MakeFixture(TEXT("F_TVUnit_W"), EHFFixtureType::TVUnit,
			FVector2D(90.0, 45.0), 180.0);
		F.Params.ShutterCount = 2;
		F.Params.ShelfCount = 4;
		F.Params.PlinthHeight = 8.0;
		F.Params.HandleStyle = EHFHandleStyle::HandlelessGroove;

		return AHFCasedGoodsActor::ParamsFor(F);
	}
}

// ============================================================================================ bed

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedIsThreeThingsTest, "HouseForge.Bedroom.ABedIsNotOneBox",
	HF_TEST_FLAGS)

bool FHFBedIsThreeThingsTest::RunTest(const FString& Parameters)
{
	const FHFBedBuild Built = FHFBedKit::Build(MakeKingBed());

	TestTrue(TEXT("A bed builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// ------------------------------------------------------------------- the drawn box IS the object

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("It is the drawn width"), Bounds.Max.X - Bounds.Min.X, 180.0, 0.01);

	// AND THE DRAWN DEPTH, headboard included. This is the assertion that stops the head of every bed
	// in the flat standing in the wall behind it: the headboard is built INSIDE the footprint the
	// drawing gave, not added on the back of it.
	TestNearlyEqual(TEXT("It is the drawn depth, headboard and all"),
		Bounds.Max.Y - Bounds.Min.Y, 200.0, 0.01);
	TestNearlyEqual(TEXT("Nothing stands behind the drawn back plane"), Bounds.Max.Y, 200.0, 0.01);

	// The drawn HEIGHT is the mattress top and the built height is the headboard, which is the whole
	// reason AHFBedActor::ParamsFor has to hand a built envelope to FHFCeilingFit.
	TestNearlyEqual(TEXT("The mattress top is where the drawing put it"),
		Built.Mattress.GetBounds().Max.Z, 60.0, 0.01);
	TestNearlyEqual(TEXT("But the object stands as tall as its headboard"), Bounds.Max.Z, 105.0, 0.01);

	// ------------------------------------------------------------- three surfaces, three materials
	//
	// THE POINT OF THE KIT. A bed drawn as one solid in one material is the most conspicuous
	// placeholder a bedroom can hold, and no amount of correct dimensioning rescues it - so what is
	// asserted is not that a bed was built but that three DIFFERENT materials meet on it.

	const TSet<EHFSurfaceRole> Present = FHFMeshOps::RolesPresent(Built.Shell);

	TestTrue(TEXT("The frame is carcass"), Present.Contains(EHFSurfaceRole::JoineryCarcass));
	TestTrue(TEXT("The headboard is a faced panel"), Present.Contains(EHFSurfaceRole::ShutterLaminate));
	TestTrue(TEXT("The mattress is soft"), Present.Contains(EHFSurfaceRole::Fabric));

	// And each sub-assembly carries its own, rather than the shell merely containing all three
	// somewhere: a mattress tagged as carcass renders in cabinet laminate and passes the test above.
	TestTrue(TEXT("The mattress itself is fabric and nothing else"),
		FHFMeshOps::RolesPresent(Built.Mattress).Num() == 1
		&& FHFMeshOps::RolesPresent(Built.Mattress).Contains(EHFSurfaceRole::Fabric));
	TestTrue(TEXT("The frame itself is carcass and nothing else"),
		FHFMeshOps::RolesPresent(Built.Frame).Num() == 1
		&& FHFMeshOps::RolesPresent(Built.Frame).Contains(EHFSurfaceRole::JoineryCarcass));

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedMattressOversailsTest,
	"HouseForge.Bedroom.TheMattressOversailsTheFrame", HF_TEST_FLAGS)

bool FHFBedMattressOversailsTest::RunTest(const FString& Parameters)
{
	// THE ONE MEASUREMENT THAT DECIDES WHETHER A BED READS AS A BED. Built flush, the mattress and
	// the box under it share a silhouette and the bed becomes a crate - correct in every dimension
	// and legible as nothing. The shadow line along the overhang is what separates them from across a
	// room, and it is a clearance BETWEEN two solids, so it stops being measurable the moment they
	// are merged. That is why FHFBedBuild keeps them apart.
	const FHFBedBuild Built = FHFBedKit::Build(MakeKingBed());
	TestTrue(TEXT("A bed builds"), Built.bValid);

	const FAxisAlignedBox3d Mattress = Built.Mattress.GetBounds();
	const FAxisAlignedBox3d Frame = Built.Frame.GetBounds();
	const double Inset = Built.Used.FrameInset;

	AddInfo(FString::Printf(TEXT("Mattress X %.1f..%.1f over a frame at %.1f..%.1f; inset %.2f cm."),
		Mattress.Min.X, Mattress.Max.X, Frame.Min.X, Frame.Max.X, Inset));

	TestTrue(TEXT("The inset is a real one, not a rounding"), Inset > 1.0);

	TestNearlyEqual(TEXT("The mattress oversails the frame on the left"),
		Frame.Min.X - Mattress.Min.X, Inset, 0.01);
	TestNearlyEqual(TEXT("And on the right"), Mattress.Max.X - Frame.Max.X, Inset, 0.01);
	TestNearlyEqual(TEXT("And at the foot"), Frame.Min.Y - Mattress.Min.Y, Inset, 0.01);

	// NOT at the head, and deliberately: that end dies into the headboard, which is wider than the
	// frame and covers it. An inset there would open a slot between the two which light gets into
	// from the one direction nobody can reach to see what is in it.
	TestNearlyEqual(TEXT("But not at the head, where the headboard closes it"),
		Frame.Max.Y, Mattress.Max.Y, 0.01);

	// And the mattress really is ON the frame rather than floating over it or sunk into it.
	TestNearlyEqual(TEXT("The mattress lies on the deck"), Mattress.Min.Z, Frame.Max.Z, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBedParametersBiteTest, "HouseForge.Bedroom.ABedAnswersToItsSizes",
	HF_TEST_FLAGS)

bool FHFBedParametersBiteTest::RunTest(const FString& Parameters)
{
	const FHFBedBuild King = FHFBedKit::Build(MakeKingBed());

	// A QUEEN IS THE SAME OBJECT AT A DIFFERENT SIZE, not a different fixture - and both are real
	// Indian mattress sizes rather than numbers picked to make a test pass.
	FHFBedParams QueenParams = MakeKingBed();
	QueenParams.Width = 150.0;

	const FHFBedBuild Queen = FHFBedKit::Build(QueenParams);

	TestTrue(TEXT("A queen builds too"), Queen.bValid);
	TestNearlyEqual(TEXT("And is 300 mm narrower"),
		King.Shell.GetBounds().Max.X - Queen.Shell.GetBounds().Max.X, 30.0, 0.01);
	TestTrue(TEXT("Parameters genuinely change the output"),
		Volume(Queen.Shell) < Volume(King.Shell) - 1.0);

	// A thicker mattress raises the deck rather than the bed, because the drawn height is the top of
	// the mattress: the figure a plan gives a bed is the surface somebody sits on.
	FHFBedParams Thick = MakeKingBed();
	Thick.MattressThickness = 25.0;

	const FHFBedBuild ThickBuilt = FHFBedKit::Build(Thick);

	TestNearlyEqual(TEXT("A thicker mattress does not raise the bed"),
		ThickBuilt.Shell.GetBounds().Max.Z, King.Shell.GetBounds().Max.Z, 0.01);
	TestNearlyEqual(TEXT("It lowers the deck under it"),
		King.Frame.GetBounds().Max.Z - ThickBuilt.Frame.GetBounds().Max.Z, 5.0, 0.01);

	// ------------------------------------------------------------------------- degenerate input
	//
	// EMPTY, NOT A SLIVER. An empty mesh appends harmlessly; a degenerate one carries through every
	// volume measurement taken afterwards and is exactly the kind of thing that looks fine.

	FHFBedParams Nothing;
	Nothing.Width = 0.0;

	const FHFBedBuild NoBed = FHFBedKit::Build(Nothing);
	TestFalse(TEXT("A bed with no width is refused"), NoBed.bValid);
	TestEqual(TEXT("And produces an empty mesh rather than garbage"), NoBed.Shell.TriangleCount(), 0);

	FHFBedParams Flat = MakeKingBed();
	Flat.MattressTopZ = 0.0;

	const FHFBedBuild NoHeight = FHFBedKit::Build(Flat);
	TestFalse(TEXT("A bed with no height is refused"), NoHeight.bValid);
	TestEqual(TEXT("And is empty too"), NoHeight.Shell.TriangleCount(), 0);

	return true;
}

// =========================================================================================== desk

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDeskKneeHoleTest, "HouseForge.Bedroom.ADeskHasSomewhereForKnees",
	HF_TEST_FLAGS)

bool FHFDeskKneeHoleTest::RunTest(const FString& Parameters)
{
	const FHFDeskBuild Built = FHFDeskKit::Build(MakeStudyTable());

	TestTrue(TEXT("A desk builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestNearlyEqual(TEXT("It is the drawn length"), Bounds.Max.X - Bounds.Min.X, 120.0, 0.01);
	TestNearlyEqual(TEXT("And the drawn writing height"), Bounds.Max.Z, 75.0, 0.01);

	// THE TOP RUNS THE FULL DRAWN DEPTH and the supports do not, which is the whole of the skirting
	// decision: the board runs on behind a desk, so the worktop oversails it and the legs stand in
	// front of it. Without this the desk is 18 mm inside the skirting in every room, permanently, and
	// nothing measuring the desk on its own can see it.
	TestNearlyEqual(TEXT("The top reaches the wall"), Built.Top.GetBounds().Max.Y, 55.0, 0.01);
	TestNearlyEqual(TEXT("The gable stands clear of the skirting behind it"),
		Built.Gable.GetBounds().Max.Y, 53.0, 0.01);
	TestNearlyEqual(TEXT("And so does the pedestal"),
		Built.Pedestal.GetBounds().Max.Y, 53.0, 0.01);

	// ------------------------------------------------------------------------------ the knee hole
	//
	// A DESK IS DEFINED BY THE HOLE IN THE MIDDLE OF IT. Expressed as a cased good with one bay of
	// drawers and two empty bays it would come out as a 1200 sideboard with a chair pulled up to it -
	// correct in width, depth and height, and unusable. So the hole is measured as an actual empty
	// volume rather than inferred from the pedestal's width.

	const double InnerX0 = Built.PedestalX0 + Built.Used.PedestalWidth;
	const double InnerX1 = Built.GableX0;
	const double ClearWidth = InnerX1 - InnerX0;
	const double ClearHeight = Built.Used.TopUnderZ();

	AddInfo(FString::Printf(TEXT("Knee hole %.1f cm wide by %.1f cm high."), ClearWidth, ClearHeight));

	// 700 of clear width and 650 of clear height are what a person needs under a desk. Both are held
	// by FHFDeskKit::Sanitise, which gives the PEDESTAL away rather than the hole when a drawing's
	// arithmetic runs out.
	TestTrue(TEXT("There is room for a chair between the supports"), ClearWidth >= 70.0);
	TestTrue(TEXT("And room for knees under the top"), ClearHeight >= 65.0);

	// And it is genuinely empty. Sampled short of the modesty panel at the back and short of the
	// underside of the top, because both of those are meant to be there.
	const FBox KneeHole(
		FVector(InnerX0 + 0.1, 0.1, 0.1),
		FVector(InnerX1 - 0.1, Built.Used.SupportDepth() - 5.0, ClearHeight - Built.Used.ModestyPanelHeight - 0.1));

	TestFalse(TEXT("Nothing at all is built in the knee hole"),
		AnythingInside(Built.Shell, KneeHole));

	// The modesty panel IS there, at the back, closing the hole in elevation. A desk without one is a
	// trestle: you see straight through it to the skirting.
	TestTrue(TEXT("But the back of it is closed by a modesty panel"),
		Built.ModestyPanel.TriangleCount() > 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFDeskDrawersOpenTest, "HouseForge.Bedroom.TheDeskDrawersComeOut",
	HF_TEST_FLAGS)

bool FHFDeskDrawersOpenTest::RunTest(const FString& Parameters)
{
	const FHFDeskBuild Built = FHFDeskKit::Build(MakeStudyTable());

	// Every part that is a drawer rather than the geared runner member carrying one.
	TArray<const FHFMeshPart*> Drawers;
	for (const FHFMeshPart& Part : Built.Parts)
	{
		if (Part.Motion.DrivenByPartId.IsNone() && Part.Motion.Type == EHFMotionType::Slide)
		{
			Drawers.Add(&Part);
		}
	}

	TestEqual(TEXT("The pedestal has the two drawers it was drawn with"), Drawers.Num(), 2);

	for (const FHFMeshPart* Drawer : Drawers)
	{
		const FBox Shut = PosedBounds(*Drawer, 0.0);
		const FBox Open = PosedBounds(*Drawer, 1.0);

		const double Travel = Shut.Min.Y - Open.Min.Y;

		AddInfo(FString::Printf(TEXT("'%s' travels %.1f cm out of the pedestal."),
			*Drawer->PartId.ToString(), Travel));

		// A REAL DISTANCE, IN CENTIMETRES, AND IN THE RIGHT DIRECTION. Front is -Y, so a drawer that
		// opened backwards would report exactly the same magnitude of travel and would be a box driven
		// through the wall behind the desk. The figure itself is a full-extension runner in a 51 cm
		// pedestal, so most of the box clears the carcass.
		TestTrue(*FString::Printf(TEXT("'%s' comes OUT of the desk, not into it"),
			*Drawer->PartId.ToString()), Travel > 30.0);

		// And the box it carries really leaves the carcass. Measured on the deepest point of the
		// drawer rather than on its front, because a front that slides forward off a box that stayed
		// behind is a front on nothing, and looks identical from the front.
		TestTrue(*FString::Printf(TEXT("'%s' brings its box with it"), *Drawer->PartId.ToString()),
			Open.Max.Y < Shut.Max.Y - 30.0);
	}

	// The two are not the same drawer at two heights: a graduated bank has fronts of different
	// heights, which is what stops a bank of drawers rendering as one slab with lines on it.
	if (Drawers.Num() == 2)
	{
		const FBox First = PosedBounds(*Drawers[0], 0.0);
		const FBox Second = PosedBounds(*Drawers[1], 0.0);

		TestTrue(TEXT("The two fronts are at different heights"),
			FMath::Abs(First.Min.Z - Second.Min.Z) > 5.0);
	}

	// ------------------------------------------------------------------------- degenerate input

	FHFDeskParams Nothing = MakeStudyTable();
	Nothing.Width = 0.0;

	const FHFDeskBuild NoDesk = FHFDeskKit::Build(Nothing);
	TestFalse(TEXT("A desk with no length is refused"), NoDesk.bValid);
	TestEqual(TEXT("And produces an empty mesh rather than garbage"), NoDesk.Shell.TriangleCount(), 0);
	TestEqual(TEXT("With no drawers hanging off nothing"), NoDesk.Parts.Num(), 0);

	// Parameters bite: a wider desk is a bigger desk, and a wider pedestal is a smaller knee hole.
	FHFDeskParams Wider = MakeStudyTable();
	Wider.Width = 150.0;

	TestTrue(TEXT("Parameters genuinely change the output"),
		Volume(FHFDeskKit::Build(Wider).Shell) > Volume(Built.Shell) + 1.0);

	return true;
}

// =================================================================================== shoe rack

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShoeRackTiersTest, "HouseForge.Bedroom.AShoeRackIsTiers",
	HF_TEST_FLAGS)

bool FHFShoeRackTiersTest::RunTest(const FString& Parameters)
{
	const FHFCasedGoodsParams Params = MakeShoeRack();
	const FHFCasedGoodsBuild Built = FHFCasedGoodsKit::Build(Params);

	TestTrue(TEXT("A shoe rack builds"), Built.bValid);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestNearlyEqual(TEXT("It is the drawn length"), Bounds.Max.X - Bounds.Min.X, 120.0, 0.01);
	TestNearlyEqual(TEXT("It stands on the floor"), Bounds.Min.Z, 0.0, 0.01);
	TestNearlyEqual(TEXT("And is the drawn height"), Bounds.Max.Z, 90.0, 0.01);

	// THE FLAPS STACK, THEY DO NOT STAND SIDE BY SIDE. A pair of side-hung doors on a 350-deep box is
	// a cupboard that happens to have shoes in it; what a foyer actually has is a shallow cabinet
	// whose fronts tip forward, precisely because 350 mm is too little depth to swing a door into a
	// hallway somebody is standing in. So the drawing's ShutterCount is read as a TIER count, and
	// each tier is its own carcass in the stack.
	TestEqual(TEXT("Two flaps means two tiers, one above the other"), Built.Used.Units.Num(), 2);

	for (const FHFCaseUnit& Unit : Built.Used.Units)
	{
		TestEqual(TEXT("Each tier is one compartment across"), Unit.BayCount, 1);
		TestEqual(TEXT("Closed by exactly one flap"), Unit.Bays[0].LeafCount, 1);
		TestEqual(TEXT("Which tilts out at its foot"),
			static_cast<int32>(Unit.Bays[0].Motion), static_cast<int32>(EHFShutterMotion::BottomHung));

		// AND THE DRAWN SHELF COUNT ADDS UP. Three divisions over two tiers is one tier board and one
		// shelf inside each tier - the rack that was drawn, not a rack with two extra boards in it.
		TestEqual(TEXT("With one shelf in it"), Unit.Bays[0].ShelfCount, 1);
	}

	// The tiers really are stacked rather than nominally so: the second sits on the first.
	TestEqual(TEXT("Two carcasses were built"), Built.Carcasses.Num(), 2);
	TestTrue(TEXT("The upper tier stands on the lower one"),
		Built.UnitBaseZ.Num() == 2 && Built.UnitBaseZ[1] > Built.UnitBaseZ[0] + 30.0);

	// A shoe compartment is about a shoe tall. 820 of carcass over an 80 plinth, halved into tiers
	// and each halved again by its shelf, is about 178 - which is the 180 pitch a shoe rack is built
	// to and is the figure that decides whether anything fits in it.
	const double TierHeight = Built.Used.Units[0].Height;
	const double Compartment = (TierHeight - 2.0 * Built.Used.BoardThickness()) * 0.5;

	AddInfo(FString::Printf(TEXT("Tier %.1f cm high, compartment about %.1f cm."),
		TierHeight, Compartment));
	TestTrue(TEXT("A compartment is tall enough for a shoe"), Compartment > 15.0);
	TestTrue(TEXT("And not so tall it is a cupboard"), Compartment < 26.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShoeRackFlapsOpenTest,
	"HouseForge.Bedroom.TheShoeRackFlapsTiltOutAndOpenSomething", HF_TEST_FLAGS)

bool FHFShoeRackFlapsOpenTest::RunTest(const FString& Parameters)
{
	const FHFCasedGoodsParams Params = MakeShoeRack();
	const FHFCasedGoodsBuild Built = FHFCasedGoodsKit::Build(Params);

	TestTrue(TEXT("A shoe rack builds"), Built.bValid);
	TestEqual(TEXT("It has one part per tier"), Built.Parts.Num(), 2);

	// ------------------------------------------------------------ every one of them tilts OUT and DOWN
	//
	// NOT MERELY THAT IT MOVED. A leaf hung the wrong way about the same axis reports exactly the same
	// angle and the same magnitude of travel while swinging backwards through the shelf behind it -
	// invisible in elevation, obvious the instant anybody opens it, and it is the same class of
	// failure as the wardrobe pair that both travelled 118 cm and uncovered nothing.

	for (int32 Index = 0; Index < Built.Parts.Num(); ++Index)
	{
		const FHFMeshPart& Flap = Built.Parts[Index];

		TestEqual(*FString::Printf(TEXT("Flap %d turns on a hinge"), Index),
			static_cast<int32>(Flap.Motion.Type), static_cast<int32>(EHFMotionType::Hinge));

		// About the HORIZONTAL axis. A vertical one is a cabinet door, which is the thing this is not.
		TestTrue(*FString::Printf(TEXT("Flap %d turns about a horizontal axis"), Index),
			FMath::Abs(Flap.Motion.UnitAxis().X) > 0.99);

		// A SHALLOW ANGLE, and its own figure. 68 degrees is where a tilt-out's stay puts it, because
		// past about 70 the compartment tips its contents onto the floor. A cabinet door's 100 here
		// would have four flaps lying flat out across a foyer walkway.
		const double Angle = FMath::Abs(Flap.Motion.MaxAngleDegrees);

		AddInfo(FString::Printf(TEXT("Flap %d stops at %.0f degrees."), Index, Angle));
		TestTrue(*FString::Printf(TEXT("Flap %d stops short of horizontal"), Index), Angle < 75.0);
		TestTrue(*FString::Printf(TEXT("And opens far enough to be worth opening")), Angle > 55.0);

		const FBox Shut = PosedBounds(Flap, 0.0);
		const FBox Open = PosedBounds(Flap, 1.0);

		// OUT INTO THE ROOM, which is -Y. The distance is real centimetres: a 41 cm flap at 68 degrees
		// reaches about 38 cm forward, which is what has to be clear in front of a shoe rack.
		const double Reach = Shut.Min.Y - Open.Min.Y;

		AddInfo(FString::Printf(TEXT("Flap %d reaches %.1f cm out into the room."), Index, Reach));
		TestTrue(*FString::Printf(TEXT("Flap %d comes forward, not backward"), Index), Reach > 25.0);

		// AND NOTHING OF IT GOES BACKWARDS. This is the assertion that catches the sign being wrong:
		// a flap hung the other way sweeps into the compartment it is meant to be uncovering, and
		// every measurement of angle and of magnitude still agrees.
		TestTrue(*FString::Printf(TEXT("Flap %d never swings back into its own compartment"), Index),
			Open.Max.Y <= Shut.Max.Y + 0.01);

		// And it comes DOWN as it comes out, which is what makes it a tilt-out rather than a lift-up.
		TestTrue(*FString::Printf(TEXT("Flap %d drops as it opens"), Index),
			Open.Max.Z < Shut.Max.Z - 15.0);
	}

	// ------------------------------------------------------------------ and the compartment OPENS
	//
	// THE MEASUREMENT THAT MATTERS, in centimetres of visible aperture. A flap can turn its full
	// declared angle and still leave the mouth of the compartment covered - that is exactly what a
	// wardrobe's cancelling leaf pair did - so what is asserted is the gap that appears at the head
	// of the compartment once the front has dropped out of the way.

	for (int32 Index = 0; Index < Built.Parts.Num(); ++Index)
	{
		const FHFMeshPart& Flap = Built.Parts[Index];

		const double CompartmentTopZ = PosedBounds(Flap, 0.0).Max.Z;
		const double Aperture = CompartmentTopZ - PosedBounds(Flap, 1.0).Max.Z;

		AddInfo(FString::Printf(TEXT("Flap %d uncovers %.1f cm at the head of its compartment."),
			Index, Aperture));

		// A 41 cm flap dropping to 68 degrees leaves about 26 cm of open mouth, which is a hand and a
		// pair of shoes. Anything under about 15 is a cabinet that opens onto a slot.
		TestTrue(*FString::Printf(TEXT("Flap %d actually uncovers its compartment"), Index),
			Aperture > 15.0);
	}

	// AND THE TWO DO NOT FOUL EACH OTHER. Both tilt forward out of the same face, so the obvious way
	// for this to be wrong is the upper flap swinging down through the lower one - which would be a
	// rack that can have either flap open but never both, and which nothing above would catch.
	if (Built.Parts.Num() == 2)
	{
		const FBox Lower = PosedBounds(Built.Parts[0], 1.0);
		const FBox Upper = PosedBounds(Built.Parts[1], 1.0);

		AddInfo(FString::Printf(TEXT("Open together: lower tops out at Z %.1f, upper starts at %.1f."),
			Lower.Max.Z, Upper.Min.Z));

		TestTrue(TEXT("Both flaps can be open at once without meeting"),
			Upper.Min.Z >= Lower.Max.Z - 0.01);
	}

	return true;
}

// =================================================================== nightstand and TV unit

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFNightstandTest, "HouseForge.Bedroom.ANightstandIsADrawerBank",
	HF_TEST_FLAGS)

bool FHFNightstandTest::RunTest(const FString& Parameters)
{
	const FHFCasedGoodsBuild Built = FHFCasedGoodsKit::Build(MakeNightstand());

	TestTrue(TEXT("A nightstand builds"), Built.bValid);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestNearlyEqual(TEXT("It is the drawn width"), Bounds.Max.X - Bounds.Min.X, 45.0, 0.01);
	TestNearlyEqual(TEXT("It is the drawn height"), Bounds.Max.Z, 55.0, 0.01);
	TestNearlyEqual(TEXT("And stands on the floor"), Bounds.Min.Z, 0.0, 0.01);

	// ONE BAY, ALL DRAWERS. At 450 wide there is no room for two bays, and a hinged door at that
	// width standing beside a bed swings across it - which is why the nightstand recipe does not go
	// through ComposeBays at all.
	TestEqual(TEXT("It is a single bay"), Built.Used.Units.Num(), 1);
	TestEqual(TEXT("Undivided"), Built.Used.Units[0].BayCount, 1);
	TestEqual(TEXT("Filled with drawers"),
		static_cast<int32>(Built.Used.Units[0].Bays[0].Front), static_cast<int32>(EHFCaseFront::DrawerBank));

	// It stands on a real toe kick rather than the 10 mm a spec's default sentinel would have given
	// it. The plinth is what makes a bedside unit read as furniture rather than as a box on the floor.
	TestNearlyEqual(TEXT("On a toe kick somebody can see"), Built.Used.PlinthHeight, 8.0, 0.01);
	TestTrue(TEXT("Which is really built"), Built.Plinth.TriangleCount() > 0);

	// And the drawers come out. Measured as travel in centimetres and in the right direction, for the
	// same reason as everywhere else in this file.
	int32 Moving = 0;
	for (const FHFMeshPart& Part : Built.Parts)
	{
		if (!Part.Motion.DrivenByPartId.IsNone() || Part.Motion.Type != EHFMotionType::Slide)
		{
			continue;
		}

		++Moving;

		const double Travel = PosedBounds(Part, 0.0).Min.Y - PosedBounds(Part, 1.0).Min.Y;
		TestTrue(*FString::Printf(TEXT("'%s' comes out of the unit"), *Part.PartId.ToString()),
			Travel > 20.0);
	}

	TestEqual(TEXT("Both drawers move"), Moving, 2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTVUnitTest, "HouseForge.Bedroom.ATVUnitIsTwoObjectsAtTwoHeights",
	HF_TEST_FLAGS)

bool FHFTVUnitTest::RunTest(const FString& Parameters)
{
	// A TALL STORAGE COLUMN AND A LOW CONSOLE ARE THE SAME OBJECT AT TWO HEIGHTS, and the drawing
	// already separates them: the column is drawn with shutters and shelves, the console with
	// drawers. This is the assertion that the recipe reads exactly that and invents nothing.

	const FHFCasedGoodsBuild Console = FHFCasedGoodsKit::Build(MakeTVConsole());
	const FHFCasedGoodsBuild Column = FHFCasedGoodsKit::Build(MakeTVColumn());

	TestTrue(TEXT("The console builds"), Console.bValid);
	TestTrue(TEXT("The column builds"), Column.bValid);
	TestTrue(TEXT("Every triangle of the console carries a role"), EveryTriangleHasARole(Console.Shell));
	TestTrue(TEXT("Every triangle of the column carries a role"), EveryTriangleHasARole(Column.Shell));

	TestNearlyEqual(TEXT("The console is the drawn box"),
		Console.Shell.GetBounds().Max.Z, 60.0, 0.01);
	TestNearlyEqual(TEXT("And the drawn length"),
		Console.Shell.GetBounds().Max.X - Console.Shell.GetBounds().Min.X, 120.0, 0.01);

	TestNearlyEqual(TEXT("The column is the drawn box too"),
		Column.Shell.GetBounds().Max.Z, 180.0, 0.01);

	// ------------------------------------------------------------------------------ the console

	int32 ConsoleDrawers = 0;
	for (const FHFMeshPart& Part : Console.Parts)
	{
		if (Part.Motion.DrivenByPartId.IsNone() && Part.Motion.Type == EHFMotionType::Slide)
		{
			++ConsoleDrawers;

			const double Travel = PosedBounds(Part, 0.0).Min.Y - PosedBounds(Part, 1.0).Min.Y;
			TestTrue(*FString::Printf(TEXT("Console drawer '%s' comes out"), *Part.PartId.ToString()),
				Travel > 20.0);
		}
	}

	TestEqual(TEXT("The console has its three drawers"), ConsoleDrawers, 3);

	// A GRADUATED BANK, not three equal boxes. An evenly divided bank is one of the clearest tells
	// that joinery was generated rather than made, and it costs nothing to get right.
	{
		TArray<double> FrontHeights;
		for (const FHFMeshPart& Part : Console.Parts)
		{
			if (Part.Motion.DrivenByPartId.IsNone() && Part.Motion.Type == EHFMotionType::Slide)
			{
				const FBox Front = PosedBounds(Part, 0.0);
				FrontHeights.Add(Front.Max.Z - Front.Min.Z);
			}
		}

		FrontHeights.Sort();

		if (FrontHeights.Num() == 3)
		{
			AddInfo(FString::Printf(TEXT("Console fronts %.1f / %.1f / %.1f cm."),
				FrontHeights[0], FrontHeights[1], FrontHeights[2]));

			TestTrue(TEXT("The bank is graduated rather than divided evenly"),
				FrontHeights[2] > FrontHeights[0] + 2.0);
		}
	}

	// ------------------------------------------------------------------------------- the column

	int32 ColumnLeaves = 0;
	for (const FHFMeshPart& Part : Column.Parts)
	{
		if (Part.Motion.Type == EHFMotionType::Hinge)
		{
			++ColumnLeaves;

			// A door, so it turns about the VERTICAL axis - which is what separates it from the shoe
			// rack's flap, the one other thing in this group hung on a hinge.
			TestTrue(*FString::Printf(TEXT("Column leaf '%s' turns about a vertical axis"),
				*Part.PartId.ToString()), FMath::Abs(Part.Motion.UnitAxis().Z) > 0.99);
		}
	}

	TestEqual(TEXT("The column has the two doors it was drawn with"), ColumnLeaves, 2);

	// A PAIR OPENS FROM THE MIDDLE OUT, which is how a run of doors is hung: adjacent leaves meet, so
	// one handle sits either side of the same shadow line. Both hung the same way is the tell that a
	// run of doors was stamped rather than set out.
	if (ColumnLeaves == 2)
	{
		TArray<double> Swings;
		for (const FHFMeshPart& Part : Column.Parts)
		{
			if (Part.Motion.Type == EHFMotionType::Hinge)
			{
				Swings.Add(Part.Motion.MaxAngleDegrees);
			}
		}

		TestTrue(TEXT("The two doors are hung on opposite sides"), Swings[0] * Swings[1] < 0.0);
	}

	// And the column is shelved out rather than being an empty box with doors on it.
	TestTrue(TEXT("The column carries its shelves"), Column.Interior.TriangleCount() > 0);
	TestTrue(TEXT("The console does not, because its bay is full of drawer boxes"),
		Volume(Column.Interior) > Volume(Console.Interior));

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
