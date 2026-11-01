"""Developer-only catalog refresh; review and ship the resulting JSON in a release."""
from concurrent.futures import ThreadPoolExecutor, as_completed
from datetime import datetime, timezone
import argparse
import json
from pathlib import Path
import re
import sys
import urllib.request
sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from skinlab.catalog import CATALOG_PATH, validate_catalog
from skinlab.profiles import skin_catalog_entry

def read_json(url):
    request=urllib.request.Request(url,headers={'User-Agent':'SkinLab-Catalog/0.3'})
    with urllib.request.urlopen(request,timeout=5) as response:
        data=response.read(8*1024*1024+1)
    if len(data)>8*1024*1024:raise ValueError('Catalog response exceeds limit')
    return json.loads(data)


TENCENT='https://game.gtimg.cn/images/lol/act/img/js/'


def parse_tencent_champion(item,raw):
    model=item['alias'];hero=int(item['heroId']);skins=[]
    if not re.fullmatch('[A-Za-z][A-Za-z0-9]{0,39}',model):raise ValueError('Invalid remote champion alias')
    entries=raw['skins']
    parents={str(s.get('chromasBelongId','0')) for s in entries if str(s.get('chromasBelongId','0'))!='0'}
    for skin in entries:
        # This provider lists chroma variants as separate records; exclude the
        # variants rather than excluding the base skin that owns them.
        if str(skin.get('chromasBelongId','0'))!='0':continue
        number=int(skin['skinId'])-hero*1000
        row=skin_catalog_entry(model,{'num':number,'name':skin['name'],'chromas':str(skin['skinId']) in parents})
        row['rarity']=str(skin.get('emblemsName',''))
        if row['rarity'].casefold() in ('ultimate',):row['requires_special_handling']=True
        skins.append(row)
    return {'id':model,'key':str(hero),'name':item.get('title',item['name'])+' · '+item['name'],
            'skins':sorted(skins,key=lambda s:s['skin_id'])}


def update_catalog(path,progress=None,cancel=None):
    """Use the official CN website data; catalog version != runtime compatibility."""
    index=read_json(TENCENT+'heroList/hero_list.js');items=index['hero']
    if not 1<=len(items)<=500:raise ValueError('Unexpected champion count')
    def fetch(item):
        hero=str(item['heroId'])
        if not hero.isdecimal() or len(hero)>6:raise ValueError('Invalid remote champion key')
        return parse_tencent_champion(item,read_json(TENCENT+'hero/'+hero+'.js'))
    champions=[]
    pool=ThreadPoolExecutor(max_workers=6)
    try:
        for future in as_completed([pool.submit(fetch,item) for item in items]):
            if cancel and cancel.is_set():raise OSError('Catalog update cancelled')
            champions.append(future.result())
            if progress:progress(len(champions),len(items))
    except BaseException:
        pool.shutdown(wait=False,cancel_futures=True);raise
    else:pool.shutdown(wait=True)
    if cancel and cancel.is_set():raise OSError('Catalog update cancelled')
    catalog=validate_catalog({'schema_version':1,'version':str(index['version']),'locale':'zh_CN','source_file_time':index.get('fileTime'),
        'source':TENCENT,'updated_at_utc':datetime.now(timezone.utc).isoformat(),
        'champions':sorted(champions,key=lambda c:c['id'])})
    path=Path(path);path.parent.mkdir(parents=True,exist_ok=True)
    temporary=path.with_suffix('.json.tmp');temporary.write_text(json.dumps(catalog,ensure_ascii=False,indent=2),encoding='utf-8');temporary.replace(path)
    return catalog



if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=CATALOG_PATH)
    args = parser.parse_args()
    result = update_catalog(args.output)
    print(json.dumps({'version': result['version'], 'champions': len(result['champions']),
                      'skins': sum(len(c['skins']) for c in result['champions'])}))
