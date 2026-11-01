import copy
import unittest
from skinlab.profiles import profile_from_report, skin_catalog_entry


class RuntimeProfileTests(unittest.TestCase):
    def setUp(self):
        self.sha = 'a' * 64
        recipes = [('local_player','rva',0x8000),('character_data_stack','field_offset',0x120),
                   ('skin_id_encoding','field_offset',0x20),('character_data_stack_update','rva',0x2000),
                   ('material_registry_getter','rva',0x3000),('swap_chain','field_offset',0x40)]
        rules = [{'name':n,'state':'unique_reread_equal','candidate':{key:v}} for n,key,v in recipes]
        rules[2]['candidate'].update(rotate_first=5,xor_key=0xa5192d37,rotate_second=11)
        self.report = {'status':'observed','disk_sha256':self.sha,'observation':{
            'image_size':0x10000,'collection_complete':True,'gap_count':0,'code_snapshot_sha256':'b'*64,
            'model_name':'Fixture','results':rules}}

    def test_required_candidates_can_resolve_despite_unrelated_ambiguity(self):
        self.report['observation']['results'].append({'name':'hero_list','state':'ambiguous','candidate':None})
        profile = profile_from_report(self.report,self.sha)
        self.assertEqual(profile.character_stack_offset,0x120)
        self.assertEqual(profile.codec.xor_key,0xa5192d37)

    def test_changed_build_is_rejected(self):
        with self.assertRaisesRegex(ValueError,'identity'):
            profile_from_report(self.report,'c'*64)

    def test_critical_ambiguity_is_rejected(self):
        self.report['observation']['results'][0]['state']='ambiguous'
        with self.assertRaisesRegex(ValueError,'local_player'):
            profile_from_report(self.report,self.sha)

    def test_partial_or_bad_rva_is_rejected(self):
        for mutation in ('partial','outside'):
            r=copy.deepcopy(self.report)
            if mutation=='partial':r['observation']['gap_count']=1
            else:r['observation']['results'][0]['candidate']['rva']=0x10000
            with self.subTest(mutation=mutation),self.assertRaises(ValueError):profile_from_report(r,self.sha)

    def test_bad_rotation_is_rejected(self):
        self.report['observation']['results'][2]['candidate']['rotate_first']=32
        with self.assertRaisesRegex(ValueError,'rotation'):profile_from_report(self.report,self.sha)

    def test_chroma_availability_does_not_exclude_base_skin(self):
        row=skin_catalog_entry('Smolder',{'num':1,'name':'Heavenscale Smolder','chromas':True})
        self.assertTrue(row['has_chroma_variants'])
        self.assertIsNone(row['requires_special_handling'])


if __name__=='__main__':unittest.main()
