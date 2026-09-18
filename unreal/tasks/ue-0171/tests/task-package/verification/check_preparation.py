"""Independent task evaluator: stdlib checks against pinned captured scene records."""
import importlib.util
import json
import math
from pathlib import Path
import sys

HERE=Path(__file__).resolve().parent
NAMES=['Preparation.Coordinates','Preparation.TerrainInventory','Preparation.CollisionPolicy','Preparation.Composition','Preparation.AuthoredLayout']
def evaluate(project):
    project=Path(project); rows=[]
    def record(name, fn):
        try: fn(); rows.append({'name':name,'status':'passed'})
        except Exception as exc: rows.append({'name':name,'status':'failed','message':str(exc)})
    def read(rel):return json.loads((project/rel).read_text())
    expected_terrain=json.loads((HERE/'fixtures/EntryTerrainDefinition.json').read_text())
    expected_composition=json.loads((HERE/'fixtures/EntryCompositionDefinition.json').read_text())
    def coordinates():
        spec=importlib.util.spec_from_file_location('submitted_coordinates',project/'Scripts/scene_coordinates.py'); mod=importlib.util.module_from_spec(spec);spec.loader.exec_module(mod)
        assert mod.position([1,-2,3])==[300,100,-200]
        assert mod.direction([1,-2,3])==[3,1,-2]
        a=[-2,.3,0,7,0,3,.7,-4,.2,0,4,9,0,0,0,1]
        b=[1,0,.4,2,0,2,0,3,0,0,1,4,0,0,0,1]
        q=[.7,-1,3]
        expected=[sum(a[r*4+k]*sum(b[k*4+j]*(q+[1])[j] for j in range(4)) for k in range(4)) for r in range(3)]
        assert all(abs(x-y)<1e-9 for x,y in zip(mod.point(mod.multiply(a,b),q),expected))
        assert mod.baked_triangle([2,5,7],a)==[2,7,5]
        n=mod.normal(a,[0,1,0]); tangent=[a[0],a[4],a[8]]
        assert abs(sum(x*y for x,y in zip(n,tangent)))<1e-9
        assert abs(sum(x*x for x in n)-1)<1e-9
        try:mod.normal([0]*16,[0,1,0])
        except ValueError:pass
        else:raise AssertionError('Singular normal transform must be rejected')
    def terrain():
        actual=read('Docs/Fixtures/EntryTerrainDefinition.json')
        expected={x['sourceId']+'|'+x['mesh']:x for x in expected_terrain['geometry']}
        observed={x['sourceId']+'|'+x['mesh']:x for x in actual['geometry']}
        assert len(actual['geometry'])==len(expected)==102
        assert observed.keys()==expected.keys()
        for k,e in expected.items():
            for field in ('matrix','pivot','visible','collision','ground','navRelevant','navExcluded'):
                assert observed[k][field]==e[field],(k,field)
        assert len({x['asset'] for x in actual['geometry']})==37
        assert all(x['asset'].startswith('/Game/Task0170/') for x in actual['geometry'])
    def collision():
        actual=read('Docs/Fixtures/EntryTerrainDefinition.json')
        for group,count in [('boxes',24),('exclusions',8)]:
            assert len(actual[group])==count
            expected={x['sourceId']:x for x in expected_terrain[group]}
            observed={x['sourceId']:x for x in actual[group]}
            assert observed.keys()==expected.keys()
            for key,e in expected.items():
                a=observed[key];assert a.keys()==e.keys(),key
                for field,value in e.items():
                    if field=='matrix':
                        assert len(a[field])==len(value) and all(math.isclose(x,y,rel_tol=0,abs_tol=1e-12) for x,y in zip(a[field],value)),(key,field)
                    else:assert a[field]==value,(key,field)
    def composition():
        actual=read('Docs/Fixtures/EntryCompositionDefinition.json')
        assert len(actual['environment'])==166 and len(actual['additions'])==73
        expected={x['sourceId']:x for x in expected_composition['additions']}
        observed={x['sourceId']:x for x in actual['additions']}
        assert observed.keys()==expected.keys() and len(observed)==73
        for key,e in expected.items():
            for field in ('matrix','pivot','initiallyVisible','display','kind'):
                assert observed[key][field]==e[field],(key,field)
        assert sum(x['kind']=='foliage' for x in actual['additions'])==6
        assert sum(bool(x['display']) for x in actual['additions'])==9
    def layout():
        actual=read('Content/Data/Scenes/EntryGate.json')
        assert actual['players']==expected_terrain['players']
        assert actual['chainCentres']==expected_terrain['chainCentres']
        assert len(actual['chainCentres'])==82
    for name,fn in zip(NAMES,[coordinates,terrain,collision,composition,layout]):record(name,fn)
    return rows

if __name__=='__main__':
    rows=evaluate(sys.argv[1]);print(json.dumps(rows,indent=2));sys.exit(any(x['status']!='passed' for x in rows))
