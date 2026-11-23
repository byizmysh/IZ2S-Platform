from pathlib import Path
import subprocess,tempfile,json,os,struct

module=Path(__file__).resolve().parent.parent
url='https://raw.githubusercontent.com/dadadush/switch2-homebrew-catalog/main/catalog/catalog.tsv'
def entry(id,kind,version='1.0'):
    return (f'app\t{id}\t{kind}\t{version}\tExample\tFixture Studio\tTools\tExample.\n'
            f'file\t123\t'+ 'a'*64+f'\tlauncher.{kind}\n'
            f'github\thttps://github.com/test/example/releases/download/v1.0/app.{kind}\n')
with tempfile.TemporaryDirectory(prefix='cheese-variants-') as folder:
    work=Path(folder)
    for standalone in [False,True]:
        mode='nro' if standalone else 'iza';binary=work/('test-'+mode);sd=work/mode;sd.mkdir()
        subprocess.run(['cc','-std=c11','-O1','-g','-Wall','-Wextra','-Werror',
            '-fsanitize=address,undefined','-fno-omit-frame-pointer',
            *(['-DIZ_STORE_STANDALONE']if standalone else []),'-I'+str(module/'switch'),
            module/'tests/catalog-variants.c',*(module/'switch'/n for n in ['store.c','ui.c','download.c','installed-artwork.c']),
            '-Wl,--wrap=github_latest_release','-lssl','-lcrypto','-o',binary],check=True)
        metadata=work/'release.json'
        def run(command,*args,code=0):
            r=subprocess.run([binary,command,sd,*args],env={**os.environ,'CHEESE_TEST_RELEASE_JSON':str(metadata)},text=True,capture_output=True)
            assert r.returncode==code,(command,r.stdout,r.stderr)
            return r.stdout
        for address in [url,' \r\n'+url+'\r\n ',
            'https://github.com/dadadush/switch2-homebrew-catalog',
            'https://github.com/dadadush/switch2-homebrew-catalog/',
            'https://github.com/dadadush/switch2-homebrew-catalog/blob/main/catalog/catalog.tsv']:
            assert run('save-url',address)==url+'\nonline\n'
            assert run('settings')==url+'\nonline\n'
        settings=sd/'iz2s/iz2s-cheese/settings.txt';original=settings.read_bytes()
        run('save-url','https://example.com/catalog.tsv',code=1);assert settings.read_bytes()==original
        settings.write_text(url+'\r\n'+'sd')
        assert run('settings')==url+'\nsd\n'
        settings.write_text(url);assert run('settings')==url+'\nonline\n'
        settings.with_name('settings.txt.bak').write_text(url+'\nsd\n');settings.write_text('invalid\n')
        assert run('settings')==url+'\nsd\n'
        settings.with_name('settings.txt.bak').unlink()
        cache=settings.with_name('remote.tsv');cache.write_text(url+'\nIZSTORE2\n'+entry('cached','nro'))
        assert run('settings')==url+'\nonline\n'
        settings.write_text(url+'\nsd\n')
        catalog=sd/('switch/iz2s-cheese/catalog'if standalone else 'iz2s/modules/iz2s-cheese/catalog');catalog.mkdir(parents=True)
        feed=('IZSTORE3\n'+entry('iz2s-cheese','iza')+entry('iz2s-cheese','nro')+
              entry('dual','nro')+entry('dual','iza')+entry('iza-only','iza')+entry('nro-only','nro'))
        (catalog/'catalog.tsv').write_text(feed)
        ids=['iz2s-cheese','dual','iza-only','nro-only']
        (catalog/'artwork.bin').write_bytes(b'IZART1\n\4'+b''.join(id.encode().ljust(48,b'\0')+struct.pack('<I',0xff336699)*(96*96) for id in ids))
        output=run('variants');assert f'iz2s-cheese\t{mode}'in output
        if standalone:assert 'iza-only'not in output and '\tiza\t'not in output
        for bad in [feed+entry('dual','nro'),feed.replace('IZSTORE3','IZSTORE2')]:
            (catalog/'catalog.tsv').write_text(bad);run('list',code=1)
        (catalog/'catalog.tsv').write_text(feed)
        # A direct official update check must also work with an empty catalog.
        (catalog/'catalog.tsv').unlink();settings_before=settings.read_bytes();cache_before=cache.read_bytes()
        base='https://github.com/byizmysh/iz2s-cheese/releases/download/v0.8.3/'
        asset='asset-'+'b'*64+'.izdata'
        assets=[{'name':asset,'size':10,'digest':'sha256:'+'b'*64,'browser_download_url':base+asset},
                {'name':'iz2s-cheese-install.iza','size':100,'digest':'sha256:'+'c'*64,
                 'browser_download_url':base+'iz2s-cheese-install.iza'}]
        for kind in ['iza','nro']:
            assets.append({'name':f'iz2s-cheese.{kind}','size':123,'digest':'sha256:'+'a'*64,'browser_download_url':base+f'iz2s-cheese.{kind}'})
        release={'tag_name':'v0.8.3','draft':False,'prerelease':False,'body':'Text with \\"quotes\\" and {braces}.','assets':assets}
        metadata.write_text(json.dumps(release))
        output=run('update');assert output.startswith(mode+'\t0.8.3\t'+('1'if standalone else '2'))
        assert settings.read_bytes()==settings_before and cache.read_bytes()==cache_before
        for malformed in ['{bad','['*30+'0'+']'*30,'x'*65537]:
            metadata.write_text(malformed);run('update',code=1)
        for key,value in [('draft',True),('prerelease',True),('tag_name','v0.8.3-beta')]:
            bad={**release,key:value};metadata.write_text(json.dumps(bad));run('update',code=1)
        bad=json.loads(json.dumps(release));bad['assets'][-1]['browser_download_url']='https://github.com/other/repo/releases/download/v0.8.3/iz2s-cheese.nro'
        if standalone:metadata.write_text(json.dumps(bad));run('update',code=1)
        if not standalone:
            bad={**release,'assets':[*assets,assets[0]]};metadata.write_text(json.dumps(bad));run('update',code=1)
        metadata.unlink();assert 'network unavailable'in run('update',code=1)
        assert settings.read_bytes()==settings_before and cache.read_bytes()==cache_before
        print(mode+': variants, search, hidden IZA, persistent URL, recovery and official update flow ASan/UBSan PASS')
