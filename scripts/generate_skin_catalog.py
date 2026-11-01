"""Compile the public skin-number catalog into the shared native adapter."""
import argparse
import hashlib
import json
from pathlib import Path
import sys
sys.path.insert(0,str(Path(__file__).resolve().parents[1]))
from skinlab.catalog import validate_catalog


def generate(source,output):
    data=Path(source).read_bytes();catalog=validate_catalog(json.loads(data))
    values=[];entries=[]
    for champion in catalog['champions']:
        skins=sorted({s['skin_id'] for s in champion['skins']})
        entries.append((champion['id'],len(values),len(skins)));values.extend(skins)
    lines=['// Generated from the public catalog. Availability is not a test result.',
           f'// Source SHA256: {hashlib.sha256(data).hexdigest()}',
           '#ifndef SKINLAB_CATALOG_GENERATED_H','#define SKINLAB_CATALOG_GENERATED_H',
           'static const int32_t sl_catalog_numbers[]={'+','.join(map(str,values))+'};',
           'struct SlCatalogModel {const char* name;uint32_t first,count;};',
           'static const SlCatalogModel sl_catalog_models[]={']
    lines.extend('    {"%s",%d,%d},'%entry for entry in entries)
    lines += ['};','#endif','']
    Path(output).write_text('\n'.join(lines),encoding='ascii')
    return {'champions':len(entries),'skin_styles':len(values),'catalog_sha256':hashlib.sha256(data).hexdigest()}


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source',required=True,type=Path);parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args();print(json.dumps(generate(args.source,args.output)))
