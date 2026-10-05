#!/usr/bin/env python3
"""Verify Ghidra outputs and make address-preserving focus excerpts."""
import csv
import hashlib
import json
import re
from collections import Counter
from pathlib import Path
ROOT=Path(__file__).resolve().parents[2]
OUT=ROOT/'analysis/decompiled'
FOCUS={
 '00401010':'startup',
 '00401ab6':'resource_pool_initialization',
 '004092b1':'video_loading_candidate',
 '00433f2e':'window_initialization',
 '004340b3':'directdraw_initialization',
 '00434247':'display_mode_candidate',
 '00434a1c':'pixel_format_check',
 '0043a9c8':'render_target_dimensions',
 '004425ac':'file_open_wrapper',
 '004426d1':'file_read_wrapper',
 '00444816':'sprite_blit_candidate',
}
def rows(name):
 with (OUT/name).open() as f:return list(csv.DictReader(f,delimiter='\t'))
def main():
 s=json.loads((OUT/'summary.json').read_text())
 digest=hashlib.sha256((ROOT/'original/app/WET.EXE').read_bytes()).hexdigest()
 assert s['sha256']==digest, 'Ghidra source digest differs from original binary'
 functions=rows('functions.tsv');assert len(functions)==s['functions']
 failed=[f for f in functions if f['decompiled']=='false' and f['external']=='false']
 assert len(failed)==s['failed'];assert not s['cancelled'];assert s['decompiled']>0
 c=(OUT/'WET.analysis.c').read_text()
 pieces=re.split(r'(?=^/\* [0-9a-f]{8} \|)',c,flags=re.M)
 focus_dir=OUT/'focus';focus_dir.mkdir(exist_ok=True)
 selected=[]
 for piece in pieces:
  match=re.match(r'/\* ([0-9a-f]{8}) \|',piece)
  if match and match[1] in FOCUS:
   name=match[1]+'_'+FOCUS[match[1]]+'.analysis.c'
   (focus_dir/name).write_text('/* ANALYSIS EXCERPT. Inferred types and calling conventions require manual validation. */\n'+piece)
   selected.append({'address':match[1],'candidate_role':FOCUS[match[1]],'file':'focus/'+name})
 constants=rows('resolution-candidates.tsv')
 report={**s,'thunks':sum(f['thunk']=='true' for f in functions),'incomplete_functions':failed,
  'constant_counts':dict(Counter(f['scalar_decimal'] for f in constants)),
  'functions_with_640_or_480':len({f['function'] for f in constants if f['scalar_decimal'] in {'640','480'}}),
  'focus':selected,
  'limitations':['Default Windows compiler specification is not a reconstruction of the original Watcom register ABI.','Decompile-completed status does not prove semantic equivalence, full code coverage, or compilability.','Indirect COM and register-based calls need manual typing and validation.']}
 (OUT/'focus-index.json').write_text(json.dumps(report,indent=2)+'\n')
 print(f"Verified source hash and {len(functions)} function rows; {len(selected)} focus excerpts; {len(failed)} incomplete functions.")
if __name__=='__main__':main()
