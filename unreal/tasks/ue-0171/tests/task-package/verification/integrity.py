"""No source reference lookup: validate supplied counterpart functions by frozen hashes."""
import hashlib,re

def mask(text):
    pattern=r'//[^\n]*|/\*[\s\S]*?\*/|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\''
    return re.sub(pattern,lambda m:''.join('\n' if c=='\n' else ' ' for c in m[0]),text)

def function_hashes(text):
    masked=mask(text)
    pattern=re.compile(r'^[ \t]*(?P<return>(?:(?:const|static|inline|virtual)\s+)*[\w:]+(?:<[^;{}\n]+>)?\s*[*&]?)\s+(?P<name>[\w:]+)\s*\((?P<args>[^;{}]*?)\)\s*(?:const\s*)?(?:override\s*)?\{',re.M)
    hashes={};occupied=-1
    for m in pattern.finditer(masked):
        if m.start()<occupied:continue
        start=masked.index('{',m.start(),m.end());end=start+1;depth=1
        while depth:
            if end>=len(masked):raise ValueError('Unbalanced function body')
            depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
        occupied=end
        signature=' '.join(text[m.start():start].split())
        hashes[signature]=hashlib.sha256(text[start:end].encode()).hexdigest()
    constructors=re.compile(r'^[ \t]*(?P<class>\w+)::(?P<name>\w+)\([^;{}]*?\)\s*(?::[^{}]+)?\{',re.M)
    for m in constructors.finditer(masked):
        if m['class']!=m['name']:continue
        start=masked.index('{',m.start(),m.end());end=start+1;depth=1
        while depth:
            if end>=len(masked):raise ValueError('Unbalanced constructor body')
            depth+=(masked[end]=='{')-(masked[end]=='}');end+=1
        signature=' '.join(text[m.start():start].split())
        hashes[signature]=hashlib.sha256(text[start:end].encode()).hexdigest()
    return hashes
