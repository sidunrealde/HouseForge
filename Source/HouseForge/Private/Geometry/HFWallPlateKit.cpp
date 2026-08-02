// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFWallPlateKit.h"

#include "DynamicMesh/MeshTransforms.h"
#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/** Corner steps a lofted plate's outline is drawn at. */
	constexpr int32 PlateCornerSteps = 1;

	/** Sides a pin hole's outline is drawn with. Eight is round at four millimetres. */
	constexpr int32 PinHoleSides = 8;

	/** A closed rectangle in the plane, counter-clockwise. */
	TArray<FVector2D> PlanRect(const FVector2D& Centre, const FVector2D& HalfExtents)
	{
		return {
			FVector2D(Centre.X - HalfExtents.X, Centre.Y - HalfExtents.Y),
			FVector2D(Centre.X + HalfExtents.X, Centre.Y - HalfExtents.Y),
			FVector2D(Centre.X + HalfExtents.X, Centre.Y + HalfExtents.Y),
			FVector2D(Centre.X - HalfExtents.X, Centre.Y + HalfExtents.Y)
		};
	}

	/** A closed regular polygon, clockwise - the winding a hole in a prism needs. */
	TArray<FVector2D> HoleCircle(const FVector2D& Centre, double Radius, int32 Sides)
	{
		TArray<FVector2D> Out;
		Out.Reserve(Sides);

		for (int32 Index = 0; Index < Sides; ++Index)
		{
			const double Angle = -2.0 * PI * static_cast<double>(Index) / static_cast<double>(Sides);
			Out.Add(Centre + FVector2D(FMath::Cos(Angle), FMath::Sin(Angle)) * Radius);
		}

		return Out;
	}

	/**
	 * Turns a slab built in the XY plane and extruded up +Z into one standing on a wall.
	 *
	 * A quarter turn about +X takes (x, y, z) to (x, -z, y): the section's own up axis becomes world
	 * up, and the EXTRUSION runs out of the wall along -Y. Built the other way round it would need a
	 * second prism primitive whose only difference is an axis - the same trade FHFWallPlateKit already
	 * makes for the mirror's bevelled glass.
	 *
	 * The result spans Y from 0 back to -Thickness, so BackY is where its rear face lands.
	 */
	void StandOnWall(FDynamicMesh3& Mesh, double CentreX, double BackY, double BottomZ)
	{
		const FTransformSRT3d ToWall(FQuaterniond(FVector3d::UnitX(), 90.0, /*bAngleIsDegrees*/ true),
			FVector3d(CentreX, BackY, BottomZ), FVector3d::One());

		MeshTransforms::ApplyTransform(Mesh, ToWall, /*bReverseOrientationIfNeeded*/ true);
	}
}

FHFMirrorParams FHFWallPlateKit::SanitiseMirror(const FHFMirrorParams& Params)
{
	FHFMirrorParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);

	// The glass may not be thicker than the whole build-up. Everything the glass does not take is the
	// backing - see FHFMirrorParams::BackingThickness - so this one clamp settles both.
	P.GlassThickness = FMath::Clamp(P.GlassThickness, 0.2, FMath::Max(P.Depth, 0.2));

	// A bevel wider than a quarter of the plate is not a bevel, it is a pyramid.
	P.BevelWidth = FMath::Clamp(P.BevelWidth, 0.0, FMath::Min(P.Width, P.Height) * 0.25);

	return P;
}

FHFWallPlateBuild FHFWallPlateKit::BuildMirror(const FHFMirrorParams& Params)
{
	FHFWallPlateBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFMirrorParams P = SanitiseMirror(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;

	// ---------------------------------------------------------------------------- the backing board
	//
	// What holds the glass off the plaster. Held IN from the glass on every side so it is invisible
	// from in front - a backing flush with the mirror would show as a dark line round the edge from
	// any oblique view, which is the one view a bathroom mirror is always seen from.

	if (P.BackingThickness() > 0.0)
	{
		const double Inset = FMath::Min(1.5, FMath::Min(P.Width, P.Height) * 0.08);

		FDynamicMesh3 Backing;
		FHFMeshOps::InitialiseMesh(Backing);

		FHFMeshOps::AppendBox(Backing,
			FVector3d(0.0, WallY - P.BackingThickness() * 0.5, P.Height * 0.5),
			FVector3d(P.Width * 0.5 - Inset, P.BackingThickness() * 0.5, P.Height * 0.5 - Inset),
			0.0, EHFSurfaceRole::JoineryCarcass);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Backing);
	}

	// ------------------------------------------------------------------------------------ the glass
	//
	// A LOFT, NOT A BOX. See FHFMirrorParams: the bevel is the only thing about a frameless mirror
	// that catches light, and without it the plate renders as a grey rectangle that reads as a hole in
	// the wall rather than as an object on it.
	//
	// Built with its sections stacked up +Z - which is the only way FHFMeshOps::AppendLoft stacks
	// them - and then TURNED so the stack runs out of the wall. Building it the other way round would
	// mean a second loft primitive whose only difference is an axis.

	{
		const double GlassBackY = WallY - P.BackingThickness();

		const TArray<TArray<FVector2D>> Rings = {
			FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector,
				FVector2D(P.Width * 0.5, P.Height * 0.5), 0.0, PlateCornerSteps),
			FHFMeshOps::RoundedRectangle(FVector2D::ZeroVector,
				FVector2D(FMath::Max(P.Width * 0.5 - P.BevelWidth, 0.01),
					FMath::Max(P.Height * 0.5 - P.BevelWidth, 0.01)), 0.0, PlateCornerSteps)
		};

		const TArray<double> Heights = { 0.0, P.GlassThickness };

		FDynamicMesh3 Glass;
		FHFMeshOps::InitialiseMesh(Glass);

		if (!FHFMeshOps::AppendLoft(Glass, Rings, Heights, true, true, EHFSurfaceRole::Glass))
		{
			return Out;
		}

		// Stack axis +Z becomes -Y (out of the wall), and the section's own +Y becomes +Z (up the
		// wall). A quarter turn about +X does exactly that, and the translation puts the plate's back
		// on the wall face with its bottom edge on the origin.
		const FTransformSRT3d ToWall(FQuaterniond(FVector3d::UnitX(), 90.0, /*bAngleIsDegrees*/ true),
			FVector3d(0.0, GlassBackY, P.Height * 0.5), FVector3d::One());

		MeshTransforms::ApplyTransform(Glass, ToWall, /*bReverseOrientationIfNeeded*/ true);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Glass);
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The modular accessories: eight sockets and five switch plates.
//
// =============================================================================================

FName FHFWallPlateKit::RockerPartId(int32 Index)
{
	return FName(*FString::Printf(TEXT("Rocker%d"), FMath::Max(Index, 0)));
}

FHFAccessoryPlateParams FHFWallPlateKit::SanitiseAccessoryPlate(const FHFAccessoryPlateParams& Params)
{
	FHFAccessoryPlateParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);
	P.GangCount = FMath::Clamp(P.GangCount, 1, 12);

	// A border wider than a third of the plate leaves no window to clip anything into.
	P.PlateBorder = FMath::Clamp(P.PlateBorder, 0.0, FMath::Min(P.Width, P.Height) / 3.0);

	// THE WHOLE BUILD-UP IS THE DRAWN DEPTH, and the three layers have to divide it rather than add
	// up past it. The grid on the plaster, then the recess the module faces sit behind, then the
	// rocker's own stand-off inside that.
	P.GridThickness = FMath::Clamp(P.GridThickness, 0.0, P.Depth * 0.4);
	P.ModuleRecess = FMath::Clamp(P.ModuleRecess, 0.0, FMath::Max(P.Depth - P.GridThickness, 0.0));
	P.RockerProud = FMath::Clamp(P.RockerProud, 0.0, P.ModuleRecess);

	P.ModuleHeight = FMath::Clamp(P.ModuleHeight, 0.0, P.ApertureHeight());
	P.RockerThrowDegrees = FMath::Clamp(P.RockerThrowDegrees, 0.0, 45.0);

	return P;
}

FHFWallPlateBuild FHFWallPlateKit::BuildAccessoryPlate(const FHFAccessoryPlateParams& Params)
{
	FHFWallPlateBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFAccessoryPlateParams P = SanitiseAccessoryPlate(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;
	const double GridFrontY = WallY - P.GridThickness;
	const double CoverFrontY = -P.Depth * 0.5;

	// Where a module's own face sits: behind the cover by the recess, which is the shadow line that
	// separates a rocker from the plate it stands in.
	const double ModuleFaceY = CoverFrontY + P.ModuleRecess;

	const double ApertureW = P.ApertureWidth();
	const double ApertureH = P.ApertureHeight();
	const double CentreZ = P.Height * 0.5;

	// ------------------------------------------------------------------------------- the grid plate
	//
	// Screwed to the back box, and what everything else stands off. Full size, because the cover only
	// covers it - a grid held in from the cover's edge would show as a dark line all the way round.

	if (P.GridThickness > 0.0)
	{
		FDynamicMesh3 Grid;
		FHFMeshOps::InitialiseMesh(Grid);

		FHFMeshOps::AppendBox(Grid, FVector3d(0.0, WallY - P.GridThickness * 0.5, CentreZ),
			FVector3d(P.Width * 0.5, P.GridThickness * 0.5, P.Height * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Grid);
	}

	// ----------------------------------------------------------------------------------- the cover
	//
	// FOUR RAILS ROUND A REAL HOLE, not a slab with a picture of one on it. The window is what makes
	// the fitting read: it is the only place on a switch plate where light gets behind a face, and a
	// cover built solid leaves the modules sitting on top of the plate rather than through it.

	const double CoverBackY = GridFrontY;
	const double CoverCentreY = (CoverBackY + CoverFrontY) * 0.5;
	const double CoverHalfDepth = (CoverBackY - CoverFrontY) * 0.5;

	if (CoverHalfDepth > 0.0 && P.PlateBorder > 0.0)
	{
		FDynamicMesh3 Cover;
		FHFMeshOps::InitialiseMesh(Cover);

		// Top and bottom rails run the full width; the sides fill only what is left between them, so
		// the four meet at the corners rather than overlapping into a double thickness there.
		FHFMeshOps::AppendBox(Cover,
			FVector3d(0.0, CoverCentreY, P.Height - P.PlateBorder * 0.5),
			FVector3d(P.Width * 0.5, CoverHalfDepth, P.PlateBorder * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendBox(Cover,
			FVector3d(0.0, CoverCentreY, P.PlateBorder * 0.5),
			FVector3d(P.Width * 0.5, CoverHalfDepth, P.PlateBorder * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		for (const double Side : { -1.0, 1.0 })
		{
			FHFMeshOps::AppendBox(Cover,
				FVector3d(Side * (P.Width - P.PlateBorder) * 0.5, CoverCentreY, CentreZ),
				FVector3d(P.PlateBorder * 0.5, CoverHalfDepth, ApertureH * 0.5), 0.0,
				EHFSurfaceRole::Appliance);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Cover);
	}

	// ------------------------------------------------------------------------------ the blanked grid
	//
	// A MODULE IS 45 MM TALL AND THIS PLATE IS 150, so most of the window is blanked off - which is
	// what a real one looks like, and what stops a six gang plate coming out as six full-height
	// paddles. Filled flush with the module faces, so the rockers stand proud of something rather
	// than floating in a hole.

	const double ModuleH = FMath::Min(P.ModuleHeight > 0.0 ? P.ModuleHeight : ApertureH, ApertureH);

	if (GridFrontY > ModuleFaceY)
	{
		const double BlankDepth = GridFrontY - ModuleFaceY;

		FDynamicMesh3 Blank;
		FHFMeshOps::InitialiseMesh(Blank);

		FHFMeshOps::AppendBox(Blank,
			FVector3d(0.0, ModuleFaceY + BlankDepth * 0.5, CentreZ),
			FVector3d(ApertureW * 0.5, BlankDepth * 0.5, ApertureH * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Blank);
	}

	// --------------------------------------------------------------------------------- the modules
	//
	// One gang at a time, left to right. A switch gang is a rocker; a socket gang is an outlet face
	// with its own rocker beside it, which is what a switched socket is and what makes the eight
	// sockets in this flat different objects from the five switch plates rather than a relabelling.

	const double Pitch = ApertureW / static_cast<double>(P.GangCount);

	// How far a rocker may swing before its lower edge breaks the plane of the cover. DERIVED rather
	// than trusted: the drawn depth is the whole build-up and nothing may stand past it, so the throw
	// answers to the geometry instead of the geometry answering to a figure somebody typed.
	const double SwingClearance = FMath::Max(P.ModuleRecess - P.RockerProud, 0.0);
	const double MaxThrowDegrees = ModuleH > 0.0
		? FMath::RadiansToDegrees(FMath::Asin(FMath::Clamp(SwingClearance / (ModuleH * 0.5), 0.0, 1.0)))
		: 0.0;
	const double ThrowDegrees = FMath::Min(P.RockerThrowDegrees, MaxThrowDegrees);

	for (int32 Gang = 0; Gang < P.GangCount; ++Gang)
	{
		const double GangCentreX = -ApertureW * 0.5 + (static_cast<double>(Gang) + 0.5) * Pitch;

		// The shadow gap between one module and the next. Small, and the only thing separating a row
		// of rockers from one long bar.
		const double Gap = FMath::Min(0.3, Pitch * 0.12);
		const double GangWidth = FMath::Max(Pitch - Gap, 0.0);

		double RockerWidth = GangWidth;
		double RockerCentreX = GangCentreX;

		// ------------------------------------------------------------------------- the outlet face
		if (P.Kind == EHFAccessoryKind::Socket && GangWidth > 2.4 && ModuleH > 0.0)
		{
			// A 6/16 A socket is two modules and its switch is one, so the outlet takes about two
			// thirds of the gang and the rocker the rest.
			const double OutletWidth = GangWidth * 0.62;
			RockerWidth = FMath::Max(GangWidth - OutletWidth - Gap, 0.0);
			RockerCentreX = GangCentreX + GangWidth * 0.5 - RockerWidth * 0.5;

			const double OutletCentreX = GangCentreX - GangWidth * 0.5 + OutletWidth * 0.5;

			// THE PINS ARE REAL HOLES. Three dark dots drawn on a face read as three dark dots; a
			// perforated plate over a cavity is the only version that catches a shadow, and a socket
			// with no shadow in it is the most obvious placeholder there is on a wall.
			const double FaceThickness = FMath::Max(FMath::Min(0.35, P.RockerProud), 0.1);
			const double PinRadius = FMath::Min(0.28, OutletWidth * 0.075);
			const double EarthRadius = PinRadius * 1.25;
			const double PinSpread = FMath::Min(OutletWidth * 0.24, ModuleH * 0.24);

			TArray<TArray<FVector2D>> Holes;
			Holes.Add(HoleCircle(FVector2D(-PinSpread, -ModuleH * 0.16), PinRadius, PinHoleSides));
			Holes.Add(HoleCircle(FVector2D(PinSpread, -ModuleH * 0.16), PinRadius, PinHoleSides));
			Holes.Add(HoleCircle(FVector2D(0.0, ModuleH * 0.20), EarthRadius, PinHoleSides));

			FDynamicMesh3 Face;
			FHFMeshOps::InitialiseMesh(Face);

			if (FHFMeshOps::AppendPrismWithHoles(Face,
				PlanRect(FVector2D::ZeroVector, FVector2D(OutletWidth * 0.5, ModuleH * 0.5)),
				Holes, 0.0, FaceThickness, EHFSurfaceRole::Appliance))
			{
				StandOnWall(Face, OutletCentreX, ModuleFaceY - P.RockerProud + FaceThickness,
					CentreZ);

				FHFMeshOps::AppendPreservingRoles(Out.Shell, Face);
			}

			// The body behind it, as a RIM rather than a block, so the pin holes look into a cavity
			// instead of onto a wall of plastic three millimetres behind them.
			const double BodyBackY = ModuleFaceY;
			const double BodyFrontY = ModuleFaceY - P.RockerProud + FaceThickness;

			if (BodyBackY > BodyFrontY)
			{
				FDynamicMesh3 Body;
				FHFMeshOps::InitialiseMesh(Body);

				const double RimCentreY = (BodyBackY + BodyFrontY) * 0.5;
				const double RimHalfDepth = (BodyBackY - BodyFrontY) * 0.5;
				const double RimWidth = FMath::Min(0.35, OutletWidth * 0.15);

				for (const double Side : { -1.0, 1.0 })
				{
					FHFMeshOps::AppendBox(Body,
						FVector3d(OutletCentreX + Side * (OutletWidth - RimWidth) * 0.5, RimCentreY,
							CentreZ),
						FVector3d(RimWidth * 0.5, RimHalfDepth, ModuleH * 0.5), 0.0,
						EHFSurfaceRole::Appliance);

					FHFMeshOps::AppendBox(Body,
						FVector3d(OutletCentreX, RimCentreY,
							CentreZ + Side * (ModuleH - RimWidth) * 0.5),
						FVector3d(FMath::Max(OutletWidth * 0.5 - RimWidth, 0.01), RimHalfDepth,
							RimWidth * 0.5), 0.0,
						EHFSurfaceRole::Appliance);
				}

				FHFMeshOps::AppendPreservingRoles(Out.Shell, Body);
			}
		}

		// ------------------------------------------------------------------------------ the rocker
		if (RockerWidth <= 0.0 || ModuleH <= 0.0)
		{
			continue;
		}

		FHFMeshPart Rocker;
		Rocker.PartId = RockerPartId(Gang);
		FHFMeshOps::InitialiseMesh(Rocker.Mesh);

		// Drawn about its own pivot, which is the axis through its middle ON the blanking plane. A
		// rocker is a see-saw and its face is all anybody sees; the body behind the pivot is buried in
		// the module below it and is what stops the swing showing daylight through the plate.
		const double BodyDepth = FMath::Max(P.ModuleRecess * 0.8, 0.2);

		FHFMeshOps::AppendBox(Rocker.Mesh,
			FVector3d(0.0, (BodyDepth - P.RockerProud) * 0.5, 0.0),
			FVector3d(RockerWidth * 0.5, (BodyDepth + P.RockerProud) * 0.5, ModuleH * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::ApplyWorldScaleUVs(Rocker.Mesh);

		Rocker.PivotTransform = FTransform(FVector(RockerCentreX, ModuleFaceY, CentreZ));

		Rocker.Motion.Type = EHFMotionType::Hinge;
		Rocker.Motion.Axis = FVector::XAxisVector;

		// NEGATIVE, SO THE TOP GOES IN AS THE BOTTOM COMES OUT. A rotation about +X carries a point
		// above the axis towards -Y, which is out of the wall - a switch lifting at the top while it
		// is pressed at the bottom. The other sign is the one a finger actually applies, and the
		// OPPOSITION is the whole test of it: a rocker that translated would travel just as far.
		Rocker.Motion.MaxAngleDegrees = -ThrowDegrees;
		Rocker.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Rocker));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}

// =============================================================================================
//
// The consumer unit.
//
// =============================================================================================

FName FHFWallPlateKit::BreakerPartId(int32 Index)
{
	return FName(*FString::Printf(TEXT("Breaker%d"), FMath::Max(Index, 0)));
}

FHFDistributionBoardParams FHFWallPlateKit::SanitiseDistributionBoard(
	const FHFDistributionBoardParams& Params)
{
	FHFDistributionBoardParams P = Params;

	P.Width = FMath::Max(P.Width, 0.0);
	P.Height = FMath::Max(P.Height, 0.0);
	P.Depth = FMath::Max(P.Depth, 0.0);

	P.DoorThickness = FMath::Clamp(P.DoorThickness, 0.0, P.Depth * 0.35);
	P.ModulePitch = FMath::Max(P.ModulePitch, 0.1);

	// Only the ways the enclosure can actually hold. A board asked for more than it has rail for is a
	// board with breakers hanging out of both ends, and the honest answer is the rail it has.
	const int32 MaxWays = FMath::Max(FMath::FloorToInt32((P.Width * 0.82) / P.ModulePitch), 0);
	P.WayCount = FMath::Clamp(P.WayCount, 0, FMath::Min(MaxWays, 24));

	P.DoorSwingDegrees = FMath::Clamp(P.DoorSwingDegrees, 0.0, 170.0);
	P.ToggleThrowDegrees = FMath::Clamp(P.ToggleThrowDegrees, 0.0, 60.0);

	return P;
}

FHFWallPlateBuild FHFWallPlateKit::BuildDistributionBoard(const FHFDistributionBoardParams& Params)
{
	FHFWallPlateBuild Out;
	FHFMeshOps::InitialiseMesh(Out.Shell);

	const FHFDistributionBoardParams P = SanitiseDistributionBoard(Params);

	if (!P.IsValid())
	{
		return Out;
	}

	const double WallY = P.Depth * 0.5;
	const double FrontY = -P.Depth * 0.5;
	const double CaseFrontY = FrontY + P.DoorThickness;
	const double CentreZ = P.Height * 0.5;

	// Thickness of the pressed enclosure's own metal.
	const double CaseWall = FMath::Min(0.5, P.Depth * 0.12);

	// ------------------------------------------------------------------------------- the enclosure
	//
	// A TRAY, NOT A BLOCK. The whole point of a consumer unit is that its door opens onto something,
	// so the box has to be hollow: a back, four returns, and the breakers standing in it.

	{
		FDynamicMesh3 Case;
		FHFMeshOps::InitialiseMesh(Case);

		FHFMeshOps::AppendBox(Case, FVector3d(0.0, WallY - CaseWall * 0.5, CentreZ),
			FVector3d(P.Width * 0.5, CaseWall * 0.5, P.Height * 0.5), 0.0, EHFSurfaceRole::Appliance);

		const double SideBackY = WallY - CaseWall;
		const double SideCentreY = (SideBackY + CaseFrontY) * 0.5;
		const double SideHalfDepth = FMath::Max((SideBackY - CaseFrontY) * 0.5, 0.01);

		for (const double Side : { -1.0, 1.0 })
		{
			FHFMeshOps::AppendBox(Case,
				FVector3d(Side * (P.Width - CaseWall) * 0.5, SideCentreY, CentreZ),
				FVector3d(CaseWall * 0.5, SideHalfDepth, P.Height * 0.5), 0.0,
				EHFSurfaceRole::Appliance);

			FHFMeshOps::AppendBox(Case,
				FVector3d(0.0, SideCentreY, CentreZ + Side * (P.Height - CaseWall) * 0.5),
				FVector3d(FMath::Max(P.Width * 0.5 - CaseWall, 0.01), SideHalfDepth, CaseWall * 0.5),
				0.0, EHFSurfaceRole::Appliance);
		}

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Case);
	}

	// -------------------------------------------------------------------------- the rail and its ways
	//
	// Breakers on a DIN rail at 17.5 mm centres, CENTRED with the rail running on past them at both
	// ends. That is what a populated board looks like: the ways that are used, and bare rail where the
	// spare ways are.

	const double RailZ = CentreZ;
	const double BreakerBackY = WallY - CaseWall;
	const double BreakerHeight = FMath::Min(8.0, P.Height * 0.32);

	// ------------------------------------------------------ THE TOGGLES HAVE TO FIT BEHIND THE DOOR
	//
	// The breaker's body and the toggle standing out of it share the enclosure's depth, and the whole
	// lot has to end BEHIND the door - a toggle that reaches the door plane passes straight through
	// the glazing, and the board comes out with a row of metal tabs poking out of its front.
	//
	// It did. Built to the interior depth with the tab added on top, the toggles stood 2 mm past the
	// drawn box: nothing in the kit tests saw it, because a part is measured against the fitting and
	// not against the leaf in front of it, and the flat reported the board standing 6.2 cm proud of a
	// drawn 6.0. So the depth is DIVIDED rather than filled, and the tab gets its share first.
	const double InteriorDepth = FMath::Max(BreakerBackY - CaseFrontY, 0.2);
	const double TabLength = FMath::Min(1.1, InteriorDepth * 0.25);
	const double TabHeight = FMath::Min(BreakerHeight * 0.30, 1.6);

	// AND THE TOGGLE REACHES FURTHEST WHEN IT IS THROWN, not when it is upright. A tab rotating about
	// its own root swings its far CORNER round on a radius, and the corner is further from the pivot
	// than the face is: at 26 degrees a 11 x 16 mm tab reaches 2.4 mm further forward than its own
	// length. Allowing for the length alone left the toggles poking through the door by exactly that,
	// which is the second version of the same mistake. The radius covers it at any angle.
	const double ToggleReach = FMath::Sqrt(TabLength * TabLength + TabHeight * TabHeight * 0.25);

	// A shadow gap between the thrown toggle and the back of the door, so the two never touch.
	const double DoorClearance = FMath::Min(0.2, InteriorDepth * 0.05);

	const double BreakerDepth = FMath::Max(InteriorDepth - ToggleReach - DoorClearance, 0.2);
	const double BreakerFrontY = BreakerBackY - BreakerDepth;

	{
		FDynamicMesh3 Rail;
		FHFMeshOps::InitialiseMesh(Rail);

		FHFMeshOps::AppendBox(Rail,
			FVector3d(0.0, BreakerBackY - 0.4, RailZ),
			FVector3d(FMath::Max(P.Width * 0.5 - CaseWall - 0.4, 0.01), 0.4, 0.35), 0.0,
			EHFSurfaceRole::MetalHardware);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Rail);
	}

	const double BankWidth = P.WayCount * P.ModulePitch;

	for (int32 Way = 0; Way < P.WayCount; ++Way)
	{
		const double WayCentreX = -BankWidth * 0.5 + (static_cast<double>(Way) + 0.5) * P.ModulePitch;

		FDynamicMesh3 Breaker;
		FHFMeshOps::InitialiseMesh(Breaker);

		// The moulded body, held in from its neighbours by a hair so the bank reads as separate
		// devices rather than as one extruded strip.
		FHFMeshOps::AppendBox(Breaker,
			FVector3d(WayCentreX, (BreakerBackY + BreakerFrontY) * 0.5, RailZ),
			FVector3d(P.ModulePitch * 0.46, BreakerDepth * 0.5, BreakerHeight * 0.5), 0.0,
			EHFSurfaceRole::Appliance);

		FHFMeshOps::AppendPreservingRoles(Out.Shell, Breaker);

		// ------------------------------------------------------------------------------ its toggle
		//
		// SEQUENCED AFTER THE DOOR, because a breaker cannot be thrown through a shut one. The same
		// relationship the WC's seat has with its lid, expressed the same way: an ORDERING between two
		// independent parts rather than a linkage - the toggle is screwed to the breaker, not to the
		// door, and parenting it to the door would swing every breaker out of the board with it.

		FHFMeshPart Toggle;
		Toggle.PartId = BreakerPartId(Way);
		FHFMeshOps::InitialiseMesh(Toggle.Mesh);

		// Drawn reaching FORWARD out of its own pivot, which is the axis it rocks about at the front
		// face of the body it is screwed to.
		FHFMeshOps::AppendBox(Toggle.Mesh,
			FVector3d(0.0, -TabLength * 0.5, 0.0),
			FVector3d(P.ModulePitch * 0.24, TabLength * 0.5, TabHeight * 0.5), 0.0,
			EHFSurfaceRole::MetalHardware);

		FHFMeshOps::ApplyWorldScaleUVs(Toggle.Mesh);

		Toggle.PivotTransform = FTransform(FVector(WayCentreX, BreakerFrontY, RailZ));

		Toggle.Motion.Type = EHFMotionType::Hinge;
		Toggle.Motion.Axis = FVector::XAxisVector;

		// POSITIVE, SO THE TAB DROPS. A rotation about +X carries a forward-reaching tab downwards,
		// which is the way a breaker throws to off and what anybody looking at a tripped board expects
		// to see.
		Toggle.Motion.MaxAngleDegrees = P.ToggleThrowDegrees;

		Toggle.Motion.SequencedAfterPartId = DoorPartId();
		Toggle.Motion.SequenceThreshold = 0.35;
		Toggle.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Toggle));
	}

	// ----------------------------------------------------------------------------------- the door
	//
	// Side-hung on the left jamb, with a glazed centre so the breakers still read through it shut -
	// which is the state a consumer unit is in for every second of its life.

	if (P.DoorThickness > 0.0)
	{
		FHFMeshPart Door;
		Door.PartId = DoorPartId();
		FHFMeshOps::InitialiseMesh(Door.Mesh);

		const double DoorWidth = P.Width;
		const double Stile = FMath::Min(2.2, DoorWidth * 0.16);
		const double Rail = FMath::Min(2.6, P.Height * 0.12);

		// Drawn from its own hinge, which is the LEFT edge: local X runs 0 to the full width, and Z is
		// measured from the middle of the door so the pivot sits at mid height.
		for (const double Side : { 0.0, 1.0 })
		{
			FHFMeshOps::AppendBox(Door.Mesh,
				FVector3d(Side * (DoorWidth - Stile) + Stile * 0.5, -P.DoorThickness * 0.5, 0.0),
				FVector3d(Stile * 0.5, P.DoorThickness * 0.5, P.Height * 0.5), 0.0,
				EHFSurfaceRole::Appliance);

			FHFMeshOps::AppendBox(Door.Mesh,
				FVector3d(DoorWidth * 0.5, -P.DoorThickness * 0.5,
					(Side * 2.0 - 1.0) * (P.Height - Rail) * 0.5),
				FVector3d(FMath::Max(DoorWidth * 0.5 - Stile, 0.01), P.DoorThickness * 0.5,
					Rail * 0.5), 0.0, EHFSurfaceRole::Appliance);
		}

		const double GlazeWidth = FMath::Max(DoorWidth - 2.0 * Stile, 0.0);
		const double GlazeHeight = FMath::Max(P.Height - 2.0 * Rail, 0.0);

		if (GlazeWidth > 0.0 && GlazeHeight > 0.0)
		{
			FDynamicMesh3 Glaze;
			FHFMeshOps::InitialiseMesh(Glaze);

			FHFMeshOps::AppendBox(Glaze,
				FVector3d(DoorWidth * 0.5, -P.DoorThickness * 0.35, 0.0),
				FVector3d(GlazeWidth * 0.5, P.DoorThickness * 0.25, GlazeHeight * 0.5), 0.0,
				EHFSurfaceRole::Glass);

			FHFMeshOps::AppendPreservingRoles(Door.Mesh, Glaze);
		}

		FHFMeshOps::ApplyWorldScaleUVs(Door.Mesh);

		// The hinge line: the left edge of the enclosure, at its front, at mid height.
		Door.PivotTransform = FTransform(FVector(-P.Width * 0.5, CaseFrontY, CentreZ));

		Door.Motion.Type = EHFMotionType::Hinge;
		Door.Motion.Axis = FVector::ZAxisVector;

		// NEGATIVE, SO THE FREE EDGE COMES OUT OF THE WALL. A rotation about +Z carries the door's own
		// +X towards +Y, which is INTO the plaster; the other sign swings it into the room, which is
		// the only direction there is anything for it to swing into.
		Door.Motion.MaxAngleDegrees = -P.DoorSwingDegrees;
		Door.DefaultOpenAmount = 0.0;

		Out.Parts.Add(MoveTemp(Door));
	}

	FHFMeshOps::ApplyWorldScaleUVs(Out.Shell);

	Out.bValid = Out.Shell.TriangleCount() > 0;
	return Out;
}
