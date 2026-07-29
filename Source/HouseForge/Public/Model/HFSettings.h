// Copyright Siddartha G. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "Model/HFBuildDefaults.h"
#include "HFSettings.generated.h"

/**
 * How this project builds a house: the construction figures behind every generated element.
 *
 * These are the numbers a joiner would set out on a job before cutting anything - board thickness,
 * the shadow gap between shutters, the toe kick, what a door leaf is - and they are here because
 * they are decisions rather than constants. The defaults are what the plugin had compiled in, and
 * they are the right answer for a mid-market Indian flat, so a project that never opens this page
 * gets exactly what it got before.
 *
 * NOT here: numerical tolerances, segment counts, and the margins that keep a boolean well-formed.
 * Those are the generators' own business, they mean nothing outside the file that uses them, and
 * exposing them would only offer a user a way to break their geometry.
 *
 *
 * WHERE THE VALUES LIVE, and why this specifier
 * ---------------------------------------------
 * `config=HouseForge` names a config branch of the plugin's own, NOT Engine and NOT Game. UE's
 * plugin config hierarchy (FPluginManager registers every mounted plugin with FConfigCacheIni)
 * then reads, in order:
 *
 *     Plugins/HouseForge/Config/DefaultHouseForge.ini    the plugin's shipped defaults
 *     <Project>/Config/DefaultHouseForge.ini             this project's overrides
 *
 * That first file is inside the plugin's own git repository, so HouseForge can ship and version its
 * defaults with its code - squarely inside .claude/rules/01-scope.md rather than needing an
 * exception to it.
 *
 * `defaultconfig` sends the editor's writes to <Project>/Config/DefaultHouseForge.ini. That is the
 * host project's Config/, which rule 01 puts out of bounds - but out of bounds for *plugin source
 * changes*. A user dragging a slider in Project Settings and the engine writing the ini is the
 * user's action on the user's project, which is the whole point of a settings page. What the rule
 * is protecting against is this plugin quietly editing project files, and note what is NOT touched:
 * DefaultEngine.ini and DefaultGame.ini are untouched, because the values land in a file of our own
 * name that nothing else shares. Nothing HouseForge does can corrupt a setting belonging to
 * anything else.
 *
 * The specifier deliberately avoided is `EditorPerProjectUserSettings`. It is the most common choice
 * for editor tooling, but GetContainerName() derives the container from the config name and that one
 * resolves to "Editor" - which would file this page under Edit > Editor Preferences, as one user's
 * personal preference. These figures describe the building, not the person modelling it: they belong
 * to the project, they should be committed, and every artist on a job must get the same ones.
 */
UCLASS(config = HouseForge, defaultconfig, meta = (DisplayName = "HouseForge"))
class HOUSEFORGE_API UHFSettings : public UDeveloperSettings
{
	GENERATED_BODY()

public:
	UHFSettings();

	// ================================================================================= openings

	/** Door leaves and sliding door panels. */
	UPROPERTY(config, EditAnywhere, Category = "Openings|Doors",
		meta = (ShowOnlyInnerProperties))
	FHFDoorParams Door;

	/** The two-track aluminium sliding window: the standard window of the flats this plugin builds. */
	UPROPERTY(config, EditAnywhere, Category = "Openings|Sliding Windows",
		meta = (ShowOnlyInnerProperties))
	FHFSlidingWindowParams SlidingWindow;

	/** The top-hung ventilator sash. */
	UPROPERTY(config, EditAnywhere, Category = "Openings|Ventilators",
		meta = (ShowOnlyInnerProperties))
	FHFVentilatorParams Ventilator;

	/** Windows with nothing that moves, and the fallback for a unit too small to divide into sashes. */
	UPROPERTY(config, EditAnywhere, Category = "Openings|Fixed Windows",
		meta = (ShowOnlyInnerProperties))
	FHFFixedWindowParams FixedWindow;

	// ================================================================== joinery: board thickness

	/** Carcass side, top, bottom and partition board, in centimetres. 18 mm faced ply. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.6", UIMax = "3.0"))
	double CarcassBoardThickness = 1.8;

	/** Finished shutter leaf, in centimetres: 18 mm ply plus a laminate on each face. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.9", UIMax = "3.0"))
	double ShutterLeafThickness = 1.9;

	/** Drawer front, in centimetres. Built the same way as a shutter leaf. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.9", UIMax = "3.0"))
	double DrawerFrontThickness = 1.9;

	/** Drawer box side, in centimetres: 12 mm ply, or the 13 mm side of a metal box. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.6", UIMax = "2.0"))
	double DrawerBoxSideThickness = 1.2;

	/** Drawer box bottom, in centimetres. Grooved in above the bottom of the sides. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "1.8"))
	double DrawerBoxBottomThickness = 0.6;

	/** Plinth board, in centimetres. 18 faced ply, or 6 for ply clad in aluminium in a wet kitchen. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Board Thickness",
		meta = (ClampMin = "0.1", UIMin = "0.6", UIMax = "3.0"))
	double PlinthPanelThickness = 1.8;

	// ============================================================ joinery: reveals & clearances

	/**
	 * Total gap between one shutter and the next, in centimetres, taken half from each side.
	 *
	 * The most load-bearing figure on this page. Set it to zero and a run of shutters renders as one
	 * unbroken slab - the clearest tell there is that joinery was generated rather than built. It is
	 * also what a hinged leaf needs in order to swing past its neighbour at all.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Reveals and Clearances",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double ShutterRevealGap = 0.3;

	/** The same shadow line between one drawer front and the next, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Reveals and Clearances",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double DrawerRevealGap = 0.3;

	/** Gap a hinge leaves between a closed leaf's back and the carcass front edges, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Reveals and Clearances",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double ShutterBackClearance = 0.1;

	/** The same behind a drawer front, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Reveals and Clearances",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double DrawerBackClearance = 0.1;

	/** How far a shelf front sits behind the carcass front plane, in centimetres, clear of a shutter. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Reveals and Clearances",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "5.0"))
	double ShelfFrontSetback = 1.0;

	// ======================================================================== joinery: toe kick

	/** Height the carcass stands off the floor on, in centimetres. 10 in a kitchen, 8 under a TV unit. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Plinth",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "20.0"))
	double PlinthHeight = 10.0;

	/**
	 * Toe kick: how far the plinth front sits behind the shutter face, in centimetres.
	 *
	 * This one number is what makes a run read as furniture rather than as a box on the floor. It
	 * puts the base in its own shadow so the carcass appears to float. 5 to 8 in real Indian work.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Plinth",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "15.0"))
	double PlinthFrontRecess = 5.0;

	/** The same setback at an end on show, in centimetres. An end dying into a wall keeps its width. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Plinth",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "15.0"))
	double PlinthEndRecess = 5.0;

	// ========================================================================= joinery: cornice

	/** Front-to-back depth of the moulding, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Cornice",
		meta = (ClampMin = "0.0", UIMin = "1.0", UIMax = "15.0"))
	double CorniceDepth = 4.5;

	/** Height of the moulding, in centimetres. 6 caps a kitchen wall unit; 7.5 tops a wardrobe. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Cornice",
		meta = (ClampMin = "0.0", UIMin = "2.0", UIMax = "20.0"))
	double CorniceHeight = 6.0;

	/**
	 * How far the cornice front stands proud of the shutter face, in centimetres.
	 *
	 * The whole point of the part. Flush with the shutters it is a strip of board; the projection is
	 * what throws the shadow line that reads as a capped run.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Cornice",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	double CorniceProjection = 2.5;

	/** Size of the front-underside feature, in centimetres: the splay leg, the cove radius, the step. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Cornice",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "10.0"))
	double CorniceProfileSize = 2.0;

	/**
	 * Chamfer on the exposed arrises, in centimetres.
	 *
	 * Small and not really optional. A mathematically sharp edge reads as CG under any lighting, and
	 * a cornice sits at eye level in every walkthrough - see .claude/rules/04-conventions.md.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Cornice",
		meta = (ClampMin = "0.0", UIMin = "0.0", UIMax = "1.0"))
	double CorniceEdgeBevel = 0.2;

	// ============================================================ joinery: shelving and hanging

	/** Compartment height the shelf ladder aims for, in centimetres. A shoe rack wants 18. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "1.0", UIMin = "15.0", UIMax = "60.0"))
	double TargetShelfSpacing = 37.5;

	/** Below this a compartment holds nothing a wardrobe is for, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "1.0", UIMin = "10.0", UIMax = "60.0"))
	double MinUsefulCompartment = 30.0;

	/** Ply shelf board, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "0.1", UIMin = "0.6", UIMax = "3.0"))
	double ShelfThicknessPly = 1.8;

	/** Toughened glass shelf, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "0.1", UIMin = "0.4", UIMax = "1.5"))
	double ShelfThicknessGlass = 0.8;

	/** How far a ply shelf spans before it sags on camera, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "1.0", UIMin = "40.0", UIMax = "150.0"))
	double MaxShelfSpanPly = 90.0;

	/** The same for toughened glass, which sags sooner and more visibly, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "1.0", UIMin = "30.0", UIMax = "120.0"))
	double MaxShelfSpanGlass = 60.0;

	/** Hanging rail tube diameter, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "0.1", UIMin = "1.2", UIMax = "5.0"))
	double HangingRailDiameter = 2.5;

	/** Rail centre below the top of its compartment, in centimetres. A hanger needs 65 mm to lift off. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "0.0", UIMin = "3.0", UIMax = "20.0"))
	double HangingRailDrop = 6.5;

	/**
	 * Clear height under a hanging rail below which no rail is fitted, in centimetres.
	 *
	 * A shirt on a hanger wants 95 to 100 and a full-length garment 150, so 90 is a floor rather than
	 * a target - the height below which a rail is not a hanging rail. Worth knowing before changing
	 * it: the composed wardrobe's hanging bay measures 90.8, so it clears the default by 8 mm. Raise
	 * this past that and that bay will correctly report that it has no room to hang, and come back
	 * with the rail omitted rather than with a rail nothing fits under.
	 */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shelving",
		meta = (ClampMin = "0.0", UIMin = "60.0", UIMax = "160.0"))
	double MinHangingClearance = 90.0;

	// ======================================================================== joinery: shutters

	/** Bay width one shutter closes, in centimetres. 60 is where a hinged leaf starts to sag. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shutters",
		meta = (ClampMin = "1.0", UIMin = "25.0", UIMax = "90.0"))
	double ShutterModuleWidth = 45.0;

	/** Swing at open amount 1, in degrees. Concealed hinges open 100-110, not 90. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shutters",
		meta = (ClampMin = "0.0", ClampMax = "180.0", UIMin = "60.0", UIMax = "180.0"))
	double ShutterOpenAngleDegrees = 100.0;

	/** Width of the frame members around a pane in a glazed leaf, in centimetres. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shutters",
		meta = (ClampMin = "0.1", UIMin = "2.0", UIMax = "15.0"))
	double GlazedShutterStileWidth = 6.0;

	/** Pane thickness in a glazed leaf, in centimetres. A real solid, never a plane. */
	UPROPERTY(config, EditAnywhere, Category = "Joinery|Shutters",
		meta = (ClampMin = "0.1", UIMin = "0.3", UIMax = "1.2"))
	double ShutterGlassThickness = 0.5;

	// ======================================================================= validation limits

	/** What the spec validator judges a house against, before anything is built from it. */
	UPROPERTY(config, EditAnywhere, Category = "Validation",
		meta = (ShowOnlyInnerProperties))
	FHFValidationLimits Validation;

	// ================================================================================ resolving

	/**
	 * Snapshot these settings as a plain value.
	 *
	 * The boundary. Past this point nothing in HouseForge knows a settings object exists, and every
	 * generator keeps taking parameter structs it could equally have been handed by a test.
	 */
	FHFBuildDefaults Resolve() const;
};
