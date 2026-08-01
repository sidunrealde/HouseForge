// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "HFTypes.generated.h"

/**
 * Units the drawing was authored in.
 *
 * AutoCAD residential drawings in this domain are almost always millimetres; Unreal is
 * centimetres. A spec declares its units explicitly and conversion happens exactly once at
 * ingest, so nothing downstream of the spec ever sees anything but centimetres.
 * See FHFUnits::ConvertToCentimeters.
 */
UENUM(BlueprintType)
enum class EHFUnits : uint8
{
	Millimeters	UMETA(DisplayName = "Millimeters"),
	Centimeters	UMETA(DisplayName = "Centimeters"),
	Meters		UMETA(DisplayName = "Meters"),
	Feet		UMETA(DisplayName = "Feet"),
	Inches		UMETA(DisplayName = "Inches")
};

/** What a room is for. Drives defaults for finishes, fixtures and ceiling treatment. */
UENUM(BlueprintType)
enum class EHFRoomType : uint8
{
	Unknown,
	Living,
	Dining,
	Kitchen,
	Utility,
	Bedroom,
	MasterBedroom,
	Bathroom,
	Toilet,
	Balcony,
	Foyer,
	Corridor,
	Study,
	Storage
};

/** Kind of hole in a wall. */
UENUM(BlueprintType)
enum class EHFOpeningKind : uint8
{
	Door,
	SlidingDoor,
	Window,
	SlidingWindow,
	Archway,
	Ventilator
};

/** Which way a door leaf opens, as read from the swing arc on the drawing. */
UENUM(BlueprintType)
enum class EHFSwing : uint8
{
	None,
	InwardLeft,
	InwardRight,
	OutwardLeft,
	OutwardRight
};

/** False ceiling treatments that actually appear in Indian 2BHK/3BHK interior drawings. */
UENUM(BlueprintType)
enum class EHFCeilingStyle : uint8
{
	/** No false ceiling; the room is open to the slab. */
	None,
	/** Dropped band around the perimeter, centre left at slab height. */
	Peripheral,
	/** Whole room dropped uniformly. */
	FullDrop,
	/** Stepped levels, inner region higher than the band. */
	Tray,
	/** Peripheral band with a recessed channel hiding an LED strip. */
	Cove,
	/** Localised drop over a wardrobe run, kitchen counter or TV unit. */
	Bulkhead
};

/**
 * A named ceiling design, rather than six numbers somebody has to get right by hand.
 *
 * A STYLE IS A CONSTRUCTION; A TEMPLATE IS A DESIGN. EHFCeilingStyle says how the pieces are put
 * together - a band, a step, a trough - and says nothing about the figures, so "Cove" covers both a
 * 150 deep channel that reads as a halo and a 500 deep well that swallows its own light. The flat's
 * ceilings were all the second kind and all of them validated, because every rule they had to pass
 * was about depth clearing a beam and none was about the design working.
 *
 * So a template is the whole recipe: which style, how wide the band, how deep the drop, the cove
 * section, whether there are downlights and how far apart. The figures live in the project's
 * settings (FHFCeilingDefaults), the recipe picks which of them apply, and FHFCeilingTemplates
 * stamps the result onto a ceiling before anything validates or builds it.
 *
 * Four, deliberately, and they are the four that recur through the reference photographs. The
 * medallion and the wavy organic ceilings in that set are out of scope by explicit request.
 */
UENUM(BlueprintType)
enum class EHFCeilingTemplate : uint8
{
	/**
	 * No template: the ceiling's own figures stand exactly as authored.
	 *
	 * What every ceiling written before templates existed still is, and what a hand-tuned one
	 * should stay. Nothing is stamped over a Custom ceiling.
	 */
	Custom,

	/** A shallow perimeter band with a run of recessed downlights in it. The default treatment. */
	PlainBand,

	/**
	 * The band with a cove: a trough at its inner edge throwing light UP onto the surface above.
	 *
	 * The single most characteristic feature of the reference set, and the one the old figures got
	 * wrong - not by having no trough, but by putting the trough at the bottom of a 500 deep well.
	 */
	Cove,

	/** Two levels: an outer band, a shallower step inside it, and the centre panelled at the step. */
	SteppedTray,

	/**
	 * A frame band around a centre panel that sits higher than the frame, with the cove channel
	 * between them as the shadow gap. The light washes the panel rather than the slab.
	 */
	FramedPanel,

	/**
	 * Flat across the whole room, with a downlight or two in it. A kitchen, a bathroom, a corridor.
	 *
	 * A DESIGN, AND SAYING SO IS THE POINT. These rooms really are ceiled flat - a wet area hides
	 * its plumbing and its extract, a corridor is where the services run between them - but left on
	 * Custom that is indistinguishable from nobody having got to them, which is exactly how four
	 * rooms of the reference flat kept a 480 blanket drop through a round of fixing the blanket
	 * drop. Naming the design also puts them on the perimeter-ring machinery, so the flat part can
	 * be as shallow as the services need while the beams at the edges are still buried.
	 */
	FlatSoffit
};

/** Everything the drawings show that becomes an object in the level. */
UENUM(BlueprintType)
enum class EHFFixtureType : uint8
{
	Unknown,

	// Joinery
	Wardrobe,
	LoftUnit,
	KitchenBaseCabinet,
	KitchenWallCabinet,
	KitchenTallUnit,
	CounterTop,
	TVUnit,
	StudyTable,
	Bookshelf,
	Vanity,

	// Appliances and sanitary
	Sink,
	Hob,
	Chimney,
	Refrigerator,
	WashingMachine,
	WC,
	Basin,
	Shower,
	ShowerPartition,

	// Loose furniture
	Bed,
	Nightstand,
	Sofa,
	Chair,
	DiningTable,
	CoffeeTable,

	// Electrical and soft furnishing
	CeilingFan,
	LightFixture,
	SwitchPlate,
	Curtain,

	// Electrical services. Drawings carry these on their own layer, and they are what makes a
	// generated flat usable rather than merely furnished.
	PowerSocket,
	DistributionBoard,
	ACIndoorUnit,
	ACOutdoorUnit,
	Geyser,
	ExhaustFan,

	// Architectural fittings
	ShoeRack,
	Pelmet,
	Mirror,
	TowelRail,
	Railing,
	WallNiche
};

/** Handle treatment on joinery shutters and drawers. */
UENUM(BlueprintType)
enum class EHFHandleStyle : uint8
{
	None,
	Bar,
	Knob,
	JProfile,
	HandlelessGroove
};

/**
 * How a shutter leaf moves.
 *
 * A side-hung leaf is the commonest thing in a fitted kitchen and the least common thing in a
 * modern Indian wardrobe, which is why all three of these exist. Hard-coding the vertical hinge
 * ruled out a loft flap, a lift-up wall cabinet and the sliding wardrobe that most flats of this
 * class actually have.
 *
 * IN THE SPEC LAYER rather than beside the joinery kit that consumes it, and that is the whole
 * point of it living here: a drawing shows whether a wardrobe slides or swings, so it is something
 * Claude READS, and a value the geometry layer can express but no spec can carry is a value no
 * drawing can ever produce. It sat in Geometry/HFJoineryKit.h for exactly one milestone and in that
 * time every wardrobe in the reference flat was side-hung, because FHFFixtureParams had no field
 * for it and AHFWardrobeActor::ApplyFixture had nothing to copy.
 */
UENUM(BlueprintType)
enum class EHFShutterMotion : uint8
{
	/** Hinged on a vertical edge and swinging out. The kitchen and cabinet default. */
	SideHung,

	/**
	 * Hinged along its head and lifting out and up: a loft flap, a lift-up wall cabinet.
	 *
	 * The leaf hangs BELOW its hinge, so its local Z runs from -LeafHeight to 0, and it opens
	 * about the horizontal axis at its head. Its leading edge is therefore the bottom one, which is
	 * where the handle and the gas stay go.
	 */
	TopHung,

	/**
	 * Running on a track, passing its neighbour rather than swinging clear of it.
	 *
	 * Different from a hinged run in a way that shows: sliding leaves LAP one another on separate
	 * tracks instead of being separated by a reveal, so there is no shadow gap between them and no
	 * daylight either. The set-out is the same two-track rule a sliding door uses, and is taken
	 * from FHFSlidingSetOut rather than worked out again here.
	 */
	Sliding
};

/**
 * Material role a surface plays.
 *
 * Every triangle a generator emits carries one of these as its polygroup. The material panel
 * targets faces by role, so untagged geometry cannot be re-materialled by the user - see
 * .claude/rules/04-conventions.md.
 */
UENUM(BlueprintType)
enum class EHFSurfaceRole : uint8
{
	WallPaint,
	FloorFinish,
	CeilingSoffit,
	CoveInterior,
	Skirting,
	JoineryCarcass,
	ShutterLaminate,
	CounterStone,
	Glass,
	MetalHardware,
	DoorLeaf,
	WindowFrame,
	Sanitary,
	Fabric,
	Appliance,
	/** Exposed structure - beams and columns. */
	Structure,
	/**
	 * A surface that EMITS: an LED strip lying in a cove, the lens up inside a downlight can.
	 *
	 * The one role that is not a finish. It exists because a cove is the most characteristic thing
	 * in the whole reference set and the plugin built it with nothing to see: the strip was tagged
	 * MetalHardware, the lens Glass, neither placeholder had an emissive input, and nothing anywhere
	 * spawned a light - so every cove in the flat rendered as a painted line at a step. A cove that
	 * does not glow is not a cove, and "the false ceiling types are not properly visible" is most of
	 * what the user was looking at.
	 *
	 * Kept separate from MetalHardware because the profile the strip sits in really is aluminium and
	 * really does want a metal chamfer and a metal roughness; it is the light coming off it that is
	 * different. The material panel gets one place to set that, and AHFCeilingActor puts real lights
	 * at the same coordinates so the wash is light and not just a bright texel.
	 */
	LightSource
};

/**
 * A wall, described by its centreline.
 *
 * Centreline rather than face lines because that is what survives a thickness change: edit
 * Thickness and the wall grows symmetrically without moving its neighbours' junctions.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFWall
{
	GENERATED_BODY()

	/** Unique within the spec. Openings reference walls by this. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** Centreline start, in the spec's declared units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Start = FVector2D::ZeroVector;

	/** Centreline end, in the spec's declared units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D End = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Thickness = 11.5;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Height = 300.0;

	/** Floor level this wall stands on. Non-zero for parapets and half-walls. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BaseZ = 0.0;

	/** True for external/structural walls, which are typically thicker and not to be moved. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bIsExternal = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole SurfaceRole = EHFSurfaceRole::WallPaint;

	/** Centreline length in spec units. */
	double Length() const { return FVector2D::Distance(Start, End); }
};

/** A door, window or archway cut into a wall. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFOpening
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** The FHFWall::Id this opening is cut into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName WallId;

	/** Distance from the host wall's Start to the opening's centre, along the centreline. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double OffsetAlongWall = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Width = 90.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Height = 210.0;

	/** Height of the opening's bottom edge above the wall's base. Zero for doors. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double SillHeight = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFOpeningKind Kind = EHFOpeningKind::Door;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSwing Swing = EHFSwing::None;

	/** Top of the opening above the wall base. The figure headroom checks care about. */
	double HeadHeight() const { return SillHeight + Height; }
};

/**
 * An RCC downstand beam.
 *
 * These are the reason false ceilings exist in this domain: a beam hanging 450 below the slab has
 * to be boxed in, and the ceiling drop is chosen to clear the deepest beam crossing the room.
 * Modelled as a first-class element rather than a fixture because it is structural - it cannot be
 * moved to suit the furniture, and the ceiling has to work around it.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFBeam
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** Centreline, usually following a wall below or spanning between columns. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Start = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D End = FVector2D::ZeroVector;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Width = 230.0;

	/** How far the beam hangs below the slab soffit. This is what a false ceiling must clear. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Depth = 450.0;

	/** Slab soffit level this beam hangs from. Matches the storey's ceiling height. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double SoffitZ = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole SurfaceRole = EHFSurfaceRole::Structure;

	double Length() const { return FVector2D::Distance(Start, End); }

	/** Underside of the beam above the floor - the clear height beneath it. */
	double ClearHeight() const { return SoffitZ - Depth; }
};

/** An RCC column. Usually at a corner or a wall junction, and often projecting into a room. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFColumn
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** Centre of the column in plan. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Position = FVector2D::ZeroVector;

	/** Plan size before rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Size = FVector2D(230.0, 450.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double RotationDegrees = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double Height = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BaseZ = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole SurfaceRole = EHFSurfaceRole::Structure;
};

/**
 * A volume that displaces whatever is built around it.
 *
 * RCC GOES UP FIRST AND THE BLOCKWORK INFILLS AROUND IT. A wall under a downstand beam is built to
 * the beam soffit; a wall meeting a column butts against its face; a beam frames into the column it
 * lands on. In none of those does the same volume belong to both members, and the plaster that
 * spans the junction afterwards is a finish, not a second wall.
 *
 * Modelled as if it did. A beam co-linear with the wall below it occupied the top 450 of that
 * wall's own solid, so the beam and the wall each drew the same two side faces and the same top
 * face - and two faces in one plane pointing the same way is a coin toss the depth test re-tosses
 * every frame. That is the flashing the flat was reported for: a stippled band the whole 10.8 m
 * length of the south elevation, a torn sawtooth along every wall-to-ceiling junction, a striped
 * strip up the wall beside the master bedroom door. Every one of those meshes was watertight,
 * correctly wound, correctly sized and correctly tagged.
 *
 * A beam and a column reduce to the same thing as far as the member they displace is concerned, so
 * the generators see ONE concept rather than two, and a structural member added later needs no new
 * case anywhere.
 *
 * A plain oriented box, because that is what every structural member in this domain is. Anything
 * that is not - a shear wall on a curve - would arrive as several of these.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFStructuralCut
{
	GENERATED_BODY()

	/** What displaced the masonry, for the build log. */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "HouseForge")
	FName SourceId;

	/** Centre of the volume, in world centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector Centre = FVector::ZeroVector;

	/** Half-size on each axis, before rotation. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector Extents = FVector::ZeroVector;

	/** Rotation about Z, about the centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double YawDegrees = 0.0;

	double BottomZ() const { return Centre.Z - Extents.Z; }
	double TopZ() const { return Centre.Z + Extents.Z; }

	bool IsValid() const { return Extents.X > 0.0 && Extents.Y > 0.0 && Extents.Z > 0.0; }
};

/** An enclosed space. Its boundary drives the floor slab, skirting and false ceiling. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFRoom
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** As labelled on the drawing, e.g. "Master Bedroom". */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FString Name;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFRoomType Type = EHFRoomType::Unknown;

	/**
	 * Closed polygon of the finished floor area, counter-clockwise, in spec units.
	 * The closing edge is implicit - do not repeat the first point.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> Boundary;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double FloorZ = 0.0;

	/** Slab height above FloorZ, before any false ceiling drop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double CeilingHeight = 300.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFSurfaceRole FloorRole = EHFSurfaceRole::FloorFinish;

	/** Zero disables skirting, which is what you want in bathrooms and balconies. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double SkirtingHeight = 10.0;

	/** Signed polygon area in spec units squared; positive when wound counter-clockwise. */
	double SignedArea() const;

	/** Unsigned floor area in spec units squared. */
	double Area() const { return FMath::Abs(SignedArea()); }

	/** True if the point lies inside the boundary polygon (even-odd rule). */
	bool ContainsPoint(const FVector2D& Point) const;
};

/**
 * The recessed channel of a cove ceiling, which hides an LED strip.
 *
 * TWO INDEPENDENT THINGS HAVE TO BE TRUE AT ONCE, and the numbers here are what decides both.
 *
 * The strip must be HIDDEN. That reduces to one inequality with no distance in it: the top of the
 * strip must not stand above the top of the lip. The strip throws upward, so an eye below the lip
 * top sends a ray that clears the lip and keeps RISING, and the lowest thing that ray can reach
 * over the trough is the lip top itself. So concealment does not depend on where in the room the
 * viewer stands, and a lip that hides the strip from one seat hides it from every seat and from
 * every camera below the lip.
 *
 * The light must also GET OUT. That is the constraint the flat's old ceilings failed. A trough
 * 480 deep and 80 wide is a 6:1 well: almost everything the strip emits is absorbed by its own
 * sides, the escaping cone is 40 degrees off horizontal, and the near edge of the wash lands the
 * better part of a metre inboard - so the slab has a dark band across it exactly where the glow is
 * meant to start. Sizing rules worth keeping:
 *
 *     trough depth : width      at or below 1.5 : 1
 *     dark gap on the surface   under 150 mm
 *     lip above the trough floor  at least 40 mm, so the strip and its diffuser stay behind it
 *
 * All three hold at a 150 drop with a 100 channel and a 75 lip. None of them held at 500 / 80 / 50.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCoveProfile
{
	GENERATED_BODY()

	/** Width of the open channel between the band's inner lip and the higher centre, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ChannelWidth = 10.0;

	/**
	 * How far the inner lip rises above the band soffit, shielding the strip from view, in centimetres.
	 *
	 * 75, not 50. The rule three paragraphs up asks for at least 40 mm of lip standing clear of the
	 * trough floor and the trough floor is a 20 board, so 50 leaves 30 - a figure that fails the
	 * struct's own stated rule and shipped anyway, because the rule was written in a comment and
	 * asserted nowhere. 75 leaves 55, and the strip's 36 has 39 mm of margin: a thicker diffuser, a
	 * bare strip on a bracket or a 10 mm setting-out error all still stay hidden.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LipHeight = 7.5;

	/** Distance from the band's inner edge to the channel, in centimetres. A POP upstand under 25 chips. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Setback = 3.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasLedStrip = true;

	/**
	 * Width of the aluminium LED profile lying in the trough, in centimetres.
	 *
	 * The strip is REAL GEOMETRY, not a flag. bHasLedStrip has always been on this struct and
	 * nothing ever built anything for it, so the one part a cove exists to hide did not exist -
	 * which makes "the strip is concealed" unfalsifiable, and it stayed unfalsified while the
	 * trough was 480 deep. A solid in the trough is what a sight line can be cast at.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double StripWidth = 2.0;

	/** Height of the profile with its diffuser on, in centimetres. Has to stay under LipHeight. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double StripHeight = 1.6;

	/** How far the strip sits back from the inner face of the lip, in centimetres. Sets the dark gap on the wash. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double StripSetback = 2.5;
};

/**
 * A recessed downlight, as a hole in a soffit rather than as a dot on a plan.
 *
 * LightPositions on a false ceiling used to be plan coordinates and nothing else: the preview drew
 * a circle at each, the geometry ignored them entirely, and a rendered ceiling had no downlights in
 * it at all. A run of recessed lights is one of the two things that makes a band read as a designed
 * ceiling rather than as a step in the plaster - the other is the cove - so the cut-out, the trim
 * and the aperture are modelled.
 *
 * The body depth is also why a band cannot be shallower than about 100: a 20 board plus a 60 can
 * plus its wiring is what has to fit in the plenum, and that is the real reason 150 is the common
 * minimum drop rather than any aesthetic rule.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFDownlightProfile
{
	GENERATED_BODY()

	/** Cut-out diameter in the soffit board, in centimetres. A 3-inch COB is the default fitting here. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double CutoutDiameter = 7.5;

	/** Visible trim diameter, in centimetres. Stands proud of the soffit, which is what catches the light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FlangeDiameter = 9.0;

	/** How far the trim projects below the finished soffit, in centimetres. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double FlangeProjection = 0.3;

	/** How far above the soffit the aperture sits, in centimetres - the depth of the recess you look up into. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BodyDepth = 6.0;

	/** Nothing is built when this is off, and LightPositions become plan annotation again. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bRecessed = true;

	double CutoutRadius() const { return CutoutDiameter * 0.5; }
	double FlangeRadius() const { return FMath::Max(FlangeDiameter, CutoutDiameter) * 0.5; }
};

/** A false ceiling over one room. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFalseCeiling
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** The FHFRoom::Id this ceiling covers. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName RoomId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFCeilingStyle Style = EHFCeilingStyle::Peripheral;

	/**
	 * The named design these figures came from, or Custom if they were authored by hand.
	 *
	 * WHAT A DRAWING SAYS. A spec can name a template and leave every figure at zero; the project's
	 * FHFCeilingDefaults then fills them in, so a template is a real choice a drawing can express
	 * rather than a preset somebody clicked once in an editor. FHFCeilingTemplates::Apply does the
	 * stamping, ONCE, before anything validates or builds - which is what keeps the validator, the
	 * serializer and the generator all looking at the same resolved numbers.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFCeilingTemplate Template = EHFCeilingTemplate::Custom;

	/** How far the ceiling hangs below the room's slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Drop = 20.0;

	/** Width of the dropped perimeter band. Ignored by FullDrop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BandWidth = 60.0;

	/**
	 * Drop of the step inside the band, for Tray. Zero means half the outer drop.
	 *
	 * The zero is the behaviour Tray had compiled in, so a ceiling written before this field
	 * existed steps exactly where it always did. A two-level tray on site is not a halving - it is
	 * 200 outside and 100 inside - and there was no way to say that.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double InnerDrop = 0.0;

	/**
	 * A panel filling the centre inside the band, hanging this far below the slab. Zero: no panel.
	 *
	 * What turns a band into a FRAME. The panel sits HIGHER than the band soffit - 40 below the slab
	 * against the band's 150 - which is the way round the reference photographs almost always show
	 * it, and it is what gives a cove something to wash. Without it a cove throws its light at the
	 * bare slab.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double CentrePanelDrop = 0.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFCoveProfile Cove;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFDownlightProfile Downlight;

	/**
	 * A deeper ring around the outside of the ceiling, boxing in the beams that run round the room.
	 *
	 * THE ANSWER TO A BEAM IS NOT A DEEPER ROOM. Every false ceiling in the reference flat dropped a
	 * uniform 500 across the whole room, and every one of them had a correct reason: the beams are
	 * 450 deep, and a ceiling shallower than the beam it crosses leaves the beam hanging through the
	 * finished soffit. But burying one 450 beam by dropping twenty-four square metres of living room
	 * by half a metre is not what anybody builds and not what the reference designs look like. A
	 * real ceiling is shallow - 100 to 200 - and the beam gets a LOCAL box.
	 *
	 * Local, here, means a ring. Every beam in this flat runs along a wall line, so what shows in a
	 * room is the nib a 230 beam stands proud of the 115 partition under it: 57.5 on each face, for
	 * the beam's whole run, round the edge of the room. A ring 300 wide and 470 deep buries that and
	 * leaves everything more than 300 from the wall at the shallow drop. The level change is the
	 * design rather than a compromise - it is the outer step of the stepped ceilings in the
	 * reference set.
	 *
	 * A beam crossing the OPEN interior of a room is a different problem and is not this field's:
	 * that wants its own Bulkhead ceiling with its own polygon, and the validator asks for one.
	 *
	 * Zero width or zero drop means no ring.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PerimeterBulkheadWidth = 0.0;

	/** How far the ring hangs below the slab. Deeper than Drop, or it is not a step. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PerimeterBulkheadDrop = 0.0;

	/**
	 * Which edges of the outline the ring runs along, as indices into the boundary.
	 *
	 * A RING ON FOUR SIDES BURIES NOTHING ON TWO OF THEM. In the living room only BM_Mid_Lower and
	 * BM_Living_Bed2 stand proud of the 115 partitions under them; BM_South and BM_West are flush in
	 * the 230 external walls and show nothing at all. Dropped 480 right round anyway, the room reads
	 * as a deep three-level frame with 63% of its ceiling below the slab - not the shallow band with
	 * a high centre the reference designs are.
	 *
	 * So the ring answers per edge. Empty means every edge, which is what a hand-authored ceiling
	 * naming none of them means, and FHFCeilingTemplates always writes the list out.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<int32> PerimeterBulkheadEdges;

	/**
	 * Overrides the room boundary when the ceiling does not follow the walls - which is the
	 * normal case for Bulkhead. Empty means "derive from the room".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> ExplicitPolygon;

	/** Recessed spotlight positions, in spec units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> LightPositions;

	/** True when a perimeter bulkhead ring is actually built. */
	bool HasPerimeterBulkhead() const
	{
		return PerimeterBulkheadWidth > 0.0 && PerimeterBulkheadDrop > Drop;
	}

	/**
	 * The ring's footprint, as one strip per edge it runs along.
	 *
	 * Strips rather than an inset, because an inset is the same on every side by construction and
	 * the whole point is that it is not. Each strip straddles its edge - out as far as it reaches in
	 * - and overruns both ends by its own width, so two strips meeting at a corner mitre instead of
	 * leaving a notch. What is outside the outline is clipped off by whoever uses them.
	 *
	 * @param Outline The ceiling's own outline, which is ExplicitPolygon or the room boundary.
	 * @param Width   How far into the room the ring reaches.
	 */
	TArray<TArray<FVector2D>> BulkheadStrips(const TArray<FVector2D>& Outline, double Width) const;

	/**
	 * The deepest this ceiling reaches below the slab, anywhere.
	 *
	 * What headroom is measured against. Drop alone is the answer for the middle of the room and
	 * the wrong answer at its edge, and a rule that took it would report 2850 of clearance under a
	 * ceiling whose ring hangs at 2530.
	 */
	double DeepestDrop() const
	{
		return HasPerimeterBulkhead() ? FMath::Max(Drop, PerimeterBulkheadDrop) : Drop;
	}
};

/**
 * Parameters shared across joinery and furniture.
 *
 * Deliberately one flat struct rather than a per-type hierarchy: it round-trips through JSON
 * without custom converters, and Claude can fill only the fields a given fixture type cares
 * about. Each generator reads what applies to it and ignores the rest.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFixtureParams
{
	GENERATED_BODY()

	/** Hinged shutter fronts across the run. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0"))
	int32 ShutterCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0"))
	int32 DrawerCount = 0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0"))
	int32 ShelfCount = 0;

	/** Storage box above a wardrobe, standard in Indian bedrooms. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasLoft = false;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LoftHeight = 60.0;

	/** Recessed base the carcass sits on. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double PlinthHeight = 10.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFHandleStyle HandleStyle = EHFHandleStyle::Bar;

	/**
	 * How the shutter fronts move: side-hung, top-hung as a flap, or running on tracks.
	 *
	 * READ OFF THE DRAWING, not assumed. A sliding wardrobe is the commonest wardrobe in a modern
	 * Indian flat and it is drawn differently from a hinged one - no swing arcs, and a run whose
	 * leaves lap rather than being divided at every bay - so this is something a plan actually says.
	 *
	 * Side-hung is the default because it is the right answer for everything that is not a wardrobe:
	 * a kitchen base unit, a crockery unit and a vanity are all hinged.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFShutterMotion ShutterMotion = EHFShutterMotion::SideHung;

	/**
	 * How a loft's flap moves, where the fixture has a loft.
	 *
	 * Separate from ShutterMotion and NOT derived from it, because a sliding wardrobe's loft is not
	 * sliding. A sliding run's gear is a track at the head of the body; there is nothing above it
	 * for a loft leaf to run on, and hanging one off a second track standing further out into the
	 * room is neither what is built nor what would look right. Real sliding wardrobes have hinged
	 * loft shutters - or a top-hung flap, which is the other real answer and is why this is its own
	 * field rather than a rule.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (EditCondition = "bHasLoft"))
	EHFShutterMotion LoftShutterMotion = EHFShutterMotion::SideHung;

	/** Hanging rail inside a wardrobe. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasHangingRail = false;

	/** Glass panel in the shutter fronts, e.g. a crockery unit. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasGlassInsert = false;

	/** Backsplash upstand behind a kitchen counter. Zero disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double UpstandHeight = 0.0;

	/** Decorative moulding along the top of wall cabinets. Zero disables it. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double CorniceHeight = 0.0;

	/** Blade span for a ceiling fan; diameter for a round light. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Diameter = 0.0;

	/** Gang count for a switch plate. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0"))
	int32 GangCount = 0;
};

/** A piece of joinery, furniture, sanitary ware or electrical fitting. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFFixture
{
	GENERATED_BODY()

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName Id;

	/** The FHFRoom::Id this fixture sits in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName RoomId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFFixtureType Type = EHFFixtureType::Unknown;

	/** As labelled on the drawing, if it was labelled. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FString Label;

	/** Centre of the footprint, in spec units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Position = FVector2D::ZeroVector;

	/** Yaw about the footprint centre. Zero means Width runs along +X. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double RotationDegrees = 0.0;

	/** Width (X) by Depth (Y) before rotation, in spec units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FVector2D Footprint = FVector2D(60.0, 60.0);

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Height = 75.0;

	/** Height of the fixture's underside above the room floor. Non-zero for wall cabinets. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	double BaseZ = 0.0;

	/**
	 * Wall this fixture backs onto, if any. Wall-anchored fixtures align to the wall face during
	 * asset replacement instead of to the floor plane, which is what keeps a swapped-in wardrobe
	 * flush rather than floating.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FName AnchorWallId;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFFixtureParams Params;

	/** True for ceiling-mounted fixtures, whose BaseZ is measured down from the ceiling. */
	bool IsCeilingMounted() const
	{
		return Type == EHFFixtureType::CeilingFan || Type == EHFFixtureType::LightFixture;
	}
};

/**
 * A whole dwelling.
 *
 * This is the contract between Claude's reading of a drawing and the geometry code. Claude never
 * issues geometry commands - it produces one of these, the validator checks it, and the plugin
 * builds it. That keeps generation deterministic, testable without an LLM, and re-runnable.
 */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFHouseSpec
{
	GENERATED_BODY()

	/** Schema version, so older spec files can be migrated rather than silently misread. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	int32 SchemaVersion = 1;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FString Name;

	/** Source drawing this spec was read from, for traceability. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FString SourceDrawing;

	/** Units every length in this spec is expressed in. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	EHFUnits Units = EHFUnits::Millimeters;

	/**
	 * Where the units were read from on the drawing - a title block note, a dimension string, a
	 * scale bar.
	 *
	 * Units must be read, never assumed. A misread is uniquely dangerous because it leaves the
	 * spec perfectly self-consistent: every wall still meets, every opening still fits, and the
	 * house is simply built at the wrong scale. Recording the source makes declaring units a
	 * deliberate act rather than a guess, and the validator complains when it is left blank.
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FString UnitsSource;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double DefaultWallThickness = 115.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.1"))
	double DefaultWallHeight = 3000.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFWall> Walls;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFOpening> Openings;

	/** Downstand beams. False ceiling drops are chosen to clear these. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFBeam> Beams;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFColumn> Columns;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFRoom> Rooms;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFFalseCeiling> FalseCeilings;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FHFFixture> Fixtures;

	const FHFWall* FindWall(const FName& WallId) const;
	const FHFRoom* FindRoom(const FName& RoomId) const;

	/**
	 * The deepest beam that SHOWS in a room, or nullptr if none does.
	 *
	 * This is what a false ceiling has to clear, so it is the figure the ceiling drop is chosen
	 * against rather than a free design choice.
	 *
	 * Showing is the whole point, and it is not the same question as crossing. This used to return
	 * only beams clear of every boundary, on the stated grounds that a beam set out on a wall line
	 * is concealed by the wall itself. That is true of a 230 beam over a 230 wall and false of the
	 * same beam over a 115 partition, which stands 57.5 proud of the plaster on both faces for the
	 * whole run. Six of the eight beams in the reference flat are exactly that, and the resulting
	 * ledge round the top of seven rooms was reported as a visual defect while the rule written to
	 * prevent it sat one clause away from firing.
	 *
	 * See DeepestBeamCrossingRoom for the narrower question this used to answer.
	 */
	const FHFBeam* DeepestBeamOverRoom(const FName& RoomId) const;

	/**
	 * The deepest beam that shows along ONE edge of a room's boundary, or nullptr if none does.
	 *
	 * WHICH SIDES OF THE ROOM ACTUALLY NEED BOXING IN. DeepestBeamOverRoom answers for the room as a
	 * whole, and a ceiling that takes that answer wraps a 480 ring round all four walls to bury a nib
	 * that shows along two of them. In the living room that is 15.0 of 23.8 square metres dropped
	 * below the slab, and the reference designs it is meant to look like drop a tenth of that.
	 *
	 * A beam counts here only if it RUNS WITH this edge - on its line, within its own half width,
	 * over a real stretch of it - and then only if a wall does not already conceal it. A beam
	 * crossing the open middle of a room is not this question's business; see DeepestBeamCrossingRoom.
	 */
	const FHFBeam* DeepestBeamOnRoomEdge(const FName& RoomId, int32 EdgeIndex) const;

	/**
	 * The deepest beam crossing the OPEN INTERIOR of a room - clear of every boundary by its own
	 * half width - or nullptr if none does.
	 *
	 * A different and stricter question than DeepestBeamOverRoom. A beam here is one no wall runs
	 * under and no perimeter band can box in; it is a layout fact about the frame rather than a
	 * finish that can absorb it.
	 */
	const FHFBeam* DeepestBeamCrossingRoom(const FName& RoomId) const;

	/** Total floor area of all rooms, in spec units squared. */
	double TotalFloorArea() const;
};

/**
 * Even-odd point-in-polygon, for any closed boundary in this model.
 *
 * A room is not the only thing in a spec with a polygon: a bulkhead carries its own, and asking
 * whether a beam runs under one is the same question as asking whether a fixture stands in a room.
 * FHFRoom::ContainsPoint is this function applied to FHFRoom::Boundary.
 *
 * The closing edge is implicit, as everywhere else here - do not repeat the first point. Even-odd
 * rather than a convex test because half these layouts are L-shaped, and a convex-only test places
 * points inside re-entrant corners that are plainly outside the room.
 */
HOUSEFORGE_API bool HFPolygonContainsPoint(const TArray<FVector2D>& Polygon, const FVector2D& Point);

/** Unit conversion. The single place a drawing's units become Unreal centimetres. */
class HOUSEFORGE_API FHFUnits
{
public:
	/** Scale factor from the given units to Unreal centimetres. */
	static double ToCentimeterScale(EHFUnits Units);

	/** Short name for messages, e.g. "mm", "ft". */
	static FString ShortName(EHFUnits Units);

	/**
	 * Rescales every length in the spec into centimetres in place and sets Units accordingly.
	 * Idempotent: a spec already in centimetres is left untouched.
	 */
	static void ConvertToCentimeters(FHFHouseSpec& Spec);

	/**
	 * Parses a dimension the way it is written on a drawing, returning centimetres.
	 *
	 * Handles the imperial forms that make manual conversion error-prone - 12'-6", 12' 6",
	 * 12.5', 78" - as well as metric with or without a suffix: 3600, 3600mm, 360cm, 3.6m.
	 * A bare number is interpreted in DefaultUnits, which is why a spec must still declare them.
	 *
	 * @return false if the text could not be understood, leaving OutCentimeters untouched.
	 */
	static bool ParseLengthToCentimeters(const FString& Text, EHFUnits DefaultUnits, double& OutCentimeters);
};
