import hashlib
import json
import os
from io import BytesIO
from pathlib import Path
import struct
import subprocess
import sys
from tempfile import TemporaryDirectory
from zipfile import ZipFile
from PIL import Image

module = Path(__file__).resolve().parent
output = Path(sys.argv[1])
check_iza, check_nro = (value == '1' for value in sys.argv[2:])

if check_iza:
    entry = 'iz2s/modules/iz2s-cheese/launcher.iza'
    with ZipFile(output / 'iz2s-cheese-iza-sd.zip') as archive:
        bundle = json.loads(archive.read('iz2s/modules/iz2s-cheese/sd-bundle.json'))
        companions = bundle['assets']
        assert len(companions) == 1
        assert set(archive.namelist()) == {entry, 'iz2s/modules/iz2s-cheese/sd-bundle.json',
            *('iz2s/modules/iz2s-cheese/' + item['file'] for item in companions)}
        for item in companions:
            engine = archive.read('iz2s/modules/iz2s-cheese/' + item['file'])
            assert len(engine) == item['bytes'] and hashlib.sha256(engine).hexdigest() == item['sha256']
            assert engine.startswith(b'IZNATRL1')
            assert b'sb4IZ2SCheeseEvent' not in engine
            assert b'sb4NativeServicesVersion' not in engine
            assert b'sslConnectionDoHandshake' in engine and b'swkbdShow' in engine
        iza = archive.read(entry)
        assert bundle['launcher']['sha256'] == hashlib.sha256(iza).hexdigest()
        assert b'sb4IZ2SCheeseEvent' not in iza
        assert b'sb4NativeServicesVersion' not in iza
    assert iza == (output / 'iz2s-cheese.iza').read_bytes()
    assert iza.startswith(b'IZSDAPP1')
    artwork = iza.index(b'IZSTOREA')
    assert iza[artwork + 8] == 0
    assert b'IZSTORE2\napp\t' not in iza

if check_nro:
    entry = 'switch/iz2s-cheese/iz2s-cheese.nro'
    with ZipFile(output / 'iz2s-cheese-nro-sd.zip') as archive:
        assert archive.namelist() == [entry]
        nro = archive.read(entry)
    assert nro == (output / 'iz2s-cheese.nro').read_bytes()
    assert nro[16:20] == b'NRO0'
    mod = struct.unpack_from('<I', nro, 4)[0]
    assert nro[mod:mod+4] == b'MOD0' and b'OUN0\x02\0\0\0\x01\0\0\0' in nro[mod:mod+128]
    aset = struct.unpack_from('<I', nro, 24)[0]
    assert nro[aset:aset+4] == b'ASET'
    fs, size = struct.unpack_from('<2Q', nro, aset+40)
    romfs = nro[aset+fs:aset+fs+size]
    assert size and b'font.bin' in romfs
    assert all(name not in romfs for name in (b'catalog.tsv', b'sources.json', b'payloads', b'IZSTORE2'))

with TemporaryDirectory(prefix='iz-store-check-') as directory:
    work = Path(directory)
    mock = work / 'catalog-download.c'
    mock.write_text(r'''
#include "download.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

bool __wrap_catalog_download(const char *url, uint64_t limit, DownloadSink sink,
                             void *context, char out[DOWNLOAD_MESSAGE]) {
    const char *name = strrchr(url, '/');
    FILE *file = fopen(getenv(name && !strcmp(name, "/artwork.bin") ?
                             "IZ_STORE_TEST_ARTWORK_PACK" : "IZ_STORE_TEST_CATALOG"), "rb");
    if (!file) return false;
    unsigned char data[1024];
    size_t count;
    bool ok = true;
    while (ok && (count = fread(data, 1, sizeof data, file)) != 0) {
        ok = count <= limit && sink(context, data, count);
        limit -= ok ? count : 0;
    }
    ok = ok && !ferror(file);
    fclose(file);
    if (!ok) snprintf(out, DOWNLOAD_MESSAGE, "Test source read failed.");
    return ok;
}

bool __wrap_github_download(const char *url, uint64_t size, DownloadSink sink, void *context,
                            const atomic_bool *cancel, char out[DOWNLOAD_MESSAGE]) {
    const char release[] = "https://github.com/test/fixtures/releases/download/v1/";
    if (strncmp(url, release, sizeof release - 1) || !github_release_url(url, NULL)) return false;
    char path[1024];
    int length = snprintf(path, sizeof path, "%s/%s", getenv("IZ_STORE_TEST_RELEASE"),
                          url + sizeof release - 1);
    if (length < 0 || (size_t)length >= sizeof path) return false;
    FILE *file = fopen(path, "rb");
    if (!file) {
        snprintf(out, DOWNLOAD_MESSAGE, "Release file missing.");
        return false;
    }
    unsigned char data[1024];
    size_t count;
    bool ok = true;
    while (ok && (count = fread(data, 1, sizeof data, file)) != 0) {
        ok = (!cancel || !atomic_load(cancel)) && count <= size && sink(context, data, count);
        size -= ok ? count : 0;
    }
    ok = ok && !ferror(file) && size == 0;
    fclose(file);
    if (!ok) snprintf(out, DOWNLOAD_MESSAGE, "Release transfer failed.");
    return ok;
}
''')
    url = 'https://raw.githubusercontent.com/example/store-feed/main/catalog.tsv'
    source = work / 'feed.tsv'
    artwork = work / 'artwork.bin'
    external = os.environ.get('IZ_STORE_CHECK_CATALOG')
    initial = Path(external).read_text() if external else (
        'IZSTORE2\napp\tsample\tnro\t1.0\tSample app\tExample\tTools\tSample application.\n'
        'file\t123\t' + '0' * 64 + '\tlauncher.nro\n'
        'github\thttps://github.com/example/sample/releases/download/v1.0/sample.nro\n'
    )
    app_ids = [line.split('\t')[1] for line in initial.splitlines() if line.startswith('app\t')]
    assert app_ids
    image_pack = b'IZART1\n\x01' + app_ids[0].encode().ljust(48, b'\0') + b'\xff' * (96 * 96 * 4)
    release = work / 'release'
    release.mkdir()

    def digest(data):
        return hashlib.sha256(data).hexdigest()

    def file_row(data, name):
        return f'file\t{len(data)}\t{digest(data)}\t{name}\n'

    def online_iza(version, assets):
        header = bytearray(512)
        header[:8] = b'IZSDAPP1'
        struct.pack_into('<4I', header, 8, 2, len(header), len(header), 8 if assets else 0)
        header[24:32] = b'feed-iza'
        if assets:
            struct.pack_into('<2I', header, 160, len(assets), sum(map(len, assets.values())))
            identities = [[name, len(data), digest(data)] for name, data in sorted(assets.items())]
            binding = 'iz2s.sd-assets.v1\n' + json.dumps(identities, separators=(',', ':'))
            header[368:400] = hashlib.sha256(binding.encode()).digest()
        launcher = bytes(header)
        (release / 'program.iza').write_bytes(launcher)
        for name, data in assets.items():
            (release / name).write_bytes(data)
        return (f'app\tfeed-iza\tiza\t{version}\tFixture IZA\tExample\tTools\tTest.\n'
                + file_row(launcher, 'launcher.iza')
                + ''.join(file_row(data, name) for name, data in assets.items())
                + 'github\thttps://github.com/test/fixtures/releases/download/v1/program.iza\n')

    mixed_nro = ('IZSTORE2\napp\ttest-nro\tnro\t1.0\tFixture NRO\tExample\tTools\tTest.\n'
                 + file_row(b'nro', 'launcher.nro')
                 + 'github\thttps://github.com/test/fixtures/releases/download/v1/program.nro\n')
    for standalone in (False, True):
        binary = work / 'host-check'
        sd = work / ('sd-nro' if standalone else 'sd-iza')
        sd.mkdir()
        subprocess.run([
            os.environ.get('CC', 'cc'), '-std=c11', '-O2', '-Wall', '-Wextra', '-Werror',
            *(['-DIZ_STORE_STANDALONE'] if standalone else []),
            '-I' + str(module / 'switch'), module / 'host-check.c', mock,
            *(module / 'switch' / name for name in ('store.c', 'ui.c', 'download.c', 'installed-artwork.c')),
            '-Wl,--wrap=catalog_download', '-Wl,--wrap=github_download', '-lssl', '-lcrypto', '-o', binary,
        ], check=True)

        def run(*args, code=0):
            result = subprocess.run([binary, sd, *args], capture_output=True, text=True,
                                    env={**os.environ, 'IZ_STORE_TEST_CATALOG': str(source),
                                         'IZ_STORE_TEST_ARTWORK_PACK': str(artwork),
                                         'IZ_STORE_TEST_RELEASE': str(release)})
            assert result.returncode == code, (args, result.stdout, result.stderr)
            return result.stdout

        source.write_text(initial)
        artwork.write_bytes(image_pack)
        assert not run('list')
        assert not (sd / 'iz2s').exists()
        assert f'{len(app_ids)} online entries, 1 thumbnail.' in run('fetch-list', url)
        cache = sd / 'iz2s/iz2s-cheese/remote.tsv'
        assert cache.read_text() == url + '\n' + initial
        assert [line.split('\t')[0] for line in run('list').splitlines()] == app_ids
        assert run('artwork').strip() == '1'
        artwork_cache = cache.with_name('remote-artwork.bin')
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + image_pack
        artwork.write_bytes(image_pack[:-1])
        assert 'No new thumbnails' in run('fetch-list', url)
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + image_pack
        assert run('artwork').strip() == '1'
        source.write_text('IZSTORE2\ninvalid\n')
        assert 'Invalid list' in run('fetch-list', url, code=1)
        assert cache.read_text() == url + '\n' + initial
        assert len(run('list').splitlines()) == len(app_ids)
        assert 'Enter a catalog URL' in run('fetch-list', '', code=1)
        assert not run('list')
        assert run('artwork').strip() == '0'
        (cache.parent / 'catalog.tsv').write_text(initial)
        assert len(run('list').splitlines()) == len(app_ids)
        cache.unlink()
        artwork_cache.unlink()
        (cache.parent / 'settings.txt').write_text(url + '\n')
        source.write_text(initial)
        artwork.write_bytes(image_pack)
        assert '1 thumbnail.' in run('refresh')
        assert run('artwork').strip() == '1'
        artwork_cache.unlink()
        assert not run('refresh').strip()
        assert run('artwork').strip() == '1'
        source.unlink()
        artwork.unlink()
        assert not run('refresh').strip()
        assert run('artwork').strip() == '1'
        active = sd / ('switch/iz2s-cheese/catalog' if standalone else 'iz2s/modules/iz2s-cheese/catalog')
        active.mkdir(parents=True)
        local_id = 'sd-fixture'
        local = ('IZSTORE2\napp\tsd-fixture\tnro\t1.0\tSD App\tExample\tTools\tTest.\n'
                 'file\t123\t' + '0' * 64 + '\tlauncher.nro\n')
        (active / 'catalog.tsv').write_text(local)
        (active / 'artwork.bin').write_bytes(b'IZART1\n\x01' + local_id.encode().ljust(48, b'\0') + b'\xfe' * (96 * 96 * 4))
        assert local_id not in run('list')
        (cache.parent / 'settings.txt').write_text(url + '\nsd\n')
        assert [line.split('\t')[0] for line in run('list').splitlines()] == [local_id]
        assert run('artwork').strip() == '1'
        install_root = sd / ('switch' if standalone else 'iz2s/modules')
        jpeg = BytesIO()
        Image.new('RGB', (32, 32), (255, 0, 0)).save(jpeg, format='JPEG')
        image = jpeg.getvalue()
        nro = bytearray(32 + 56)
        nro[16:20] = b'NRO0'
        struct.pack_into('<I', nro, 24, 32)
        struct.pack_into('<4sIQQ', nro, 32, b'ASET', 0, 56, len(image))
        for id in (local_id, 'not-in-catalog'):
            binary_path = install_root / id / (id+'.nro' if standalone else 'launcher.nro')
            binary_path.parent.mkdir()
            binary_path.write_bytes(nro + image)
        assert run('installed').splitlines() == [local_id]
        icon = int(run('icon', local_id), 16)
        assert icon >> 24 == 255 and icon & 255 >= 250
        (install_root / local_id / '.izcheese-icon.rgba').write_bytes(b'\x00\xff\x00\xff' * (96 * 96))
        assert run('icon', local_id).strip() == 'ff00ff00'

        source.write_text(mixed_nro + online_iza('1.0', {}))
        artwork.write_bytes(image_pack)
        assert '2 online entries' in run('fetch-list', url)
        assert [line.split('\t')[0] for line in run('list').splitlines()] == ['test-nro', 'feed-iza']
        if standalone:
            assert 'IZA apps require IZ2S' in run('raw-install', 'feed-iza', code=1)
            assert not (install_root / 'feed-iza').exists()
            continue
        assert 'Installed 1.0' in run('raw-install', 'feed-iza')
        app_dir = install_root / 'feed-iza'
        launcher = app_dir / 'launcher.iza'
        old_launcher = launcher.read_bytes()

        resources = {f'asset-{digest(data)}.izdata': data for data in (b'first file', b'second file')}
        resources = dict(sorted(resources.items(), reverse=True))
        updated = mixed_nro + online_iza('2.0', resources)
        source.write_text(updated)
        assert '2 online entries' in run('fetch-list', url)

        last = next(reversed(resources))
        (release / last).write_bytes(b'X' * len(resources[last]))
        assert 'SHA-256' in run('raw-install', 'feed-iza', code=1)
        assert launcher.read_bytes() == old_launcher
        assert not list(app_dir.glob('*.izdata'))
        (release / last).write_bytes(resources[last])

        wrong_data = b'Y' * len(resources[last])
        wrong_name = f'asset-{digest(wrong_data)}.izdata'
        source.write_text(updated.replace(file_row(resources[last], last), file_row(wrong_data, wrong_name)))
        assert '2 online entries' in run('fetch-list', url)
        assert 'incompatible' in run('raw-install', 'feed-iza', code=1)
        assert launcher.read_bytes() == old_launcher

        source.write_text(updated)
        assert '2 online entries' in run('fetch-list', url)
        assert 'Updated to 2.0' in run('raw-install', 'feed-iza')
        assert launcher.read_bytes() == (release / 'program.iza').read_bytes()
        assert all((app_dir / name).read_bytes() == data for name, data in resources.items())
        assert 'Removed' in run('raw-remove', 'feed-iza')
        assert not launcher.exists() and not list(app_dir.glob('*.izdata'))


print('Verified catalog caching, artwork, mixed feeds and online IZA install, update and rollback.')
