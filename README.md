# BeastMaster NPC


## Description

This module allows each class (even Warlocks and Death Knights) to use hunter pets by adding a special NPC. This NPC provides the following options:
- teach the necessary hunter skills
- provide different pets (even exotic) which can be customized in the config file
- food for the pets
- stables (only for hunters)

The module can be configured to be restricted for hunters only. In this mode it can serve as a source for rare pets specified in the config file.


## How to use ingame

As GM:
- add NPC permanently:
 ```
 .npc add 601026
 ```
- add NPC temporarily:
 ```
 .npc add temp 601026
 ```


## Requirements

- AzerothCore v1.0.4+


## Installation

Import SQL automatically:
```
bash <ACdir>/apps/db_assembler/db_assembler.sh
```
choose 8)

Import SQL manually:
```
bash <ACdir>/apps/db_assembler/db_assembler.sh
```
choose 4)
```
cd <ACdir>/env/dist/sql
mysql -P <DBport> -u <DPuser> --password=<DBpassword> world <world_custom.sql
```

Patch Pet.cpp (recommended; otherwise you'll get errors in the logs and the Death Knight will be unable to use hunter pets)
```
cd <ACdir>
patch -l src/server/game/Entities/Pet/Pet.cpp <modules/mod-npcbeastmaster/Pet.cpp.beastmaster.patch
```
You can revert the patch as follows:
```
cd <ACdir>
patch -lR src/server/game/Entities/Pet/Pet.cpp <modules/mod-npcbeastmaster/Pet.cpp.beastmaster.patch
```
## Edit module configuration (optional)

If you need to change the module configuration, go to your server configuration folder (where your `worldserver` or `worldserver.exe` is), copy `npc_beastmaster.conf.dist` to `npc_beastmaster.conf` and edit that new file.


## Credits

* [Stoabrogga](https://github.com/Stoabrogga): further development
* [Talamortis](https://github.com/talamortis): further development
* [BarbzYHOOL](https://github.com/barbzyhool): support
* [StygianTheBest](http://stygianthebest.github.io): original author (this module is based on v2017.09.03)

AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/) - [discord chat community](https://discord.gg/PaqQRkd)


## License
This code and content is released under the [GNU AGPL v3](https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3).
