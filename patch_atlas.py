import re

with open('src/atlas.cpp', 'r') as f:
    content = f.read()

def replace_func(name, r, g, b, is_alpha=False):
    global content
    if is_alpha:
        body = f"    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {{\n        setPixelA(d, col*TILE_PX+tx, row*TILE_PX+ty, {r}, {g}, {b}, 255);\n    }}"
    else:
        body = f"    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {{\n        setPixel(d, col*TILE_PX+tx, row*TILE_PX+ty, {r}, {g}, {b});\n    }}"
    
    # regex to match static void function_name(...d, int col, int row) { ... }
    pattern = r'(static void ' + name + r'\s*\(.*?\) \{)(.*?)(^\})'
    content = re.sub(pattern, r'\1\n' + body + r'\n\3', content, flags=re.DOTALL | re.MULTILINE)

# Solid colors
replace_func('genGrassTop', 62, 175, 35)
replace_func('genGrassSide', 62, 175, 35) # Same as grass top or maybe dirt below? Make it all grass color.
replace_func('genDirt', 128, 86, 50)
replace_func('genStone', 125, 125, 125)
replace_func('genWoodTop', 178, 118, 56)
replace_func('genWoodSide', 165, 108, 50)
replace_func('genLeaves', 40, 128, 24)
replace_func('genSand', 218, 196, 112)
replace_func('genGravel', 110, 110, 110)
replace_func('genSnow', 235, 238, 250)
replace_func('genCactusTop', 30, 105, 22)
replace_func('genCactusSide', 30, 105, 22)
replace_func('genSandstone', 205, 175, 90)
replace_func('genIce', 195, 212, 238)
replace_func('genGlowstone', 255, 215, 70)
replace_func('genWater', 20, 95, 210)

replace_func('genTallGrass', 50, 162, 20, True)

# genFlower has extra param
pattern_flower = r'(static void genFlower\s*\(.*?\) \{)(.*?)(^\})'
flower_body = r'    for (int ty = 0; ty < TILE_PX; ty++) for (int tx = 0; tx < TILE_PX; tx++) {\n        setPixelA(d, col*TILE_PX+tx, row*TILE_PX+ty, hr, hg, hb, 255);\n    }'
content = re.sub(pattern_flower, r'\1\n' + flower_body + r'\n\3', content, flags=re.DOTALL | re.MULTILINE)

with open('src/atlas.cpp', 'w') as f:
    f.write(content)

