# ![logo](https://raw.githubusercontent.com/azerothcore/azerothcore.github.io/master/images/logo-github.png) AzerothCore
## BeastMaster NPC
- Latest build status with azerothcore: [![core-build](https://github.com/azerothcore/mod-npc-beastmaster/actions/workflows/core-build.yml/badge.svg)](https://github.com/azerothcore/mod-npc-beastmaster/actions/workflows/core-build.yml)

# AzerothCore - BeastMaster NPC

## Description

This module allows **all classes** (not just hunters) to adopt and use hunter pets by interacting with a special NPC. The NPC provides:
- Hunter skills (for non-hunters)
- Adoption of normal, rare, and exotic pets (all loaded from `beastmaster_tames` table)
- Pet food vendor
- Stables (for hunters)
- **Tracked pets system**: view, summon, rename, and delete your adopted pets

Players can also summon the Beastmaster NPC anywhere using chat commands.

## Important Notes

- You have to use at least AzerothCore commit [3f0739f](https://github.com/azerothcore/azerothcore-wotlk/commit/3f0739f1c9a5289444ff9d62834b7ceb38879ba9).
- The config file (`mod_npc_beastmaster.conf.dist`) controls rare and rare exotic pet highlighting by entry ID.
- Tracked pets are stored in the `beastmaster_tamed_pets` table in your characters database.
- Profanity filtering for pet names uses `conf/profanity.txt` (reloads automatically if changed).
- Tracked pets cache is session-based, thread-safe, and updates instantly after rename/delete.

## Tracked Pets Feature

- When you adopt a pet, it is automatically tracked and stored in the database.
- You can view your tracked pets from the BeastMaster NPC menu (all classes supported).
- For each tracked pet, you can:
  - **Summon**: Instantly summon the pet if you do not already have one out.
  - **Rename**: Select "Rename" and then type the new name in chat using the `.petname rename <name>` command. Type `.petname cancel` to abort.
  - **Delete**: Remove the pet from your tracked list (with confirmation).
- The tracked pets menu supports pagination if you have many pets.
- The menu displays each pet's name, date tamed, family, and rarity.
- Tracked pets update instantly after rename or delete.

## Pet Rename Commands

When you choose to rename a pet, you must use the following chat commands:
- `.petname rename <newname>` — Renames your selected pet to `<newname>`.
- `.petname cancel` — Cancels the renaming process.

If you type anything else, you will be reminded of these commands.

## How to use ingame

### Option 1: Chat Commands (Recommended)
Players can summon the Beastmaster anywhere using a chat command:
- `.beastmaster` — Summons the Beastmaster NPC at your location for 2 minutes

### Option 2: Spawn NPC Permanently
As GM:
- Add NPC permanently:
 ```
 .npc add 601026
 ```
- Add NPC temporarily:
 ```
 .npc add temp 601026
 ```

The NPC will appear as "White Fang" (entry: **601026**).

> **Note:**  
> The value `601026` is the default Beastmaster NPC entry used by this module.  
> If you wish to use a different NPC entry, update the `BeastMaster.NpcEntry` value in your `mod_npc_beastmaster.conf` file and adjust your database and configuration accordingly.

## Player Login Notice

When players log in, they will see a helpful message:
> **BeastMaster Commands Available!**  
> Type `.beastmaster` to summon the Beastmaster and adopt pets!

This notice can be disabled in the configuration file if desired.

## Notice:

Due to the uniqueness of the module you will get this message on the worldconsole, but nothing is broken.
It is due to the npc not exactly having a gossip menu in the database, the script handles the gossip menu.

![image](https://user-images.githubusercontent.com/16887899/154327532-612b03d8-64f0-460e-8f4b-7cbfd31a7381.png)

To suppress this warning, add the following to your worldserver config:
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

## Features

- Adopt normal, rare, and exotic pets
- Configurable restrictions (class, level, etc.)
- Optional tracking of all tamed pets (with menu)
- Pet food vendor and stable access
- Chat commands for easy access (`.beastmaster`)
- Login notification for new players
- **Tracked pets cache is session-based, thread-safe, and updates instantly after rename/delete**
- **Profanity filter for pet names auto-reloads if the file changes**
- **Rare and rare exotic pet highlighting is configurable by entry ID**
- **Tracked pets menu supports pagination for large collections**
- **Works with Docker deployments**

## Configuration

See `conf/mod_npc_beastmaster.conf.dist` for all options, including:
- Enable/disable login notifications
- Class restrictions
- Level requirements
- Exotic pet settings
- Pet tracking features

## SQL

Import the SQL files in `data/sql/db-world/` and `data/sql/db-characters/` to enable the NPC and tracked pets.

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
mysql -P <DBport> -u <DBuser> --password=<DBpassword> world <env/dist/sql/world_custom.sql
```

## Edit module configuration (optional)

If you need to change the module configuration, go to your server configuration folder (where your `worldserver` or `worldserver.exe` is), copy `mod_npc_beastmaster.conf.dist` to `mod_npc_beastmaster.conf` and edit that new file.

(If using Docker, place the `mod_npc_beastmaster.conf` file into your `azerothcore-wotlk/docker/worldserver/etc` folder.)

## Customization

- **NPC Entry:**  
  The default Beastmaster NPC entry is `601026`.  
  To use a different entry, set the `BeastMaster.NpcEntry` option in your `mod_npc_beastmaster.conf` file and update your database if needed.

- **Login Notifications:**  
  Login notifications can be enabled/disabled via the `BeastMaster.ShowLoginNotice` configuration option.

## Credits

* [BoiseComputer](https://github.com/BoiseComputer): further development
* [Stoabrogga](https://github.com/Stoabrogga): further development
* [Talamortis](https://github.com/talamortis): further development
* [BarbzYHOOL](https://github.com/barbzyhool): support
* [StygianTheBest](http://stygianthebest.github.io): original author (this module is based on v2017.09.03)

AzerothCore: [repository](https://github.com/azerothcore) - [website](http://azerothcore.org/) - [discord chat community](https://discord.gg/PaqQRkd)

## License
This code and content is released under the [GNU AGPL v3](https://github.com/azerothcore/azerothcore-wotlk/blob/master/LICENSE-AGPL3).
