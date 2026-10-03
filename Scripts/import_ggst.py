"""Run inside Unreal: py \"<project>/Scripts/import_ggst.py\".

Imports the downloaded originals, builds the opaque portrait material, and
verifies all 34 native assets. Safe to rerun without overwriting existing assets.
"""
import json
import sys
from pathlib import Path
import unreal

ROOT = Path(unreal.Paths.project_dir())
MANIFEST = json.loads((ROOT / 'Config/GGST/roster.json').read_text(encoding='utf-8'))
ASSETS = unreal.AssetToolsHelpers.get_asset_tools()
LIB = unreal.EditorAssetLibrary
MAT = unreal.MaterialEditingLibrary
PORTRAITS_ONLY = '--portraits-only' in sys.argv
CHARACTER_IDS = next((set(arg.split('=', 1)[1].split(',')) for arg in sys.argv if arg.startswith('--characters=')), None)

def import_asset(entry, source_key, asset_key):
    asset_path = entry[asset_key].split('.')[0]
    existing = unreal.load_asset(asset_path)
    needs_reimport = existing and source_key == 'audio_source' and abs(existing.get_editor_property('duration') - entry['duration_seconds']) > 1
    if existing and source_key == 'portrait_source':
        import_data = existing.get_editor_property('asset_import_data')
        needs_reimport = Path(import_data.get_first_filename()).resolve() != (ROOT / entry[source_key]).resolve()
        if 'portrait_crop' in entry:
            needs_reimport = needs_reimport or LIB.get_metadata_tag(existing, 'GGSTPortraitSHA256') != entry['portrait_sha256']
    if existing and not needs_reimport:
        return existing
    task = unreal.AssetImportTask()
    task.filename = str(ROOT / entry[source_key])
    task.destination_path, task.destination_name = asset_path.rsplit('/', 1)
    task.automated = True
    task.replace_existing = bool(needs_reimport)
    task.save = True
    ASSETS.import_asset_tasks([task])
    result = task.get_objects()
    assert result, 'Import failed: '+task.filename
    return result[0]

report = []
for entry in MANIFEST['characters']:
    if CHARACTER_IDS is not None and entry['id'] not in CHARACTER_IDS:
        continue
    portrait = import_asset(entry, 'portrait_source', 'portrait_asset')
    portrait.set_editor_property('srgb', True)
    portrait.set_editor_property('lod_group', unreal.TextureGroup.TEXTUREGROUP_UI)
    portrait.set_editor_property('address_x', unreal.TextureAddress.TA_CLAMP)
    portrait.set_editor_property('address_y', unreal.TextureAddress.TA_CLAMP)
    LIB.set_metadata_tag(portrait, 'GGSTPortraitSHA256', entry['portrait_sha256'])
    LIB.save_loaded_asset(portrait)
    if PORTRAITS_ONLY:
        report.append(dict(id=entry['id'], portrait=portrait.get_path_name()))
        continue
    music = import_asset(entry, 'audio_source', 'music_asset')
    duration = music.get_editor_property('duration')
    assert abs(duration-entry['duration_seconds']) < 2, f'Duration mismatch: {entry["id"]} {duration}'
    music.set_editor_property('looping', True)
    music.set_editor_property('sound_group', unreal.SoundGroup.SOUNDGROUP_MUSIC)
    LIB.save_loaded_asset(music)
    report.append(dict(id=entry['id'], duration=duration, portrait=portrait.get_path_name(), music=music.get_path_name()))
    unreal.log('GGST_IMPORTED: '+entry['id'])

path = '/Game/GGST/Materials/M_GGSTMarble'
material = unreal.load_asset(path)
needs_material_build = material is None or '--rebuild-material' in sys.argv
if not material:
    material = ASSETS.create_asset('M_GGSTMarble', '/Game/GGST/Materials', unreal.Material, unreal.MaterialFactoryNew())

def build_material(material):
    MAT.delete_all_material_expressions(material)
    material.set_editor_property('blend_mode', unreal.BlendMode.BLEND_OPAQUE)
    material.set_editor_property('shading_model', unreal.MaterialShadingModel.MSM_UNLIT)
    material.set_editor_property('two_sided', True)
    def node(cls, x, y, **properties):
        result = MAT.create_material_expression(material, cls, x, y)
        for key, value in properties.items():
            result.set_editor_property(key, value)
        return result
    def connect(a, output, b, pin):
        assert MAT.connect_material_expressions(a, output, b, pin), f'Connect failed: {pin}'
    world = node(unreal.MaterialExpressionWorldPosition, -1200, 0)
    local = node(unreal.MaterialExpressionTransformPosition, -1000, 0,
                 transform_source_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_WORLD,
                 transform_type=unreal.MaterialPositionTransformSource.TRANSFORMPOSSOURCE_LOCAL)
    connect(world, '', local, 'None')
    # Original marble is a disc in local XY, radius 200cm, Z=0..10cm.
    # Planar UVs avoid the mesh's radial fan UVs stretching portraits.
    mask = node(unreal.MaterialExpressionComponentMask, -800, 0, r=True, g=True, b=False, a=False)
    connect(local, '', mask, 'None')
    scale = node(unreal.MaterialExpressionMultiply, -600, 0, const_b=1.0/400.0)
    connect(mask, '', scale, 'A')
    flip = node(unreal.MaterialExpressionConstant2Vector, -600, 160, r=1.0, g=-1.0)
    uv = node(unreal.MaterialExpressionMultiply, -400, 0)
    connect(scale, '', uv, 'A')
    connect(flip, '', uv, 'B')
    offset = node(unreal.MaterialExpressionAdd, -200, 0, const_b=0.5)
    connect(uv, '', offset, 'A')
    texture = node(unreal.MaterialExpressionTextureSampleParameter2D, 0, -160,
                   parameter_name='Portrait', texture=unreal.load_asset('/Engine/EngineResources/WhiteSquareTexture'))
    connect(offset, '', texture, 'UVs')
    color = node(unreal.MaterialExpressionVectorParameter, 0, 250,
                 parameter_name='颜色', default_value=unreal.LinearColor(0.9,0.1,0.1,1.0))
    enabled = node(unreal.MaterialExpressionScalarParameter, 0, 420,
                   parameter_name='HasPortrait', default_value=0.0)
    # 9% opaque color rim. Texture alpha reveals the color, never the track.
    def custom_input(name):
        result = unreal.CustomInput()
        result.set_editor_property('input_name', name)
        return result
    custom = node(unreal.MaterialExpressionCustom, 250, 100,
                  code='float inside = 1.0 - smoothstep(0.445, 0.455, length(UV - 0.5)); return inside * Alpha * Enabled;',
                  output_type=unreal.CustomMaterialOutputType.CMOT_FLOAT1,
                  inputs=[custom_input('UV'), custom_input('Alpha'), custom_input('Enabled')])
    connect(offset, '', custom, 'UV')
    connect(texture, 'A', custom, 'Alpha')
    connect(enabled, '', custom, 'Enabled')
    blend = node(unreal.MaterialExpressionLinearInterpolate, 500, 0)
    connect(color, '', blend, 'A')
    connect(texture, 'RGB', blend, 'B')
    connect(custom, '', blend, 'Alpha')
    assert MAT.connect_material_property(blend, '', unreal.MaterialProperty.MP_EMISSIVE_COLOR)
    MAT.recompile_material(material)
    assert LIB.save_loaded_asset(material)

if needs_material_build:
    build_material(material)
assert material.get_editor_property('blend_mode') == unreal.BlendMode.BLEND_OPAQUE
result = dict(passed=True, count=len(report), assets=report, opaque_material=material.get_path_name())
(ROOT/('Saved/ggst_portraits_result.json' if PORTRAITS_ONLY else 'Saved/ggst_import_result.json')).write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding='utf-8')
unreal.log('GGST_IMPORT_COMPLETE: '+str(len(report)))
