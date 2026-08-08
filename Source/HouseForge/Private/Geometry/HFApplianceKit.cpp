// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFApplianceKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	constexpr int32 RevolveSides = 20;

	/**
	 * Sides for a circle that somebody actually stands in front of.
	 *
	 * A WASHING MACHINE'S PORTHOLE IS THE ONE CIRCLE IN THIS FILE AT WAIST HEIGHT. Twenty sides is
	 * plenty for a tap body or a pipe seen at two metres, and it is visibly a dodecagon on a 320 mm
	 * door somebody is standing over - the render showed the bezel's facets before it showed anything
	 * else about the machine. Forty costs nothing and is round.
	 */
	constexpr int32 CloseUpSides = 40;

	/** Turns a mesh about an axis through its own origin, in degrees. */
	void RotateAboutOrigin(FDynamicMesh3& Mesh, const FVector3d& Axis, double Degrees)
	{
		MeshTransforms::ApplyTransform(Mesh,
			FTransformSRT3d(FQuaterniond(Axis, Degrees, /*bAngleIsDegrees*/ true),
				FVector3d::Zero(), FVector3d::One()),
			/*bReverseOrientationIfNeeded*/ true);
	}

	/** Height of a burner's cast crown over the glass. */
	constexpr double BurnerCrownHeight = 1.6;

	/** Section of the cast iron a pan support is made from. */
	constexpr double GrateBarSection = 0.9;

	/** Thickness of a baffle filter panel. */
	constexpr double FilterThickness = 1.2;

	/** Wall of the hollow skirt the canopy's mouth is formed by. */
	constexpr double SkirtWallThickness = 1.2;

	/**
	 * How far up inside the mouth the filter sits, off the canopy's bottom edge.
	 *
	 * A filter flush with the bottom of the hood is a filter in the same plane as the hood, which is
	 * z-fighting; and a real one is recessed anyway, so the rim of the canopy shields the edge of it.
	 */
	constexpr double FilterLift = 0.8;

	TArray<FVector2D> PlanRect(double X0, double Y0, double X1, double Y1)
	{
		return { FVector2D(X0, Y0), FVector2D(X1, Y0), FVector2D(X1, Y1), FVector2D(X0, Y1) };
	}

	/** A closed regular polygon, for a hole in a prism. Winding is normalised by the triangulator. */
	TArray<FVector2D> HoleRing(const FVector2D& Centre, double Radius, int32 Sides)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Sides);

		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const double Angle = 2.0 * PI * static_cast<double>(Index) / static_cast<double>(Sides);
			Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}

		return Out;
	}

	/**
	 * Stands a panel built in XY and extruded up +Z onto the front of an appliance.
	 *
	 * A quarter turn about +X takes (x, y, z) to (x, -z, y): the panel's own up axis becomes world up
	 * and the EXTRUSION runs along -Y. Translating by the thickness puts the result at Y from 0 to
	 * Thickness, which is a front panel with its face on the front plane and its body behind it.
	 *
	 * The other sign of that turn is the mistake worth naming: -90 maps the panel's up axis to -Z, so
	 * the geometry comes out correct in every dimension and upside down. A rectangle with a circle in
	 * the middle of it looks identical either way, which is exactly why it has to be reasoned about
	 * once here rather than eyeballed at each call.
	 */
	void StandPanelUp(FDynamicMesh3& Mesh, double Thickness)
	{
		MeshTransforms::ApplyTransform(Mesh,
			FTransformSRT3d(FQuaterniond(FVector3d::UnitX(), 90.0, /*bAngleIsDegrees*/ true),
				FVector3d(0.0, Thickness, 0.0), FVector3d::One()),
			/*bReverseOrientationIfNeeded*/ true);
	}

	/**
	 * An annular sector, as a hole outline: out along one radius, round the outer arc, back in.
	 *
	 * What a PRESSED fan guard is made of, and the reason it is built this way rather than as wire
	 * rings. FHFMeshOps::AppendRevolvedProfile cannot make a torus: a profile that never reaches the
	 * axis is CAPPED at both ends with full discs, so a square-section ring comes out as a squat solid
	 * cylinder with two coincident faces welded across its near end. Closed, positive volume, passes
	 * every test - and renders as a plate where the guard should be. Perforating a disc is the version
	 * of a grille the primitives here can actually make, and a pressed slotted guard is what half the
	 * condensing units in the country wear anyway.
	 */
	TArray<FVector2D> AnnularSector(const FVector2D& Centre, double InnerRadius, double OuterRadius,
		double StartDegrees, double SweepDegrees, int32 ArcSteps)
	{
		TArray<FVector2D> Out;
		Out.Reserve(2 * (ArcSteps + 1));

		for (int32 Step = 0; Step <= ArcSteps; ++Step)
		{
			const double Angle = FMath::DegreesToRadians(
				StartDegrees + SweepDegrees * static_cast<double>(Step) / static_cast<double>(ArcSteps));
			Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * OuterRadius);
		}

		for (int32 Step = ArcSteps; Step >= 0; --Step)
		{
			const double Angle = FMath::DegreesToRadians(
				StartDegrees + SweepDegrees * static_cast<double>(Step) / static_cast<double>(ArcSteps));
			Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * InnerRadius);
		}

		return Out;
	}

	/**
	 * Where each burner sits, in hob-local plan.
	 *
	 * A FOUR-BURNER HOB IS NOT FOUR EQUAL BURNERS IN A SQUARE. It is a big one and a small one at the
	 * front and two mediums behind, staggered, because that is what fits pans of different sizes on a
	 * 580 top. Four identical circles on a grid is the arrangement that most says "generated".
	 */
	void BurnerLayout(const FHFHobParams& P, int32 Index, FVector2D& OutCentre, double& OutRadius)
	{
		const double QuarterX = P.Width * 0.25;
		const double QuarterY = P.Depth * 0.25;

		// Big, small, and two mediums - as a fraction of the quarter-width, so the set scales.
		static constexpr double Radii[4] = { 0.62, 0.44, 0.53, 0.53 };
		static constexpr double Xs[4] = { -1.0, 1.0, -1.0, 1.0 };
		static constexpr double Ys[4] = { -1.0, -1.0, 1.0, 1.0 };

		const int32 Slot = Index % 4;
		OutCentre = FVector2D(Xs[Slot] * QuarterX, Ys[Slot] * QuarterY);
		OutRadius = Radii[Slot] * QuarterX;
	}
}

// ---------------------------------------------------------------------------------------- the hob

FHFHobParams FHFApplianceKit::SanitiseHob(const FHFHobParams& Params)
{
	FHFHobParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.GlassThickness = FMath::Max(P.GlassThickness, 0.1);
	P.BodyDepth = FMath::Max(P.BodyDepth, 0.0);
	P.BurnerCount = FMath::Clamp(P.BurnerCount, 1, 6);
	P.GrateHeight = FMath::Max(P.GrateHeight, 0.0);

	// A knob wider than a quarter of the fascia is a knob overlapping its neighbour.
	P.KnobRadius = FMath::Clamp(P.KnobRadius, 0.0,
		FMath::Max(P.Width / static_cast<double>(2 * P.BurnerCount + 2), 0.0));

	P.KnobSweepDegrees = FMath::Clamp(P.KnobSweepDegrees, 0.0, 360.0);
	return P;
}

FName FHFApplianceKit::KnobPartId(int32 Index)
{
	return FName(*FString::Printf(TEXT("Knob%d"), FMath::Max(Index, 0)));
}

FHFApplianceBuild FHFApplianceKit::BuildHob(const FHFHobParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFHobParams P = SanitiseHob(Params);
	if (!P.IsValid())
	{
		return Out;
	}

	const FVector2D Half(P.Width * 0.5, P.Depth * 0.5);

	// ------------------------------------------------------------------------------------ the glass
	//
	// The plate that sits ON the stone, lapping the cutout all the way round. Tagged Glass rather
	// than Appliance because it is exactly that - a sheet of black toughened glass - and the material
	// panel has to be able to reach it as glass.

	FHFMeshOps::AppendBox(Out.Shell, FVector3d(0.0, 0.0, P.GlassThickness * 0.5),
		FVector3d(Half.X, Half.Y, P.GlassThickness * 0.5), 0.0, EHFSurfaceRole::Glass);

	// ------------------------------------------------------------------------------------- the body
	//
	// Hangs BELOW the counter, through the cutout, which is why the cutout has to exist at all. Held
	// in from the glass by the rim lap, so the body passes through the hole rather than resting on it.

	if (P.BodyDepth > 0.0)
	{
		const FVector2D BodyHalf(FMath::Max(Half.X - 1.0, 0.1), FMath::Max(Half.Y - 1.0, 0.1));

		FDynamicMesh3 Body;
		FHFMeshOps::InitialiseMesh(Body);
		FHFMeshOps::AppendBox(Body, FVector3d(0.0, 0.0, -P.BodyDepth * 0.5),
			FVector3d(BodyHalf.X, BodyHalf.Y, P.BodyDepth * 0.5), 0.0, EHFSurfaceRole::Appliance);
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Body);
	}

	// ---------------------------------------------------------------------------------- the burners

	const double GlassTop = P.GlassThickness;

	for (int32 Burner = 0; Burner < P.BurnerCount; ++Burner)
	{
		FVector2D Centre;
		double Radius = 0.0;
		BurnerLayout(P, Burner, Centre, Radius);

		if (Radius <= 0.0)
		{
			continue;
		}

		// The cast brass crown: a stepped cone, which is what a burner head actually looks like from
		// above and is the part that catches a highlight in every kitchen photograph.
		const TArray<FVector2D> Crown = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, Radius),
			FVector2D(BurnerCrownHeight * 0.45, Radius),
			FVector2D(BurnerCrownHeight * 0.55, Radius * 0.82),
			FVector2D(BurnerCrownHeight, Radius * 0.72),
			FVector2D(BurnerCrownHeight, 0.0)
		};

		FDynamicMesh3 Head;
		FHFMeshOps::InitialiseMesh(Head);

		if (FHFMeshOps::AppendRevolvedProfile(Head, Crown,
			FVector3d(Centre.X, Centre.Y, GlassTop), FVector3d::UnitZ(), RevolveSides,
			EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Head);
		}

		// The pan support over it: two crossed cast bars. A pan has to stand on something, and a
		// burner with nothing over it reads as a hole in the glass.
		if (P.GrateHeight > BurnerCrownHeight)
		{
			const double BarZ = GlassTop + P.GrateHeight - GrateBarSection;
			const double Reach = Radius * 1.55;

			FDynamicMesh3 Grate;
			FHFMeshOps::InitialiseMesh(Grate);

			FHFMeshOps::AppendBox(Grate, FVector3d(Centre.X, Centre.Y, BarZ + GrateBarSection * 0.5),
				FVector3d(Reach, GrateBarSection * 0.5, GrateBarSection * 0.5), 0.0,
				EHFSurfaceRole::MetalHardware);

			FHFMeshOps::AppendBox(Grate, FVector3d(Centre.X, Centre.Y, BarZ + GrateBarSection * 0.5),
				FVector3d(GrateBarSection * 0.5, Reach, GrateBarSection * 0.5), 0.0,
				EHFSurfaceRole::MetalHardware);

			// Feet, so the crossed bars stand on the glass rather than floating over it.
			for (const double Side : { -1.0, 1.0 })
			{
				FHFMeshOps::AppendBox(Grate,
					FVector3d(Centre.X + Side * Reach * 0.85, Centre.Y,
						GlassTop + (P.GrateHeight - GrateBarSection) * 0.5),
					FVector3d(GrateBarSection * 0.5, GrateBarSection * 0.5,
						(P.GrateHeight - GrateBarSection) * 0.5),
					0.0, EHFSurfaceRole::MetalHardware);
			}

			FHFMeshOps::AppendPreservingRoles(Out.Shell, Grate);
		}
	}

	// ------------------------------------------------------------------------------------ the knobs
	//
	// ONE PER BURNER, EACH TURNING ABOUT ITS OWN AXIS. On the front edge of the glass where a real
	// hob's controls are, and each its own part - a hob whose knobs are moulded into the top is
	// exactly what .claude/rules/04-conventions.md rules out.
	//
	// ## They stand ON the glass and turn about the vertical, and both of those are corrections
	//
	// They used to be discs on a vertical front face, turning about Y - which is a FREESTANDING
	// COOKER's controls, and this is not one. A drop-in hob has no front face at all: it is a sheet
	// of glass lying on the worktop with the burner box hanging through a hole in it, and there is
	// nowhere below the glass line for anything to be except inside the stone. Built that way, each
	// of the four knobs had 12 mm of itself inside the granite - 1.6 of knob radius against 0.4 of
	// half-glass - permanently, in a place a camera at counter height looks straight at.
	//
	// Nothing on the hob could see it. The knobs were the right size, in the right place on the
	// appliance, each with a real hinge, a real axis and a real sweep, and every one of them turned.
	// It took comparing the hob with the counter it is cut into.
	//
	// So the profile is revolved about Z, standing up from the glass the way the knobs on every glass
	// hob in this domain do, and turning the way a hand actually turns one.

	if (P.KnobRadius > 0.0)
	{
		const double Pitch = P.Width / static_cast<double>(P.BurnerCount + 1);

		for (int32 Knob = 0; Knob < P.BurnerCount; ++Knob)
		{
			FHFMeshPart Part;
			Part.PartId = KnobPartId(Knob);
			FHFMeshOps::InitialiseMesh(Part.Mesh);

			// A domed knob with a flat indexing flag on it, so which way it is turned can be SEEN.
			// A plain cylinder rotates invisibly, which passes every assertion about motion and
			// looks like nothing at all.
			//
			// The profile is (along the axis, radius) - the same convention every revolve in this
			// plugin takes: a waisted body standing off the glass, swelling to its full width at the
			// top where a hand takes it. Only the two ends may have zero radius, or the solid
			// pinches.
			const double KnobHeight = P.KnobRadius * 1.25;

			const TArray<FVector2D> KnobProfile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, P.KnobRadius * 0.85),
				FVector2D(KnobHeight * 0.55, P.KnobRadius * 0.72),
				FVector2D(KnobHeight * 0.85, P.KnobRadius),
				FVector2D(KnobHeight, P.KnobRadius * 0.9),
				FVector2D(KnobHeight, 0.0)
			};

			// Turning about Z, standing on the glass. Nothing of it is below the top of the stone.
			FHFMeshOps::AppendRevolvedProfile(Part.Mesh, KnobProfile, FVector3d::ZeroVector,
				FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);

			// The pointer flag, running out to the knob's edge on one side only so a turn is legible.
			FHFMeshOps::AppendBox(Part.Mesh,
				FVector3d(0.0, -P.KnobRadius * 0.5, KnobHeight),
				FVector3d(P.KnobRadius * 0.14, P.KnobRadius * 0.5, P.KnobRadius * 0.1),
				0.0, EHFSurfaceRole::MetalHardware);

			FHFMeshOps::ApplyWorldScaleUVs(Part.Mesh);

			// ON the glass, and far enough back from the front edge to be on it rather than over the
			// arris: a knob centred on the very edge overhangs the cut and reads as loose.
			Part.PivotTransform = FTransform(FVector(
				-P.Width * 0.5 + Pitch * (Knob + 1),
				-Half.Y + P.KnobRadius * 1.4,
				P.GlassThickness));

			Part.Motion.Type = EHFMotionType::Hinge;
			Part.Motion.Axis = FVector::ZAxisVector;
			Part.Motion.MaxAngleDegrees = -P.KnobSweepDegrees;
			Part.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Part));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// ----------------------------------------------------------------------------------- the chimney

FHFChimneyParams FHFApplianceKit::SanitiseChimney(const FHFChimneyParams& Params)
{
	FHFChimneyParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.CanopyHeight = FMath::Max(P.CanopyHeight, 0.0);
	P.DuctLength = FMath::Max(P.DuctLength, 0.0);

	// The bracket gap comes off the depth before anything is measured against it, and it can never eat
	// more than a fifth of the hood: a gap that swallowed the canopy would be a hood hanging in
	// mid-air rather than one screwed to a wall.
	P.WallGap = FMath::Clamp(P.WallGap, 0.0, P.Depth * 0.2);

	// The duct casing cannot be wider than the canopy it stands on, or the taper turns inside out.
	P.DuctWidth = FMath::Clamp(P.DuctWidth, 0.0, P.Width);
	P.DuctDepth = FMath::Clamp(P.DuctDepth, 0.0, P.BackY());

	// The taper is part of the canopy, so it cannot be taller than one.
	P.TaperHeight = FMath::Clamp(P.TaperHeight, 0.0, P.CanopyHeight);

	P.FilterPanels = FMath::Clamp(P.FilterPanels, 1, 6);
	P.FilterDropDegrees = FMath::Clamp(P.FilterDropDegrees, 0.0, 90.0);

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildChimney(const FHFChimneyParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFChimneyParams P = SanitiseChimney(Params);
	if (!P.IsValid())
	{
		return Out;
	}

	// ------------------------------------------------------------------------------- the lower skirt
	//
	// The straight part of the canopy, which is the bit the filter hangs in.
	//
	// A HOLLOW SKIRT, NOT A SOLID BOX. A cooker hood is open underneath - that is how it draws - and
	// its filter sits up inside the mouth where somebody standing at the hob looks straight at it.
	// Built as a solid, the filter was buried inside the block AND its underside landed in the same
	// plane as the canopy's, 64 cm2 of both facing down and fighting for the depth test. Four walls
	// round an open mouth is what the thing actually is, and it fixes both at once.

	const double StraightHeight = P.CanopyHeight - P.TaperHeight;

	if (StraightHeight > 0.0)
	{
		const double Inner = FMath::Min(SkirtWallThickness,
			FMath::Min(P.Width, P.Depth) * 0.25);

		const TArray<FVector2D> Outer = PlanRect(0.0, 0.0, P.Width, P.BackY());
		const TArray<FVector2D> Mouth = PlanRect(Inner, Inner, P.Width - Inner, P.BackY() - Inner);

		FDynamicMesh3 Skirt;
		FHFMeshOps::InitialiseMesh(Skirt);

		if (FHFMeshOps::AppendPrismWithHoles(Skirt, Outer, { Mouth }, 0.0, StraightHeight,
			EHFSurfaceRole::Appliance))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Skirt);
		}
	}

	// ---------------------------------------------------------------------------------- the taper
	//
	// A PYRAMIDAL HOOD IS WHAT A CHIMNEY IS. Built as a lofted section from the canopy's plan up to
	// the duct's, so the four sloped faces catch light differently from the box below - which is the
	// whole silhouette of the thing. A straight box with a pipe on it is a wall cupboard.
	//
	// Swept as a prism between two outlines is not available, so the taper is assembled from four
	// quads by hand: the one place in this kit where a primitive does not already exist.

	if (P.TaperHeight > 0.0)
	{
		const double Z0 = StraightHeight;
		const double Z1 = P.CanopyHeight;

		const double DuctX0 = (P.Width - P.DuctWidth) * 0.5;
		const double DuctX1 = DuctX0 + P.DuctWidth;

		// The duct rises at the BACK of the canopy, against the wall, which is where a flue actually
		// runs - not up the middle of the hood.
		const double DuctY0 = P.BackY() - P.DuctDepth;
		const double DuctY1 = P.BackY();

		const FVector3d Bottom[4] = {
			FVector3d(0.0, 0.0, Z0), FVector3d(P.Width, 0.0, Z0),
			FVector3d(P.Width, P.BackY(), Z0), FVector3d(0.0, P.BackY(), Z0)
		};
		const FVector3d Top[4] = {
			FVector3d(DuctX0, DuctY0, Z1), FVector3d(DuctX1, DuctY0, Z1),
			FVector3d(DuctX1, DuctY1, Z1), FVector3d(DuctX0, DuctY1, Z1)
		};

		FDynamicMesh3 Taper;
		FHFMeshOps::InitialiseMesh(Taper);

		const int32 GroupId = FHFMeshOps::GroupForRole(EHFSurfaceRole::Appliance);

		for (int32 Side = 0; Side < 4; ++Side)
		{
			const int32 Next = (Side + 1) % 4;

			const int32 A = Taper.AppendVertex(Bottom[Side]);
			const int32 B = Taper.AppendVertex(Bottom[Next]);
			const int32 C = Taper.AppendVertex(Top[Next]);
			const int32 D = Taper.AppendVertex(Top[Side]);

			Taper.AppendTriangle(A, B, C, GroupId);
			Taper.AppendTriangle(A, C, D, GroupId);
		}

		// The top of the taper, closed where the duct is not.
		FHFMeshOps::AppendPreservingRoles(Out.Shell, Taper);
	}

	// -------------------------------------------------------------------------------------- the duct
	//
	// RESOLVED BY THE COMPOSING LAYER AND HANDED IN, never guessed here. A chimney that stops at the
	// top of its own canopy is a box on a wall, and one built to a fixed length in a room whose false
	// ceiling somebody deepened is a duct buried in plasterboard - the exact failure a ceiling fan's
	// rod already had once.

	if (P.DuctLength > 0.0 && P.DuctWidth > 0.0 && P.DuctDepth > 0.0)
	{
		FDynamicMesh3 Duct;
		FHFMeshOps::InitialiseMesh(Duct);

		FHFMeshOps::AppendBox(Duct,
			FVector3d(P.Width * 0.5, P.BackY() - P.DuctDepth * 0.5,
				P.CanopyHeight + P.DuctLength * 0.5),
			FVector3d(P.DuctWidth * 0.5, P.DuctDepth * 0.5, P.DuctLength * 0.5),
			0.0, EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Duct);
	}

	// ------------------------------------------------------------------------------- the baffle filter
	//
	// ITS OWN PART, HINGED ALONG ITS LOWER FRONT EDGE. A chimney's filter drops open for cleaning,
	// which is the only thing about a cooker hood that a person ever actually moves.

	// Inside the mouth: lifted off the bottom edge, held clear of the side walls, and standing behind
	// the front wall rather than in it.
	const double MouthHeight = (StraightHeight > 0.0 ? StraightHeight : P.CanopyHeight) - FilterLift;
	const double FilterHeight = FMath::Max(MouthHeight * 0.8, 0.0);
	const double FilterRun = P.Width - 2.0 * SkirtWallThickness;

	if (FilterHeight > 0.0 && FilterRun > 0.0)
	{
		FHFMeshPart Filter;
		Filter.PartId = FilterPartId();
		FHFMeshOps::InitialiseMesh(Filter.Mesh);

		const double PanelWidth = FilterRun / static_cast<double>(P.FilterPanels);

		for (int32 Panel = 0; Panel < P.FilterPanels; ++Panel)
		{
			// A small gap between panels, which is what makes a baffle read as a baffle rather than
			// as one flat plate.
			const double X0 = Panel * PanelWidth + 0.3;
			const double X1 = (Panel + 1) * PanelWidth - 0.3;

			if (X1 <= X0)
			{
				continue;
			}

			FHFMeshOps::AppendBox(Filter.Mesh,
				FVector3d((X0 + X1) * 0.5, FilterThickness * 0.5, FilterHeight * 0.5),
				FVector3d((X1 - X0) * 0.5, FilterThickness * 0.5, FilterHeight * 0.5),
				0.0, EHFSurfaceRole::MetalHardware);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Filter.Mesh);

		// Hinged on the line along the BOTTOM FRONT of the filter, so it drops away from the canopy
		// and hangs down in front of the hob - which is exactly what one does when it is released.
		// Behind the skirt's front wall and lifted clear of its bottom edge, so the panel is inside
		// the mouth rather than in the same place as the metal round it.
		Filter.PivotTransform = FTransform(
			FVector(SkirtWallThickness, SkirtWallThickness, FilterLift));

		Filter.Motion.Type = EHFMotionType::Hinge;
		Filter.Motion.Axis = FVector::XAxisVector;

		// POSITIVE, SO THE PANEL DROPS OUT OF THE HOOD AND NOT INTO IT. A rotation about +X carries
		// the top of the filter towards -Y, which is forward into the room, and down - which is what
		// a released baffle does and what makes it reachable. The other sign tipped the top backwards
		// into the canopy, through the wall behind it, and it looked plausible in the render until
		// the direction of travel was checked against where a hand would have to go.
		Filter.Motion.MaxAngleDegrees = P.FilterDropDegrees;
		Filter.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Filter));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The geyser.
//
// =============================================================================================

FHFGeyserParams FHFApplianceKit::SanitiseGeyser(const FHFGeyserParams& Params)
{
	FHFGeyserParams P = Params;

	P.Length = FMath::Max(P.Length, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.BracketThickness = FMath::Clamp(P.BracketThickness, 0.0, FMath::Max(P.Depth * 0.2, 0.0));
	P.DialRadius = FMath::Clamp(P.DialRadius, 0.0, FMath::Max(P.VesselDiameter() * 0.25, 0.0));
	P.DialSweepDegrees = FMath::Clamp(P.DialSweepDegrees, 0.0, 350.0);
	P.PipeRadius = FMath::Clamp(P.PipeRadius, 0.0, FMath::Max(P.VesselDiameter() * 0.08, 0.0));

	// THE PIPEWORK STAYS INSIDE THE DRAWN BOX. A real geyser's connections do hang below it, and they
	// are still clamped here, because the drawn envelope is what everything else in the house tests
	// against - the ceiling fit, the validator's clash rule and every clearance measured in the built
	// flat. A fitting whose geometry quietly leaves its own declared box makes all three of those
	// answer about something that is not there.
	const double PipeTopZ = P.Height * 0.5 - P.VesselDiameter() * 0.425;
	P.PipeDrop = FMath::Clamp(P.PipeDrop, 0.0, FMath::Max(PipeTopZ, 0.0));

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildGeyser(const FHFGeyserParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFGeyserParams P = SanitiseGeyser(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double Radius = P.VesselDiameter() * 0.5;

	// Centred in what the box has left once the wall bracket has had its share, and pushed BACK so
	// the vessel's own skin touches the plaster. A cylinder centred in the drawn box instead would
	// hang a bracket's thickness off the wall, on a fitting that is bolted to it.
	const double AxisZ = P.Height * 0.5;
	const double AxisY = P.Depth * 0.5 - P.BracketThickness - Radius;

	const FVector3d LeftEnd(-P.Length * 0.5, AxisY, AxisZ);

	// ------------------------------------------------------------------------------- the vessel
	//
	// A DOMED CYLINDER, not a capped one. A pressure vessel's ends are dished because a flat one
	// cannot hold pressure, and the dish is what the eye reads: a cylinder with flat discs on it
	// looks like a length of pipe, which is the wrong object at exactly this size.
	{
		const double Dish = FMath::Min(Radius * 0.45, P.Length * 0.22);
		const double Barrel = FMath::Max(P.Length - 2.0 * Dish, 0.0);

		const TArray<FVector2D> Profile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, Radius * 0.42),
			FVector2D(Dish * 0.32, Radius * 0.78),
			FVector2D(Dish * 0.72, Radius * 0.96),
			FVector2D(Dish, Radius),
			FVector2D(Dish + Barrel, Radius),
			FVector2D(Dish + Barrel + Dish * 0.28, Radius * 0.96),
			FVector2D(Dish + Barrel + Dish * 0.68, Radius * 0.78),
			FVector2D(P.Length, Radius * 0.42),
			FVector2D(P.Length, 0.0)
		};

		FDynamicMesh3 Vessel;
		FHFMeshOps::InitialiseMesh(Vessel);

		if (!FHFMeshOps::AppendRevolvedProfile(Vessel, Profile, LeftEnd, FVector3d::UnitX(),
			RevolveSides * 2, EHFSurfaceRole::Appliance))
		{
			return Out;
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Vessel);
	}

	// ------------------------------------------------------------------------- the wall bracket

	if (P.BracketThickness > 0.0)
	{
		FDynamicMesh3 Bracket;
		FHFMeshOps::InitialiseMesh(Bracket);

		// A strap plate across the back, narrower than the vessel and shorter than it, which is what
		// a geyser actually hangs on: two lugs on a spine.
		FHFMeshOps::AppendBox(Bracket,
			FVector3d(0.0, P.Depth * 0.5 - P.BracketThickness * 0.5, AxisZ),
			FVector3d(P.Length * 0.34, P.BracketThickness * 0.5, Radius * 0.72), 0.0,
			EHFSurfaceRole::MetalHardware);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Bracket);
	}

	// ------------------------------------------------------------------------------ the pipework
	//
	// Inlet and outlet, dropping out of the underside towards the wall. Short, because they run into
	// the plaster within a hand's width of the vessel - but present, because a geyser with no
	// connections at all is a drum on a wall.

	if (P.PipeRadius > 0.0 && P.PipeDrop > 0.0)
	{
		const double PipeXs[2] = { -P.Length * 0.28, P.Length * 0.28 };

		for (const double PipeX : PipeXs)
		{
			FDynamicMesh3 Pipe;
			FHFMeshOps::InitialiseMesh(Pipe);

			const TArray<FVector2D> Profile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, P.PipeRadius),
				FVector2D(P.PipeDrop, P.PipeRadius),
				FVector2D(P.PipeDrop, 0.0)
			};

			// Started inside the vessel's skin so the two solids meet rather than touching, which is
			// the difference between a joint and two faces in one plane.
			if (FHFMeshOps::AppendRevolvedProfile(Pipe, Profile,
				FVector3d(PipeX, AxisY, AxisZ - Radius * 0.85), -FVector3d::UnitZ(),
				RevolveSides, EHFSurfaceRole::MetalHardware))
			{
				FHFMeshOps::AppendPreservingRoles(Out.Shell, Pipe);
			}
		}
	}

	// --------------------------------------------------------------------- the thermostat dial
	//
	// ON THE END CAP, where a horizontal geyser's control is, turning about the vessel's own axis.
	// Small, and still a moving part: a person turns it, so it turns. See
	// .claude/rules/04-conventions.md.

	if (P.DialRadius > 0.0 && P.DialSweepDegrees > 0.0)
	{
		FHFMeshPart Dial;
		Dial.PartId = ThermostatPartId();
		FHFMeshOps::InitialiseMesh(Dial.Mesh);

		// Drawn about its own pivot: a knurled disc with a pointer standing off it, because a plain
		// disc gives nothing to see the rotation BY - a knob that turns invisibly is a knob that may
		// as well not have turned.
		const TArray<FVector2D> Body = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.DialRadius),
			FVector2D(1.4, P.DialRadius * 0.92),
			FVector2D(1.8, P.DialRadius * 0.7),
			FVector2D(1.8, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Dial.Mesh, Body, FVector3d::ZeroVector,
			-FVector3d::UnitX(), RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FDynamicMesh3 Pointer;
			FHFMeshOps::InitialiseMesh(Pointer);

			// A raised rib across the face, which is what a thermostat's index actually is.
			FHFMeshOps::AppendBox(Pointer, FVector3d(-1.9, 0.0, P.DialRadius * 0.45),
				FVector3d(0.35, P.DialRadius * 0.16, P.DialRadius * 0.45), 0.0,
				EHFSurfaceRole::MetalHardware);

			FHFMeshOps::AppendPreservingRoles(Dial.Mesh, Pointer);
			FHFMeshOps::ApplyWorldScaleUVs(Dial.Mesh);

			// Set into the dished end, not floating off it: the dish pulls back from the barrel, so
			// the dial sits at the length the profile actually reaches at the axis.
			Dial.PivotTransform = FTransform(FVector(-P.Length * 0.5 + 0.6, AxisY, AxisZ));
			Dial.Motion.Type = EHFMotionType::Hinge;
			Dial.Motion.Axis = FVector::XAxisVector;
			Dial.Motion.MaxAngleDegrees = P.DialSweepDegrees;
			Dial.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Dial));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The split air conditioner's indoor unit.
//
// =============================================================================================

FName FHFApplianceKit::DeflectorPartId(int32 Index)
{
	return FName(*FString::Printf(TEXT("Deflector%d"), FMath::Max(Index, 0)));
}

FHFSplitACParams FHFApplianceKit::SanitiseSplitAC(const FHFSplitACParams& Params)
{
	FHFSplitACParams P = Params;

	P.Length = FMath::Max(P.Length, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.LouvreOpenDegrees = FMath::Clamp(P.LouvreOpenDegrees, 0.0, 90.0);
	P.DeflectorSwingDegrees = FMath::Clamp(P.DeflectorSwingDegrees, 0.0, 60.0);
	P.DeflectorCount = FMath::Clamp(P.DeflectorCount, 0, 24);

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildSplitAC(const FHFSplitACParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFSplitACParams P = SanitiseSplitAC(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	// Half depth: the wall face is at +HalfY and the frontmost point of the casing at -HalfY.
	const double HalfY = P.Depth * 0.5;
	const double H = P.Height;

	// ------------------------------------------------------------------------------- the casing
	//
	// THE SECTION IS THE OBJECT. Every point below is a fraction of the drawn box, so a unit of any
	// size comes out the same shape - a flat back on the plaster, a top that runs forward and domes,
	// a front that bulges and tucks under, and the discharge channel cut up into the underside.
	//
	// The channel is a genuine CONCAVITY in the polygon rather than a line drawn on a box: the vane
	// has to lie in something, and a louvre stuck onto a flat soffit reads as a strip of tape.

	const TArray<FVector2D> Section = {
		FVector2D(HalfY, 0.0),				// back bottom, on the plaster
		FVector2D(HalfY, H),				// back top

		FVector2D(HalfY * 0.55, H * 0.995),	// the top, running forward and doming over
		FVector2D(HalfY * 0.05, H * 0.965),
		FVector2D(-HalfY * 0.40, H * 0.905),
		FVector2D(-HalfY * 0.72, H * 0.815),
		FVector2D(-HalfY * 0.94, H * 0.700),

		FVector2D(-HalfY, H * 0.530),		// the front, at its furthest out
		FVector2D(-HalfY * 0.965, H * 0.330),
		FVector2D(-HalfY * 0.820, H * 0.200),
		FVector2D(-HalfY * 0.680, H * 0.140),	// the front lip of the discharge

		FVector2D(-HalfY * 0.600, H * 0.250),	// up into the channel
		FVector2D(-HalfY * 0.140, H * 0.235),	// along its ceiling
		FVector2D(-HalfY * 0.090, H * 0.067),	// and down its back wall

		FVector2D(HalfY * 0.36, H * 0.017)	// the bottom pan, running back to the wall
	};

	// u is +Y and the sweep is +X, so the derived v axis is X cross Y = +Z: the section stands up and
	// the casing extrudes along the wall, which is exactly what a constant-profile moulding is.
	if (!FHFMeshOps::AppendExtrudedSection(Out.Shell, Section,
		FVector3d(-P.Length * 0.5, 0.0, 0.0), FVector3d::UnitY(), FVector3d::UnitX(),
		P.Length, EHFSurfaceRole::Appliance))
	{
		return Out;
	}

	// The geometry of the channel, read back off the section so the vane and the deflectors cannot
	// drift from the hole they live in.
	const double ChannelFrontY = -HalfY * 0.680;
	const double ChannelFrontZ = H * 0.140;
	const double ChannelBackY = -HalfY * 0.090;
	const double ChannelCeilingZ = H * 0.235;
	const double ChannelHingeZ = H * 0.085;
	const double ChannelReach = ChannelBackY - ChannelFrontY;
	const double ChannelMidY = (ChannelFrontY + ChannelBackY) * 0.5;

	// NO DISPLAY STRIP, and it is worth saying why rather than leaving a gap. One was built, as a dark
	// inset on the lower front - and the casing's front is a CURVE, so a box placed at a fixed offset
	// sat entirely inside it at one height and stood proud of it at another. Rendered, it was simply
	// not there. Chasing a flush 8 mm inset round a swept profile is work for a detail nobody can see
	// at 2.2 m, and the render shows the front reads perfectly well without one.

	// --------------------------------------------------------------------- what the vane will occupy
	//
	// Worked out BEFORE the deflectors, because the deflectors have to sit clear of it. See below.

	const double VaneThickness = FMath::Min(0.7, ChannelReach * 0.12);
	const double VaneReach = ChannelReach * 0.94;
	const double VaneRise = (ChannelFrontZ - ChannelHingeZ) * (VaneReach / ChannelReach);

	// The top of the shut vane at a point along the channel. It lies ALONG the mouth rather than
	// across it, so it is lowest at the hinge and highest at the lip.
	auto ShutVaneTopZ = [&](double AtY)
	{
		const double Fraction = FMath::Clamp((ChannelBackY - AtY) / FMath::Max(VaneReach, 0.01), 0.0, 1.0);
		return ChannelHingeZ + VaneThickness + VaneRise * Fraction;
	};

	// ------------------------------------------------------------------------- the vertical deflectors
	//
	// ONE PART PER FIN, AND EACH ON ITS OWN AXIS - which is what cost a rewrite and is worth stating.
	// A real set is ganged, so it is tempting to emit one part carrying all seven and turn it; but a
	// rigid rotation of the whole set about one axis SWINGS the outer fins sideways instead of turning
	// them, and at 30 degrees the end fin would leave the casing entirely. Ganged means they turn
	// TOGETHER, not that they turn about a shared centre. Each fin therefore pivots where its own pin
	// is, which is the only arrangement that stays inside a 65 mm channel.
	//
	// Every one of them still answers to one master open amount, which is the gang.

	if (P.DeflectorCount > 0 && ChannelReach > 0.0)
	{
		const double FinPitch = P.Length * 0.86 / static_cast<double>(P.DeflectorCount);

		// ------------------------------------------------ THEY LIVE IN THE THROAT, ABOVE THE VANE
		//
		// Set out across the whole channel and started just above the hinge, the fins ran STRAIGHT
		// THROUGH the shut vane along its entire length - the vane rises towards the lip and the fins
		// did not, so the two crossed. In the rendered flat the deflector ticks showed through a
		// closed louvre, which is a discharge you can see into with the machine off.
		//
		// Both halves of the fix come from where the parts actually are on a real unit: the vertical
		// deflectors sit BACK in the throat, not at the mouth, and they hang ABOVE the horizontal
		// vane rather than beside it. So the fins are pulled towards the hinge, where the vane is
		// low, and their underside is measured off the vane's own shut position at their front edge.
		const double FinReach = ChannelReach * 0.45;
		const double FinCentreY = ChannelBackY - FinReach * 0.5 - ChannelReach * 0.05;
		const double FinFrontY = FinCentreY - FinReach * 0.5;

		const double FinBottomZ = ShutVaneTopZ(FinFrontY) + 0.25;
		const double FinHeight = FMath::Max(ChannelCeilingZ - FinBottomZ - 0.3, 0.2);

		for (int32 Fin = 0; Fin < P.DeflectorCount; ++Fin)
		{
			FHFMeshPart Deflector;
			Deflector.PartId = DeflectorPartId(Fin);
			FHFMeshOps::InitialiseMesh(Deflector.Mesh);

			// Drawn about its own pin, which is the vertical through the middle of the blade.
			FHFMeshOps::AppendBox(Deflector.Mesh,
				FVector3d(0.0, 0.0, FinHeight * 0.5),
				FVector3d(0.12, FinReach * 0.5, FinHeight * 0.5), 0.0, EHFSurfaceRole::Appliance);

			FHFMeshOps::ApplyWorldScaleUVs(Deflector.Mesh);

			const double FinX = -P.Length * 0.43 + (static_cast<double>(Fin) + 0.5) * FinPitch;

			Deflector.PivotTransform = FTransform(FVector(FinX, FinCentreY, FinBottomZ));

			Deflector.Motion.Type = EHFMotionType::Hinge;
			Deflector.Motion.Axis = FVector::ZAxisVector;
			Deflector.Motion.MaxAngleDegrees = P.DeflectorSwingDegrees;
			Deflector.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Deflector));
		}
	}

	// ---------------------------------------------------------------------------- the discharge vane
	//
	// Hinged on its REAR axis, so its tip swings down AND BACK - which is the arc a real one traces
	// and the reason an opening louvre appears to withdraw as it falls. Hinging it at the front lip
	// instead would throw the tip forwards out of the casing, past the drawn box, and into the room.

	if (ChannelReach > 0.0 && P.LouvreOpenDegrees > 0.0)
	{
		FHFMeshPart Louvre;
		Louvre.PartId = LouvrePartId();
		FHFMeshOps::InitialiseMesh(Louvre.Mesh);

		// SHUT MEANS SHUT. VaneRise, computed above, makes the vane lie ALONG the mouth line rather
		// than horizontally across it, so its tip arrives AT the front lip: the channel's rear is at
		// the hinge and its lip is higher and further forward, and a vane drawn flat leaves a 20 mm
		// slot open at the front with the unit switched off. The rise is read off the section rather
		// than chosen, so the two cannot drift - and the deflectors are set out against the same
		// figure, which is what keeps them out of it.

		// A shallow scoop rather than a flat plate: it catches a highlight along its length instead of
		// going out as one dead grey band.
		const double Scoop = VaneReach * 0.05;

		const TArray<FVector2D> VaneSection = {
			FVector2D(0.0, 0.0),
			FVector2D(-VaneReach * 0.5, VaneRise * 0.5 - Scoop),
			FVector2D(-VaneReach, VaneRise),
			FVector2D(-VaneReach, VaneRise + VaneThickness),
			FVector2D(-VaneReach * 0.5, VaneRise * 0.5 - Scoop + VaneThickness),
			FVector2D(0.0, VaneThickness)
		};

		if (FHFMeshOps::AppendExtrudedSection(Louvre.Mesh, VaneSection,
			FVector3d(-P.Length * 0.46, 0.0, 0.0), FVector3d::UnitY(), FVector3d::UnitX(),
			P.Length * 0.92, EHFSurfaceRole::Appliance))
		{
			FHFMeshOps::ApplyWorldScaleUVs(Louvre.Mesh);

			Louvre.PivotTransform = FTransform(FVector(0.0, ChannelBackY, ChannelHingeZ));

			Louvre.Motion.Type = EHFMotionType::Hinge;
			Louvre.Motion.Axis = FVector::XAxisVector;

			// POSITIVE, SO THE TIP DROPS. A rotation about +X carries the vane's forward-reaching tip
			// downwards; the other sign lifts it up into the channel's own ceiling, which measures as
			// motion and is a vane closing harder than shut.
			Louvre.Motion.MaxAngleDegrees = P.LouvreOpenDegrees;
			Louvre.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Louvre));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The condensing unit.
//
// =============================================================================================

FHFCondenserParams FHFApplianceKit::SanitiseCondenser(const FHFCondenserParams& Params)
{
	FHFCondenserParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.FootHeight = FMath::Clamp(P.FootHeight, 0.0, P.Height * 0.25);
	P.BladeCount = FMath::Clamp(P.BladeCount, 2, 8);
	P.CoilSlats = FMath::Clamp(P.CoilSlats, 0, 40);

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildCondenser(const FHFCondenserParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFCondenserParams P = SanitiseCondenser(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	// Front-left corner of the footprint on the floor: X runs 0..Width along the wall, Y runs 0 at
	// the front to Depth at the wall, Z up from the floor.
	const double CaseBottomZ = P.FootHeight;
	const double CaseHeight = P.Height - P.FootHeight;
	const double CaseCentreZ = CaseBottomZ + CaseHeight * 0.5;
	const double PanelThickness = FMath::Min(0.8, P.Depth * 0.05);

	// How far the coil slats stand in front of the recessed end panels. Their whole reading is the
	// shadow between them, so the recess has to be deeper than the slats are thick.
	const double CoilSlatDepth = P.CoilSlats > 0 ? PanelThickness * 1.1 : 0.0;

	const double FanRadius = P.FanRadius();
	const FVector2D FanCentre(P.Width * 0.5, CaseBottomZ + CaseHeight * 0.55);

	// ------------------------------------------------------------------------------ the front panel
	//
	// A REAL HOLE FOR THE FAN, triangulated with the panel rather than cut out of it. The aperture is
	// the whole front of the object: a condenser is a box with a big circle in it, and a fan guard
	// stuck onto a solid panel is a wheel painted on a crate.

	{
		TArray<TArray<FVector2D>> Holes;
		Holes.Add(HoleRing(FanCentre, FanRadius, RevolveSides));

		FDynamicMesh3 Front;
		FHFMeshOps::InitialiseMesh(Front);

		const TArray<FVector2D> Outer = {
			FVector2D(0.0, CaseBottomZ),
			FVector2D(P.Width, CaseBottomZ),
			FVector2D(P.Width, P.Height),
			FVector2D(0.0, P.Height)
		};

		if (FHFMeshOps::AppendPrismWithHoles(Front, Outer, Holes, 0.0, PanelThickness,
			EHFSurfaceRole::Appliance))
		{
			StandPanelUp(Front, PanelThickness);
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Front);
		}
	}

	// --------------------------------------------------------------------- the case round the panel
	//
	// Back, two ends and a lid, leaving the front open where the panel already is. Hollow, because the
	// fan turns INSIDE it and a solid block would leave the aperture looking at plastic.

	{
		FDynamicMesh3 Case;
		FHFMeshOps::InitialiseMesh(Case);

		FHFMeshOps::AppendBox(Case,
			FVector3d(P.Width * 0.5, P.Depth - PanelThickness * 0.5, CaseCentreZ),
			FVector3d(P.Width * 0.5, PanelThickness * 0.5, CaseHeight * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Case,
			FVector3d(P.Width * 0.5, P.Depth * 0.5, P.Height - PanelThickness * 0.5),
			FVector3d(P.Width * 0.5, P.Depth * 0.5, PanelThickness * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		// THE ENDS ARE SET BACK BY THE DEPTH OF THE COIL SLATS IN FRONT OF THEM. Built flush with the
		// drawn face they were the outermost thing on the unit, and the slats - which have to be
		// inside the drawn width - ended up entirely BURIED in them. Eleven per side, invisible, in
		// the rendered flat: the unit came out as a plain box with a fan in it, and nothing measured
		// it because a buried solid is still closed, still positive, and still inside the box.
		for (const double Side : { 0.0, 1.0 })
		{
			const double PanelCentreX = Side > 0.5
				? P.Width - CoilSlatDepth - PanelThickness * 0.5
				: CoilSlatDepth + PanelThickness * 0.5;

			FHFMeshOps::AppendBox(Case,
				FVector3d(PanelCentreX, P.Depth * 0.5, CaseCentreZ),
				FVector3d(PanelThickness * 0.5, P.Depth * 0.5, CaseHeight * 0.5), 0.0,
				EHFSurfaceRole::Appliance);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Case);
	}

	// ---------------------------------------------------------------------------------- the coil
	//
	// Horizontal slats over each end, which is where the coil actually shows on a domestic unit. The
	// back is not slatted: it faces the parapet and nothing ever sees it, and fifty boxes nobody can
	// look at is a triangle budget spent on nothing.

	if (P.CoilSlats > 0)
	{
		FDynamicMesh3 Coil;
		FHFMeshOps::InitialiseMesh(Coil);

		// Nearly the whole height, so the louvre reads as the end of the unit rather than as a band
		// across the middle of a blank panel.
		const double SlatSpan = CaseHeight * 0.90;
		const double SlatPitch = SlatSpan / static_cast<double>(P.CoilSlats);
		const double SlatThickness = FMath::Min(SlatPitch * 0.55, 0.6);

		for (int32 Slat = 0; Slat < P.CoilSlats; ++Slat)
		{
			const double SlatZ = CaseBottomZ + CaseHeight * 0.05
				+ (static_cast<double>(Slat) + 0.5) * SlatPitch;

			for (const double Side : { 0.0, 1.0 })
			{
				// FROM THE DRAWN FACE BACK TO THE RECESSED PANEL, and not a millimetre past either.
				// The slats are what the eye sees of the end of the unit; the panel behind them is
				// what makes each of them cast a line.
				const double SlatCentreX = Side > 0.5
					? P.Width - CoilSlatDepth * 0.5
					: CoilSlatDepth * 0.5;

				FHFMeshOps::AppendBox(Coil,
					FVector3d(SlatCentreX, P.Depth * 0.52, SlatZ),
					FVector3d(CoilSlatDepth * 0.5, P.Depth * 0.40, SlatThickness * 0.5), 0.0,
					EHFSurfaceRole::MetalHardware);
			}
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Coil);
	}

	// ----------------------------------------------------------------------------------- the feet
	//
	// Two rails under the case. A condenser standing dead on the floor slab reads as a box that fell
	// off a lorry; the gap under it is what says it was bolted down.

	if (P.FootHeight > 0.0)
	{
		FDynamicMesh3 Feet;
		FHFMeshOps::InitialiseMesh(Feet);

		for (const double Along : { 0.22, 0.78 })
		{
			FHFMeshOps::AppendBox(Feet,
				FVector3d(P.Width * Along, P.Depth * 0.5, P.FootHeight * 0.5),
				FVector3d(P.Width * 0.09, P.Depth * 0.42, P.FootHeight * 0.5), 0.0,
				EHFSurfaceRole::MetalHardware);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Feet);
	}

	// ------------------------------------------------------------------------------ the fan guard
	//
	// Concentric rings and radial spokes over the aperture. It is what a person actually sees of the
	// fan - the blades are behind it and half in shadow - and it is the single detail that separates a
	// condensing unit from a grey box.

	if (FanRadius > 0.0)
	{
		const double GuardRadius = FanRadius * 1.06;
		const double GuardThickness = FMath::Max(PanelThickness * 0.45, 0.2);

		// Two bands of slots with a rib between them and between each pair, which is what a pressed
		// guard is: mostly open, with just enough metal to keep a hand out.
		TArray<TArray<FVector2D>> Slots;

		const FVector2D Origin2D(FanCentre.X, FanCentre.Y);

		for (int32 Slot = 0; Slot < 10; ++Slot)
		{
			Slots.Add(AnnularSector(Origin2D, FanRadius * 0.60, FanRadius * 0.93,
				36.0 * static_cast<double>(Slot) + 4.0, 28.0, 5));
		}

		for (int32 Slot = 0; Slot < 6; ++Slot)
		{
			Slots.Add(AnnularSector(Origin2D, FanRadius * 0.22, FanRadius * 0.52,
				60.0 * static_cast<double>(Slot) + 7.0, 46.0, 5));
		}

		FDynamicMesh3 Guard;
		FHFMeshOps::InitialiseMesh(Guard);

		if (FHFMeshOps::AppendPrismWithHoles(Guard, HoleRing(Origin2D, GuardRadius, RevolveSides * 2),
			Slots, 0.0, GuardThickness, EHFSurfaceRole::MetalHardware))
		{
			StandPanelUp(Guard, GuardThickness);

			// Proud of the front panel, so the guard casts a shadow into the slots rather than lying
			// in the same plane as the sheet behind it.
			MeshTransforms::Translate(Guard, FVector3d(0.0, -GuardThickness * 0.6, 0.0));

			FHFMeshOps::AppendPreservingRoles(Out.Shell, Guard);
		}
	}

	// ------------------------------------------------------------------------------------ the fan
	//
	// ITS OWN PART, AND IT SPINS. EHFMotionType::Spin and EHFPartCollision::TraceOnly, exactly as a
	// ceiling fan's rotor: collision geometry does not turn with the render, so a blocking rotor is a
	// blade frozen at whatever azimuth the level was saved at.

	if (FanRadius > 0.0)
	{
		FHFMeshPart Fan;
		Fan.PartId = CondenserFanPartId();
		FHFMeshOps::InitialiseMesh(Fan.Mesh);

		const double HubRadius = FanRadius * 0.20;
		const double BladeRoot = HubRadius * 0.85;
		const double BladeSpan = FanRadius * 0.90 - BladeRoot;

		const TArray<FVector2D> Hub = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, HubRadius),
			FVector2D(HubRadius * 0.8, HubRadius * 0.75),
			FVector2D(HubRadius * 0.9, 0.0)
		};

		FHFMeshOps::AppendRevolvedProfile(Fan.Mesh, Hub, FVector3d::Zero(), -FVector3d::UnitY(),
			RevolveSides, EHFSurfaceRole::MetalHardware);

		if (BladeSpan > 0.0)
		{
			for (int32 Blade = 0; Blade < P.BladeCount; ++Blade)
			{
				FDynamicMesh3 Vane;
				FHFMeshOps::InitialiseMesh(Vane);

				FHFMeshOps::AppendBox(Vane,
					FVector3d(BladeRoot + BladeSpan * 0.5, 0.0, 0.0),
					FVector3d(BladeSpan * 0.5, 0.12, FanRadius * 0.30), 0.0,
					EHFSurfaceRole::Appliance);

				// PITCHED ABOUT ITS OWN RADIUS FIRST, then swung to its station. A flat blade is a
				// paddle: it moves air in neither direction and reads as one the moment it is lit.
				RotateAboutOrigin(Vane, FVector3d::UnitX(), 28.0);
				RotateAboutOrigin(Vane, FVector3d::UnitY(),
					360.0 * static_cast<double>(Blade) / static_cast<double>(P.BladeCount));

				FHFMeshOps::AppendPreservingRoles(Fan.Mesh, Vane);
			}
		}

		FHFMeshOps::ApplyWorldScaleUVs(Fan.Mesh);

		Fan.PivotTransform = FTransform(
			FVector(FanCentre.X, PanelThickness + FanRadius * 0.34, FanCentre.Y));

		Fan.Motion.Type = EHFMotionType::Spin;
		Fan.Motion.Axis = FVector::YAxisVector;
		Fan.Motion.RevolutionsPerMinute = P.FanRevolutionsPerMinute;
		Fan.Collision = EHFPartCollision::TraceOnly;

		// A quarter turn of offset so two units side by side are not the same object twice.
		Fan.DefaultSpinTurns = 0.17;

		Out.Parts.Add(MoveTemp(Fan));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The refrigerator.
//
// =============================================================================================

FName FHFApplianceKit::FridgeDoorPartId(int32 Index)
{
	return FName(*FString::Printf(TEXT("Door%d"), FMath::Max(Index, 0)));
}

FHFRefrigeratorParams FHFApplianceKit::SanitiseRefrigerator(const FHFRefrigeratorParams& Params)
{
	FHFRefrigeratorParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.SkirtingSetback = FMath::Clamp(P.SkirtingSetback, 0.0, P.Depth * 0.25);
	P.DoorThickness = FMath::Clamp(P.DoorThickness, 0.0, P.BuiltDepth() * 0.35);
	P.PlinthHeight = FMath::Clamp(P.PlinthHeight, 0.0, P.Height * 0.25);
	P.FreezerFraction = FMath::Clamp(P.FreezerFraction, 0.1, 0.9);
	P.DoorSwingDegrees = FMath::Clamp(P.DoorSwingDegrees, 0.0, 170.0);

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildRefrigerator(const FHFRefrigeratorParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFRefrigeratorParams P = SanitiseRefrigerator(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double BodyDepth = P.BuiltDepth();

	// The carcass stops where the doors begin: a cabinet built to the full depth with doors hung on
	// the front of it is 65 mm deeper than the drawn box in every direction anybody can measure.
	const double CarcassFrontY = P.DoorThickness;
	const double CarcassDepth = FMath::Max(BodyDepth - P.DoorThickness, 0.01);

	const double CabinetBottomZ = P.PlinthHeight;
	const double CabinetHeight = P.Height - P.PlinthHeight;
	const double FreezerHeight = CabinetHeight * P.FreezerFraction;

	// -------------------------------------------------------------------------------- the carcass

	{
		FDynamicMesh3 Carcass;
		FHFMeshOps::InitialiseMesh(Carcass);

		FHFMeshOps::AppendBox(Carcass,
			FVector3d(P.Width * 0.5, CarcassFrontY + CarcassDepth * 0.5,
				CabinetBottomZ + CabinetHeight * 0.5),
			FVector3d(P.Width * 0.5, CarcassDepth * 0.5, CabinetHeight * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Carcass);
	}

	// --------------------------------------------------------------------------- the plinth grille
	//
	// Held in on all sides so the cabinet appears to stand on a recessed base, and slatted, because
	// that is what the condenser behind it breathes through. A solid block would be a fridge sitting
	// on the floor, which is the one thing no refrigerator does.

	if (P.PlinthHeight > 0.0)
	{
		FDynamicMesh3 Plinth;
		FHFMeshOps::InitialiseMesh(Plinth);

		const double Recess = FMath::Min(2.0, P.PlinthHeight * 0.3);

		FHFMeshOps::AppendBox(Plinth,
			FVector3d(P.Width * 0.5, CarcassFrontY + Recess + (BodyDepth - CarcassFrontY - Recess) * 0.5,
				P.PlinthHeight * 0.5),
			FVector3d(P.Width * 0.5 - Recess * 0.5,
				FMath::Max((BodyDepth - CarcassFrontY - Recess) * 0.5, 0.01), P.PlinthHeight * 0.5),
			0.0, EHFSurfaceRole::MetalHardware);

		// The vented fascia, standing in front of the recess so the shadow gap still reads.
		FHFMeshOps::AppendBox(Plinth,
			FVector3d(P.Width * 0.5, CarcassFrontY + Recess * 0.5, P.PlinthHeight * 0.5),
			FVector3d(P.Width * 0.5 - Recess * 0.5, Recess * 0.5, P.PlinthHeight * 0.35), 0.0,
			EHFSurfaceRole::MetalHardware);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Plinth);
	}

	// ---------------------------------------------------------------------------------- the doors
	//
	// BOTH HUNG ON THE SAME SIDE, which is what a two-door refrigerator is: nobody opens a freezer
	// left-handed and the compartment under it right-handed. Hung on the RIGHT stile, so the free edge
	// is on the left and both leaves swing the same way.

	const double Shadow = FMath::Min(0.4, CabinetHeight * 0.004);

	struct FDoorPlan
	{
		double BottomZ = 0.0;
		double Height = 0.0;
	};

	const FDoorPlan Doors[2] = {
		{ CabinetBottomZ + CabinetHeight - FreezerHeight, FreezerHeight - Shadow },
		{ CabinetBottomZ, CabinetHeight - FreezerHeight - Shadow }
	};

	for (int32 Index = 0; Index < 2; ++Index)
	{
		const FDoorPlan& Plan = Doors[Index];

		if (Plan.Height <= 0.0 || P.DoorThickness <= 0.0)
		{
			continue;
		}

		FHFMeshPart Door;
		Door.PartId = FridgeDoorPartId(Index);
		FHFMeshOps::InitialiseMesh(Door.Mesh);

		// Drawn from its own hinge, which is the RIGHT stile: local X runs 0 back to -Width, so a
		// positive rotation about +Z carries the free edge out of the cabinet and into the room.
		FHFMeshOps::AppendBox(Door.Mesh,
			FVector3d(-P.Width * 0.5, P.DoorThickness * 0.5, Plan.Height * 0.5),
			FVector3d(P.Width * 0.5, P.DoorThickness * 0.5, Plan.Height * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		// A vertical bar handle on the free edge. It is the only thing on the front of a refrigerator
		// that is not flat, and without it the appliance is a white slab.
		const double HandleRadius = FMath::Min(1.0, P.DoorThickness * 0.22);
		const double HandleStand = HandleRadius * 2.6;
		const double HandleLength = FMath::Min(Plan.Height * 0.62, 60.0);

		{
			FDynamicMesh3 Handle;
			FHFMeshOps::InitialiseMesh(Handle);

			const TArray<FVector2D> Bar = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, HandleRadius),
				FVector2D(HandleLength, HandleRadius),
				FVector2D(HandleLength, 0.0)
			};

			FHFMeshOps::AppendRevolvedProfile(Handle, Bar,
				FVector3d(-P.Width + HandleStand * 1.4, -HandleStand,
					Plan.Height * 0.5 - HandleLength * 0.5),
				FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);

			// Two stub brackets back to the leaf, so the bar stands off it rather than lying on it.
			for (const double End : { 0.18, 0.82 })
			{
				FHFMeshOps::AppendBox(Handle,
					FVector3d(-P.Width + HandleStand * 1.4, -HandleStand * 0.5,
						Plan.Height * 0.5 - HandleLength * 0.5 + HandleLength * End),
					FVector3d(HandleRadius * 0.8, HandleStand * 0.5, HandleRadius * 0.8), 0.0,
					EHFSurfaceRole::MetalHardware);
			}

			FHFMeshOps::AppendPreservingRoles(Door.Mesh, Handle);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Door.Mesh);

		Door.PivotTransform = FTransform(FVector(P.Width, 0.0, Plan.BottomZ));

		Door.Motion.Type = EHFMotionType::Hinge;
		Door.Motion.Axis = FVector::ZAxisVector;

		// POSITIVE, SO THE FREE EDGE COMES FORWARD OUT OF THE ROOM SIDE. The leaf is drawn along -X
		// from its hinge, and a rotation about +Z carries -X towards -Y, which is out of the cabinet
		// and into the kitchen. The other sign swings both doors straight into the wall behind them,
		// which is a swept transform that travels exactly as far and opens nothing - so the sign is
		// checked as a swept point rather than reasoned about. See the tests.
		Door.Motion.MaxAngleDegrees = P.DoorSwingDegrees;
		Door.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Door));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The washing machine.
//
// =============================================================================================

FHFWashingMachineParams FHFApplianceKit::SanitiseWashingMachine(const FHFWashingMachineParams& Params)
{
	FHFWashingMachineParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.SkirtingSetback = FMath::Clamp(P.SkirtingSetback, 0.0, P.Depth * 0.25);
	P.FasciaHeight = FMath::Clamp(P.FasciaHeight, 0.0, P.Height * 0.35);

	// The porthole has to fit between the fascia and the bottom of the case, with its own rim.
	const double Below = FMath::Max(P.Height - P.FasciaHeight, 0.0);
	P.PortholeDiameter = FMath::Clamp(P.PortholeDiameter, 0.0,
		FMath::Min(P.Width * 0.72, Below * 0.82));
	P.PortholeCentreZ = FMath::Clamp(P.PortholeCentreZ, P.PortholeDiameter * 0.6,
		FMath::Max(Below - P.PortholeDiameter * 0.6, P.PortholeDiameter * 0.6));

	P.DoorSwingDegrees = FMath::Clamp(P.DoorSwingDegrees, 0.0, 170.0);
	P.DrawerTravel = FMath::Clamp(P.DrawerTravel, 0.0, P.BuiltDepth() * 0.45);
	P.DialRadius = FMath::Clamp(P.DialRadius, 0.0, FMath::Max(P.FasciaHeight * 0.42, 0.0));
	P.DialSweepDegrees = FMath::Clamp(P.DialSweepDegrees, 0.0, 350.0);

	return P;
}

FHFApplianceBuild FHFApplianceKit::BuildWashingMachine(const FHFWashingMachineParams& Params)
{
	FHFApplianceBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFWashingMachineParams P = SanitiseWashingMachine(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double BodyDepth = P.BuiltDepth();
	const double FrontThickness = FMath::Min(1.2, BodyDepth * 0.06);
	const double PortholeRadius = P.PortholeDiameter * 0.5;

	const FVector2D PortholeCentre(P.Width * 0.5, P.PortholeCentreZ);

	// ----------------------------------------------------------------------------- the front panel
	//
	// A REAL HOLE FOR THE PORTHOLE, triangulated with the panel. The drum opening is the whole face of
	// a front loader; a glass disc laid on a flat panel has no depth behind it and reads as a sticker.

	{
		TArray<TArray<FVector2D>> Holes;
		Holes.Add(HoleRing(PortholeCentre, PortholeRadius, CloseUpSides));

		const TArray<FVector2D> Outer = {
			FVector2D(0.0, 0.0),
			FVector2D(P.Width, 0.0),
			FVector2D(P.Width, P.Height),
			FVector2D(0.0, P.Height)
		};

		FDynamicMesh3 Front;
		FHFMeshOps::InitialiseMesh(Front);

		if (FHFMeshOps::AppendPrismWithHoles(Front, Outer, Holes, 0.0, FrontThickness,
			EHFSurfaceRole::Appliance))
		{
			StandPanelUp(Front, FrontThickness);
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Front);
		}
	}

	// ---------------------------------------------------------------------------------- the case

	{
		FDynamicMesh3 Case;
		FHFMeshOps::InitialiseMesh(Case);

		FHFMeshOps::AppendBox(Case,
			FVector3d(P.Width * 0.5, FrontThickness + (BodyDepth - FrontThickness) * 0.5, P.Height * 0.5),
			FVector3d(P.Width * 0.5, FMath::Max((BodyDepth - FrontThickness) * 0.5, 0.01),
				P.Height * 0.5), 0.0, EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Case);
	}

	// --------------------------------------------------------------------------------- the fascia
	//
	// The control panel, held proud of the front so it reads as a separate pressing, with the drawer
	// aperture in one end of it and the dial in the other.

	const double FasciaBottomZ = P.Height - P.FasciaHeight;
	const double FasciaStand = FMath::Min(0.8, FrontThickness);
	const double DrawerWidth = FMath::Min(P.Width * 0.34, 22.0);
	const double DrawerHeight = FMath::Max(P.FasciaHeight * 0.62, 0.1);
	const double DrawerCentreX = DrawerWidth * 0.5 + P.Width * 0.05;
	const double DrawerCentreZ = FasciaBottomZ + P.FasciaHeight * 0.5;

	{
		FDynamicMesh3 Fascia;
		FHFMeshOps::InitialiseMesh(Fascia);

		// Built as a frame round the drawer aperture, so the drawer runs INTO something.
		const double ApertureLeft = DrawerCentreX - DrawerWidth * 0.5;
		const double ApertureRight = DrawerCentreX + DrawerWidth * 0.5;
		const double ApertureBottom = DrawerCentreZ - DrawerHeight * 0.5;
		const double ApertureTop = DrawerCentreZ + DrawerHeight * 0.5;

		FHFMeshOps::AppendBox(Fascia,
			FVector3d((ApertureRight + P.Width) * 0.5, -FasciaStand * 0.5,
				FasciaBottomZ + P.FasciaHeight * 0.5),
			FVector3d(FMath::Max((P.Width - ApertureRight) * 0.5, 0.01), FasciaStand * 0.5,
				P.FasciaHeight * 0.5), 0.0, EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Fascia,
			FVector3d(ApertureLeft * 0.5, -FasciaStand * 0.5, FasciaBottomZ + P.FasciaHeight * 0.5),
			FVector3d(FMath::Max(ApertureLeft * 0.5, 0.01), FasciaStand * 0.5, P.FasciaHeight * 0.5),
			0.0, EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Fascia,
			FVector3d(DrawerCentreX, -FasciaStand * 0.5, (ApertureTop + P.Height) * 0.5),
			FVector3d(DrawerWidth * 0.5, FasciaStand * 0.5,
				FMath::Max((P.Height - ApertureTop) * 0.5, 0.01)), 0.0, EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Fascia,
			FVector3d(DrawerCentreX, -FasciaStand * 0.5, (ApertureBottom + FasciaBottomZ) * 0.5),
			FVector3d(DrawerWidth * 0.5, FasciaStand * 0.5,
				FMath::Max((ApertureBottom - FasciaBottomZ) * 0.5, 0.01)), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Fascia);
	}

	// ----------------------------------------------------------------------------- the door seal
	//
	// The rubber boot round the opening and the drum behind it. Without them the porthole is a hole
	// through to nothing - which is exactly what a hole in a panel is, and it reads as a missing part
	// rather than as a machine.
	//
	// The boot is an ANNULUS built by perforating a disc, not by revolving a ring: see AnnularSector
	// for why a profile that never touches the axis cannot make one.

	if (PortholeRadius > 0.0)
	{
		const double BootThickness = FMath::Max(FrontThickness * 1.4, 0.6);

		TArray<TArray<FVector2D>> Bore;
		Bore.Add(HoleRing(PortholeCentre, PortholeRadius * 0.86, CloseUpSides));

		FDynamicMesh3 Boot;
		FHFMeshOps::InitialiseMesh(Boot);

		if (FHFMeshOps::AppendPrismWithHoles(Boot,
			HoleRing(PortholeCentre, PortholeRadius * 1.03, CloseUpSides), Bore,
			0.0, BootThickness, EHFSurfaceRole::MetalHardware))
		{
			StandPanelUp(Boot, BootThickness);
			MeshTransforms::Translate(Boot, FVector3d(0.0, FrontThickness * 0.8, 0.0));

			FHFMeshOps::AppendPreservingRoles(Out.Shell, Boot);
		}

		// The back of the drum, a hand's depth in. Solid, because both ends of its profile reach the
		// axis - which is the shape this primitive is for, and the reason it works here and not for
		// the boot.
		const double DrumDepth = FMath::Min(BodyDepth * 0.35, 22.0);

		FDynamicMesh3 Drum;
		FHFMeshOps::InitialiseMesh(Drum);

		const TArray<FVector2D> Back = {
			FVector2D(DrumDepth, 0.0),
			FVector2D(DrumDepth, PortholeRadius * 0.88),
			FVector2D(DrumDepth + 3.0, PortholeRadius * 0.88),
			FVector2D(DrumDepth + 3.0, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Drum, Back,
			FVector3d(PortholeCentre.X, FrontThickness, PortholeCentre.Y),
			FVector3d::UnitY(), CloseUpSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Drum);
		}
	}

	// -------------------------------------------------------------------------------- the porthole
	//
	// Hinged on the LEFT, which is where a front loader's door hangs, and glazed - the drum behind the
	// glass is the only interesting thing on the face of the machine.

	if (PortholeRadius > 0.0 && P.DoorSwingDegrees > 0.0)
	{
		FHFMeshPart Porthole;
		Porthole.PartId = PortholePartId();
		FHFMeshOps::InitialiseMesh(Porthole.Mesh);

		const double RimRadius = PortholeRadius * 1.14;
		const double RimDepth = FMath::Max(FrontThickness * 1.6, 0.6);

		// Drawn about its own hinge on the left of the aperture: the door's centre is out at +X.
		const FVector2D LocalCentre(RimRadius, 0.0);

		// The bezel, as an ANNULUS - see AnnularSector. A revolved ring would come out a solid disc
		// and would hide the glass it is supposed to frame, which is a failure that looks perfectly
		// correct from behind and from every wireframe.
		TArray<TArray<FVector2D>> Aperture;
		Aperture.Add(HoleRing(LocalCentre, PortholeRadius * 0.80, CloseUpSides));

		bool bRimBuilt = false;

		{
			FDynamicMesh3 Rim;
			FHFMeshOps::InitialiseMesh(Rim);

			if (FHFMeshOps::AppendPrismWithHoles(Rim, HoleRing(LocalCentre, RimRadius, CloseUpSides),
				Aperture, 0.0, RimDepth, EHFSurfaceRole::Appliance))
			{
				StandPanelUp(Rim, RimDepth);
				MeshTransforms::Translate(Rim, FVector3d(0.0, -RimDepth, 0.0));

				FHFMeshOps::AppendPreservingRoles(Porthole.Mesh, Rim);
				bRimBuilt = true;
			}
		}

		if (bRimBuilt)
		{
			// The glass, DISHED rather than flat. A front loader's door bulges into the drum, and that
			// curve is the only thing on the whole machine giving a specular highlight worth having.
			// Both ends of the profile reach the axis, so this one really is a solid of revolution.
			const TArray<FVector2D> Glass = {
				FVector2D(-RimDepth, 0.0),
				FVector2D(-RimDepth, PortholeRadius * 0.84),
				FVector2D(0.0, PortholeRadius * 0.84),
				FVector2D(RimDepth * 1.4, PortholeRadius * 0.62),
				FVector2D(RimDepth * 2.4, PortholeRadius * 0.30),
				FVector2D(RimDepth * 2.8, 0.0)
			};

			FDynamicMesh3 Pane;
			FHFMeshOps::InitialiseMesh(Pane);

			if (FHFMeshOps::AppendRevolvedProfile(Pane, Glass,
				FVector3d(LocalCentre.X, 0.0, 0.0), FVector3d::UnitY(), CloseUpSides,
				EHFSurfaceRole::Glass))
			{
				FHFMeshOps::AppendPreservingRoles(Porthole.Mesh, Pane);
			}

			FHFMeshOps::ApplyWorldScaleUVs(Porthole.Mesh);

			Porthole.PivotTransform = FTransform(
				FVector(PortholeCentre.X - RimRadius, 0.0, PortholeCentre.Y));

			Porthole.Motion.Type = EHFMotionType::Hinge;
			Porthole.Motion.Axis = FVector::ZAxisVector;

			// NEGATIVE, SO THE FREE EDGE COMES OUT AND ROUND TO THE LEFT. The leaf is drawn along +X
			// from its hinge, and a rotation about +Z carries +X towards +Y - which is INTO the drum.
			// The sign is checked as a swept transform rather than reasoned about; see the tests.
			Porthole.Motion.MaxAngleDegrees = -P.DoorSwingDegrees;
			Porthole.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Porthole));
		}
	}

	// ------------------------------------------------------------------------ the detergent drawer

	if (P.DrawerTravel > 0.0 && DrawerHeight > 0.0)
	{
		FHFMeshPart Drawer;
		Drawer.PartId = DetergentDrawerPartId();
		FHFMeshOps::InitialiseMesh(Drawer.Mesh);

		const double DrawerDepth = FMath::Max(P.DrawerTravel * 1.15, 0.1);

		// A tray with a front on it, drawn about its own closed position: the front sits in the
		// fascia's aperture and the tray runs back into the machine behind it.
		FHFMeshOps::AppendBox(Drawer.Mesh,
			FVector3d(0.0, -FasciaStand * 0.5, 0.0),
			FVector3d(DrawerWidth * 0.5 - 0.15, FasciaStand * 0.5, DrawerHeight * 0.5 - 0.15), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Drawer.Mesh,
			FVector3d(0.0, DrawerDepth * 0.5, -DrawerHeight * 0.28),
			FVector3d(DrawerWidth * 0.42, DrawerDepth * 0.5, DrawerHeight * 0.2), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::ApplyWorldScaleUVs(Drawer.Mesh);

		Drawer.PivotTransform = FTransform(FVector(DrawerCentreX, 0.0, DrawerCentreZ));

		Drawer.Motion.Type = EHFMotionType::Slide;

		// OUT OF THE MACHINE, which is -Y. A drawer that travelled +Y would report its whole declared
		// distance and would have gone further into the cabinet.
		Drawer.Motion.Axis = -FVector::YAxisVector;
		Drawer.Motion.MaxTravelCm = P.DrawerTravel;
		Drawer.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Drawer));
	}

	// ------------------------------------------------------------------------- the programme dial
	//
	// A control a person turns, so it turns. The same rule that gave the geyser a thermostat instead
	// of a moulded bump - see .claude/rules/04-conventions.md.

	if (P.DialRadius > 0.0 && P.DialSweepDegrees > 0.0)
	{
		FHFMeshPart Dial;
		Dial.PartId = ProgrammeDialPartId();
		FHFMeshOps::InitialiseMesh(Dial.Mesh);

		// STARTED INSIDE THE FASCIA, not on its face. A dial whose back disc lands exactly on the
		// panel behind it puts two drawn faces in one plane: the depth test picks a different winner
		// each frame and a 17.8 cm2 disc strobes on the front of the machine as the camera moves. The
		// same rule the geyser's pipework follows - the difference between a joint and two coincident
		// surfaces - and HouseForge.SampleHouse.NoTwoSurfacesShareAPlane is what found it.
		const double DialSink = FMath::Max(FasciaStand * 0.5, 0.2);

		const TArray<FVector2D> Body = {
			FVector2D(DialSink, 0.0),
			FVector2D(DialSink, P.DialRadius),
			FVector2D(-1.0, P.DialRadius * 0.94),
			FVector2D(-1.3, P.DialRadius * 0.70),
			FVector2D(-1.3, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Dial.Mesh, Body, FVector3d::Zero(),
			FVector3d::UnitY(), RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			// The index mark, without which a turning knob turns invisibly.
			FDynamicMesh3 Pointer;
			FHFMeshOps::InitialiseMesh(Pointer);

			FHFMeshOps::AppendBox(Pointer, FVector3d(0.0, -1.35, P.DialRadius * 0.45),
				FVector3d(P.DialRadius * 0.14, 0.25, P.DialRadius * 0.45), 0.0,
				EHFSurfaceRole::MetalHardware);

			FHFMeshOps::AppendPreservingRoles(Dial.Mesh, Pointer);
			FHFMeshOps::ApplyWorldScaleUVs(Dial.Mesh);

			Dial.PivotTransform = FTransform(
				FVector(P.Width - FMath::Max(P.DialRadius * 1.8, 3.0), -FasciaStand, DrawerCentreZ));

			Dial.Motion.Type = EHFMotionType::Hinge;
			Dial.Motion.Axis = FVector::YAxisVector;
			Dial.Motion.MaxAngleDegrees = P.DialSweepDegrees;
			Dial.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Dial));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
