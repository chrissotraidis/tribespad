#!/usr/bin/env python3
"""Merge shader recordings into the list the app builds at launch (halopad_metal.m, warm_shaders).

A recording is what the Mac engine writes with VENGEANCE_SHADER_RECORD=<file>: each pipeline the
game built, as "R <vertex length> <pixel length> <descriptor hex>", then both shader sources.
The list keeps each source and pipeline once, in the order first seen (menus, then maps):
"TribesPad shaders 1", "S <length>" and a source, "P <vertex> <pixel> <descriptor hex>".
It is made from the game's own shaders, so it stays in generated/ (never in the repository);
scripts/build-ipad-engine.py bundles it when present.

  shader_warmup.py OUT RECORDING..."""
import pathlib
import sys


def recordings(path):
    data = path.read_bytes()
    at = 0
    while at < len(data):
        nl = data.index(b'\n', at)
        head = data[at:nl].split()
        assert head[0] == b'R' and len(head) == 4, (path, at)
        vs, ps = int(head[1]), int(head[2])
        start = nl + 1
        yield data[start:start + vs], data[start + vs:start + vs + ps], head[3]
        assert data[start + vs + ps:start + vs + ps + 1] == b'\n', (path, at)
        at = start + vs + ps + 1


def main(out, inputs):
    sources, pipelines = {}, {}
    for path in inputs:
        for vs, ps, desc in recordings(pathlib.Path(path)):
            key = (sources.setdefault(vs, len(sources)), sources.setdefault(ps, len(sources)), desc)
            pipelines.setdefault(key, len(pipelines))
    body = [b'TribesPad shaders 1\n']
    for source in sources:
        body += [b'S %d\n' % len(source), source, b'\n']
    for vs, ps, desc in pipelines:
        body.append(b'P %d %d %s\n' % (vs, ps, desc))
    pathlib.Path(out).write_bytes(b''.join(body))
    print('%s: %d pipelines, %d shaders' % (out, len(pipelines), len(sources)))


if __name__ == '__main__':
    main(sys.argv[1], sys.argv[2:])
