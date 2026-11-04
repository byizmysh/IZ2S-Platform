# OunceCake

(`ozdumptool` if you're ***lame*** and ***boring***) 

Tool to dump unencrypted romFS and exeFS for Switch 2 games, updates and DLC.

Supports digital games, game cards, Switch 2 Editions, etc.

## Disclaimer

For the love of God, do NOT share any files you rip using this tool. It is to be used with ONLY your own games on your OWN system. This tool doesn't provide system keys or any method to decrypt files. The files are loaded normally by the system, then pulled over FTP.

## Usage

0. Take note of your Switch 2's local IP address. It can be found in the network settings.

1. Clone the repo: 
``` 
git clone https://github.com/misshexa66/OunceCake.git
```

2. Run `node ouncecake.cjs <path-to-ns2jb-run>`. The output from ns2jb will tell you where this path is. (`Run folder: /home/hexa/projects/ns2jb2/runs/switch2-Icu7Jd`)

3. OunceCake will prompt you to pick a game. Pick one, then wait.

4. When OunceCake says `FTP up! ftp://<switch 2 ip>:2121`, use an FTP client to connect to your Switch 2's local IP address with port 2121. A username and password is not required.

## Licensing

OunceCake is licensed under the terms of the GPLv2 (because it borrows a tiny bit of code from [Atmosphere](https://github.com/Atmosphere-NX/Atmosphere))

You can find a copy of the license in the LICENSE file.
