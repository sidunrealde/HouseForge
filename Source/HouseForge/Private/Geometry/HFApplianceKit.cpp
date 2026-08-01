// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFApplianceKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	constexpr int32 RevolveSides = 20;

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

	if (P.KnobRadius > 0.0)
	{
		const double Pitch = P.Width / static_cast<double>(P.BurnerCount + 1);

		for (int32 Knob = 0; Knob < P.BurnerCount; ++Knob)
		{
			FHFMeshPart Part;
			Part.PartId = KnobPartId(Knob);
			FHFMeshOps::InitialiseMesh(Part.Mesh);

			// A domed disc with a flat indexing flag on it, so which way it is turned can be SEEN.
			// A plain cylinder rotates invisibly, which passes every assertion about motion and
			// looks like nothing at all.
			const TArray<FVector2D> KnobProfile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, P.KnobRadius),
				FVector2D(P.KnobRadius * 0.75, P.KnobRadius),
				FVector2D(P.KnobRadius * 1.1, P.KnobRadius * 0.6),
				FVector2D(P.KnobRadius * 1.2, 0.0)
			};

			// Turning about Y, which is the axis running back into the hob: the knob faces the room.
			FHFMeshOps::AppendRevolvedProfile(Part.Mesh, KnobProfile, FVector3d::ZeroVector,
				-FVector3d::UnitY(), RevolveSides, EHFSurfaceRole::MetalHardware);

			// The pointer flag, off centre so a turn is legible.
			FHFMeshOps::AppendBox(Part.Mesh,
				FVector3d(0.0, -P.KnobRadius * 0.62, P.KnobRadius * 0.55),
				FVector3d(P.KnobRadius * 0.16, P.KnobRadius * 0.62, P.KnobRadius * 0.45),
				0.0, EHFSurfaceRole::MetalHardware);

			FHFMeshOps::ApplyWorldScaleUVs(Part.Mesh);

			Part.PivotTransform = FTransform(FVector(
				-P.Width * 0.5 + Pitch * (Knob + 1),
				-Half.Y - P.KnobRadius * 0.2,
				P.GlassThickness * 0.5));

			Part.Motion.Type = EHFMotionType::Hinge;
			Part.Motion.Axis = FVector::YAxisVector;
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

	// The duct casing cannot be wider than the canopy it stands on, or the taper turns inside out.
	P.DuctWidth = FMath::Clamp(P.DuctWidth, 0.0, P.Width);
	P.DuctDepth = FMath::Clamp(P.DuctDepth, 0.0, P.Depth);

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

		const TArray<FVector2D> Outer = PlanRect(0.0, 0.0, P.Width, P.Depth);
		const TArray<FVector2D> Mouth = PlanRect(Inner, Inner, P.Width - Inner, P.Depth - Inner);

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
		const double DuctY0 = P.Depth - P.DuctDepth;
		const double DuctY1 = P.Depth;

		const FVector3d Bottom[4] = {
			FVector3d(0.0, 0.0, Z0), FVector3d(P.Width, 0.0, Z0),
			FVector3d(P.Width, P.Depth, Z0), FVector3d(0.0, P.Depth, Z0)
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
			FVector3d(P.Width * 0.5, P.Depth - P.DuctDepth * 0.5,
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
