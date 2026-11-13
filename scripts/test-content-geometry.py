#!/usr/bin/env python3
"""Check the game-area geometry each window is measured into (content_geometry.h)."""
import os,pathlib,subprocess,tempfile
R=pathlib.Path(__file__).resolve().parents[1]
env=dict(os.environ,DEVELOPER_DIR=os.environ.get('DEVELOPER_DIR','/Applications/Xcode.app/Contents/Developer'))
with tempfile.TemporaryDirectory() as out:
    binary=pathlib.Path(out)/'content-geometry'
    subprocess.run(['xcrun','clang','-Wall','-Wextra','-Werror','tests/content_geometry_test.c','-lm','-o',str(binary)],cwd=R,env=env,check=True)
    print(subprocess.run([str(binary)],capture_output=True,text=True,check=True).stdout,end='')
