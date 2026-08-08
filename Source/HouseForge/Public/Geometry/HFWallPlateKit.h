// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "DynamicMesh/DynamicMesh3.h"
#include "Model/HFArticulation.h"
#include "Model/HFTypes.h"
#include "HFWallPlateKit.generated.h"

/**
 * Thin things fixed flat to a finished wall face.
 *
 * ## Why this is a kit and not a mirror
 *
 * A mirror is the first of nineteen instances that are all the same construction problem: something
 * a few centimetres thick, screwed to plaster, whose whole reading is its EDGE. A socket, a switch
 * plate, a distribution board and a pelmet are the rest of them, and they share the two things that
 * are hard about the type - a back that must land exactly on the wall face rather than near it, and
 * a front whose profile is the only thing that separates a real fitting from a decal.
 *
 * The mirror is what it starts with because the sanitary group needs it. See
 * FHFFixturePlacement::OnWallFace for the other half of the problem, which is not this kit's.
 *
 * Pure - parameters in, meshes out, no world, no actor, no editor, no settings object. See
 * .claude/rules/04-conventions.md.
 */

/**
 * A bathroom mirror: a bevelled glass plate on a backing board.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, at the BOTTOM of the plate,
 * +Y running BACK into the wall. Z = 0 is the bottom edge, so a mirror placed at BaseZ 1000 has its
 * bottom edge exactly there - which is the figure a drawing gives and the one that has to agree with
 * the basin below it.
 *
 * ## The bevel is the whole fitting
 *
 * A frameless mirror is a rectangle of silvered glass, and there is nothing about a rectangle of
 * silvered glass for light to catch: rendered as a flat plate it is a grey panel that reflects the
 * room and reads as a hole in the wall. What makes a real one read is the 15-20 mm polished BEVEL
 * round its edge, which is a second surface at a shallow angle and therefore a bright line all the
 * way round under any lighting at all. So the glass is a lofted solid - a smaller front face over a
 * full-size back one - rather than a box.
 *
 * A 30 mm build-up is a frameless mirror. There is no room in it for a frame and none is built; a
 * mirror CABINET would be a different fixture with a door, and this spec does not declare one.
 *
 * ## What moves
 *
 * Nothing, and it is said out loud. A mirror cabinet's door opens; a 600 x 30 x 800 plate on a wall
 * has no moving part, and inventing one would be a mechanism built to satisfy a rule rather than to
 * match an object. See .claude/rules/04-conventions.md.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFMirrorParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 80.0;

	/** Whole build-up, wall face to the front of the glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 3.0;

	/** Silvered plate. 5-6 mm is what a mirror this size is cut from. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GlassThickness = 0.6;

	/** The polished band round the edge, measured across the face. Zero for a plain cut edge. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BevelWidth = 1.8;

	/**
	 * Backing board the glass is bonded to: WHATEVER THE DEPTH HAS LEFT, and not a figure of its own.
	 *
	 * A drawing gives a mirror one dimension in this direction - 30 mm - and that figure is the whole
	 * build-up standing off the plaster, because it is the only part of it anybody can measure. Given
	 * its own thickness the backing has no reason to add up to that: a 12 mm board behind 6 mm of
	 * glass leaves the fitting 12 mm shy of its drawn front, which is a mirror hanging in a recess it
	 * does not have.
	 */
	double BackingThickness() const { return FMath::Max(Depth - GlassThickness, 0.0); }

	bool IsValid() const { return Width > 0.0 && Height > 0.0 && GlassThickness > 0.0; }
};

/**
 * What a modular electrical accessory carries in its module window.
 *
 * The two are one construction with two fillings, which is why they are a kind rather than two
 * params: an Indian modular plate is a back grid, a cover with a window in it, and whatever is
 * clipped into the window. A lighting plate carries rockers; a socket carries an outlet and a rocker
 * per gang. Everything else about them - the grid, the cover, the border, the recess, the throw - is
 * identical, and it is exactly the part that thirteen instances get wrong together if it is written
 * twice.
 */
UENUM(BlueprintType)
enum class EHFAccessoryKind : uint8
{
	/** One rocker per gang. A lighting switch plate. */
	Switch,

	/** A perforated outlet face and its own rocker per gang. A switched socket. */
	Socket
};

/**
 * A modular electrical accessory: a socket or a switch plate on a finished wall.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, Z = 0 at the BOTTOM of the plate,
 * +Y running BACK into the wall - the same datum FHFMirrorParams uses and the one
 * FHFFixturePlacement::OnWallFace places. The back plane sits at +Depth/2 and lands on the plaster;
 * the cover's front face is at -Depth/2 and is the whole of what stands proud.
 *
 * ## Thirteen of them, and the proud dimension repeats thirteen times
 *
 * There are eight sockets and five switch plates in the reference flat and not one of them is
 * individually important. What matters is that they are all the same object: a plate that stands
 * 20 mm off the plaster reads as a plate, and one that stands 60 mm off it reads as a box screwed to
 * the wall - in thirteen places at once, at eye level in every room. So the whole build-up is the
 * DRAWN depth and nothing is allowed past it, including a rocker at full throw. See
 * FHFWallPlateKit::BuildAccessoryPlate, which clamps the throw to the clearance rather than trusting
 * the figure.
 *
 * ## What a plate is actually made of
 *
 * Three layers, because that is what makes the edge read. A GRID PLATE on the plaster, a COVER with a
 * window cut in it standing proud of the grid, and MODULES clipped through the window sitting a little
 * behind the cover's own face. Built as one slab it is a rectangle of white with no shadow anywhere on
 * it, which is exactly how a decal looks.
 *
 * ## What moves
 *
 * One rocker per gang, rocking about a horizontal axis through its own middle - so the top edge goes
 * IN as the bottom edge comes OUT. That opposition is the whole test of it: a rocker that translated
 * would move exactly as far and would not be a rocker. See FHFWallPlateKit::RockerPartId.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFAccessoryPlateParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 16.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 12.0;

	/** Whole build-up, plaster face to the front of the cover. Nothing may stand past it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "1", ClampMax = "12"))
	int32 GangCount = 2;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Front")
	EHFAccessoryKind Kind = EHFAccessoryKind::Socket;

	/** Cover plate margin round the module window. 12 mm is what a modular cover actually leaves. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double PlateBorder = 1.2;

	/** Plate screwed to the back box, which the cover stands off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double GridThickness = 0.35;

	/** How far behind the cover's front face the module faces sit. The shadow line round each rocker. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ModuleRecess = 0.5;

	/** How far a rocker stands proud of the blanked grid around it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RockerProud = 0.25;

	/**
	 * Height of a module, which is a bought size and not a share of the plate.
	 *
	 * 45 mm is what one module is, on every range sold. A plate drawn 150 tall does not have a 150 mm
	 * rocker in it - it has a 45 mm rocker with blanked grid above and below, and the difference is
	 * the whole reading of the fitting from across a room.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ModuleHeight = 4.5;

	/**
	 * How far a rocker throws, in degrees. Clamped on build to what the drawn depth can carry.
	 *
	 * Small on purpose. A 45 mm rocker at 6 degrees moves its edge 2.4 mm, which is what a switch
	 * actually does; anything an assertion could see from across the room would be a lever.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double RockerThrowDegrees = 6.0;

	double ApertureWidth() const { return FMath::Max(Width - 2.0 * PlateBorder, 0.0); }
	double ApertureHeight() const { return FMath::Max(Height - 2.0 * PlateBorder, 0.0); }

	bool IsValid() const
	{
		return Width > 0.0 && Height > 0.0 && Depth > 0.0 && GangCount > 0
			&& ApertureWidth() > 0.0 && ApertureHeight() > 0.0;
	}
};

/**
 * A consumer unit: an enclosure of breakers behind a door, high on a wall.
 *
 * ## Frame
 *
 * As FHFAccessoryPlateParams - centre of the footprint in plan, Z = 0 at the bottom of the drawn box,
 * +Y back into the wall, back plane on the plaster.
 *
 * ## What moves, and the ordering between the two
 *
 * The door swings, and every breaker toggle throws - but a breaker cannot be thrown through a shut
 * door, so each toggle declares FHFPartMotion::SequencedAfterPartId against the door. That is the same
 * relationship the WC's seat has with its lid and it is expressed the same way: an ordering between
 * two independent parts, not a linkage. Opening the whole fixture therefore opens the door and then
 * throws the breakers behind it, which is a pose the board could really be in.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDistributionBoardParams
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 30.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 35.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 6.0;

	/** How many single-pole ways are populated. The rest of the rail is blanked, as a real board is. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0", ClampMax = "24"))
	int32 WayCount = 8;

	/** One DIN module. 17.5 mm is the standard, and it is what sets out every breaker ever made. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ModulePitch = 1.75;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double DoorThickness = 0.6;

	/**
	 * How far the door opens, in degrees.
	 *
	 * 90, NOT 110, AND THE WALL IS THE REASON. A consumer unit is screwed to the plaster and its door
	 * is hinged on the front of the box; at exactly 90 degrees the leaf lies in the plane of the
	 * wall, and every degree past that carries its free edge INTO the wall. The reference flat's
	 * board went 110 and put 50 mm of door inside W_Mid_Upper - and, at the far end of its arc,
	 * clipped the head of D_Kitchen next to it.
	 *
	 * Surface-mounted enclosures really do stop there, which is why the flush-mounted ones that open
	 * further are the ones sold on it. A board recessed into the masonry can be given more here; one
	 * standing proud of it cannot, whatever the catalogue says.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions",
		meta = (ClampMin = "0.0", ClampMax = "170.0"))
	double DoorSwingDegrees = 90.0;

	/** How far a breaker toggle throws from on to off. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double ToggleThrowDegrees = 26.0;

	bool IsValid() const { return Width > 0.0 && Height > 0.0 && Depth > DoorThickness; }
};

/**
 * A curtain pelmet: a board and a fascia forming a downward slot that hides a curtain track.
 *
 * ## Frame
 *
 * Centimetres, origin at the CENTRE of the drawn footprint in plan, Z = 0 at the BOTTOM of the drawn
 * box, +Y running BACK into the wall - the same datum FHFMirrorParams and FHFAccessoryPlateParams
 * use. The back plane is at +Depth/2 and lands on the plaster; the TOP is what matters here, because
 * a pelmet is set out from the ceiling rather than from the floor. See FHFFixturePlacement::UnderSoffit.
 *
 * ## Why it is a drop under the ceiling and not a pocket cut into it
 *
 * A pelmet in a flat with a false ceiling is usually a POCKET: the ceiling stops short of the window
 * wall and the track hangs in the slot that leaves, so nothing at all projects below the finished
 * soffit. That is the better detail and it is not what this builds, for two reasons that are both
 * numbers rather than preferences.
 *
 * FIRST, THE BAND IS TOO SHALLOW TO POCKET. The living room's ceiling is a 600 wide cove band
 * dropping 150. Cut a 180 pocket into it and the slot is 150 deep less the 20 board, so 130 of clear
 * height - and a curtain track is 25 with a pinch-pleat heading of 75 to 90 hanging off it, before
 * any fixing. The heading would show below the ceiling line, which is the one thing a pelmet exists
 * to prevent. The cove wins the 150; the pelmet takes its own 200 below it.
 *
 * SECOND, A FIXTURE MAY NOT DECIDE WHERE A CEILING IS. FHFCeilingFit resolves every fixture in a room
 * AGAINST the finished soffit. A pelmet that cut its own aperture in the ceiling would make the
 * soffit depend on the fixtures, and the fixtures depend on the soffit - a cycle, in the one
 * mechanism in this plugin that exists to stop fittings and ceilings disagreeing.
 *
 * So it hangs 200 below the band with its top board BURIED in it rather than touching it, its front
 * face 180 from the plaster and therefore well inside the 600 band. From the room it reads as a
 * second step of the same ceiling over the window, which is what it is.
 *
 * ## What moves: nothing, and the curtain is why
 *
 * The obvious moving part is a pair of curtain leaves sliding to each side, and they are deliberately
 * not built. A curtain is not a rigid body: drawn back it GATHERS to about a fifth of its width, and
 * no translation of a solid does that. A leaf modelled closed and slid sideways runs a metre past the
 * end of its own pelmet; a leaf modelled gathered and slid across covers a 1900 window with a 350
 * rag. Both measure as motion and both are visible lies - which is the failure this project already
 * has a name for, from a wardrobe whose two leaves each travelled their full 118 cm and cancelled.
 *
 * EHFFixtureType::Curtain is its own type for exactly this reason and the reference flat declares
 * none. So the pelmet carries the track and the slot, and nothing on it moves.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFPelmetParams
{
	GENERATED_BODY()

	/** Length of the run along the wall. Wider than the window, so the stack clears the glass. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Width = 190.0;

	/** Wall face to the front of the fascia. 150-200 is what a track and a heading need to hide in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Depth = 18.0;

	/** How far the fascia hangs below the finished soffit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double Height = 20.0;

	/**
	 * The boards. A project figure, resolved by the composing layer from the joinery defaults.
	 *
	 * A pelmet is made of the same 18 mm ply the joinery in the flat is made of, by the same carpenter
	 * on the same day. Reading it here rather than declaring a number of its own is what keeps it that
	 * way when somebody changes the board on the settings page.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double BoardThickness = 1.8;

	/** Width of the aluminium track section. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TrackWidth = 2.5;

	/** Depth of the track section below the board it is screwed to. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TrackDepth = 1.5;

	/**
	 * Gap between the back of the fascia and the front of the track.
	 *
	 * What a curtain hangs in. Too small and the heading fouls the fascia every time it is drawn; too
	 * large and the curtain hangs away from the window with a light gap behind it.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Dimensions", meta = (ClampMin = "0.0"))
	double TrackSetback = 3.5;

	/** Radius rolled onto the fascia's bottom arris. The line the room actually sees. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge|Softness", meta = (ClampMin = "0.0"))
	double EdgeRoll = 0.3;

	/** Clear length of the slot, between the two end returns. */
	double ClearWidth() const { return FMath::Max(Width - 2.0 * BoardThickness, 0.0); }

	/** Front-to-back of the slot, behind the fascia. */
	double SlotDepth() const { return FMath::Max(Depth - BoardThickness, 0.0); }

	/**
	 * How much curtain heading the fascia hides, below the track. THE MEASUREMENT THAT SAYS IT WORKS.
	 *
	 * A pinch-pleat heading is 75 to 90 mm tall and it hangs off the track's gliders. A pelmet whose
	 * fascia stops above that shows a row of hooks, which is the one thing the whole fitting exists to
	 * prevent - and it is invisible in plan, in section and in every test that only measures the box.
	 */
	double ConcealedHeadingHeight() const
	{
		return FMath::Max(Height - BoardThickness - TrackDepth, 0.0);
	}

	/** The drawn box IS the object: nothing on a pelmet stands above its own top board. */
	double BuiltHeight() const { return Height; }

	bool IsValid() const
	{
		return Width > 2.0 * BoardThickness && Depth > BoardThickness
			&& Height > BoardThickness && BoardThickness > 0.0;
	}
};

/** A composed wall plate. Plain data carrying meshes by value. */
struct HOUSEFORGE_API FHFWallPlateBuild
{
	UE::Geometry::FDynamicMesh3 Shell;
	TArray<FHFMeshPart> Parts;
	bool bValid = false;
};

/**
 * A composed pelmet, with the track kept apart from the case that hides it.
 *
 * Separate for the reason every build struct here keeps its sub-assemblies: "the fascia hides the
 * track" is a statement about the gap between two solids, and it stops being answerable the moment
 * they are one mesh.
 */
struct HOUSEFORGE_API FHFPelmetBuild
{
	UE::Geometry::FDynamicMesh3 Shell;

	/** Fascia, top board and the two end returns, finished as the ceiling is. */
	UE::Geometry::FDynamicMesh3 Case;

	/** The track section, in MetalHardware. */
	UE::Geometry::FDynamicMesh3 Track;

	FHFPelmetParams Used;
	bool bValid = false;
};

class HOUSEFORGE_API FHFWallPlateKit
{
public:
	static FHFMirrorParams SanitiseMirror(const FHFMirrorParams& Params);
	static FHFAccessoryPlateParams SanitiseAccessoryPlate(const FHFAccessoryPlateParams& Params);
	static FHFDistributionBoardParams SanitiseDistributionBoard(const FHFDistributionBoardParams& Params);
	static FHFPelmetParams SanitisePelmet(const FHFPelmetParams& Params);

	/** Backing board and bevelled glass. Nothing moves; see FHFMirrorParams. */
	static FHFWallPlateBuild BuildMirror(const FHFMirrorParams& Params);

	/** Grid, cover, blanked window, outlets where the kind asks for them, and a rocker per gang. */
	static FHFWallPlateBuild BuildAccessoryPlate(const FHFAccessoryPlateParams& Params);

	/** Enclosure, rail, breakers, and the door they are sequenced behind. */
	static FHFWallPlateBuild BuildDistributionBoard(const FHFDistributionBoardParams& Params);

	/**
	 * Fascia, top board, two end returns and the track they hide. Nothing moves; see FHFPelmetParams.
	 *
	 * @return A build with bValid false and empty meshes when the parameters describe no pelmet.
	 */
	static FHFPelmetBuild BuildPelmet(const FHFPelmetParams& Params);

	/** Part id of a rocker, left to right. */
	static FName RockerPartId(int32 Index);

	/** Part id of a consumer unit's breaker toggle, left to right. */
	static FName BreakerPartId(int32 Index);

	/** Part id of the consumer unit's door. */
	static FName DoorPartId() { return TEXT("Door"); }
};
