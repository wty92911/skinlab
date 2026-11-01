"""Typed runtime candidates and explicit coverage, independent of the UI.

This module does not read/write a process, load a DLL, or treat a unique byte
pattern as a universal compatibility guarantee.
"""
from dataclasses import asdict, dataclass
import re


def _u32(value, name, minimum=0, maximum=0xffffffff):
    if type(value) is not int or not minimum <= value <= maximum:
        raise ValueError(f'Invalid {name}')
    return value


@dataclass(frozen=True)
class SkinCodec:
    rotate_first: int
    xor_key: int
    rotate_second: int


@dataclass(frozen=True)
class OrdinaryProfile:
    game_sha256: str
    code_snapshot_sha256: str
    local_player_rva: int
    character_stack_offset: int
    object_skin_offset: int
    update_rva: int
    material_registry_getter_rva: int
    swap_chain_offset: int
    codec: SkinCodec
    observed_model: str
    # Structure-member evidence, distinct from instruction-parser offsets.
    stack_skin_offset: int = 0x38
    stack_model_pointer_offset: int = 0x18

    def to_dict(self):
        return asdict(self)


def profile_from_report(report, expected_disk_sha256):
    if not re.fullmatch('[0-9a-f]{64}', expected_disk_sha256):
        raise ValueError('Expected a full lowercase disk SHA256')
    if report.get('disk_sha256') != expected_disk_sha256:
        raise ValueError('Game build identity differs from the expected build')
    image = report.get('observation') or {}
    if report.get('status') != 'observed' or not image.get('collection_complete') or image.get('gap_count') != 0:
        raise ValueError('A complete observed code snapshot is required')
    if not re.fullmatch('[0-9a-f]{64}', image.get('code_snapshot_sha256', '')):
        raise ValueError('Missing code snapshot identity')
    size = _u32(image.get('image_size'), 'image size', 1, 128 * 1024 * 1024)
    rules = {}
    for rule in image.get('results', []):
        name = rule.get('name')
        if name in rules:
            raise ValueError('Duplicate rule names')
        rules[name] = rule

    def field(name, key, maximum):
        rule = rules.get(name, {})
        if rule.get('state') != 'unique_reread_equal' or not rule.get('candidate'):
            raise ValueError(f'{name} is not unique and reread-equal')
        return _u32(rule['candidate'].get(key), name, 1, maximum)

    local = field('local_player', 'rva', size - 8)
    stack = field('character_data_stack', 'field_offset', 1024 * 1024)
    skin = field('skin_id_encoding', 'field_offset', 1024 * 1024)
    encoding = rules['skin_id_encoding']['candidate']
    codec = SkinCodec(_u32(encoding.get('rotate_first'), 'first rotation', 0, 31),
                      _u32(encoding.get('xor_key'), 'XOR key'),
                      _u32(encoding.get('rotate_second'), 'second rotation', 0, 31))
    return OrdinaryProfile(expected_disk_sha256, image['code_snapshot_sha256'], local, stack, skin,
        field('character_data_stack_update', 'rva', size - 1),
        field('material_registry_getter', 'rva', size - 1),
        field('swap_chain', 'field_offset', 1024 * 1024), codec, image.get('model_name', ''))


def skin_catalog_entry(champion, raw):
    """Data Dragon `chromas` means available variants, NOT 'this is a chroma'.

    Data Dragon's basic skin list is insufficient to establish all special
    runtime requirements. Unknown classification stays unknown.
    """
    number = _u32(raw.get('num'), 'skin catalog number')
    return {'champion': champion, 'skin_id': number, 'name': str(raw.get('name', '')),
            'has_chroma_variants': bool(raw.get('chromas', False)),
            'requires_special_handling': None}
