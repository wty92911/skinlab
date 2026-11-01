import unittest
from skinlab.active_game import active_hero_from_data


class ActiveHeroTests(unittest.TestCase):
    def test_selects_active_player_not_first_player(self):
        players=[{'summonerName':'bot','rawChampionName':'game_character_displayname_Annie'},
                 {'summonerName':'self','rawChampionName':'game_character_displayname_Lux','championName':'光辉女郎','skinID':0}]
        self.assertEqual(active_hero_from_data('self',players)['model'],'Lux')

    def test_accepts_riot_id_and_does_not_return_player_name(self):
        hero=active_hero_from_data('name#tag',[{'riotId':'name#tag','rawChampionName':'game_character_displayname_Smolder','skinID':1}])
        self.assertEqual(hero['model'],'Smolder')
        self.assertNotIn('riotId',hero)

    def test_no_guess_when_active_player_is_missing(self):
        self.assertIsNone(active_hero_from_data('self',[{'summonerName':'bot','rawChampionName':'game_character_displayname_Lux'}]))


if __name__=='__main__':unittest.main()
