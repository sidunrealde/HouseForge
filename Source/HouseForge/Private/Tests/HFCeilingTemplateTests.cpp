// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "DynamicMesh/DynamicMeshAABBTree3.h"
#include "Geometry/HFGenerators.h"
#include "Geometry/HFMeshOps.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFCeilingTemplates.h"
#include "Model/HFSampleHouse.h"
#include "Model/HFSpecValidator.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

/**
 * THE NAMED CEILING DESIGNS, HELD TO WHAT THEY LOOK LIKE RATHER THAN TO WHAT THEY ARE MADE OF.
 *
 * HFCeilingTests already measures the STYLES: that a band is an annulus, that a trough opens
 * upward, that no soffit edge is left open. Every one of those passed on the flat's committed
 * ceilings, and the flat's committed ceilings were the thing the user reported. They were 500 deep
 * across every room, which is not a construction fault - it is a design that does not read, and
 * nothing in the suite was asking whether a design reads.
 *
 * So this file asks the questions a photograph would:
 *
 *   - is the ceiling SHALLOW, with the level change local to the beam it exists for;
 *   - is the beam actually buried, and does the validator agree that it is;
 *   - are the downlights real holes at a real pitch rather than dots on a plan;
 *   - and can the cove's strip be seen from anywhere a person can stand.
 *
 * All in CENTIMETRES, which is what the generators work in.
 */
namespace
{
	/** A 500 x 400 room under a 300 slab: a living room, roughly, in centimetres. */
	FHFRoom MakeTemplateRoom()
	{
		FHFRoom Room;
		Room.Id = TEXT("R_Test");
		Room.Boundary = { FVector2D(0, 0), FVector2D(500, 0), FVector2D(500, 400), FVector2D(0, 400) };
		Room.FloorZ = 0.0;
		Room.CeilingHeight = 300.0;
		return Room;
	}

	/** The reference flat's own beam section: 23 wide, 45 deep, hung from a 300 slab. */
	FHFBeam MakeEdgeBeam()
	{
		FHFBeam Beam;
		Beam.Id = TEXT("BM_Test");
		Beam.Start = FVector2D(0.0, 0.0);
		Beam.End = FVector2D(500.0, 0.0);
		Beam.Width = 23.0;
		Beam.Depth = 45.0;
		Beam.SoffitZ = 300.0;
		return Beam;
	}

	FHFFalseCeiling MakeTemplated(EHFCeilingTemplate Template, const FHFRoom& Room, const FHFBeam* Beam)
	{
		FHFFalseCeiling Ceiling;
		Ceiling.Id = TEXT("FC_Test");
		Ceiling.RoomId = Room.Id;
		Ceiling.Template = Template;

		// UnitScale 1: the figures are in centimetres and so is this room.
		FHFCeilingTemplates::Apply(Ceiling, Room, Beam, FHFCeilingDefaults(), 1.0);
		return Ceiling;
	}

	FString NameOf(EHFCeilingTemplate Template)
	{
		return StaticEnum<EHFCeilingTemplate>()->GetNameStringByValue(static_cast<int64>(Template));
	}

	const TArray<EHFCeilingTemplate>& AllTemplates()
	{
		static const TArray<EHFCeilingTemplate> Templates = {
			EHFCeilingTemplate::PlainBand, EHFCeilingTemplate::Cove,
			EHFCeilingTemplate::SteppedTray, EHFCeilingTemplate::FramedPanel
		};
		return Templates;
	}

	/** Lowest and highest surface over a plan point, whichever way the triangles face. */
	struct FColumn
	{
		bool bAny = false;
		double Lowest = 0.0;
		double Highest = 0.0;
	};

	FColumn ColumnAt(const FDynamicMesh3& Mesh, double X, double Y)
	{
		FColumn Out;

		for (const int32 Tid : Mesh.TriangleIndicesItr())
		{
			FVector3d A, B, C;
			Mesh.GetTriVertices(Tid, A, B, C);

			const double Denominator = (B.Y - C.Y) * (A.X - C.X) + (C.X - B.X) * (A.Y - C.Y);
			if (FMath::Abs(Denominator) < 1e-9)
			{
				continue;
			}

			const double U = ((B.Y - C.Y) * (X - C.X) + (C.X - B.X) * (Y - C.Y)) / Denominator;
			const double V = ((C.Y - A.Y) * (X - C.X) + (A.X - C.X) * (Y - C.Y)) / Denominator;
			const double W = 1.0 - U - V;
			if (U < -1e-9 || V < -1e-9 || W < -1e-9)
			{
				continue;
			}

			const double Z = U * A.Z + V * B.Z + W * C.Z;
			Out.Lowest = Out.bAny ? FMath::Min(Out.Lowest, Z) : Z;
			Out.Highest = Out.bAny ? FMath::Max(Out.Highest, Z) : Z;
			Out.bAny = true;
		}

		return Out;
	}
}

/**
 * Every template builds a solid, and builds a SHALLOW one.
 *
 * The second half is the assertion the flat needed and did not have. A ceiling can be watertight,
 * correctly positioned, correctly tagged and dropped half a metre across a whole room, and that is
 * exactly what was committed - so a test that only measures closure and bounds cannot tell a
 * designed ceiling from the box that was reported.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingTemplatesTest,
	"HouseForge.Ceilings.TemplatesAreShallowAndSolid", HF_TEST_FLAGS)

bool FHFCeilingTemplatesTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeTemplateRoom();
	const FHFBeam Beam = MakeEdgeBeam();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;

	for (const EHFCeilingTemplate Template : AllTemplates())
	{
		const FString Name = NameOf(Template);
		const FHFFalseCeiling Ceiling = MakeTemplated(Template, Room, &Beam);

		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);
		if (!TestTrue(*FString::Printf(TEXT("%s produces geometry"), *Name), Mesh.TriangleCount() > 0))
		{
			continue;
		}

		AddInfo(FString::Printf(
			TEXT("%s: style %s, drop %.1f, band %.1f, ring %.1f wide dropping %.1f, %d downlights."),
			*Name, *StaticEnum<EHFCeilingStyle>()->GetNameStringByValue(static_cast<int64>(Ceiling.Style)),
			Ceiling.Drop, Ceiling.BandWidth, Ceiling.PerimeterBulkheadWidth,
			Ceiling.PerimeterBulkheadDrop, Ceiling.LightPositions.Num()));

		TestTrue(*FString::Printf(TEXT("%s is watertight"), *Name), FHFMeshOps::IsClosed(Mesh));
		TestTrue(*FString::Printf(TEXT("%s has positive volume"), *Name),
			TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X > 0.0);

		// SHALLOW. 200 is the top of the range the reference designs sit in; the committed flat was
		// at 500, and 500 is what a rule expressed as "deep enough for the beam" produces.
		TestTrue(*FString::Printf(TEXT("%s drops %.1f, which is shallow"), *Name, Ceiling.Drop),
			Ceiling.Drop > 0.0 && Ceiling.Drop <= 20.0);

		// AND THE DEPTH IS LOCAL. The ring exists, it is deeper than the ceiling inside it, and it is
		// a band rather than the room: a ring as wide as the room is a full drop wearing a new field.
		TestTrue(*FString::Printf(TEXT("%s boxes the beam in with a ring"), *Name),
			Ceiling.HasPerimeterBulkhead());
		TestTrue(*FString::Printf(TEXT("%s's ring clears the 45 beam"), *Name),
			Ceiling.PerimeterBulkheadDrop >= Beam.Depth);
		TestTrue(*FString::Printf(TEXT("%s's ring is a band, not the room"), *Name),
			Ceiling.PerimeterBulkheadWidth > 0.0 && Ceiling.PerimeterBulkheadWidth < 100.0);

		// Below the slab and inside the room, as every style already has to be.
		const FAxisAlignedBox3d Bounds = Mesh.GetBounds();
		TestTrue(*FString::Printf(TEXT("%s hangs below the structural soffit"), *Name),
			Bounds.Min.Z < StructuralZ - 1.0);
		TestTrue(*FString::Printf(TEXT("%s does not poke above the slab"), *Name),
			Bounds.Max.Z <= StructuralZ + 0.1);
		TestTrue(*FString::Printf(TEXT("%s stays inside the room"), *Name),
			Bounds.Min.X >= -0.1 && Bounds.Min.Y >= -0.1 &&
			Bounds.Max.X <= 500.1 && Bounds.Max.Y <= 400.1);

		// ------------------------------------------------------------------ the level change reads
		//
		// Sectioned across the ring and the band beside it. Three things at once: the ring is the
		// deepest thing in the room, the ceiling inside it is far shallower, and BOTH run up to the
		// slab - a level change with an open edge would show the plenum through the step, which is
		// the defect an earlier pass fixed and this must not reintroduce.
		const double RingSoffitZ = StructuralZ - Ceiling.PerimeterBulkheadDrop;
		const double BandSoffitZ = StructuralZ - Ceiling.Drop;

		// Off-grid on purpose: a probe landing on a triangle edge in plan is a coin toss.
		const FColumn Ring = ColumnAt(Mesh, 237.0, Ceiling.PerimeterBulkheadWidth * 0.5);
		const FColumn Band = ColumnAt(Mesh, 237.0, Ceiling.PerimeterBulkheadWidth + 7.0);

		if (TestTrue(*FString::Printf(TEXT("%s has ceiling over its ring"), *Name), Ring.bAny))
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s's ring soffit is at its own drop"), *Name),
				Ring.Lowest, RingSoffitZ, 0.05);
			TestNearlyEqual(*FString::Printf(TEXT("%s's ring closes against the slab"), *Name),
				Ring.Highest, StructuralZ, 0.05);
		}

		if (TestTrue(*FString::Printf(TEXT("%s has ceiling just inside its ring"), *Name), Band.bAny))
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s is shallow immediately inside the ring"), *Name),
				Band.Lowest, BandSoffitZ, 0.05);
			TestNearlyEqual(*FString::Printf(TEXT("%s closes that band against the slab too"), *Name),
				Band.Highest, StructuralZ, 0.05);
		}

		// The step is a real step and the right way round.
		TestTrue(*FString::Printf(TEXT("%s's ring hangs below the ceiling inside it"), *Name),
			RingSoffitZ < BandSoffitZ - 1.0);
	}

	return true;
}

/**
 * A template's centre stays high - that is the whole difference from a full drop.
 *
 * Measured rather than inferred from the style, because the two coves in this set answer it
 * differently on purpose: a plain Cove leaves the middle open to the slab so its light washes the
 * structure, and a FramedPanel fills it with a panel sitting HIGHER than the band, which is what
 * the light washes instead. Both are shallow; neither is a full drop.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingTemplateCentreTest,
	"HouseForge.Ceilings.TemplatesLeaveTheCentreHigh", HF_TEST_FLAGS)

bool FHFCeilingTemplateCentreTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeTemplateRoom();
	const FHFBeam Beam = MakeEdgeBeam();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;

	// Well clear of any band: the middle of a 500 x 400 room.
	constexpr double CentreX = 253.0;
	constexpr double CentreY = 197.0;

	for (const EHFCeilingTemplate Template : AllTemplates())
	{
		const FString Name = NameOf(Template);
		const FHFFalseCeiling Ceiling = MakeTemplated(Template, Room, &Beam);
		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);

		const FColumn Centre = ColumnAt(Mesh, CentreX, CentreY);
		const double Drop = FHFGenerators::CeilingSoffitDropAt(Ceiling, Room, FVector2D(CentreX, CentreY));

		AddInfo(FString::Printf(TEXT("%s over the middle of the room: soffit drop %.1f."), *Name, Drop));

		// 10 cm is already generous for a centre: the deepest of these templates puts a stepped
		// panel at 10 and the rest leave the middle open or panel it at 4.
		TestTrue(*FString::Printf(TEXT("%s leaves the centre of the room within 10 of the slab"), *Name),
			Drop <= 10.0 + UE_KINDA_SMALL_NUMBER);

		// The mesh has to agree with the answer a fan's rod is resolved against. The two used to be
		// hand-copied switch statements; they are one layout now, and this is what says so.
		if (Centre.bAny)
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s's centre soffit matches what a fan is told"), *Name),
				Centre.Lowest, StructuralZ - Drop, 0.05);
		}
		else
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s reports no cover where it built none"), *Name),
				Drop, 0.0, 1e-6);
		}
	}

	return true;
}

/**
 * The cove's strip cannot be seen from anywhere in the room, at any height a person's eye is at.
 *
 * COMPUTED, NOT EYEBALLED. The geometry says concealment is one inequality - the strip's top below
 * the lip's top - because a strip throwing upward sends every ray that clears the lip AWAY from an
 * eye beneath it. That argument has no distance in it, so the way to hold it to the mesh is to try
 * every position rather than the two or three that seem representative.
 *
 * Cast at the strip's TOP EDGE, which is the first part of it that would come into view, from a
 * standing eye at 160 and a sitting one at 120, over a grid across the whole floor, on all four
 * runs of the trough.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingTemplateStripHiddenTest,
	"HouseForge.Ceilings.TemplateCoveStripIsNeverInSight", HF_TEST_FLAGS)

bool FHFCeilingTemplateStripHiddenTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeTemplateRoom();
	const FHFBeam Beam = MakeEdgeBeam();
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;

	for (const EHFCeilingTemplate Template : { EHFCeilingTemplate::Cove, EHFCeilingTemplate::FramedPanel })
	{
		const FString Name = NameOf(Template);
		const FHFFalseCeiling Ceiling = MakeTemplated(Template, Room, &Beam);

		if (!TestTrue(*FString::Printf(TEXT("%s carries a strip at all"), *Name),
			Ceiling.Cove.bHasLedStrip && Ceiling.Cove.StripHeight > 0.0))
		{
			continue;
		}

		// Where the strip is, from the same figures the generator sets it out from. The ring pushes
		// the whole design inboard, so the band is measured from the styled loop rather than the room.
		const double RingInset = Ceiling.HasPerimeterBulkhead()
			? Ceiling.PerimeterBulkheadWidth - 0.5
			: 0.0;

		const double SoffitZ = StructuralZ - Ceiling.Drop;
		const double BoardTopZ = SoffitZ + 2.0;
		const double StripTopZ = BoardTopZ + Ceiling.Cove.StripHeight;
		const double LipTopZ = SoffitZ + Ceiling.Cove.LipHeight;

		const double SolidBand = Ceiling.BandWidth - Ceiling.Cove.ChannelWidth - Ceiling.Cove.Setback;
		const double StripCentre = RingInset + SolidBand + Ceiling.Cove.ChannelWidth
			- Ceiling.Cove.StripSetback - Ceiling.Cove.StripWidth * 0.5;

		AddInfo(FString::Printf(
			TEXT("%s: strip top %.2f, lip top %.2f, slab %.2f; trough %.1f deep by %.1f wide."),
			*Name, StripTopZ, LipTopZ, StructuralZ, Ceiling.Drop - 2.0, Ceiling.Cove.ChannelWidth));

		// THE INEQUALITY ITSELF. If this fails no amount of ray casting will save it, and if it
		// holds the sweep below should find nothing - so a disagreement between the two is a defect
		// in one of them rather than in the ceiling.
		TestTrue(*FString::Printf(TEXT("%s keeps the strip's top below the lip's top"), *Name),
			StripTopZ <= LipTopZ);

		// AND THE TROUGH IS NOT A WELL. A 6 to 1 trough hides the strip perfectly and hides its
		// light with it, which is what the committed 500 drop did and what was reported as the cove
		// not being visible. Depth here is from the trough floor to the slab.
		const double TroughDepth = StructuralZ - BoardTopZ;
		const double Aspect = TroughDepth / FMath::Max(Ceiling.Cove.ChannelWidth, 0.01);
		AddInfo(FString::Printf(TEXT("%s trough aspect: %.2f to 1."), *Name, Aspect));
		TestTrue(*FString::Printf(TEXT("%s's trough is at most 1.5 to 1, so its light gets out"), *Name),
			Aspect <= 1.5);

		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);
		if (!TestTrue(*FString::Printf(TEXT("%s generates"), *Name), Mesh.TriangleCount() > 0))
		{
			continue;
		}

		FDynamicMeshAABBTree3 Tree(&Mesh, true);

		auto CanSee = [&Tree](const FVector3d& Eye, const FVector3d& Target)
		{
			const double Distance = (Target - Eye).Length();
			if (Distance <= 1.0)
			{
				return true;
			}

			const FRay3d Ray(Eye, (Target - Eye) / Distance);
			return Tree.FindNearestHitTriangle(Ray,
				FDynamicMeshAABBTree3::FQueryOptions(Distance - 0.5)) == IndexConstants::InvalidID;
		};

		// All four runs of the trough: a lip that hid the strip along one wall and not along the
		// next would pass any single-probe test.
		const TArray<FVector3d> Targets = {
			FVector3d(253.0, StripCentre, StripTopZ - 0.05),
			FVector3d(253.0, 400.0 - StripCentre, StripTopZ - 0.05),
			FVector3d(StripCentre, 197.0, StripTopZ - 0.05),
			FVector3d(500.0 - StripCentre, 197.0, StripTopZ - 0.05)
		};

		int32 Cast = 0;
		int32 Seen = 0;
		FVector3d FirstSeenFrom = FVector3d::Zero();

		for (const double EyeZ : { 160.0, 120.0 })
		{
			for (double X = 6.0; X < 500.0; X += 23.0)
			{
				for (double Y = 4.0; Y < 400.0; Y += 19.0)
				{
					for (const FVector3d& Target : Targets)
					{
						++Cast;
						if (CanSee(FVector3d(X, Y, EyeZ), Target))
						{
							if (Seen == 0)
							{
								FirstSeenFrom = FVector3d(X, Y, EyeZ);
							}
							++Seen;
						}
					}
				}
			}
		}

		AddInfo(FString::Printf(TEXT("%s: %d sight lines cast at the strip."), *Name, Cast));
		TestTrue(*FString::Printf(TEXT("%s's sweep actually cast sight lines"), *Name), Cast > 2000);
		TestEqual(*FString::Printf(
			TEXT("%s's strip is out of sight everywhere (first seen from %.0f, %.0f, %.0f)"),
			*Name, FirstSeenFrom.X, FirstSeenFrom.Y, FirstSeenFrom.Z), Seen, 0);
	}

	return true;
}

/**
 * The downlights are recorded, evenly spaced, and actually cut into the ceiling.
 *
 * LightPositions has been on FHFFalseCeiling since the beginning and no generator has ever read it:
 * the preview drew a circle at each and the built ceiling had no fittings in it at all. So all
 * three halves of this matter - that a run is laid out, that it is laid out evenly rather than by
 * repeated addition leaving a short gap at one corner, and that the geometry has holes in it where
 * the run says.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingDownlightTest,
	"HouseForge.Ceilings.DownlightsAreRecordedAndCut", HF_TEST_FLAGS)

bool FHFCeilingDownlightTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeTemplateRoom();
	const FHFBeam Beam = MakeEdgeBeam();
	const FHFCeilingDefaults Defaults;
	const double StructuralZ = Room.FloorZ + Room.CeilingHeight;

	for (const EHFCeilingTemplate Template : AllTemplates())
	{
		const FString Name = NameOf(Template);
		FHFFalseCeiling Ceiling = MakeTemplated(Template, Room, &Beam);

		const int32 Count = Ceiling.LightPositions.Num();
		if (!TestTrue(*FString::Printf(TEXT("%s lays out a run of downlights"), *Name), Count >= 8))
		{
			continue;
		}

		// EVENLY. Every gap the same, and near the figure asked for - the run closes on itself, so
		// the last fitting is the same distance from the first as from its neighbour. A run laid out
		// by repeated addition leaves the remainder as one short gap, and that gap is the first
		// thing the eye finds in a photograph of a ceiling.
		double Shortest = TNumericLimits<double>::Max();
		double Longest = 0.0;

		for (int32 Index = 0; Index < Count; ++Index)
		{
			const double Gap = FVector2D::Distance(
				Ceiling.LightPositions[Index], Ceiling.LightPositions[(Index + 1) % Count]);
			Shortest = FMath::Min(Shortest, Gap);
			Longest = FMath::Max(Longest, Gap);
		}

		AddInfo(FString::Printf(TEXT("%s: %d downlights, gaps %.2f to %.2f (asked for %.1f)."),
			*Name, Count, Shortest, Longest, Defaults.DownlightSpacing));

		// The tolerance is for the corners: a run walks round a corner rather than through it, so
		// the straight-line distance between two fittings either side of one is a chord.
		TestTrue(*FString::Printf(TEXT("%s's downlights are evenly spaced"), *Name),
			Longest - Shortest < Defaults.DownlightSpacing * 0.35);
		TestTrue(*FString::Printf(TEXT("%s's pitch is near the figure asked for"), *Name),
			Longest <= Defaults.DownlightSpacing * 1.35 && Shortest >= Defaults.DownlightSpacing * 0.5);

		// RECORDED IN THREE DIMENSIONS, at the aperture rather than at the plasterboard. A light
		// parented to the soffit plane is shaded by its own trim.
		const TArray<FVector> Apertures = FHFGenerators::CeilingDownlights(Ceiling, Room);
		TestEqual(*FString::Printf(TEXT("%s reports every fitting it laid out"), *Name),
			Apertures.Num(), Count);

		const double SoffitZ = StructuralZ - Ceiling.Drop;
		for (const FVector& Aperture : Apertures)
		{
			TestNearlyEqual(*FString::Printf(TEXT("%s's aperture sits up the can"), *Name),
				Aperture.Z, SoffitZ + Ceiling.Downlight.BodyDepth, 0.01);
		}

		// AND CUT. The same ceiling with the fittings switched off is the control: bored, the soffit
		// loses material and gains trims, so the two meshes cannot have the same triangle count.
		const FDynamicMesh3 Bored = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);

		FHFFalseCeiling Plain = Ceiling;
		Plain.Downlight.bRecessed = false;
		const FDynamicMesh3 Unbored = FHFGenerators::GenerateCeiling(Plain, Room, {}, 0.0);

		TestTrue(*FString::Printf(TEXT("%s's fittings add geometry"), *Name),
			Bored.TriangleCount() > Unbored.TriangleCount());
		TestTrue(*FString::Printf(TEXT("%s stays watertight with its fittings in"), *Name),
			FHFMeshOps::IsClosed(Bored));

		// The bore is a hole through the soffit, not a dimple in it: directly under a fitting there
		// is no plasterboard at the soffit plane at all - only the trim ring around it, which is
		// why the probe is taken dead centre.
		const FVector2D First = Ceiling.LightPositions[0];
		const FColumn Under = ColumnAt(Bored, First.X, First.Y);

		if (TestTrue(*FString::Printf(TEXT("%s has something over its first fitting"), *Name), Under.bAny))
		{
			TestTrue(*FString::Printf(
				TEXT("%s's bore goes right through the soffit at the fitting (lowest %.2f, soffit %.2f)"),
				*Name, Under.Lowest, SoffitZ),
				Under.Lowest > SoffitZ + 0.1);
		}
	}

	return true;
}

/**
 * A beam round the edge of a room is buried by the ring, and the validator agrees that it is.
 *
 * THE TWO HALVES HAVE TO AGREE, and this is where the old model came apart. The validator could see
 * that a 45 beam standing proud of the 11.5 partition under it showed in the room, and the only way
 * to satisfy it was to drop the whole ceiling past 45 - which is how every room in the flat came to
 * be dropped 500. The rule was right; the remedies it would accept were too few.
 *
 * So: geometry that buries the beam, a rule that accepts it, and - just as important - a rule that
 * still refuses a ring too shallow or too narrow to do the job.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingRingBuriesBeamTest,
	"HouseForge.Ceilings.PerimeterRingBuriesTheBeam", HF_TEST_FLAGS)

bool FHFCeilingRingBuriesBeamTest::RunTest(const FString& Parameters)
{
	const FHFRoom Room = MakeTemplateRoom();
	const FHFBeam Beam = MakeEdgeBeam();

	// A one-room spec: the room, the 11.5 partition its south wall is, and the 23 beam over it that
	// therefore stands 5.75 proud of the plaster on each face.
	auto MakeSpec = [&Room, &Beam](const FHFFalseCeiling& Ceiling)
	{
		FHFHouseSpec Spec;
		Spec.Units = EHFUnits::Centimeters;
		Spec.Rooms.Add(Room);
		Spec.Beams.Add(Beam);
		Spec.FalseCeilings.Add(Ceiling);

		FHFWall South;
		South.Id = TEXT("W_South");
		South.Start = FVector2D(0.0, 0.0);
		South.End = FVector2D(500.0, 0.0);
		South.Thickness = 11.5;
		South.Height = 300.0;
		Spec.Walls.Add(South);

		return Spec;
	};

	// The beam really does show, or none of what follows is about anything.
	{
		FHFFalseCeiling Nothing;
		Nothing.Id = TEXT("FC_Test");
		Nothing.RoomId = Room.Id;
		Nothing.Style = EHFCeilingStyle::None;

		const FHFHouseSpec Spec = MakeSpec(Nothing);
		TestNotNull(TEXT("A 23 beam over an 11.5 partition shows in the room"),
			Spec.DeepestBeamOverRoom(Room.Id));
	}

	auto Reported = [&MakeSpec](const FHFFalseCeiling& Ceiling)
	{
		return FHFSpecValidator::Validate(MakeSpec(Ceiling)).Contains(TEXT("CeilingDoesNotClearBeam"));
	};

	for (const EHFCeilingTemplate Template : AllTemplates())
	{
		const FString Name = NameOf(Template);
		const FHFFalseCeiling Ceiling = MakeTemplated(Template, Room, &Beam);

		// THE POINT OF THE WHOLE CHANGE, in one assertion: a ceiling dropping 15 satisfies a rule
		// about a beam hanging 45, because the ring over the beam is what buries it.
		TestFalse(*FString::Printf(
			TEXT("%s drops only %.0f and the validator still passes it, because of the ring"),
			*Name, Ceiling.Drop), Reported(Ceiling));

		// GEOMETRY, not just arithmetic. Sectioned over the beam's own line: the ceiling there hangs
		// below the beam's soffit, so a person in the room sees plasterboard rather than concrete.
		const FDynamicMesh3 Mesh = FHFGenerators::GenerateCeiling(Ceiling, Room, {}, 0.0);
		const FColumn OverBeam = ColumnAt(Mesh, 237.0, Beam.Width * 0.5);

		if (TestTrue(*FString::Printf(TEXT("%s covers the beam's line"), *Name), OverBeam.bAny))
		{
			TestTrue(*FString::Printf(
				TEXT("%s's soffit (%.2f) hangs below the beam soffit (%.2f) where the beam runs"),
				*Name, OverBeam.Lowest, Beam.SoffitZ - Beam.Depth),
				OverBeam.Lowest < Beam.SoffitZ - Beam.Depth);
		}
	}

	// ------------------------------------------------------------------ and what it still refuses
	//
	// A ring is only an exemption while it does the job. Both ways it can fail to are checked, or
	// the clause would be a rubber stamp on the presence of two non-zero fields.
	{
		FHFFalseCeiling Shallow = MakeTemplated(EHFCeilingTemplate::Cove, Room, &Beam);
		Shallow.PerimeterBulkheadDrop = 30.0;
		TestTrue(TEXT("A ring shallower than the beam excuses nothing"), Reported(Shallow));

		FHFFalseCeiling Narrow = MakeTemplated(EHFCeilingTemplate::Cove, Room, &Beam);
		// 10 wide, against a beam whose centreline is on the wall and which therefore reaches 11.5
		// into the room. The far edge of the beam hangs out of the box that was meant to hold it.
		Narrow.PerimeterBulkheadWidth = 10.0;
		TestTrue(TEXT("A ring narrower than the beam's reach excuses nothing"), Reported(Narrow));

		// And a room with nothing showing gets no ring at all, rather than one for the sake of it.
		const FHFFalseCeiling NoBeam = MakeTemplated(EHFCeilingTemplate::Cove, Room, nullptr);
		TestFalse(TEXT("A room with no beam showing gets no ring"), NoBeam.HasPerimeterBulkhead());
		TestTrue(TEXT("...and is still a ceiling"),
			FHFGenerators::GenerateCeiling(NoBeam, Room, {}, 0.0).TriangleCount() > 0);
	}

	return true;
}

/**
 * Resolving templates touches ceilings and nothing else.
 *
 * The pass runs over a whole spec and rewrites figures in place, which is exactly the shape of
 * change that quietly moves something it was not asked to. Room areas are the measurement this
 * project is judged on - twelve of them, matched against the published drawing to the centimetre -
 * so they are what this holds still.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFCeilingTemplatesTouchNothingElseTest,
	"HouseForge.Ceilings.TemplatesLeaveTheRoomsAlone", HF_TEST_FLAGS)

bool FHFCeilingTemplatesTouchNothingElseTest::RunTest(const FString& Parameters)
{
	FHFHouseSpec Before = FHFSampleHouse::Make2BHK();
	FHFHouseSpec After = Before;

	// Applied a second time, with the same defaults the sample already used. Idempotent means the
	// areas are unchanged AND the ceilings are too, which is what lets the editor re-apply on every
	// settings change without anything drifting.
	FHFCeilingTemplates::Apply(After, FHFCeilingDefaults());

	if (!TestEqual(TEXT("The room count is unchanged"), After.Rooms.Num(), Before.Rooms.Num()))
	{
		return false;
	}

	for (int32 Index = 0; Index < Before.Rooms.Num(); ++Index)
	{
		TestEqual(*FString::Printf(TEXT("Room %d keeps its id"), Index),
			After.Rooms[Index].Id, Before.Rooms[Index].Id);
		TestNearlyEqual(*FString::Printf(TEXT("Room '%s' keeps its area"),
			*Before.Rooms[Index].Id.ToString()),
			After.Rooms[Index].Area(), Before.Rooms[Index].Area(), 0.01);
	}

	TestEqual(TEXT("The wall count is unchanged"), After.Walls.Num(), Before.Walls.Num());
	TestEqual(TEXT("The beam count is unchanged"), After.Beams.Num(), Before.Beams.Num());
	TestEqual(TEXT("The fixture count is unchanged"), After.Fixtures.Num(), Before.Fixtures.Num());

	for (int32 Index = 0; Index < Before.FalseCeilings.Num(); ++Index)
	{
		const FHFFalseCeiling& A = Before.FalseCeilings[Index];
		const FHFFalseCeiling& B = After.FalseCeilings[Index];

		TestEqual(*FString::Printf(TEXT("Ceiling '%s' resolves to the same drop twice"), *A.Id.ToString()),
			B.Drop, A.Drop);
		TestEqual(*FString::Printf(TEXT("Ceiling '%s' resolves to the same band twice"), *A.Id.ToString()),
			B.BandWidth, A.BandWidth);
		TestEqual(*FString::Printf(TEXT("Ceiling '%s' resolves to the same ring twice"), *A.Id.ToString()),
			B.PerimeterBulkheadDrop, A.PerimeterBulkheadDrop);
		TestEqual(*FString::Printf(TEXT("Ceiling '%s' lays out the same run twice"), *A.Id.ToString()),
			B.LightPositions.Num(), A.LightPositions.Num());
	}

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
