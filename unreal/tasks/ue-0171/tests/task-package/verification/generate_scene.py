"""Editor-side trusted invocation only; submitted builders do the actual work."""
from pathlib import Path
import runpy
import unreal

root=Path(unreal.Paths.project_dir()).resolve()
for name in ('build_entry_terrain.py','build_entry_composition.py'):
    runpy.run_path(str(root/'Scripts'/name),run_name='__main__')
unreal.log('TRIPLET_GENERATION_COMPLETE')
