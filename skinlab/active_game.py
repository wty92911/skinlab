"""Read only the local Live Client API; never injects or activates a session."""
import json
import ssl
import urllib.request


def active_hero_from_data(active_name,players):
    for player in players:
        if active_name not in (player.get('summonerName'),player.get('riotId')):continue
        raw=player.get('rawChampionName','');prefix='game_character_displayname_'
        model=raw[len(prefix):] if raw.startswith(prefix) else ''
        if model:return {'model':model,'skin_id':player.get('skinID'),'display_name':player.get('championName','')}
    return None


def current_hero():
    # Keep loopback traffic local even on machines with a system HTTP proxy.
    opener=urllib.request.build_opener(urllib.request.ProxyHandler({}),
        urllib.request.HTTPSHandler(context=ssl._create_unverified_context()))
    def get(endpoint):
        with opener.open('https://127.0.0.1:2999/liveclientdata/'+endpoint,timeout=.8) as response:
            data=response.read(1024*1024+1)
        if len(data)>1024*1024:raise ValueError('Local response too large')
        return json.loads(data)
    try:return active_hero_from_data(get('activeplayername'),get('playerlist'))
    except (OSError,ValueError):return None
