// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFSanitaryKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Sides a revolved fitting is built from. 24 is round at arm's length and cheap. */
	constexpr int32 RevolveSides = 24;

	/** Corners a pressed bowl's rounded vertical arris is stepped in. */
	constexpr int32 BowlCornerSteps = 4;

	/** Thickness of the pressed steel a bowl is folded from. */
	constexpr double BowlWallThickness = 0.15;

	/**
	 * A rounded rectangle in plan, wound counter-clockwise.
	 *
	 * A PRESSED BOWL HAS NO SQUARE CORNERS, because it is drawn from a single sheet and a right angle
	 * cannot be drawn without tearing. It is also the detail that most gives a generated sink away: a
	 * box with a hole in it reads as a box however well it is proportioned, and the corner radius is
	 * the first thing the eye picks up when light runs round the inside of the bowl.
	 */
	TArray<FVector2D> RoundedRect(const FVector2D& Centre, const FVector2D& Half, double Radius)
	{
		const double R = FMath::Clamp(Radius, 0.0, FMath::Min(Half.X, Half.Y));

		TArray<FVector2D> Out;
		if (R <= UE_KINDA_SMALL_NUMBER)
		{
			Out.Add(Centre + FVector2D(-Half.X, -Half.Y));
			Out.Add(Centre + FVector2D(Half.X, -Half.Y));
			Out.Add(Centre + FVector2D(Half.X, Half.Y));
			Out.Add(Centre + FVector2D(-Half.X, Half.Y));
			return Out;
		}

		// Corner centres, counter-clockwise from the front-left.
		const FVector2D Corners[4] = {
			Centre + FVector2D(-(Half.X - R), -(Half.Y - R)),
			Centre + FVector2D(Half.X - R, -(Half.Y - R)),
			Centre + FVector2D(Half.X - R, Half.Y - R),
			Centre + FVector2D(-(Half.X - R), Half.Y - R)
		};

		// Each corner's arc starts a quarter turn on from the previous, beginning pointing at -Y.
		for (int32 Corner = 0; Corner < 4; ++Corner)
		{
			const double Start = -HALF_PI + Corner * HALF_PI;

			for (int32 Step = 0; Step <= BowlCornerSteps; ++Step)
			{
				const double Angle = Start + HALF_PI * static_cast<double>(Step)
					/ static_cast<double>(BowlCornerSteps);
				Out.Add(Corners[Corner] + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * R);
			}
		}

		return Out;
	}

	/** Scales a closed polygon about a centre - how a bowl's base is got from its mouth. */
	TArray<FVector2D> ScaledAbout(const TArray<FVector2D>& Polygon, const FVector2D& Centre, double Scale)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Polygon.Num());
		for (const FVector2D& Point : Polygon)
		{
			Out.Add(Centre + (Point - Centre) * Scale);
		}
		return Out;
	}

	// ------------------------------------------------------------------- the lofted ceramic forms
	//
	// Corner steps every lofted ring is drawn at. Fixed across a whole loft rather than chosen per
	// ring, because FHFMeshOps::AppendLoft joins sections by INDEX - see its note on correspondence.
	constexpr int32 LoftCornerSteps = 5;

	/** How round a lofted section's corners are, as a fraction of its shorter half-extent. */
	constexpr double LoftCornerFraction = 0.8;

	/** One horizontal section of a lofted form, expressed as fractions of the rim's own figures. */
	struct FLoftStation
	{
		/** 0 at the rim, 1 at the bottom of the form. */
		double Depth;

		double WidthScale;
		double LengthScale;

		/** How far the section's centre shifts BACK, as a fraction of the rim's half-length. */
		double BackShift;
	};

	/**
	 * A WC pan, in section, from the rim down to the floor.
	 *
	 * THIS TABLE IS THE OBJECT. A pan is a bowl overhanging a pedestal that stands well behind it -
	 * which is why the sections narrow AND walk backwards as they descend, and why a WC modelled as
	 * anything symmetric reads wrong from the side however well its rim is dimensioned. The waist
	 * about two thirds down is the other half of it: a straight taper is a plant pot.
	 */
	const FLoftStation PanStations[] = {
		{ 0.00, 1.00, 1.00, 0.00 },		// the rim
		{ 0.12, 0.97, 0.96, 0.01 },		// tucked under the rim's overhang
		{ 0.40, 0.80, 0.78, 0.11 },		// the bowl draws in
		{ 0.70, 0.55, 0.54, 0.28 },		// the waist of the pedestal
		{ 0.90, 0.53, 0.50, 0.34 },
		{ 1.00, 0.62, 0.58, 0.32 }		// the foot flares onto the floor
	};

	/** A basin's outer body, from the rim down to its underside. Far shallower than a pan. */
	const FLoftStation BasinStations[] = {
		{ 0.00, 1.00, 1.00, 0.00 },
		{ 0.30, 0.96, 0.95, 0.00 },
		{ 0.70, 0.85, 0.83, 0.02 },
		{ 1.00, 0.72, 0.70, 0.03 }
	};

	/**
	 * Builds the rings of a lofted form and the height of each, bottom-up.
	 *
	 * @param Inset      Taken off every section's half-extents. The ceramic wall, for a cavity.
	 * @param TopZ       Height of the rim section.
	 * @param BottomZ    Height of the last section.
	 */
	void LoftSections(const FLoftStation* Stations, int32 StationCount, const FVector2D& RimCentre,
		const FVector2D& RimHalf, double TopZ, double BottomZ, double Inset,
		TArray<TArray<FVector2D>>& OutRings, TArray<double>& OutZ)
	{
		OutRings.Reset();
		OutZ.Reset();

		// Bottom-up, which is the order AppendLoft wants, from a table written top-down because that
		// is the order a section drawing of a piece of sanitaryware is read in.
		for (int32 Index = StationCount - 1; Index >= 0; --Index)
		{
			const FLoftStation& Station = Stations[Index];

			const FVector2D Half(
				FMath::Max(RimHalf.X * Station.WidthScale - Inset, 0.01),
				FMath::Max(RimHalf.Y * Station.LengthScale - Inset, 0.01));

			const FVector2D Centre(RimCentre.X, RimCentre.Y + RimHalf.Y * Station.BackShift);

			OutRings.Add(FHFMeshOps::RoundedRectangle(Centre, Half,
				FMath::Min(Half.X, Half.Y) * LoftCornerFraction, LoftCornerSteps));
			OutZ.Add(FMath::Lerp(TopZ, BottomZ, Station.Depth));
		}
	}

	/**
	 * The lofted form's own outline at ONE height, interpolated between the stations either side.
	 *
	 * WHY THIS IS NOT "the nearest station's ring". A pan's floor lands wherever the bowl depth puts
	 * it, which is nowhere near a station - and the section a station below is a good deal narrower
	 * than the pan actually is at the floor's own height. Built from that, the floor slab is a disc
	 * that does not reach the wall it is supposed to close: the mesh stays closed, the volume stays
	 * right, and there is a slot of daylight all the way round the bottom of the bowl. That is the
	 * sink's bottomless bowls again, one level of subtlety further in.
	 */
	TArray<FVector2D> SectionAt(const FLoftStation* Stations, int32 StationCount,
		const FVector2D& RimCentre, const FVector2D& RimHalf, double TopZ, double BottomZ,
		double Z, double Inset)
	{
		const double Span = TopZ - BottomZ;
		const double Depth = Span > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp((TopZ - Z) / Span, 0.0, 1.0) : 0.0;

		int32 Upper = 1;
		while (Upper < StationCount - 1 && Stations[Upper].Depth < Depth)
		{
			++Upper;
		}

		const FLoftStation& A = Stations[Upper - 1];
		const FLoftStation& B = Stations[Upper];

		const double Range = B.Depth - A.Depth;
		const double T = Range > UE_KINDA_SMALL_NUMBER
			? FMath::Clamp((Depth - A.Depth) / Range, 0.0, 1.0) : 0.0;

		const FVector2D Half(
			FMath::Max(RimHalf.X * FMath::Lerp(A.WidthScale, B.WidthScale, T) - Inset, 0.01),
			FMath::Max(RimHalf.Y * FMath::Lerp(A.LengthScale, B.LengthScale, T) - Inset, 0.01));

		const FVector2D Centre(RimCentre.X,
			RimCentre.Y + RimHalf.Y * FMath::Lerp(A.BackShift, B.BackShift, T));

		return FHFMeshOps::RoundedRectangle(Centre, Half,
			FMath::Min(Half.X, Half.Y) * LoftCornerFraction, LoftCornerSteps);
	}

	/**
	 * Appends a tap onto a surface, and hands back its two moving parts.
	 *
	 * ONE TAP FOR THE SINK AND THE BASIN. They are the same fitting - a monobloc mixer whose lever
	 * lifts and whose spout swivels - and two copies of it would be two chances to get the lever's
	 * sign wrong, which is the one mistake on a tap that is invisible in a still: a lever pressed
	 * DOWN through the worktop travels exactly as far as one that lifts.
	 *
	 * @param Base Where the tap meets the surface it is mounted on, in the fitting's own space.
	 */
	void AppendTap(FDynamicMesh3& Shell, TArray<FHFMeshPart>& OutParts, const FHFTapParams& Tap,
		const FVector3d& Base)
	{
		if (!Tap.IsValid())
		{
			return;
		}

		FDynamicMesh3 Body;
		FHFMeshOps::InitialiseMesh(Body);

		// A slim column swelling very slightly at the base, which is what a monobloc actually is: a
		// straight cylinder reads as a pipe, and the flare is where it meets the stone.
		const TArray<FVector2D> BodyProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, Tap.BodyRadius * 1.35),
			FVector2D(1.0, Tap.BodyRadius * 1.1),
			FVector2D(2.0, Tap.BodyRadius),
			FVector2D(Tap.BodyHeight, Tap.BodyRadius),
			FVector2D(Tap.BodyHeight, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Body, BodyProfile, Base, FVector3d::UnitZ(),
			RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Shell, Body);
		}

		// ------------------------------------------------------------------------------ the spout
		//
		// ITS OWN PART, TURNING ABOUT THE BODY'S AXIS. A spout that cannot swing cannot reach the far
		// bowl of a double sink, which is the entire reason a double sink has a swivel spout.

		FHFMeshPart Spout;
		Spout.PartId = FHFSanitaryKit::TapSpoutPartId();
		FHFMeshOps::InitialiseMesh(Spout.Mesh);

		const TArray<FVector2D> ArmProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, Tap.SpoutRadius),
			FVector2D(Tap.SpoutReach, Tap.SpoutRadius),
			FVector2D(Tap.SpoutReach, 0.0)
		};

		FHFMeshOps::AppendRevolvedProfile(Spout.Mesh, ArmProfile,
			FVector3d(0.0, 0.0, Tap.BodyHeight - Tap.SpoutRadius),
			-FVector3d::UnitY(), RevolveSides, EHFSurfaceRole::MetalHardware);

		// The nose, pointing down into the bowl.
		const TArray<FVector2D> NoseProfile = {
			FVector2D(0.0, Tap.SpoutRadius),
			FVector2D(Tap.SpoutRadius * 2.2, Tap.SpoutRadius * 0.85),
			FVector2D(Tap.SpoutRadius * 2.2, 0.0)
		};

		FHFMeshOps::AppendRevolvedProfile(Spout.Mesh, NoseProfile,
			FVector3d(0.0, -Tap.SpoutReach, Tap.BodyHeight - Tap.SpoutRadius),
			-FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware);

		FHFMeshOps::ApplyWorldScaleUVs(Spout.Mesh);

		Spout.PivotTransform = FTransform(Base);
		Spout.Motion.Type = EHFMotionType::Hinge;
		Spout.Motion.Axis = FVector::ZAxisVector;
		Spout.Motion.MaxAngleDegrees = Tap.SpoutSwivelDegrees;
		Spout.DefaultOpenAmount = 0.0;

		OutParts.Add(MoveTemp(Spout));

		// ------------------------------------------------------------------------------ the lever

		if (Tap.LeverLength <= 0.0)
		{
			return;
		}

		FHFMeshPart Lever;
		Lever.PartId = FHFSanitaryKit::TapLeverPartId();
		FHFMeshOps::InitialiseMesh(Lever.Mesh);

		const double LeverRadius = FMath::Max(Tap.SpoutRadius * 0.55, 0.25);

		const TArray<FVector2D> LeverProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, LeverRadius),
			FVector2D(Tap.LeverLength, LeverRadius * 0.8),
			FVector2D(Tap.LeverLength, 0.0)
		};

		// Runs backwards from the top of the body, which is where a monobloc's lever sits.
		FHFMeshOps::AppendRevolvedProfile(Lever.Mesh, LeverProfile,
			FVector3d::ZeroVector, FVector3d::UnitY(), RevolveSides, EHFSurfaceRole::MetalHardware);

		FHFMeshOps::ApplyWorldScaleUVs(Lever.Mesh);

		Lever.PivotTransform = FTransform(Base + FVector3d(0.0, 0.0, Tap.BodyHeight));

		// Lifting is a rotation about the axis ACROSS the tap, and the SIGN is the whole content of
		// it: the lever runs backwards from the top of the body along +Y, so a positive turn about +X
		// carries its far end upwards. Negative, it presses the handle down through the worktop
		// instead - a movement of exactly the same size, in the one direction a mixer lever cannot go,
		// and indistinguishable from correct in any still.
		Lever.Motion.Type = EHFMotionType::Hinge;
		Lever.Motion.Axis = FVector::XAxisVector;
		Lever.Motion.MaxAngleDegrees = Tap.LeverLiftDegrees;
		Lever.DefaultOpenAmount = 0.0;

		OutParts.Add(MoveTemp(Lever));
	}
}

FHFSinkParams FHFSanitaryKit::SanitiseSink(const FHFSinkParams& Params)
{
	FHFSinkParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.BowlDepth = FMath::Max(P.BowlDepth, 0.0);
	P.BowlCount = FMath::Clamp(P.BowlCount, 1, 3);
	P.RimThickness = FMath::Max(P.RimThickness, 0.05);

	// The rim cannot eat the bowls. Every bowl needs a flat each side of it and one between it and
	// its neighbour, so the widest usable rim is what leaves each bowl a positive width.
	const double MaxRim = P.Width / static_cast<double>(2 * P.BowlCount + 2);
	P.RimWidth = FMath::Clamp(P.RimWidth, 0.0, FMath::Max(MaxRim, 0.0));

	const double BowlWidth = P.Width / static_cast<double>(P.BowlCount) - 2.0 * P.RimWidth;
	const double BowlBreadth = P.Depth - 2.0 * P.RimWidth;

	P.BowlCornerRadius = FMath::Clamp(P.BowlCornerRadius, 0.0,
		FMath::Max(FMath::Min(BowlWidth, BowlBreadth) * 0.5, 0.0));

	P.Tap.BodyHeight = FMath::Max(P.Tap.BodyHeight, 0.0);
	P.Tap.BodyRadius = FMath::Max(P.Tap.BodyRadius, 0.0);
	P.Tap.SpoutRadius = FMath::Clamp(P.Tap.SpoutRadius, 0.0, FMath::Max(P.Tap.BodyRadius, 0.0));
	P.Tap.SpoutReach = FMath::Max(P.Tap.SpoutReach, 0.0);
	P.Tap.LeverLength = FMath::Max(P.Tap.LeverLength, 0.0);
	P.Tap.LeverLiftDegrees = FMath::Clamp(P.Tap.LeverLiftDegrees, 0.0, 90.0);
	P.Tap.SpoutSwivelDegrees = FMath::Clamp(P.Tap.SpoutSwivelDegrees, 0.0, 180.0);

	return P;
}

FHFSinkBuild FHFSanitaryKit::BuildSink(const FHFSinkParams& Params)
{
	FHFSinkBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFSinkParams P = SanitiseSink(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double ModuleWidth = P.Width / static_cast<double>(P.BowlCount);
	const FVector2D BowlHalf(
		FMath::Max(ModuleWidth * 0.5 - P.RimWidth, 0.0),
		FMath::Max(P.Depth * 0.5 - P.RimWidth, 0.0));

	if (BowlHalf.X <= 0.0 || BowlHalf.Y <= 0.0)
	{
		return Out;
	}

	// -------------------------------------------------------------------------------------- the rim
	//
	// A pressed plate with a hole over each bowl, triangulated with its holes rather than cut. The
	// rim is what the sink is HUNG BY - the whole thing sits on the stone by this flange - so it is
	// the one part whose outline has to be exactly the drawn footprint.
	//
	// IT SITS ON THE STONE, NOT IN IT. The rim occupies 0 to RimThickness, ABOVE Z = 0, because a
	// top-mounted sink stands proud of the worktop by the thickness of its own flange. Modelled the
	// other way up - flange buried in the slab with its top face level with the stone - it put 318
	// cm2 of steel in exactly the same plane as the granite around it, both facing up: textbook
	// z-fighting, invisible in a still and strobing the moment the camera moves. The coplanar scan
	// caught it on the first build.

	// A BOWL IS A THIN PRESSING, AND ITS WALL IS BUILT AS A SOLID MINUS A SOLID rather than as a
	// thin annulus swept down. Swept, the annulus between the bowl's outer face and its inner is
	// about a millimetre and a half across a 20 cm drop, and a triangulator handed a ring that thin
	// resolves it as the outer polygon alone - which is a SOLID BLOCK the shape of the bowl, in
	// exactly the place a bowl should be, with the rim sitting correctly on top of it. It looks
	// perfect from every angle and it is not a sink. Two robust solid prisms and one boolean cannot
	// fail that way, and the volume test below is what says so.
	const double Wall = FMath::Min(BowlWallThickness * 2.0,
		FMath::Min(BowlHalf.X, BowlHalf.Y) * 0.5);

	TArray<FVector2D> BowlOuter[3];
	TArray<FVector2D> BowlInner[3];
	TArray<TArray<FVector2D>> RimHoles;

	for (int32 Bowl = 0; Bowl < P.BowlCount; ++Bowl)
	{
		const FVector2D Centre(-P.Width * 0.5 + ModuleWidth * (Bowl + 0.5), 0.0);

		BowlOuter[Bowl] = RoundedRect(Centre, BowlHalf, P.BowlCornerRadius);
		BowlInner[Bowl] = RoundedRect(Centre, BowlHalf - FVector2D(Wall, Wall),
			P.BowlCornerRadius - Wall);

		// The rim laps the TOP OF THE BOWL WALL, which is what a pressing does - the flange and the
		// bowl are one piece of steel folded over. Holed at the inner face, so the wall's top edge is
		// covered rather than meeting the rim's hole in a shared vertical plane.
		RimHoles.Add(BowlInner[Bowl]);
	}

	const TArray<FVector2D> RimOutline = {
		FVector2D(-P.Width * 0.5, -P.Depth * 0.5),
		FVector2D(P.Width * 0.5, -P.Depth * 0.5),
		FVector2D(P.Width * 0.5, P.Depth * 0.5),
		FVector2D(-P.Width * 0.5, P.Depth * 0.5)
	};

	if (!FHFMeshOps::AppendPrismWithHoles(Out.Shell, RimOutline, RimHoles,
		0.0, P.RimThickness, EHFSurfaceRole::Sanitary))
	{
		return Out;
	}

	// ------------------------------------------------------------------------------------ the bowls
	//
	// Each bowl is a tub: four tapered walls and a base, hollow, hung from the hole in the rim. Built
	// as a wall prism plus a base rather than as a solid block, because a sink that is not hollow is
	// the single most obvious failure this fixture can have and nothing but real geometry proves it.
	//
	// The walls draw IN towards the base, which is how a bowl is pressed and what makes water find
	// the waste rather than standing in the corners.

	const double BaseZ = -P.BowlDepth;

	for (int32 Bowl = 0; Bowl < P.BowlCount; ++Bowl)
	{
		// ------------------------------------------------------------------------ the four walls
		//
		// A SOLID CUT CLEAN THROUGH, and then a base put back under it as its own slab.
		//
		// Cutting a cavity that stopped at the inside of the base was the obvious way round and it
		// produced BOTTOMLESS BOWLS - two neat white tubes looking straight through into the cream
		// carcass below. The cavity's bottom face and the base's top face wanted to be the same plane,
		// which is the one case a mesh boolean has no good answer for, and it resolved it by taking
		// the base. Every measurement still passed: the bowls were hollow, the volume was small, the
		// bounds reached the right depth. It took looking into the sink to see there was no bottom.
		//
		// So neither solid is asked to end where the other begins. The cut goes right through, which
		// is unambiguous, and the base is a separate slab that meets the walls face to face.
		FDynamicMesh3 Walls;
		FHFMeshOps::InitialiseMesh(Walls);

		if (!FHFMeshOps::AppendPrism(Walls, BowlOuter[Bowl], BaseZ, 0.0, EHFSurfaceRole::Sanitary))
		{
			continue;
		}

		FDynamicMesh3 Cavity;
		FHFMeshOps::InitialiseMesh(Cavity);

		if (FHFMeshOps::AppendPrism(Cavity, BowlInner[Bowl], BaseZ - Wall - 1.0,
			P.RimThickness + 1.0, EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::SubtractInPlace(Walls, Cavity);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Walls);

		// ------------------------------------------------------------------------------ the base
		//
		// The floor of the bowl, sitting under the walls: its top IS the surface water stands on.
		//
		// THICKER THAN THE WALLS, DOWNWARDS ONLY. The floor's top stays exactly at the bowl's depth,
		// so nothing about the bowl a person sees or measures changes; the slab simply has some
		// substance under it. At one wall thickness it was 3 mm of steel spanning 33 x 38 - right on
		// FHFRenderFinish::MinFeatureFactor's threshold, thin enough to be lost in the render finish,
		// and the bowl looked straight through into the cream carcass below. A real bowl is a formed
		// pan with a sound-deadening pad under it and is nothing like 3 mm at the bottom either.
		FDynamicMesh3 Base;
		FHFMeshOps::InitialiseMesh(Base);

		if (FHFMeshOps::AppendPrism(Base, BowlOuter[Bowl], BaseZ - Wall * 4.0, BaseZ,
			EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Base);
		}

		Out.BowlVolume += (2.0 * (BowlHalf.X - Wall)) * (2.0 * (BowlHalf.Y - Wall)) * P.BowlDepth;
	}

	// -------------------------------------------------------------------------------------- the tap
	//
	// Mounted on the rim at the back, which is where the tap hole is punched, and centred on the run.

	if (P.bHasTap)
	{
		AppendTap(Out.Shell, Out.Parts, P.Tap, FVector3d(0.0, P.Depth * 0.5 - P.RimWidth * 0.5, 0.0));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The WC.
//
// =============================================================================================

FHFWCParams FHFSanitaryKit::SanitiseWC(const FHFWCParams& Params)
{
	FHFWCParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Projection = FMath::Max(P.Projection, 0.0);
	P.SeatHeight = FMath::Max(P.SeatHeight, 0.0);
	P.SeatThickness = FMath::Clamp(P.SeatThickness, 0.2, FMath::Max(P.SeatHeight * 0.2, 0.2));

	// The cistern may not eat the pan. A WC whose cistern is deeper than its projection is a cistern
	// on the floor, and clamping to two thirds leaves a pan that is still a pan.
	P.CisternDepth = FMath::Clamp(P.CisternDepth, 0.0, P.Projection * 0.66);
	P.CisternHeight = FMath::Max(P.CisternHeight, 0.0);
	P.CisternInset = FMath::Clamp(P.CisternInset, 0.0, FMath::Max(P.Width * 0.25, 0.0));

	P.CeramicThickness = FMath::Clamp(P.CeramicThickness, 0.2,
		FMath::Max(FMath::Min(P.Width, P.PanLength()) * 0.15, 0.2));

	// The rim is never thinner than the china it is formed in, and never so wide that it closes the
	// opening: a third of the shorter half-extent still leaves a bowl a person could use.
	P.RimWidth = FMath::Clamp(P.RimWidth, P.CeramicThickness,
		FMath::Max(FMath::Min(P.Width, P.PanLength()) * 0.16, P.CeramicThickness));

	// The bowl cannot be deeper than the rim stands off the floor, or its floor would be underground.
	P.BowlDepth = FMath::Clamp(P.BowlDepth, 0.0, FMath::Max(P.RimZ() - P.CeramicThickness * 4.0, 0.0));

	P.LidLiftDegrees = FMath::Clamp(P.LidLiftDegrees, 0.0, 170.0);
	P.FlushButtonTravel = FMath::Max(P.FlushButtonTravel, 0.0);

	return P;
}

FHFWCBuild FHFSanitaryKit::BuildWC(const FHFWCParams& Params)
{
	FHFWCBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFWCParams P = SanitiseWC(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double PanLength = P.PanLength();
	const double RimZ = P.RimZ();

	// The pan occupies what the cistern has left of the projection, and the rim is set out from the
	// middle of THAT rather than from the middle of the drawn box.
	const double PanBackY = P.Projection * 0.5 - P.CisternDepth;
	const double PanFrontY = -P.Projection * 0.5;

	const FVector2D RimCentre(0.0, (PanBackY + PanFrontY) * 0.5);
	const FVector2D RimHalf(P.Width * 0.5, PanLength * 0.5);

	Out.RimZ = RimZ;

	// ------------------------------------------------------------------------------------- the pan
	//
	// One lofted solid from the rim to the floor, then a cavity cut clean through it, then a floor
	// slab put back under the cavity.
	//
	// CUT CLEAN THROUGH, exactly as the sink's bowls are, and for the reason recorded there: a cavity
	// asked to stop at the inside of a floor puts two faces in one plane, which is the single case a
	// mesh boolean has no good answer for. It resolved that one by taking the floor, and the sink came
	// out as two neat bottomless tubes that measured hollow, measured deep enough and looked perfect
	// from every angle except the one that mattered.

	{
		TArray<TArray<FVector2D>> Rings;
		TArray<double> Heights;
		LoftSections(PanStations, UE_ARRAY_COUNT(PanStations), RimCentre, RimHalf, RimZ, 0.0, 0.0,
			Rings, Heights);

		FDynamicMesh3 Pan;
		FHFMeshOps::InitialiseMesh(Pan);

		if (!FHFMeshOps::AppendLoft(Pan, Rings, Heights, /*bCapBottom*/ true, /*bCapTop*/ true,
			EHFSurfaceRole::Sanitary))
		{
			return Out;
		}

		const double BowlFloorZ = RimZ - P.BowlDepth;

		// INSET BY THE RIM RATHER THAN BY THE WALL. A WC's rim is a hollow flushing channel 35-45 mm
		// across, not the top edge of a 10 mm wall - and at the wall thickness the opening comes within
		// a centimetre of the outside of the pan, leaving a seat whose ring is narrower than its hole.
		TArray<TArray<FVector2D>> CavityRings;
		TArray<double> CavityHeights;
		LoftSections(PanStations, UE_ARRAY_COUNT(PanStations), RimCentre, RimHalf,
			RimZ + 1.0, -1.0, P.RimWidth, CavityRings, CavityHeights);

		FDynamicMesh3 Cavity;
		FHFMeshOps::InitialiseMesh(Cavity);

		if (FHFMeshOps::AppendLoft(Cavity, CavityRings, CavityHeights, true, true,
			EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::SubtractInPlace(Pan, Cavity);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Pan);

		// The floor of the bowl: a slab meeting the walls face to face, with substance under it rather
		// than a membrane. THICKER THAN THE WALL, DOWNWARDS ONLY, so nothing a person sees or measures
		// about the bowl changes - the same rule the sink's bowl floor follows, and for the same
		// reason: at one wall thickness it falls under the render finish's minimum feature size and
		// the bowl looks straight through into whatever is below it.
		//
		// SIZED FROM THE CAVITY AT ITS OWN HEIGHT rather than from the rim. The pan draws in as it
		// descends, so a slab cut to the rim's outline would stand outside the pan by a third of its
		// width and hang in the air on both sides.
		{
			const double FloorBottomZ = BowlFloorZ - P.CeramicThickness * 3.0;

			// THE PAN'S OWN OUTLINE AT THE FLOOR'S OWN HEIGHT, and not inset at all. The slab is wider
			// than the cavity everywhere by exactly the wall thickness, so its edge is buried in the
			// china and what shows is a floor that reaches the wall all the way round. Interpolated
			// rather than taken from the nearest station - see SectionAt, and the slot of daylight
			// round the bottom of the bowl that not interpolating produced.
			const TArray<TArray<FVector2D>> FloorRings = {
				SectionAt(PanStations, UE_ARRAY_COUNT(PanStations), RimCentre, RimHalf,
					RimZ, 0.0, FloorBottomZ, 0.0),
				SectionAt(PanStations, UE_ARRAY_COUNT(PanStations), RimCentre, RimHalf,
					RimZ, 0.0, BowlFloorZ, 0.0)
			};
			const TArray<double> FloorHeights = { FloorBottomZ, BowlFloorZ };

			FDynamicMesh3 Floor;
			FHFMeshOps::InitialiseMesh(Floor);

			if (FHFMeshOps::AppendLoft(Floor, FloorRings, FloorHeights, true, true,
				EHFSurfaceRole::Sanitary))
			{
				FHFMeshOps::AppendPreservingRoles(Out.Shell, Floor);
			}
		}

		// What the bowl actually holds, for a caller that has to prove it is hollow rather than a
		// block of china the right shape. Off the OPENING and the taper, not the outside of the pan.
		Out.BowlVolume = (P.Width - 2.0 * P.RimWidth) * (PanLength - 2.0 * P.RimWidth)
			* P.BowlDepth * 0.55;
	}

	// --------------------------------------------------------------------------------- the cistern

	const double CisternFrontY = PanBackY;
	const double CisternTopZ = RimZ + P.CisternHeight;

	if (P.CisternHeight > 0.0 && P.CisternDepth > 0.0)
	{
		const FVector2D CisternCentre(0.0, (CisternFrontY + P.Projection * 0.5) * 0.5);
		const FVector2D CisternHalf(FMath::Max(P.Width * 0.5 - P.CisternInset, 0.01),
			P.CisternDepth * 0.5);

		// The lid is a separate slab standing slightly proud, which is the shadow line that makes a
		// cistern read as a cistern rather than as a block.
		const double LidThickness = FMath::Min(2.0, P.CisternHeight * 0.15);

		FDynamicMesh3 Body;
		FHFMeshOps::InitialiseMesh(Body);

		if (FHFMeshOps::AppendPrism(Body,
			FHFMeshOps::RoundedRectangle(CisternCentre, CisternHalf,
				FMath::Min(CisternHalf.X, CisternHalf.Y) * 0.25, 3),
			RimZ, CisternTopZ - LidThickness, EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Body);
		}

		FDynamicMesh3 Lid;
		FHFMeshOps::InitialiseMesh(Lid);

		// The lid oversails the box at the sides and the FRONT, and not at the back: the back is on the
		// wall, and a lid grown there puts the widest thing on the fitting 4 mm outside the box it was
		// drawn in - which OnWallFace would then dutifully hold off the plaster by 4 mm.
		constexpr double LidOversail = 0.4;

		if (FHFMeshOps::AppendPrism(Lid,
			FHFMeshOps::RoundedRectangle(CisternCentre - FVector2D(0.0, LidOversail * 0.5),
				CisternHalf + FVector2D(LidOversail, LidOversail * 0.5),
				FMath::Min(CisternHalf.X, CisternHalf.Y) * 0.25, 3),
			CisternTopZ - LidThickness, CisternTopZ, EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Lid);
		}

		// ------------------------------------------------------------------- the dual-flush plate
		//
		// SMALL, AND STILL A MOVING PART. It travels 8 mm, which is less than anything else in this
		// flat moves - and the rule is that a thing which moves in the real object moves in this one,
		// not that it moves far enough to be worth the trouble. See .claude/rules/04-conventions.md.

		if (P.FlushButtonTravel > 0.0)
		{
			FHFMeshPart Button;
			Button.PartId = FlushButtonPartId();
			FHFMeshOps::InitialiseMesh(Button.Mesh);

			const FVector2D ButtonHalf(FMath::Min(4.0, CisternHalf.X * 0.5),
				FMath::Min(2.6, CisternHalf.Y * 0.6));
			const double ButtonProud = 0.6;

			// In its own local space with the pivot on the origin, like every other part: the plate
			// hangs below its own datum so the datum is the cistern's finished top.
			if (FHFMeshOps::AppendPrism(Button.Mesh,
				FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector, ButtonHalf,
					FMath::Min(ButtonHalf.X, ButtonHalf.Y) * 0.5, 4),
				-P.FlushButtonTravel, ButtonProud, EHFSurfaceRole::MetalHardware))
			{
				FHFMeshOps::ApplyWorldScaleUVs(Button.Mesh);

				Button.PivotTransform = FTransform(FVector(CisternCentre.X, CisternCentre.Y, CisternTopZ));
				Button.Motion.Type = EHFMotionType::Slide;
				Button.Motion.Axis = FVector::ZAxisVector;

				// DOWN. A flush plate is pressed, and the sign is the whole of it: upwards it travels
				// exactly as far, out of the cistern, and every assertion about travel still passes.
				Button.Motion.MaxTravelCm = -P.FlushButtonTravel;

				Out.Parts.Add(MoveTemp(Button));
			}
		}
	}

	// -------------------------------------------------------------------------- the seat and lid
	//
	// ## Where they stop, and why it is not simply what was asked for
	//
	// A close-coupled seat leans back onto its cistern. Asked for 100 degrees with an 18 cm cistern
	// standing immediately behind the hinge, it sweeps the last several degrees of that arc THROUGH
	// the cistern's front face - a full, correct-looking travel that the object could not perform. So
	// the request is clamped to the angle at which the leaf meets the ceramic behind it.

	// INSIDE THE CHINA ON EVERY SIDE. A seat proud of the pan it sits on would put the widest thing on
	// the fitting outside the box the fitting was drawn in - and would also stand its own side faces in
	// exactly the plane of the pan's, which is z-fighting waiting for the camera to move. 3 mm in is
	// what a real seat is set at, and it leaves the china's edge catching the light on its own.
	constexpr double SeatSetIn = 0.3;

	const double HingeY = CisternFrontY - 2.5;
	const double SeatLength = HingeY - (PanFrontY + SeatSetIn);

	double Lift = P.LidLiftDegrees;

	if (P.CisternHeight > 0.0 && P.CisternDepth > 0.0 && SeatLength > 0.0)
	{
		// How far past vertical the leaf can lean before its back face touches the cistern.
		const double Reach = FMath::Clamp(
			(CisternFrontY - HingeY - P.SeatThickness * 0.5) / SeatLength, 0.0, 1.0);
		Lift = FMath::Min(Lift, 90.0 + FMath::RadiansToDegrees(FMath::Asin(Reach)));
	}

	Out.LidLiftDegrees = Lift;

	if (SeatLength > 0.0)
	{
		// The seat and lid share one outline: the rim's, set in a little all round.
		const FVector2D LeafCentre(RimCentre.X, (HingeY + (PanFrontY + SeatSetIn)) * 0.5);
		const FVector2D LeafHalf(FMath::Max(RimHalf.X - SeatSetIn, 0.1), SeatLength * 0.5);
		const double LeafRadius = FMath::Min(LeafHalf.X, LeafHalf.Y) * LoftCornerFraction;

		const TArray<FVector2D> LeafOutline =
			FHFMeshOps::RoundedRectangle(LeafCentre, LeafHalf, LeafRadius, LoftCornerSteps);

		// LIFTING IS A NEGATIVE TURN ABOUT +X, and the sign is the whole content of it. The leaf runs
		// FORWARD from its hinge along -Y, so a positive turn about +X drives its far end down through
		// the pan - the same distance, in the one direction a WC seat cannot go, and identical to
		// correct in any still.
		auto MakeLeaf = [&](FName PartId, double BottomZ, double TopZ, const TArray<TArray<FVector2D>>& Holes)
		{
			FHFMeshPart Leaf;
			Leaf.PartId = PartId;
			FHFMeshOps::InitialiseMesh(Leaf.Mesh);

			const double PivotZ = (BottomZ + TopZ) * 0.5;

			// Authored about the pivot, which is what keeps the generator pure - see FHFMeshPart.
			TArray<FVector2D> Local;
			Local.Reserve(LeafOutline.Num());
			for (const FVector2D& Point : LeafOutline)
			{
				Local.Add(Point - FVector2D(0.0, HingeY));
			}

			TArray<TArray<FVector2D>> LocalHoles;
			for (const TArray<FVector2D>& Hole : Holes)
			{
				TArray<FVector2D> Moved;
				Moved.Reserve(Hole.Num());
				for (const FVector2D& Point : Hole)
				{
					Moved.Add(Point - FVector2D(0.0, HingeY));
				}
				LocalHoles.Add(MoveTemp(Moved));
			}

			const bool bBuilt = LocalHoles.IsEmpty()
				? FHFMeshOps::AppendPrism(Leaf.Mesh, Local, BottomZ - PivotZ, TopZ - PivotZ,
					EHFSurfaceRole::ShutterLaminate)
				: FHFMeshOps::AppendPrismWithHoles(Leaf.Mesh, Local, LocalHoles, BottomZ - PivotZ,
					TopZ - PivotZ, EHFSurfaceRole::ShutterLaminate);

			if (!bBuilt)
			{
				return;
			}

			FHFMeshOps::ApplyWorldScaleUVs(Leaf.Mesh);

			Leaf.PivotTransform = FTransform(FVector(0.0, HingeY, PivotZ));
			Leaf.Motion.Type = EHFMotionType::Hinge;
			Leaf.Motion.Axis = FVector::XAxisVector;
			Leaf.Motion.MaxAngleDegrees = -Lift;
			Leaf.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Leaf));
		};

		// The seat is a RING. Its opening is what makes it a seat rather than a board, and it is the
		// one hole in this fitting somebody actually looks through.
		//
		// SET OUT FROM THE BOWL AND NOT FROM THE SEAT'S OWN OUTLINE, which is the only way it can be
		// guaranteed to land on the rim: the seat is centred on its hinge and the bowl is centred on
		// itself, so a hole inset from the leaf by a constant would sit correctly at the front and
		// hang over the china at the back. Held INSIDE the pan's opening on every side, so the ring
		// covers the flushing rim rather than framing a slot of it.
		const FVector2D HoleHalf(
			FMath::Max(RimHalf.X - P.RimWidth - 1.0, 1.0),
			FMath::Max(RimHalf.Y - P.RimWidth - 2.0, 1.0));

		TArray<TArray<FVector2D>> SeatHole;
		SeatHole.Add(FHFMeshOps::RoundedRectangle(RimCentre - FVector2D(0.0, 1.0), HoleHalf,
			FMath::Min(HoleHalf.X, HoleHalf.Y) * LoftCornerFraction, LoftCornerSteps));

		MakeLeaf(SeatPartId(), RimZ, RimZ + P.SeatThickness, SeatHole);

		// And the lid on top of it, solid.
		MakeLeaf(LidPartId(), P.SeatHeight, P.SeatHeight + P.SeatThickness, {});

		// THE SEAT MAY NOT RISE THROUGH THE LID ABOVE IT. A threshold of zero makes the allowance the
		// blocker's own amount exactly - see FHFPartMotion::AllowanceFrom - so the seat's angle tracks
		// the lid's rather than being released at some point along it. That is the real constraint:
		// the two are nested, the lid rests ON the seat, and a lid can be lifted by itself while a
		// seat can never be lifted by itself.
		for (FHFMeshPart& Part : Out.Parts)
		{
			if (Part.PartId == SeatPartId())
			{
				Part.Motion.SequencedAfterPartId = LidPartId();
				Part.Motion.SequenceThreshold = 0.0;
			}
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The basin.
//
// =============================================================================================

FHFBasinParams FHFSanitaryKit::SanitiseBasin(const FHFBasinParams& Params)
{
	FHFBasinParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.CeramicThickness = FMath::Clamp(P.CeramicThickness, 0.2,
		FMath::Max(FMath::Min(P.Width, P.Depth) * 0.12, 0.2));

	// The rim and the tap ledge together may not close the bowl. Each is clamped to what leaves a
	// positive opening in its own direction, which is the honest answer for a basin drawn too small
	// to be one - an empty mesh rather than a sliver.
	P.RimWidth = FMath::Clamp(P.RimWidth, P.CeramicThickness,
		FMath::Max(P.Width * 0.3, P.CeramicThickness));
	P.TapLedgeWidth = FMath::Clamp(P.TapLedgeWidth, P.RimWidth,
		FMath::Max(P.Depth - P.RimWidth - 4.0, P.RimWidth));

	P.CornerRadius = FMath::Clamp(P.CornerRadius, 0.0, FMath::Min(P.Width, P.Depth) * 0.5);

	// A bowl cannot be deeper than the basin is tall, or its floor would be under the counter it
	// stands on. A vessel basin's floor is a few centimetres above the stone and that is all.
	P.BowlDepth = FMath::Clamp(P.BowlDepth, 0.0,
		FMath::Max(P.Height - P.CeramicThickness * 3.0, 0.0));

	P.ShroudDrop = FMath::Max(P.ShroudDrop, 0.0);

	P.Tap.BodyHeight = FMath::Max(P.Tap.BodyHeight, 0.0);
	P.Tap.BodyRadius = FMath::Max(P.Tap.BodyRadius, 0.0);
	P.Tap.SpoutRadius = FMath::Clamp(P.Tap.SpoutRadius, 0.0, FMath::Max(P.Tap.BodyRadius, 0.0));
	P.Tap.SpoutReach = FMath::Max(P.Tap.SpoutReach, 0.0);
	P.Tap.LeverLength = FMath::Max(P.Tap.LeverLength, 0.0);
	P.Tap.LeverLiftDegrees = FMath::Clamp(P.Tap.LeverLiftDegrees, 0.0, 90.0);
	P.Tap.SpoutSwivelDegrees = FMath::Clamp(P.Tap.SpoutSwivelDegrees, 0.0, 180.0);

	return P;
}

FHFBasinBuild FHFSanitaryKit::BuildBasin(const FHFBasinParams& Params)
{
	FHFBasinBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFBasinParams P = SanitiseBasin(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const FVector2D RimCentre = FVector2D::ZeroVector;
	const FVector2D RimHalf(P.Width * 0.5, P.Depth * 0.5);

	// Where the bowl's mouth sits inside the rim. FORWARD of centre, because the ledge at the back
	// takes more of the depth than the rim at the front does - which is the whole shape of a basin
	// with a tap on it, and is why this is not simply an inset.
	const FVector2D BowlCentre(0.0, -(P.TapLedgeWidth - P.RimWidth) * 0.5);
	const FVector2D BowlHalf(
		FMath::Max(P.Width * 0.5 - P.RimWidth, 0.0),
		FMath::Max((P.Depth - P.RimWidth - P.TapLedgeWidth) * 0.5, 0.0));

	if (BowlHalf.X <= 0.0 || BowlHalf.Y <= 0.0)
	{
		return Out;
	}

	// ----------------------------------------------------------------------------------- the body

	{
		TArray<TArray<FVector2D>> Rings;
		TArray<double> Heights;
		LoftSections(BasinStations, UE_ARRAY_COUNT(BasinStations), RimCentre, RimHalf,
			P.Height, 0.0, 0.0, Rings, Heights);

		// The rim's own corner radius rather than the loft's fraction: a basin's plan is a stated
		// figure on the drawing, where a WC pan's is only ever "rounded".
		Rings.Last() = FHFMeshOps::RoundedRectangle(RimCentre, RimHalf, P.CornerRadius, LoftCornerSteps);

		FDynamicMesh3 Body;
		FHFMeshOps::InitialiseMesh(Body);

		if (!FHFMeshOps::AppendLoft(Body, Rings, Heights, true, true, EHFSurfaceRole::Sanitary))
		{
			return Out;
		}

		// The bowl, cut clean through - the sink's lesson again, and the WC's: a cavity asked to stop
		// on the inside of a floor shares a plane with it, and the boolean resolves that by taking the
		// floor. Two robust solids and one cut cannot fail that way.
		const double BowlFloorZ = P.Height - P.BowlDepth;

		FDynamicMesh3 Cavity;
		FHFMeshOps::InitialiseMesh(Cavity);

		const TArray<TArray<FVector2D>> CavityRings = {
			FHFMeshOps::RoundedRectangle(BowlCentre, BowlHalf * 0.86,
				FMath::Min(BowlHalf.X, BowlHalf.Y) * 0.55, LoftCornerSteps),
			FHFMeshOps::RoundedRectangle(BowlCentre, BowlHalf,
				FMath::Min(BowlHalf.X, BowlHalf.Y) * 0.7, LoftCornerSteps)
		};
		const TArray<double> CavityHeights = { -1.0, P.Height + 1.0 };

		if (FHFMeshOps::AppendLoft(Cavity, CavityRings, CavityHeights, true, true,
			EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::SubtractInPlace(Body, Cavity);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Body);

		// And the floor put back under it, thicker than the wall and downwards only, so the inside of
		// the bowl stays exactly where it was measured to be. Sized from the cavity AT THAT HEIGHT,
		// because the cavity tapers - a slab cut to the mouth would stand outside the bowl.
		FDynamicMesh3 Floor;
		FHFMeshOps::InitialiseMesh(Floor);

		const double FloorScale = FMath::Lerp(0.86, 1.0,
			P.Height > 0.0 ? FMath::Clamp(BowlFloorZ / P.Height, 0.0, 1.0) : 1.0);

		if (FHFMeshOps::AppendPrism(Floor,
			FHFMeshOps::RoundedRectangle(BowlCentre, BowlHalf * FloorScale,
				FMath::Min(BowlHalf.X, BowlHalf.Y) * 0.55, LoftCornerSteps),
			BowlFloorZ - P.CeramicThickness * 2.5, BowlFloorZ, EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Floor);
		}

		Out.BowlVolume = (2.0 * BowlHalf.X) * (2.0 * BowlHalf.Y) * P.BowlDepth * 0.7;

		// The waste. A chrome boss standing a few millimetres proud of the china it is bedded into,
		// which is what a pop-up waste is and what stops the bowl reading as a smooth hollow.
		FDynamicMesh3 Waste;
		FHFMeshOps::InitialiseMesh(Waste);

		const TArray<FVector2D> WasteProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, 3.2),
			FVector2D(0.5, 3.2),
			FVector2D(0.9, 2.6),
			FVector2D(0.9, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Waste, WasteProfile,
			FVector3d(BowlCentre.X, BowlCentre.Y, BowlFloorZ), FVector3d::UnitZ(),
			RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Waste);
		}
	}

	// --------------------------------------------------------------------------------- the shroud
	//
	// WHAT HOLDS A WALL-HUNG BASIN UP, and it is entirely outside the drawn box. A plan dimensions the
	// bowl - 550 x 450 at 800 - and says nothing at all about the trap, the brackets or the pedestal,
	// so a basin built to the drawn box and nothing else is a ceramic bowl floating at 800 with its
	// waste in mid-air. That is not a small omission in a small room: it is the whole lower half of
	// the object, at eye level from the door.

	if (P.Mount == EHFBasinMount::WallHung && P.ShroudDrop > 0.0)
	{
		const FVector2D ShroudTopHalf(P.Width * 0.34, P.Depth * 0.42);
		const FVector2D ShroudBottomHalf(P.Width * 0.22, P.Depth * 0.26);

		// Set BACK, not centred: a semi-pedestal is against the wall and the bowl overhangs it, in
		// exactly the way a WC pan overhangs its foot.
		const TArray<TArray<FVector2D>> ShroudRings = {
			FHFMeshOps::RoundedRectangle(FVector2D(0.0, P.Depth * 0.5 - ShroudBottomHalf.Y),
				ShroudBottomHalf, FMath::Min(ShroudBottomHalf.X, ShroudBottomHalf.Y) * 0.8,
				LoftCornerSteps),
			FHFMeshOps::RoundedRectangle(FVector2D(0.0, P.Depth * 0.5 - ShroudTopHalf.Y),
				ShroudTopHalf, FMath::Min(ShroudTopHalf.X, ShroudTopHalf.Y) * 0.8, LoftCornerSteps)
		};
		const TArray<double> ShroudHeights = { -P.ShroudDrop, 0.0 };

		FDynamicMesh3 Shroud;
		FHFMeshOps::InitialiseMesh(Shroud);

		if (FHFMeshOps::AppendLoft(Shroud, ShroudRings, ShroudHeights, true, true,
			EHFSurfaceRole::Sanitary))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Shroud);
		}
	}

	// ------------------------------------------------------------------------------------ the tap
	//
	// ON THE LEDGE. See FHFBasinParams: a pillar tap standing on the counter behind the bowl is the
	// other real fitting and it does not fit - the master bathroom's basin leaves 50 mm of stone
	// behind it, and a tap put there stands through the splashback.

	if (P.bHasTap)
	{
		AppendTap(Out.Shell, Out.Parts, P.Tap,
			FVector3d(0.0, P.Depth * 0.5 - P.TapLedgeWidth * 0.5, P.Height));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The shower.
//
// =============================================================================================

FHFShowerParams FHFSanitaryKit::SanitiseShower(const FHFShowerParams& Params)
{
	FHFShowerParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);

	P.MixerHeight = FMath::Clamp(P.MixerHeight, 0.0, FMath::Max(P.Height - 10.0, 0.0));
	P.MixerWidth = FMath::Clamp(P.MixerWidth, 0.0, FMath::Max(P.Width, 0.0));
	P.MixerRadius = FMath::Max(P.MixerRadius, 0.1);
	P.RiserRadius = FMath::Clamp(P.RiserRadius, 0.1, FMath::Max(P.MixerRadius, 0.1));

	// The arm may not reach further than the wet area is deep, or the rose hangs over dry floor -
	// which is a shower that soaks the room it is in.
	P.ArmReach = FMath::Clamp(P.ArmReach, 0.0, FMath::Max(P.Depth - 10.0, 0.0));
	P.RoseDiameter = FMath::Clamp(P.RoseDiameter, 0.0, FMath::Max(P.Width * 0.5, 0.0));

	P.RoseTiltDegrees = FMath::Clamp(P.RoseTiltDegrees, 0.0, 60.0);
	P.LeverSweepDegrees = FMath::Clamp(P.LeverSweepDegrees, 0.0, 170.0);

	P.ThresholdHeight = FMath::Max(P.ThresholdHeight, 0.0);
	P.ThresholdWidth = FMath::Clamp(P.ThresholdWidth, 0.0, FMath::Max(P.Depth * 0.25, 0.0));
	P.GullySize = FMath::Clamp(P.GullySize, 0.0, FMath::Max(FMath::Min(P.Width, P.Depth) * 0.4, 0.0));

	return P;
}

FHFShowerBuild FHFSanitaryKit::BuildShower(const FHFShowerParams& Params)
{
	FHFShowerBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFShowerParams P = SanitiseShower(Params);
	Out.Used = P;

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;

	// ------------------------------------------------------------------------------- the wet area
	//
	// A SHOWER IS A PIECE OF FLOOR BEFORE IT IS A FITTING. Reduced to a rose on a pipe it is a tap
	// hanging over an ordinary bathroom floor, and the corner never reads as a shower at all - which
	// is most of what a 900 x 900 "shower area" on a plan is actually describing.
	//
	// The threshold is on the FRONT edge only. See FHFShowerParams::ThresholdHeight for why not all
	// four: a wet area is bounded on whichever sides happen to be open, and only the generator's
	// caller could know which those are.

	if (P.ThresholdHeight > 0.0 && P.ThresholdWidth > 0.0)
	{
		FDynamicMesh3 Kerb;
		FHFMeshOps::InitialiseMesh(Kerb);

		// Eased on top, which is what a marble threshold is: a square arris on something people step
		// over barefoot is both wrong and the first thing anybody would notice.
		const double Ease = FMath::Min(0.5, FMath::Min(P.ThresholdHeight, P.ThresholdWidth) * 0.4);

		const TArray<FVector2D> Section = {
			FVector2D(0.0, 0.0),
			FVector2D(P.ThresholdWidth, 0.0),
			FVector2D(P.ThresholdWidth, P.ThresholdHeight - Ease),
			FVector2D(P.ThresholdWidth - Ease, P.ThresholdHeight),
			FVector2D(Ease, P.ThresholdHeight),
			FVector2D(0.0, P.ThresholdHeight - Ease)
		};

		if (FHFMeshOps::AppendExtrudedSection(Kerb, Section,
			FVector3d(-P.Width * 0.5, -P.Depth * 0.5, 0.0),
			FVector3d::UnitY(), FVector3d::UnitX(), P.Width, EHFSurfaceRole::CounterStone))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Kerb);
		}
	}

	if (P.GullySize > 2.5)
	{
		// Laid FLUSH. A gully that stands proud is a thing to trip on and reads as a box on the floor;
		// what shows is the frame's top face and the shadow of the slots, which is all a real one is.
		const double GullyHalf = P.GullySize * 0.5;
		const double InnerHalf = GullyHalf - 1.0;

		FDynamicMesh3 Frame;
		FHFMeshOps::InitialiseMesh(Frame);

		TArray<TArray<FVector2D>> Hole;
		Hole.Add(FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector,
			FVector2D(InnerHalf, InnerHalf), 0.3, 2));

		if (FHFMeshOps::AppendPrismWithHoles(Frame,
			FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector, FVector2D(GullyHalf, GullyHalf), 0.3, 2),
			Hole, -0.6, 0.2, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Frame);
		}

		// The grating: bars with the slots between them. Built as bars rather than as a plate with
		// slots cut, because a slot cut through a 2 mm plate is a boolean against a feature thinner
		// than the render finish will keep.
		constexpr int32 Bars = 5;
		const double Pitch = (2.0 * InnerHalf) / Bars;

		for (int32 Bar = 0; Bar < Bars; ++Bar)
		{
			const double CentreY = -InnerHalf + Pitch * (Bar + 0.5);

			FDynamicMesh3 Slat;
			FHFMeshOps::InitialiseMesh(Slat);
			FHFMeshOps::AppendBox(Slat, FVector3d(0.0, CentreY, -0.1),
				FVector3d(InnerHalf, Pitch * 0.28, 0.3), 0.0, EHFSurfaceRole::MetalHardware);
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Slat);
		}
	}

	// ------------------------------------------------------------------------------ the brassware

	const double MixerZ = P.MixerHeight;
	const double MixerY = WallY - P.MixerRadius;

	// The exposed bar, spanning its two wall inlets.
	if (P.MixerWidth > 0.0)
	{
		FDynamicMesh3 Bar;
		FHFMeshOps::InitialiseMesh(Bar);

		const TArray<FVector2D> BarProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.MixerRadius),
			FVector2D(P.MixerWidth, P.MixerRadius),
			FVector2D(P.MixerWidth, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Bar, BarProfile,
			FVector3d(-P.MixerWidth * 0.5, MixerY, MixerZ), FVector3d::UnitX(),
			RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Bar);
		}

		// Escutcheons: the flanges that cover the wall penetrations at each end.
		const double Sides[2] = { -1.0, 1.0 };
		for (const double Side : Sides)
		{
			FDynamicMesh3 Plate;
			FHFMeshOps::InitialiseMesh(Plate);

			const TArray<FVector2D> PlateProfile = {
				FVector2D(0.0, 0.0),
				FVector2D(0.0, P.MixerRadius * 1.7),
				FVector2D(1.2, P.MixerRadius * 1.55),
				FVector2D(1.2, 0.0)
			};

			if (FHFMeshOps::AppendRevolvedProfile(Plate, PlateProfile,
				FVector3d(Side * P.MixerWidth * 0.5, WallY, MixerZ), -FVector3d::UnitY(),
				RevolveSides, EHFSurfaceRole::MetalHardware))
			{
				FHFMeshOps::AppendPreservingRoles(Out.Shell, Plate);
			}
		}
	}

	// The riser, from the bar up to the arm, and the arm out over the standing area.
	const double ArmZ = FMath::Max(P.Height - P.RiserRadius * 1.6, MixerZ);

	{
		FDynamicMesh3 Riser;
		FHFMeshOps::InitialiseMesh(Riser);

		const TArray<FVector2D> RiserProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.RiserRadius),
			FVector2D(ArmZ - MixerZ, P.RiserRadius),
			FVector2D(ArmZ - MixerZ, 0.0)
		};

		if (ArmZ > MixerZ && FHFMeshOps::AppendRevolvedProfile(Riser, RiserProfile,
			FVector3d(0.0, MixerY, MixerZ), FVector3d::UnitZ(), RevolveSides,
			EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Riser);
		}

		FDynamicMesh3 Arm;
		FHFMeshOps::InitialiseMesh(Arm);

		const TArray<FVector2D> ArmProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.RiserRadius * 1.6),
			FVector2D(P.ArmReach, P.RiserRadius * 1.35),
			FVector2D(P.ArmReach, 0.0)
		};

		if (P.ArmReach > 0.0 && FHFMeshOps::AppendRevolvedProfile(Arm, ArmProfile,
			FVector3d(0.0, MixerY, ArmZ), -FVector3d::UnitY(), RevolveSides,
			EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::AppendPreservingRoles(Out.Shell, Arm);
		}
	}

	// ---------------------------------------------------------------------------------- the lever
	//
	// On the near end of the bar, turning about the bar's own axis - which is where a bar mixer's
	// control is and how it moves.

	if (P.LeverSweepDegrees > 0.0 && P.MixerWidth > 0.0)
	{
		FHFMeshPart Lever;
		Lever.PartId = MixerLeverPartId();
		FHFMeshOps::InitialiseMesh(Lever.Mesh);

		const double LeverLength = FMath::Max(P.MixerRadius * 2.6, 3.0);

		const TArray<FVector2D> LeverProfile = {
			FVector2D(0.0, 0.0),
			FVector2D(0.0, P.MixerRadius * 0.95),
			FVector2D(LeverLength * 0.35, P.MixerRadius * 0.8),
			FVector2D(LeverLength, P.MixerRadius * 0.45),
			FVector2D(LeverLength, 0.0)
		};

		// Pointing DOWN when shut, which is where a mixer's handle rests, and turning up from there.
		if (FHFMeshOps::AppendRevolvedProfile(Lever.Mesh, LeverProfile,
			FVector3d::ZeroVector, -FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::ApplyWorldScaleUVs(Lever.Mesh);

			Lever.PivotTransform = FTransform(
				FVector(-P.MixerWidth * 0.5 + P.MixerRadius * 1.2, MixerY, MixerZ));
			Lever.Motion.Type = EHFMotionType::Hinge;
			Lever.Motion.Axis = FVector::XAxisVector;
			Lever.Motion.MaxAngleDegrees = P.LeverSweepDegrees;
			Lever.DefaultOpenAmount = 0.0;

			Out.Parts.Add(MoveTemp(Lever));
		}
	}

	// ----------------------------------------------------------------------------------- the rose
	//
	// ITS OWN PART, TILTING ON ITS BALL. Every overhead rose sold is on a ball joint precisely so the
	// spray can be aimed, and one moulded solid into its arm is the same kind of tell as a hob with
	// its knobs cast into the fascia.

	const double BallY = MixerY - P.ArmReach;

	if (P.RoseDiameter > 0.0)
	{
		const double RoseRadius = P.RoseDiameter * 0.5;
		const double BallRadius = FMath::Max(P.RiserRadius * 1.35, 0.5);
		const double FaceDrop = BallRadius * 2.2 + 2.0;

		FHFMeshPart Rose;
		Rose.PartId = RosePartId();
		FHFMeshOps::InitialiseMesh(Rose.Mesh);

		// Drawn about the ball at its own origin: the ball, a short neck, and the head flaring out to
		// a flat spray face below it. The profile runs DOWN the +Z axis, so its first coordinate is
		// negative going away from the joint.
		const TArray<FVector2D> RoseProfile = {
			FVector2D(BallRadius, 0.0),
			FVector2D(BallRadius * 0.55, BallRadius * 0.85),
			FVector2D(0.0, BallRadius),
			FVector2D(-BallRadius * 0.7, BallRadius * 0.75),
			FVector2D(-BallRadius * 1.6, BallRadius * 0.7),
			FVector2D(-BallRadius * 2.2, RoseRadius * 0.8),
			FVector2D(-FaceDrop + 0.8, RoseRadius),
			FVector2D(-FaceDrop, RoseRadius),
			FVector2D(-FaceDrop, 0.0)
		};

		if (FHFMeshOps::AppendRevolvedProfile(Rose.Mesh, RoseProfile, FVector3d::ZeroVector,
			FVector3d::UnitZ(), RevolveSides, EHFSurfaceRole::MetalHardware))
		{
			FHFMeshOps::ApplyWorldScaleUVs(Rose.Mesh);

			Rose.PivotTransform = FTransform(FVector(0.0, BallY, ArmZ));

			// About the axis ACROSS the shower, so the spray aims forward into the room or back at the
			// wall - which is the direction a rose is actually adjusted in. Tilting sideways is the one
			// adjustment nobody makes.
			Rose.Motion.Type = EHFMotionType::Hinge;
			Rose.Motion.Axis = FVector::XAxisVector;
			Rose.Motion.MaxAngleDegrees = P.RoseTiltDegrees;
			Rose.DefaultOpenAmount = 0.0;

			Out.RoseCentre = FVector(0.0, BallY, ArmZ - FaceDrop);

			Out.Parts.Add(MoveTemp(Rose));
		}
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
