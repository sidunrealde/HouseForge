// Copyright Siddartha G. All Rights Reserved.

#include "HouseForge.h"

#if WITH_DEV_AUTOMATION_TESTS

#include "Geometry/HFFrameKit.h"
#include "Geometry/HFMeshOps.h"
#include "Geometry/HFWallPlateKit.h"
#include "MeshQueries.h"
#include "Misc/AutomationTest.h"
#include "Model/HFTypes.h"

using namespace UE::Geometry;

#define HF_TEST_FLAGS (EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

// ---------------------------------------------------------------------------------------------
//
// The trim: the balcony guards and the curtain pelmets, on the bench.
//
// These are the two types in the catalogue whose correctness is least visible in a screenshot and
// most nearly a matter of arithmetic, which is exactly why they are worth measuring here.
//
// A RAILING IS THE ONLY FIXTURE IN THE FLAT WITH A CODE BEHIND IT. Two of its numbers are not
// preferences - a guard clears 1050 above the floor it protects, and nothing anywhere in it passes a
// 100 mm sphere - and both are properties of the finished assembly rather than of any one member. So
// they are asserted as such: on the worst gap anywhere in the guard, and on the height above both the
// floor and the coping, which are different questions with different answers.
//
// A PELMET IS MEASURED ON WHAT IT HIDES. Its box is trivially a box; the thing that decides whether
// it works is the clear drop between the track and the bottom of the fascia, because a pelmet whose
// fascia stops above the curtain heading shows a row of hooks - which is the one thing the fitting
// exists to prevent and is invisible in plan, in section, and in any test that measures the box.
//
// Nothing here is asserted on a triangle count. See .claude/rules/04-conventions.md.
//
// ---------------------------------------------------------------------------------------------

namespace HouseForgeTrim
{
	double Volume(const FDynamicMesh3& Mesh)
	{
		return TMeshQueries<FDynamicMesh3>::GetVolumeArea(Mesh).X;
	}

	/** True when every triangle carries a polygroup that maps back to a surface role. */
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

	FBox BoundsOf(const FDynamicMesh3& Mesh)
	{
		FBox Out(ForceInit);
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			Out += FVector(Mesh.GetVertex(Vertex));
		}
		return Out;
	}

	/** Bounds of only the geometry lying in a Z band - how a member is found without a transform. */
	FBox BoundsInZBand(const FDynamicMesh3& Mesh, double Z0, double Z1)
	{
		FBox Out(ForceInit);
		for (const int32 Vertex : Mesh.VertexIndicesItr())
		{
			const FVector3d P = Mesh.GetVertex(Vertex);
			if (P.Z >= Z0 && P.Z <= Z1)
			{
				Out += FVector(P);
			}
		}
		return Out;
	}

	/** The reference flat's living-room balcony guard: 4200 of MS on a 450 dwarf wall. */
	FHFRailingParams ReferenceRailing()
	{
		FHFRailingParams P;
		P.Width = 420.0;
		P.Depth = 6.0;
		P.Height = 80.0;
		P.MountBaseHeight = 45.0;
		return P;
	}

	/** The reference flat's living-room pelmet: 1900 over the south window. */
	FHFPelmetParams ReferencePelmet()
	{
		FHFPelmetParams P;
		P.Width = 190.0;
		P.Depth = 18.0;
		P.Height = 20.0;
		P.BoardThickness = 1.8;
		return P;
	}

	/**
	 * The widest run of empty space anywhere across the guard, at a given height, in centimetres.
	 *
	 * MEASURED OFF THE MESH AND NOT OFF THE PARAMETERS, which is the whole point of it existing next
	 * to FHFRailingParams::WorstClearGap. The parameter answer is what the setting-out intends; this
	 * is what was actually built, and the two are only the same if the balusters really went where
	 * the arithmetic said. A bay laid out by repeated addition from one post agrees with the formula
	 * for every gap except the last one against the far post - which is precisely the opening the
	 * sphere rule exists to close, and precisely the one a parameter-only assertion cannot see.
	 *
	 * Sampled as occupancy along X: the run is divided into fine cells, every cell that any triangle
	 * SPANNING this height covers is marked solid, and the answer is the longest unbroken stretch of
	 * unmarked cells.
	 *
	 * Spanning, and not "whose centroid is near". A soft box is lofted, so a baluster's side face is
	 * one tall quad whose centroid is at its own mid-height and nowhere else - a centroid test finds
	 * geometry at exactly one height per member and empty air everywhere else, which would report the
	 * whole run as one enormous gap and pass every assertion below for the wrong reason.
	 */
	double WidestGapAtHeight(const FDynamicMesh3& Mesh, double Z, double Width)
	{
		constexpr int32 Cells = 20000;
		const double Cell = Width / Cells;

		TArray<bool> Solid;
		Solid.Init(false, Cells);

		for (const int32 Tri : Mesh.TriangleIndicesItr())
		{
			const FIndex3i Verts = Mesh.GetTriangle(Tri);

			double MinX = TNumericLimits<double>::Max();
			double MaxX = -TNumericLimits<double>::Max();
			double MinZ = TNumericLimits<double>::Max();
			double MaxZ = -TNumericLimits<double>::Max();

			for (int32 I = 0; I < 3; ++I)
			{
				const FVector3d P = Mesh.GetVertex(Verts[I]);
				MinX = FMath::Min(MinX, P.X);
				MaxX = FMath::Max(MaxX, P.X);
				MinZ = FMath::Min(MinZ, P.Z);
				MaxZ = FMath::Max(MaxZ, P.Z);
			}

			if (Z < MinZ || Z > MaxZ)
			{
				continue;
			}

			const int32 First = FMath::Clamp(FMath::FloorToInt32((MinX + Width * 0.5) / Cell), 0, Cells - 1);
			const int32 Last = FMath::Clamp(FMath::CeilToInt32((MaxX + Width * 0.5) / Cell), 0, Cells - 1);

			for (int32 Index = First; Index <= Last; ++Index)
			{
				Solid[Index] = true;
			}
		}

		int32 Longest = 0;
		int32 Run = 0;
		for (int32 Index = 0; Index < Cells; ++Index)
		{
			Run = Solid[Index] ? 0 : Run + 1;
			Longest = FMath::Max(Longest, Run);
		}

		return Longest * Cell;
	}
}

// ============================================================================= the balcony guard

/**
 * The guard is a closed solid with real volume, every triangle roled, and exactly its drawn box.
 *
 * The drawn box matters more here than on most fittings: a railing stands on a 115 mm coping, so a
 * member 20 mm outside the drawn 60 is a base plate hanging over a four-storey drop.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingIsASolidInItsDrawnBoxTest,
	"HouseForge.Trim.RailingIsASolidInItsDrawnBox", HF_TEST_FLAGS)

bool FHFRailingIsASolidInItsDrawnBoxTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFRailingParams P = ReferenceRailing();
	const FHFRailingBuild Build = FHFFrameKit::BuildRailing(P);

	TestTrue(TEXT("A railing builds"), Build.bValid);
	TestTrue(TEXT("The guard has positive volume"), Volume(Build.Shell) > 0.0);
	TestTrue(TEXT("The guard is closed"), FHFMeshOps::IsClosed(Build.Shell));
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Build.Shell));

	const FBox Bounds = BoundsOf(Build.Shell);

	TestEqual(TEXT("The guard is exactly as long as it was drawn"),
		Bounds.Max.X - Bounds.Min.X, P.Width, 0.01);
	TestEqual(TEXT("The guard is exactly as deep as it was drawn"),
		Bounds.Max.Y - Bounds.Min.Y, P.Depth, 0.01);
	TestEqual(TEXT("The guard is exactly as tall as it was drawn"),
		Bounds.Max.Z - Bounds.Min.Z, P.Height, 0.01);

	// Symmetric about the footprint centre in both plan axes, which is what lets OnWallTop put it on
	// a coping by moving the actor rather than by knowing anything about the mesh.
	TestEqual(TEXT("The guard is centred across its own footprint"),
		Bounds.Min.Y + Bounds.Max.Y, 0.0, 0.01);
	TestEqual(TEXT("The guard is centred along its own footprint"),
		Bounds.Min.X + Bounds.Max.X, 0.0, 0.01);

	// The coping is at Z = 0 and the base plates sit on it, so nothing may hang below.
	TestEqual(TEXT("Nothing hangs below the coping"), Bounds.Min.Z, 0.0, 0.001);

	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Build.Shell);
	TestTrue(TEXT("The steel is MetalHardware"), Roles.Contains(EHFSurfaceRole::MetalHardware));
	TestFalse(TEXT("An MS balustrade has no glass in it"), Roles.Contains(EHFSurfaceRole::Glass));

	return true;
}

/**
 * NOTHING ANYWHERE IN THE GUARD PASSES A 100 mm SPHERE, measured on the mesh.
 *
 * The rule with a number behind it - NBC 2016 Part 4, and the same figure in every code that has one.
 * Asserted three ways, because the guard has three kinds of opening and only one of them is the one
 * everybody counts:
 *
 *   - between the balusters, which is what a spacing calculation is about;
 *   - under the bottom rail, which is whatever is left when the rail has been put where it looks
 *     right, and is the one that passes a toddler head first;
 *   - across the end bays, measured off the built mesh rather than off the arithmetic, because a run
 *     laid out by repeated addition agrees with the formula everywhere except against the far post.
 *
 * And a fourth thing, which is not a gap at all: there is no horizontal member between the two rails.
 * A guard with intermediate horizontals is a ladder, and a child who climbs one arrives above the
 * handrail - so the height that made it compliant is now the height they fall from.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingPassesNoSphereTest,
	"HouseForge.Trim.RailingPassesNoSphere", HF_TEST_FLAGS)

bool FHFRailingPassesNoSphereTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFRailingParams P = FHFFrameKit::SanitiseRailing(ReferenceRailing());
	const FHFRailingBuild Build = FHFFrameKit::BuildRailing(P);

	TestTrue(TEXT("A railing builds"), Build.bValid);

	// --------------------------------------------------------------- what the setting-out intends
	TestTrue(*FString::Printf(TEXT("The worst intended gap is %.2f cm, at or under the %.0f cm rule"),
		P.WorstClearGap(), P.MaxClearGap), P.WorstClearGap() <= P.MaxClearGap + 0.001);

	TestTrue(*FString::Printf(TEXT("The gap under the bottom rail is %.2f cm"), P.BottomRailClearance),
		P.BottomRailClearance <= P.MaxClearGap + 0.001);

	TestTrue(TEXT("The bay is filled with bars rather than left open"), P.BalustersPerBay() > 0);

	// ------------------------------------------------------------------- what was actually built
	//
	// Mid-infill, clear of both rails, where only the balusters are.
	const double MidZ = P.BottomRailClearance + P.BottomRailHeight
		+ P.InfillClearHeight() * 0.5;

	const double BuiltGap = WidestGapAtHeight(Build.Shell, MidZ, P.Width);

	TestTrue(*FString::Printf(
		TEXT("The widest gap actually built at mid-height is %.2f cm, at or under the %.0f cm rule"),
		BuiltGap, P.MaxClearGap), BuiltGap <= P.MaxClearGap + 0.05);

	// The built answer must also AGREE with the intended one. A mesh whose gaps are all comfortably
	// under the rule for the wrong reason - bars twice as thick as declared, say - would pass the
	// line above and be a different railing from the one the parameters describe.
	TestEqual(TEXT("What was built is what the setting-out intended"),
		BuiltGap, P.BalusterClearGap(), 0.1);

	// ----------------------------------------------------------------------- and it is not a ladder
	//
	// A horizontal member spanning a bay would show up as a stretch of the run with NO gap in it at
	// its own height. Sampled just above the bottom rail and just below the top one - the band a
	// second rail would be put in - the widest gap must still be a baluster gap.
	const double LowZ = P.BottomRailClearance + P.BottomRailHeight + P.InfillClearHeight() * 0.2;
	const double HighZ = P.BottomRailClearance + P.BottomRailHeight + P.InfillClearHeight() * 0.8;

	TestTrue(TEXT("There is no horizontal member low in the infill"),
		WidestGapAtHeight(Build.Shell, LowZ, P.Width) > P.BalusterSection);
	TestTrue(TEXT("There is no horizontal member high in the infill"),
		WidestGapAtHeight(Build.Shell, HighZ, P.Width) > P.BalusterSection);

	return true;
}

/**
 * THE GUARD IS TALL ENOUGH, MEASURED FROM THE TWO PLACES IT HAS TO BE MEASURED FROM.
 *
 * A code height is measured from the BALCONY FLOOR, and the railing is only part of the guard: the
 * parapet under it is the rest. The reference flat draws an 800 railing on what was a 1100 parapet -
 * 1900 above the floor, which is taller than standing eye level and turns the balcony into a cage,
 * and passed every test in this repository for three milestones because nothing ever added the two
 * numbers up. Here they are added up.
 *
 * The second measurement is the one raising the parapet makes WORSE rather than better: a coping is a
 * foothold, so the guard also has to clear the coping by enough that a child standing on it is still
 * behind it. 750 is the usual figure.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingIsAtACodeHeightTest,
	"HouseForge.Trim.RailingIsAtACodeHeight", HF_TEST_FLAGS)

bool FHFRailingIsAtACodeHeightTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFRailingParams P = FHFFrameKit::SanitiseRailing(ReferenceRailing());

	// NBC 2016 Part 4. 1200 is what bye-laws commonly ask above 15 m, and the reference flat clears
	// that as well - which is worth asserting rather than the bare minimum, because a guard designed
	// exactly to 1050 has nothing left when a floor finish is laid on the balcony.
	TestTrue(*FString::Printf(TEXT("The guard stands %.0f cm above the balcony floor"),
		P.GuardHeightAboveFloor()), P.GuardHeightAboveFloor() >= 105.0);

	TestTrue(*FString::Printf(TEXT("The guard clears the 120 cm figure as well (%.0f cm)"),
		P.GuardHeightAboveFloor()), P.GuardHeightAboveFloor() >= 120.0);

	TestTrue(*FString::Printf(TEXT("The guard stands %.0f cm above the coping, a foothold"),
		P.HeightAboveFoothold()), P.HeightAboveFoothold() >= 75.0);

	// AND IT IS NOT A CAGE. The upper bound is what the parapet change was actually for: a barrier
	// above standing eye level cannot be seen over, and the living room's south window - sill 900,
	// head 2100 - looks straight at it from 1.5 m away.
	TestTrue(*FString::Printf(TEXT("The guard can be seen over (%.0f cm, eye level is about 160)"),
		P.GuardHeightAboveFloor()), P.GuardHeightAboveFloor() <= 140.0);

	return true;
}

/**
 * The parameters genuinely change the object: more length is more posts, a wider bay is more bars,
 * and a glass infill is glass.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingParametersChangeItTest,
	"HouseForge.Trim.RailingParametersChangeIt", HF_TEST_FLAGS)

bool FHFRailingParametersChangeItTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFRailingParams Short = FHFFrameKit::SanitiseRailing(ReferenceRailing());

	FHFRailingParams Long = ReferenceRailing();
	Long.Width = 840.0;
	Long = FHFFrameKit::SanitiseRailing(Long);

	TestTrue(TEXT("Twice the run gets more posts"), Long.PostCount() > Short.PostCount());
	TestTrue(TEXT("The posts still span no further than the limit"),
		(Long.Width - Long.PostSection) / (Long.PostCount() - 1) <= Long.MaxPostSpacing + 0.001);
	TestTrue(TEXT("And the bars still close the sphere"),
		Long.WorstClearGap() <= Long.MaxClearGap + 0.001);

	// A WIDER BAY IS MORE BARS, NOT WIDER GAPS. Allowing the posts further apart is the one change a
	// designer would make that could silently open the infill up.
	FHFRailingParams WideBays = ReferenceRailing();
	WideBays.MaxPostSpacing = 200.0;
	WideBays = FHFFrameKit::SanitiseRailing(WideBays);

	TestTrue(TEXT("Wider bays have more bars in them"),
		WideBays.BalustersPerBay() > Short.BalustersPerBay());
	TestTrue(TEXT("And the gap is still under the rule"),
		WideBays.WorstClearGap() <= WideBays.MaxClearGap + 0.001);

	// ------------------------------------------------------------------------------- glass infill
	FHFRailingParams Glazed = ReferenceRailing();
	Glazed.Infill = EHFRailingInfill::Glass;

	const FHFRailingBuild GlassBuild = FHFFrameKit::BuildRailing(Glazed);
	const FHFRailingBuild BarBuild = FHFFrameKit::BuildRailing(Short);

	TestTrue(TEXT("A glazed guard builds"), GlassBuild.bValid);

	const TSet<EHFSurfaceRole> GlassRoles = FHFMeshOps::RolesPresent(GlassBuild.Shell);
	TestTrue(TEXT("A glazed guard has glass in it"), GlassRoles.Contains(EHFSurfaceRole::Glass));

	// The panel has real thickness rather than being a plane: a zero-thickness pane refracts nothing
	// and reflects wrongly, which is the rule in .claude/rules/04-conventions.md.
	TestTrue(TEXT("The panel is a solid, not a plane"), Volume(GlassBuild.Infill) > 0.0);

	const FBox GlassInfill = BoundsOf(GlassBuild.Infill);
	TestEqual(TEXT("The panel is as thick as it was specified"),
		GlassInfill.Max.Y - GlassInfill.Min.Y,
		FHFFrameKit::SanitiseRailing(Glazed).GlassThickness, 0.01);

	TestTrue(TEXT("Glass and bars are different objects"),
		FMath::Abs(Volume(GlassBuild.Shell) - Volume(BarBuild.Shell)) > 1.0);

	// The frame is the same either way: the infill is a choice about what fills it, not about it.
	TestEqual(TEXT("The frame is unchanged by what fills it"),
		Volume(GlassBuild.Frame), Volume(BarBuild.Frame), 0.01);

	return true;
}

/** Degenerate input produces nothing at all, rather than a sliver nobody notices. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingRefusesDegenerateInputTest,
	"HouseForge.Trim.RailingRefusesDegenerateInput", HF_TEST_FLAGS)

bool FHFRailingRefusesDegenerateInputTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	auto Empty = [this](const TCHAR* What, const FHFRailingParams& P)
	{
		const FHFRailingBuild Build = FHFFrameKit::BuildRailing(P);
		TestFalse(*FString::Printf(TEXT("%s builds nothing"), What), Build.bValid);
		TestEqual(*FString::Printf(TEXT("%s leaves an empty mesh"), What),
			Build.Shell.TriangleCount(), 0);
	};

	FHFRailingParams NoWidth = ReferenceRailing();
	NoWidth.Width = 0.0;
	Empty(TEXT("A railing with no length"), NoWidth);

	FHFRailingParams NoDepth = ReferenceRailing();
	NoDepth.Depth = 0.0;
	Empty(TEXT("A railing with no thickness"), NoDepth);

	FHFRailingParams NoHeight = ReferenceRailing();
	NoHeight.Height = 0.0;
	Empty(TEXT("A railing with no height"), NoHeight);

	FHFRailingParams NoPost = ReferenceRailing();
	NoPost.PostSection = 0.0;
	Empty(TEXT("A railing with no posts to hold it up"), NoPost);

	FHFRailingParams Negative = ReferenceRailing();
	Negative.Width = -420.0;
	Empty(TEXT("A railing with a negative length"), Negative);

	return true;
}

/**
 * A dangerous figure is refused rather than honoured.
 *
 * The sphere rule is enforced on the way in, not documented in a comment. A designer who types 150
 * into the gap under the bottom rail - because 150 looks right, and it does - gets 100, in the same
 * way FHFCeilingDefaults clamps a band drop that would push a downlight through the slab.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFRailingSanitiseEnforcesTheRuleTest,
	"HouseForge.Trim.RailingSanitiseEnforcesTheRule", HF_TEST_FLAGS)

bool FHFRailingSanitiseEnforcesTheRuleTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	FHFRailingParams Loose = ReferenceRailing();
	Loose.BottomRailClearance = 15.0;

	const FHFRailingParams Fixed = FHFFrameKit::SanitiseRailing(Loose);

	TestEqual(TEXT("A 150 mm gap under the bottom rail is cut back to the rule"),
		Fixed.BottomRailClearance, Fixed.MaxClearGap, 0.001);

	// And it is enforced in the built mesh, not only in the struct: the bottom rail really is that
	// close to the coping.
	const FHFRailingBuild Build = FHFFrameKit::BuildRailing(Loose);
	const FBox LowBand = BoundsInZBand(Build.Shell, 0.0, Fixed.MaxClearGap + 1.0);

	TestTrue(TEXT("The bottom rail is within the rule of the coping"),
		LowBand.IsValid && LowBand.Max.Z >= Fixed.MaxClearGap - 0.01);

	// A member thicker than the drawn box is clamped rather than allowed to stand proud of it.
	FHFRailingParams FatPost = ReferenceRailing();
	FatPost.PostSection = 12.0;

	const FHFRailingParams Trimmed = FHFFrameKit::SanitiseRailing(FatPost);
	TestTrue(TEXT("A post may not be thicker than the railing was drawn"),
		Trimmed.PostSection <= Trimmed.Depth + 0.001);

	return true;
}

// ============================================================================= the curtain pelmet

/**
 * The pelmet is a closed solid with real volume, every triangle roled, and exactly its drawn box.
 *
 * WATERTIGHT WITH AN OPEN UNDERSIDE, which is the property worth stating out loud. A pelmet is a slot
 * you look up into, and it is watertight all the same because every member is its own closed solid
 * joined through AppendPreservingRoles - the slot is a gap BETWEEN solids, not a hole in one.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPelmetIsASolidInItsDrawnBoxTest,
	"HouseForge.Trim.PelmetIsASolidInItsDrawnBox", HF_TEST_FLAGS)

bool FHFPelmetIsASolidInItsDrawnBoxTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFPelmetParams P = ReferencePelmet();
	const FHFPelmetBuild Build = FHFWallPlateKit::BuildPelmet(P);

	TestTrue(TEXT("A pelmet builds"), Build.bValid);
	TestTrue(TEXT("The pelmet has positive volume"), Volume(Build.Shell) > 0.0);
	TestTrue(TEXT("The pelmet is closed"), FHFMeshOps::IsClosed(Build.Shell));
	TestTrue(TEXT("Every triangle carries a surface role"), EveryTriangleHasARole(Build.Shell));

	const FBox Bounds = BoundsOf(Build.Shell);

	TestEqual(TEXT("The pelmet is exactly as long as it was drawn"),
		Bounds.Max.X - Bounds.Min.X, P.Width, 0.01);
	TestEqual(TEXT("The pelmet is exactly as deep as it was drawn"),
		Bounds.Max.Y - Bounds.Min.Y, P.Depth, 0.01);
	TestEqual(TEXT("The pelmet is exactly as tall as it was drawn"),
		Bounds.Max.Z - Bounds.Min.Z, P.Height, 0.01);

	// The back plane is at +Depth/2 and the fascia's front at -Depth/2: the drawn depth is the whole
	// projection off the plaster and nothing is allowed past it. That is what
	// FHFFixturePlacement::UnderSoffit relies on when it lands the back on the wall face.
	TestEqual(TEXT("The back plane is where the placement expects it"),
		Bounds.Max.Y, P.Depth * 0.5, 0.01);
	TestEqual(TEXT("Nothing stands proud of the drawn front"),
		Bounds.Min.Y, -P.Depth * 0.5, 0.01);

	// The top is at the drawn height, which is what UnderSoffit lands on the ceiling.
	TestEqual(TEXT("The top board's face is the top of the drawn box"), Bounds.Max.Z, P.Height, 0.01);

	const TSet<EHFSurfaceRole> Roles = FHFMeshOps::RolesPresent(Build.Shell);

	// THE CASE IS FINISHED AS THE CEILING IS, and that is a decision rather than a leftover. A pelmet
	// is painted with the ceiling it hangs off, by the same painter on the same day, and it is the
	// finish - not the geometry - that decides whether the room reads a step in its ceiling or a box
	// screwed to its wall.
	TestTrue(TEXT("The case is finished as the ceiling"),
		Roles.Contains(EHFSurfaceRole::CeilingSoffit));
	TestTrue(TEXT("The track is metal"), Roles.Contains(EHFSurfaceRole::MetalHardware));

	return true;
}

/**
 * THE PELMET HIDES WHAT IT EXISTS TO HIDE.
 *
 * A curtain track is 25 mm and a pinch-pleat heading is 75 to 90 hanging off it. A pelmet whose
 * fascia stops above that shows a row of hooks along the top of the curtain - which is the one thing
 * the whole fitting is for, and it is invisible in plan, in section, and in any test that only
 * measures the box.
 *
 * Measured between two solids rather than on one, which is why FHFPelmetBuild keeps the track apart
 * from the case.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPelmetHidesTheTrackTest,
	"HouseForge.Trim.PelmetHidesTheTrack", HF_TEST_FLAGS)

bool FHFPelmetHidesTheTrackTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFPelmetParams P = FHFWallPlateKit::SanitisePelmet(ReferencePelmet());
	const FHFPelmetBuild Build = FHFWallPlateKit::BuildPelmet(P);

	TestTrue(TEXT("A pelmet builds"), Build.bValid);
	TestTrue(TEXT("There is a track in it"), Build.Track.TriangleCount() > 0);
	TestTrue(TEXT("The track is a solid section, not a bar"), Volume(Build.Track) > 0.0);

	const FBox Track = BoundsOf(Build.Track);
	const FBox Case = BoundsOf(Build.Case);

	// A pinch-pleat heading is 75 to 90 mm. Below 90 the hooks start to show.
	TestTrue(*FString::Printf(TEXT("The fascia hides %.1f cm of curtain heading below the track"),
		P.ConcealedHeadingHeight()), P.ConcealedHeadingHeight() >= 9.0);

	TestEqual(TEXT("The measured drop below the track agrees with the parameter"),
		Track.Min.Z - Case.Min.Z, P.ConcealedHeadingHeight(), 0.05);

	// THE TRACK IS BEHIND THE FASCIA, not in front of it. A track set out from the wrong face hangs
	// its curtain outside the box, which is a pelmet hiding nothing at all.
	TestTrue(TEXT("The track is behind the fascia"), Track.Min.Y > -P.Depth * 0.5 + P.BoardThickness - 0.01);

	// And there is somewhere for the curtain to hang: a heading needs the fascia not to touch it.
	TestTrue(*FString::Printf(TEXT("There is %.1f cm of clear air in front of the track"),
		Track.Min.Y - (-P.Depth * 0.5 + P.BoardThickness)),
		Track.Min.Y - (-P.Depth * 0.5 + P.BoardThickness) >= 1.0);

	// The track runs the length of the slot rather than stopping short of it, or the curtain cannot
	// be drawn to the end of its own pelmet.
	TestEqual(TEXT("The track runs the clear length of the slot"),
		Track.Max.X - Track.Min.X, P.ClearWidth(), 0.05);

	// The ends are closed. A pelmet you can see into from the side of the room is a slot with the
	// track lit up in it, and every one of these is on a window wall the seating looks along.
	const FBox LeftEnd = BoundsInZBand(Build.Case, 0.0, P.Height * 0.25);
	TestTrue(TEXT("The case reaches the ends of the run"),
		LeftEnd.IsValid && FMath::IsNearlyEqual(LeftEnd.Min.X, -P.Width * 0.5, 0.01));

	return true;
}

/** The parameters genuinely change the object, and a shallow pelmet is squeezed rather than refused. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPelmetParametersChangeItTest,
	"HouseForge.Trim.PelmetParametersChangeIt", HF_TEST_FLAGS)

bool FHFPelmetParametersChangeItTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	const FHFPelmetBuild Reference = FHFWallPlateKit::BuildPelmet(ReferencePelmet());

	FHFPelmetParams Wide = ReferencePelmet();
	Wide.Width = 220.0;
	const FHFPelmetBuild WideBuild = FHFWallPlateKit::BuildPelmet(Wide);

	TestTrue(TEXT("A longer pelmet is a bigger object"),
		Volume(WideBuild.Shell) > Volume(Reference.Shell) + 1.0);
	TestEqual(TEXT("And it is exactly as long as it was asked for"),
		BoundsOf(WideBuild.Shell).Max.X - BoundsOf(WideBuild.Shell).Min.X, Wide.Width, 0.01);

	// The board thickness is a PROJECT figure, resolved by the composing layer from the joinery
	// defaults - so changing it has to change the pelmet, or reading it was pointless.
	FHFPelmetParams ThickBoard = ReferencePelmet();
	ThickBoard.BoardThickness = 2.5;
	const FHFPelmetBuild ThickBuild = FHFWallPlateKit::BuildPelmet(ThickBoard);

	TestTrue(TEXT("A thicker board is a heavier pelmet"),
		Volume(ThickBuild.Shell) > Volume(Reference.Shell) + 1.0);
	TestTrue(TEXT("And it leaves a shorter slot"),
		FHFWallPlateKit::SanitisePelmet(ThickBoard).ClearWidth()
			< FHFWallPlateKit::SanitisePelmet(ReferencePelmet()).ClearWidth() - 0.5);

	// A SHALLOW PELMET IS SQUEEZED, NOT REFUSED. A drawing that gives a 100 deep pelmet has drawn a
	// shallow one; the honest answer is the deepest track setback that fits, not an empty mesh.
	FHFPelmetParams Shallow = ReferencePelmet();
	Shallow.Depth = 8.0;

	const FHFPelmetParams ShallowFixed = FHFWallPlateKit::SanitisePelmet(Shallow);
	const FHFPelmetBuild ShallowBuild = FHFWallPlateKit::BuildPelmet(Shallow);

	TestTrue(TEXT("A shallow pelmet still builds"), ShallowBuild.bValid);
	TestTrue(TEXT("And its track still fits inside it"),
		ShallowFixed.TrackSetback + ShallowFixed.TrackWidth <= ShallowFixed.SlotDepth() + 0.001);
	TestEqual(TEXT("And it is still exactly as deep as it was drawn"),
		BoundsOf(ShallowBuild.Shell).Max.Y - BoundsOf(ShallowBuild.Shell).Min.Y, Shallow.Depth, 0.01);

	return true;
}

/** Degenerate input produces nothing at all. */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFPelmetRefusesDegenerateInputTest,
	"HouseForge.Trim.PelmetRefusesDegenerateInput", HF_TEST_FLAGS)

bool FHFPelmetRefusesDegenerateInputTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	auto Empty = [this](const TCHAR* What, const FHFPelmetParams& P)
	{
		const FHFPelmetBuild Build = FHFWallPlateKit::BuildPelmet(P);
		TestFalse(*FString::Printf(TEXT("%s builds nothing"), What), Build.bValid);
		TestEqual(*FString::Printf(TEXT("%s leaves an empty mesh"), What),
			Build.Shell.TriangleCount(), 0);
	};

	FHFPelmetParams NoWidth = ReferencePelmet();
	NoWidth.Width = 0.0;
	Empty(TEXT("A pelmet with no length"), NoWidth);

	FHFPelmetParams NoDepth = ReferencePelmet();
	NoDepth.Depth = 0.0;
	Empty(TEXT("A pelmet with no depth"), NoDepth);

	FHFPelmetParams NoHeight = ReferencePelmet();
	NoHeight.Height = 0.0;
	Empty(TEXT("A pelmet with no height"), NoHeight);

	FHFPelmetParams Negative = ReferencePelmet();
	Negative.Height = -20.0;
	Empty(TEXT("A pelmet with a negative height"), Negative);

	return true;
}

/**
 * NOTHING IN THIS GROUP ARTICULATES, and both of the two say why.
 *
 * Stated as a test rather than only as a comment, because the alternative is that somebody later
 * "fixes" it by bolting a mechanism onto one of them to satisfy the rule in
 * .claude/rules/04-conventions.md - which is the rule read backwards. It says a thing that moves in
 * the real object moves here, not that everything must be given something to move.
 *
 * A GATE swings and there is none in 10.2 m of continuous balustrade. A CURTAIN slides, and it is the
 * one thing in this milestone a rigid part genuinely cannot represent: fabric drawn back gathers to
 * about a fifth of its width, and no translation of a solid does that. A leaf modelled closed and
 * slid sideways runs a metre past the end of its own pelmet; a leaf modelled gathered and slid across
 * covers a 1900 window with a 350 rag. Both measure as motion and both are visible lies - which is
 * this project's own failure mode, from a wardrobe whose two leaves each travelled their full 118 cm
 * and cancelled.
 */
IMPLEMENT_SIMPLE_AUTOMATION_TEST(FHFTrimHasNoMovingPartsTest,
	"HouseForge.Trim.NothingInTheTrimArticulates", HF_TEST_FLAGS)

bool FHFTrimHasNoMovingPartsTest::RunTest(const FString& Parameters)
{
	using namespace HouseForgeTrim;

	// NEITHER BUILD STRUCT HAS A Parts ARRAY. That is the statement, and it is made in the type
	// rather than at run time: FHFRailingBuild and FHFPelmetBuild carry sub-assemblies for
	// measurement and nothing that opens, so there is no empty Parts array for somebody to fill in
	// later and no master open amount on either actor to drive it with. Both actors derive from
	// AHFElementActor and not AHFArticulatedActor, which HFTrimActorTests asserts in the flat.
	static_assert(sizeof(FHFRailingBuild) > 0, "A railing is composed, not articulated.");
	static_assert(sizeof(FHFPelmetBuild) > 0, "A pelmet is composed, not articulated.");

	const FHFRailingBuild Railing = FHFFrameKit::BuildRailing(ReferenceRailing());
	TestTrue(TEXT("A balustrade builds its whole self as one fixed shell"),
		Railing.bValid && Volume(Railing.Shell) > 0.0);

	const FHFPelmetBuild Pelmet = FHFWallPlateKit::BuildPelmet(ReferencePelmet());
	TestTrue(TEXT("A pelmet builds a track and a slot, and nothing that opens"), Pelmet.bValid);

	// What the pelmet DOES have is the space a curtain would hang in, which is the honest half of the
	// answer: the fitting is complete, and the curtain is a fixture of its own that this flat does
	// not declare.
	const FHFPelmetParams P = FHFWallPlateKit::SanitisePelmet(ReferencePelmet());
	TestTrue(TEXT("There is a slot for a curtain to hang in"), P.SlotDepth() > 5.0);
	TestTrue(TEXT("And the heading it would hang by is hidden"), P.ConcealedHeadingHeight() >= 9.0);

	return true;
}

#undef HF_TEST_FLAGS

#endif // WITH_DEV_AUTOMATION_TESTS
