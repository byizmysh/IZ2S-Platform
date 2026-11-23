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
    prefix = 'iz2s/modules/iz2s-cheese/'
    with ZipFile(output / 'iz2s-cheese-iza-sd.zip') as archive, \
            ZipFile(output / 'iz2s-cheese-install.iza') as installer:
        bundle = json.loads(archive.read(prefix + 'sd-bundle.json'))
        companions = bundle['assets']
        assert len(companions) == 1
        members = {'launcher.iza', 'sd-bundle.json', *(item['file'] for item in companions)}
        assert sorted(archive.namelist()) == sorted(prefix + name for name in members)
        assert sorted(installer.namelist()) == sorted(members)
        for name in members:
            assert installer.read(name) == archive.read(prefix + name)
        for item in companions:
            engine = installer.read(item['file'])
            assert len(engine) == item['bytes'] and hashlib.sha256(engine).hexdigest() == item['sha256']
            assert engine.startswith(b'IZNATRL1')
            assert b'sb4IZ2SCheeseEvent' not in engine
            assert b'sb4NativeServicesVersion' not in engine
            assert b'sslConnectionDoHandshake' in engine and b'swkbdShow' in engine
        iza = installer.read('launcher.iza')
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
    assert size and b'font.bin' in romfs and b'brand.rgba' in romfs
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
    FILE *requests = fopen(getenv("IZ_STORE_TEST_REQUESTS"), "a");
    if (!requests) return false;
    fprintf(requests, "%s\n", url);
    fclose(requests);
    const char *name = strrchr(url, '/');
    bool artwork = name && !strcmp(name, "/artwork.bin");
    const char *variable = strstr(url, "/second-feed/") ?
        (artwork ? "IZ_STORE_TEST_SECOND_ARTWORK" : "IZ_STORE_TEST_SECOND_CATALOG") :
        (artwork ? "IZ_STORE_TEST_ARTWORK_PACK" : "IZ_STORE_TEST_CATALOG");
    FILE *file = fopen(getenv(variable), "rb");
    if (!file) {
        snprintf(out, DOWNLOAD_MESSAGE, "Test source is offline.");
        return false;
    }
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
    second_url = 'https://raw.githubusercontent.com/example/second-feed/main/catalog.tsv'
    source = work / 'feed.tsv'
    artwork = work / 'artwork.bin'
    second_source = work / 'second.tsv'
    second_artwork = work / 'second-artwork.bin'
    requests = work / 'requests.txt'
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
                                         'IZ_STORE_TEST_SECOND_CATALOG': str(second_source),
                                         'IZ_STORE_TEST_SECOND_ARTWORK': str(second_artwork),
                                         'IZ_STORE_TEST_REQUESTS': str(requests),
                                         'IZ_STORE_TEST_RELEASE': str(release)})
            assert result.returncode == code, (args, result.stdout, result.stderr)
            return result.stdout

        source.write_text(initial)
        artwork.write_bytes(image_pack)
        assert not run('list')
        assert not (sd / 'iz2s').exists()
        assert f'{len(app_ids)} online entries, 1 thumbnail.' in run('fetch-list', url)
        cache_key = digest(url.encode())
        cache = sd / f'iz2s/iz2s-cheese/catalog-{cache_key}.tsv'
        assert cache.read_text() == url + '\n' + initial
        assert [line.split('\t')[0] for line in run('list').splitlines()] == app_ids
        assert run('artwork').strip() == '1'
        artwork_cache = cache.with_name(f'artwork-{cache_key}.bin')
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + image_pack
        artwork.write_bytes(image_pack[:-1])
        assert 'No new thumbnails' in run('fetch-list', url)
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + image_pack
        assert run('artwork').strip() == '1'
        source.write_text('IZSTORE2\ninvalid\n')
        assert 'Invalid list' in run('fetch-list', url, code=1)
        assert cache.read_text() == url + '\n' + initial
        assert len(run('list').splitlines()) == len(app_ids)
        assert 'enable a source' in run('fetch-list', '', code=1)
        assert not run('list')
        assert run('artwork').strip() == '0'
        (cache.parent / 'catalog.tsv').write_text(initial)
        (cache.parent / 'settings.txt').write_text('IZCATALOGS3\nsd-on\n')
        assert len(run('list').splitlines()) == len(app_ids)
        cache.unlink()
        artwork_cache.unlink()
        (cache.parent / 'settings.txt').write_text(url + '\n')
        source.write_text(initial)
        artwork.write_bytes(image_pack)
        assert '1 thumbnail.' in run('refresh')
        assert run('artwork').strip() == '1'
        artwork_cache.unlink()
        assert not run('load-catalog').strip()
        assert run('artwork').strip() == '1'

        # A normal startup must keep using cache. Y/Refresh must fetch both files,
        # even when a valid catalog and artwork are already cached.
        first_id = app_ids[0]
        updated_ids = [first_id, 'refresh-added']
        updated_feed = 'IZSTORE2\n' + ''.join(
            f'app\t{id}\tnro\t99.0\tUpdated app\tExample\tTools\tUpdated details.\n'
            'file\t123\t' + '0' * 64 + '\tlauncher.nro\n'
            'github\thttps://github.com/example/sample/releases/download/v99.0/sample.nro\n'
            for id in updated_ids)
        updated_art = b'IZART1\n\x02' + b''.join(
            id.encode().ljust(48, b'\0') + bytes([101 + i]) * (96 * 96 * 4)
            for i, id in enumerate(updated_ids))
        source.write_text(updated_feed)
        artwork.write_bytes(updated_art)
        requests.write_text('')
        assert not run('load-catalog').strip()
        assert requests.read_text() == ''
        assert cache.read_text() == url + '\n' + initial
        assert '2 online entries, 2 thumbnails.' in run('refresh')
        assert requests.read_text().splitlines() == [url, url.rsplit('/', 1)[0] + '/artwork.bin']
        assert cache.read_text() == url + '\n' + updated_feed
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + updated_art
        refreshed = [line.split('\t') for line in run('list').splitlines()]
        assert [line[0] for line in refreshed] == updated_ids
        assert all(line[-1] == '99.0' for line in refreshed)

        source.write_text('IZSTORE2\ninvalid\n')
        assert 'showing cached catalog' in run('refresh', code=1)
        assert cache.read_text() == url + '\n' + updated_feed
        assert artwork_cache.read_bytes() == url.encode() + b'\n' + updated_art
        assert [line.split('\t')[0] for line in run('list').splitlines()] == updated_ids
        source.unlink()
        artwork.unlink()
        requests.write_text('')
        assert not run('load-catalog').strip()
        assert requests.read_text() == ''
        assert 'showing cached catalog' in run('refresh', code=1)
        assert 'Test source is offline' in run('refresh', code=1)
        assert [line.split('\t')[0] for line in run('list').splitlines()] == updated_ids
        assert run('artwork').strip() == '2'

        source.write_text(updated_feed)
        artwork.write_bytes(updated_art)
        second_ids = [first_id, 'second-only']
        second_source.write_text(updated_feed.replace('refresh-added', 'second-only').replace('99.0', '98.0'))
        second_artwork.write_bytes(b'IZART1\n\x02' + b''.join(
            id.encode().ljust(48, b'\0') + b'\xa7' * (96 * 96 * 4) for id in second_ids))
        settings_file = cache.with_name('settings.txt')
        settings_file.write_text(f'IZCATALOGS1\nonline\n1\t{url}\n1\t{second_url}\n')
        requests.write_text('')
        assert '3 apps from 2/2 catalogs' in run('refresh')
        assert requests.read_text().splitlines() == [
            url, url.rsplit('/', 1)[0] + '/artwork.bin', second_url, second_url.rsplit('/', 1)[0] + '/artwork.bin']
        merged = [line.split('\t') for line in run('list').splitlines()]
        assert [(line[0], line[-1]) for line in merged] == [(first_id, '99.0'), ('refresh-added', '99.0'), ('second-only', '98.0')]
        assert run('artwork').strip() == '3'
        second_source.unlink()
        assert 'Available caches were kept' in run('refresh', code=1)
        assert 'second-only' in run('list')
        settings_file.write_text(f'IZCATALOGS1\nonline\n1\t{url}\n0\t{second_url}\n')
        requests.write_text('')
        assert '2 online entries' in run('refresh')
        assert second_url not in requests.read_text()
        assert 'second-only' not in run('list') and run('artwork').strip() == '2'
        settings_file.write_text(f'IZCATALOGS1\nonline\n0\t{url}\n0\t{second_url}\n')
        requests.write_text('')
        assert not run('refresh').strip()
        assert requests.read_text() == '' and not run('list')

        # A redirect is discovered at launch even with a valid cache.
        original_settings = f'IZCATALOGS1\nonline\n1\t{url}\n0\t{second_url}\n'
        settings_file.write_text(original_settings)
        for redirect, diagnostic in (
            (f'IZREDIRECT1\t{url}\n', 'redirect loop'),
            ('IZREDIRECT1\thttp://192.168.1.10/feed.tsv\n', 'same origin'),
            (f'IZREDIRECT1\t{second_url}\nextra\n', 'same origin'),
            (f'IZREDIRECT1\t{second_url}\n', 'Test source is offline'),
        ):
            source.write_text(redirect)
            assert diagnostic in run('open-catalogs', code=1)
            assert settings_file.read_text() == original_settings
            assert [line.split('\t')[0] for line in run('list').splitlines()] == updated_ids

        community = 'https://raw.githubusercontent.com/dadadush/switch2-homebrew-catalog/main/catalog/catalog-new.tsv'
        settings_file.write_text(f'IZCATALOGS1\nonline\n1\t{community}\n0\t{second_url}\n')
        second_source.write_text(updated_feed.replace('refresh-added', 'second-only'))
        requests.write_text('')
        assert 'Catalog address updated' in run('open-catalogs')
        assert requests.read_text().splitlines()[:2] == [community, second_url]
        assert settings_file.read_text() == f'IZCATALOGS3\nsd-off\n3\t{second_url}\n'
        assert 'second-only' in run('list') and 'refresh-added' not in run('list')
        requests.write_text('')
        run('open-catalogs')
        assert requests.read_text().splitlines()[0] == second_url and community not in requests.read_text()
        source.write_text(updated_feed)
        settings_file.write_text(f'IZCATALOGS1\nonline\n1\t{url}\n0\t{second_url}\n')

        active = sd / ('switch/iz2s-cheese/catalog' if standalone else 'iz2s/modules/iz2s-cheese/catalog')
        active.mkdir(parents=True)
        local_id = 'sd-fixture'
        local = ('IZSTORE2\napp\tsd-fixture\tnro\t1.0\tSD App\tExample\tTools\tTest.\n'
                 'file\t123\t' + '0' * 64 + '\tlauncher.nro\n')
        (active / 'catalog.tsv').write_text(local)
        (active / 'artwork.bin').write_bytes(b'IZART1\n\x01' + local_id.encode().ljust(48, b'\0') + b'\xfe' * (96 * 96 * 4))
        assert local_id not in run('list')
        settings_file.write_text(f'IZCATALOGS2\nsd\n1\t{url}\n0\t{second_url}\n')
        assert [line.split('\t')[0] for line in run('list').splitlines()] == [local_id]
        assert run('artwork').strip() == '1'
        requests.write_text('')
        assert 'SD catalog reloaded' in run('refresh')
        assert requests.read_text() == ''
        assert [line.split('\t')[0] for line in run('list').splitlines()] == [local_id]
        settings_file.write_text(f'IZCATALOGS3\nsd-on\n1\t{url}\n0\t{second_url}\n')
        assert '3 apps from 2/2 catalogs' in run('refresh')
        assert [line.split('\t')[0] for line in run('list').splitlines()] == [*updated_ids, local_id]
        assert run('artwork').strip() == '3'
        (active / 'catalog.tsv').write_text('invalid\n')
        assert 'Some sources could not be refreshed' in run('refresh', code=1)
        assert [line.split('\t')[0] for line in run('list').splitlines()] == updated_ids
        (active / 'catalog.tsv').write_text(local)
        settings_file.write_text(f'IZCATALOGS3\nsd-on\n0\t{url}\n0\t{second_url}\n')
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
            assert not run('raw-install', 'feed-iza', code=2)
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


print('Verified catalog migration on startup, redirect fallback, legacy source settings, mixed SD/online lists, thumbnails, per-source disable and online IZA install/rollback.')
