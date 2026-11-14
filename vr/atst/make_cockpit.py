#!/usr/bin/env python3
"""
OpenJK VR - the AT-ST cockpit as the game draws it, from cockpit.json (the
AT-ST Cockpit Test study's own shapes: simple ones made by a script, nothing
from the films or other games, none of the game's files).

  python3 make_cockpit.py <out dir>          cockpit.md3 and yoke.md3 there
  python3 make_cockpit.py --c <file.h>       the same, for the renderer to
                                             build in (rd-vanilla/tr_vratst.cpp:
                                             it's there on every server, a pure
                                             one too, with nothing to download)

The models:
  cockpit.md3   the cabin round the pilot - its walls, seats, panels, lights,
                and the plain skin round it with the visors over the viewports
                (in your own view the game's AT-ST isn't drawn: the cockpit's
                in its head) - but not the yoke
  yoke.md3      the pilot's yoke, about its pivot (the game tips and turns it)
  hand.md3      a gloved fist, round its middle: your hands in the cockpit (the
                game draws no body round you in it) - a box with its edges
                taken off, made here, of the study's sizes for a hand
Game axes (x forward, y left, z up), the study's units (34 to the metre); the
cockpit's origin is the study's: the cabin floor, at the back of it. Tags:
  cockpit.md3
    tag_eye       the pilot's eyes, sitting up, looking ahead
    tag_yoke      the yoke's pivot (it tips forward about its y axis, turns
                  about its z)
    tag_origin    where the game's AT-ST has its origin, standing - the
                  cockpit fitted to its head (measured from its model:
                  ATST_ORIGIN below)
    tag_thoracic  where the bone its head turns on ("thoracic": its neck)
                  pivots, standing (ATST_THORACIC below): the head's stride,
                  from how that bone rises and falls - the head goes with it
                  (20 above it), but for how its driver's look tips it
  yoke.md3
    tag_lgrip     the middle of the left grip, as a hand holds it
    tag_rgrip     the right grip's
Each material is a surface (split where it has more corners than the
renderer takes, SHADER_MAX_VERTEXES); its shader is models/vr/atst/<material>.
Faces are drawn from both sides (the study's are): the shaders don't cull.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
"""
import base64, json, math, os, struct, sys, zlib

HERE = os.path.dirname(os.path.abspath(__file__))
MAX_VERTS = 999					# a surface's corners: fewer than the renderer's SHADER_MAX_VERTEXES (1000)
MAX_TRIS = 1999					# and its triangles: fewer than SHADER_MAX_INDEXES / 3
REPEAT = {'floor': 3.0}			# the study's texture repeats (floorTex.repeat)
XYZ_SCALE = 64.0				# MD3_XYZ_SCALE: 1/64 of a unit

# Measured from the game's AT-ST (models/players/atst/model.glm and atst.gla,
# the game's own files: not in this source - only these measurements), the
# study's cockpit fitted to its head as it stands (BOTH_STAND1): the study's
# floor is 197 above the ground, and so 173 above the AT-ST's origin (24
# above the ground: its box's bottom, DEFAULT_MINS_2); the bone its head
# turns on, "thoracic", pivots 18.86 ahead of the origin, 150.15 above it.
ATST_ORIGIN = (0.0, 0.0, -173.0)
ATST_THORACIC = (18.86, 0.0, -22.85)
YOKE_GRIP = (-0.5, 5.6, 4.6)	# the left grip's middle, from the yoke's pivot (the right's: y the other way)
FIST = (1.7, 1.45, 1.6)			# a fist's half sizes: along what it holds (its x), across it, up from it (some 10 by 9 by 9 cm)
FIST_EDGE = 0.55				# ...its edges taken off this far


def game(v):
	"""three.js axes (x across, y up, z back) to the game's (x forward, y left, z up)"""
	return (-v[2], -v[0], v[1])


def normal_code(n):
	"""an MD3 normal: lat (around z, from x) in its high byte, lng (down from z) in its low - 256 to a turn (tr_surface.cpp)"""
	x, y, z = n
	ln = math.sqrt(x * x + y * y + z * z) or 1.0
	x, y, z = x / ln, y / ln, z / ln
	lng = int(round(math.acos(max(-1.0, min(1.0, z))) * 256.0 / (2.0 * math.pi))) & 0xff
	lat = int(round(math.atan2(y, x) * 256.0 / (2.0 * math.pi))) & 0xff
	return (lat << 8) | lng


def decode(b64, fmt, size):
	raw = base64.b64decode(b64)
	return [struct.unpack_from(fmt, raw, i) for i in range(0, len(raw), size)]


def load(path):
	"""(mover, material) -> triangles ((pos, nrm, st) x3); a mover's own groups are about its pivot already"""
	d = json.load(open(path))
	mats = {}
	for g in d['groups']:
		pos = decode(g['pos'], '<3f', 12)
		nrm = decode(g['nrm'], '<3b', 3)
		uv = decode(g['uv'], '<2f', 8)
		rep = REPEAT.get(g['mat'], 1.0)
		tris = mats.setdefault((g['mover'], g['mat']), [])
		for t in range(g['count']):
			corners = []
			for k in range(3):
				i = t * 3 + k
				p = game(pos[i])
				n = game(tuple(c / 127.0 for c in nrm[i]))
				st = (uv[i][0] * rep, 1.0 - uv[i][1] * rep)	# (the image's top is t 0)
				corners.append((p, n, st))
			tris.append(corners)
	return d, mats


def surfaces(tris):
	"""a material's triangles as surfaces of shared corners, each within the renderer's limits"""
	out = []
	verts, index, faces = [], {}, []
	for tri in tris:
		keys = []
		for p, n, st in tri:
			q = tuple(int(round(p[a] * XYZ_SCALE)) for a in range(3))
			assert all(-32768 <= c <= 32767 for c in q), q
			keys.append((q, normal_code(n), (round(st[0], 5), round(st[1], 5))))
		new = sum(1 for k in set(keys) if k not in index)
		if len(verts) + new > MAX_VERTS or len(faces) + 1 > MAX_TRIS:
			out.append((verts, faces))
			verts, index, faces = [], {}, []
		face = []
		for k in keys:
			if k not in index:
				index[k] = len(verts)
				verts.append(k)
			face.append(index[k])
		faces.append(face)
	if faces:
		out.append((verts, faces))
	return out


IDENT = ((1.0, 0.0, 0.0), (0.0, 1.0, 0.0), (0.0, 0.0, 1.0))


def fist():
	"""a box with its edges and corners taken off (FIST, FIST_EDGE): its triangles ((pos, nrm, st) x3), facing out"""
	h, e = FIST, FIST_EDGE
	tris = []

	def tri(a, b, c, n):
		# wound to face out along n (both ways drawn all the same: the shaders don't cull)
		ab = [b[i] - a[i] for i in range(3)]
		ac = [c[i] - a[i] for i in range(3)]
		cr = (ab[1] * ac[2] - ab[2] * ac[1], ab[2] * ac[0] - ab[0] * ac[2], ab[0] * ac[1] - ab[1] * ac[0])
		if sum(cr[i] * n[i] for i in range(3)) < 0:
			b, c = c, b
		ln = math.sqrt(sum(c2 * c2 for c2 in n))
		nn = tuple(c2 / ln for c2 in n)
		tris.append([(a, nn, (0.5, 0.5)), (b, nn, (0.5, 0.5)), (c, nn, (0.5, 0.5))])

	def quad(a, b, c, d, n):
		tri(a, b, c, n)
		tri(a, c, d, n)

	def pt(axis, s, u, v):
		"""a point on the face s * axis, at (u, v) along the other two axes in order"""
		p = [0.0, 0.0, 0.0]
		o = [i for i in range(3) if i != axis]
		p[axis] = s * h[axis]
		p[o[0]] = u
		p[o[1]] = v
		return tuple(p)

	for axis in range(3):
		o = [i for i in range(3) if i != axis]
		for s in (-1.0, 1.0):
			n = [0.0, 0.0, 0.0]
			n[axis] = s
			u, v = h[o[0]] - e, h[o[1]] - e
			quad(pt(axis, s, -u, -v), pt(axis, s, u, -v), pt(axis, s, u, v), pt(axis, s, -u, v), n)
	for a in range(3):
		for b in range(a + 1, 3):
			c = 3 - a - b
			for sa in (-1.0, 1.0):
				for sb in (-1.0, 1.0):
					def p(on, along):
						q = [0.0, 0.0, 0.0]
						q[a] = sa * (h[a] if on == a else h[a] - e)
						q[b] = sb * (h[b] if on == b else h[b] - e)
						q[c] = along
						return tuple(q)
					n = [0.0, 0.0, 0.0]
					n[a], n[b] = sa, sb
					quad(p(a, -(h[c] - e)), p(a, h[c] - e), p(b, h[c] - e), p(b, -(h[c] - e)), n)
	for sx in (-1.0, 1.0):
		for sy in (-1.0, 1.0):
			for sz in (-1.0, 1.0):
				s = (sx, sy, sz)
				corner = []
				for axis in range(3):
					q = [s[i] * (h[i] - e) for i in range(3)]
					q[axis] = s[axis] * h[axis]
					corner.append(tuple(q))
				tri(corner[0], corner[1], corner[2], s)
	return tris


def md3(name, parts, tags):
	"""parts: (shader, [(verts, faces)]); tags: (name, origin) - identity axes; one frame. The file's bytes."""
	surfs = []
	lo, hi = [1e9] * 3, [-1e9] * 3
	for shader, chunks in parts:
		for n, (verts, faces) in enumerate(chunks):
			surfs.append(('%s_%d' % (shader.split('/')[-1], n), shader, verts, faces))
			for q, _, _ in verts:
				for a in range(3):
					lo[a] = min(lo[a], q[a] / XYZ_SCALE)
					hi[a] = max(hi[a], q[a] / XYZ_SCALE)
	radius = math.sqrt(sum(max(abs(lo[a]), abs(hi[a])) ** 2 for a in range(3)))
	head_size = struct.calcsize('<4si64s9i')	# md3Header_t: 108
	frame = struct.pack('<3f3f3ff16s', *lo, *hi, 0.0, 0.0, 0.0, radius, b'cockpit')	# md3Frame_t: 56
	tag_blob = b''.join(struct.pack('<64s3f9f', t[0].encode(), *t[1], *[c for row in IDENT for c in row]) for t in tags)	# md3Tag_t: 112
	surf_blobs = []
	for sname, shader, verts, faces in surfs:
		ofs_shaders = struct.calcsize('<4s64s10i')	# md3Surface_t: 108
		ofs_tris = ofs_shaders + struct.calcsize('<64si')	# md3Shader_t: 68
		ofs_st = ofs_tris + 12 * len(faces)
		ofs_xyz = ofs_st + 8 * len(verts)
		ofs_end = ofs_xyz + 8 * len(verts)
		blob = struct.pack('<4s64s10i', b'IDP3', sname.encode()[:63], 0, 1, 1, len(verts), len(faces), ofs_tris, ofs_shaders, ofs_st,
			ofs_xyz, ofs_end)
		blob += struct.pack('<64si', shader.encode(), 0)
		blob += b''.join(struct.pack('<3i', *f) for f in faces)
		blob += b''.join(struct.pack('<2f', *v[2]) for v in verts)
		blob += b''.join(struct.pack('<3hH', *v[0], v[1]) for v in verts)
		assert len(blob) == ofs_end
		surf_blobs.append(blob)
	ofs_frames = head_size
	ofs_tags = ofs_frames + len(frame)
	ofs_surfs = ofs_tags + len(tag_blob)
	ofs_end = ofs_surfs + sum(len(b) for b in surf_blobs)
	head = struct.pack('<4si64s9i', b'IDP3', 15, name.encode(), 0, 1, len(tags), len(surfs), 0, ofs_frames, ofs_tags, ofs_surfs, ofs_end)
	assert len(head) == head_size == 108
	data = head + frame + tag_blob + b''.join(surf_blobs)
	assert len(data) == ofs_end
	return data, len(surfs), sum(len(s[2]) for s in surfs), sum(len(s[3]) for s in surfs)


def build():
	d, mats = load(os.path.join(HERE, 'cockpit.json'))
	eye = tuple(d['facts']['eye'])				# (game axes already)
	pivot = game(d['movers']['yoke']['pivot'])
	cockpit, stick = [], []
	for (mover, mat), tris in sorted(mats.items()):
		shader = 'models/vr/atst/' + mat
		(stick if mover == 'yoke' else cockpit).append((shader, surfaces(tris)))
	lgrip = YOKE_GRIP
	rgrip = (YOKE_GRIP[0], -YOKE_GRIP[1], YOKE_GRIP[2])
	return [
		('models/vr/atst/cockpit.md3',) + md3('models/vr/atst/cockpit.md3', cockpit,
			[('tag_eye', eye), ('tag_yoke', pivot), ('tag_origin', ATST_ORIGIN), ('tag_thoracic', ATST_THORACIC)]),
		('models/vr/atst/yoke.md3',) + md3('models/vr/atst/yoke.md3', stick, [('tag_lgrip', lgrip), ('tag_rgrip', rgrip)]),
		('models/vr/atst/hand.md3',) + md3('models/vr/atst/hand.md3', [('models/vr/atst/glove', surfaces(fist()))], []),
	]


C_HEAD = '''/*
===========================================================================
OpenJK VR (multiplayer) - the AT-ST cockpit's models, built in (made by
vr/atst/make_cockpit.py from the AT-ST Cockpit Test study's shapes,
vr/atst/cockpit.json: don't edit - make it again). Each is an MD3 file,
compressed (zlib) and written out in base64, a piece to a string.

This file is part of the OpenJK source code and is licensed under the GNU
General Public License version 2, like the rest of OpenJK.
===========================================================================
*/

typedef struct {
	const char			*name;		// as the game asks for it
	int					size;		// its MD3's bytes
	int					packed;		// compressed
	const char *const	*text;		// the compressed bytes in base64, a piece to a string, then NULL
} vrAtstModel_t;
'''

PIECE = 4000		# characters to a string (well within every compiler's longest)


def write_c(path, models):
	out = [C_HEAD]
	for n, (name, data, *_rest) in enumerate(models):
		packed = zlib.compress(data, 9)
		text = base64.b64encode(packed).decode()
		out.append('\nstatic const char *const vrAtstModel%d[] = {\n' % n)
		for i in range(0, len(text), PIECE):
			out.append('\t"%s",\n' % text[i:i + PIECE])
		out.append('\tNULL\n};\n')
	out.append('\nstatic const vrAtstModel_t vrAtstModels[] = {\n')
	for n, (name, data, *_rest) in enumerate(models):
		out.append('\t{ "%s", %d, %d, vrAtstModel%d },\n' % (name, len(data), len(zlib.compress(data, 9)), n))
	out.append('};\n')
	open(path, 'w', newline='\n').write(''.join(out))


def main():
	args = sys.argv[1:]
	models = build()
	for name, data, ns, nv, nt in models:
		print('%s: %d surfaces, %d corners, %d triangles, %d bytes (%d compressed)' % (name, ns, nv, nt, len(data), len(zlib.compress(data, 9))))
	if args[:1] == ['--c']:
		write_c(args[1], models)
		print('wrote', args[1])
	else:
		out = args[0] if args else '.'
		os.makedirs(out, exist_ok=True)
		for name, data, *_rest in models:
			open(os.path.join(out, os.path.basename(name)), 'wb').write(data)


if __name__ == '__main__':
	main()
