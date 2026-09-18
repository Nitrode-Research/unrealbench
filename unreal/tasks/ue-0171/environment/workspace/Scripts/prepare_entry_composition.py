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
    raise NotImplementedError("Restore scene definition compilation from supplied local inputs")


if __name__=='__main__': compile_definition()
