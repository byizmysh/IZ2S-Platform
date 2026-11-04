"""SSH policy shared by ARM input profiles. Never writes the HID device itself."""
import os
SSH = ['ssh', '-F', os.devnull, '-o', 'StrictHostKeyChecking=yes', '-o', 'BatchMode=yes',
       '-o', 'ConnectTimeout=4', '-o', 'ServerAliveInterval=3', '-o', 'ServerAliveCountMax=2']
identity = os.environ.get('IZ2S_SSH_IDENTITY') or os.environ.get('IZ15_SSH_IDENTITY')
if identity: SSH += ['-i', identity]
known_hosts = os.environ.get('IZ2S_SSH_KNOWN_HOSTS') or os.environ.get('IZ15_SSH_KNOWN_HOSTS')
if known_hosts: SSH += ['-o', 'UserKnownHostsFile='+known_hosts]
host_alias = os.environ.get('IZ2S_SSH_HOST_KEY_ALIAS') or os.environ.get('IZ15_SSH_HOST_KEY_ALIAS')
if host_alias: SSH += ['-o', 'HostKeyAlias='+host_alias]
