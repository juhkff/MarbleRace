"""Reframe the original downloaded portraits; no runtime per-character UVs."""
import hashlib
import json
from pathlib import Path
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
RECIPES = ROOT / 'SourceAssets/GGST/portrait_crops.json'


def prepare_portraits(entries, fetch=None):
    recipes = json.loads(RECIPES.read_text(encoding='utf-8'))
    for entry in entries:
        recipe = recipes.get(entry['id'])
        if not recipe:
            continue
        source = ROOT / recipe['source']
        if not source.exists():
            if fetch is None:
                raise FileNotFoundError(source)
            source.parent.mkdir(parents=True, exist_ok=True)
            source.write_bytes(fetch(recipe['url']))
        target = ROOT / 'SourceAssets/GGST/Portraits/Cropped' / (entry['id'] + '.png')
        target.parent.mkdir(parents=True, exist_ok=True)
        with Image.open(source) as original:
            # Preserve the original pixels and black backdrop. A shifted window
            # beyond the original bounds continues the backdrop in solid black.
            portrait = original.convert('RGB').crop(tuple(recipe['box']))
            assert portrait.width == portrait.height and portrait.width >= 256
            portrait.save(target)
        if 'portrait_original_source' not in entry:
            entry['portrait_original_source'] = entry['portrait_source']
            entry['portrait_original_sha256'] = entry['portrait_sha256']
        entry['portrait_source'] = target.relative_to(ROOT).as_posix()
        entry['portrait_sha256'] = hashlib.sha256(target.read_bytes()).hexdigest()
        entry['portrait_crop'] = dict(recipe, source_sha256=hashlib.sha256(source.read_bytes()).hexdigest())


if __name__ == '__main__':
    path = ROOT / 'Config/GGST/roster.json'
    manifest = json.loads(path.read_text(encoding='utf-8'))
    prepare_portraits(manifest['characters'])
    path.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print('Prepared centered portraits from original downloaded portraits')
