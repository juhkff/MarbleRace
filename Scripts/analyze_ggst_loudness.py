"""Measure full tracks and generate shared, peak-safe playback gain calibration."""
import argparse
import concurrent.futures
import hashlib
import json
import math
import pathlib
import re
import subprocess

ROOT = pathlib.Path(__file__).resolve().parents[1]

def analyze(character):
    path = ROOT / character['audio_source']
    result = subprocess.run([
        str(ROOT / 'Tools/FFmpeg/ffmpeg.exe'), '-hide_banner', '-nostats',
        '-i', str(path), '-af', 'loudnorm=I=-18:TP=-1:LRA=11:print_format=json',
        '-f', 'null', '-'], capture_output=True, text=True, check=True)
    measures = json.loads(re.findall(r'\{[^{}]*"input_i"[^{}]*\}', result.stderr)[-1])
    return dict(id=character['id'], music_asset=character['music_asset'],
                integrated_lufs=float(measures['input_i']), true_peak_dbtp=float(measures['input_tp']),
                audio_sha256=hashlib.sha256(path.read_bytes()).hexdigest())

def main():
    roster = json.loads((ROOT / 'Config/GGST/roster.json').read_text(encoding='utf-8'))
    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as executor:
        tracks = list(executor.map(analyze, roster['characters']))
    # Choose a common target that never clips any track: adjust gain only,
    # preserving each song's dynamics and the original imported audio.
    target = min(-18.0, min(t['integrated_lufs'] - t['true_peak_dbtp'] - 1.0 for t in tracks))
    for track in tracks:
        track['gain_db'] = round(target - track['integrated_lufs'], 4)
        track['gain'] = round(math.pow(10.0, track['gain_db'] / 20.0), 8)
    output = dict(target_lufs=round(target, 4), true_peak_ceiling_dbtp=-1.0, tracks=tracks)
    (ROOT / 'Config/GGST/music_levels.json').write_text(json.dumps(output, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f"{len(tracks)} tracks; original range {min(t['integrated_lufs'] for t in tracks):.2f} to "
          f"{max(t['integrated_lufs'] for t in tracks):.2f} LUFS; common target {target:.2f} LUFS")

if __name__ == '__main__':
    main()
