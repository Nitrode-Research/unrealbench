"""Isolated task-owned verifier. No solver/judge calls or shared runner mutations."""
import argparse, collections, hashlib, json, os, pathlib, re, shutil, subprocess, sys

def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()

ENGINE_INI_CANONICAL_SHA256='b32bc7de368bae6d4335cc86ec68e384b63e4a7e5cd4a0b1829f6c70f3d7cbf8'
def protected_path_matches(path,rel,expected):
    if sha(path)==expected:return True
    if rel!='Config/DefaultEngine.ini':return False
    text=path.read_text(encoding='utf-8-sig').replace('\r\n','\n').replace('\r','\n')
    text=re.sub(r'(?ms)^\[/Script/AndroidFileServerEditor\.AndroidFileServerRuntimeSettings\]\n.*?(?=^\[|\Z)','',text)
    text='\n'.join(line.rstrip() for line in text.splitlines())
    text=re.sub(r'\n{3,}','\n\n',text).strip()+'\n'
    return hashlib.sha256(text.encode()).hexdigest()==ENGINE_INI_CANONICAL_SHA256

def assess_lane(data, exit_code, names, native_report):
    records=re.findall(r'Test Completed\. Result=\{([^}]+)\}.*?Path=\{([^}]+)\}',data)
    counts=collections.Counter(p for _,p in records)
    reasons=[]
    if not names or len(names)!=len(set(names)):
        reasons.append('invalid_expected_manifest')
    if set(counts)!=set(names) or any(v!=1 for v in counts.values()):
        reasons.append('missing_duplicate_or_unexpected_log_records')
    if any(state not in {'Success','Fail'} for state,_ in records):
        reasons.append('nonterminal_log_records')
    terminal=re.findall(r'LogAutomationCommandLine:.*?\*\*\*\* TEST COMPLETE\. EXIT CODE: (-?\d+) \*\*\*\*',data)
    failed=any(state=='Fail' for state,_ in records)
    expected_exit=-1 if failed else 0
    # UE Automation Quit requests uint8(-1): 255 on Windows and Unix.
    if terminal!=[str(expected_exit)] or exit_code!=(255 if failed else 0):
        reasons.append('missing_or_abnormal_automation_exit')
    if re.search(r'Fatal error:|Unhandled Exception|Assertion failed:|=== Critical error:',data):
        reasons.append('crash_marker')
    try:
        native=json.loads(native_report.read_text(encoding='utf-8-sig'))
        native_records=[(t['state'],t['fullTestPath']) for t in native['tests']]
        if collections.Counter(native_records)!=collections.Counter(records):
            reasons.append('native_report_log_mismatch')
        if native.get('notRun',0) or native.get('inProcess',0):
            reasons.append('nonterminal_native_report')
    except (OSError,ValueError,KeyError,TypeError):
        reasons.append('missing_or_invalid_native_report')
    return {'complete':not reasons,'exit_code':exit_code,'expected':len(names),
            'records':records,'incomplete_reasons':reasons,'native_report':str(native_report)}

def main():
    ap=argparse.ArgumentParser()
    ap.add_argument('--project',type=pathlib.Path,required=True)
    ap.add_argument('--engine',type=pathlib.Path,required=True)
    ap.add_argument('--output',type=pathlib.Path,required=True)
    ap.add_argument('--lane',choices=['all','headless','rendered'],default='all')
    ap.add_argument('--render-arg',action='append',default=[])
    args=ap.parse_args(); package=pathlib.Path(__file__).resolve().parent
    if args.output.resolve().is_relative_to(args.project.resolve()): ap.error('Output must be outside the candidate project.')
    if any(x not in {'-dx12','-sm6','-AllowSoftwareRendering','-vulkan'} for x in args.render_arg): ap.error('Unsupported rendered-platform flag.')
    if args.output.exists(): ap.error('Use a fresh output directory; old logs cannot establish validity.')
    args.output.mkdir(parents=True)
    report={'reward':None,'strict_success':None,'reward_valid':False,'status':'initializing','lanes':{}}
    def finish(status,code):
        report['status']=status
        (args.output/'verification.json').write_text(json.dumps(report,indent=2))
        return code
    contract=json.loads((package/'protection-manifest.json').read_text())
    changed=[p for p,h in contract['protected'].items() if not (args.project/p).is_file() or not protected_path_matches(args.project/p,p,h)]
    known=set(contract['protected'])|set(contract['editable'])
    report['candidate_hashes']={p:sha(args.project/p) for p in sorted(known) if (args.project/p).is_file()}
    relevant=lambda p: p.suffix.lower() in {'.cpp','.h','.cs','.uproject','.ini','.ush','.usf','.uasset','.umap'}
    extra=[p.relative_to(args.project).as_posix() for p in args.project.rglob('*') if p.is_file() and relevant(p) and p.relative_to(args.project).parts[0] in {'Source','Content','Shaders','Config','Plugins'} and p.relative_to(args.project).as_posix() not in known]
    if changed or extra:
        report['integrity_violations']={'changed_protected':changed,'extra_project_files':extra}
        return finish('invalid_submission_boundary',2)
    project=args.output/'project'
    shutil.copytree(args.project,project,ignore=shutil.ignore_patterns('Binaries','Intermediate','Saved','DerivedDataCache','.git'))
    shutil.copytree(package/'evaluator'/'RTSTests',project/'Source'/'RTSTests')
    shutil.copytree(package/'tests',project/'Source'/'RTSTests'/'Private'/'Injected')
    uproject=project/'RTS.uproject'; desc=json.loads(uproject.read_text())
    desc['Modules'].append({'Name':'RTSTests','Type':'Editor','LoadingPhase':'PostEngineInit'})
    uproject.write_text(json.dumps(desc,indent=2))
    target=project/'Source'/'RTSEditor.Target.cs'
    target.write_text(target.read_text().replace('new string[] { "RTS" }','new string[] { "RTS", "RTSTests" }'))
    platform='Win64' if os.name=='nt' else 'Linux'
    build=args.engine/'Engine'/'Build'/'BatchFiles'/('Build.bat' if os.name=='nt' else 'Linux/Build.sh')
    editor=args.engine/'Engine'/'Binaries'/platform/('UnrealEditor-Cmd.exe' if os.name=='nt' else 'UnrealEditor')
    try:
        # Multiple Harbor containers share the host. Keep each UBT invocation within
        # a predictable memory envelope and avoid a premature one-hour inner timeout.
        with (args.output/'build.log').open('w') as log:
            built=subprocess.run([str(build),'RTSEditor',platform,'Development',str(uproject.resolve()),'-WaitMutex','-NoUBA','-MaxParallelActions=4'],stdout=log,stderr=subprocess.STDOUT,timeout=9600)
        if built.returncode: return finish('compile_failed_unattributed',2)
        expected=json.loads((package/'expected-tests.json').read_text())
        all_records=[]
        for lane,names in expected.items():
            if args.lane!='all' and lane!=args.lane: continue
            logfile=args.output/(lane+'.log')
            native=args.output/(lane+'-report')
            cmd=[str(editor),str(uproject.resolve()),'-unattended','-nop4','-nosplash','-nosound',f'-abslog={logfile.resolve()}',f'-ExecCmds=Automation RunTests {contract["prefix"]}.{lane.title()};Quit',f'-ReportExportPath={native.resolve()}']
            cmd+=['-NullRHI'] if lane=='headless' else ['-RenderOffScreen',*args.render_arg]
            with (args.output/(lane+'-console.log')).open('w') as log:
                proc=subprocess.run(cmd,stdout=log,stderr=subprocess.STDOUT,timeout=3600)
            data=logfile.read_text(errors='replace')
            result=assess_lane(data,proc.returncode,names,native/'index.json')
            report['lanes'][lane]=result
            all_records+=result['records']
            if not result['complete']: return finish('incomplete_verification',2)
        if args.lane!='all': return finish('diagnostic_subset_only',0)
        passed=sum(state=='Success' for state,_ in all_records)
        report.update(reward_valid=True,strict_success=passed==len(all_records),reward=passed/len(all_records),diagnostic_group_fraction=passed/len(all_records))
        return finish('complete',0 if report['strict_success'] else 1)
    except (OSError,subprocess.TimeoutExpired) as exc:
        report['error']=str(exc)
        return finish('environment_or_timeout',2)

if __name__=='__main__': sys.exit(main())
