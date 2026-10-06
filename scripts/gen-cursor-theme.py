#!/usr/bin/env python3
"""Generate/check immutable default native arrow theme glyphs."""
import argparse,json,pathlib
R=pathlib.Path(__file__).resolve().parents[1];ap=argparse.ArgumentParser();ap.add_argument('--check',action='store_true');a=ap.parse_args();p=json.loads((R/'config/runtime/cursor-theme.json').read_text());assert p['width']==p['height']==32 and p['hotspot']==[0,0] and p['system_id']==32512
rows=p['rows'];assert len(rows)==32 and all(len(row)==32 and set(row)<=set('.BW') for row in rows)
source='/* Generated from config/runtime/cursor-theme.json. */\nstatic const unsigned char tb_cursor_arrow[32][33]={\n'+',\n'.join(json.dumps(row) for row in rows)+'\n};\n';target=R/'port/core/cursor_theme.h'
if a.check:assert target.read_text()==source,'Regenerate cursor theme'
else:target.write_text(source)
