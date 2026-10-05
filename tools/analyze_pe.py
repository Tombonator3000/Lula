#!/usr/bin/env python3
"""Read-only, dependency-free PE32 evidence export (never executes the target)."""
import argparse
import csv
import datetime
import hashlib
import json
import re
import shutil
import struct
import subprocess
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
KEYWORDS = re.compile(r'direct|ddraw|dsound|surface|palette|color|screen|init|\.(?:cml|col|vic|smk|bmp|pal|dat|ani)\b|file|load', re.I)
RESOURCE_TYPES = {1:'CURSOR',2:'BITMAP',3:'ICON',4:'MENU',5:'DIALOG',6:'STRING',7:'FONTDIR',8:'FONT',9:'ACCELERATOR',10:'RCDATA',11:'MESSAGETABLE',12:'GROUP_CURSOR',14:'GROUP_ICON',16:'VERSION',24:'MANIFEST'}

class PE:
    def __init__(self, path):
        self.path = path
        self.data = path.read_bytes()
        if self.data[:2] != b'MZ': raise ValueError('DOS MZ signature missing')
        self.pe = self.u32(0x3c)
        if self.data[self.pe:self.pe+4] != b'PE\0\0': raise ValueError('PE signature missing')
        self.machine,self.nsections,self.timestamp,_,_,self.optional_size,self.characteristics = struct.unpack_from('<HHIIIHH', self.data, self.pe+4)
        self.opt = self.pe + 24
        if self.u16(self.opt) != 0x10b: raise ValueError('Only PE32 is supported')
        self.image_base = self.u32(self.opt+28)
        self.sections = []
        for i in range(self.nsections):
            s = self.opt+self.optional_size+i*40
            name,vsize,rva,rsize,offset = struct.unpack_from('<8sIIII', self.data, s)
            self.sections.append({'name':name.rstrip(b'\0').decode('ascii','replace'),'virtual_size':vsize,'rva':rva,'raw_size':rsize,'file_offset':offset,'characteristics':self.u32(s+36)})
        self.directories = [(self.u32(self.opt+96+i*8),self.u32(self.opt+100+i*8)) for i in range(min(16,self.u32(self.opt+92)))]
    def u16(self,o): return struct.unpack_from('<H',self.data,o)[0]
    def u32(self,o): return struct.unpack_from('<I',self.data,o)[0]
    def offset(self,rva):
        if rva < self.u32(self.opt+60): return rva
        for s in self.sections:
            if s['rva'] <= rva < s['rva']+s['raw_size']: return s['file_offset']+rva-s['rva']
        raise ValueError('RVA is not backed by file bytes: '+hex(rva))
    def rva(self,offset):
        for s in self.sections:
            if s['file_offset'] <= offset < s['file_offset']+s['raw_size']: return s['rva']+offset-s['file_offset']
        return offset if offset < self.u32(self.opt+60) else None
    def z(self,offset): return self.data[offset:self.data.index(b'\0',offset)].decode('cp1252','replace')
    def imports(self):
        result=[]
        if len(self.directories)<2 or not self.directories[1][0]: return result
        off=self.offset(self.directories[1][0])
        while True:
            original,timestamp,forwarder,name_rva,iat=struct.unpack_from('<IIIII',self.data,off)
            if not any((original,timestamp,forwarder,name_rva,iat)): break
            dll=self.z(self.offset(name_rva)); thunk=self.offset(original or iat); i=0
            while self.u32(thunk+i*4):
                value=self.u32(thunk+i*4)
                if value & 0x80000000: name=None; ordinal=value&0xffff; hint=None
                else: name=self.z(self.offset(value)+2); ordinal=None; hint=self.u16(self.offset(value))
                result.append({'dll':dll,'name':name,'ordinal':ordinal,'hint':hint,'iat_rva':iat+i*4,'iat_va':self.image_base+iat+i*4})
                i+=1
            off+=20
        return result
    def resources(self):
        result=[]
        if len(self.directories)<3 or not self.directories[2][0]: return result
        base=self.offset(self.directories[2][0]); visited=set()
        def walk(relative,path):
            if relative in visited or len(path)>4: raise ValueError('Invalid or cyclic resource directory')
            visited.add(relative)
            off=base+relative; n=self.u16(off+12)+self.u16(off+14)
            for i in range(n):
                name,value=struct.unpack_from('<II',self.data,off+16+i*8)
                if name&0x80000000:
                    p=base+(name&0x7fffffff); count=self.u16(p)
                    ident=self.data[p+2:p+2+count*2].decode('utf-16-le','replace')
                else: ident=name
                newpath=path+[ident]
                if value&0x80000000: walk(value&0x7fffffff,newpath)
                else:
                    rva,size,codepage,_=struct.unpack_from('<IIII',self.data,base+value)
                    payload=self.data[self.offset(rva):self.offset(rva)+size]
                    result.append({'path':newpath,'type':RESOURCE_TYPES.get(newpath[0],str(newpath[0])),'rva':rva,'va':self.image_base+rva,'file_offset':self.offset(rva),'size':size,'codepage':codepage,'sha256':hashlib.sha256(payload).hexdigest()})
        walk(0,[])
        return result
    def strings(self):
        rows=[]
        for m in re.finditer(rb'[\x20-\x7e\x80-\xff]{5,}',self.data):
            s=m.group().decode('cp1252','replace'); rva=self.rva(m.start())
            rows.append({'offset':m.start(),'rva':rva,'va':self.image_base+rva if rva is not None else None,'encoding':'cp1252','text':s})
        for m in re.finditer(rb'(?:[\x20-\x7e]\x00){5,}',self.data):
            s=m.group().decode('utf-16-le','replace'); rva=self.rva(m.start())
            rows.append({'offset':m.start(),'rva':rva,'va':self.image_base+rva if rva is not None else None,'encoding':'utf-16-le','text':s})
        return sorted(rows,key=lambda r:r['offset'])

def tsv(path,rows,fields):
    with path.open('w',encoding='utf-8',newline='') as f:
        w=csv.DictWriter(f,fields,delimiter='\t'); w.writeheader(); w.writerows(rows)

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('binary',nargs='?',type=Path,default=ROOT/'original/app/WET.EXE')
    parser.add_argument('--output',type=Path,default=ROOT/'analysis/binary')
    args=parser.parse_args(); args.output.mkdir(parents=True,exist_ok=True)
    pe=PE(args.binary); imports=pe.imports(); resources=pe.resources(); strings=pe.strings()
    report={'input':str(args.binary.resolve().relative_to(ROOT)) if args.binary.resolve().is_relative_to(ROOT) else args.binary.name,'sha256':hashlib.sha256(pe.data).hexdigest(),'size':len(pe.data),'machine':hex(pe.machine),'format':'PE32','linker_version':f'{pe.data[pe.opt+2]}.{pe.data[pe.opt+3]}','timestamp_utc':datetime.datetime.fromtimestamp(pe.timestamp,datetime.timezone.utc).isoformat(),'image_base':hex(pe.image_base),'entry_rva':hex(pe.u32(pe.opt+16)),'entry_va':hex(pe.image_base+pe.u32(pe.opt+16)),'subsystem':pe.u16(pe.opt+68),'sections':pe.sections,'imports':imports,'resources':resources,'strings_count':len(strings),'limitations':['PE timestamp and linker version are metadata, not proof of authorship or compiler.','Strings in executable sections may be coincidental byte sequences.','Static disassembly and Ghidra analysis do not recover original source code.']}
    (args.output/'pe-report.json').write_text(json.dumps(report,indent=2,ensure_ascii=False)+'\n',encoding='utf-8')
    tsv(args.output/'imports.tsv',imports,['dll','name','ordinal','hint','iat_rva','iat_va'])
    tsv(args.output/'strings.tsv',strings,['offset','rva','va','encoding','text'])
    tsv(args.output/'interesting-strings.tsv',[r for r in strings if KEYWORDS.search(r['text'])],['offset','rva','va','encoding','text'])
    for name,flags in [('headers.txt',['-p']),('disassembly.asm',['-d','-M','intel'])]:
        if shutil.which('objdump'):
            with (args.output/name).open('w',encoding='utf-8') as f:
                subprocess.run(['objdump',*flags,str(args.binary)],stdout=f,check=True)
    print(f"Read-only analysis: {len(imports)} imports, {len(resources)} resources, {len(strings)} strings; SHA-256 {report['sha256']}")

if __name__=='__main__': main()
