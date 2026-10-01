"""Add one Wardrobe consumable while preserving every original archive entry."""
import copy
import io
import sys
import zipfile
from pathlib import Path
from xml.etree import ElementTree as ET

from codec import read_pak, binary_xml, encode_binary_xml, encode_pak

ITEM_ID = 168100001
STRINGS = ((9901001, 'STR_WARDROBE_APPEARANCE_UNLOCK', 'Appearance Unlock'),
           (9901002, 'STR_WARDROBE_APPEARANCE_UNLOCK_DESC',
            'Unlocks one equipment appearance for your account in Wardrobe. Keeps the equipment. Unlocked skins can be applied without another material.'))

def rebuild(path, replacements, destination):
    output = io.BytesIO()
    with read_pak(path) as source, zipfile.ZipFile(output, 'w', compression=zipfile.ZIP_DEFLATED) as target:
        for entry in source.infolist():
            target.writestr(entry, replacements.get(entry.filename, source.read(entry.filename)))
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(encode_pak(output.getvalue()))
    with read_pak(destination) as check:
        assert check.testzip() is None
        for name, data in replacements.items():
            assert check.read(name) == data

def prepare_wardrobe_item(client, output):
    path = client / 'Data/Items/Items.pak'
    with read_pak(path) as archive:
        root = binary_xml(archive.read('client_items_etc.xml'))
        existing = [e for e in root if e.findtext('id') == str(ITEM_ID)]
        if existing and existing[0].findtext('name') != 'wardrobe_appearance_unlock':
            raise ValueError('Appearance Unlock item ID is already occupied')
        for entry in existing:
            root.remove(entry)
        original = next(e for e in root if e.findtext('id') == '168100000')
        item = copy.deepcopy(original)
        values = {'id': str(ITEM_ID), 'name': 'wardrobe_appearance_unlock',
                  'desc': STRINGS[0][1], 'desc_long': STRINGS[1][1],
                  'max_stack_count': '100', 'can_split': 'TRUE', 'quality': 'rare'}
        for key, value in values.items():
            child = item.find(key)
            if child is None:
                child = ET.SubElement(item, key)
            child.text = value
        root.insert(list(root).index(original) + 1, item)
        changed = encode_binary_xml(root)
        assert binary_xml(changed).findall('client_item')
    rebuild(path, {'client_items_etc.xml': changed}, output / 'Data/Items/Items.pak')
    locale = output / 'L10N/enu/data/data.pak'
    source = locale if locale.exists() else client / 'L10N/enu/data/data.pak'
    name = 'strings/client_strings_item.xml'
    with read_pak(source) as archive:
        data = archive.read(name)
        binary = data[:1] == b'\x80'
        root = binary_xml(data) if binary else ET.fromstring(data)
        for ident, label, text in STRINGS:
            conflicts = [e for e in root if e.findtext('id') == str(ident) or e.findtext('name') == label]
            for entry in conflicts:
                if entry.findtext('name') != label:
                    raise ValueError('Wardrobe string ID is already occupied')
                root.remove(entry)
            entry = ET.SubElement(root, 'string')
            for key, value in (('id', str(ident)), ('name', label), ('body', text)):
                ET.SubElement(entry, key).text = value
        changed = encode_binary_xml(root) if binary else ET.tostring(root, encoding='utf-16', xml_declaration=True)
    rebuild(source, {name: changed}, locale)
