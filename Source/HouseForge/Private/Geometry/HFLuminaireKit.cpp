// Copyright Siddartha G. All Rights Reserved.

#include "Geometry/HFLuminaireKit.h"

#include "Geometry/HFMeshOps.h"

using namespace UE::Geometry;

namespace
{
	/**
	 * The smallest stock this kit will build anything out of.
	 *
	 * A revolve of a zero-radius interior point pinches the solid instead of closing it - the
	 * header of AppendRevolvedProfile says so - and a body of zero depth is two coincident caps,
	 * which is the coplanar pair the lens construction exists to avoid. Both are reachable from a
	 * spec that leaves a figure at its default zero, so both are floored rather than trusted.
	 */
	constexpr double MinStock = 0.4;
}

FHFLuminaireParams FHFLuminaireKit::Sanitise(const FHFLuminaireParams& In)
{
	FHFLuminaireParams P = In;

	P.Diameter = FMath::Max(P.Diameter, MinStock * 4.0);
	P.BodyHeight = FMath::Max(P.BodyHeight, MinStock * 2.0);
	P.DropLength = FMath::Max(P.DropLength, 0.0);

	// The canopy cannot be wider than the body it feeds, or the fitting reads as a plate with a
	// lamp hanging under it. Nor deeper than the drop it has to fit inside - a canopy longer than
	// the flex would put the body inside the ceiling.
	P.CanopyDiameter = FMath::Clamp(P.CanopyDiameter, MinStock * 4.0, P.Diameter);
	P.CanopyHeight = FMath::Clamp(P.CanopyHeight, MinStock, FMath::Max(P.DropLength * 0.5, MinStock));

	P.FlexDiameter = FMath::Clamp(P.FlexDiameter, MinStock, P.CanopyDiameter * 0.5);

	// Floored at a real value rather than at zero: see FHFLuminaireParams::LensProud.
	P.LensProud = FMath::Clamp(P.LensProud, 0.1, P.BodyHeight);

	// Bounded strictly below 1 as well as above 0.1. At exactly 1 the dome base and the rim are the
	// same circle, the bezel disappears, and the dome skin leaves the rim tangentially - which is
	// the coplanar case again, arrived at through the fraction rather than through the proudness.
	P.LensFraction = FMath::Clamp(P.LensFraction, 0.1, 0.95);

	P.SideCount = FMath::Clamp(P.SideCount, 8, 64);

	return P;
}

double FHFLuminaireKit::LensRadius(const FHFLuminaireParams& Params)
{
	const FHFLuminaireParams P = Sanitise(Params);

	// Taken off the RIM radius rather than off the nominal diameter, because the body tapers
	// slightly towards the room and the lens sits in what the body actually presents there.
	return P.Diameter * 0.5 * 0.94 * P.LensFraction;
}

double FHFLuminaireKit::AxisExtent(const FHFLuminaireParams& Params)
{
	const FHFLuminaireParams P = Sanitise(Params);
	return P.RimZ() + P.LensProud;
}

FVector3d FHFLuminaireKit::LensCentre(const FHFLuminaireParams& Params)
{
	// The emitting FACE, not the middle of the diffuser: a light set back inside the dome is shaded
	// by the dome, exactly as a downlight parented at the soffit is shaded by its own trim ring.
	// AHFCeilingActor learned that one the hard way and its comment says so.
	return FVector3d(0.0, 0.0, AxisExtent(Params));
}

FDynamicMesh3 FHFLuminaireKit::Build(const FHFLuminaireParams& InParams)
{
	const FHFLuminaireParams P = Sanitise(InParams);

	FDynamicMesh3 Mesh;
	FHFMeshOps::InitialiseMesh(Mesh);

	const FVector3d Origin = FVector3d::Zero();
	const FVector3d Axis = FVector3d::UnitZ();

	const double BodyRadius = P.Diameter * 0.5;
	const double RimRadius = BodyRadius * 0.94;

	// ------------------------------------------------------------------ the canopy and the flex
	//
	// Only when the fitting hangs. A surface-mounted panel has no outlet box showing and no stem;
	// building them anyway would leave a 9 cm plate and a stub buried inside the drum, invisible,
	// costing triangles and confusing every volume assertion made about the fitting.
	if (P.HasDrop())
	{
		const double CanopyRadius = P.CanopyDiameter * 0.5;

		// A shallow dished plate: full radius at the ceiling, drawn in as it comes away from it.
		FHFMeshOps::AppendRevolvedProfile(Mesh,
			{
				FVector2D(0.0, CanopyRadius),
				FVector2D(P.CanopyHeight * 0.55, CanopyRadius),
				FVector2D(P.CanopyHeight, CanopyRadius * 0.6)
			},
			Origin, Axis, P.SideCount, EHFSurfaceRole::MetalHardware);

		// The flex, from inside the canopy to inside the body. Overlapped at BOTH ends rather than
		// butted: a stem that stops exactly on the canopy face and exactly on the body face is two
		// more coplanar pairs, and at this diameter they flicker as a dotted line down the stem.
		const double FlexTop = FMath::Max(P.CanopyHeight - MinStock, 0.0);
		const double FlexBottom = FMath::Min(P.DropLength + P.BodyHeight * 0.4, P.RimZ());

		if (FlexBottom > FlexTop)
		{
			FHFMeshOps::AppendRevolvedProfile(Mesh,
				{
					FVector2D(FlexTop, P.FlexDiameter * 0.5),
					FVector2D(FlexBottom, P.FlexDiameter * 0.5)
				},
				Origin, Axis, FMath::Max(P.SideCount / 2, 8), EHFSurfaceRole::MetalHardware);
		}
	}

	// ------------------------------------------------------------------------------- the body
	//
	// A drum from the drop to the rim, tapering very slightly towards the room. The taper is not
	// decoration: it puts a real, if shallow, angle on the arris so the edge catches light instead
	// of reading as the mathematically sharp one .claude/rules/04-conventions.md rules out, and it
	// does it in the generator rather than relying on the bevel pass to find it.
	const double BodyTop = P.DropLength;
	const double BodyBottom = P.RimZ();

	// Where the fitting meets the surface it is screwed to, a surface-mounted body starts a shade
	// narrower so the plaster line is not a coplanar meeting either.
	const double TopRadius = P.HasDrop() ? BodyRadius * 0.88 : BodyRadius * 0.97;

	FHFMeshOps::AppendRevolvedProfile(Mesh,
		{
			FVector2D(BodyTop, TopRadius),
			FVector2D(BodyTop + (BodyBottom - BodyTop) * 0.28, BodyRadius),
			FVector2D(BodyBottom, RimRadius)
		},
		Origin, Axis, P.SideCount, EHFSurfaceRole::Appliance);

	// ------------------------------------------------------------------------------- the lens
	//
	// A shallow dome whose top is buried in the body and whose base is narrower than the rim, so
	// what shows is a bezel of body around a diffuser standing proud of it. See the header for why
	// it is not a disc lying on the rim.
	const double LensR = LensRadius(P);
	const double LensTop = FMath::Max(BodyBottom - FMath::Max(P.BodyHeight * 0.35, MinStock), BodyTop);

	FHFMeshOps::AppendRevolvedProfile(Mesh,
		{
			FVector2D(LensTop, LensR),
			FVector2D(BodyBottom, LensR),

			// Domed rather than flat, and stopped at a real radius rather than at an apex: a
			// diffuser is a shallow cap, not a cone, and an apex here would be a point of maximum
			// emissive brightness dead centre in every frame the fitting appears in.
			FVector2D(BodyBottom + P.LensProud, LensR * 0.62)
		},
		Origin, Axis, P.SideCount, EHFSurfaceRole::LightSource);

	return Mesh;
}
