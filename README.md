# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore
## BeastMaster NPC
- Latest build status with azerothcore: [![Build Status](https://github.com/azerothcore/mod-npc-beastmaster/workflows/core-build/badge.svg?branch=master&event=push)](https://github.com/azerothcore/mod-npc-beastmaster)

## Important notes

You have to use at least AzerothCore commit [3f0739f](https://github.com/azerothcore/azerothcore-wotlk/commit/3f0739f1c9a5289444ff9d62834b7ceb38879ba9).


## Description

This module allows each class (even Warlocks and Death Knights) to use hunter pets by adding a special NPC. This NPC provides the following options:
- teach the necessary hunter skills
- provide different pets (even exotic) which can be customized in the config file
- food for the pets
- stables (only for hunters)

The module can be configured to be restricted to hunters only. In this mode it can serve as a source for rare pets specified in the config file.


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
## Notice:

Due to the uniquiness of the module you will get this message on the worldconsole, but nothing is broken.
It is due to the npc not exactly having a gossip menu in the database, the script handles the gossip menu.

![image](https://user-images.githubusercontent.com/16887899/154327532-612b03d8-64f0-460e-8f4b-7cbfd31a7381.png)

Please add the adjustment to the conf to have that error message not show up:
```
#    Creatures.CustomIDs
#        Description: The list of custom creatures with gossip dialogues hardcoded in core,
#                     divided by "," without spaces.
#                     It is implied that you do not use for these NPC dialogs data from "gossip_menu" table.
#                     Server will skip these IDs during the definitions validation process.
#        Example:     Creatures.CustomIDs = "190010,55005,999991,25462,98888,601026" - Npcs for Transmog, Guild-zone, 1v1-arena modules
#                                                                               Skip Dk Module, Racial Trait Swap Modules
#        Default:     ""

Creatures.CustomIDs = "190010,55005,999991,25462,98888,601026"
```

## Installation

Clone Git repository:

```
cd <ACdir>
git clone https://github.com/azerothcore/mod-npc-beastmaster.git modules/mod-npc-beastmaster
```

Import SQL automatically:
```
cd <ACdir>
bash apps/db_assembler/db_assembler.sh
```
choose 8)

Import SQL manually:
```
cd <ACdir>
bash apps/db_assembler/db_assembler.sh
```
choose 4)
```
cd <ACdir>
mysql -P <DBport> -u <DPuser> --password=<DBpassword> world <env/dist/sql/world_custom.sql
```


## Edit module configuration (optional)

If you need to change the module configuration, go to your server configuration folder (where your `worldserver` or `worldserver.exe` is), copy `npc_beastmaster.conf.dist` to `npc_beastmaster.conf` and edit that new file.

(If using Docker, place the `npc_beastmaster.conf` file into your `azerothcore-wotlk\docker\worldserver\etc` folder.)

## Credits

* [Stoabrogga](https://github.com/Stoabrogga): further development
* [Talamortis](https://github.com/talamortis): further development
* [BarbzYHOOL](https://github.com/barbzyhool): support
* [StygianTheBest](http://stygianthebest.github.io): original author (this module is based on v2017.09.03)

AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/) - [discord chat community](https://discord.gg/PaqQRkd)


## License
This code and content is released under the [GNU AGPL v3](https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3).
