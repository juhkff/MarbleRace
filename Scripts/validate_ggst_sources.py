"""Offline validation of roster coverage, original source files and native assets."""
import hashlib
import json
from pathlib import Path
from PIL import Image

root = Path(__file__).resolve().parents[1]
manifest = json.loads((root/'Config/GGST/roster.json').read_text(encoding='utf-8'))
entries = manifest['characters']
assert len(entries) == 34
for key in ['id', 'name', 'color', 'music_download_url']:
    assert len({entry[key] for entry in entries}) == 34, f'Duplicate {key}'
for entry in entries:
    for source_key, hash_key in [('portrait_source','portrait_sha256'),('audio_source','audio_sha256')]:
        file = root/entry[source_key]
        assert hashlib.sha256(file.read_bytes()).hexdigest() == entry[hash_key], file
    if 'portrait_crop' in entry:
        crop = entry['portrait_crop']
        assert hashlib.sha256((root/crop['source']).read_bytes()).hexdigest() == crop['source_sha256']
        assert hashlib.sha256((root/entry['portrait_original_source']).read_bytes()).hexdigest() == entry['portrait_original_sha256']
    with Image.open(root/entry['portrait_source']) as image:
        assert abs(image.width-image.height) <= 1 and min(image.size) >= 256
        image.verify()
    assert entry['duration_seconds'] > 180
    for key in ['portrait_asset','music_asset']:
        path = entry[key].split('.')[0].replace('/Game/', 'Content/')+'.uasset'
        assert (root/path).is_file(), path
assert (root/'Content/GGST/Materials/M_GGSTMarble.uasset').is_file()
print('PASS: 34 distinct characters, colors, full themes, source hashes and native assets')
