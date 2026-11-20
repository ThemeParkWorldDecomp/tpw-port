#!/usr/bin/env python3
"""Generate the approved TPW settings defaults from original Windows discs.

Reads ISO9660/Joliet cooked or raw images without mounting them. No TPI data.
Use --check to compare the generated source without changing it. --discs is the
external pc-original directory; no extracted game files are written anywhere.
--check always checks the committed file against KNOWN_OUTPUT_SHA256 (a hand edit
fails); with --if-present it regenerates only when every disc is there (ctest's
sam_originals_discs test). The maintainer's exception of 2026-11-17 (ROOT/RULES.md).
"""
import argparse
import hashlib
import importlib.util
import pathlib
import re
import struct

# launcher/sam_originals_data.inc as this script writes it from the discs below.
KNOWN_OUTPUT_SHA256 = 'ec707f0bb246c5b29edfc13f24a10d346483bf9b9ccb031b0c8b7c6ac56030ee'


class Disc:
    def __init__(self, path):
        self.file = path.open('rb')
        self.file.seek(16 * 2352)
        raw = self.file.read(16)
        self.stride = 2352 if raw[:12] == b'\0' + b'\xff' * 10 + b'\0' else 2048
        self.offset = (16 if raw[15] == 1 else 24) if self.stride == 2352 else 0
        root = None
        self.encoding = 'ascii'
        for sector in range(16, 64):
            data = self.read(sector, 2048)
            if data[1:6] != b'CD001':
                raise ValueError('not an ISO9660 disc')
            if data[0] == 255:
                break
            if data[0] == 1 and root is None:
                root = self.record(data[156:])
            if data[0] == 2 and data[88:90] == b'%/':
                root = self.record(data[156:])
                self.encoding = 'utf-16-be'
        if root is None:
            raise ValueError('no ISO9660 root')
        self.files = {}
        self.walk(root, '')

    @staticmethod
    def record(data):
        return (struct.unpack_from('<I', data, 2)[0], struct.unpack_from('<I', data, 10)[0],
                bool(data[25] & 2), data[33:33 + data[32]])

    def read(self, sector, size):
        result = bytearray()
        for i in range((size + 2047) // 2048):
            self.file.seek((sector + i) * self.stride + self.offset)
            chunk = self.file.read(2048)
            if len(chunk) != 2048:
                raise ValueError('truncated disc')
            result.extend(chunk)
        return bytes(result[:size])

    def walk(self, entry, prefix):
        data = self.read(entry[0], entry[1])
        pos = 0
        while pos < len(data):
            size = data[pos]
            if not size:
                pos = (pos // 2048 + 1) * 2048
                continue
            item = self.record(data[pos:pos + size])
            pos += size
            if item[3] in (b'\0', b'\1'):
                continue
            name = item[3].decode(self.encoding).split(';')[0]
            path = prefix + name
            if item[2]:
                self.walk(item, path + '/')
            else:
                self.files[path.lower()] = item


SOURCES = (
    ('us-pc', 'us-may2000-theme-park-gold-edition/TPWORLD.ISO'),
    ('eur-pc', 'eur-theme-park-world/ThemeParkWorld.img'),
    ('gold', 'us-may2000-theme-park-gold-edition/TPWORLD.ISO'),
)


def manifest_contents():
    """Accepted original hashes, used to refuse a wrong/older source disc."""
    manifest = (pathlib.Path(__file__).resolve().parents[1] / 'assets/manifest_data.inc').read_text()
    hashes = []
    for row in re.finditer(r'\{\{((?:0x[0-9a-f]{2},?){32})\}, (\d+)u, \d+u\}', manifest):
        hashes.append((''.join(re.findall(r'0x([0-9a-f]{2})', row[1])), int(row[2])))
    records = {}
    names = {}
    for row in re.finditer(r'\{"([^"]+)", (\d+)u, (\d+), \w+, ([^,]+),', manifest):
        for edition in row[4].split('|'):
            records[(edition, row[1].lower())] = hashes[int(row[2]):int(row[2]) + int(row[3])]
            names[(edition, row[1].lower())] = row[1]
    return records, names


def generate(discs, mac_disc=None):
    result = ['// Original .sam defaults. Written by devtools/gen_sam_defaults.py; do not edit.\n',
              '// Approved only for repairing a player\'s imported TPW settings, never a missing disc.\n']
    entries = []
    blobs = {}
    records, names = manifest_contents()
    if not records:
        raise ValueError('cannot read original manifest')
    for edition, source in SOURCES:
        disc = Disc(discs / source)
        count = 0
        for path, item in sorted(disc.files.items()):
            if not path.endswith('.sam') or '/data/' not in '/' + path:
                continue
            dest = 'data/' + path.split('data/', 1)[1]
            data = disc.read(item[0], item[1])
            if b'\0' in data:
                raise ValueError('NUL in original ' + path)
            digest = hashlib.sha256(data).hexdigest()
            manifest_edition = 'kEditionEurPc' if edition == 'eur-pc' else 'kEditionUsPc'
            if (digest, len(data)) not in records.get((manifest_edition, dest), []):
                # Primary ISO names can be 8.3 even when the game uses a long
                # name. Resolve only a unique accepted content in that folder.
                candidates = [name for (ed, name), accepted in records.items()
                              if ed == manifest_edition and name.endswith('.sam')
                              and pathlib.PurePosixPath(name).parent == pathlib.PurePosixPath(dest).parent
                              and (digest, len(data)) in accepted]
                if len(candidates) != 1:
                    raise ValueError('source .sam is not the accepted original: ' + source + ':' + dest)
                dest = candidates[0]
            if digest not in blobs:
                symbol = 'kSam' + str(len(blobs))
                blobs[digest] = symbol
                result.append('// SHA-256 ' + digest + '\n')
                result.append('static const char ' + symbol + '[] =\n')
                for start in range(0, len(data), 80):
                    escaped = ''.join('\\%03o' % b for b in data[start:start + 80])
                    result.append('    "' + escaped + '"\n')
                result.append('    ;\n')
            entries.append((edition, names[(manifest_edition, dest)], blobs[digest], len(data)))
            # Only byte-identical originals may serve the Mac edition. Its
            # different tuning/settings files require its own original disc.
            if edition in ('us-pc', 'eur-pc'):
                mac = 'us' if edition == 'us-pc' else 'eur'
                mac_record = 'kEditionUs' if mac == 'us' else 'kEditionEur'
                if (digest, len(data)) in records.get((mac_record, dest), []):
                    entries.append((mac, names[(mac_record, dest)], blobs[digest], len(data)))
            count += 1
        disc.file.close()
        if not count:
            raise ValueError('no original .sam files in ' + source)
        print(edition + ': ' + str(count) + ' original .sam files')
    if mac_disc:
        # Reuse the existing vendored HFS reader. Read the original CD in
        # memory; never extract game data into this checkout.
        helper_path = pathlib.Path(__file__).resolve().parent / 'import_originals.py'
        spec = importlib.util.spec_from_file_location('sam_original_disc', helper_path)
        helper = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(helper)
        import machfs
        image = helper.read_toast(mac_disc)
        start, length, _ = helper.find_hfs(image)
        volume = machfs.Volume()
        volume.read(memoryview(image)[start:start + length])
        del image
        count = 0
        for path, obj in volume.iter_paths():
            if not isinstance(obj, machfs.File) or path[0] != 'Theme Park Data':
                continue
            dest = '/'.join(path[1:]).lower()
            if not dest.endswith('.sam'):
                continue
            data = bytes(obj.data)
            digest = hashlib.sha256(data).hexdigest()
            for edition, manifest_edition in (('us', 'kEditionUs'), ('eur', 'kEditionEur')):
                if (digest, len(data)) not in records.get((manifest_edition, dest), []):
                    continue
                canonical = names[(manifest_edition, dest)]
                if any(ed == edition and name == canonical for ed, name, _, _ in entries):
                    continue
                if digest not in blobs:
                    symbol = 'kSam' + str(len(blobs))
                    blobs[digest] = symbol
                    result.append('// SHA-256 ' + digest + '\n')
                    result.append('static const char ' + symbol + '[] =\n')
                    for offset in range(0, len(data), 80):
                        escaped = ''.join('\\%03o' % b for b in data[offset:offset + 80])
                        result.append('    "' + escaped + '"\n')
                    result.append('    ;\n')
                entries.append((edition, canonical, blobs[digest], len(data)))
                count += 1
        print('Mac originals: ' + str(count) + ' additional settings defaults')
    result.append('static const SamDefault kSamDefaults[] = {\n')
    for edition, dest, symbol, size in entries:
        result.append('    {"%s", "%s", %s, %d},\n' % (edition, dest, symbol, size))
    result.append('};\n')
    return ''.join(result)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--discs', type=pathlib.Path, required=True)
    parser.add_argument('--mac-disc', type=pathlib.Path, help='original US Mac CD Toast zip')
    parser.add_argument('--out', type=pathlib.Path,
                        default=pathlib.Path(__file__).resolve().parents[1] / 'launcher/sam_originals_data.inc')
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--if-present', action='store_true',
                        help='with --check: regenerate only when every disc is there')
    args = parser.parse_args()
    if args.check:
        if not args.out.exists() or hashlib.sha256(args.out.read_bytes()).hexdigest() != KNOWN_OUTPUT_SHA256:
            raise SystemExit('the committed .sam defaults are not the generated ones (KNOWN_OUTPUT_SHA256)')
        discs = [args.discs / source for _, source in SOURCES] + ([args.mac_disc] if args.mac_disc else [])
        if args.if_present and not all(path.exists() for path in discs):
            print('original .sam defaults: known output; the discs are not here, not regenerated')
            return
    text = generate(args.discs, args.mac_disc)
    if args.check:
        if not args.out.exists() or args.out.read_text() != text:
            raise SystemExit('generated .sam defaults differ')
        print('original .sam defaults match')
    else:
        args.out.write_text(text)


if __name__ == '__main__':
    main()
