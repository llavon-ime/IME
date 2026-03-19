from pathlib import Path
import json

def bopomofo_cns(path: str):
    mp = {}
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()
        for line in lines:
            cns, bopo = line.strip().split('	')
            if bopo in mp:
                mp[bopo].append(cns)
            else:
                mp[bopo] = [cns]
    return mp

def cns_unicode_path(path: str):
    mp = {}
    with open(path, encoding='utf-8') as f:
        lines = f.readlines()
        for line in lines:
            cns, unicode = line.strip().split('	')
            mp[cns] = unicode
    return mp

# def cns_unicode(dir: str):
#     dir: Path = Path(dir)
#     mp = {}
#     for path in dir.iterdir():
#         with open(path, encoding='utf-8') as f:
#             lines = f.readlines()
#             for line in lines:
#                 cns, unicode = line.strip().split('	')
#                 mp[cns] = unicode
#     return mp

def utfcode2char(code: str):
    return chr(int(code, 16)).encode('utf-8').decode('utf-8')

if __name__ == "__main__":
    b2cns = bopomofo_cns("../.dataset/Properties/CNS_phonetic.txt")
    cns2unicode = cns_unicode_path("../.dataset/MapingTables/Unicode/CNS2UNICODE_Unicode BMP.txt")
    # cns2unicode = cns_unicode('../.dataset/MapingTables/Unicode')
    b2char = {}
    for b, cns_list in b2cns.items():
        # char_list = [utfcode2char(cns2unicode[cns]) for cns in cns_list]
        char_list = []
        for cns in cns_list:
            if cns in cns2unicode:
                char_list.append(utfcode2char(cns2unicode[cns]))
        if b[-1] not in [' ', 'ˊ', 'ˇ', 'ˋ', '˙']:
            b += ' '        
        b2char[b] = char_list
    
    with open("../.dataset/bopomofo_char.txt", "w", encoding='utf-8') as f:
        json.dump(b2char, f, ensure_ascii=False)