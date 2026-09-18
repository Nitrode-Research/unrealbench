"""Task-local clean generation, integrity, headless and rendered verification.

Never invokes a solver. Task 172 scores 44 preparation/headless checks plus one source-preservation check.
The rendered HUD check is excluded from reward and full success.
"""
from pathlib import Path
import argparse, hashlib, json, os, re, shutil, subprocess, sys, time
from check_preparation import evaluate
from integrity import function_hashes

PACKAGE=Path(__file__).resolve().parents[1]

def digest(path):return hashlib.sha256(path.read_bytes()).hexdigest()
ENGINE_INI_CANONICAL_SHA256='071c8928b8862dca5d09a9acf67ad689b4f4d1215efd1e9268d18265810bce28'
def protected_path_matches(path,rel,expected):
    if digest(path)==expected:return True
    if rel!='Config/DefaultEngine.ini':return False
    text=path.read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n')
    text=re.sub(r'(?ms)^\[/Script/AndroidFileServerEditor\.AndroidFileServerRuntimeSettings\]\n.*?(?=^\[|\Z)','',text)
    text='\n'.join(line.rstrip() for line in text.splitlines())
    text=re.sub(r'\n{3,}','\n\n',text).strip()+'\n'
    return hashlib.sha256(text.encode()).hexdigest()==ENGINE_INI_CANONICAL_SHA256
def snapshot(source,dest):
    excluded={'Binaries','Intermediate','Saved','DerivedDataCache','.git','__pycache__','TaskTripletVerification','Tests'}
    def ignore(directory,names):
        rel=Path(directory).relative_to(source).as_posix();omit=set(names)&excluded
        if rel=='Content':omit|={n for n in names if n.startswith('Task0170')}
        if rel=='Content/Data':omit.add('Tests')
        if rel=='Content/Data/Scenes':omit.add('EntryGate.json')
        if rel=='Docs':omit.add('Fixtures')
        return omit
    for path in source.rglob('*'):
        if path.is_symlink():raise ValueError('Submission contains a symlink')
    shutil.copytree(source,dest,ignore=ignore)

def protected_errors(project):
    manifest=json.loads((PACKAGE/'protected-paths.json').read_text())
    errors=['Protected input changed/missing: '+p for p,h in manifest.items() if not (project/p).is_file() or not protected_path_matches(project/p,p,h)]
    editable={r['file'] for r in json.loads((PACKAGE/'removal-manifest.json').read_text())['records']}
    for root in ('Source','Config','SourceData','Scripts'):
        for path in (project/root).rglob('*'):
            if not path.is_file():continue
            rel=path.relative_to(project).as_posix()
            # Candidate local tests are removed by snapshot; evaluator injection
            # is independently owned. New production bypass modules are not allowed.
            if '/Tests/' in rel or '/TaskTripletVerification/' in rel or '/__pycache__/' in rel:continue
            if rel not in manifest and rel not in editable:errors.append('Undeclared production/input file: '+rel)
    cache={}
    for rec in json.loads((PACKAGE/'protected-functions.json').read_text()):
        path=project/rec['file']
        if rec['file'] not in cache:cache[rec['file']]=function_hashes(path.read_text()) if path.is_file() else {}
        if cache[rec['file']].get(rec['signature'])!=rec['sha256']:errors.append('Supplied counterpart changed: '+rec['file']+' '+rec['signature'])
    return errors

def is_source_preservation_error(message):
    # Only production C++ source changes become scored failures. Immutable data,
    # scripts, build settings and execution validity still protect the evaluator.
    if message.startswith('Supplied counterpart changed: '):return True
    for prefix in ('Protected input changed/missing: ', 'Undeclared production/input file: '):
        if message.startswith(prefix):
            path=message[len(prefix):]
            return path.startswith('Source/') and Path(path).suffix.lower() in ('.cpp','.h')
    return False

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--project',type=Path,required=True)
    parser.add_argument('--output',type=Path,required=True)
    parser.add_argument('--engine',type=Path,default=Path(os.environ.get('UE_ENGINE_ROOT','C:/Program Files/Epic Games/UE_5.8')))
    parser.add_argument('--prepare-only',action='store_true')
    parser.add_argument('--headless-only',action='store_true')
    args=parser.parse_args();src=args.project.resolve();out=args.output.resolve()
    if out.exists() or out==src or src in out.parents:parser.error('Output must be a new directory outside the submission')
    if not (src/'foundations_ue.uproject').is_file():parser.error('Missing project')
    out.mkdir(parents=True);work=out/'project';task=json.loads((PACKAGE/'verification/checks.json').read_text())
    rows=[];errors=[];stages=[];submission_failures=[];blocked_stages=[]
    # ue-0172-valid-fixtures-v3
    report={'scoring_revision':task.get('scoring_revision'),'task':task['task'],'reward':None,'reward_valid':False,'strict_success':None,'diagnostic_fraction':None,'checks':rows,'infrastructure_errors':errors,'submission_failures':submission_failures,'blocked_stages':blocked_stages,'stages':stages}
    preservation={'name':'Task0172.Submission.SuppliedCodePreservation','status':'passed','entries':[]}
    rows.append(preservation)
    def check_preservation(project):
        for message in protected_errors(project):
            if is_source_preservation_error(message):
                preservation['status']='failed'
                if message not in preservation['entries']:preservation['entries'].append(message)
            elif message not in errors:errors.append(message)
    def save():
        report['passed']=sum(r['status']=='passed' for r in rows);report['failed']=sum(r['status']=='failed' for r in rows)
        (out/'report.json').write_text(json.dumps(report,indent=2)+'\n')
    def run(label,cmd,timeout=1800,engine_log=False):
        begin=time.monotonic();log=out/(label+'.log');code=None
        console=out/(label+'-console.log') if engine_log else log
        if engine_log:cmd=[*cmd,'-abslog='+str(log)]
        try:
            with console.open('w',encoding='utf-8') as stream:
                code=subprocess.run(cmd,cwd=work,stdout=stream,stderr=subprocess.STDOUT,timeout=timeout,env={**os.environ,'PYTHONDONTWRITEBYTECODE':'1'}).returncode
        except (OSError,subprocess.TimeoutExpired) as exc:errors.append(label+': '+str(exc))
        text=log.read_text(errors='replace') if log.exists() else ''
        if engine_log:
            if not log.is_file():errors.append(label+': missing native engine log')
            text+='\n'+(console.read_text(errors='replace') if console.exists() else '')
        if re.search(r'Fatal error:|Unhandled Exception:|Assertion failed:|Ensure condition failed:|INVALID_VERIFICATION',text,re.I):errors.append(label+': crash/ensure/incomplete fixture')
        stage={'name':label,'exit_code':code,'duration_seconds':round(time.monotonic()-begin,3),'log':log.name}
        if engine_log:stage['console_log']=console.name
        stages.append(stage);save()
        return code,text
    try:
        check_preservation(src)
        if errors:raise RuntimeError('Submission integrity failed before execution')
        snapshot(src,work)
        manifest={p.relative_to(work).as_posix():digest(p) for p in sorted(work.rglob('*')) if p.is_file()}
        (out/'input-sha256.json').write_text(json.dumps(manifest,indent=2)+'\n')
        for script in ('prepare_entry_terrain.py','prepare_entry_composition.py'):
            code,_=run(script,[sys.executable,str(work/'Scripts'/script)],120)
            if code!=0:errors.append(script+': preparation process failed')
        rows.extend(evaluate(work))
        failed_preparation=[row['name'] for row in rows if row['name'] in task['preparation'] and row['status']!='passed']
        if failed_preparation:submission_failures.append({'stage':'preparation','checks':failed_preparation})
        save()
        if args.prepare_only:
            report['diagnostic_only']=True;save();return 0 if not errors and all(r['status']=='passed' for r in rows) else 1
        editor=args.engine/'Engine/Binaries/Linux/UnrealEditor-Cmd';build=args.engine/'Engine/Build/BatchFiles/Linux/Build.sh'
        if not editor.is_file() or not build.is_file():raise RuntimeError('Pinned Linux editor/build tools unavailable')
        testdest=work/'Source/foundations_ue/TaskTripletVerification';testdest.mkdir()
        for file in (PACKAGE/'tests').glob('*.cpp'):shutil.copy2(file,testdest/file.name)
        # Bound per-container compiler parallelism and reserve most of the outer
        # four-hour verifier budget for a clean editor build.
        code,_=run('compile',[str(build),'foundations_ueEditor','Linux','Development','-project='+str(work/'foundations_ue.uproject'),'-WaitMutex','-NoHotReloadFromIDE','-gather','-NoUBA','-MaxParallelActions=4'],10800)
        if code!=0:raise RuntimeError('Compile incomplete; reward invalid (compiler evidence retained for attribution)')
        # The engine's unrelated Android deployment plugin writes a generated
        # token into DefaultEngine.ini during editor startup. Disable that plugin
        # only for this Windows verification process; retain exact config hashes.
        common=[str(editor),str(work/'foundations_ue.uproject'),'-unattended','-nopause','-nosplash','-NoSound','-DisablePlugins=AndroidFileServer']
        if not failed_preparation:
            code,log=run('generate',common+['-NullRHI','-ExecutePythonScript='+str(PACKAGE/'verification/generate_scene.py')],900,engine_log=True)
            if code!=0 or 'TRIPLET_GENERATION_COMPLETE' not in log:errors.append('Native scene generation incomplete')
        else:blocked_stages.append({'name':'generate','reason':'submitted_preparation_failed','blocking_checks':failed_preparation})
        # Evaluation data arrives only after submitted generation has finished.
        for fixture in (PACKAGE/'verification/fixtures').glob('*.json'):
            target=work/'Content/Data/Tests'/fixture.name;target.parent.mkdir(parents=True,exist_ok=True);shutil.copy2(fixture,target)
        target=work/'Docs/Fixtures';target.mkdir(parents=True,exist_ok=True)
        shutil.copy2(PACKAGE/'verification/fixtures/UnityRelations.json',target/'UnityRelations.json')
        for lane in ('headless','rendered'):
            if not task[lane] or (lane=='rendered' and args.headless_only):continue
            expected=task[lane];filter_name=task['prefix']+('.Headless' if lane=='headless' else '.Rendered')
            extra=['-NullRHI'] if lane=='headless' else ['-RenderOffScreen','-dx12','-sm6']
            native=out/lane
            code,log=run(lane,common+extra+['-ExecCmds=Automation RunTests '+filter_name+';Quit','-ReportExportPath='+str(native)],1800,engine_log=True)
            index=native/'index.json'
            if not index.is_file():errors.append(lane+': missing automation report');continue
            data=json.loads(index.read_text(encoding='utf-8-sig'));seen=[]
            for test in data.get('tests',[]):
                name=test.get('fullTestPath');state=test.get('state');seen.append(name)
                if state not in ('Success','Fail'):errors.append(lane+': incomplete '+str(name))
                rows.append({'name':name,'status':'passed' if state=='Success' else 'failed','entries':test.get('entries',[])})
            if sorted(seen)!=sorted(expected):errors.append(lane+': missing/duplicate/unexpected checks')
            # Automation Quit sets an explicit uint8 status: 0 success, 255 failure.
            # Generic -TestExit force-exits Unix with 1 even when every test passes.
            failed=any(test.get('state')=='Fail' for test in data.get('tests',[]))
            completion=set(re.findall(r'\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*',log))
            if completion!={str(-1 if failed else 0)}:errors.append(lane+': missing/conflicting automation completion marker')
            else:stages[-1]['automation_exit_code']=-1 if failed else 0
            if code not in (0,255):errors.append(lane+': abnormal termination')
            elif code!=(255 if failed else 0):errors.append(lane+': exit code disagrees with checks')
        check_preservation(work)
        expected=task['submission']+task['preparation']+task['headless']+task['rendered']
        if args.headless_only and task['rendered']:report['diagnostic_only']=True
        elif not errors and not submission_failures and not blocked_stages and sorted(r['name'] for r in rows)==sorted(expected):
            report['reward_valid']=True;report['strict_success']=all(r['status']=='passed' for r in rows)
            report['reward']=sum(r['status']=='passed' for r in rows)/len(expected);report['diagnostic_fraction']=report['reward']
        save();return 2 if not report['reward_valid'] else 0 if report['strict_success'] else 1
    except Exception as exc:errors.append(str(exc));save();return 2

if __name__=='__main__':sys.exit(main())
