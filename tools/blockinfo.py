#!/usr/bin/env python3
"""Generate render models and procedural texture recipes for Java 1.19.4 block states.

Block names, state ranges and collision shapes come from PrismarineJS minecraft-data
(MIT). Colors and texture patterns are original approximations; no Mojang assets
are used. Usage: python tools/blockinfo.py [blocks.json blockCollisionShapes.json]
"""
import hashlib, json, sys, urllib.request
from pathlib import Path

BASE = 'https://raw.githubusercontent.com/PrismarineJS/minecraft-data/master/data/pc/1.19.4/'
def read(i, name):
    return Path(sys.argv[i]).read_bytes() if len(sys.argv) > i else urllib.request.urlopen(BASE+name, timeout=30).read()
braw = read(1, 'blocks.json'); sraw = read(2, 'blockCollisionShapes.json')
blocks = json.loads(braw); shapes = json.loads(sraw)

DYES = {
    'white': 0xe9ecec, 'orange': 0xf07613, 'magenta': 0xbd44b3, 'light_blue': 0x3aafd9,
    'yellow': 0xf8c527, 'lime': 0x70b919, 'pink': 0xed8dac, 'gray': 0x3e4447,
    'light_gray': 0x8e8e86, 'cyan': 0x158991, 'purple': 0x792aac, 'blue': 0x35399d,
    'brown': 0x724728, 'green': 0x546d1b, 'red': 0xa12722, 'black': 0x141519,
}
TERRACOTTA = {
    'white': 0xd1b2a1, 'orange': 0xa15325, 'magenta': 0x95576c, 'light_blue': 0x706c8a,
    'yellow': 0xba8523, 'lime': 0x677535, 'pink': 0xa04d4e, 'gray': 0x392a23,
    'light_gray': 0x876b62, 'cyan': 0x575b5b, 'purple': 0x764656, 'blue': 0x4a3b5b,
    'brown': 0x4d3323, 'green': 0x4c532a, 'red': 0x8e3c2e, 'black': 0x251610,
}
# (planks, log bark, log inner/stripped, leaves)
WOODS = {
    'oak': (0xa2834f, 0x6d5532, 0xb29158, 0x4f8a2e), 'spruce': (0x735531, 0x3b2a17, 0x7a5a36, 0x3d6040),
    'birch': (0xc5b37b, 0xd8d7d2, 0xc4b07a, 0x6a9a44), 'jungle': (0xa07351, 0x56481e, 0xab8457, 0x3f8d22),
    'acacia': (0xa95b33, 0x686259, 0xae5d3b, 0x5b8a25), 'cherry': (0xe3b3ad, 0x3a2232, 0xd99da0, 0xe8a2c6),
    'dark_oak': (0x432b14, 0x3c2e1a, 0x4b331c, 0x3b6e1f), 'mangrove': (0x763631, 0x55452b, 0x7a3b33, 0x55902c),
    'bamboo': (0xc8b357, 0x8a9a33, 0xc3ad53, 0x6a9a44), 'crimson': (0x6a344b, 0x5c1d1d, 0x8a3a5a, 0x7b0000),
    'warped': (0x2b6963, 0x3a3a4d, 0x3a8e8c, 0x167e86),
}
STONES = {
    'stone': 0x7f7f7f, 'granite': 0x9a6b57, 'polished_granite': 0x9c6a57, 'diorite': 0xbdbcbd,
    'polished_diorite': 0xc0c0c2, 'andesite': 0x888889, 'polished_andesite': 0x848685,
    'cobblestone': 0x7a7a7a, 'mossy_cobblestone': 0x6e7a5e, 'bedrock': 0x555555, 'gravel': 0x837f7e,
    'sand': 0xdbcfa3, 'red_sand': 0xbe6621, 'sandstone': 0xd8cb9b, 'red_sandstone': 0xb5621f,
    'clay': 0xa0a6b3, 'obsidian': 0x15111e, 'crying_obsidian': 0x2a0a52, 'netherrack': 0x6f3534,
    'soul_sand': 0x513e32, 'soul_soil': 0x4c3a2f, 'basalt': 0x4b4b50, 'smooth_basalt': 0x48484e,
    'blackstone': 0x2a2329, 'end_stone': 0xdbdea0, 'tuff': 0x6c6d66, 'calcite': 0xdfe0dc,
    'deepslate': 0x4d4d51, 'cobbled_deepslate': 0x4a4a4f, 'dripstone_block': 0x86695a,
    'mud': 0x3c393d, 'packed_mud': 0x8e6b50, 'mud_bricks': 0x89684f, 'prismarine': 0x63a397,
    'dark_prismarine': 0x335b4b, 'purpur': 0xa97ea9, 'quartz': 0xece6df, 'nether_bricks': 0x2c1519,
    'red_nether_bricks': 0x450709, 'bricks': 0x966252, 'stone_bricks': 0x7a797a, 'magma_block': 0x8e3f1f,
    'amethyst_block': 0x8662bf, 'mossy_stone_bricks': 0x737d68, 'terracotta': 0x985e43, 'smooth_stone': 0x9e9e9e,
}
METALS = {
    'iron': 0xdcdcdc, 'gold': 0xf6d03d, 'diamond': 0x62ede4, 'emerald': 0x2acb57, 'lapis': 0x1f43a6,
    'redstone': 0xaf1a05, 'coal': 0x1e1e1e, 'copper': 0xc06a50, 'netherite': 0x423d3f,
    'raw_iron': 0xa6876b, 'raw_copper': 0x9a6a4f, 'raw_gold': 0xdda92e,
}
ORE_SPECK = {'coal': 0x2b2b2b, 'iron': 0xd8af93, 'gold': 0xfcee4b, 'diamond': 0x5decf5, 'emerald': 0x17dd62,
             'lapis': 0x1c4fbe, 'redstone': 0xff0000, 'copper': 0xe0835f, 'nether_quartz': 0xeae5de, 'nether_gold': 0xfcee4b}
FLOWERS = {
    'dandelion': 0xf5e11b, 'poppy': 0xd8231a, 'blue_orchid': 0x2aa8e0, 'allium': 0xb977e8,
    'azure_bluet': 0xe8eaf0, 'red_tulip': 0xd92a1a, 'orange_tulip': 0xf0761a, 'white_tulip': 0xeeeeee,
    'pink_tulip': 0xf0a3d0, 'oxeye_daisy': 0xf1f1e8, 'cornflower': 0x4766e0, 'wither_rose': 0x2a2a20,
    'lily_of_the_valley': 0xf5f5f5, 'torchflower': 0xe88a2a, 'sunflower': 0xf5d21b, 'lilac': 0xd08fd8,
    'rose_bush': 0xc8231a, 'peony': 0xe6a6d6, 'pink_petals': 0xf2a8c8, 'spore_blossom': 0xd060a0,
    'flowering_azalea': 0xc06ad8, 'chorus_flower': 0x9a6a9a,
}
GRASS = 0x6fa544; FOLIAGE = 0x5a9a32; WATER = 0x3a6fd8; DIRT = 0x8a6040

textures = []; texture_ids = {}
def tex(pattern, c1, c2=0):
    key = (pattern, c1, c2)
    if key not in texture_ids:
        texture_ids[key] = len(textures); textures.append(key)
    return texture_ids[key]

def shape_ids(b):
    s = shapes['blocks'][b['name']]
    return s if isinstance(s, list) else [s]

def full_cube(b):
    full = [k for k, v in shapes['shapes'].items() if v == [[0, 0, 0, 1, 1, 1]]]
    return all(str(s) in full for s in shape_ids(b))

def empty(b):
    return all(shapes['shapes'][str(s)] == [] for s in shape_ids(b))

def strip(n, *suffixes):
    for s in suffixes:
        if n.endswith(s): return n[:-len(s)]
    return n

def wood_of(n):
    for w in sorted(WOODS, key=len, reverse=True):
        if n.startswith(w+'_') or n.startswith('stripped_'+w+'_') or n.startswith('potted_'+w): return w
    return None

def dye_of(n):
    for d in sorted(DYES, key=len, reverse=True):
        if n.startswith(d+'_'): return d
    return None

def base_material(n):
    """Return (top, side, bottom) textures for the material a block is made of."""
    w = wood_of(n)
    if 'stripped' in n and w and any(k in n for k in ('log', 'wood', 'stem', 'hyphae', 'bamboo_block')):
        t = tex('LOG_TOP', WOODS[w][2], WOODS[w][2]); s = tex('PLANKS', WOODS[w][2], WOODS[w][2])
        return (t, s, t) if 'log' in n or 'stem' in n or 'bamboo_block' in n else (s, s, s)
    if w and any(n.endswith(k) for k in ('_log', '_stem', 'bamboo_block')):
        t = tex('LOG_TOP', WOODS[w][2], WOODS[w][1]); return (t, tex('LOG_SIDE', WOODS[w][1]), t)
    if w and any(n.endswith(k) for k in ('_wood', '_hyphae')):
        s = tex('LOG_SIDE', WOODS[w][1]); return (s, s, s)
    if w and not n.endswith(('_leaves', '_sapling', '_propagule', '_fungus', '_roots', '_nylium', '_wart_block')):
        p = tex('PLANKS', WOODS[w][0]); return (p, p, p)
    d = dye_of(n)
    if d:
        if 'glazed_terracotta' in n: t = tex('GLAZED', DYES[d], TERRACOTTA[d])
        elif 'terracotta' in n: t = tex('FLAT', TERRACOTTA[d])
        elif 'concrete_powder' in n: t = tex('SAND', DYES[d])
        elif 'concrete' in n: t = tex('FLAT', DYES[d])
        elif 'stained_glass' in n: t = tex('STAINED_GLASS', DYES[d])
        elif 'shulker' in n: t = tex('TILES', DYES[d])
        elif 'candle' in n and 'cake' not in n: t = tex('FLAT', DYES[d])
        else: t = tex('WOOL', DYES[d])
        return (t, t, t)
    for metal, color in METALS.items():
        if n in (metal+'_block', metal):
            t = tex('METAL' if metal not in ('coal', 'raw_iron', 'raw_copper', 'raw_gold', 'netherite') else 'STONE', color)
            return (t, t, t)
    if n.endswith('_ore'):
        host = STONES['deepslate'] if n.startswith('deepslate') else STONES['netherrack'] if n.startswith('nether') else STONES['stone']
        mineral = n.replace('deepslate_', '').replace('_ore', '')
        t = tex('ORE', host, ORE_SPECK.get(mineral, 0xffffff)); return (t, t, t)
    if 'copper' in n:
        c = 0x52a385 if 'oxidized' in n else 0x6c9e74 if 'weathered' in n else 0xa07c64 if 'exposed' in n else METALS['copper']
        t = tex('TILES' if 'cut' in n else 'METAL', c); return (t, t, t)
    base = n
    for prefix in ('infested_', 'chiseled_', 'cracked_', 'smooth_', 'cut_'):
        if base.startswith(prefix) and base[len(prefix):] in STONES or base.startswith(prefix) and strip(base[len(prefix):], '_stairs', '_slab', '_wall') in STONES:
            base = base[len(prefix):]
    base = strip(base, '_stairs', '_slab', '_wall', '_pillar', '_bricks', '_tiles', '_block')
    plural = strip(n, '_stairs', '_slab', '_wall')
    brick_like = plural.endswith(('bricks', 'tiles')) or 'brick_' in n or 'tile_' in n or n.endswith(('brick', 'tile'))
    for name in (plural, base, base+'_block', base.replace('polished_', '')):
        if name in STONES:
            c = STONES[name]
            if brick_like and 'bricks' in name and name == 'bricks': t = tex('BRICKS', c, 0xb0a39a)
            elif brick_like and name in ('nether_bricks', 'red_nether_bricks', 'mud_bricks'): t = tex('BRICKS', c, max(0, c-0x101010))
            elif brick_like: t = tex('STONE_BRICKS', c)
            elif n.startswith('polished') or n.startswith('smooth') or 'cut_' in n or name in ('quartz', 'purpur'): t = tex('TILES', c)
            elif name in ('sand', 'red_sand'): t = tex('SAND', c)
            elif name == 'gravel': t = tex('GRAVEL', c)
            elif 'cobble' in name: t = tex('COBBLE', c)
            elif name == 'bedrock': t = tex('BEDROCK', c)
            elif name in ('obsidian', 'crying_obsidian'): t = tex('OBSIDIAN', c)
            elif name == 'magma_block': t = tex('LAVA', c)
            elif name == 'sandstone' or name == 'red_sandstone':
                t = tex('SANDSTONE', c); return (tex('SAND', c), t, tex('SAND', c))
            else: t = tex('STONE', c)
            if name in ('basalt',) or n.endswith('_pillar'):
                top = tex('TILES', c); return (top, t, top)
            return (t, t, t)
    return None

def describe(b):
    n = b['name']
    model, pas, flags = 'CUBE', 0, []
    if n in ('air', 'cave_air', 'void_air', 'barrier', 'light', 'structure_void', 'moving_piston'):
        return 'NONE', 0, [], (0, 0, 0)
    if n.endswith('_sign'):
        t = tex('PLANKS', WOODS[wood_of(n) or 'oak'][0])
        return ('WALL_SIGN' if '_wall_' in n else 'SIGN'), 1, [], (t, t, t)
    if n.endswith('_banner'):
        t = tex('WOOL', DYES[dye_of(n) or 'white'])
        return ('WALL_BANNER' if '_wall_' in n else 'BANNER'), 1, [], (t, t, t)
    if n.endswith(('_sign', '_banner', '_button', '_head', '_skull')) or n in ('lever', 'tripwire', 'tripwire_hook', 'end_portal', 'end_gateway', 'nether_portal', 'fire', 'soul_fire', 'cobweb', 'frogspawn', 'sculk_vein', 'glow_lichen', 'structure_block', 'jigsaw'):
        if n in ('nether_portal', 'end_portal', 'end_gateway'):
            t = tex('WATER', 0x5a1fa8 if n == 'nether_portal' else 0x101020); return 'LIQUID', 2, ['EMISSIVE'], (t, t, t)
        if n in ('fire', 'soul_fire'):
            t = tex('FLOWER', 0xe07a10 if n == 'fire' else 0x30c8d0, 0xffd040); return 'CROSS', 1, ['EMISSIVE'], (t, t, t)
        if n == 'cobweb':
            t = tex('PLANT', 0xe8e8e8); return 'CROSS', 1, [], (t, t, t)
        return 'NONE', 0, [], (0, 0, 0)
    if n == 'water' or n == 'bubble_column':
        t = tex('WATER', WATER); return 'LIQUID', 2, ['WATER'], (t, t, t)
    if n == 'lava':
        t = tex('LAVA', 0xd25a12); return 'LIQUID', 0, ['EMISSIVE'], (t, t, t)
    if n in ('seagrass', 'tall_seagrass', 'kelp', 'kelp_plant') or n.endswith('coral') or n.endswith('coral_fan'):
        c = 0x3f8f2a if 'kelp' in n or 'grass' in n else 0x9a9a92 if n.startswith('dead') else {'tube': 0x3050d0, 'brain': 0xd05aa0, 'bubble': 0xa020c0, 'fire': 0xc02a2a, 'horn': 0xd8c030}.get(n.split('_')[0], 0x888888)
        t = tex('PLANT', c); return 'CROSS', 1, ['WATER'], (t, t, t)
    if n.endswith('coral_wall_fan') or n == 'sea_pickle':
        return 'NONE', 0, ['WATER'], (0, 0, 0)
    if n == 'grass_block':
        return 'CUBE', 0, ['OPAQUE'], (tex('GRASS_TOP', GRASS), tex('GRASS_SIDE', DIRT, GRASS), tex('DIRT', DIRT))
    if n == 'mycelium':
        return 'CUBE', 0, ['OPAQUE'], (tex('GRASS_TOP', 0x6f6265), tex('GRASS_SIDE', DIRT, 0x6f6265), tex('DIRT', DIRT))
    if n == 'podzol':
        return 'CUBE', 0, ['OPAQUE'], (tex('DIRT', 0x5b3d1a), tex('GRASS_SIDE', DIRT, 0x5b3d1a), tex('DIRT', DIRT))
    if n in ('crimson_nylium', 'warped_nylium'):
        c = 0x8b1d1d if 'crimson' in n else 0x1b7a6a
        return 'CUBE', 0, ['OPAQUE'], (tex('GRASS_TOP', c), tex('GRASS_SIDE', STONES['netherrack'], c), tex('STONE', STONES['netherrack']))
    if n in ('dirt', 'coarse_dirt', 'rooted_dirt'):
        t = tex('DIRT', DIRT if n == 'dirt' else 0x77553a); return 'CUBE', 0, ['OPAQUE'], (t, t, t)
    if n in ('farmland', 'dirt_path'):
        top = tex('DIRT', 0x5a3a1f) if n == 'farmland' else tex('SAND', 0x9a7f48)
        return 'BOXES', 0, [], (top, tex('DIRT', DIRT), tex('DIRT', DIRT))
    if n.endswith('_leaves') or n in ('flowering_azalea_leaves',):
        w = wood_of(n); c = WOODS[w][3] if w else 0x5a8a3a
        if 'azalea' in n: c = 0x5a8a30
        t = tex('LEAVES', c, 0xc070d0 if 'flowering' in n else (0xf0b0d0 if w == 'cherry' else 0))
        return 'CUBE', 1, ['LEAVES'], (t, t, t)
    if n in ('snow', 'snow_block', 'powder_snow'):
        t = tex('SNOW', 0xf6fbfb); return ('SNOW' if n == 'snow' else 'CUBE'), 0, ([] if n == 'snow' else ['OPAQUE']), (t, t, t)
    if n in ('ice', 'frosted_ice'):
        t = tex('ICE', 0x8eb4f8); return 'CUBE', 2, ['SAMECULL'], (t, t, t)
    if n in ('packed_ice', 'blue_ice'):
        t = tex('ICE_SOLID', 0x8aaee8 if n == 'packed_ice' else 0x74a6f0); return 'CUBE', 0, ['OPAQUE'], (t, t, t)
    if n in ('glass', 'glass_pane', 'tinted_glass'):
        t = tex('GLASS', 0xd8eef2 if n != 'tinted_glass' else 0x2a2430)
        return ('CUBE' if n != 'glass_pane' else 'BOXES'), 1, ['SAMECULL'], (t, t, t)
    if 'stained_glass' in n:
        t = base_material(n)[0]; return ('CUBE' if not n.endswith('pane') else 'BOXES'), 2, ['SAMECULL'], (t, t, t)
    if n in ('slime_block', 'honey_block'):
        t = tex('STAINED_GLASS', 0x70c050 if n == 'slime_block' else 0xf0a020); return 'CUBE', 2, ['SAMECULL'], (t, t, t)
    if n in ('torch', 'wall_torch', 'soul_torch', 'soul_wall_torch', 'redstone_torch', 'redstone_wall_torch'):
        flame = 0x40e0f0 if 'soul' in n else 0xff3010 if 'redstone' in n else 0xffd050
        t = tex('TORCH', 0x7a5a30, flame); return ('TORCH' if 'wall' not in n else 'NONE'), 1, ['EMISSIVE'], (t, t, t)
    if n in ('lantern', 'soul_lantern'):
        t = tex('METAL', 0xf0b040 if n == 'lantern' else 0x60d0e0); return 'BOXES', 0, ['EMISSIVE'], (t, t, t)
    if n in ('glowstone', 'sea_lantern', 'shroomlight', 'jack_o_lantern', 'redstone_lamp', 'ochre_froglight', 'verdant_froglight', 'pearlescent_froglight', 'beacon', 'end_rod'):
        c = {'glowstone': 0xf0c86a, 'sea_lantern': 0xc8e0dc, 'shroomlight': 0xf09a4a, 'jack_o_lantern': 0xe0901a,
             'redstone_lamp': 0x8a5530, 'ochre_froglight': 0xf2e0a0, 'verdant_froglight': 0xd8eec0,
             'pearlescent_froglight': 0xf0d8e8, 'beacon': 0x7ae8e0, 'end_rod': 0xf0e8e0}[n]
        t = tex('GLOW', c); return ('CUBE' if n != 'end_rod' else 'BOXES'), 0, ['EMISSIVE'] + (['OPAQUE'] if n not in ('beacon', 'end_rod') else []), (t, t, t)
    if n.endswith('rail'):
        t = tex('RAIL', 0x8a8a8a, 0x6a4a2a); return 'FLAT', 1, [], (t, t, t)
    if n == 'redstone_wire':
        t = tex('WIRE', 0xa01010); return 'FLAT', 1, [], (t, t, t)
    if n == 'lily_pad':
        t = tex('LEAVES', 0x2a7a2a); return 'FLAT', 1, [], (t, t, t)
    if n.endswith('_pressure_plate') or n.endswith('carpet') or n == 'moss_carpet':
        t = base_material(n.replace('_pressure_plate', '').replace('_carpet', '_wool') if 'carpet' in n and n != 'moss_carpet' else n.replace('_pressure_plate', ''))
        if n == 'moss_carpet': t = (tex('STONE', 0x597a2c),)*3
        if 'weighted' in n: t = (tex('METAL', 0xf6d03d if 'light' in n else 0xdcdcdc),)*3
        if t is None: t = (tex('TILES', 0x7f7f7f),)*3
        return 'PLATE', 0, [], t
    if n in ('cactus',):
        return 'BOXES', 0, [], (tex('TILES', 0x5a8a2a), tex('CACTUS', 0x4f7f25), tex('TILES', 0x5a8a2a))
    if n in ('pumpkin', 'carved_pumpkin', 'melon'):
        c = 0xd57d1c if 'pumpkin' in n else 0x6f9a26
        return 'CUBE', 0, ['OPAQUE'], (tex('LOG_TOP', c, c), tex('LOG_SIDE', c), tex('LOG_TOP', c, c))
    if n in ('hay_block',):
        return 'CUBE', 0, ['OPAQUE'], (tex('LOG_TOP', 0xc8a020, 0xa88010), tex('LOG_SIDE', 0xc8a020), tex('LOG_TOP', 0xc8a020, 0xa88010))
    if n in ('bookshelf', 'chiseled_bookshelf'):
        p = tex('PLANKS', WOODS['oak'][0]); return 'CUBE', 0, ['OPAQUE'], (p, tex('BOOKS', 0x6a4a2a), p)
    if n in ('crafting_table', 'furnace', 'blast_furnace', 'smoker', 'dispenser', 'dropper', 'observer', 'piston', 'sticky_piston', 'note_block', 'jukebox', 'loom', 'barrel', 'cartography_table', 'fletching_table', 'smithing_table', 'tnt', 'beehive', 'bee_nest', 'target', 'lodestone', 'respawn_anchor', 'spawner', 'command_block', 'chain_command_block', 'repeating_command_block', 'dried_kelp_block', 'sponge', 'wet_sponge', 'bone_block', 'honeycomb_block', 'moss_block', 'sculk', 'sculk_catalyst', 'nether_wart_block', 'warped_wart_block', 'ancient_debris', 'budding_amethyst', 'reinforced_deepslate', 'brown_mushroom_block', 'red_mushroom_block', 'mushroom_stem', 'muddy_mangrove_roots', 'gilded_blackstone', 'coal_block', 'quartz_bricks'):
        c = {'crafting_table': 0x8a6438, 'furnace': 0x6e6e6e, 'blast_furnace': 0x5a5a5e, 'smoker': 0x5a4a3a, 'dispenser': 0x707070, 'dropper': 0x707070,
             'observer': 0x5a5a5a, 'piston': 0x9a8a6a, 'sticky_piston': 0x8a9a6a, 'note_block': 0x6a4a32, 'jukebox': 0x6a4a32, 'loom': 0xb09a7a,
             'barrel': 0x7a5a32, 'cartography_table': 0x6a5a4a, 'fletching_table': 0xc8b48a, 'smithing_table': 0x3a3a46, 'tnt': 0xc83a2a,
             'beehive': 0xb8944a, 'bee_nest': 0xc8a03a, 'target': 0xe0d0c0, 'lodestone': 0x8a8a8e, 'respawn_anchor': 0x2a1a4a, 'spawner': 0x1e2a3a,
             'command_block': 0xb07a5a, 'chain_command_block': 0x7aa08a, 'repeating_command_block': 0x7a5ab0, 'dried_kelp_block': 0x3a4a2a,
             'sponge': 0xc8c048, 'wet_sponge': 0xa8a838, 'bone_block': 0xe0dcc8, 'honeycomb_block': 0xe09a20, 'moss_block': 0x597a2c,
             'sculk': 0x0e2a32, 'sculk_catalyst': 0x2a3a3a, 'nether_wart_block': 0x7a0a0a, 'warped_wart_block': 0x167e86, 'ancient_debris': 0x5e4a42,
             'budding_amethyst': 0x8a62c0, 'reinforced_deepslate': 0x50545a, 'brown_mushroom_block': 0x956a4b, 'red_mushroom_block': 0xc02a28,
             'mushroom_stem': 0xcbc4b8, 'muddy_mangrove_roots': 0x4a3a2a, 'gilded_blackstone': 0x3a2a26, 'coal_block': 0x1e1e1e, 'quartz_bricks': 0xece6df}[n]
        pattern = 'STONE' if n in ('moss_block', 'sculk', 'nether_wart_block', 'warped_wart_block', 'sponge', 'wet_sponge', 'coal_block', 'mushroom_stem', 'brown_mushroom_block', 'red_mushroom_block', 'muddy_mangrove_roots') else 'STONE_BRICKS' if n == 'quartz_bricks' else 'MACHINE'
        t = tex(pattern, c, max(0, c - 0x202020)); top = tex('TILES', c) if pattern == 'MACHINE' else t
        if n == 'tnt': top = tex('TILES', 0xd0d0c0)
        if n in ('crafting_table', 'fletching_table', 'cartography_table', 'smithing_table', 'loom', 'barrel', 'jukebox', 'note_block'): top = tex('PLANKS', c)
        return 'CUBE', 0, ['OPAQUE'], (top, t, top if n not in ('crafting_table',) else tex('PLANKS', WOODS['oak'][0]))
    if n in ('chest', 'trapped_chest', 'ender_chest'):
        t = tex('PLANKS', 0xa2742a) if 'ender' not in n else tex('TILES', 0x1e3a3a); return 'CHEST', 0, [], (t, t, t)
    if n.endswith('_bed'):
        d = dye_of(n); return 'BOXES', 0, [], (tex('WOOL', DYES[d]), tex('WOOL', DYES[d]), tex('PLANKS', WOODS['oak'][0]))
    if n in ('wheat', 'carrots', 'potatoes', 'beetroots', 'torchflower_crop', 'nether_wart', 'pumpkin_stem', 'melon_stem', 'attached_pumpkin_stem', 'attached_melon_stem', 'sugar_cane', 'bamboo', 'bamboo_sapling', 'sweet_berry_bush', 'cave_vines', 'cave_vines_plant', 'twisting_vines', 'twisting_vines_plant', 'weeping_vines', 'weeping_vines_plant', 'hanging_roots', 'big_dripleaf_stem', 'small_dripleaf', 'chorus_plant', 'cocoa', 'pointed_dripstone', 'amethyst_cluster', 'large_amethyst_bud', 'medium_amethyst_bud', 'small_amethyst_bud', 'chain', 'iron_bars'):
        c = {'wheat': 0xb8a838, 'carrots': 0x4a9a2a, 'potatoes': 0x4a9a2a, 'beetroots': 0x4a8a2a, 'nether_wart': 0x9a1a1a, 'sugar_cane': 0x8ac060,
             'bamboo': 0x6a9a2a, 'sweet_berry_bush': 0x2a6a3a, 'twisting_vines': 0x18a090, 'twisting_vines_plant': 0x18a090, 'weeping_vines': 0x8a1a1a,
             'weeping_vines_plant': 0x8a1a1a, 'hanging_roots': 0xa07a5a, 'chorus_plant': 0x5a3a5a, 'cocoa': 0x9a5a2a, 'pointed_dripstone': 0x86695a,
             'chain': 0x3a3e4a, 'iron_bars': 0x8a8a8a}.get(n, 0x9a62c8 if 'amethyst' in n else 0x5a9a32)
        model = 'BOXES' if n in ('chorus_plant', 'cocoa', 'iron_bars') else 'CROSS'
        t = tex('CROP', c); return model, 1, [], (t, t, t)
    if n in ('vine', 'ladder', 'scaffolding'):
        if n == 'scaffolding':
            t = tex('RAIL', 0xc8a050, 0xa07a30); return 'CUBE', 1, ['SAMECULL'], (t, t, t)
        t = tex('LEAVES', FOLIAGE) if n == 'vine' else tex('RAIL', 0x8a6a3a, 0x8a6a3a)
        return ('NONE' if n == 'vine' else 'BOXES'), 1, [], (t, t, t)
    if n in FLOWERS or n in ('azalea', 'flowering_azalea'):
        if n in ('azalea', 'flowering_azalea'):
            t = tex('LEAVES', 0x5a8a30, FLOWERS.get(n, 0)); return 'BOXES', 1, [], (t, t, t)
        t = tex('FLOWER', 0x4a8a2a, FLOWERS[n]); return 'CROSS', 1, [], (t, t, t)
    if n in ('brown_mushroom', 'red_mushroom', 'crimson_fungus', 'warped_fungus'):
        c = {'brown_mushroom': 0x9a6a4a, 'red_mushroom': 0xd02a28, 'crimson_fungus': 0xa02a2a, 'warped_fungus': 0x18a090}[n]
        t = tex('FLOWER', 0xd8d0c0, c); return 'CROSS', 1, [], (t, t, t)
    if n in ('grass', 'fern', 'tall_grass', 'large_fern', 'dead_bush', 'nether_sprouts', 'crimson_roots', 'warped_roots', 'mangrove_propagule') or n.endswith('_sapling'):
        c = 0x9a7a4a if n == 'dead_bush' else 0x18a090 if 'warped' in n or 'sprouts' in n else 0xa02a2a if 'crimson' in n else GRASS - 0x081008
        w = wood_of(n)
        if n.endswith('_sapling') and w: c = WOODS[w][3]
        t = tex('PLANT', c) if not n.endswith('_sapling') else tex('LEAVES', c); return 'CROSS', 1, [], (t, t, t)
    if n.startswith('potted_') or n == 'flower_pot':
        t = tex('TILES', 0x7a4030); return 'BOXES', 0, [], (t, t, t)
    material = base_material(n)
    if material is None:
        material = (tex('STONE', 0x8a8a8a),)*3
    if empty(b):
        return 'NONE', 0, [], (0, 0, 0)
    if full_cube(b):
        return 'CUBE', 0, ['OPAQUE'], material
    return 'BOXES', 0, [], material

lines = ['/* Generated by tools/blockinfo.py. Block names, state IDs and shapes: minecraft-data (MIT);',
         '   see vendor/MINECRAFT-DATA-LICENSE. Colors and patterns are original approximations. */',
         f'/* blocks SHA256 {hashlib.sha256(braw).hexdigest()} */', f'/* shapes SHA256 {hashlib.sha256(sraw).hexdigest()} */',
         '#ifdef MC_BLOCKINFO_BLOCKS', 'static const McBlockDef block_defs[] = {']
tex(*('FLAT', 0xff00ff, 0))  # texture 0: missing
for b in blocks:
    model, pas, flags, (top, side, bottom) = describe(b)
    if [st['name'] for st in b['states']] == ['axis'] and model == 'CUBE': flags = flags + ['AXIS']
    flag_expr = '|'.join('MF_'+f for f in flags) or '0'
    stride, count = 0, 0
    for j, st in enumerate(b['states']):
        if st['name'] in ('facing', 'rotation'):
            stride, count = 1, st['num_values']
            for tail in b['states'][j+1:]: stride *= tail['num_values']
            break
    lines.append(f'{{{b["minStateId"]},{b["maxStateId"]},MM_{model},{pas},{flag_expr},{top},{side},{bottom},{stride},{count}}}, /* {b["name"]} */')
lines += ['};', '#endif', '#ifdef MC_BLOCKINFO_TEXTURES', 'static const McTexDef tex_defs[] = {']
lines += [f'{{TP_{p},0x{c1:06x},0x{c2:06x}}},' for p, c1, c2 in textures]
lines += ['};', '#endif']
Path('src/blockinfo.inc').write_text('\n'.join(lines)+'\n')
print(len(blocks), 'blocks,', len(textures), 'textures')
