"""Authors the HouseForge material library: two master materials and one instance per surface role.

Run in the editor to (re)create the assets under /HouseForge/Materials. They are then committed and
loaded at runtime by FHFMaterialLibrary; nothing calls this at generation time.

    UnrealEditor-Cmd.exe HouseBuilder.uproject ^
        -run=pythonscript -script="Plugins/HouseForge/Scripts/gen_materials.py" ^
        -unattended -nopause -nosplash -stdout

Why a script rather than assets authored by hand: the set has to stay in step with EHFSurfaceRole,
and the only way to see at a glance that it does is to have the roles, their colours and their
finishes written down in one readable list. Re-running is idempotent - each asset is overwritten in
place, so material instances that a level already references keep their identity and no level needs
re-saving.


WHAT THIS REPLACED, AND WHY
===========================

The previous version of this script exposed BaseColor, Metallic, Specular and Roughness as flat
constants and sampled nothing at all. Seventeen roles, seventeen uniform colours. The world-scale
UVs that FHFMeshOps::ApplyWorldScaleUVs works hard to make correct DROVE NOTHING, because no
material read TexCoord. A flat with plaster, vitrified tile, laminate, granite and powder-coated
aluminium all rendering as untextured constant-roughness colour reads as a massing model under any
lighting, however good the geometry is.


WHERE THE DETAIL COMES FROM: THREE SOURCES, AND ONE THAT IS REFUSED
===================================================================

There is no texture library. The plugin ships without one and the user has no asset library yet, so
"add textures" cannot mean "reference textures that do not exist". Detail therefore comes from:

1. THE SHADING MODEL. A clear coat over a diffuse base is not an approximation of a glazed vitrified
   tile, a high-gloss acrylic shutter or a vitreous-china WC - it is physically what those objects
   are. This costs no assets and fixes the BRDF rather than painting over it, which is why it is
   first. See CoatWeight / CoatRoughness.

2. ANALYTIC PATTERN, where the real-world counterpart is itself analytic. A 600 mm tile grid with a
   2 mm joint is a grid: procedural is EXACT here, not an approximation. It stays sharp at any
   camera distance and can be aligned to the room's own corner rather than the world origin, which
   is what makes a floor read as laid tile instead of an infinite sheet clipped by walls.

3. STATISTICAL NOISE, where the real surface is statistical: plaster tooth, powder-coat orange peel,
   granite speckle, roller-sheen drift. Low-frequency ROUGHNESS variation matters most of all - a
   four-metre wall at perfectly uniform roughness is the strongest CG tell in the whole render,
   stronger than the missing albedo map.

REFUSED, BY NAME: procedural WOOD GRAIN, procedural VEINED MARBLE, and anything with a MOTIF
(printed tiles, laminate prints, curtain prints, rugs). Real veneer has cathedral figure, medullary
rays and pore lines - non-stationary structure that noise cannot produce by construction. Stretched
noise gives "CG wood", which is MORE distracting than an honest flat brown at the right gloss. So
DoorLeaf gets a flat tone, a correct semi-gloss and a faint pore bump, and nothing else. The rule
that separates the two: procedural succeeds where the real surface is statistical or geometric, and
fails where it is authored.

WHAT A REAL TEXTURE LIBRARY WOULD ADD LATER, stated plainly because none of the above is a
substitute for it: albedo with actual structure - tile face patterning, veneer figure, fabric print,
granite that is a photograph of granite rather than a statistical stand-in - and normal maps with
authored features rather than filtered noise. Everything here is a correct BRDF with plausible
micro-detail; what it cannot be is a specific real product. The parameter set is built so plugging
one in later is an instance edit and not a re-author - see UseAlbedoMap and friends.


ENGINE CONTENT: WHAT IS REFERENCED AND WHAT IS NOT
==================================================

STARTER CONTENT IS NEVER REFERENCED. It is an optional install component, and on this machine
Engine/Content/StarterContent holds exactly one asset (Textures/T_ground_Moss_D) - no materials, no
meshes. A material referencing it would fail here, before it ever reached a clean project.

The only engine content referenced is the DEFAULT VALUE of the five texture parameters:
/Engine/EngineResources/WhiteSquareTexture and /Engine/EngineMaterials/DefaultNormal. Both are core
engine content present in every install. They are also INERT: every UseXMap static switch defaults
to False, so the sampler is compiled out entirely and the default texture is never fetched. The
reference exists so the parameter has something to show in the details panel until a user assigns
their own map.

No engine material FUNCTION is referenced. World-aligned projection is built from nodes here rather
than calling /Engine/Functions/.../WorldAlignedTexture, so the plugin does not become an
engine-version compatibility surface for content Epic reorganises between releases.

The procedural noise is the engine's Noise node in its Value - Computational mode, which is pure ALU
- no texture lookups and no asset reference of any kind, in any project, on any platform, with no
cook dependency. The tile grid is a small Custom HLSL node, likewise asset-free.


SUBSTRATE IS ON IN THE HOST PROJECT, AND THIS MATERIAL DOES NOT DEPEND ON IT
===========================================================================

HouseBuilder runs r.Substrate=True. That is a host-project renderer setting in Config/, which
.claude/rules/01-scope.md puts out of this plugin's scope - the plugin cannot set it, and must not
assume it. So the graph is written against the legacy pin set (BaseColor / Metallic / Specular /
Roughness / Normal / AmbientOcclusion / ClearCoat / Refraction) rather than against
UMaterialExpressionSubstrateSlabBSDF, which would simply fail to shade in any project with Substrate
off. With Substrate on, the engine converts these pins into a slab - a coated slab where CoatWeight
is non-zero - so the physical improvement lands either way and nothing is conditional on a setting
this repository does not own.


THE MILLIMETRE CONTRACT
=======================

FHFMeshOps::ApplyWorldScaleUVs unwraps UV0 so one UV unit is exactly TexelSizeCm of surface, along
every edge of every triangle, in every chart. TexelSizeCm is 100 - see FHFRenderFinish::TexelSizeCm
- so one UV unit is one metre.

That is what lets tiling be expressed in millimetres and MEAN it:

    repeats per UV unit = UVWorldSizeCm * 10 / TilingMM

UVWorldSizeCm is a parameter rather than a baked 100 so the material and the unwrap cannot drift
apart silently; HouseForge.Materials.TilingIsInMillimetres asserts the two agree. This project
converts millimetres to centimetres exactly once, at spec ingest, and has been bitten at that
boundary - so the conversion here is written out in full at the site it happens rather than folded
into a constant.
"""

import unreal

FOLDER = "/HouseForge/Materials"

OPAQUE_PARENT = "M_HF_Surface"
GLAZED_PARENT = "M_HF_SurfaceGlazed"

# The world size one UV unit covers, in centimetres. MUST equal FHFRenderFinish::TexelSizeCm; a test
# asserts it, because a mismatch here makes every tiling number in the flat quietly wrong by a ratio
# while every individual value still looks reasonable.
UV_WORLD_SIZE_CM = 100.0

# Texture parameter defaults. Core engine content, and never sampled while the matching UseXMap
# switch is False - which is the default for all five. See the header.
WHITE_TEXTURE = "/Engine/EngineResources/WhiteSquareTexture.WhiteSquareTexture"
FLAT_NORMAL_TEXTURE = "/Engine/EngineMaterials/DefaultNormal.DefaultNormal"


# =================================================================================================
#
# The tile pattern block.
#
# A Custom HLSL node rather than twenty wired nodes, because the grid maths reads as maths and would
# be unreviewable as a graph. It declares no helper functions on purpose: the translator pastes this
# verbatim INSIDE a generated function body (FHLSLMaterialTranslator::CustomExpression), so a nested
# declaration would not compile. Everything statistical is left to the engine's Noise node.
#
# FILTERING IS NOT OPTIONAL. UVWidth is the pattern-space pixel footprint, computed in the graph from
# real DDX/DDY nodes. A grout line thinner than a pixel drawn with a hard step moires; the same line
# drawn with a smoothstep over the footprint stays a clean grey line all the way out. Past the point
# where a whole tile is a couple of pixels wide the grid cannot be resolved at all, so it is faded
# out rather than left to alias.
#
# =================================================================================================

HF_TILE_HLSL = r"""
// UV is in TILES: one unit is one tile, by construction of the UV frame outside this node.
float2 Cell = floor(UV);
float2 InTile = UV - Cell;

// Distance to the nearest tile edge, in tiles.
float2 EdgeDist = min(InTile, 1.0 - InTile);
float Near = min(EdgeDist.x, EdgeDist.y);

float HalfJoint = GroutFrac * 0.5;
float AA = max(UVWidth, 1e-5);
float Grout = 1.0 - smoothstep(HalfJoint - AA, HalfJoint + AA, Near);

// No joint width means no grid at all, and no grid means no per-tile shade either: a role with no
// tile module must not pick up blotches on a cell boundary that is not there.
float HasGrid = (GroutFrac > 0.0) ? 1.0 : 0.0;
Grout *= HasGrid;

// Once a whole tile is down to a couple of pixels the grid is below the resolution limit and
// drawing it produces moire rather than detail.
Grout *= saturate(1.0 - UVWidth * 3.0);

// Real vitrified tiles are shade-batched and visibly vary from box to box. Keyed on the cell index,
// so a tile is one flat shade edge to edge, which is what a laid floor looks like.
TileShade = (frac(sin(dot(Cell, float2(127.1, 311.7))) * 43758.5453123) * 2.0 - 1.0) * HasGrid;

return Grout;
"""


# The fine bump, in ITS OWN NODE, and the separation is the whole point.
#
# It started life as three more outputs on the tile node above, which was wrong in a way that only
# surfaced when a test asked what UseProceduralBump actually saved: NOTHING. The tile node's grout
# and shade outputs are always used, so the node was always compiled, so the three Noise evaluations
# feeding it were always evaluated - and the static switch merely chose whether to LOOK at a normal
# that had already been paid for in full. Four material instances had taken on their own shader
# permutation in exchange for no saving whatsoever.
#
# Split out, the switch does what it says: with UseProceduralBump off nothing downstream reads this
# node, so the node and the three Noise evaluations feeding it are dead and the compiler drops them.
HF_BUMP_HLSL = r"""
// A tangent-space normal from three noise samples taken in UV space - so the gradient really is in
// the tangent frame the shader will light it in, rather than a world-space slope reinterpreted as
// one.
//
// BUMPSTRENGTH IS A SURFACE SLOPE, and giving it that meaning is what makes it safe. The samples sit
// HF_BUMP_STEP apart in noise space, so dividing the difference by that step turns it into a real
// gradient; multiplying by BumpStrength then makes the number the tangent of the steepest slope the
// surface ever reaches. 0.03 is about 1.7 degrees.
//
// It used to be an arbitrary factor of sixteen on an undivided difference, which is dimensionless,
// unrelated to the feature size, and roughly a hundred times too strong. At a 2 mm feature size that
// still read as a fine tooth and looked fine; at the 30-60 mm of powder-coat orange peel and ceiling
// trowel it turned into centimetre-wide dents, and every gloss surface in the flat came back looking
// like hammered metal. Real orange peel is tens of MICRONS deep over tens of millimetres - a slope of
// well under a thousandth - so the honest values here are far smaller than they look.
if (BumpStrength <= 1e-6)
{
    return float3(0.0, 0.0, 1.0);
}

float Dx = ((NoiseX - NoiseC) / HF_BUMP_STEP) * BumpStrength;
float Dy = ((NoiseY - NoiseC) / HF_BUMP_STEP) * BumpStrength;
return normalize(float3(-Dx, -Dy, 1.0));
"""

# How far apart, in noise units, the two offset samples sit from the centre one. Small enough that
# the difference is a local gradient rather than a chord across a whole feature, large enough that
# the value noise has actually changed between them.
HF_BUMP_STEP = 0.25


# =================================================================================================
#
# The roles.
#
# In EHFSurfaceRole order. The name must match the enumerator exactly: FHFMaterialLibrary builds the
# asset path from StaticEnum's name string, so a mismatch shows up as a role that renders in the
# default checkerboard.
#
# Colours are sRGB, the space they were picked in, and converted to linear below. Entering them
# straight as linear is the classic way to end up with a flat that is uniformly too dark and too
# saturated while every individual number looks reasonable in the diff.
#
# Chosen as a plausible mid-range Indian flat rather than as a colour key: acrylic emulsion over
# gypsum putty, double-charge vitrified tile, POP false ceiling, pre-laminated ply carcasses, dark
# speckled granite counters, powder-coated aluminium windows.
#
# =================================================================================================

def role(name, colour, rough, metal=0.0, spec=0.5, opacity=None,
         tiling_mm=1000.0, coat=0.0, coat_rough=0.06,
         grout_mm=0.0, grout_colour=None, grout_rough=0.7,
         macro_rough=0.03, macro_albedo=0.010, macro_mm=1500.0,
         bump=0.0, bump_mm=2.0, tile_shade=0.0, emissive=0.0):
    return dict(name=name, colour=colour, rough=rough, metal=metal, spec=spec, opacity=opacity,
                tiling_mm=tiling_mm, coat=coat, coat_rough=coat_rough,
                grout_mm=grout_mm, grout_colour=grout_colour, grout_rough=grout_rough,
                macro_rough=macro_rough, macro_albedo=macro_albedo, macro_mm=macro_mm,
                bump=bump, bump_mm=bump_mm, tile_shade=tile_shade, emissive=emissive)


ROLES = [
    # Acrylic emulsion over gypsum putty on POP - Asian Paints Tractor/Royale class, matt. Warm
    # ivory, never pure white: a real emulsion sits around 0.72-0.78 linear albedo.
    #
    # THE LARGEST SURFACE BY AREA IN THE RENDER, so it is where uniform roughness is most visible and
    # where the cheapest fix pays most. Macro drift is turned up here above every other role.
    role("WallPaint", (0.902, 0.886, 0.859), 0.85,
         macro_rough=0.045, macro_albedo=0.012, macro_mm=1200.0, bump=0.022, bump_mm=2.0),

    # Double-charge / GVT vitrified tile - Kajaria, Somany, Johnson - 600x600, glossy polished, laid
    # with 2 mm spacers. The highest-gloss large surface in the flat and THE priority role.
    #
    # The coat is the physics: a glazed body really is a rough-ish ceramic under a thin smooth glaze,
    # and one slab with two lobes is what that is. The ROUGHNESS contrast across the joint reads far
    # more strongly than the colour contrast, which is why the grout roughness is double the field.
    # Under Lumen the surface cache reads this albedo, so the floor tone colours every bounce.
    role("FloorFinish", (0.847, 0.824, 0.784), 0.35, spec=0.55,
         tiling_mm=600.0, coat=0.6, coat_rough=0.08,
         grout_mm=2.0, grout_colour=(0.722, 0.698, 0.659), grout_rough=0.70,
         macro_rough=0.020, macro_albedo=0.006, tile_shade=0.020),

    # POP / gypsum board, trowelled and painted matt white distemper. Matter than the walls - POP
    # takes paint flatter than putty. What sells a false ceiling is the AO in its step and the cove
    # shadow, not texture, so little instruction budget is spent here. High albedo matters because
    # this is the surface that returns the uplight.
    role("CeilingSoffit", (0.941, 0.933, 0.918), 0.90,
         macro_rough=0.025, macro_albedo=0.008, macro_mm=700.0, bump=0.004, bump_mm=40.0),

    # The inside face of the cove pocket: the surface an LED strip washes and the surface Lumen
    # bounces that wash off. A lighting decision wearing a material's clothes.
    #
    # DELIBERATELY THE BRIGHTEST ALBEDO AND THE MATTEST SURFACE IN THE SET, with no bump at all. Any
    # gloss here produces a hot streak reflection of the strip instead of a soft wash, and any albedo
    # drop kills the cove's throw.
    role("CoveInterior", (0.957, 0.949, 0.933), 0.92, spec=0.35,
         macro_rough=0.010, macro_albedo=0.004, bump=0.0),

    # Almost always the floor tile cut down to a 75-100 mm band, not a separate material - so it
    # matches FloorFinish exactly, coat included. No grid: a 100 mm band cut from a 600 mm tile shows
    # a joint only where the floor's own joint runs into it, and drawing one on the band itself is
    # the tell that it was authored as a separate object.
    role("Skirting", (0.847, 0.824, 0.784), 0.35, spec=0.55,
         tiling_mm=600.0, coat=0.6, coat_rough=0.08,
         macro_rough=0.020, macro_albedo=0.006),

    # Pre-laminated particle board / BWR ply carcass, matt to satin. Seen mostly as the inside of a
    # wardrobe and the sides of a base unit, lit indirectly, so its job is to be a believable warm
    # neutral rather than to be looked at.
    role("JoineryCarcass", (0.788, 0.729, 0.635), 0.62,
         tiling_mm=1200.0, macro_rough=0.025, bump=0.004, bump_mm=10.0),

    # High-gloss acrylic / post-laminated shutter fronts. THE COAT IS THE POINT: a gloss shutter is a
    # pigmented base under a thick clear layer, and that is a coat, not a low roughness number. With
    # roughness alone it reads as painted metal.
    role("ShutterLaminate", (0.310, 0.396, 0.388), 0.42, spec=0.55,
         tiling_mm=1200.0, coat=0.5, coat_rough=0.10,
         macro_rough=0.015, macro_albedo=0.005, bump=0.0015, bump_mm=30.0),

    # Speckled granite - Black Galaxy / Steel Grey - rather than a veined marble, and that is a
    # deliberate refusal. Statuario veining from noise is camouflage every time; a speckle IS
    # statistical, so noise at the right scale is the correct model rather than a stand-in for one.
    # Polished but not mirror: the coat carries the polish, the base carries the stone.
    role("CounterStone", (0.161, 0.161, 0.176), 0.30, spec=0.60,
         tiling_mm=400.0, coat=0.5, coat_rough=0.09,
         macro_rough=0.050, macro_albedo=0.110, macro_mm=15.0, bump=0.010, bump_mm=1.5),

    # THE ONE TRANSMISSIVE ROLE. A window drawn as an opaque pane reads as a boarded-up hole; a
    # window drawn as flat translucency reads as a plastic film. See build_glazed_master.
    role("Glass", (0.925, 0.965, 0.949), 0.02, spec=1.0, opacity=0.06,
         macro_rough=0.0, macro_albedo=0.0, bump=0.0),

    # Chrome-plated brass and satin stainless: handles, hinges, taps, rails. Metallic, and rough
    # enough to be satin rather than a mirror, because a mirror-finish handle in an untextured room
    # reflects nothing and reads as a grey blob.
    role("MetalHardware", (0.706, 0.714, 0.722), 0.24, metal=1.0,
         tiling_mm=200.0, macro_rough=0.030, macro_albedo=0.0, macro_mm=120.0,
         bump=0.008, bump_mm=1.0),

    # Flush door, membrane or veneered, semi-gloss PU. FLAT TONE AND CORRECT GLOSS, WITH A FAINT PORE
    # BUMP AND NOTHING ELSE - see the header's refusal. Procedural wood grain from stretched noise is
    # the uncanny middle: it reads worse than an honest brown at the right sheen, because real veneer
    # figure is authored structure that noise cannot produce.
    role("DoorLeaf", (0.478, 0.325, 0.216), 0.45, spec=0.5,
         tiling_mm=900.0, coat=0.3, coat_rough=0.14,
         macro_rough=0.030, macro_albedo=0.020, macro_mm=800.0, bump=0.015, bump_mm=3.0),

    # Powder-coated aluminium sliding window sections. ORANGE PEEL AT 20-40 MM is the entire visual
    # signature of powder coat and exactly the kind of statistical micro-relief noise is right for.
    # Metallic under the coat, which is what makes a section read as aluminium rather than grey
    # plastic.
    role("WindowFrame", (0.290, 0.298, 0.310), 0.38, metal=1.0,
         tiling_mm=300.0, macro_rough=0.020, bump=0.0015, bump_mm=30.0),

    # Vitreous china: WC, basin, cistern. A glaze over a body - one slab, two lobes, physically what
    # the object is. The most obviously wrong surface in the old set, because sanitaryware with no
    # coat reads as painted plaster.
    role("Sanitary", (0.965, 0.965, 0.957), 0.30, spec=0.5,
         tiling_mm=600.0, coat=0.7, coat_rough=0.05,
         macro_rough=0.008, macro_albedo=0.004),

    # Upholstery, curtains, mattress ticking, bedding. Very rough, with a weave bump at 0.5-1.5 mm,
    # which is the closest a non-cloth shading model gets to the grazing-angle sheen that is cloth's
    # real signature. NO PRINT: a motif is authored, not statistical.
    role("Fabric", (0.522, 0.463, 0.408), 0.95, spec=0.2,
         tiling_mm=150.0, macro_rough=0.020, macro_albedo=0.025, macro_mm=400.0,
         bump=0.045, bump_mm=1.2),

    # Fridge, hob, chimney, washing machine: painted steel and brushed stainless panels.
    role("Appliance", (0.741, 0.749, 0.757), 0.26, metal=1.0,
         tiling_mm=400.0, coat=0.3, coat_rough=0.10,
         macro_rough=0.018, bump=0.0015, bump_mm=25.0),

    # Exposed structure - beams and columns - in plastered RCC. Reads as the walls do, one shade
    # cooler and greyer so a dropped beam is legible as structure rather than as a fold in the wall.
    role("Structure", (0.678, 0.671, 0.655), 0.88,
         macro_rough=0.040, macro_albedo=0.012, macro_mm=1500.0, bump=0.014, bump_mm=2.5),

    # THE ONE ROLE THAT EMITS. A cove hides its strip from every camera in the flat by construction,
    # so with nothing emissive and no light in the trough there was, correctly, nothing to see. Warm
    # white at 3000 K, which is what these flats are lit with, and the intensity is a stop below where
    # the bloom takes over the frame, because the wash on the slab is the subject and not the strip.
    role("LightSource", (1.0, 0.894, 0.769), 0.35,
         macro_rough=0.0, macro_albedo=0.0, bump=0.0, emissive=12.0),

    # SILVERED GLASS, NOT GLAZING. A mirror and a window pane were both tagged Glass, which was
    # invisible while Glass was a flat translucent blue-grey and becomes very visible the moment Glass
    # gains real transmission: every mirror in the flat would turn into a hole through the wall.
    #
    # A mirror is a front-surface reflector - metallic, almost perfectly smooth, very slightly warm
    # because silver is - and it is opaque. Its bevel is the only part that catches light directly,
    # and that bevel is geometry, which FHFWallPlateKit::BuildMirror already lofts.
    role("Mirror", (0.972, 0.960, 0.915), 0.02, metal=1.0, spec=1.0,
         macro_rough=0.004, macro_albedo=0.0, bump=0.0),
]

# Roles whose procedural bump is compiled out entirely rather than merely set to zero strength.
# UseProceduralBump is a STATIC switch, so this removes three noise evaluations from the shader
# instead of multiplying their result by nothing.
NO_BUMP = {"CoveInterior", "Glass", "LightSource", "Mirror"}


# =================================================================================================
#
# Graph construction helpers.
#
# =================================================================================================

MEL = unreal.MaterialEditingLibrary


def srgb_to_linear(c):
    """The sRGB transfer function. Unreal's vector parameters are linear."""
    if c <= 0.04045:
        return c / 12.92
    return ((c + 0.055) / 1.055) ** 2.4


def linear_colour(srgb, alpha=1.0):
    r, g, b = srgb
    return unreal.LinearColor(srgb_to_linear(r), srgb_to_linear(g), srgb_to_linear(b), alpha)


def empty_material(material, name):
    """Strips a master back to nothing, and REFUSES TO CARRY ON IF IT COULD NOT.

    delete_all_material_expressions on its own is not enough, and quietly so. Re-running this script
    over an already-authored master left 8 texture parameters where 5 were built and 31 samplers
    where 20 were - nodes from the previous author that survived the wipe, wired to nothing,
    inflating the shader and making the script's documented idempotence false. It surfaced only
    because HouseForge.Materials.MastersActuallySampleTextures counts what it finds; nothing about
    the resulting material looked wrong.

    The cause is the output pin: an expression still referenced by a material property input is not
    a free-floating node, so the property inputs are disconnected FIRST and the material is taken off
    material-attributes mode, which is where the whole graph hangs from. Then whatever is left is
    deleted one at a time, and the count is checked. A leak here is not something to discover later.
    """
    material.set_editor_property("use_material_attributes", False)

    for prop in (unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES, unreal.MaterialProperty.MP_BASE_COLOR,
                 unreal.MaterialProperty.MP_ROUGHNESS, unreal.MaterialProperty.MP_METALLIC,
                 unreal.MaterialProperty.MP_SPECULAR, unreal.MaterialProperty.MP_NORMAL,
                 unreal.MaterialProperty.MP_AMBIENT_OCCLUSION, unreal.MaterialProperty.MP_OPACITY,
                 unreal.MaterialProperty.MP_REFRACTION, unreal.MaterialProperty.MP_EMISSIVE_COLOR):
        MEL.disconnect_material_property(material, prop)

    MEL.delete_all_material_expressions(material)

    for expression in list(MEL.get_material_expressions(material)):
        MEL.delete_material_expression(material, expression)

    left = MEL.get_num_material_expressions(material)
    if left != 0:
        raise RuntimeError(
            "{} still holds {} expression(s) after being emptied; re-authoring on top of them would "
            "produce a material with duplicate parameters and no way to tell".format(name, left))


def replace_asset(name, asset_class, factory):
    """Returns the asset, re-authored from scratch: loaded and emptied, or created if absent.

    IN PLACE RATHER THAN DELETE-AND-RECREATE, which is what this used to do and could not do
    reliably. Under -run=pythonscript the asset registry has not finished its scan when the script
    starts, so does_asset_exist answers False for a .uasset sitting right there on disk: the delete
    is skipped, create_asset then refuses with "already exists in package", and it surfaces as a
    None three calls away inside set_editor_property. load_asset does not care about the registry -
    it loads the package by path - so that is what decides.

    Re-authoring in place is the better behaviour anyway: the asset keeps its identity, so every
    material instance and every saved level goes on pointing at it and nothing needs re-saving.
    """
    path = "{}/{}".format(FOLDER, name)

    existing = unreal.EditorAssetLibrary.load_asset(path)
    if existing is not None:
        if isinstance(existing, unreal.Material):
            empty_material(existing, name)
        return existing

    tools = unreal.AssetToolsHelpers.get_asset_tools()
    return tools.create_asset(name, FOLDER, asset_class, factory)


class Graph:
    """A material under construction.

    EVERY WIRING CALL IS CHECKED. connect_material_expressions returns False for a misspelt input
    name and carries on, which produces a material that compiles, renders, and silently ignores the
    branch that was supposed to be wired into it - the exact class of failure this milestone exists
    to fix. Nothing here is allowed to fail quietly, and the failure message carries the node's real
    input names so the next misspelling takes one run to find rather than a bisect.
    """

    def __init__(self, material):
        self.material = material

    def node(self, cls, x, y):
        n = MEL.create_material_expression(self.material, cls, x, y)
        if n is None:
            raise RuntimeError("could not create a {}".format(cls))
        return n

    def wire(self, src, src_out, dst, dst_in):
        if not MEL.connect_material_expressions(src, src_out, dst, dst_in):
            raise RuntimeError(
                "could not wire {}[{}] -> {}[{}]; valid inputs are {}, valid outputs of source are {}".format(
                    src.get_name(), src_out or "<default>", dst.get_name(), dst_in,
                    list(MEL.get_material_expression_input_names(dst)),
                    list(MEL.get_material_expression_output_names(src))))

    def to_property(self, src, src_out, prop):
        if not MEL.connect_material_property(src, src_out, prop):
            raise RuntimeError("could not wire {} to material property {}".format(src.get_name(), prop))

    # ---- parameters ----------------------------------------------------------------------------

    def _named(self, cls, name, x, y, group, sort):
        n = self.node(cls, x, y)
        n.set_editor_property("parameter_name", name)
        n.set_editor_property("group", group)
        n.set_editor_property("sort_priority", sort)
        return n

    def scalar(self, name, default, x, y, group="", sort=0):
        n = self._named(unreal.MaterialExpressionScalarParameter, name, x, y, group, sort)
        n.set_editor_property("default_value", default)
        return n

    def vector(self, name, default, x, y, group="", sort=0):
        n = self._named(unreal.MaterialExpressionVectorParameter, name, x, y, group, sort)
        n.set_editor_property("default_value", default)
        return n

    def switch(self, name, default, x, y, group="", sort=0):
        n = self._named(unreal.MaterialExpressionStaticSwitchParameter, name, x, y, group, sort)
        n.set_editor_property("default_value", default)
        return n

    def texture(self, name, default_path, x, y, group="", sort=0):
        n = self._named(unreal.MaterialExpressionTextureObjectParameter, name, x, y, group, sort)
        # unreal.load_asset rather than EditorAssetLibrary.load_asset: under -run=pythonscript the
        # asset registry has not scanned /Engine, so the registry-backed loader reports these core
        # textures as missing while StaticLoadObject finds them without complaint.
        tex = unreal.load_asset(default_path)
        if tex is None:
            raise RuntimeError("default texture '{}' is missing".format(default_path))
        n.set_editor_property("texture", tex)
        return n

    # ---- arithmetic ----------------------------------------------------------------------------

    def const(self, value, x, y):
        n = self.node(unreal.MaterialExpressionConstant, x, y)
        n.set_editor_property("r", value)
        return n

    def const3(self, r, g, b, x, y):
        n = self.node(unreal.MaterialExpressionConstant3Vector, x, y)
        n.set_editor_property("constant", unreal.LinearColor(r, g, b, 1.0))
        return n

    def binary(self, cls, a, b, x, y, a_out="", b_out=""):
        n = self.node(cls, x, y)
        self.wire(a, a_out, n, "A")
        self.wire(b, b_out, n, "B")
        return n

    def mul(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionMultiply, a, b, x, y, a_out, b_out)

    def add(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionAdd, a, b, x, y, a_out, b_out)

    def sub(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionSubtract, a, b, x, y, a_out, b_out)

    def div(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionDivide, a, b, x, y, a_out, b_out)

    def append(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionAppendVector, a, b, x, y, a_out, b_out)

    def dot(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionDotProduct, a, b, x, y, a_out, b_out)

    def maximum(self, a, b, x, y, a_out="", b_out=""):
        return self.binary(unreal.MaterialExpressionMax, a, b, x, y, a_out, b_out)

    def unary(self, cls, a, x, y, a_out=""):
        n = self.node(cls, x, y)
        self.wire(a, a_out, n, "")
        return n

    def lerp(self, a, b, alpha, x, y, a_out="", b_out="", alpha_out=""):
        n = self.node(unreal.MaterialExpressionLinearInterpolate, x, y)
        self.wire(a, a_out, n, "A")
        self.wire(b, b_out, n, "B")
        self.wire(alpha, alpha_out, n, "Alpha")
        return n

    def clamp(self, a, x, y, a_out="", low=0.0, high=1.0):
        n = self.node(unreal.MaterialExpressionClamp, x, y)
        n.set_editor_property("min_default", low)
        n.set_editor_property("max_default", high)
        self.wire(a, a_out, n, "")
        return n

    def mask(self, a, x, y, r=False, g=False, b=False, a_ch=False, a_out=""):
        n = self.node(unreal.MaterialExpressionComponentMask, x, y)
        n.set_editor_property("r", r)
        n.set_editor_property("g", g)
        n.set_editor_property("b", b)
        n.set_editor_property("a", a_ch)
        self.wire(a, a_out, n, "")
        return n

    def power(self, base, exponent, x, y, base_out="", exp_out=""):
        n = self.node(unreal.MaterialExpressionPower, x, y)
        self.wire(base, base_out, n, "Base")
        self.wire(exponent, exp_out, n, "Exp")
        return n

    def pick(self, name, when_true, when_false, x, y, group="", sort=0, default=False,
             true_out="", false_out=""):
        """A static switch parameter. The unchosen branch is compiled out, not merely unused."""
        n = self.switch(name, default, x, y, group, sort)
        self.wire(when_true, true_out, n, "True")
        self.wire(when_false, false_out, n, "False")
        return n

    def noise(self, position, filter_width, x, y, pos_out="", scale=1.0, levels=3):
        """Value - Computational: ~53 instructions per level and NO TEXTURE LOOKUPS AT ALL.

        The mode matters. The texture-based modes are higher quality but sample
        View.PerlinNoiseGradientTexture, and the computational one is pure ALU - which is what makes
        this whole material free of any asset reference. FilterWidth is fed the real pixel footprint
        in noise space; leaving it at zero is what makes procedural detail crawl.
        """
        n = self.node(unreal.MaterialExpressionNoise, x, y)
        n.set_editor_property("noise_function", unreal.NoiseFunction.NOISEFUNCTION_VALUE_ALU)
        n.set_editor_property("scale", scale)
        n.set_editor_property("levels", levels)
        n.set_editor_property("turbulence", False)
        n.set_editor_property("output_min", -1.0)
        n.set_editor_property("output_max", 1.0)
        self.wire(position, pos_out, n, "World Position")
        self.wire(filter_width, "", n, "FilterWidth")
        return n


GROUP_SURFACE = "01 Surface"
GROUP_TILING = "02 Tiling"
GROUP_MAPS = "03 Texture Maps"
GROUP_DETAIL = "04 Procedural Detail"
GROUP_GROUT = "05 Grout"
GROUP_GLASS = "06 Glass"
GROUP_EMISSIVE = "07 Emissive"


# =================================================================================================
#
# The shared front end: how a UV and a world position become the coordinate every map samples at.
#
# =================================================================================================

def build_uv_frame(g):
    """The tiled, rotated, offset UV, the world-space position, and the pixel footprint of each.

    THE ORDER IS ROTATE, THEN SCALE, THEN OFFSET, and the offset is applied in TILES rather than in
    UV units. That is deliberate: the useful thing a user does with the offset is slide a tile grid
    so a whole tile lands in the room's corner, and expressing that in tiles means the number they
    type is "a third of a tile" rather than a value whose meaning changes whenever the tiling does.
    The room-corner grout alignment a laid floor needs is exactly this, and it stays a material-side
    offset because UV0 is world-anchored on floors and ceilings.
    """
    tiling_mm = g.scalar("TilingMM", 1000.0, -2600, -600, GROUP_TILING, 0)
    uv_world_cm = g.scalar("UVWorldSizeCm", UV_WORLD_SIZE_CM, -2600, -480, GROUP_TILING, 1)
    rotation = g.scalar("TilingRotationDegrees", 0.0, -2600, -360, GROUP_TILING, 2)
    offset = g.vector("TilingOffsetTiles", unreal.LinearColor(0, 0, 0, 0), -2600, -240,
                      GROUP_TILING, 3)

    # ---- repeats per UV unit: UVWorldSizeCm * 10 / TilingMM ------------------------------------
    #
    # Written out rather than folded into one constant so the millimetre-to-centimetre step is
    # visible at the site it happens. This project converts mm to cm exactly once, at spec ingest,
    # and has been bitten at that boundary; the material is the second place the two units meet.
    mm_per_cm = g.const(10.0, -2400, -520)
    uv_world_mm = g.mul(uv_world_cm, mm_per_cm, -2200, -500)
    repeats = g.div(uv_world_mm, tiling_mm, -2000, -560)

    # ---- rotation ------------------------------------------------------------------------------
    deg_to_rad = g.const(3.14159265358979 / 180.0, -2400, -340)
    radians = g.mul(rotation, deg_to_rad, -2200, -360)

    cos_n = g.unary(unreal.MaterialExpressionCosine, radians, -2000, -400)
    cos_n.set_editor_property("period", 6.283185307179586)
    sin_n = g.unary(unreal.MaterialExpressionSine, radians, -2000, -280)
    sin_n.set_editor_property("period", 6.283185307179586)

    tex_coord = g.node(unreal.MaterialExpressionTextureCoordinate, -2600, -100)
    tex_coord.set_editor_property("coordinate_index", 0)

    u = g.mask(tex_coord, -2400, -140, r=True)
    v = g.mask(tex_coord, -2400, -40, g=True)

    # U' = U*cos - V*sin ; V' = U*sin + V*cos
    ru = g.sub(g.mul(u, cos_n, -2200, -180), g.mul(v, sin_n, -2200, -80), -2000, -140)
    rv = g.add(g.mul(u, sin_n, -2200, 20), g.mul(v, cos_n, -2200, 120), -2000, 60)
    rotated = g.append(ru, rv, -1800, -40)

    scaled = g.mul(rotated, repeats, -1600, -40)
    offset_xy = g.mask(offset, -1800, 100, r=True, g=True)
    uv = g.add(scaled, offset_xy, -1400, 0)

    # ---- the pixel footprint of the RAW UV ------------------------------------------------------
    #
    # max(length(ddx(UV0)), length(ddy(UV0))), measured once on the unscaled coordinate and then
    # scaled per consumer. Rotation is rigid and translation is constant, so the footprint of any
    # derived coordinate is this one times that coordinate's own scale - which is two multiplies
    # rather than two more derivative pairs, and is exactly equal rather than an approximation.
    ddx = g.node(unreal.MaterialExpressionDDX, -2400, 240)
    g.wire(tex_coord, "", ddx, "Value")
    ddy = g.node(unreal.MaterialExpressionDDY, -2400, 360)
    g.wire(tex_coord, "", ddy, "Value")

    raw_width = g.maximum(g.unary(unreal.MaterialExpressionLength, ddx, -2200, 240),
                          g.unary(unreal.MaterialExpressionLength, ddy, -2200, 360),
                          -2000, 300)

    uv_width = g.mul(raw_width, repeats, -1800, 300)

    world_pos = g.node(unreal.MaterialExpressionWorldPosition, -2600, 480)

    return dict(uv=uv, uv_width=uv_width, raw_width=raw_width, raw_uv=tex_coord,
                world_pos=world_pos, tiling_mm=tiling_mm, uv_world_cm=uv_world_cm,
                repeats=repeats, mm_per_cm=mm_per_cm)


def build_world_aligned(g, frame):
    """Three world-planar coordinate sets and the normal-derived weights that blend them.

    WHY THIS IS NOT AN ENGINE FUNCTION CALL. /Engine/Functions/.../WorldAlignedTexture would do it in
    one node and is stable content, but it also samples the texture itself - so the projection could
    not be swapped without swapping the sampler too, and the plugin would take on an engine-version
    compatibility surface for a graph that is fifteen nodes.

    THE SIGN CORRECTION IS THE POINT. Projecting +X and -X onto the same (Y, Z) plane makes +U run
    one way on the front face of a panel and the opposite way on its back. Handedness is carried
    per-vertex so the lighting maths stays correct, but any directional texture - wood grain, tile
    grout, brushed metal, a plaster trowel direction - runs backwards on half the surfaces in the
    flat, and a mirrored normal map is a well-known tell. The identical bug was found and fixed in
    FHFMeshOps' unwrap; this is the projection-mode half of the same defect.
    """
    normal = g.node(unreal.MaterialExpressionVertexNormalWS, -1200, 900)
    abs_n = g.unary(unreal.MaterialExpressionAbs, normal, -1000, 900)

    # Sharpened so a surface is dominated by one plane over most of its sweep and the crossfade is a
    # narrow band, rather than a permanent three-way average that halves the contrast of every map.
    powered = g.power(abs_n, g.const(4.0, -1000, 1020), -800, 900)
    total = g.dot(powered, g.const3(1.0, 1.0, 1.0, -800, 1020), -600, 900)
    weights = g.div(powered, total, -400, 900)

    wx = g.mask(weights, -200, 840, r=True)
    wy = g.mask(weights, -200, 940, g=True)
    wz = g.mask(weights, -200, 1040, b=True)

    # Tiling as a world length in centimetres. The world-aligned branch needs no mesh UV at all:
    # world position is already in centimetres, so this is the whole of the millimetre conversion.
    tiling_cm = g.div(frame["tiling_mm"], frame["mm_per_cm"], -1000, 1160)
    pos = g.div(frame["world_pos"], tiling_cm, -800, 1160)

    px = g.mask(pos, -600, 1120, r=True)
    py = g.mask(pos, -600, 1220, g=True)
    pz = g.mask(pos, -600, 1320, b=True)

    nx = g.mask(normal, -600, 1420, r=True)
    ny = g.mask(normal, -600, 1520, g=True)
    nz = g.mask(normal, -600, 1620, b=True)

    def flipped(component, face_normal, x, y):
        """component * sign(face normal), so the two faces of a slab are not mirror images."""
        return g.mul(component, g.unary(unreal.MaterialExpressionSign, face_normal, x, y + 60), x + 200, y)

    uvx = g.append(flipped(py, nx, -400, 1120), pz, -100, 1140)
    uvy = g.append(flipped(px, ny, -400, 1280), pz, -100, 1300)
    uvz = g.append(flipped(px, nz, -400, 1440), py, -100, 1460)

    # The world-aligned footprint, in the same units as its coordinates: one UV unit is
    # UVWorldSizeCm of surface, and one world-aligned unit is TilingMM/10 of it.
    width_cm = g.mul(frame["raw_width"], frame["uv_world_cm"], -400, 1700)
    tri_width = g.div(width_cm, tiling_cm, -200, 1700)

    return dict(uvx=uvx, uvy=uvy, uvz=uvz, wx=wx, wy=wy, wz=wz, width=tri_width)


def sample_map(g, frame, tri, name, default_path, x, y, sampler_type):
    """One texture parameter, sampled through whichever projection is switched on.

    Both branches read the same UMaterialExpressionTextureObjectParameter, so a role has ONE texture
    parameter per map however it is projected - assigning a map does not mean assigning it twice.

    The projection switch is a STATIC switch, so the branch that is off is compiled out rather than
    merely unused. UV mode costs one sampler; world-aligned costs three, and only when asked for.
    """
    tex = g.texture(name, default_path, x, y, GROUP_MAPS)

    def sampler(coord, xx, yy):
        n = g.node(unreal.MaterialExpressionTextureSample, xx, yy)
        n.set_editor_property("sampler_type", sampler_type)
        g.wire(tex, "", n, "Tex")
        g.wire(coord, "", n, "UVs")
        return n

    uv_sample = sampler(frame["uv"], x + 300, y)

    bx = g.mul(sampler(tri["uvx"], x + 300, y + 160), tri["wx"], x + 560, y + 160, a_out="RGB")
    by = g.mul(sampler(tri["uvy"], x + 300, y + 320), tri["wy"], x + 560, y + 320, a_out="RGB")
    bz = g.mul(sampler(tri["uvz"], x + 300, y + 480), tri["wz"], x + 560, y + 480, a_out="RGB")

    tri_sum = g.add(g.add(bx, by, x + 740, y + 240), bz, x + 920, y + 320)

    return g.pick("WorldAlignedProjection", tri_sum, uv_sample, x + 1120, y + 240,
                  GROUP_TILING, 4, false_out="RGB")


def build_detail(g, frame, tri):
    """Macro variation, the tile grid, and the fine bump - and the parameters that drive them.

    MACRO VARIATION IS DRIVEN FROM WORLD POSITION, THE BUMP FROM UV, and the difference is not
    arbitrary. Macro drift is a scalar wash across a wall and must not break at a chart boundary, so
    it wants a coordinate that is continuous across the whole flat: world position is, and UV0 is
    not, because ApplyWorldScaleUVs cuts a chart at every hard edge. The bump is a NORMAL, so its
    gradient has to live in the tangent frame the shader lights it in - which is the UV frame, not
    the world frame. Driving the bump from world position would tilt every facet's tooth by that
    facet's own orientation.
    """
    macro_rough = g.scalar("MacroRoughnessAmount", 0.03, -2600, 700, GROUP_DETAIL, 0)
    macro_albedo = g.scalar("MacroAlbedoAmount", 0.01, -2600, 820, GROUP_DETAIL, 1)
    macro_mm = g.scalar("MacroVariationMM", 1500.0, -2600, 940, GROUP_DETAIL, 2)
    bump_strength = g.scalar("DetailBumpStrength", 0.0, -2600, 1060, GROUP_DETAIL, 3)
    bump_mm = g.scalar("DetailBumpMM", 2.0, -2600, 1180, GROUP_DETAIL, 4)
    tile_shade = g.scalar("TileShadeVariation", 0.0, -2600, 1300, GROUP_DETAIL, 5)
    grout_mm = g.scalar("GroutWidthMM", 0.0, -2600, 1420, GROUP_GROUT, 0)

    # ---- macro variation, in world space --------------------------------------------------------
    macro_cm = g.div(macro_mm, frame["mm_per_cm"], -2400, 940)
    macro_pos = g.div(frame["world_pos"], macro_cm, -2200, 940)

    # Footprint in macro-noise space: UV footprint is in UV units, one of which is UVWorldSizeCm.
    macro_width = g.div(g.mul(frame["raw_width"], frame["uv_world_cm"], -2400, 1060),
                        macro_cm, -2200, 1060)
    macro_noise = g.noise(macro_pos, macro_width, -2000, 960, levels=3)

    # ---- the fine bump, in UV space -------------------------------------------------------------
    #
    # Independent of TilingMM on purpose: a plaster tooth is a property of the plaster, not of the
    # pattern printed on it, so changing the tile size must not change how coarse the surface is.
    bump_repeats = g.div(g.mul(frame["uv_world_cm"], frame["mm_per_cm"], -2400, 1180),
                         bump_mm, -2200, 1180)
    bump_uv = g.mul(frame["raw_uv"], bump_repeats, -2000, 1180)
    bump_width = g.mul(frame["raw_width"], bump_repeats, -2000, 1300)

    zero = g.const(0.0, -2000, 1420)
    # The same step the HLSL divides by, so the finite difference really is a gradient. Both come
    # from HF_BUMP_STEP; there is no second place to change one and forget the other.
    step = g.const3(HF_BUMP_STEP, 0.0, 0.0, -2000, 1500)
    step_y = g.const3(0.0, HF_BUMP_STEP, 0.0, -2000, 1580)

    bump_pos = g.append(bump_uv, zero, -1800, 1180)
    bump_pos_x = g.add(bump_pos, step, -1600, 1300)
    bump_pos_y = g.add(bump_pos, step_y, -1600, 1420)

    noise_c = g.noise(bump_pos, bump_width, -1400, 1180, levels=2)
    noise_x = g.noise(bump_pos_x, bump_width, -1400, 1320, levels=2)
    noise_y = g.noise(bump_pos_y, bump_width, -1400, 1460, levels=2)

    # ---- the tile pattern -----------------------------------------------------------------------
    #
    # The joint as a fraction of one tile, which is what the HLSL wants: both are lengths in
    # millimetres, so the ratio needs no unit conversion at all and cannot pick up a factor of ten.
    grout_frac = g.div(grout_mm, frame["tiling_mm"], -2400, 1420)

    # In world-aligned mode the pattern coordinate is the triplanar one, so the footprint the grid is
    # antialiased against has to be the triplanar footprint too, or the grout aliases in exactly the
    # mode that was chosen to avoid stretching.
    pattern_width = g.pick("WorldAlignedProjection", tri["width"], frame["uv_width"],
                           -1200, 1600, GROUP_TILING, 4)

    def cin(n):
        i = unreal.CustomInput()
        i.set_editor_property("input_name", n)
        return i

    def cout(n, t):
        o = unreal.CustomOutput()
        o.set_editor_property("output_name", n)
        o.set_editor_property("output_type", t)
        return o

    tile = g.node(unreal.MaterialExpressionCustom, -1000, 1200)
    tile.set_editor_property("code", HF_TILE_HLSL)
    tile.set_editor_property("description", "HFTilePattern")
    tile.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT1)
    tile.set_editor_property("inputs", [cin("UV"), cin("UVWidth"), cin("GroutFrac")])
    tile.set_editor_property("additional_outputs",
                             [cout("TileShade", unreal.CustomMaterialOutputType.CMOT_FLOAT1)])

    g.wire(frame["uv"], "", tile, "UV")
    g.wire(pattern_width, "", tile, "UVWidth")
    g.wire(grout_frac, "", tile, "GroutFrac")

    # A SEPARATE NODE, so UseProceduralBump can compile the three Noise evaluations away entirely
    # rather than merely ignoring their result. See HF_BUMP_HLSL.
    bump = g.node(unreal.MaterialExpressionCustom, -1000, 1500)
    bump.set_editor_property("code", HF_BUMP_HLSL)
    bump.set_editor_property("description", "HFDetailNormal")

    step_define = unreal.CustomDefine()
    step_define.set_editor_property("define_name", "HF_BUMP_STEP")
    step_define.set_editor_property("define_value", repr(HF_BUMP_STEP))
    bump.set_editor_property("additional_defines", [step_define])

    bump.set_editor_property("output_type", unreal.CustomMaterialOutputType.CMOT_FLOAT3)
    bump.set_editor_property("inputs",
                             [cin("NoiseC"), cin("NoiseX"), cin("NoiseY"), cin("BumpStrength")])

    g.wire(noise_c, "", bump, "NoiseC")
    g.wire(noise_x, "", bump, "NoiseX")
    g.wire(noise_y, "", bump, "NoiseY")
    g.wire(bump_strength, "", bump, "BumpStrength")

    # ---- the two scalar deltas ------------------------------------------------------------------
    shade = g.mul(tile, tile_shade, -700, 1500, a_out="TileShade")
    rough_delta = g.add(g.mul(macro_noise, macro_rough, -700, 900), shade, -500, 1000)
    albedo_scale = g.add(g.const(1.0, -700, 1080),
                         g.add(g.mul(macro_noise, macro_albedo, -700, 1180), shade, -500, 1200),
                         -300, 1120)

    return dict(grout_mask=tile, detail_normal=bump,
                rough_delta=rough_delta, albedo_scale=albedo_scale, grout_mm=grout_mm)


# =================================================================================================
#
# The masters.
#
# =================================================================================================

def build_common_surface(g):
    """Everything the opaque and glazed masters share, returned as pins for MakeMaterialAttributes.

    OUTPUT GOES THROUGH MakeMaterialAttributes RATHER THAN THE INDIVIDUAL PROPERTY PINS. Not a style
    choice: EMaterialProperty's MP_CustomData0 and MP_CustomData1 - which are where clear coat weight
    and clear coat roughness live - are not exposed to Python, so connect_material_property cannot
    reach them at all. MakeMaterialAttributes names every pin including ClearCoat, and this script is
    the only way these assets are authored, so the route that can express the whole material wins.
    """
    frame = build_uv_frame(g)
    tri = build_world_aligned(g, frame)
    detail = build_detail(g, frame, tri)

    base_colour = g.vector("BaseColor", unreal.LinearColor(0.5, 0.5, 0.5, 1.0), -2600, 0,
                           GROUP_SURFACE, 0)
    roughness = g.scalar("Roughness", 0.5, -2600, 120, GROUP_SURFACE, 1)
    metallic = g.scalar("Metallic", 0.0, -2600, 240, GROUP_SURFACE, 2)
    specular = g.scalar("Specular", 0.5, -2600, 360, GROUP_SURFACE, 3)

    grout_colour = g.vector("GroutColor", unreal.LinearColor(0.45, 0.42, 0.39, 1.0), -2600, 1540,
                            GROUP_GROUT, 1)
    grout_rough = g.scalar("GroutRoughness", 0.7, -2600, 1660, GROUP_GROUT, 2)

    # ---- albedo ---------------------------------------------------------------------------------
    albedo_map = sample_map(g, frame, tri, "AlbedoMap", WHITE_TEXTURE, 200, -1600,
                            unreal.MaterialSamplerType.SAMPLERTYPE_COLOR)
    tinted = g.mul(albedo_map, base_colour, 1600, -1600)
    albedo = g.pick("UseAlbedoMap", tinted, base_colour, 1800, -1500, GROUP_MAPS, 0)

    varied = g.mul(albedo, detail["albedo_scale"], 2000, -1500)

    # Grout is lerped over the FINISHED albedo rather than mixed into the base colour, so a user who
    # assigns a real tile albedo still gets their joint drawn on top of it at the right width.
    final_colour = g.lerp(varied, grout_colour, detail["grout_mask"], 2200, -1500,
                          alpha_out="")

    # ---- roughness ------------------------------------------------------------------------------
    rough_map = sample_map(g, frame, tri, "RoughnessMap", WHITE_TEXTURE, 200, -800,
                           unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    rough_mapped = g.mul(rough_map, roughness, 1600, -800)
    rough_base = g.pick("UseRoughnessMap", rough_mapped, roughness, 1800, -700, GROUP_MAPS, 1)

    rough_varied = g.add(rough_base, detail["rough_delta"], 2000, -700)
    rough_grouted = g.lerp(rough_varied, grout_rough, detail["grout_mask"], 2200, -700,
                           alpha_out="")
    final_rough = g.clamp(rough_grouted, 2400, -700, low=0.02, high=1.0)

    # ---- metallic -------------------------------------------------------------------------------
    metal_map = sample_map(g, frame, tri, "MetallicMap", WHITE_TEXTURE, 200, 0,
                           unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    metal_mapped = g.mul(metal_map, metallic, 1600, 0)
    final_metal = g.pick("UseMetallicMap", metal_mapped, metallic, 1800, 100, GROUP_MAPS, 2)

    # ---- ambient occlusion ----------------------------------------------------------------------
    ao_map = sample_map(g, frame, tri, "AOMap", WHITE_TEXTURE, 200, 800,
                        unreal.MaterialSamplerType.SAMPLERTYPE_LINEAR_GRAYSCALE)
    final_ao = g.pick("UseAOMap", ao_map, g.const(1.0, 1600, 920), 1800, 900, GROUP_MAPS, 3)

    # ---- normal ---------------------------------------------------------------------------------
    #
    # The map and the procedural bump are ALTERNATIVES, not a blend. Correctly combining two normals
    # needs a reorientation, and a user who supplied a real normal map does not want filtered noise
    # added underneath it - their map already carries the surface's own tooth.
    normal_map = sample_map(g, frame, tri, "NormalMap", FLAT_NORMAL_TEXTURE, 200, 1600,
                            unreal.MaterialSamplerType.SAMPLERTYPE_NORMAL)
    normal_strength = g.scalar("NormalMapStrength", 1.0, -2600, 1780, GROUP_MAPS, 4)
    flat_normal = g.const3(0.0, 0.0, 1.0, 1600, 1720)
    scaled_normal = g.lerp(flat_normal, normal_map, normal_strength, 1800, 1660)

    # Compiled out for roles that want no bump at all - see NO_BUMP - rather than left multiplying
    # three noise evaluations by a zero.
    procedural_normal = g.pick("UseProceduralBump", detail["detail_normal"], flat_normal,
                               2000, 1820, GROUP_DETAIL, 6, default=True)
    final_normal = g.pick("UseNormalMap", scaled_normal, procedural_normal, 2200, 1720,
                          GROUP_MAPS, 5)

    # ---- emissive, as colour times strength, defaulting to a strength of zero --------------------
    #
    # Two parameters rather than one so an instance can set how bright a fitting is without also
    # restating its colour. This is the hook the cove strip and the downlight lens hang on.
    emissive_colour = g.vector("EmissiveColor", unreal.LinearColor(1, 1, 1, 1), -2600, 1900,
                               GROUP_EMISSIVE, 0)
    emissive_strength = g.scalar("EmissiveStrength", 0.0, -2600, 2020, GROUP_EMISSIVE, 1)
    emissive = g.mul(emissive_colour, emissive_strength, 2200, 1960)

    return dict(frame=frame, base_colour=base_colour, colour=final_colour, rough=final_rough,
                metal=final_metal, spec=specular, ao=final_ao, normal=final_normal,
                emissive=emissive)


def attach(g, pins, extra, colour=None):
    """Wires every pin into a MakeMaterialAttributes and hands it to the material.

    @param colour  Replaces the shared graph's albedo. Glass uses it: for a transmissive surface the
                   base colour IS the transmitted colour, so it is the thing thickness acts on.
    """
    g.material.set_editor_property("use_material_attributes", True)

    mma = g.node(unreal.MaterialExpressionMakeMaterialAttributes, 2800, 0)
    for pin_name, (node, out) in extra.items():
        g.wire(node, out, mma, pin_name)

    g.wire(colour if colour is not None else pins["colour"], "", mma, "BaseColor")
    g.wire(pins["rough"], "", mma, "Roughness")
    g.wire(pins["metal"], "", mma, "Metallic")
    g.wire(pins["spec"], "", mma, "Specular")
    g.wire(pins["ao"], "", mma, "AmbientOcclusion")
    g.wire(pins["normal"], "", mma, "Normal")
    g.wire(pins["emissive"], "", mma, "EmissiveColor")

    g.to_property(mma, "", unreal.MaterialProperty.MP_MATERIAL_ATTRIBUTES)


def finish(material, name):
    errors = MEL.recompile_material(material)
    if errors:
        raise RuntimeError("{} failed to compile:\n  {}".format(name, "\n  ".join(errors)))
    unreal.EditorAssetLibrary.save_loaded_asset(material)
    return material


def build_opaque_master(name):
    """The finish every non-glazed role instances.

    CLEAR COAT, WITH A WEIGHT THAT DEFAULTS TO ZERO. A glazed vitrified tile, a high-gloss acrylic
    shutter and a vitreous-china WC are all a pigmented, rough-ish body under a thin smooth clear
    layer. That is two lobes, not one low roughness number, and with a single lobe they all read as
    painted metal. Under Substrate this becomes a coat slab over a base slab, which is physically
    what the object is; with Substrate off it is the engine's clear coat model, which is the same
    idea more cheaply. Either way nothing here is conditional on a setting this plugin does not own.

    One master rather than a separate coated one: only six of the eighteen roles want a coat, but
    splitting them would mean two parents to keep in step for the sake of a lobe that contributes
    nothing at weight zero, in an interior with no gameplay budget.
    """
    material = replace_asset(name, unreal.Material, unreal.MaterialFactoryNew())
    material.set_editor_property("shading_model", unreal.MaterialShadingModel.MSM_CLEAR_COAT)
    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property("two_sided", False)

    g = Graph(material)
    pins = build_common_surface(g)

    coat = g.scalar("CoatWeight", 0.0, -2600, 2140, GROUP_SURFACE, 4)
    coat_rough = g.scalar("CoatRoughness", 0.06, -2600, 2260, GROUP_SURFACE, 5)

    attach(g, pins, {"ClearCoat": (coat, ""), "ClearCoatRoughness": (coat_rough, "")})
    return finish(material, name)


def build_glazed_master(name):
    """Glass, and the two numbers that make it read as glass rather than as a plastic film.

    THICKNESS AND REFRACTION, not opacity alone. The old master was a flat translucent tint: it
    passed every check there was - blend mode translucent, opacity well under one - and still read as
    a boarded hole with a film over it, because nothing about it bent light or absorbed any.

    - REFRACTION. Index of refraction 1.52 is soda-lime float glass. With RefractionMethod set to
      index of refraction the pane displaces what is behind it, which is most of what says "there is
      a solid object here" for a surface with almost no albedo of its own.

    - THICKNESS, AS ABSORPTION. Real float glass is green edge-on because iron in the melt absorbs
      across the path length, and that is a Beer-Lambert term: the transmitted colour raised to the
      power of the path length in reference thicknesses. GlassThicknessMM 6 is standard window float,
      4 a ventilator, 12 a shower screen - and the tint deepens as it should instead of being a
      number that only ever shifts the whole pane uniformly.

    The geometry already has real thickness - .claude/rules/04-conventions.md requires it, and the
    sliding sets and FHFWallPlateKit build it - so this is the optical half of the same promise.

    MIRRORS ARE NOT INSTANCES OF THIS. They were, while Glass was an opaque-ish blue-grey film and
    the difference did not show. A mirror is a front-surface reflector and belongs on the opaque
    master; see EHFSurfaceRole::Mirror.
    """
    material = replace_asset(name, unreal.Material, unreal.MaterialFactoryNew())

    material.set_editor_property("blend_mode", unreal.BlendMode.BLEND_TRANSLUCENT)
    # Per-pixel surface lighting, not the default volumetric mode: glass with no specular response is
    # a grey film, and the highlight is most of what says "pane" rather than "gap".
    material.set_editor_property("translucency_lighting_mode",
                                 unreal.TranslucencyLightingMode.TLM_SURFACE_PER_PIXEL_LIGHTING)

    # SINGLE SIDED, AND THAT IS A CONSEQUENCE OF THE GEOMETRY BEING RIGHT. Two-sided is what a glass
    # material wants when the pane is a plane and there is no back face to draw. HouseForge's panes
    # are closed solids with real thickness - .claude/rules/04-conventions.md requires it - so the
    # back face exists, and drawing it means refracting the same background a second time. Rendered,
    # that came back as a smeared turquoise marbling across every window in the flat rather than as
    # glass. Backface culling on a closed solid discards nothing that should have been visible.
    material.set_editor_property("two_sided", False)
    material.set_editor_property("refraction_method", unreal.RefractionMode.RM_INDEX_OF_REFRACTION)

    g = Graph(material)
    pins = build_common_surface(g)

    opacity = g.scalar("Opacity", 0.06, -2600, 2140, GROUP_GLASS, 0)
    ior = g.scalar("IndexOfRefraction", 1.52, -2600, 2260, GROUP_GLASS, 1)
    thickness_mm = g.scalar("GlassThicknessMM", 6.0, -2600, 2380, GROUP_GLASS, 2)
    reference_mm = g.const(6.0, -2400, 2500)

    # Beer-Lambert as a power: the tint at the reference thickness raised to the ratio of actual to
    # reference path length. Doubling the pane doubles the number of absorption lengths, which is
    # what squaring the transmitted colour does.
    ratio = g.div(thickness_mm, reference_mm, -2200, 2440)
    absorbed = g.power(pins["colour"], ratio, -2000, 2380)

    attach(g, pins, {"Opacity": (opacity, ""), "Refraction": (ior, "")}, colour=absorbed)
    return finish(material, name)


# =================================================================================================
#
# The instances.
#
# =================================================================================================

def build_instance(spec, opaque, glazed):
    name = "MI_HF_{}".format(spec["name"])
    instance = replace_asset(name, unreal.MaterialInstanceConstant,
                             unreal.MaterialInstanceConstantFactoryNew())

    parent = glazed if spec["opacity"] is not None else opaque
    MEL.set_material_instance_parent(instance, parent)

    def vec(param, colour):
        MEL.set_material_instance_vector_parameter_value(instance, param, colour)

    def num(param, value):
        MEL.set_material_instance_scalar_parameter_value(instance, param, float(value))

    vec("BaseColor", linear_colour(spec["colour"]))
    num("Roughness", spec["rough"])
    num("Metallic", spec["metal"])
    num("Specular", spec["spec"])

    num("TilingMM", spec["tiling_mm"])
    num("MacroRoughnessAmount", spec["macro_rough"])
    num("MacroAlbedoAmount", spec["macro_albedo"])
    num("MacroVariationMM", spec["macro_mm"])
    num("DetailBumpStrength", spec["bump"])
    num("DetailBumpMM", spec["bump_mm"])
    num("TileShadeVariation", spec["tile_shade"])

    num("GroutWidthMM", spec["grout_mm"])
    if spec["grout_colour"] is not None:
        vec("GroutColor", linear_colour(spec["grout_colour"]))
    num("GroutRoughness", spec["grout_rough"])

    if spec["opacity"] is not None:
        num("Opacity", spec["opacity"])
    else:
        num("CoatWeight", spec["coat"])
        num("CoatRoughness", spec["coat_rough"])

    if spec["emissive"] > 0.0:
        vec("EmissiveColor", linear_colour(spec["colour"]))
        num("EmissiveStrength", spec["emissive"])

    if spec["name"] in NO_BUMP:
        MEL.set_material_instance_static_switch_parameter_value(
            instance, "UseProceduralBump", False)

    MEL.update_material_instance(instance)
    unreal.EditorAssetLibrary.save_loaded_asset(instance)
    return instance


def main():
    # THE REGISTRY HAS TO HAVE SEEN THIS FOLDER FIRST.
    #
    # Run as a commandlet the asset registry never scans the plugin's own content, so load_asset
    # fails with "could not be found in the Asset Registry" for a .uasset sitting right there on
    # disk, and create_asset then refuses with "already exists in package". Neither message reaches
    # the caller: what surfaces is a None three calls away inside set_editor_property, which is a
    # very long way from "nobody scanned the folder".
    unreal.AssetRegistryHelpers.get_asset_registry().scan_paths_synchronous(
        [FOLDER], force_rescan=True)

    if not unreal.EditorAssetLibrary.does_directory_exist(FOLDER):
        unreal.EditorAssetLibrary.make_directory(FOLDER)

    opaque = build_opaque_master(OPAQUE_PARENT)
    unreal.log("HouseForge: authored {}".format(OPAQUE_PARENT))

    glazed = build_glazed_master(GLAZED_PARENT)
    unreal.log("HouseForge: authored {}".format(GLAZED_PARENT))

    for spec in ROLES:
        build_instance(spec, opaque, glazed)
        unreal.log("HouseForge: authored MI_HF_{}".format(spec["name"]))

    unreal.log("HouseForge: {} role materials written to {}".format(len(ROLES), FOLDER))


main()
