#!/usr/bin/python3
"""Install/remove only this user's IZ2S Dolphin service menu and launcher."""
import argparse
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[2]


def quoted(value):
    # Desktop Entry string escaping followed by Exec argument escaping.
    value = str(value).replace('%', '%%')
    value = value.replace('\\', '\\\\\\\\').replace('"', '\\\\"').replace('`', '\\\\`').replace('$', '\\\\$')
    if '\n' in value or '\r' in value:
        raise ValueError('Newline in installation path')
    return '"' + value + '"'


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument('--uninstall', action='store_true')
    options = parser.parse_args()
    base = Path(os.environ.get('XDG_DATA_HOME', Path.home() / '.local/share'))
    service = base / 'kio/servicemenus/run21-switch2-dolphin.desktop'
    launcher = base / 'applications/run21-switch2-dolphin.desktop'
    if options.uninstall:
        for file in (service, launcher):
            file.unlink(missing_ok=True)
        print('Dolphin integration removed.')
        return
    subprocess.run(['/usr/bin/python3', '-c', 'from PyQt6 import QtWidgets,QtNetwork'], check=True)
    command = quoted('/usr/bin/python3') + ' ' + quoted(ROOT / 'tools/dolphin-fs-rpc/choose.py')
    content = {
        service: '[Desktop Entry]\nType=Service\nMimeType=inode/directory;\nActions=chooseSwitchGame;\n'
                 'X-KDE-Priority=TopLevel\n\n[Desktop Action chooseSwitchGame]\n'
                 'Name=Switch 2 - Choose a game via RPC…\nIcon=folder-games\nExec=' + command + '\n',
        launcher: '[Desktop Entry]\nType=Application\nName=Switch 2 - Games in Dolphin\n'
                  'Comment=Browse console games through IZ2S FS RPC\nIcon=folder-games\n'
                  'Terminal=false\nCategories=Utility;FileTools;\nStartupNotify=true\nExec=' + command + '\n'
    }
    for file, text in content.items():
        file.parent.mkdir(parents=True, exist_ok=True)
        file.write_text(text)
        file.chmod(0o755)
        # Type=Service is a KDE extension to the standard Desktop Entry type.
        if file == launcher:
            subprocess.run(['desktop-file-validate', str(file)], check=True)
        print(file)
    subprocess.run(['kbuildsycoca6', '--noincremental'], check=True)


if __name__ == '__main__':
    main()
