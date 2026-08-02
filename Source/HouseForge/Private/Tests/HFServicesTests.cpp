// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Actors/HFServiceActors.h"
#include "Geometry/HFApplianceKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWallPlateKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFArticulation.h"
#include "Model/HFCeilingFit.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The services group, on the bench: eight sockets, five switch plates, a consumer unit, three split
// AC heads, two condensing units, a refrigerator and a washing machine - twenty-one in all.
//
// Measured on volume, watertightness, bounds against the drawn box, roles, swept transforms and
// visible apertures in centimetres - never on a triangle count. See .claude/rules/04-conventions.md.
//
// THE MOTION ASSERTIONS HERE ARE DELIBERATELY NOT "DID IT MOVE". Every sign in this group had a
// plausible wrong answer that travelled exactly as far as the right one:
//
//   - a refrigerator door hinged the other way sweeps its full 110 degrees INTO the wall behind the
//     cabinet, and opens nothing;
//   - a switch rocker that translated instead of rocking moves its face just as far, and is a button;
//   - a split AC's vane hinged at its front lip throws its tip forward out of the casing rather than
//     down out of the discharge;
//   - and seven vertical deflectors ganged onto ONE axis swing the end blade clean out of a 65 mm
//     channel while every one of them reports the same rotation.
//
// So each is checked for DIRECTION and for what it uncovers, in centimetres.
//
// ---------------------------------------------------------------------------------------------

namespace
{
	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	// FAxisAlignedBox3d::Height() IS ITS Y EXTENT AND Depth() ITS Z, which is the opposite of every
	// convention in this plugin - here Y runs into the wall and Z is up. Four assertions in this file
	// were written against the names and measured the wrong axis, and each of them still produced a
	// perfectly plausible number. Named for the axis instead, so there is nothing left to misread.
	double AlongX(const FAxisAlignedBox3d& Box) { return Box.Max.X - Box.Min.X; }
	double AlongY(const FAxisAlignedBox3d& Box) { return Box.Max.Y - Box.Min.Y; }
	double AlongZ(const FAxisAlignedBox3d& Box) { return Box.Max.Z - Box.Min.Z; }

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

	/**
	 * How many mesh vertices sit on a circle of a given radius about a point, in the XZ plane.
	 *
	 * THE MEASUREMENT THAT SAYS A HOLE IS A HOLE. A socket's pin apertures and a fan guard's slots
	 * are the difference between a perforated fitting and a picture of one, and neither volume nor
	 * bounds nor watertightness can tell the two apart - a plate with three holes in it and a plate
	 * with three dark dots painted on it are both closed solids of very nearly the same volume.
	 *
	 * What separates them is that a real perforation puts VERTICES on its own outline. Counting them
	 * is exact, cheap, and cannot be satisfied by anything except geometry that was actually cut.
	 */
	int32 VerticesOnCircleXZ(const FDynamicMesh3& Mesh, const FVector2D& CentreXZ, double Radius,
		double Tolerance)
	{
		int32 Count = 0;

		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(Vertex);
			const double Distance = FVector2D(P.X - CentreXZ.X, P.Z - CentreXZ.Y).Size();

			if (FMath::Abs(Distance - Radius) <= Tolerance)
			{
				++Count;
			}
		}

		return Count;
	}

	FHFAccessoryPlateParams MakeSocket()
	{
		FHFAccessoryPlateParams P;
		P.Width = 16.0;
		P.Height = 12.0;
		P.Depth = 2.0;
		P.GangCount = 2;
		P.Kind = EHFAccessoryKind::Socket;
		return P;
	}

	FHFAccessoryPlateParams MakeSwitchPlate(int32 Gangs)
	{
		FHFAccessoryPlateParams P;
		P.Width = 4.0 * Gangs;
		P.Height = 15.0;
		P.Depth = 2.0;
		P.GangCount = Gangs;
		P.Kind = EHFAccessoryKind::Switch;
		return P;
	}

	FHFDistributionBoardParams MakeBoard()
	{
		FHFDistributionBoardParams P;
		P.Width = 30.0;
		P.Height = 35.0;
		P.Depth = 6.0;
		P.WayCount = 8;
		return P;
	}

	FHFSplitACParams MakeSplitAC()
	{
		FHFSplitACParams P;
		P.Length = 90.0;
		P.Depth = 22.0;
		P.Height = 30.0;
		P.DeflectorCount = 7;
		return P;
	}

	FHFCondenserParams MakeCondenser()
	{
		FHFCondenserParams P;
		P.Width = 80.0;
		P.Depth = 35.0;
		P.Height = 60.0;
		return P;
	}

	FHFRefrigeratorParams MakeFridge()
	{
		FHFRefrigeratorParams P;
		P.Width = 70.0;
		P.Depth = 70.0;
		P.Height = 180.0;
		P.SkirtingSetback = 2.0;
		return P;
	}

	FHFWashingMachineParams MakeWasher()
	{
		FHFWashingMachineParams P;
		P.Width = 60.0;
		P.Depth = 60.0;
		P.Height = 85.0;
		P.SkirtingSetback = 2.0;
		return P;
	}
}

// =============================================================================================
//
// The modular accessories.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSocketPlateTest,
	"HouseForge.Services.SocketIsAPerforatedThreeLayerPlate", HF_TEST_FLAGS)

bool FHFSocketPlateTest::RunTest(const FString&)
{
	const FHFAccessoryPlateParams P = MakeSocket();
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(P);

	TestTrue(TEXT("A socket plate builds"), Built.bValid);
	TestTrue(TEXT("The plate is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	// ---------------------------------------------------------- the drawn box, and nothing past it
	//
	// THIRTEEN OF THESE AT EYE LEVEL. The stand-off is the whole reading of the fitting and a plate
	// that quietly grew past its drawn depth would be wrong in every room at once.
	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestEqual(TEXT("It is exactly as wide as it is drawn"), AlongX(Bounds), P.Width, 0.01);
	TestEqual(TEXT("It is exactly as tall as it is drawn"), AlongZ(Bounds), P.Height, 0.01);

	TestEqual(TEXT("Its back lands on the plaster"), Bounds.Max.Y, P.Depth * 0.5, 0.01);
	TestEqual(TEXT("Nothing stands past the drawn stand-off"), Bounds.Min.Y, -P.Depth * 0.5, 0.01);

	// ------------------------------------------------------------------- the pins are actual holes
	//
	// See VerticesOnCircleXZ. Three apertures per gang, each an eight-sided outline appearing on both
	// faces of the plate the prism was triangulated from - so at least sixteen vertices land on each
	// pin circle, and none would if the face were a solid slab with a decal on it.
	const double ApertureW = P.ApertureWidth();
	const double Pitch = ApertureW / static_cast<double>(P.GangCount);
	const double Gap = FMath::Min(0.3, Pitch * 0.12);
	const double GangWidth = Pitch - Gap;
	const double OutletWidth = GangWidth * 0.62;
	const double ModuleH = P.ModuleHeight;

	const double PinRadius = FMath::Min(0.28, OutletWidth * 0.075);
	const double PinSpread = FMath::Min(OutletWidth * 0.24, ModuleH * 0.24);

	int32 PerforatedOutlets = 0;

	for (int32 Gang = 0; Gang < P.GangCount; ++Gang)
	{
		const double GangCentreX = -ApertureW * 0.5 + (static_cast<double>(Gang) + 0.5) * Pitch;
		const double OutletCentreX = GangCentreX - GangWidth * 0.5 + OutletWidth * 0.5;
		const double CentreZ = P.Height * 0.5;

		const int32 Live = VerticesOnCircleXZ(Built.Shell,
			FVector2D(OutletCentreX - PinSpread, CentreZ - ModuleH * 0.16), PinRadius, 0.02);
		const int32 Neutral = VerticesOnCircleXZ(Built.Shell,
			FVector2D(OutletCentreX + PinSpread, CentreZ - ModuleH * 0.16), PinRadius, 0.02);

		if (Live >= 16 && Neutral >= 16)
		{
			++PerforatedOutlets;
		}
	}

	TestEqual(TEXT("Every gang's outlet is genuinely perforated"), PerforatedOutlets, P.GangCount);

	// ----------------------------------------------------------------------- and one rocker a gang
	TestEqual(TEXT("One rocker per gang"), Built.Parts.Num(), P.GangCount);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSwitchPlateTest,
	"HouseForge.Services.SwitchPlateCarriesOneModulePerGang", HF_TEST_FLAGS)

bool FHFSwitchPlateTest::RunTest(const FString&)
{
	for (const int32 Gangs : { 4, 6, 8 })
	{
		const FHFAccessoryPlateParams P = MakeSwitchPlate(Gangs);
		const FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(P);

		TestTrue(FString::Printf(TEXT("A %d gang plate builds"), Gangs), Built.bValid);
		TestTrue(FString::Printf(TEXT("A %d gang plate is watertight"), Gangs),
			FHFMeshOps::IsClosed(Built.Shell));
		TestEqual(FString::Printf(TEXT("A %d gang plate has %d rockers"), Gangs, Gangs),
			Built.Parts.Num(), Gangs);

		const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
		TestEqual(FString::Printf(TEXT("A %d gang plate is as wide as drawn"), Gangs),
			AlongX(Bounds), P.Width, 0.01);
		TestEqual(FString::Printf(TEXT("A %d gang plate stands off exactly as drawn"), Gangs),
			AlongY(Bounds), P.Depth, 0.01);

		// A MODULE IS A BOUGHT SIZE, NOT A SHARE OF THE PLATE. A 150 mm plate does not carry a 150 mm
		// rocker; it carries a 45 mm one with blanked grid above and below, and getting that wrong
		// turns every plate in the flat into a row of full-height paddles.
		for (int32 Gang = 0; Gang < Gangs; ++Gang)
		{
			const FHFMeshPart* Rocker = FindPart(Built.Parts, FHFWallPlateKit::RockerPartId(Gang));
			if (Rocker == nullptr)
			{
				continue;
			}

			const FBox Shut = PosedBounds(*Rocker, 0.0);

			TestEqual(FString::Printf(TEXT("Rocker %d is one module tall"), Gang),
				Shut.GetSize().Z, P.ModuleHeight, 0.05);
			TestTrue(FString::Printf(TEXT("Rocker %d is narrower than its gang's pitch"), Gang),
				Shut.GetSize().X < P.ApertureWidth() / static_cast<double>(Gangs));
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRockerMotionTest,
	"HouseForge.Services.AccessoryRockersRockRatherThanTranslate", HF_TEST_FLAGS)

bool FHFRockerMotionTest::RunTest(const FString&)
{
	const FHFAccessoryPlateParams P = MakeSwitchPlate(6);
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(P);

	const FHFMeshPart* Rocker = FindPart(Built.Parts, FHFWallPlateKit::RockerPartId(0));
	if (Rocker == nullptr)
	{
		AddError(TEXT("No rocker was built."));
		return false;
	}

	TestEqual(TEXT("A rocker hinges"), Rocker->Motion.Type, EHFMotionType::Hinge);

	// The two ends of the visible face, in the part's own space: the front plane is at -RockerProud
	// and the module is P.ModuleHeight tall about the pivot.
	const FVector TopFace(0.0, -P.RockerProud, P.ModuleHeight * 0.5);
	const FVector BottomFace(0.0, -P.RockerProud, -P.ModuleHeight * 0.5);

	const FVector TopShut = PosedPoint(*Rocker, TopFace, 0.0);
	const FVector TopPressed = PosedPoint(*Rocker, TopFace, 1.0);
	const FVector BottomShut = PosedPoint(*Rocker, BottomFace, 0.0);
	const FVector BottomPressed = PosedPoint(*Rocker, BottomFace, 1.0);

	const double TopTravel = TopPressed.Y - TopShut.Y;
	const double BottomTravel = BottomPressed.Y - BottomShut.Y;

	// ------------------------------------------------------------------ A SEE-SAW, NOT A BUTTON
	//
	// The whole assertion of this group. +Y is into the wall, so the top edge must go IN as the bottom
	// edge comes OUT - and a rocker modelled as a plunger would move both ends the same way by
	// exactly the same distance, which is a switch that measures perfectly and cannot be flicked.
	TestTrue(TEXT("The top edge presses into the wall"), TopTravel > 0.0);
	TestTrue(TEXT("The bottom edge comes out of the wall"), BottomTravel < 0.0);

	// AND BY A SWITCH'S OWN TRAVEL. 2-3 mm at the edge is what one actually moves; a centimetre would
	// be a lever, and a tenth of a millimetre would be a moulding pretending to be a control.
	TestTrue(FString::Printf(TEXT("The edge travels like a switch (%.2f mm)"), TopTravel * 10.0),
		TopTravel > 0.15 && TopTravel < 0.5);

	TestEqual(TEXT("The two edges travel equally and oppositely"),
		TopTravel, -BottomTravel, 0.01);

	// ---------------------------------------------------- and it stays inside the drawn stand-off
	//
	// The rocker's throw is DERIVED from the recess it sits in rather than trusted from a figure, so a
	// pressed switch may not break the plane of the plate around it in any pose.
	const FBox Pressed = PosedBounds(*Rocker, 1.0);

	TestTrue(FString::Printf(
		TEXT("A pressed rocker stays behind the cover face (front at %.3f, cover at %.3f)"),
		Pressed.Min.Y, -P.Depth * 0.5),
		Pressed.Min.Y >= -P.Depth * 0.5 - 0.001);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFAccessoryParamsTest,
	"HouseForge.Services.AccessoryPlateAnswersToItsParameters", HF_TEST_FLAGS)

bool FHFAccessoryParamsTest::RunTest(const FString&)
{
	const double FourGang = Volume(FHFWallPlateKit::BuildAccessoryPlate(MakeSwitchPlate(4)).Shell);
	const double EightGang = Volume(FHFWallPlateKit::BuildAccessoryPlate(MakeSwitchPlate(8)).Shell);

	TestTrue(TEXT("A wider plate has more material in it"), EightGang > FourGang * 1.5);

	// The kind is not a label: a socket carries an outlet body and a perforated face that a switch
	// plate does not, so the two are different objects at the same size.
	FHFAccessoryPlateParams AsSwitch = MakeSocket();
	AsSwitch.Kind = EHFAccessoryKind::Switch;

	const double SocketVolume = Volume(FHFWallPlateKit::BuildAccessoryPlate(MakeSocket()).Shell);
	const double SwitchVolume = Volume(FHFWallPlateKit::BuildAccessoryPlate(AsSwitch).Shell);

	TestTrue(TEXT("A socket and a switch plate of one size are different objects"),
		FMath::Abs(SocketVolume - SwitchVolume) > 0.5);

	// A deeper plate stands further off the wall, and by exactly the difference.
	FHFAccessoryPlateParams Deep = MakeSwitchPlate(6);
	Deep.Depth = 3.0;

	const FAxisAlignedBox3d Shallow =
		FHFWallPlateKit::BuildAccessoryPlate(MakeSwitchPlate(6)).Shell.GetBounds();
	const FAxisAlignedBox3d Deeper = FHFWallPlateKit::BuildAccessoryPlate(Deep).Shell.GetBounds();

	TestEqual(TEXT("A deeper plate stands off by its own depth"),
		AlongY(Deeper) - AlongY(Shallow), 1.0, 0.01);

	return true;
}

// =============================================================================================
//
// The consumer unit.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFConsumerUnitFormTest,
	"HouseForge.Services.ConsumerUnitIsAHollowEnclosure", HF_TEST_FLAGS)

bool FHFConsumerUnitFormTest::RunTest(const FString&)
{
	const FHFDistributionBoardParams P = MakeBoard();
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildDistributionBoard(P);

	TestTrue(TEXT("A board builds"), Built.bValid);
	TestTrue(TEXT("The enclosure is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();
	TestEqual(TEXT("It is as wide as drawn"), AlongX(Bounds), P.Width, 0.01);
	TestEqual(TEXT("It is as tall as drawn"), AlongZ(Bounds), P.Height, 0.01);
	TestEqual(TEXT("Its back lands on the plaster"), Bounds.Max.Y, P.Depth * 0.5, 0.01);

	// A TRAY, NOT A BLOCK. The whole point of a consumer unit is that its door opens onto something,
	// and a solid box would fill the drawn envelope. Well under half of it is the honest figure for a
	// pressed enclosure with a rail of breakers in it.
	const double BoxVolume = P.Width * P.Height * P.Depth;
	const double Fill = Volume(Built.Shell) / BoxVolume;

	TestTrue(FString::Printf(TEXT("The enclosure is hollow rather than solid (%.0f%% filled)"),
		Fill * 100.0), Fill > 0.05 && Fill < 0.55);

	// A door and a toggle per way.
	TestEqual(TEXT("A door and one toggle per way"), Built.Parts.Num(), P.WayCount + 1);
	TestTrue(TEXT("The door is there"),
		FindPart(Built.Parts, FHFWallPlateKit::DoorPartId()) != nullptr);

	// ------------------------------------------------- AND EVERY BREAKER IS BEHIND THE DOOR, SHUT AND OFF
	//
	// THE DEFECT THIS ASSERTION EXISTS FOR. The breaker bodies and the toggles standing out of them
	// share the enclosure's depth with the door, and built to the interior depth with the tab added on
	// top the toggles stood 2 mm PAST the drawn box - straight through the glazing, a row of metal
	// tabs poking out of the front of the board.
	//
	// Nothing in a part's own measurements sees it: a toggle is correct, it throws the right way and
	// the right distance, and it is the LEAF IN FRONT OF IT that it is wrong about. Only the whole
	// assembly, measured against the box it is supposed to fit inside, says so. It was found in the
	// built flat, by a fitting reporting 6.20 cm proud against a drawn 6.00.
	for (const FHFMeshPart& Part : Built.Parts)
	{
		if (Part.PartId == FHFWallPlateKit::DoorPartId())
		{
			continue;
		}

		for (const double Amount : { 0.0, 1.0 })
		{
			const FBox Posed = PosedBounds(Part, Amount);

			TestTrue(*FString::Printf(
				TEXT("'%s' stays behind the door at %.0f%% thrown (front at %.2f, door at %.2f)"),
				*Part.PartId.ToString(), Amount * 100.0, Posed.Min.Y,
				-P.Depth * 0.5 + P.DoorThickness),
				Posed.Min.Y >= -P.Depth * 0.5 + P.DoorThickness - 0.001);
		}
	}

	// AND THE ENCLOSURE ITSELF STOPS WHERE THE DOOR BEGINS. The shell reaches the door's back face and
	// no further; the drawn front of the box belongs to the leaf. A tray built to the full depth would
	// leave the door hung in front of a box that already filled the fitting.
	TestEqual(TEXT("The enclosure stops where its door begins"),
		Bounds.Min.Y, -P.Depth * 0.5 + P.DoorThickness, 0.01);

	// And the door takes it to exactly the drawn box, with nothing past it.
	const FHFMeshPart* Door = FindPart(Built.Parts, FHFWallPlateKit::DoorPartId());
	if (Door != nullptr)
	{
		TestEqual(TEXT("The shut door lands exactly on the drawn front"),
			PosedBounds(*Door, 0.0).Min.Y, -P.Depth * 0.5, 0.01);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFConsumerUnitDoorTest,
	"HouseForge.Services.ConsumerUnitDoorOpensIntoTheRoom", HF_TEST_FLAGS)

bool FHFConsumerUnitDoorTest::RunTest(const FString&)
{
	const FHFDistributionBoardParams P = MakeBoard();
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildDistributionBoard(P);

	const FHFMeshPart* Door = FindPart(Built.Parts, FHFWallPlateKit::DoorPartId());
	if (Door == nullptr)
	{
		AddError(TEXT("No door was built."));
		return false;
	}

	// The free edge, in the door's own space: it is drawn from its hinge along +X.
	const FVector FreeEdge(P.Width, 0.0, 0.0);

	const FVector Shut = PosedPoint(*Door, FreeEdge, 0.0);
	const FVector Open = PosedPoint(*Door, FreeEdge, 1.0);

	// INTO THE ROOM, WHICH IS -Y. The other sign sweeps the same 110 degrees straight into the
	// plaster: it travels exactly as far, satisfies any assertion that the door "moved", and opens
	// nothing at all.
	TestTrue(FString::Printf(TEXT("The free edge swings out of the wall (from %.1f to %.1f)"),
		Shut.Y, Open.Y), Open.Y < Shut.Y - 1.0);

	TestTrue(TEXT("The door swings clear of the enclosure it covers"),
		Open.Y < -P.Width * 0.8);

	// AND THE APERTURE IT UNCOVERS, in centimetres. At full open the whole leaf has to be back beyond
	// its own hinge jamb, so not a millimetre of the board's face is still covered. A door that swung
	// to thirty degrees would measure as motion and leave the breakers unreachable behind it.
	const FBox OpenBounds = PosedBounds(*Door, 1.0);

	TestTrue(FString::Printf(
		TEXT("The whole face is uncovered: the leaf reaches back only to X %.1f, at the jamb on %.1f"),
		OpenBounds.Max.X, -P.Width * 0.5),
		OpenBounds.Max.X <= -P.Width * 0.5 + 1.0);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFConsumerUnitInterlockTest,
	"HouseForge.Services.ConsumerUnitBreakersWaitForTheDoor", HF_TEST_FLAGS)

bool FHFConsumerUnitInterlockTest::RunTest(const FString&)
{
	const FHFDistributionBoardParams P = MakeBoard();
	const FHFWallPlateBuild Built = FHFWallPlateKit::BuildDistributionBoard(P);

	// The same solve the actor runs, over the whole assembly rather than one part at a time.
	TArray<FHFPartState> States;
	for (const FHFMeshPart& Part : Built.Parts)
	{
		FHFPartState& State = States.AddDefaulted_GetRef();
		State.PartId = Part.PartId;
		State.PivotTransform = Part.PivotTransform;
		State.Motion = Part.Motion;
	}

	// Everything asked to open at once, which is what MasterOpenAmount does.
	for (FHFPartState& State : States)
	{
		State.OpenAmount = 1.0;
	}

	TArray<FHFUnresolvedDependency> Unresolved;
	TestTrue(TEXT("The assembly resolves without a cycle"),
		FHFArticulation::ResolvePartAmounts(States, nullptr, &Unresolved));
	TestEqual(TEXT("No toggle names a part that is not there"), Unresolved.Num(), 0);

	// -------------------------------------------------------- and now with the door held shut
	for (FHFPartState& State : States)
	{
		State.OpenAmount = State.PartId == FHFWallPlateKit::DoorPartId() ? 0.0 : 1.0;
	}

	FHFArticulation::ResolvePartAmounts(States);

	int32 Thrown = 0;
	for (const FHFPartState& State : States)
	{
		if (State.PartId != FHFWallPlateKit::DoorPartId() && State.OpenAmount > 0.001)
		{
			++Thrown;
		}
	}

	// A BREAKER CANNOT BE THROWN THROUGH A SHUT DOOR. Without the ordering every toggle resolves
	// unconstrained and the whole bank throws behind a closed lid, which is exactly the pose
	// FHFPartMotion::SequencedAfterPartId exists to make impossible.
	TestEqual(TEXT("Nothing throws behind a shut door"), Thrown, 0);

	return true;
}

// =============================================================================================
//
// The split AC indoor unit.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSplitACFormTest,
	"HouseForge.Services.SplitACIsAMouldedSectionNotABox", HF_TEST_FLAGS)

bool FHFSplitACFormTest::RunTest(const FString&)
{
	const FHFSplitACParams P = MakeSplitAC();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildSplitAC(P);

	TestTrue(TEXT("A split head builds"), Built.bValid);
	TestTrue(TEXT("The casing is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestEqual(TEXT("It is as long as drawn"), AlongX(Bounds), P.Length, 0.01);
	TestEqual(TEXT("It is as tall as drawn"), AlongZ(Bounds), P.Height, 0.01);
	TestEqual(TEXT("Its back lands on the plaster"), Bounds.Max.Y, P.Depth * 0.5, 0.01);
	TestEqual(TEXT("It reaches exactly its drawn projection"), Bounds.Min.Y, -P.Depth * 0.5, 0.01);

	// ------------------------------------------------------------------------- IT IS NOT A BOX
	//
	// Two measurements, and neither is a triangle count. A box fills its envelope; a moulding does
	// not, and a sliver would not fill enough of it to be a casing at all.
	const double BoxVolume = P.Length * P.Depth * P.Height;
	const double Fill = Volume(Built.Shell) / BoxVolume;

	TestTrue(FString::Printf(TEXT("The casing is a moulding, not a box (%.0f%% of its envelope)"),
		Fill * 100.0), Fill > 0.55 && Fill < 0.92);

	// AND ITS WIDEST POINT IS PART WAY UP, which is what a bulged front means and what no box has.
	// The forwardmost vertices must be neither at the top nor at the bottom of the drawn box.
	double FrontMostZ = -1.0;
	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		const FVector3d Point = Built.Shell.GetVertex(Vertex);
		if (Point.Y <= -P.Depth * 0.5 + 0.01)
		{
			FrontMostZ = Point.Z;
			break;
		}
	}

	TestTrue(FString::Printf(TEXT("The front bulges part way up the casing (at Z %.1f of %.1f)"),
		FrontMostZ, P.Height),
		FrontMostZ > P.Height * 0.2 && FrontMostZ < P.Height * 0.8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSplitACLouvreTest,
	"HouseForge.Services.SplitACLouvreOpensTheDischarge", HF_TEST_FLAGS)

bool FHFSplitACLouvreTest::RunTest(const FString&)
{
	const FHFSplitACParams P = MakeSplitAC();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildSplitAC(P);

	const FHFMeshPart* Louvre = FindPart(Built.Parts, FHFApplianceKit::LouvrePartId());
	if (Louvre == nullptr)
	{
		AddError(TEXT("No discharge vane was built."));
		return false;
	}

	// The front lip of the discharge channel, read off the same fractions the section is drawn from.
	const FVector2D Lip(-P.Depth * 0.5 * 0.680, P.Height * 0.140);

	// The vane's own tip, in its local space: it reaches forward from its hinge and rises to the lip.
	const FBox Shut = PosedBounds(*Louvre, 0.0);
	const FBox Open = PosedBounds(*Louvre, 1.0);

	// ---------------------------------------------------------------- SHUT MEANS SHUT, in centimetres
	//
	// The vane lies along the mouth rather than horizontally across it, so its tip arrives at the lip.
	// Drawn flat it leaves a 20 mm slot open with the machine switched off, which reads as a broken
	// louvre from anywhere in the room.
	const double ShutGap = FVector2D(Shut.Min.Y - Lip.X, Shut.Max.Z - Lip.Y).Size();

	TestTrue(FString::Printf(TEXT("Shut, the vane meets the lip (%.2f cm short)"), ShutGap),
		ShutGap < 1.2);

	// ------------------------------------------------------------ AND OPEN MEANS OPEN, in centimetres
	//
	// The visible aperture: how far the vane's tip has dropped away from the lip it was closed
	// against. That is exactly how far anybody can see into the discharge.
	const double Aperture = FVector2D(Open.Min.Y - Lip.X, Open.Min.Z - Lip.Y).Size();

	TestTrue(FString::Printf(TEXT("Open, the discharge is clear by %.1f cm"), Aperture),
		Aperture > 5.0);

	TestTrue(TEXT("The vane drops rather than lifting"), Open.Min.Z < Shut.Min.Z - 4.0);

	// AND IT SWINGS BACK, NOT FORWARD. Hinged on its rear axis the tip traces a circle about the back
	// of the channel, so it withdraws as it falls - which is what a real one does. A vane hinged at
	// the lip instead throws its tip forward out of the casing and into the room.
	TestTrue(FString::Printf(TEXT("The vane withdraws as it falls (front from %.1f to %.1f)"),
		Shut.Min.Y, Open.Min.Y), Open.Min.Y > Shut.Min.Y);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSplitACDeflectorTest,
	"HouseForge.Services.SplitACDeflectorsTurnOnTheirOwnPins", HF_TEST_FLAGS)

bool FHFSplitACDeflectorTest::RunTest(const FString&)
{
	const FHFSplitACParams P = MakeSplitAC();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildSplitAC(P);

	int32 Found = 0;

	for (int32 Fin = 0; Fin < P.DeflectorCount; ++Fin)
	{
		const FHFMeshPart* Deflector =
			FindPart(Built.Parts, FHFApplianceKit::DeflectorPartId(Fin));

		if (Deflector == nullptr)
		{
			continue;
		}

		++Found;

		const FBox Straight = PosedBounds(*Deflector, 0.0);
		const FBox Turned = PosedBounds(*Deflector, 1.0);

		// It actually turns: a blade square to the flow is a millimetre wide in X, and a turned one
		// presents its whole length.
		TestTrue(FString::Printf(TEXT("Deflector %d turns"), Fin),
			Turned.GetSize().X > Straight.GetSize().X + 1.0);

		// ------------------------------------------------ AND IT STAYS IN THE CHANNEL WHILE IT DOES
		//
		// THE ASSERTION THAT CAUGHT THE FIRST VERSION. Seven blades ganged onto one shared axis all
		// report exactly this rotation, and the end blade leaves the casing entirely - at 30 degrees
		// it swings 19 cm sideways out of a 6.5 cm channel. Ganged means they turn TOGETHER, not that
		// they turn about a common centre.
		TestTrue(FString::Printf(
			TEXT("Deflector %d stays inside the casing when turned (X %.1f..%.1f of +/-%.1f)"),
			Fin, Turned.Min.X, Turned.Max.X, P.Length * 0.5),
			Turned.Min.X > -P.Length * 0.5 && Turned.Max.X < P.Length * 0.5);

		TestTrue(FString::Printf(TEXT("Deflector %d stays inside the drawn projection"), Fin),
			Turned.Min.Y > -P.Depth * 0.5 && Turned.Max.Y < P.Depth * 0.5);
	}

	TestEqual(TEXT("Every deflector is its own part"), Found, P.DeflectorCount);

	// ------------------------------------------------- AND NONE OF THEM IS INSIDE THE SHUT VANE
	//
	// THE SECOND THING THE RENDER FOUND. The vane lies ALONG the mouth and rises towards the lip; the
	// fins were set out from the hinge at a constant height and did not rise with it, so every one of
	// them ran straight through the vane along its whole length. In the flat that is a closed louvre
	// with the deflector ticks showing through it - a discharge you can see into with the machine off.
	//
	// Measured as a real clearance rather than as an intersection test: the lowest fin has to be above
	// the highest point the shut vane reaches underneath it.
	const FHFMeshPart* Louvre = FindPart(Built.Parts, FHFApplianceKit::LouvrePartId());

	if (Louvre != nullptr)
	{
		const FBox VaneShut = PosedBounds(*Louvre, 0.0);

		for (int32 Fin = 0; Fin < P.DeflectorCount; ++Fin)
		{
			const FHFMeshPart* Deflector =
				FindPart(Built.Parts, FHFApplianceKit::DeflectorPartId(Fin));

			if (Deflector == nullptr)
			{
				continue;
			}

			const FBox Blade = PosedBounds(*Deflector, 0.0);

			// Only where they actually overlap in plan is there anything to clear.
			const bool bOverlapsInY = Blade.Min.Y < VaneShut.Max.Y && Blade.Max.Y > VaneShut.Min.Y;

			if (!bOverlapsInY)
			{
				continue;
			}

			// The vane's top at the fin's front edge, which is where the vane is highest under it.
			const double Fraction = FMath::Clamp(
				(VaneShut.Max.Y - Blade.Min.Y) / FMath::Max(VaneShut.Max.Y - VaneShut.Min.Y, 0.01),
				0.0, 1.0);
			const double VaneTopUnderFin =
				FMath::Lerp(VaneShut.Min.Z, VaneShut.Max.Z, Fraction);

			TestTrue(FString::Printf(
				TEXT("Deflector %d hangs above the shut vane (blade at %.2f, vane top %.2f)"),
				Fin, Blade.Min.Z, VaneTopUnderFin),
				Blade.Min.Z >= VaneTopUnderFin - 0.01);
		}
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFSplitACCeilingTest,
	"HouseForge.Services.SplitACLowersUnderADeepCeiling", HF_TEST_FLAGS)

bool FHFSplitACCeilingTest::RunTest(const FString&)
{
	// NOTHING IN THE REFERENCE FLAT IS SQUEEZED, AND THAT IS WHY THIS EXISTS. The living rooms carry
	// 150-200 mm perimeter bands and the AC heads top out at 2500 under them, so the fit runs and
	// changes nothing every single time. A mechanism that never once produces its own outcome is a
	// mechanism that ships unrun - and this is the exact path the ceiling fans' rod took before a user
	// found a rotor inside the plasterboard.
	TestEqual(TEXT("A split head answers to the ceiling over it"),
		FHFCeilingFit::RuleFor(EHFFixtureType::ACIndoorUnit), EHFCeilingFitRule::Lowers);

	FHFRoom Room;
	Room.Id = TEXT("R_Test");
	Room.FloorZ = 0.0;
	Room.CeilingHeight = 300.0;
	Room.Boundary = { FVector2D(0.0, 0.0), FVector2D(400.0, 0.0), FVector2D(400.0, 400.0),
		FVector2D(0.0, 400.0) };

	FHFFixture Unit;
	Unit.Id = TEXT("F_AC_Test");
	Unit.RoomId = Room.Id;
	Unit.Type = EHFFixtureType::ACIndoorUnit;
	Unit.Position = FVector2D(200.0, 40.0);
	Unit.Footprint = FVector2D(90.0, 22.0);
	Unit.Height = 30.0;
	Unit.BaseZ = 220.0;

	// A 550 mm full drop - the kind of ceiling a user gets the moment they drag the depth slider. The
	// unit's head is at 250 and the soffit comes down to 245, so it has to move.
	FHFFalseCeiling Deep;
	Deep.Id = TEXT("FC_Test");
	Deep.RoomId = Room.Id;
	Deep.Style = EHFCeilingStyle::FullDrop;
	Deep.Drop = 55.0;

	const double Clearance = 5.0;
	const double BuiltHeight = AHFSplitACActor::ParamsFor(Unit).BuiltHeight();

	TestEqual(TEXT("A split head is built exactly its drawn height"), BuiltHeight, Unit.Height, 0.01);

	const FHFCeilingFitResult Result =
		FHFCeilingFit::Fit(Unit, Room, { Deep }, Clearance, BuiltHeight);

	TestEqual(TEXT("It is lowered rather than shortened or refused"),
		Result.Action, EHFCeilingFitAction::Lowered);

	// IN CENTIMETRES, and against the arithmetic rather than against whatever came out. The soffit is
	// at 245, the clearance takes 5, the unit is 30 tall - so its base comes down from 220 to 210.
	TestEqual(TEXT("The soffit is where the ceiling puts it"), Result.SoffitZ, 245.0, 0.01);
	TestEqual(TEXT("It ends up exactly under the soffit"),
		Result.BaseZ + Unit.Height, Result.SoffitZ - Clearance, 0.01);
	TestEqual(TEXT("It came down by exactly the ten centimetres it had to"),
		Result.Adjustment, 10.0, 0.01);

	// A shallower ceiling leaves it alone. The rule is never to raise a fitting: a drawing put it at a
	// height for a reason.
	FHFFalseCeiling Shallow = Deep;
	Shallow.Drop = 15.0;

	const FHFCeilingFitResult Clear =
		FHFCeilingFit::Fit(Unit, Room, { Shallow }, Clearance, BuiltHeight);

	TestEqual(TEXT("A shallow ceiling leaves it where it was drawn"),
		Clear.Action, EHFCeilingFitAction::Unchanged);

	return true;
}

// =============================================================================================
//
// The condensing unit.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCondenserFormTest,
	"HouseForge.Services.CondenserIsAGuardedCaseOnFeet", HF_TEST_FLAGS)

bool FHFCondenserFormTest::RunTest(const FString&)
{
	const FHFCondenserParams P = MakeCondenser();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildCondenser(P);

	TestTrue(TEXT("A condensing unit builds"), Built.bValid);
	TestTrue(TEXT("The case is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	// The corner-and-base datum: X 0..Width, Y 0..Depth, Z 0..Height.
	TestEqual(TEXT("It is as wide as drawn"), AlongX(Bounds), P.Width, 0.01);
	TestEqual(TEXT("It is as tall as drawn"), Bounds.Max.Z, P.Height, 0.01);
	TestEqual(TEXT("It stands on the floor"), Bounds.Min.Z, 0.0, 0.01);
	TestEqual(TEXT("It reaches its own back"), Bounds.Max.Y, P.Depth, 0.01);

	// A HOLLOW CASE. The fan turns inside it, so a solid block would leave the aperture looking at
	// plastic - and would fill the whole drawn envelope.
	const double Fill = Volume(Built.Shell) / (P.Width * P.Depth * P.Height);
	TestTrue(FString::Printf(TEXT("The case is hollow (%.0f%% filled)"), Fill * 100.0),
		Fill > 0.05 && Fill < 0.60);

	// THE GUARD IS PERFORATED. Sixteen slots in two bands, each an arc outline that puts vertices on
	// its own radius - and none of which would exist if the guard were a disc with a grid drawn on it.
	// See VerticesOnCircleXZ.
	const FVector2D FanCentreXZ(P.Width * 0.5, P.FootHeight + (P.Height - P.FootHeight) * 0.55);

	const int32 OuterBand =
		VerticesOnCircleXZ(Built.Shell, FanCentreXZ, P.FanRadius() * 0.93, 0.02);
	const int32 InnerBand =
		VerticesOnCircleXZ(Built.Shell, FanCentreXZ, P.FanRadius() * 0.52, 0.02);

	TestTrue(FString::Printf(TEXT("The guard's outer band is genuinely slotted (%d vertices)"),
		OuterBand), OuterBand >= 40);
	TestTrue(FString::Printf(TEXT("The guard's inner band is genuinely slotted (%d vertices)"),
		InnerBand), InnerBand >= 24);

	// AND THE FAN APERTURE IS A REAL HOLE IN THE FRONT PANEL, not a circle laid on it.
	const int32 Aperture = VerticesOnCircleXZ(Built.Shell, FanCentreXZ, P.FanRadius(), 0.02);
	TestTrue(FString::Printf(TEXT("The front panel is cut for the fan (%d vertices)"), Aperture),
		Aperture >= 40);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCondenserFanTest,
	"HouseForge.Services.CondenserFanSpinsAndDoesNotBlock", HF_TEST_FLAGS)

bool FHFCondenserFanTest::RunTest(const FString&)
{
	const FHFCondenserParams P = MakeCondenser();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildCondenser(P);

	const FHFMeshPart* Fan = FindPart(Built.Parts, FHFApplianceKit::CondenserFanPartId());
	if (Fan == nullptr)
	{
		AddError(TEXT("No fan was built."));
		return false;
	}

	// A FAN IS NOT OPEN. It revolves, it has no limit to reach, and its pose is a phase - see
	// EHFMotionType::Spin, whose whole argument applies here word for word.
	TestEqual(TEXT("The fan spins rather than opening"), Fan->Motion.Type, EHFMotionType::Spin);
	TestTrue(TEXT("It has a real speed on it"), FMath::Abs(Fan->Motion.RevolutionsPerMinute) > 100.0);

	// AND IT DOES NOT BLOCK. Collision geometry does not turn with the render, so a blocking rotor is
	// a blade frozen at whatever azimuth the level was saved at - a pawn walks through the gap between
	// two blades and hits an invisible wall a few degrees later.
	TestEqual(TEXT("The rotor answers traces and stops nothing"),
		Fan->Collision, EHFPartCollision::TraceOnly);

	// THE PHASE ACTUALLY MOVES A BLADE, and by the arc a blade tip really travels. A blade tip at
	// radius r, turned half a revolution, ends up 2r away from where it started.
	FHFPartState State;
	State.PivotTransform = Fan->PivotTransform;
	State.Motion = Fan->Motion;

	const FAxisAlignedBox3d Rotor = Fan->Mesh.GetBounds();
	const double TipRadius = FMath::Max(Rotor.Max.X, Rotor.Max.Z);

	const FVector Tip(TipRadius, 0.0, 0.0);

	State.SpinTurns = 0.0;
	const FVector At0 = State.CurrentPose().TransformPosition(Tip);

	State.SpinTurns = 0.5;
	const FVector AtHalf = State.CurrentPose().TransformPosition(Tip);

	TestEqual(TEXT("Half a turn carries a blade tip across its own diameter"),
		FVector::Dist(At0, AtHalf), 2.0 * TipRadius, 0.05);

	// And a phase is UNBOUNDED, which is the whole difference from an open amount.
	State.SpinTurns = 12.5;
	const FVector AtTwelveAndAHalf = State.CurrentPose().TransformPosition(Tip);

	TestEqual(TEXT("Twelve and a half turns is the same pose as half a turn"),
		FVector::Dist(AtHalf, AtTwelveAndAHalf), 0.0, 0.05);

	return true;
}

// =============================================================================================
//
// The refrigerator and the washing machine.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRefrigeratorFormTest,
	"HouseForge.Services.RefrigeratorStandsClearOfTheSkirting", HF_TEST_FLAGS)

bool FHFRefrigeratorFormTest::RunTest(const FString&)
{
	const FHFRefrigeratorParams P = MakeFridge();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildRefrigerator(P);

	TestTrue(TEXT("A refrigerator builds"), Built.bValid);
	TestTrue(TEXT("The cabinet is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestEqual(TEXT("It is as wide as drawn"), AlongX(Bounds), P.Width, 0.01);
	TestEqual(TEXT("It is as tall as drawn"), Bounds.Max.Z, P.Height, 0.01);
	TestEqual(TEXT("It stands on the floor"), Bounds.Min.Z, 0.0, 0.01);

	// ------------------------------------------------------- THE SKIRTING RUNS BEHIND IT
	//
	// A refrigerator is not scribed joinery, so the board runs on behind it and the cabinet has to
	// stop short of the drawn back plane. Left flat against it the appliance stands 18 mm inside a
	// skirting board - permanently, invisibly in plan, and in every test of the appliance alone. The
	// same defect a study table was found with, and the same fix.
	TestEqual(TEXT("The cabinet stops short of the drawn back by the skirting's own depth"),
		Bounds.Max.Y, P.Depth - P.SkirtingSetback, 0.01);

	// A cabinet, not a block: the doors are separate and the carcass stops where they begin.
	TestEqual(TEXT("A freezer door and a fresh food door"), Built.Parts.Num(), 2);

	// The freezer is the smaller compartment and it is on TOP, which is what this appliance is.
	const FHFMeshPart* Freezer = FindPart(Built.Parts, FHFApplianceKit::FridgeDoorPartId(0));
	const FHFMeshPart* Fresh = FindPart(Built.Parts, FHFApplianceKit::FridgeDoorPartId(1));

	if (Freezer == nullptr || Fresh == nullptr)
	{
		AddError(TEXT("A door is missing."));
		return false;
	}

	const FBox FreezerBox = PosedBounds(*Freezer, 0.0);
	const FBox FreshBox = PosedBounds(*Fresh, 0.0);

	TestTrue(TEXT("The freezer is above the fresh food compartment"),
		FreezerBox.Min.Z > FreshBox.Max.Z - 1.0);
	TestTrue(TEXT("The freezer is the smaller of the two"),
		FreezerBox.GetSize().Z < FreshBox.GetSize().Z);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRefrigeratorDoorTest,
	"HouseForge.Services.RefrigeratorDoorsOpenIntoTheRoom", HF_TEST_FLAGS)

bool FHFRefrigeratorDoorTest::RunTest(const FString&)
{
	const FHFRefrigeratorParams P = MakeFridge();
	const FHFApplianceBuild Built = FHFApplianceKit::BuildRefrigerator(P);

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FHFMeshPart* Door = FindPart(Built.Parts, FHFApplianceKit::FridgeDoorPartId(Index));
		if (Door == nullptr)
		{
			AddError(FString::Printf(TEXT("Door %d is missing."), Index));
			continue;
		}

		TestEqual(FString::Printf(TEXT("Door %d hinges"), Index),
			Door->Motion.Type, EHFMotionType::Hinge);

		// The free edge, in the leaf's own space: it is drawn from its hinge along -X.
		const FVector FreeEdge(-P.Width, 0.0, 0.0);

		const FVector Shut = PosedPoint(*Door, FreeEdge, 0.0);
		const FVector Open = PosedPoint(*Door, FreeEdge, 1.0);

		// OUT INTO THE ROOM, WHICH IS -Y. The other sign sweeps the same 110 degrees straight into
		// the wall behind the cabinet: the leaf travels exactly as far, every "did it move"
		// assertion passes, and the refrigerator cannot be opened.
		TestTrue(FString::Printf(
			TEXT("Door %d swings out into the room (free edge from Y %.1f to %.1f)"),
			Index, Shut.Y, Open.Y), Open.Y < -P.Width * 0.7);

		// AND IT UNCOVERS THE CABINET, in centimetres. At full open no part of the leaf may still lie
		// across the compartment it covers: a door that swings 30 degrees measures as motion and
		// leaves the fridge unreachable.
		const FBox OpenBounds = PosedBounds(*Door, 1.0);

		TestTrue(FString::Printf(
			TEXT("Door %d clears the compartment (nearest point %.1f cm past the left jamb)"),
			Index, OpenBounds.Min.X),
			OpenBounds.Min.X > -0.5);

		// BOTH THE SAME WAY. Nobody opens a freezer left-handed and the compartment under it
		// right-handed on the same cabinet.
		TestTrue(FString::Printf(TEXT("Door %d is hung on the same side as its neighbour"), Index),
			Door->Motion.MaxAngleDegrees > 0.0);
	}

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWashingMachineFormTest,
	"HouseForge.Services.WashingMachineIsAPerforatedFrontWithADrum", HF_TEST_FLAGS)

bool FHFWashingMachineFormTest::RunTest(const FString&)
{
	const FHFWashingMachineParams Raw = MakeWasher();
	const FHFWashingMachineParams P = FHFApplianceKit::SanitiseWashingMachine(Raw);
	const FHFApplianceBuild Built = FHFApplianceKit::BuildWashingMachine(Raw);

	TestTrue(TEXT("A washing machine builds"), Built.bValid);
	TestTrue(TEXT("The case is watertight"), FHFMeshOps::IsClosed(Built.Shell));
	TestTrue(TEXT("It has positive volume"), Volume(Built.Shell) > 0.0);
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Built.Shell));

	const FAxisAlignedBox3d Bounds = Built.Shell.GetBounds();

	TestEqual(TEXT("It is as wide as drawn"), AlongX(Bounds), P.Width, 0.01);
	TestEqual(TEXT("It is as tall as drawn"), Bounds.Max.Z, P.Height, 0.01);
	TestEqual(TEXT("It stands on the floor"), Bounds.Min.Z, 0.0, 0.01);

	// As the refrigerator: the skirting runs on behind it.
	TestEqual(TEXT("The case stops short of the drawn back by the skirting's own depth"),
		Bounds.Max.Y, P.Depth - P.SkirtingSetback, 0.01);

	// THE PORTHOLE IS A REAL HOLE. Nothing else about the front of a front loader reads at all: a
	// glass disc laid on a flat panel has no depth behind it and goes out as a sticker.
	const int32 Opening = VerticesOnCircleXZ(Built.Shell,
		FVector2D(P.Width * 0.5, P.PortholeCentreZ), P.PortholeDiameter * 0.5, 0.02);

	TestTrue(FString::Printf(TEXT("The front panel is cut for the drum (%d vertices)"), Opening),
		Opening >= 40);

	// AND THERE IS SOMETHING BEHIND IT. A hole to nothing is a missing part, not a machine.
	const FVector2D DrumCentre(P.Width * 0.5, P.PortholeCentreZ);
	int32 BehindTheGlass = 0;

	for (const int32 Vertex : Built.Shell.VertexIndicesItr())
	{
		const FVector3d Point = Built.Shell.GetVertex(Vertex);
		const double Radial = FVector2D(Point.X - DrumCentre.X, Point.Z - DrumCentre.Y).Size();

		if (Radial < P.PortholeDiameter * 0.45 && Point.Y > 8.0)
		{
			++BehindTheGlass;
		}
	}

	TestTrue(FString::Printf(TEXT("The drum is behind the opening (%d vertices)"), BehindTheGlass),
		BehindTheGlass > 8);

	return true;
}

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFWashingMachineMotionTest,
	"HouseForge.Services.WashingMachinePartsAllMove", HF_TEST_FLAGS)

bool FHFWashingMachineMotionTest::RunTest(const FString&)
{
	const FHFWashingMachineParams P =
		FHFApplianceKit::SanitiseWashingMachine(MakeWasher());
	const FHFApplianceBuild Built = FHFApplianceKit::BuildWashingMachine(MakeWasher());

	// ------------------------------------------------------------------------------ the porthole
	const FHFMeshPart* Porthole = FindPart(Built.Parts, FHFApplianceKit::PortholePartId());
	if (Porthole == nullptr)
	{
		AddError(TEXT("No porthole was built."));
		return false;
	}

	const FBox Shut = PosedBounds(*Porthole, 0.0);
	const FBox Open = PosedBounds(*Porthole, 1.0);

	// THE APERTURE, IN CENTIMETRES. Shut, the door covers the opening; open at 160 degrees it has
	// swung round to the left and off it entirely, so the drum is reachable. A door swinging the other
	// way sweeps the same arc INTO the machine and uncovers nothing.
	TestTrue(FString::Printf(TEXT("Open, the door has swung clear of the opening (X max %.1f)"),
		Open.Max.X), Open.Max.X < P.Width * 0.5 - P.PortholeDiameter * 0.3);

	TestTrue(FString::Printf(TEXT("Open, the door stands out of the machine (Y min %.1f)"),
		Open.Min.Y), Open.Min.Y < Shut.Min.Y - 2.0);

	// --------------------------------------------------------------------- the detergent drawer
	const FHFMeshPart* Drawer = FindPart(Built.Parts, FHFApplianceKit::DetergentDrawerPartId());
	if (Drawer == nullptr)
	{
		AddError(TEXT("No detergent drawer was built."));
		return false;
	}

	TestEqual(TEXT("The drawer slides"), Drawer->Motion.Type, EHFMotionType::Slide);

	const FBox DrawerShut = PosedBounds(*Drawer, 0.0);
	const FBox DrawerOut = PosedBounds(*Drawer, 1.0);

	// OUT OF THE MACHINE, and by its declared travel. A drawer running the other way reports the same
	// distance and has gone further in.
	TestEqual(FString::Printf(TEXT("The drawer pulls out %.1f cm"),
		DrawerShut.Min.Y - DrawerOut.Min.Y),
		DrawerShut.Min.Y - DrawerOut.Min.Y, P.DrawerTravel, 0.01);

	// ------------------------------------------------------------------------ the programme dial
	const FHFMeshPart* Dial = FindPart(Built.Parts, FHFApplianceKit::ProgrammeDialPartId());
	if (Dial == nullptr)
	{
		AddError(TEXT("No programme dial was built."));
		return false;
	}

	// A KNOB THAT TURNS INVISIBLY IS A KNOB THAT MAY AS WELL NOT HAVE TURNED. The index mark is what
	// makes the rotation legible, so it is the point that gets measured: it must move round the dial's
	// own face rather than the dial merely reporting an angle.
	const FVector Index(0.0, -1.35, P.DialRadius * 0.6);

	const FVector IndexAt0 = PosedPoint(*Dial, Index, 0.0);
	const FVector IndexAtFull = PosedPoint(*Dial, Index, 1.0);

	TestTrue(FString::Printf(TEXT("The dial's index sweeps round its face (%.2f cm)"),
		FVector::Dist(IndexAt0, IndexAtFull)),
		FVector::Dist(IndexAt0, IndexAtFull) > P.DialRadius * 0.5);

	// And about the machine's own axis, so it turns in its own plane rather than tipping out of it.
	TestEqual(TEXT("The dial turns in the plane of the fascia"), IndexAt0.Y, IndexAtFull.Y, 0.01);

	return true;
}

// =============================================================================================
//
// Degenerate input.
//
// =============================================================================================

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFServicesDegenerateTest,
	"HouseForge.Services.KitsRefuseDegenerateInput", HF_TEST_FLAGS)

bool FHFServicesDegenerateTest::RunTest(const FString&)
{
	// EMPTY RATHER THAN GARBAGE. A zero-sized fitting is a drawing mistake, and the honest answer to
	// one is nothing at all - a sliver would spawn an actor with a degenerate mesh that every later
	// operation would have to survive.

	{
		FHFAccessoryPlateParams P = MakeSocket();
		P.Width = 0.0;
		const FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(P);
		TestFalse(TEXT("A zero-width plate builds nothing"), Built.bValid);
		TestEqual(TEXT("A zero-width plate emits no triangles"), Built.Shell.TriangleCount(), 0);
	}

	{
		FHFDistributionBoardParams P = MakeBoard();
		P.Height = 0.0;
		const FHFWallPlateBuild Built = FHFWallPlateKit::BuildDistributionBoard(P);
		TestFalse(TEXT("A zero-height board builds nothing"), Built.bValid);
	}

	{
		FHFSplitACParams P = MakeSplitAC();
		P.Depth = 0.0;
		const FHFApplianceBuild Built = FHFApplianceKit::BuildSplitAC(P);
		TestFalse(TEXT("A zero-depth split head builds nothing"), Built.bValid);
	}

	{
		FHFCondenserParams P = MakeCondenser();
		P.Width = 0.0;
		const FHFApplianceBuild Built = FHFApplianceKit::BuildCondenser(P);
		TestFalse(TEXT("A zero-width condenser builds nothing"), Built.bValid);
	}

	{
		FHFRefrigeratorParams P = MakeFridge();
		P.Height = 0.0;
		const FHFApplianceBuild Built = FHFApplianceKit::BuildRefrigerator(P);
		TestFalse(TEXT("A zero-height refrigerator builds nothing"), Built.bValid);
	}

	{
		FHFWashingMachineParams P = MakeWasher();
		P.Width = 0.0;
		const FHFApplianceBuild Built = FHFApplianceKit::BuildWashingMachine(P);
		TestFalse(TEXT("A zero-width washing machine builds nothing"), Built.bValid);
	}

	// And a plate asked for more gangs than its window can carry still comes out as a plate rather
	// than as a row of zero-width rockers.
	{
		FHFAccessoryPlateParams P = MakeSwitchPlate(2);
		P.GangCount = 12;
		const FHFWallPlateBuild Built = FHFWallPlateKit::BuildAccessoryPlate(P);
		TestTrue(TEXT("An over-ganged plate still builds"), Built.bValid);
		TestTrue(TEXT("An over-ganged plate is still watertight"),
			FHFMeshOps::IsClosed(Built.Shell));
	}

	return true;
}

#endif // WITH_DEV_AUTOMATION_TESTS
