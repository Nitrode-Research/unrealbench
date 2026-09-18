"""Compile E02 scene composition and pinned asset inputs without modifying E01."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import subprocess
from scene_coordinates import position,point

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'SourceData/UnityScenes'


def write(path,value):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(value,indent=2)+'\n',encoding='utf-8')


def compile_definition(source=None,capture=None):
    capture=ROOT/"SourceData/EntryComposition" if capture is None else capture
    index=json.loads((SOURCE/'index.json').read_text())
    base=json.loads((ROOT/'Docs/Fixtures/EntryTerrainDefinition.json').read_text())
    scene=json.loads((SOURCE/'Scenes/EntryGate.json').read_text())
    known={x['sourceId'] for x in base['geometry']}
    meshes={m['key']:m for m in index['meshes']}
    supplement=json.loads((capture/'ItemDisplays.json').read_text())
    assert supplement['sourceRevision']==index['sourceRevision']
    extra=ROOT/'SourceData/EntryComposition'
    extra.mkdir(parents=True,exist_ok=True)
    # Supplied capture is immutable when it already occupies the staging location.
    if capture.resolve()!=extra.resolve(): write(extra/'ItemDisplays.json',supplement)
    for key,entry in supplement['meshes'].items():
        target=extra/entry['file'];target.parent.mkdir(parents=True,exist_ok=True)
        if (capture/entry['file']).resolve()!=target.resolve(): shutil.copy2(capture/entry['file'],target)
        meshes[key]={**entry,'file':target.relative_to(ROOT).as_posix()}
    for key,entry in meshes.items():
        if not entry['file'].startswith('SourceData/'):entry['file']='SourceData/UnityScenes/'+entry['file']
    def props(c):return {p['path']:p['value'] for p in c['properties']}
    all_environment=[]; additions=[]; excluded=[]
    def placement(identity,name,path,key,matrix,materials,visible=True,display=None):
        linear=[matrix[r*4+c] for r in range(3) for c in range(3)]
        sig=hashlib.sha256(json.dumps([key,linear],separators=(',',':')).encode()).hexdigest()[:12]
        label=re.sub('[^A-Za-z0-9_]','_',meshes[key]['source']['name'])
        return {'sourceId':identity,'name':name,'path':path,'mesh':key,'matrix':matrix,
                'pivot':position([matrix[3],matrix[7],matrix[11]]),
                'asset':f'/Game/Task0170/EntryComposition/SM_{label}_{sig}',
                'materials':materials,'initiallyVisible':visible,'display':display,
                'kind':'foliage' if any(m.endswith('/Leaves.mat') for m in materials) else 'prop',
                'collision':'NoCollision; source world collision and navigation are retained from E01'}
    for node in scene['nodes']:
        if node['path'].startswith('0:StoryBundle'):continue
        cs={c['type']:c for c in node['components']}
        renderer=cs.get('UnityEngine.MeshRenderer');mesh=cs.get('UnityEngine.MeshFilter')
        if not renderer or not mesh:continue
        if not node['activeInHierarchy'] or not renderer['renderer']['enabled']:
            excluded.append({'sourceId':node['id'],'path':node['path'],'reason':'Inactive source variant'})
            continue
        if node['layer']==9:
            excluded.append({'sourceId':node['id'],'path':node['path'],'reason':'Interaction gizmo/UI, tracked by E03 and I01'})
            continue
        geometry=json.loads((ROOT/meshes[mesh['mesh']]['file']).read_text())
        world_vertices=[point(node['worldMatrix'],v) for v in geometry['vertices']]
        low=[min(v[i] for v in world_vertices) for i in range(3)]
        high=[max(v[i] for v in world_vertices) for i in range(3)]
        bounds={'center':[(a+b)/2 for a,b in zip(low,high)],'size':[b-a for a,b in zip(low,high)]}
        all_environment.append({'sourceId':node['id'],'path':node['path'],'mesh':mesh['mesh'],'worldMatrix':node['worldMatrix'],
                                'bounds':bounds,'sourceRendererBounds':renderer['renderer']['bounds']})
        if node['id'] in known:continue
        mats=[m['assetPath'] for m in renderer['renderer']['materials']]
        additions.append(placement(node['id'],node['name'],node['path'],mesh['mesh'],node['worldMatrix'],mats))
    for display in supplement['displays']:
        scope={'SecurityKisokInteraction':'EntryGate.Kiosk','RockInteraction':'EntryGate.Rock','RollingGateInteraction':'EntryGate.Gate'}[display['interactionName']]
        behavior={'interactionId':scope,'itemFact':display['itemFact'],'initialItem':display['initialItem']}
        for part in display['parts']:
            identity=display['displayId']+'/part/'+str(part['member'])
            additions.append(placement(identity,part['name'],display['interactionName']+'/'+display['displayName']+'/'+part['name'],
                part['mesh'],part['worldMatrix'],part['materials'],display['initiallyVisible'] and part['active'],behavior))
    assert len(all_environment)==166 and sum(p['kind']=='foliage' for p in additions)==6
    assert len(additions)==73
    used={p['mesh'] for p in additions}
    materials=json.loads((SOURCE/'Materials.json').read_text())
    leaves=next(m for m in materials if m['source']['name']=='Leaves')
    material_props={p['path']:p['value'] for p in leaves['properties']}
    colors={}
    for i in range(material_props['m_SavedProperties.m_Colors.Array.size']):
        prefix=f'm_SavedProperties.m_Colors.Array.data[{i}]'
        colors[material_props[prefix+'.first']]=material_props[prefix+'.second']
    # Pinned local inputs were staged by the task author; no source checkout/network.
    inputs=[]
    definition={'schema':1,'sourceRevision':index['sourceRevision'],'targetMap':base['targetMap'],
        'baseDefinitionSha256':hashlib.sha256((ROOT/'Docs/Fixtures/EntryTerrainDefinition.json').read_bytes()).hexdigest(),
        'sceneSha256':index['artifacts']['Scenes/EntryGate.json']['sha256'],
        'captureToolSha256':'captured-input-supplied-see-PROVENANCE',
        'captureSha256':hashlib.sha256((capture/'ItemDisplays.json').read_bytes()).hexdigest(),
        'environment':all_environment,'additions':additions,'excluded':excluded,'meshes':{k:meshes[k] for k in sorted(used)},
        'leafProperties':{k:colors[k] for k in ['_ColorDark','_ColorLight','_Line','_Scales','_Strengths']},
        'sourceInputs':inputs,'landmarks':[
            {'name':'Foliage cards','match':'Tree','count':6},{'name':'No-entry sign','match':'NoEntry','count':3},
            {'name':'Utility pole assemblies','match':'UtilityPole','count':19},
            {'name':'Original chair display','match':'SecurityKisokInteraction/ChairDisplay','count':4},
            {'name':'Original rock display','match':'RockInteraction/RockDisplay','count':1}]}
    for landmark in definition['landmarks']:
        assert sum(landmark['match'] in p['path'] for p in additions)==landmark['count'],landmark
    write(ROOT/'Docs/Fixtures/EntryCompositionDefinition.json',definition)
    write(ROOT/'Content/Data/Tests/EntryComposition.json',{'schema':1,'environment':all_environment,
        'additions':[{k:p[k] for k in ['sourceId','asset','pivot','initiallyVisible','display','kind']} for p in additions]})
    print('Prepared',len(additions),'additions including six foliage cards and nine item-display mesh parts.')
    return definition


if __name__=='__main__': compile_definition()
