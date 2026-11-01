"""Release-shipped skin names and IDs. Loading never accesses the network."""
import json
from pathlib import Path
import re

CATALOG_PATH = Path(__file__).resolve().parent / 'data/catalog.json'

def validate_catalog(catalog):
    if catalog.get('schema_version')!=1 or not re.fullmatch(r'\d+\.\d+(?:\.\d+)?',catalog.get('version','')):
        raise ValueError('Invalid catalog version/schema')
    champions=catalog.get('champions',[])
    if not 1<=len(champions)<=500:raise ValueError('Invalid champion count')
    seen=set()
    for champion in champions:
        model=champion['id']
        if not re.fullmatch('[A-Za-z][A-Za-z0-9]{0,39}',model) or model in seen:raise ValueError('Invalid or duplicate champion ID')
        seen.add(model);numbers=set()
        for skin in champion['skins']:
            number=skin['skin_id']
            if type(number) is not int or not 0<=number<=100000 or number in numbers:raise ValueError('Invalid or duplicate skin number')
            if not isinstance(skin['name'],str) or not 1<=len(skin['name'])<=256:raise ValueError('Invalid skin name')
            numbers.add(number)
        if 0 not in numbers:raise ValueError('Champion catalog lacks default skin')
    return catalog


def load_catalog(path=CATALOG_PATH):
    return validate_catalog(json.loads(Path(path).read_text('utf-8')))
