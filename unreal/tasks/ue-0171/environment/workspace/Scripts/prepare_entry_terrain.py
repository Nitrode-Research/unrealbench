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
    raise NotImplementedError("Restore scene definition compilation from supplied local inputs")


if __name__=='__main__': compile_definition()
