"""Maintain the Japanese catalog; source identifiers and project data stay English."""
from pathlib import Path
import re
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
dictionary = {}
for line in (ROOT / 'translations/ja.tsv').read_text(encoding='utf-8').splitlines():
    if '\t' in line:
        en, ja = line.split('\t', 1)
        dictionary[en.replace('\\n', '\n').replace('\\t', '\t')] = ja.replace('\\n', '\n').replace('\\t', '\t')

def translate(text):
    if text in dictionary:
        return dictionary[text]
    # Brand names, sample identifiers, literal colors and keyboard bindings.
    if text in {'Porymap', 'porymap', 'MyNewMap', 'MyNewLayout', 'LAYOUT_MY_NEW_LAYOUT',
                'RGB (000, 000, 000)', '#FFFFFF', 'prefabs.json', '4bpp', '8bpp', 'ms'}:
        return text
    if re.fullmatch(r'(?:Ctrl|Alt|Shift)\+.*|[A-Zxy]|[0-9]+|\.{3}|#|%1', text):
        return text
    return None

tree = ET.parse(ROOT / 'translations/porymap_ja.ts')
root = tree.getroot()
root.set('language', 'ja_JP')
missing = []
for context in root.findall('context'):
    for message in context.findall('message'):
        source = message.findtext('source') or ''
        translation = message.find('translation')
        if translation is None:
            translation = ET.SubElement(message, 'translation')
        value = translate(source)
        if value is None and source.startswith('<html>'):
            # Translate complete rich-text paragraphs, retaining Qt markup and links.
            value = source
            for en, ja in sorted(dictionary.items(), key=lambda p: -len(p[0])):
                value = value.replace('>' + en + '<', '>' + ja + '<')
            if value == source:
                value = None
        if value is not None:
            placeholders = lambda s: set(re.findall(r'%(?:[1-9][0-9]*|n|[DFL])', s))
            if placeholders(source) != placeholders(value):
                raise ValueError('Translation placeholders differ: ' + source)
            translation.attrib.pop('type', None)
            translation.text = value
        else:
            missing.append(source)
if missing:
    raise ValueError('Untranslated messages:\n' + '\n'.join(dict.fromkeys(missing)))
ET.indent(tree, space='    ')
tree.write(ROOT / 'translations/porymap_ja.ts', encoding='utf-8', xml_declaration=True)
print('Translated:', sum(m.find('translation').get('type') != 'unfinished' for m in root.findall('./context/message')))
