"""Download the official roster portraits and the user-supplied NetEase radio.

Run with Python; no account cookies or private endpoints are used.
Existing valid source files are reused. The manifest records provenance/hashes.
"""
import concurrent.futures
import hashlib
import html
import json
from pathlib import Path
import re
import urllib.request
import sys
from PIL import Image
from prepare_ggst_portraits import prepare_portraits

sys.stdout.reconfigure(encoding='utf-8')

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'SourceAssets' / 'GGST'
HOST = 'https://www.guiltygear.com'
INDEX = HOST + '/ggst/en/character/'
RADIO = 'https://music.163.com/api/dj/program/byradio?radioId=962855155&limit=1000&offset=0'
# Costume/hair colors, adjusted in brightness/hue to distinguish similar costumes.
# All values are sRGB and always have alpha 255.
CHARACTERS = [
 ('sol', 'Sol Badguy', '#D62C32', 'Find Your One Way', '红色外套'),
 ('kyk', 'Ky Kiske', '#4D8FFF', 'The Roar of the Spark', '蓝白制服'),
 ('may', 'May', '#FF920D', 'The Disaster of Passion', '橙色卫衣'),
 ('axl', 'Axl Low', '#EBC545', 'Out of the Box', '金发与黄色配饰'),
 ('chp', 'Chipp Zanuff', '#B2C9D6', 'Play the Hero', '银白发与灰色护具'),
 ('pot', 'Potemkin', '#596E36', 'Armor-clad Faith', '军绿色装甲'),
 ('fau', 'Faust', '#598FBA', 'Alone Infection', '蓝色医疗服'),
 ('mll', 'Millia Rage', '#F6DF93', 'Love the Subhuman Self', '浅金色长发'),
 ('zat', 'Zato-1', '#38303F', 'Let Me Carve Your Way', '黑色服装与影子'),
 ('ram', 'Ramlethal Valentine', '#D6D9C4', 'Necessary Discrepancy', '象牙白外套'),
 ('leo', 'Leo Whitefang', '#B78332', 'Hellfire', '棕金色鬃发'),
 ('nag', 'Nagoriyuki', '#713D66', 'What do you fight for', '暗紫色武士装'),
 ('gio', 'Giovanna', '#22AD86', 'Trigger', '绿色狼灵 Rei'),
 ('anji', 'Anji Mito', '#2584D0', 'Rock Parade', '蓝色扇子与服饰'),
 ('ino', 'I-No', '#A51247', 'Requiem', '深红色帽子'),
 ('gld', 'Goldlewis Dickinson', '#8D7054', 'The Kiss of Death', '棕色制服'),
 ('jko', "Jack-O'", '#F2674D', "Perfection Can't Please Me", '橙红色头发'),
 ('cos', 'Happy Chaos', '#66CEDB', 'Drift', '青蓝色皮肤'),
 ('bkn', 'Baiken', '#F1A4AB', 'Mirror of the World', '粉色长发'),
 ('tst', 'Testament', '#7444A6', 'Like a Weed, Naturally, as a Matter of Course', '紫色衬里与黑色礼服'),
 ('bgt', 'Bridget', '#73BAFF', 'The Town Inside Me', '浅蓝色连帽服'),
 ('sin', 'Sin Kiske', '#F7F2DB', 'The Hourglass', '米白色服装与旗帜'),
 ('bed', 'Bedman?', '#A27B9C', 'The Circle', '灰紫色机械床'),
 ('ask', 'Asuka R\u266f', '#AE4CDA', 'The Gravity', '紫色法术与白色长袍'),
 ('jhn', 'Johnny', '#C9A35C', 'Just Lean', '黑色外套的金色扣饰'),
 ('elp', 'Elphelt Valentine', '#F258A4', 'Extras', '粉色舞台服'),
 ('aba', 'A.B.A', '#6AC69B', 'Symphony', '薄荷绿头发'),
 ('sly', 'Slayer', '#742D36', 'Ups and Downs', '酒红色西装'),
 ('dzy', 'Queen Dizzy', '#344D9C', 'Radiant Dawn', '深蓝色头发'),
 ('ven', 'Venom', '#ABD5CE', 'A Tenth of Myself', '浅青色长发'),
 ('uni', 'Unika', '#CE6733', 'Carpe Diem', '铜橙色头发与武器'),
 ('luc', 'Lucy', '#C9A3EE', 'I Really Want to Stay at Your House', '淡紫色渐变头发'),
 ('jam', 'Jam Kuradoberi', '#F04463', 'Trying hard is hard, so I try hard to try hard.', '玫红色旗袍'),
 ('rbk', 'Robo-Ky', '#9ABA27', 'Breath', '黄绿色机械与护目镜'),
]

def fetch(url):
    req = urllib.request.Request(url, headers={'User-Agent': 'Mozilla/5.0', 'Referer': 'https://music.163.com/' if '163.com' in url else INDEX})
    with urllib.request.urlopen(req, timeout=90) as response:
        return response.read()

def normalized(text):
    return re.sub(r'[^a-z0-9]', '', text.lower())

def main():
    SOURCE.mkdir(parents=True, exist_ok=True)
    index = fetch(INDEX).decode()
    programs = json.loads(fetch(RADIO))['programs']
    # Lucy's crossover song is absent from the supplied GGST radio.
    lucy_radio = 'https://music.163.com/api/dj/program/byradio?radioId=1492489996&limit=100&offset=0'
    programs.extend(json.loads(fetch(lucy_radio))['programs'])
    songs = {normalized(p['name']): p for p in programs}
    manifest_path = ROOT / 'Config/GGST/roster.json'
    old_entries = {e['id']: e for e in json.loads(manifest_path.read_text(encoding='utf-8'))['characters']} if manifest_path.exists() else {}
    cards = re.findall(r'<a\s+[^>]*href="(https://www.guiltygear.com/ggst/en/character/[a-z]+/)"[^>]*>(.*?)</a>', index, re.S)
    portrait_urls = {url: HOST + re.search(r'<img\s+[^>]*src="([^"]+)"', body).group(1) for url, body in cards if '<img' in body}
    official_ids = {url.rstrip('/').split('/')[-1] for url in portrait_urls}
    assert official_ids == {c[0] for c in CHARACTERS}, f'Official roster changed: {official_ids}'

    def download(character):
        cid, name, color, theme, color_reason = character
        page = INDEX + cid + '/'
        detail = fetch(page).decode()
        official_theme = re.search(r'<h3>BGM</h3>\s*<p>(.*?)</p>', detail, re.S)
        if cid == 'luc':
            # The guest character page has no BGM section. These patch notes
            # explicitly list the Cyberpunk crossover tracks added with Lucy.
            theme_reference = 'https://www.guiltygear.com/ggst/kr/news/post-2536/'
            assert theme in fetch(theme_reference).decode()
            official_theme = theme
        else:
            assert official_theme, f'Missing official theme: {cid}'
            official_theme = html.unescape(re.sub('<[^>]+>', '', official_theme.group(1))).strip()
            theme_reference = page
        assert normalized(official_theme) == normalized(theme), f'Theme mismatch: {cid}: {official_theme} vs {theme}'
        portrait_url = portrait_urls[page]
        extension = Path(portrait_url).suffix
        portrait = SOURCE / 'Portraits' / (cid + extension)
        portrait.parent.mkdir(exist_ok=True)
        if not portrait.exists():
            portrait.write_bytes(fetch(portrait_url))
        with Image.open(portrait) as img:
            img.verify()
        program = songs[normalized(theme)]
        song_id = program['mainSong']['id']
        audio_url = f'https://music.163.com/song/media/outer/url?id={song_id}.mp3'
        audio = SOURCE / 'Music' / (cid + '.mp3')
        audio.parent.mkdir(exist_ok=True)
        old_url = old_entries.get(cid, {}).get('music_download_url')
        if not audio.exists() or old_url != audio_url:
            data = fetch(audio_url)
            assert len(data) > 1000000 and (data.startswith(b'ID3') or data[0] == 0xff), f'Invalid full audio: {cid}'
            audio.write_bytes(data)
        print(f'{cid}: {name} / {theme} / {program["mainSong"]["duration"]/1000:.1f}s', flush=True)
        return dict(id=cid, name=name, color=color, color_reason=color_reason, theme_title=theme,
                    portrait_asset=f'/Game/GGST/Portraits/T_{cid}.T_{cid}',
                    music_asset=f'/Game/GGST/Music/BGM_{cid}.BGM_{cid}',
                    character_url=page, portrait_url=portrait_url, official_theme_title=official_theme,
                    theme_reference=theme_reference,
                    portrait_source=str(portrait.relative_to(ROOT)).replace('\\','/'),
                    audio_source=str(audio.relative_to(ROOT)).replace('\\','/'),
                    music_page=f'https://music.163.com/dj?id={program["id"]}',
                    music_download_url=audio_url, duration_seconds=program['mainSong']['duration']/1000,
                    portrait_sha256=hashlib.sha256(portrait.read_bytes()).hexdigest(),
                    audio_sha256=hashlib.sha256(audio.read_bytes()).hexdigest())

    with concurrent.futures.ThreadPoolExecutor(max_workers=4) as pool:
        entries = list(pool.map(download, CHARACTERS))
    prepare_portraits(entries, fetch=fetch)
    manifest = dict(version=1, checked_date='2026-10-02', roster_source=INDEX,
                    reference_source='https://www.dustloop.com/w/GGST',
                    music_source='https://music.163.com/djradio?id=962855155', characters=entries)
    destination = ROOT / 'Config/GGST/roster.json'
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_text(json.dumps(manifest, ensure_ascii=False, indent=2)+'\n', encoding='utf-8')
    print(f'Complete: {len(entries)} characters, {destination}', flush=True)

if __name__ == '__main__':
    main()
