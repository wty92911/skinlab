import copy
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch
from skinlab.catalog import load_catalog, validate_catalog
from scripts.update_catalog import parse_tencent_champion, update_catalog
SEED = {'schema_version':1,'version':'16.21','champions':[{'id':'Smolder','skins':[
    {'skin_id':0,'name':'原皮'},{'skin_id':1,'name':'天龙之子'}]}]}


class CatalogTests(unittest.TestCase):
    def test_available_chromas_do_not_remove_base_skin(self):
        raw={'skins':[{'skinId':'901000','name':'原皮','chromasBelongId':'0'},
                      {'skinId':'901001','name':'基础款式','chromasBelongId':'0'},
                      {'skinId':'901002','name':'炫彩变体','chromasBelongId':'901001'}]}
        row=parse_tencent_champion({'alias':'Smolder','heroId':'901','name':'斯莫德'},raw)
        self.assertEqual([x['skin_id'] for x in row['skins']],[0,1])
        self.assertTrue(row['skins'][1]['has_chroma_variants'])

    def test_duplicate_skin_and_bad_champion_rejected(self):
        for change in ('duplicate','path'):
            data=copy.deepcopy(SEED)
            if change=='duplicate':data['champions'][0]['skins'].append(data['champions'][0]['skins'][0])
            else:data['champions'][0]['id']='../bad'
            with self.subTest(change=change),self.assertRaises(ValueError):validate_catalog(data)

    def test_failed_update_preserves_existing_catalog(self):
        with tempfile.TemporaryDirectory() as folder:
            path=Path(folder)/'catalog.json';before=json.dumps(SEED);path.write_text(before,encoding='utf-8')
            def fetch(url):
                if url.endswith('hero_list.js'):return {'hero':[{'heroId':'901','alias':'Smolder','name':'斯莫德'}],'version':'16.21'}
                raise OSError('network failure')
            with patch('scripts.update_catalog.read_json',side_effect=fetch),self.assertRaises(OSError):update_catalog(path)
            self.assertEqual(path.read_text('utf-8'),before)

    def test_shipped_catalog_loads_offline(self):
        with patch('urllib.request.urlopen', side_effect=AssertionError('network')):
            catalog = load_catalog()
            self.assertGreater(len(catalog['champions']), 100)


if __name__=='__main__':unittest.main()
