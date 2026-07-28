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
	Structure
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

/** The recessed channel of a cove ceiling, which hides an LED strip. */
USTRUCT(BlueprintType)
struct HOUSEFORGE_API FHFCoveProfile
{
	GENERATED_BODY()

	/** Width of the open channel between the band's inner lip and the higher centre. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double ChannelWidth = 10.0;

	/** How far the inner lip rises above the band soffit, shielding the strip from view. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double LipHeight = 5.0;

	/** Distance from the band's inner edge to the channel. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Setback = 2.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	bool bHasLedStrip = true;
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

	/** How far the ceiling hangs below the room's slab. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double Drop = 20.0;

	/** Width of the dropped perimeter band. Ignored by FullDrop. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge", meta = (ClampMin = "0.0"))
	double BandWidth = 60.0;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	FHFCoveProfile Cove;

	/**
	 * Overrides the room boundary when the ceiling does not follow the walls - which is the
	 * normal case for Bulkhead. Empty means "derive from the room".
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> ExplicitPolygon;

	/** Recessed spotlight positions, in spec units. */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "HouseForge")
	TArray<FVector2D> LightPositions;
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
	 * The deepest beam crossing a room, or nullptr if none do.
	 *
	 * This is what a false ceiling has to clear, so it is the figure the ceiling drop is chosen
	 * against rather than a free design choice.
	 */
	const FHFBeam* DeepestBeamOverRoom(const FName& RoomId) const;

	/** Total floor area of all rooms, in spec units squared. */
	double TotalFloorArea() const;
};

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
