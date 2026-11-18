"""Install one validated NRO through the existing paired IZlauncher FTP service."""
import re
import sys
from deployment import main


if __name__ == '__main__':
    if len(sys.argv) != 3 or not re.fullmatch(r'[a-z0-9][a-z0-9-]{0,46}', sys.argv[1]):
        raise SystemExit('Expected a validated NRO ID and source path')
    app_id, source = sys.argv[1:]
    main('launcher.nro', f'/iz2s/modules/{app_id}/launcher.nro',
         ['--paired', '--source', source], kind='nro', initial=True)
