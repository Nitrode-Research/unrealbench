"""Compile the E01 scene foundation from validated source data (stdlib only)."""
import hashlib
import json
import math
from pathlib import Path
import re
from scene_coordinates import IDENTITY, multiply, point, position

ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'SourceData/UnityScenes'


def write(path,data):
    path.parent.mkdir(parents=True,exist_ok=True)
    path.write_text(json.dumps(data,indent=2)+'\n',encoding='utf-8')


def compile_definition():
    scene=json.loads((SOURCE/'Scenes/EntryGate.json').read_text())
    index=json.loads((SOURCE/'index.json').read_text())
    assert hashlib.sha256((SOURCE/'Scenes/EntryGate.json').read_bytes()).hexdigest()==index['artifacts']['Scenes/EntryGate.json']['sha256']
    nodes=scene['nodes']; by_id={n['id']:n for n in nodes}; meshes={m['key']:m for m in index['meshes']}
    def component(node,kind): return next((c for c in node['components'] if c['type']==kind),None)
    def props(c): return {p['path']:p['value'] for p in c['properties']}
    def nav(node):
        while node:
            modifier=component(node,'Unity.AI.Navigation.NavMeshModifier')
            if modifier and modifier['enabled']:
                p=props(modifier)
                return not p['m_IgnoreFromBuild'],bool(p['m_OverrideArea'] and p['m_Area']==1)
            node=by_id.get(node['parent']['id']) if node['parent'] else None
        return False,False
    def shape_matrix(node,center,size):
        local=list(IDENTITY)
        for a in range(3): local[a*4+a]=size[a]; local[a*4+3]=center[a]
        return multiply(node['worldMatrix'],local)
    geometry=[]; boxes=[]; exclusions=[]; starts=[]; centres=[]; source_keys=set()
    for node in nodes:
        if not node['activeInHierarchy']: continue
        player=component(node,'Player.PlayerController')
        if player:
            p=props(player); matrix=node['worldMatrix']
            starts.append({'identity':int(p['Type']),'sourceId':node['id'],'position':position(node['worldPosition']),
                           'yaw':math.degrees(math.atan2(matrix[2],matrix[10]))})
        chain=component(node,'Player.ChainManager')
        if chain:
            p=props(chain); assert p['_length']==82
            centres=[position(p[f'_positions.Array.data[{i}]']) for i in reversed(range(82))]
        modifier=component(node,'Unity.AI.Navigation.NavMeshModifierVolume')
        if modifier and modifier['enabled']:
            p=props(modifier); assert p['m_Area']==1
            exclusions.append({'sourceId':node['id'],'name':node['name'],'matrix':shape_matrix(node,p['m_Center'],p['m_Size'])})
        if node['path'].startswith('0:StoryBundle') or node['layer'] not in (0,3): continue
        physical=[c for c in node['components'] if 'collider' in c and c['collider']['enabled'] and not c['collider']['isTrigger']]
        relevant,excluded=nav(node)
        for c in physical:
            shape=c['collider']
            if c['type']=='UnityEngine.BoxCollider':
                boxes.append({'sourceId':c['id'],'name':node['name'],'matrix':shape_matrix(node,shape['center'],shape['size']),
                              'navRelevant':relevant,'navExcluded':excluded})
            elif c['type']!='UnityEngine.MeshCollider':
                raise RuntimeError('E01 solid collider needs a policy: '+node['path']+' '+c['type'])
        renderer=component(node,'UnityEngine.MeshRenderer'); mesh_filter=component(node,'UnityEngine.MeshFilter')
        # Terrain shell, enclosure and solid obstacles. Decorative vegetation,
        # cables, distant props and interaction presentation belong to E02.
        visible=bool(renderer and renderer['renderer']['enabled'] and mesh_filter and (
            node['path'].startswith('1:World/0:LoadingDocsTerrain/') or
            '/Fence' in node['path'] or '/0:Fence' in node['path'] or '/Kiosk' in node['path'] or
            any(part.split(':',1)[-1].startswith(('FenceMesh','Pole','Kiosk','HighwayBarrier')) for part in node['path'].split('/'))))
        collision_meshes={c['collider']['mesh'] for c in physical if c['type']=='UnityEngine.MeshCollider'}
        keys=set(collision_meshes)
        if visible: keys.add(mesh_filter['mesh'])
        for key in sorted(keys):
            source_keys.add(key)
            matrix=node['worldMatrix']; linear=[matrix[r*4+c] for r in range(3) for c in range(3)]
            collision=key in collision_meshes
            ident=hashlib.sha256(json.dumps([key,linear,collision],separators=(',',':')).encode()).hexdigest()[:12]
            name=re.sub('[^A-Za-z0-9_]','_',meshes[key]['source']['name'])
            geometry.append({'sourceId':node['id'],'name':node['name'],'path':node['path'],'mesh':key,
                'asset':f'/Game/Task0170/EntryGate/SM_{name}_{ident}', 'matrix':matrix,'pivot':position(node['worldPosition']),
                'visible':visible and key==mesh_filter['mesh'],'collision':collision,'navRelevant':relevant and collision,
                'navExcluded':excluded,'ground':node['layer']==3,
                'materials':renderer['renderer']['materials'] if renderer else []})
    starts.sort(key=lambda p:p['identity']); assert len(starts)==2 and len(centres)==82
    definition={'schema':1,'sourceRevision':index['sourceRevision'],'sourceSceneSha256':index['artifacts']['Scenes/EntryGate.json']['sha256'],
                'targetMap':'/Game/Task0170/EntryGate_Source','players':starts,'chainCentres':centres,
                'geometry':geometry,'boxes':boxes,'exclusions':exclusions,
                'sourceMeshes':{k:meshes[k] for k in sorted(source_keys)},
                'policy':'E01 terrain/enclosure foundation; active prototype and legacy maps remain unchanged; full scene composition is pending E02.'}
    write(ROOT/'Docs/Fixtures/EntryTerrainDefinition.json',definition)
    write(ROOT/'Content/Data/Scenes/EntryGate.json',{k:definition[k] for k in ('schema','sourceRevision','players','chainCentres')})
    print(f'Prepared {len(geometry)} mesh placements, {len(boxes)} solid boxes, {len(exclusions)} navigation exclusions, two starts and 82 chain centres.')
    return definition


if __name__=='__main__': compile_definition()
