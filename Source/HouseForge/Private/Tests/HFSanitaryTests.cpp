// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFCasedGoodsActor.h"
#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFCasedGoodsKit.h"
#include "Geometry/HFFrameKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFSanitaryKit.h"
#include "Geometry/HFWallPlateKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The sanitary group, on the bench: the WC, the basin, the shower, the vanity's top, the geyser,
// the mirror and the towel rail.
//
// Measured on properties that decide whether the thing is what it claims to be - volume,
// watertightness, bounds against the drawn box, roles, hollowness, swept travel and visible
// aperture in centimetres - and never on a triangle count. See .claude/rules and the milestone
// brief: a count changes the moment anything is chamfered and says nothing about correctness.
//
// The motion assertions here are deliberately not "did it move". A WC seat that swings its whole
// declared arc THROUGH the cistern behind it moves exactly as far as one that stops against it, and
// a lid that rotates the wrong way about its hinge sweeps the same distance into the pan as the
// right one does out of it. Both are measured for direction and for what they uncover.
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

	/** The part carrying an id, or null. */
	const FHFMeshPart* FindPart(const TArray<FHFMeshPart>& Parts, FName Id)
	{
		for (const FHFMeshPart& Part : Parts)
		{
			if (Part.PartId == Id)
			{
				return &Part;
			}
		}
		return nullptr;
	}

	/** A part's own mesh bounds, in the actor's frame, at a given open amount. */
	FBox PosedBounds(const FHFMeshPart& Part, double OpenAmount)
	{
		FBox Out(ForceInit);
		for (const int32 Vertex : Part.Mesh.VertexIndicesItr())
		{
			Out += PosedPoint(Part, FVector(Part.Mesh.GetVertex(Vertex)), OpenAmount);
		}
		return Out;
	}

	FHFWCParams MakeWC()
	{
		FHFWCParams P;
		P.Width = 38.0;
		P.Projection = 60.0;
		P.SeatHeight = 40.0;
		return P;
	}

	FHFBasinParams MakeBasin(EHFBasinMount Mount)
	{
		FHFBasinParams P;
		P.Width = 50.0;
		P.Depth = 40.0;
		P.Height = 18.0;
		P.BowlDepth = 13.0;
		P.Mount = Mount;
		return P;
	}

	FHFShowerParams MakeShower()
	{
		FHFShowerParams P;
		P.Width = 90.0;
		P.Depth = 90.0;
		P.Height = 210.0;
		P.ArmReach = 34.0;
		return P;
	}

	FHFGeyserParams MakeGeyser()
	{
		FHFGeyserParams P;
		P.Length = 45.0;
		P.Depth = 40.0;
		P.Height = 45.0;
		return P;
	}

	FHFMirrorParams MakeMirror()
	{
		FHFMirrorParams P;
		P.Width = 60.0;
		P.Height = 80.0;
		P.Depth = 3.0;
		return P;
	}

	FHFTowelRailParams MakeTowelRail()
	{
		FHFTowelRailParams P;
		P.Width = 50.0;
		P.Depth = 4.0;
		P.Height = 6.0;
		return P;
	}
}

// ================================================================================== the primitives

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRoundedRectangleTest,
	"HouseForge.Sanitary.RoundedRectangleIsRoundedAndConvex", HF_TEST_FLAGS)

bool FHFRoundedRectangleTest::RunTest(const FString& Parameters)
{
	// THE TEST THAT WOULD HAVE CAUGHT THE CLOVER. Every corner arc was drawn a quarter turn early, in
	// the quadrant belonging to the next corner, so at the radius sanitaryware needs the four arcs
	// swept into the middle and the outline came out a FOUR-LOBED CLOVER. Every WC pan, every basin
	// and every one of the sink's bowls was one, and it took rendering a bathroom to see it.
	//
	// Nothing measured it because a clover measures the same as the shape it should have been: the
	// same bounding box, closed, plausible volume, right corner radius wherever it is drawn at all.
	// What separates them is CONVEXITY, which is cheap and exact.
	const FVector2D Centre(3.0, -4.0);
	const FVector2D Half(19.0, 21.0);

	for (const double Fraction : { 0.0, 0.15, 0.5, 0.8, 1.0 })
	{
		const double Radius = FMath::Min(Half.X, Half.Y) * Fraction;
		const TArray<FVector2D> Ring = FHFMeshOps::RoundedRectangle(Centre, Half, Radius, 5);

		TestEqual(TEXT("The ring is drawn at a fixed point count whatever the radius"), Ring.Num(), 24);
		TestTrue(TEXT("And wound counter-clockwise"), FHFMeshOps::SignedArea(Ring) > 0.0);

		// Convex: every turn from one edge to the next goes the same way. A clover reverses at each
		// lobe, which is the whole of the difference.
		bool bConvex = true;
		FBox2D Box(ForceInit);

		for (int32 Index = 0; Index < Ring.Num(); ++Index)
		{
			const FVector2D& A = Ring[Index];
			const FVector2D& B = Ring[(Index + 1) % Ring.Num()];
			const FVector2D& C = Ring[(Index + 2) % Ring.Num()];

			Box += A;

			// Consecutive points COINCIDE wherever a corner's radius has eaten its whole half-extent -
			// the two arcs either side of the flat then share an end - and a zero-length edge has no
			// direction to turn from. Skipped rather than measured; it is not a reversal.
			const FVector2D U = B - A;
			const FVector2D V = C - B;

			if (U.SizeSquared() < 1e-12 || V.SizeSquared() < 1e-12)
			{
				continue;
			}

			// Normalised, so the threshold means an angle rather than an area and does not tighten as
			// the outline grows.
			const FVector2D Un = U.GetSafeNormal();
			const FVector2D Vn = V.GetSafeNormal();
			const double Turn = Un.X * Vn.Y - Un.Y * Vn.X;

			if (Turn < -1e-6)
			{
				if (bConvex)
				{
					AddError(FString::Printf(
						TEXT("Radius %.1f turns the wrong way at point %d: (%.3f, %.3f) -> (%.3f, %.3f) -> (%.3f, %.3f), sin %.6f."),
						Radius, Index, A.X, A.Y, B.X, B.Y, C.X, C.Y, Turn));
				}
				bConvex = false;
			}
		}

		TestTrue(*FString::Printf(TEXT("At radius %.1f the outline is convex"), Radius), bConvex);

		// And it really is the rectangle it was asked for, at every radius: an outline that shrank to
		// fit its own corners would be convex too.
		TestNearlyEqual(TEXT("It spans its full width"), Box.Max.X - Box.Min.X, Half.X * 2.0, 0.001);
		TestNearlyEqual(TEXT("It spans its full depth"), Box.Max.Y - Box.Min.Y, Half.Y * 2.0, 0.001);
		TestNearlyEqual(TEXT("Centred where it was asked for"),
			(Box.Max.X + Box.Min.X) * 0.5, Centre.X, 0.001);

		// The corners really are taken off: a square rectangle has the full area, a fully rounded one
		// is an ellipse at pi/4 of it.
		const double Area = FHFMeshOps::SignedArea(Ring);
		const double Full = Half.X * Half.Y * 4.0;

		AddInfo(FString::Printf(TEXT("At radius %.1f the outline holds %.0f%% of its box."),
			Radius, Area / Full * 100.0));

		TestTrue(TEXT("It never has more area than its box"), Area <= Full + 0.001);
		TestTrue(TEXT("Nor less than an ellipse in it"), Area >= Full * 0.77);
	}

	return true;
}

// ============================================================================================ WC

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWCFormTest, "HouseForge.Sanitary.WCIsAHollowLoftedPan",
	HF_TEST_FLAGS)

bool FHFWCFormTest::RunTest(const FString& Parameters)
{
	const FHFWCBuild Built = FHFSanitaryKit::BuildWC(MakeWC());

	TestTrue(TEXT("A WC builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	// THE DRAWN BOX IN PLAN, EXACTLY. A WC is set out from its own centreline and its projection is
	// the figure that has to clear the door swing, so both are the drawing's and neither is derived.
	TestNearlyEqual(TEXT("It is as wide as it is drawn"), Bounds.Max.X - Bounds.Min.X, 38.0, 0.05);
	TestNearlyEqual(TEXT("It projects as far as it is drawn"), Bounds.Max.Y - Bounds.Min.Y, 60.0, 0.05);
	TestNearlyEqual(TEXT("Its back is on the wall line"), Bounds.Max.Y, 30.0, 0.01);
	TestNearlyEqual(TEXT("It stands on the floor"), Bounds.Min.Z, 0.0, 0.01);

	// AND NOT IN HEIGHT, which is the whole point of BuiltHeight existing. The drawn 400 is the SEAT;
	// the object standing there is nearly twice that with its cistern on.
	const FHFWCParams& P = Built.Used;
	TestNearlyEqual(TEXT("The seat top is the drawn height"), P.SeatHeight, 40.0, 0.01);
	TestTrue(TEXT("What is built is far taller than what is drawn"), Bounds.Max.Z > 70.0);
	TestNearlyEqual(TEXT("BuiltHeight is what is actually built"),
		Bounds.Max.Z, P.BuiltHeight(), 0.05);

	// A LOFTED FORM, NOT A PRISM. The pan draws in and walks BACK as it descends, so a section at the
	// foot is both narrower than the rim and sitting behind it - which is what makes the bowl overhang
	// its pedestal. Measured by asking how much of the plan area at the rim is still there at ankle
	// height: a box would answer "all of it".
	auto WidthAt = [&Built](double Z)
	{
		double MinX = TNumericLimits<double>::Max();
		double MaxX = -TNumericLimits<double>::Max();

		for (const int32 Vertex : Built.Shell.VertexIndicesItr())
		{
			const FVector3d Point = Built.Shell.GetVertex(Vertex);
			if (FMath::Abs(Point.Z - Z) < 1.5)
			{
				MinX = FMath::Min(MinX, Point.X);
				MaxX = FMath::Max(MaxX, Point.X);
			}
		}
		return MaxX > MinX ? MaxX - MinX : 0.0;
	};

	const double AtRim = WidthAt(Built.RimZ - 1.0);
	const double AtFoot = WidthAt(4.0);

	AddInfo(FString::Printf(TEXT("Pan is %.1f cm across at the rim and %.1f at the foot."),
		AtRim, AtFoot));

	TestTrue(TEXT("The pan draws in towards its foot"), AtFoot < AtRim * 0.8);
	TestTrue(TEXT("The foot is still a foot rather than a point"), AtFoot > AtRim * 0.4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWCBowlTest, "HouseForge.Sanitary.WCBowlIsHollowAndHasAFloor",
	HF_TEST_FLAGS)

bool FHFWCBowlTest::RunTest(const FString& Parameters)
{
	const FHFWCBuild Built = FHFSanitaryKit::BuildWC(MakeWC());
	TestTrue(TEXT("A WC builds"), Built.bValid);

	// HOLLOW, MEASURED. A solid block of china the right shape passes every bound above and is not a
	// WC; only the volume says which one was built.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	const double Envelope = Bounds.Width() * Bounds.Depth() * Bounds.Height();
	const double Solid = Volume(Built.Shell);

	AddInfo(FString::Printf(TEXT("WC solid volume %.0f cm3 in an envelope of %.0f."), Solid, Envelope));

	TestTrue(TEXT("It is not a solid block"), Solid < Envelope * 0.55);
	TestTrue(TEXT("It is not a shell of nothing"), Solid > Envelope * 0.1);
	TestTrue(TEXT("The bowl has a usable volume"), Built.BowlVolume > 5000.0);

	// AND IT HAS A BOTTOM. The sink's bowls came out as neat bottomless tubes that measured hollow,
	// measured deep enough and looked perfect from every angle except down into them - so the floor is
	// asserted directly, as upward-facing area at the height the floor is supposed to be.
	const double FloorZ = Built.RimZ - Built.Used.BowlDepth;
	double UpwardArea = 0.0;

	for (const int32 Tri : Built.Shell.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Built.Shell.GetTriVertices(Tri, A, B, C);

		const FVector3d Centre = (A + B + C) / 3.0;
		if (FMath::Abs(Centre.Z - FloorZ) > 0.6)
		{
			continue;
		}

		const FVector3d Normal = Built.Shell.GetTriNormal(Tri);
		if (Normal.Z > 0.9)
		{
			UpwardArea += Built.Shell.GetTriArea(Tri);
		}
	}

	AddInfo(FString::Printf(TEXT("Upward-facing pan floor at %.1f cm: %.0f cm2."), FloorZ, UpwardArea));

	// Something you could stand water in. The opening is about 31 x 34, so anything above a few
	// hundred square centimetres is a real floor rather than a sliver the boolean happened to leave.
	TestTrue(TEXT("There is a floor at the bottom of the pan"), UpwardArea > 300.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWCLidTest, "HouseForge.Sanitary.WCSeatAndLidActuallyLift",
	HF_TEST_FLAGS)

bool FHFWCLidTest::RunTest(const FString& Parameters)
{
	const FHFWCBuild Built = FHFSanitaryKit::BuildWC(MakeWC());

	const FHFMeshPart* Lid = FindPart(Built.Parts, FHFSanitaryKit::LidPartId());
	const FHFMeshPart* Seat = FindPart(Built.Parts, FHFSanitaryKit::SeatPartId());
	const FHFMeshPart* Button = FindPart(Built.Parts, FHFSanitaryKit::FlushButtonPartId());

	if (Lid == nullptr || Seat == nullptr || Button == nullptr)
	{
		AddError(TEXT("A WC must have a lid, a seat and a flush plate."));
		return false;
	}

	// ---------------------------------------------------------------------------- which way it goes
	//
	// THE SIGN IS THE WHOLE CONTENT OF IT. A leaf turned the other way about the same hinge travels
	// exactly as far, straight down through the pan, and satisfies any assertion that merely asks
	// whether it moved.

	const FBox Shut = PosedBounds(*Lid, 0.0);
	const FBox Open = PosedBounds(*Lid, 1.0);

	AddInfo(FString::Printf(TEXT("Lid rises from Z %.1f-%.1f to %.1f-%.1f cm."),
		Shut.Min.Z, Shut.Max.Z, Open.Min.Z, Open.Max.Z));

	TestTrue(TEXT("The lid goes UP, not down"), Open.Max.Z > Shut.Max.Z + 30.0);

	// The far end specifically, because the hinge end does not move at all and an average would hide
	// a leaf that had merely tilted.
	const FVector FarEdge(0.0, -(Built.Used.Projection - Built.Used.CisternDepth - 3.0), 0.0);
	AddInfo(FString::Printf(TEXT("The lid's far edge rises %.1f cm."),
		PosedPoint(*Lid, FarEdge, 1.0).Z - PosedPoint(*Lid, FarEdge, 0.0).Z));

	TestTrue(TEXT("Its far edge rises most of its own length"),
		PosedPoint(*Lid, FarEdge, 1.0).Z - PosedPoint(*Lid, FarEdge, 0.0).Z > 35.0);

	// ------------------------------------------------------------------- what it actually uncovers
	//
	// The visible aperture, in centimetres: how much of the pan's own length the lid still lies over.
	// Shut it covers the whole bowl; open it must be off it, or the lid has swung to a photogenic
	// angle and left the WC exactly as closed as it was.

	const double PanFrontY = -Built.Used.Projection * 0.5;
	const double PanBackY = Built.Used.Projection * 0.5 - Built.Used.CisternDepth;

	auto CoverOverThePan = [PanFrontY, PanBackY](const FBox& Box)
	{
		return FMath::Max(FMath::Min(Box.Max.Y, PanBackY) - FMath::Max(Box.Min.Y, PanFrontY), 0.0);
	};

	const double Covered = CoverOverThePan(Shut);
	const double Uncovered = CoverOverThePan(Open);

	AddInfo(FString::Printf(TEXT("Lid lies over %.1f cm of the pan shut and %.1f cm open."),
		Covered, Uncovered));

	TestTrue(TEXT("Shut, the lid covers the pan"), Covered > Built.Used.PanLength() * 0.9);
	TestTrue(TEXT("Open, the pan is clear"), Uncovered < 8.0);

	// ------------------------------------------------------------- and it stops on the cistern
	//
	// Not where it was asked to. 100 degrees with an 18 cm cistern immediately behind the hinge puts
	// the last eight of them inside the china - a full, correct-looking travel the object cannot
	// perform. See FHFWCBuild::LidLiftDegrees.

	AddInfo(FString::Printf(TEXT("Lid was asked for %.0f degrees and resolved to %.1f."),
		MakeWC().LidLiftDegrees, Built.LidLiftDegrees));

	TestTrue(TEXT("The lift is clamped below what was asked for"),
		Built.LidLiftDegrees < MakeWC().LidLiftDegrees);
	TestTrue(TEXT("It still passes vertical so the leaf rests back"), Built.LidLiftDegrees >= 90.0);

	const double CisternFrontY = PanBackY;
	TestTrue(TEXT("Open, the lid is clear of the cistern's face"), Open.Max.Y <= CisternFrontY + 0.2);

	// -------------------------------------------------------------- the seat cannot rise alone
	//
	// A threshold of zero makes the seat's allowance the lid's own amount exactly, so the two are
	// nested the way the real pair is: the lid may be lifted by itself, the seat may never be.

	TestEqual(TEXT("The seat is sequenced after the lid"),
		Seat->Motion.SequencedAfterPartId, FHFSanitaryKit::LidPartId());

	TArray<FHFPartState> Assembly;
	for (const FHFMeshPart& Part : Built.Parts)
	{
		FHFPartState State;
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
		State.OpenAmount = Part.Motion.Opens() ? 1.0 : 0.0;
		Assembly.Add(State);
	}

	// Everything asked to open at once: the lid may, the seat follows it, and both end up open.
	TestTrue(TEXT("The assembly resolves"), FHFArticulation::ResolvePartAmounts(Assembly));

	for (const FHFPartState& State : Assembly)
	{
		if (State.PartId == FHFSanitaryKit::SeatPartId())
		{
			TestNearlyEqual(TEXT("With the lid up, the seat may come up too"), State.OpenAmount, 1.0, 0.001);
		}
	}

	// And with the lid DOWN, the seat is held at zero however far it is asked to go.
	for (FHFPartState& State : Assembly)
	{
		State.OpenAmount = State.PartId == FHFSanitaryKit::LidPartId() ? 0.0 : 1.0;
	}
	FHFArticulation::ResolvePartAmounts(Assembly);

	for (const FHFPartState& State : Assembly)
	{
		if (State.PartId == FHFSanitaryKit::SeatPartId())
		{
			TestNearlyEqual(TEXT("Under a shut lid, the seat cannot rise at all"),
				State.OpenAmount, 0.0, 0.001);
		}
	}

	// ---------------------------------------------------------------------------- the flush plate
	//
	// Eight millimetres, and DOWNWARDS. Small enough that it is only worth having because the rule is
	// that a thing which moves moves - and the sign still matters, because upwards it travels exactly
	// as far, out of the cistern.

	const FBox ButtonUp = PosedBounds(*Button, 0.0);
	const FBox ButtonIn = PosedBounds(*Button, 1.0);

	const double Press = ButtonUp.Max.Z - ButtonIn.Max.Z;
	AddInfo(FString::Printf(TEXT("The flush plate presses %.2f cm in."), Press));

	TestNearlyEqual(TEXT("The plate presses in by its declared travel"),
		Press, Built.Used.FlushButtonTravel, 0.01);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWCParamsTest, "HouseForge.Sanitary.WCAnswersToItsParameters",
	HF_TEST_FLAGS)

bool FHFWCParamsTest::RunTest(const FString& Parameters)
{
	const FHFWCBuild Standard = FHFSanitaryKit::BuildWC(MakeWC());

	FHFWCParams Taller = MakeWC();
	Taller.SeatHeight = 45.0;

	const FHFWCBuild Comfort = FHFSanitaryKit::BuildWC(Taller);

	TestTrue(TEXT("A comfort-height WC builds"), Comfort.bValid);
	TestTrue(TEXT("Its rim is higher"), Comfort.RimZ > Standard.RimZ + 4.0);
	TestTrue(TEXT("So is everything above it"),
		Comfort.Shell.GetBounds().Max.Z > Standard.Shell.GetBounds().Max.Z + 4.0);

	FHFWCParams NoCistern = MakeWC();
	NoCistern.CisternHeight = 0.0;

	const FHFWCBuild BackToWall = FHFSanitaryKit::BuildWC(NoCistern);

	TestTrue(TEXT("A WC with no cistern still builds"), BackToWall.bValid);
	TestTrue(TEXT("And it is only as tall as its seat"),
		BackToWall.Shell.GetBounds().Max.Z < 45.0);

	// With nothing behind the hinge, the leaf may go as far as it was asked to - which is the other
	// half of the clamp being geometric rather than a constant.
	TestNearlyEqual(TEXT("With no cistern the lift is the one asked for"),
		BackToWall.LidLiftDegrees, NoCistern.LidLiftDegrees, 0.01);

	// ---------------------------------------------------------------------------------- degenerate
	//
	// An empty mesh rather than garbage. A sliver carries through every volume measurement taken
	// afterwards and a zero-triangle mesh appends harmlessly.
	FHFWCParams Nothing;
	Nothing.Width = 0.0;

	const FHFWCBuild Empty = FHFSanitaryKit::BuildWC(Nothing);
	TestFalse(TEXT("A WC with no width does not build"), Empty.bValid);
	TestEqual(TEXT("And emits no geometry at all"), Empty.Shell.TriangleCount(), 0);
	TestEqual(TEXT("And no parts"), Empty.Parts.Num(), 0);

	FHFWCParams AllCistern = MakeWC();
	AllCistern.CisternDepth = 500.0;

	const FHFWCBuild Clamped = FHFSanitaryKit::BuildWC(AllCistern);
	TestTrue(TEXT("A cistern deeper than the fitting is clamped, not refused"), Clamped.bValid);
	TestTrue(TEXT("And it leaves a pan"), Clamped.Used.PanLength() > 0.0);

	return true;
}

// ========================================================================================= basin

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBasinFormTest, "HouseForge.Sanitary.BasinIsAHollowBowlOnALedge",
	HF_TEST_FLAGS)

bool FHFBasinFormTest::RunTest(const FString& Parameters)
{
	const FHFBasinBuild Built = FHFSanitaryKit::BuildBasin(MakeBasin(EHFBasinMount::CounterTop));

	TestTrue(TEXT("A basin builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("It is as wide as it is drawn"), Bounds.Max.X - Bounds.Min.X, 50.0, 0.05);
	TestNearlyEqual(TEXT("It is as deep as it is drawn"), Bounds.Max.Y - Bounds.Min.Y, 40.0, 0.05);

	// STANDING ON THE SURFACE, not sunk into it. Z = 0 is the mounting plane, so a vessel basin has
	// nothing at all below it - which is what makes the same frame work for a wall-hung one, where the
	// shroud hangs below and the bowl is in exactly the same place.
	TestNearlyEqual(TEXT("A vessel basin sits on the plane it is placed at"), Bounds.Min.Z, 0.0, 0.01);

	// The BOWL is the drawn height; the tap standing on the ledge is not, and is not pretending to be.
	TestTrue(TEXT("The tap stands above the bowl"), Bounds.Max.Z > 18.0 + 8.0);

	// Hollow, and with a floor: the same two questions the WC and the sink are asked.
	const double Envelope = Bounds.Width() * Bounds.Depth() * Bounds.Height();
	AddInfo(FString::Printf(TEXT("Basin solid volume %.0f cm3 in an envelope of %.0f."),
		Volume(Built.Shell), Envelope));

	TestTrue(TEXT("It is not a solid block"), Volume(Built.Shell) < Envelope * 0.6);
	TestTrue(TEXT("The bowl holds something"), Built.BowlVolume > 3000.0);

	const double FloorZ = Built.Used.Height - Built.Used.BowlDepth;
	double UpwardArea = 0.0;

	for (const int32 Tri : Built.Shell.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Built.Shell.GetTriVertices(Tri, A, B, C);

		const FVector3d Centre = (A + B + C) / 3.0;
		if (FMath::Abs(Centre.Z - FloorZ) < 0.6 && Built.Shell.GetTriNormal(Tri).Z > 0.9)
		{
			UpwardArea += Built.Shell.GetTriArea(Tri);
		}
	}

	AddInfo(FString::Printf(TEXT("Upward-facing bowl floor at %.1f cm: %.0f cm2."), FloorZ, UpwardArea));
	TestTrue(TEXT("The bowl has a floor"), UpwardArea > 200.0);

	// THE TAP IS INSIDE THE FOOTPRINT. That is the whole reason it is on a ledge rather than standing
	// on the counter behind the bowl: the master bathroom's basin leaves 5 cm of stone behind it.
	bool bTapWithinFootprint = true;
	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		const FVector3d Point = Built.Shell.GetVertex(Vertex);
		if (Point.Z > Built.Used.Height + 0.5 && Point.Y > Built.Used.Depth * 0.5 + 0.01)
		{
			bTapWithinFootprint = false;
		}
	}
	TestTrue(TEXT("Nothing on the tap ledge leaves the drawn footprint"), bTapWithinFootprint);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBasinMountTest, "HouseForge.Sanitary.BasinCarriesItsOwnMount",
	HF_TEST_FLAGS)

bool FHFBasinMountTest::RunTest(const FString& Parameters)
{
	const FHFBasinBuild OnStone = FHFSanitaryKit::BuildBasin(MakeBasin(EHFBasinMount::CounterTop));
	const FHFBasinBuild OnWall = FHFSanitaryKit::BuildBasin(MakeBasin(EHFBasinMount::WallHung));

	TestTrue(TEXT("Both mounts build"), OnStone.bValid && OnWall.bValid);
	TestTrue(TEXT("A wall-hung basin is still closed"), FHFMeshOps::IsClosed(OnWall.Shell));

	// EVERYTHING HOLDING IT UP IS BELOW THE DRAWN BOX. The plan dimensions the bowl and says nothing
	// at all about the trap, so a basin built to the drawn box alone is a ceramic bowl floating at 800
	// with its waste in mid-air - the lower half of the object, at eye level from the door.
	const double StoneBottom = OnStone.Shell.GetBounds().Min.Z;
	const double WallBottom = OnWall.Shell.GetBounds().Min.Z;

	AddInfo(FString::Printf(TEXT("Vessel basin bottoms out at %.1f cm; wall-hung at %.1f."),
		StoneBottom, WallBottom));

	TestNearlyEqual(TEXT("A vessel basin has nothing below the counter"), StoneBottom, 0.0, 0.01);
	TestTrue(TEXT("A wall-hung one carries a shroud over its trap"), WallBottom < -20.0);
	TestNearlyEqual(TEXT("Which reaches exactly its declared drop"),
		WallBottom, OnWall.Used.BuiltBottomZ(), 0.5);

	// The bowl itself is in the same place either way, which is what lets one frame serve both.
	TestNearlyEqual(TEXT("The rim is at the same height on both"),
		OnWall.Shell.GetBounds().Max.Z, OnStone.Shell.GetBounds().Max.Z, 0.01);

	TestTrue(TEXT("The wall-hung one is the bigger object"),
		Volume(OnWall.Shell) > Volume(OnStone.Shell) * 1.2);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFBasinTapTest, "HouseForge.Sanitary.BasinTapActuallyMoves",
	HF_TEST_FLAGS)

bool FHFBasinTapTest::RunTest(const FString& Parameters)
{
	const FHFBasinBuild Built = FHFSanitaryKit::BuildBasin(MakeBasin(EHFBasinMount::CounterTop));

	const FHFMeshPart* Spout = FindPart(Built.Parts, FHFSanitaryKit::TapSpoutPartId());
	const FHFMeshPart* Lever = FindPart(Built.Parts, FHFSanitaryKit::TapLeverPartId());

	if (Spout == nullptr || Lever == nullptr)
	{
		AddError(TEXT("A basin tap must have a spout and a lever."));
		return false;
	}

	// The nose of the spout, which is the point of it: it has to come off the bowl to be cleaned round.
	const FVector Nose(0.0, -Built.Used.Tap.SpoutReach, 0.0);
	const double Swing = FVector::Dist(PosedPoint(*Spout, Nose, 0.0), PosedPoint(*Spout, Nose, 1.0));

	AddInfo(FString::Printf(TEXT("The spout's nose travels %.1f cm at full swivel."), Swing));
	TestTrue(TEXT("The spout swivels a usable distance"), Swing > 5.0);

	// THE LEVER LIFTS. Pressed down it travels exactly as far, through the ledge it is bolted to, and
	// no assertion about distance alone can tell the two apart.
	const FVector Tip(0.0, Built.Used.Tap.LeverLength, 0.0);
	const FVector Rest = PosedPoint(*Lever, Tip, 0.0);
	const FVector Lifted = PosedPoint(*Lever, Tip, 1.0);

	AddInfo(FString::Printf(TEXT("The lever's tip lifts %.1f cm."), Lifted.Z - Rest.Z));
	TestTrue(TEXT("The lever goes up"), Lifted.Z > Rest.Z + 1.5);

	// ---------------------------------------------------------------- AND IT STAYS OFF THE PLASTER
	//
	// A monobloc's lever runs BACKWARDS, and a basin's back is on the wall. At the flat's own figures
	// a 70 mm lever on a tap centred in its ledge ended 20 mm INSIDE the masonry, on both basins -
	// and there is no view of the fitting from which that shows except along the wall. So the tap is
	// set forward on its ledge and the lever clamped to what is left, and both are measured HERE
	// rather than in the flat, because a fitting that only fits in the room it happens to be in is a
	// fitting that will not fit the next one.
	const double Back = Built.Used.Depth * 0.5;

	for (const double Amount : { 0.0, 0.5, 1.0 })
	{
		TestTrue(*FString::Printf(TEXT("At %.0f%% open the lever is still clear of the wall"),
			Amount * 100.0), PosedPoint(*Lever, Tip, Amount).Y < Back);
	}

	AddInfo(FString::Printf(TEXT("The lever's tip stops %.1f cm short of the basin's back."),
		Back - PosedPoint(*Lever, Tip, 0.0).Y));

	// And the same for the fixed tap body, which is the thing actually bolted through the ledge.
	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		if (Built.Shell.GetVertex(Vertex).Z > Built.Used.Height + 0.5)
		{
			TestTrue(TEXT("Nothing on the tap ledge reaches past the basin's back"),
				Built.Shell.GetVertex(Vertex).Y <= Back + 0.01);
		}
	}

	return true;
}

// ======================================================================================== shower

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShowerFormTest, "HouseForge.Sanitary.ShowerIsAFittingAndAFloor",
	HF_TEST_FLAGS)

bool FHFShowerFormTest::RunTest(const FString& Parameters)
{
	const FHFShowerBuild Built = FHFSanitaryKit::BuildShower(MakeShower());

	TestTrue(TEXT("A shower builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("The wet area is as wide as it is drawn"),
		Bounds.Max.X - Bounds.Min.X, 90.0, 0.05);
	TestNearlyEqual(TEXT("The threshold is on the front edge"), Bounds.Min.Y, -45.0, 0.05);
	TestNearlyEqual(TEXT("The brassware is on the wall line"), Bounds.Max.Y, 45.0, 0.05);

	// The declared height is the ARM, which is what the rose hangs from - so nothing may stand above
	// it, and the arm has to actually reach it.
	TestTrue(TEXT("Nothing stands above the drawn height"), Bounds.Max.Z <= 210.0 + 0.01);
	TestTrue(TEXT("The arm reaches the drawn height"), Bounds.Max.Z > 205.0);

	// A SHOWER IS A PIECE OF FLOOR BEFORE IT IS A FITTING. Both halves are asserted, because a rose on
	// a pipe over an ordinary floor never reads as a shower corner at all.
	TestTrue(TEXT("There is a threshold at the floor"),
		FHFMeshOps::RolesPresent(Built.Shell).Contains(EHFSurfaceRole::CounterStone));
	TestTrue(TEXT("And chrome above it"),
		FHFMeshOps::RolesPresent(Built.Shell).Contains(EHFSurfaceRole::MetalHardware));

	// The gully is laid FLUSH - a wet area gully that stood proud would be a thing to trip on and
	// would read as a box on the floor.
	TestTrue(TEXT("The gully does not stand proud of the floor"), Bounds.Min.Z < 0.0);

	double GullyTop = -TNumericLimits<double>::Max();
	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		const FVector3d Point = Built.Shell.GetVertex(Vertex);

		// Down at the floor as well as over the drain: the riser, the arm and the rose all pass
		// directly above the gully, and without the height test this measures the shower head.
		if (FMath::Abs(Point.X) < 10.0 && FMath::Abs(Point.Y) < 10.0 && Point.Z < 20.0)
		{
			GullyTop = FMath::Max(GullyTop, Point.Z);
		}
	}
	AddInfo(FString::Printf(TEXT("The gully's top face is at %.2f cm."), GullyTop));
	TestTrue(TEXT("The gully is all but flush"), GullyTop < 0.5);

	// THE ROSE IS OVER THE STANDING AREA, not against the wall. An arm too short puts the spray on the
	// tiles behind whoever is using it, which is the one thing about a shower that has to be right.
	AddInfo(FString::Printf(TEXT("The rose hangs at Y %.1f cm, %.1f cm off the wall."),
		Built.RoseCentre.Y, 45.0 - Built.RoseCentre.Y));

	TestTrue(TEXT("The rose is well off the wall"), 45.0 - Built.RoseCentre.Y > 25.0);
	TestTrue(TEXT("And still inside the wet area"), Built.RoseCentre.Y > -45.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShowerMotionTest, "HouseForge.Sanitary.ShowerRoseAndLeverMove",
	HF_TEST_FLAGS)

bool FHFShowerMotionTest::RunTest(const FString& Parameters)
{
	const FHFShowerBuild Built = FHFSanitaryKit::BuildShower(MakeShower());

	const FHFMeshPart* Rose = FindPart(Built.Parts, FHFSanitaryKit::RosePartId());
	const FHFMeshPart* Lever = FindPart(Built.Parts, FHFSanitaryKit::MixerLeverPartId());

	if (Rose == nullptr || Lever == nullptr)
	{
		AddError(TEXT("A shower must have a rose and a mixer lever."));
		return false;
	}

	// ------------------------------------------------------------------- what a tilting rose is FOR
	//
	// Aiming the spray. So the thing to measure is not that the head moved - it is that the direction
	// it points in CHANGED, by the declared angle. A rose translated bodily by its arm satisfies any
	// assertion about travel and sprays in exactly the same direction it did before.

	const FVector Face(0.0, 0.0, -10.0);
	const FVector Aimed = PosedPoint(*Rose, Face, 0.0) - PosedPoint(*Rose, FVector::ZeroVector, 0.0);
	const FVector Tilted = PosedPoint(*Rose, Face, 1.0) - PosedPoint(*Rose, FVector::ZeroVector, 1.0);

	const double TurnedBy = FMath::RadiansToDegrees(
		FMath::Acos(FMath::Clamp(FVector::DotProduct(Aimed.GetSafeNormal(), Tilted.GetSafeNormal()),
			-1.0, 1.0)));

	AddInfo(FString::Printf(TEXT("The spray turns through %.1f degrees."), TurnedBy));
	TestNearlyEqual(TEXT("The spray turns by the declared tilt"),
		TurnedBy, Built.Used.RoseTiltDegrees, 0.5);

	// And the rim travels a measurable distance with it, in centimetres, so the tilt is a real
	// movement of a real head rather than a rotation of a point.
	const FVector Rim(Built.Used.RoseDiameter * 0.5, 0.0, -10.0);
	const double RimTravel = FVector::Dist(PosedPoint(*Rose, Rim, 0.0), PosedPoint(*Rose, Rim, 1.0));

	AddInfo(FString::Printf(TEXT("The rose's rim travels %.1f cm."), RimTravel));
	TestTrue(TEXT("The rose's rim travels a visible distance"), RimTravel > 3.0);

	// -------------------------------------------------------------------------------- the lever
	//
	// Hanging DOWN when shut, lifting from there, which is where a bar mixer's handle rests.
	const FVector Tip(0.0, 0.0, -FMath::Max(Built.Used.MixerRadius * 2.6, 3.0));
	const FVector Rest = PosedPoint(*Lever, Tip, 0.0);
	const FVector Open = PosedPoint(*Lever, Tip, 1.0);

	AddInfo(FString::Printf(TEXT("The mixer's lever tip sweeps %.1f cm and rises %.1f."),
		FVector::Dist(Rest, Open), Open.Z - Rest.Z));

	TestTrue(TEXT("The lever sweeps a usable distance"), FVector::Dist(Rest, Open) > 4.0);
	TestTrue(TEXT("And it comes up rather than going further down"), Open.Z > Rest.Z + 2.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFShowerParamsTest, "HouseForge.Sanitary.ShowerAnswersToItsParameters",
	HF_TEST_FLAGS)

bool FHFShowerParamsTest::RunTest(const FString& Parameters)
{
	const FHFShowerBuild Standard = FHFSanitaryKit::BuildShower(MakeShower());

	FHFShowerParams Bigger = MakeShower();
	Bigger.RoseDiameter = 25.0;
	Bigger.Height = 220.0;

	const FHFShowerBuild Grand = FHFSanitaryKit::BuildShower(Bigger);

	TestTrue(TEXT("A larger shower builds"), Grand.bValid);
	TestTrue(TEXT("Its arm is higher"),
		Grand.Shell.GetBounds().Max.Z > Standard.Shell.GetBounds().Max.Z + 8.0);
	TestTrue(TEXT("Its rose is bigger"), Volume(Grand.Shell) > Volume(Standard.Shell));

	// AN ARM MAY NOT REACH PAST THE WET AREA. A rose hanging over dry floor is a shower that soaks
	// the room it is in, so the request is clamped rather than honoured.
	FHFShowerParams Overreaching = MakeShower();
	Overreaching.ArmReach = 300.0;

	const FHFShowerBuild Clamped = FHFSanitaryKit::BuildShower(Overreaching);
	TestTrue(TEXT("An impossible arm is clamped, not refused"), Clamped.bValid);
	TestTrue(TEXT("And the rose stays over the wet area"),
		Clamped.RoseCentre.Y > -Overreaching.Depth * 0.5);

	FHFShowerParams Nothing;
	Nothing.Height = 0.0;

	const FHFShowerBuild Empty = FHFSanitaryKit::BuildShower(Nothing);
	TestFalse(TEXT("A shower with no height does not build"), Empty.bValid);
	TestEqual(TEXT("And emits no geometry at all"), Empty.Shell.TriangleCount(), 0);

	return true;
}

// ======================================================================================== vanity

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFVanityTopTest, "HouseForge.Sanitary.VanityCarriesItsOwnTop",
	HF_TEST_FLAGS)

bool FHFVanityTopTest::RunTest(const FString& Parameters)
{
	FHFFixture Fixture;
	Fixture.Id = TEXT("F_MBath_Vanity");
	Fixture.Type = EHFFixtureType::Vanity;
	Fixture.Footprint = FVector2D(90.0, 50.0);
	Fixture.Height = 80.0;
	Fixture.Params.ShutterCount = 2;
	Fixture.Params.DrawerCount = 1;

	const FHFCasedGoodsParams P = AHFCasedGoodsActor::ParamsFor(Fixture);
	const FHFCasedGoodsBuild Built = FHFCasedGoodsKit::Build(P);

	TestTrue(TEXT("A vanity builds"), Built.bValid);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));
	TestTrue(TEXT("It is a cased good the composing layer builds"),
		AHFCasedGoodsActor::Builds(EHFFixtureType::Vanity));

	TestTrue(TEXT("It has a stone top"), P.TopThickness > 0.0);
	TestTrue(TEXT("Which is really built"), Built.Top.TriangleCount() > 0);
	TestTrue(TEXT("In stone"),
		FHFMeshOps::RolesPresent(Built.Top).Contains(EHFSurfaceRole::CounterStone));

	// ------------------------------------------------------- THE TOP COMES OUT OF THE DRAWN HEIGHT
	//
	// Not off the top of it, which is the whole of the difference between a top and a cornice. The
	// drawn 800 IS the working surface, because the basin, the tap and the mirror above are all set
	// out from it - so a top built above the 800 would present its stone at 820 and every one of them
	// would be 20 mm out.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("The finished top is exactly the drawn height"), Bounds.Max.Z, 80.0, 0.01);
	TestNearlyEqual(TEXT("Which is where the carcass stack ends plus the slab"),
		P.TopBottomZ() + P.TopThickness, 80.0, 0.01);
	TestNearlyEqual(TEXT("And the built height is unchanged by it"), P.BuiltHeight(), 80.0, 0.01);

	// The stone OVERSAILS the doors it covers, which is what a fabricator leaves and what stops the
	// drip running down the shutter faces.
	AddInfo(FString::Printf(TEXT("The slab's front is at Y %.2f, the shutter face at %.2f."),
		Bounds.Min.Y, Built.ShutterFaceY));

	TestTrue(TEXT("The stone stands proud of the shutters"), Bounds.Min.Y < Built.ShutterFaceY - 1.0);

	// ---------------------------------------------------------------- AND ITS FRONT ARRIS IS EASED
	//
	// A vanity top is at hand height in a small room and its edge is the only part of it anybody
	// touches. Measured rather than trusted, because the bullnose is cut by BOOLEANS and a boolean
	// that fails leaves a perfectly good SQUARE edge with nothing but a line in a log to say so - and
	// because the steps that make the round are axis-aligned, so no amount of looking at face normals
	// distinguishes an eased edge from a square one.
	//
	// What does distinguish them is HOW MUCH of the front face is still at the front plane. Square,
	// the whole 90 x 3 elevation stands there; worked, the top and bottom bands have stepped back and
	// only the middle is left.
	double AtTheFrontPlane = 0.0;

	for (const int32 Tri : Built.Top.TriangleIndicesItr())
	{
		FVector3d A, B, C;
		Built.Top.GetTriVertices(Tri, A, B, C);

		const FVector3d Centre = (A + B + C) / 3.0;
		if (Centre.Y < Bounds.Min.Y + 0.01 && Built.Top.GetTriNormal(Tri).Y < -0.9)
		{
			AtTheFrontPlane += Built.Top.GetTriArea(Tri);
		}
	}

	const double SquareEdge = P.Width * P.TopThickness;
	AddInfo(FString::Printf(TEXT("The front elevation is %.0f cm2 of a possible %.0f."),
		AtTheFrontPlane, SquareEdge));

	TestTrue(TEXT("The front edge is really worked, not left square"),
		AtTheFrontPlane < SquareEdge * 0.7);
	TestTrue(TEXT("But there is still a face to it"), AtTheFrontPlane > SquareEdge * 0.1);

	// A run with no top is untouched by any of this, which is what keeps the nine other cased goods in
	// the flat safe. Built from a fresh set of dimensions rather than from P with the top switched
	// off, because Sanitise has already resolved P's unit heights against a stack the top was taking
	// its share of - and a carcass keeps an explicit height once it has one.
	FHFCasedGoodsParams NoTop;
	NoTop.Width = 90.0;
	NoTop.Depth = 50.0;
	NoTop.Height = 80.0;
	NoTop.TopThickness = 0.0;

	const FHFCasedGoodsBuild Plain = FHFCasedGoodsKit::Build(NoTop);
	TestTrue(TEXT("A run with no top still builds"), Plain.bValid);
	TestEqual(TEXT("And has no top mesh"), Plain.Top.TriangleCount(), 0);
	TestNearlyEqual(TEXT("And is still exactly as tall as it is drawn"),
		Plain.Shell.GetBounds().Max.Z, 80.0, 0.01);

	return true;
}

// ======================================================================================== geyser

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGeyserFormTest, "HouseForge.Sanitary.GeyserIsARoundVessel",
	HF_TEST_FLAGS)

bool FHFGeyserFormTest::RunTest(const FString& Parameters)
{
	const FHFApplianceBuild Built = FHFApplianceKit::BuildGeyser(MakeGeyser());

	TestTrue(TEXT("A geyser builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	// INSIDE THE DRAWN BOX, all of it - including the pipework, which is clamped for exactly this
	// reason. The ceiling fit, the validator's clash rule and every clearance measured in the built
	// flat all test against the declared envelope, and a fitting that quietly leaves its own box makes
	// all three answer about something that is not there.
	TestNearlyEqual(TEXT("It is as long as it is drawn"), Bounds.Max.X - Bounds.Min.X, 45.0, 0.05);
	TestTrue(TEXT("It does not leave its drawn depth"),
		Bounds.Min.Y >= -20.0 - 0.01 && Bounds.Max.Y <= 20.0 + 0.01);
	TestTrue(TEXT("Nor its drawn height"),
		Bounds.Min.Z >= -0.01 && Bounds.Max.Z <= 45.0 + 0.01);

	// ITS BACK IS ON THE WALL. A cylinder centred in the drawn box would hang a bracket's thickness
	// off the plaster, on a fitting that is bolted to it.
	TestNearlyEqual(TEXT("The bracket reaches the wall line"), Bounds.Max.Y, 20.0, 0.05);

	// ROUND, AND IT HAS TO BE. A geyser's whole silhouette is its curvature and it hangs at 2100 where
	// it is seen against the ceiling from every part of the room. Measured as the ratio of the solid
	// to the box it sits in: a cylinder in a square section fills about pi/4 of it, a box fills all.
	const double Envelope = Bounds.Width() * Bounds.Depth() * Bounds.Height();
	const double Fill = Volume(Built.Shell) / Envelope;

	AddInfo(FString::Printf(TEXT("The vessel fills %.0f%% of its drawn box."), Fill * 100.0));

	TestTrue(TEXT("It is not a box"), Fill < 0.72);
	TestTrue(TEXT("It is not a pencil either"), Fill > 0.4);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFGeyserDialTest, "HouseForge.Sanitary.GeyserThermostatTurns",
	HF_TEST_FLAGS)

bool FHFGeyserDialTest::RunTest(const FString& Parameters)
{
	const FHFApplianceBuild Built = FHFApplianceKit::BuildGeyser(MakeGeyser());

	const FHFMeshPart* Dial = FindPart(Built.Parts, FHFApplianceKit::ThermostatPartId());
	if (Dial == nullptr)
	{
		AddError(TEXT("A geyser must have a thermostat dial."));
		return false;
	}

	// The POINTER, which is the only thing on a dial that shows it has turned at all. A knob measured
	// at its own centre reports zero travel however far it goes round.
	const FVector Pointer(-1.9, 0.0, 2.0);
	const FVector Cold = PosedPoint(*Dial, Pointer, 0.0);
	const FVector Hot = PosedPoint(*Dial, Pointer, 1.0);

	AddInfo(FString::Printf(TEXT("The thermostat's pointer sweeps %.2f cm."), FVector::Dist(Cold, Hot)));

	TestTrue(TEXT("The pointer actually sweeps"), FVector::Dist(Cold, Hot) > 1.5);

	// About the vessel's own axis, which is the only axis a dial on an end cap can turn about: it must
	// not move along the cylinder while it turns.
	TestNearlyEqual(TEXT("It turns in its own plane"), Hot.X, Cold.X, 0.01);

	// Degenerate input: an empty mesh rather than a sliver.
	FHFGeyserParams Nothing;
	Nothing.Depth = 0.0;
	Nothing.Height = 0.0;

	const FHFApplianceBuild Empty = FHFApplianceKit::BuildGeyser(Nothing);
	TestFalse(TEXT("A geyser with no section does not build"), Empty.bValid);
	TestEqual(TEXT("And emits no geometry at all"), Empty.Shell.TriangleCount(), 0);

	// And it answers to its parameters: a longer vessel is a bigger one.
	FHFGeyserParams Bigger = MakeGeyser();
	Bigger.Length = 60.0;

	const FHFApplianceBuild Large = FHFApplianceKit::BuildGeyser(Bigger);
	TestTrue(TEXT("A longer geyser builds"), Large.bValid);
	TestTrue(TEXT("And holds more"), Volume(Large.Shell) > Volume(Built.Shell) * 1.2);

	return true;
}

// ==================================================================== mirror and towel rail

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFMirrorTest, "HouseForge.Sanitary.MirrorIsABevelledPlate",
	HF_TEST_FLAGS)

bool FHFMirrorTest::RunTest(const FString& Parameters)
{
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildMirror(MakeMirror());

	TestTrue(TEXT("A mirror builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));
	TestTrue(TEXT("And the plate itself is glass"),
		FHFMeshOps::RolesPresent(Built.Shell).Contains(EHFSurfaceRole::Glass));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("It is as wide as it is drawn"), Bounds.Max.X - Bounds.Min.X, 60.0, 0.01);
	TestNearlyEqual(TEXT("It is as tall as it is drawn"), Bounds.Max.Z - Bounds.Min.Z, 80.0, 0.01);
	TestNearlyEqual(TEXT("Its bottom edge is on the origin"), Bounds.Min.Z, 0.0, 0.01);

	// THE WHOLE 30 MM IS USED. A drawing gives a mirror one dimension in this direction and it is the
	// build-up standing off the plaster; a fitting that came out 18 mm thick would hang in a recess it
	// does not have. See FHFMirrorParams::BackingThickness.
	TestNearlyEqual(TEXT("It fills its drawn depth"), Bounds.Max.Y - Bounds.Min.Y, 3.0, 0.01);
	TestNearlyEqual(TEXT("With its back on the wall line"), Bounds.Max.Y, 1.5, 0.01);

	// ------------------------------------------------------------------------- and it is BEVELLED
	//
	// The one thing about a frameless mirror for light to catch. Without it the plate is a grey
	// rectangle that reads as a hole in the wall rather than as an object on it - so the front face
	// must genuinely be smaller than the back, by the bevel, on all four edges.
	const FHFMirrorParams Used = FHFWallPlateKit::SanitiseMirror(MakeMirror());
	const double GlassBackY = Used.Depth * 0.5 - Used.BackingThickness();

	double FrontHalfWidth = 0.0;
	double BackHalfWidth = 0.0;

	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		const FVector3d Point = Built.Shell.GetVertex(Vertex);
		if (Point.Y < Bounds.Min.Y + 0.01)
		{
			FrontHalfWidth = FMath::Max(FrontHalfWidth, FMath::Abs(Point.X));
		}
		if (FMath::Abs(Point.Y - GlassBackY) < 0.01)
		{
			BackHalfWidth = FMath::Max(BackHalfWidth, FMath::Abs(Point.X));
		}
	}

	AddInfo(FString::Printf(TEXT("Glass is %.1f cm across at the back and %.1f at the polished face."),
		BackHalfWidth * 2.0, FrontHalfWidth * 2.0));

	TestNearlyEqual(TEXT("The glass's back edge is the full plate"), BackHalfWidth, 30.0, 0.01);
	TestNearlyEqual(TEXT("And the polished face is inset by the bevel"),
		BackHalfWidth - FrontHalfWidth, Used.BevelWidth, 0.01);

	// Nothing moves, and it is asserted rather than assumed - a mirror CABINET would open.
	TestEqual(TEXT("A mirror has no moving part"), Built.Parts.Num(), 0);

	// Degenerate input.
	FHFMirrorParams Nothing;
	Nothing.Width = 0.0;

	const FHFWallPlateBuild Empty = FHFWallPlateKit::BuildMirror(Nothing);
	TestFalse(TEXT("A mirror with no width does not build"), Empty.bValid);
	TestEqual(TEXT("And emits no geometry at all"), Empty.Shell.TriangleCount(), 0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTowelRailTest, "HouseForge.Sanitary.TowelRailIsATubeOnBrackets",
	HF_TEST_FLAGS)

bool FHFTowelRailTest::RunTest(const FString& Parameters)
{
	const FHFFrameBuild Built = FHFFrameKit::BuildTowelRail(MakeTowelRail());

	TestTrue(TEXT("A towel rail builds"), Built.bValid);
	TestTrue(TEXT("It is closed"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestNearlyEqual(TEXT("The rail is as long as it is drawn"), Bounds.Max.X - Bounds.Min.X, 50.0, 0.05);
	TestNearlyEqual(TEXT("It stands the drawn distance off the wall"),
		Bounds.Max.Y - Bounds.Min.Y, 4.0, 0.05);
	TestNearlyEqual(TEXT("With its flanges on the wall line"), Bounds.Max.Y, 2.0, 0.01);

	// A rail does not FILL its drawn height - the box is the bracket's envelope - but it may not leave
	// it either, which is what a clash check against the wall behind actually needs.
	TestTrue(TEXT("It stays inside its drawn height"),
		Bounds.Min.Z >= -0.01 && Bounds.Max.Z <= 6.0 + 0.01);

	// THIN, AND THAT IS THE POINT OF THE TYPE. A rail whose members had swollen to fill their box
	// would be a shelf. Measured as fill: a 19 mm tube and two flanges in a 500 x 40 x 60 box is a
	// few percent of it.
	const double Envelope = Bounds.Width() * Bounds.Depth() * Bounds.Height();
	const double Fill = Volume(Built.Shell) / Envelope;

	AddInfo(FString::Printf(TEXT("The rail fills %.1f%% of its drawn box."), Fill * 100.0));
	TestTrue(TEXT("It is made of thin members"), Fill < 0.3);

	// ONE PIECE. Members run INTO what they land on rather than up to it, so a rail, its stems and its
	// flanges are a single closed solid rather than five that happen to touch - which at this section
	// is the difference between a joint and a visible dark seam.
	TestTrue(TEXT("The whole fitting is one closed solid"), FHFMeshOps::IsClosed(Built.Shell));

	TestEqual(TEXT("A straight rail has no moving part"), Built.Parts.Num(), 0);

	// It answers to its parameters.
	FHFTowelRailParams Fatter = MakeTowelRail();
	Fatter.RailDiameter = 2.5;

	const FHFFrameBuild Chunky = FHFFrameKit::BuildTowelRail(Fatter);
	TestTrue(TEXT("A heavier rail builds"), Chunky.bValid);
	TestTrue(TEXT("And is heavier"), Volume(Chunky.Shell) > Volume(Built.Shell) * 1.2);

	FHFTowelRailParams Nothing;
	Nothing.Width = 0.0;

	const FHFFrameBuild Empty = FHFFrameKit::BuildTowelRail(Nothing);
	TestFalse(TEXT("A rail with no length does not build"), Empty.bValid);
	TestEqual(TEXT("And emits no geometry at all"), Empty.Shell.TriangleCount(), 0);

	return true;
}

#endif	// WITH_DEV_AUTOMATION_TESTS
