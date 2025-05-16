/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright
 * information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but without
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License
 * for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NpcBeastmaster.h"
#include "Chat.h"
#include "Common.h"
#include "Config.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptMgr.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "WorldSession.h"
#include <fstream>
#include <locale>
#include <map>
#include <mutex>
#include <regex>
#include <sstream>
#include <sys/stat.h>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Beastmaster NPC entry (change if you use a different NPC)
constexpr uint32 BEASTMASTER_NPC_ENTRY = 601026;
// Beastmaster Whistle item entry (change if you use a different item)
constexpr uint32 BEASTMASTER_WHISTLE_ITEM = 21744;

namespace BeastmasterDB
{
    // Handles all database operations related to the Beastmaster module.
    bool TrackTamedPet(Player *player, uint32 creatureEntry,
                       std::string const &petName)
    {
        // Check if already tracked
        QueryResult result =
            CharacterDatabase.Query("SELECT 1 FROM beastmaster_tamed_pets WHERE "
                                    "owner_guid = {} AND entry = {}",
                                    player->GetGUID().GetCounter(), creatureEntry);

        if (result)
            return false; // Already tracked

        CharacterDatabase.Execute("INSERT INTO beastmaster_tamed_pets (owner_guid, "
                                  "entry, name) VALUES ({}, {}, '{}')",
                                  player->GetGUID().GetCounter(), creatureEntry,
                                  petName.c_str());
        return true;
    }
} // namespace BeastmasterDB

namespace
{
    // List of hunter spells to grant/remove for non-hunters adopting pets.
    std::vector<uint32> HunterSpells = {883, 982, 2641, 6991,
                                        48990, 1002, 1462, 6197};

    using PetList = std::vector<PetInfo>;

    PetList allPets;
    PetList normalPets;
    PetList exoticPets;
    PetList rarePets;
    PetList rareExoticPets;

    std::set<uint32> rarePetEntries;
    std::set<uint32> rareExoticPetEntries;

    // Cached config options for performance and consistency.
    struct BeastmasterConfig
    {
        bool hunterOnly = true;
        bool allowExotic = false;
        bool keepPetHappy = false;
        uint32 minLevel = 10;
        bool hunterBeastMasteryRequired = true;
        bool trackTamedPets = false;
    } beastmasterConfig;

    enum PetGossip
    {
        PET_BEASTMASTER_HOWL = 9036,
        PET_PAGE_SIZE = 13,
        PET_PAGE_START_PETS = 501,
        PET_PAGE_START_EXOTIC_PETS = 601,
        PET_PAGE_START_RARE_PETS = 701,
        PET_PAGE_START_RARE_EXOTIC_PETS = 801,
        PET_PAGE_MAX = 901,
        PET_MAIN_MENU = 50,
        PET_REMOVE_SKILLS = 80,
        PET_GOSSIP_HELLO = 601026,
        PET_GOSSIP_BROWSE = 601027,
        PET_TRACKED_PETS_MENU = 1000 // New action for tracked pets menu
    };

    // PetSpells
    constexpr auto PET_SPELL_CALL_PET = 883;
    constexpr auto PET_SPELL_TAME_BEAST = 13481;
    constexpr auto PET_SPELL_BEAST_MASTERY = 53270;
    constexpr auto PET_MAX_HAPPINESS = 1048000;

    std::unordered_map<uint32, PetInfo> allPetsByEntry;

    // Mutex for thread safety when accessing/modifying global pet lists/maps
    std::mutex petsMutex;

    // Cache for tracked pets
    std::unordered_map<uint64,
                       std::vector<std::tuple<uint32, std::string, std::string>>>
        trackedPetsCache;
    std::mutex trackedPetsCacheMutex;
} // namespace

enum BeastmasterEvents
{
    BEASTMASTER_EVENT_EAT = 1
};

// Add these new actions for tracked pets
enum TrackedPetActions
{
    PET_TRACKED_SUMMON = 2000,
    PET_TRACKED_RENAME = 3000,
    PET_TRACKED_DELETE = 4000,
    PET_TRACKED_PAGE_SIZE = 10
};

// Add a new enum for rename dialog
enum
{
    PET_TRACKED_RENAME_PROMPT = 5000
};

// Global profanity list and last modification time
static std::unordered_set<std::string> sProfanityList;
static time_t sProfanityListMTime = 0;

// Helper to get file modification time
static time_t GetFileMTime(const std::string &path)
{
    struct stat statbuf;
    if (stat(path.c_str(), &statbuf) == 0)
        return statbuf.st_mtime; // <-- use st_mtime
    return 0;
}

// Load or reload profanity words from conf/profanity.txt if changed
static void LoadProfanityListIfNeeded()
{
    const std::string path = "modules/mod-npc-beastmaster/conf/profanity.txt";
    time_t mtime = GetFileMTime(path);
    if (mtime == 0)
        return; // file missing

    if (mtime == sProfanityListMTime && !sProfanityList.empty())
        return; // already loaded and unchanged

    sProfanityList.clear();
    std::ifstream f(path);
    if (!f.is_open())
    {
        LOG_WARN("module", "Beastmaster: Could not open profanity.txt, skipping "
                           "profanity filter.");
        return;
    }
    std::string word;
    while (std::getline(f, word))
    {
        std::transform(word.begin(), word.end(), word.begin(), ::tolower);
        if (!word.empty())
            sProfanityList.insert(word);
    }
    sProfanityListMTime = mtime;
    // Only log once per reload, not per word
    LOG_INFO("module", "Beastmaster: Loaded {} profane words (mtime={})",
             sProfanityList.size(), long(mtime));
}

// Enhanced profanity check: any profane substring, always up-to-date
static bool IsProfane(const std::string &name)
{
    LoadProfanityListIfNeeded();
    std::string lower = name;
    std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
    for (auto const &bad : sProfanityList)
        if (lower.find(bad) != std::string::npos)
            return true;
    return false;
}

// Enhanced pet name validator:
// - 2–16 chars
// - Letters, spaces and dashes only
// - No leading/trailing spaces
static bool IsValidPetName(const std::string &name)
{
    if (name.size() < 2 || name.size() > 16)
        return false;
    // no leading/trailing spaces
    if (std::isspace(name.front()) || std::isspace(name.back()))
        return false;
    // match allowed chars
    static const std::regex allowed("^[A-Za-z][A-Za-z \\-']*[A-Za-z]$");
    return std::regex_match(name, allowed);
}

// Helper to load rare pet IDs from config
static std::set<uint32> ParseEntryList(const std::string &csv)
{
    std::set<uint32> result;
    std::stringstream ss(csv);
    std::string item;
    while (std::getline(ss, item, ','))
    {
        try
        {
            result.insert(std::stoul(item));
        }
        catch (...)
        {
        }
    }
    return result;
}

// Helper to get PetInfo from entry
static const PetInfo *FindPetInfo(uint32 entry)
{
    std::lock_guard<std::mutex> lock(petsMutex);
    auto it = allPetsByEntry.find(entry);
    return it != allPetsByEntry.end() ? &it->second : nullptr;
}

class BeastmasterBool : public DataMap::Base
{
public:
    explicit BeastmasterBool(bool v) : value(v) {}
    bool value;
};

class BeastmasterUInt32 : public DataMap::Base
{
public:
    explicit BeastmasterUInt32(uint32 v) : value(v) {}
    uint32 value;
};

class BeastmasterPetMap : public DataMap::Base
{
public:
    std::map<uint32, uint32> map;
    BeastmasterPetMap(const std::map<uint32, uint32> &m) : map(m) {}
};

/*static*/ NpcBeastmaster *NpcBeastmaster::instance()
{
    static NpcBeastmaster instance;
    return &instance;
}

// Loads all configuration options and pets from SQL and config.
void NpcBeastmaster::LoadSystem(bool /*reload = false*/)
{
    std::lock_guard<std::mutex> lock(petsMutex);

    beastmasterConfig.hunterOnly =
        sConfigMgr->GetOption<bool>("BeastMaster.HunterOnly", true);
    beastmasterConfig.allowExotic =
        sConfigMgr->GetOption<bool>("BeastMaster.AllowExotic", false);
    beastmasterConfig.keepPetHappy =
        sConfigMgr->GetOption<bool>("BeastMaster.KeepPetHappy", false);
    beastmasterConfig.minLevel =
        sConfigMgr->GetOption<uint32>("BeastMaster.MinLevel", 10);
    beastmasterConfig.hunterBeastMasteryRequired = sConfigMgr->GetOption<uint32>(
        "BeastMaster.HunterBeastMasteryRequired", true);
    beastmasterConfig.trackTamedPets =
        sConfigMgr->GetOption<bool>("BeastMaster.TrackTamedPets", false);

    // Parse rare pet entry lists from config
    rarePetEntries = ParseEntryList(
        sConfigMgr->GetOption<std::string>("BeastMaster.RarePets", ""));
    rareExoticPetEntries = ParseEntryList(
        sConfigMgr->GetOption<std::string>("BeastMaster.RareExoticPets", ""));

    // Load pets from SQL
    allPets.clear();
    normalPets.clear();
    exoticPets.clear();
    rarePets.clear();
    rareExoticPets.clear();
    allPetsByEntry.clear();

    QueryResult result = WorldDatabase.Query(
        "SELECT entry, name, family, rarity FROM beastmaster_tames");
    if (!result)
    {
        LOG_ERROR(
            "module",
            "Beastmaster: Could not load tames from beastmaster_tames table!");
        return;
    }

    do
    {
        Field *fields = result->Fetch();
        PetInfo info;
        info.entry = fields[0].Get<uint32>();
        info.name = fields[1].Get<std::string>();
        info.family = fields[2].Get<uint32>();
        info.rarity = fields[3].Get<std::string>();

        // Set icon based on family or entry
        switch (info.family)
        {
        case 1: // Wolf
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 2: // Cat
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 3: // Bear
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 4: // Boar
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 7: // Carrion Bird
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 8: // Crocolisk
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 9: // Gorilla
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 10: // Crab
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 15: // Turtle
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 20: // Raptor
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 21: // Tallstrider, Devilsaur
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 30: // Dragonhawk, Rhino
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 24: // Silithid
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 31: // Worm
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 25: // Core Hound
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 34: // Spirit Beast
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        case 27: // Wind Serpent
            info.icon = GOSSIP_ICON_TRAINER;
            break;
        default:
            info.icon = GOSSIP_ICON_VENDOR;
            break;
        }

        allPets.push_back(info);
        allPetsByEntry[info.entry] = info;

        // Classify
        if (rarePetEntries.count(info.entry))
            rarePets.push_back(info);
        else if (rareExoticPetEntries.count(info.entry))
            rareExoticPets.push_back(info);
        else if (info.rarity == "exotic")
            exoticPets.push_back(info);
        else
            normalPets.push_back(info);
    } while (result->NextRow());
}

// Shows the main gossip menu to the player.
void NpcBeastmaster::ShowMainMenu(Player *player, Creature *creature)
{
    // Restrict to hunters if configured.
    if (beastmasterConfig.hunterOnly && player->getClass() != CLASS_HUNTER)
    {
        if (creature)
            creature->Whisper("I am sorry, but pets are for hunters only.",
                              LANG_UNIVERSAL, player);
        else
            ChatHandler(player->GetSession())
                .PSendSysMessage("I am sorry, but pets are for hunters only.");
        return;
    }

    // Enforce minimum level requirement.
    if (player->GetLevel() < beastmasterConfig.minLevel &&
        beastmasterConfig.minLevel != 0)
    {
        std::string messageExperience = Acore::StringFormat(
            "Sorry {}, but you must reach level {} before adopting a pet.",
            player->GetName(), beastmasterConfig.minLevel);
        if (creature)
            creature->Whisper(messageExperience.c_str(), LANG_UNIVERSAL, player);
        else
            ChatHandler(player->GetSession())
                .PSendSysMessage("%s", messageExperience.c_str());
        return;
    }

    ClearGossipMenuFor(player);

    // Main menu options.
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Pets",
                     GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS);
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Pets",
                     GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS);

    // Exotic pets menu logic.
    if (beastmasterConfig.allowExotic ||
        player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
        player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
    {
        if (player->getClass() != CLASS_HUNTER)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
        }
        else if (!beastmasterConfig.hunterBeastMasteryRequired ||
                 player->HasTalent(PET_SPELL_BEAST_MASTERY,
                                   player->GetActiveSpec()))
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
        }
    }

    // Remove pet skills (not for hunters).
    if (player->getClass() != CLASS_HUNTER &&
        player->HasSpell(PET_SPELL_CALL_PET))
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Unlearn Hunter Abilities",
                         GOSSIP_SENDER_MAIN, PET_REMOVE_SKILLS);

    // Add tracked pets menu if enabled (for all classes now)
    if (beastmasterConfig.trackTamedPets)
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "My Tamed Pets",
                         GOSSIP_SENDER_MAIN, PET_TRACKED_PETS_MENU);

    // Stables for hunters only.
    if (player->getClass() == CLASS_HUNTER)
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Visit Stable",
                         GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

    // Pet Food Vendor.
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Buy Pet Food",
                     GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);

    // Add option to receive the Beastmaster Whistle
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Get Beastmaster Whistle",
                     GOSSIP_SENDER_MAIN, 90010);

    if (creature)
        SendGossipMenuFor(player, PET_GOSSIP_HELLO, creature->GetGUID());
    else
        SendGossipMenuFor(player, PET_GOSSIP_HELLO, ObjectGuid::Empty);

    // Play sound effect.
    player->PlayDirectSound(PET_BEASTMASTER_HOWL);
}

// Handles gossip menu selections.
void NpcBeastmaster::GossipSelect(Player *player, Creature *creature,
                                  uint32 action)
{
    ClearGossipMenuFor(player);

    if (action == PET_MAIN_MENU)
    {
        ShowMainMenu(player, creature);
    }
    else if (action >= PET_PAGE_START_PETS &&
             action < PET_PAGE_START_EXOTIC_PETS)
    {
        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN,
                         PET_MAIN_MENU);
        int page = action - PET_PAGE_START_PETS + 1;
        int maxPage = normalPets.size() / PET_PAGE_SIZE +
                      (normalPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page);

        AddPetsToGossip(player, normalPets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_EXOTIC_PETS &&
             action < PET_PAGE_START_RARE_PETS)
    {
        if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
              player->HasTalent(PET_SPELL_BEAST_MASTERY,
                                player->GetActiveSpec())))
        {
            player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
            std::ostringstream messageLearn;
            messageLearn << "I have taught you the art of Beast Mastery, "
                         << player->GetName() << ".";
            creature->Whisper(messageLearn.str().c_str(), LANG_UNIVERSAL, player);
        }

        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN,
                         PET_MAIN_MENU);
        int page = action - PET_PAGE_START_EXOTIC_PETS + 1;
        int maxPage = exoticPets.size() / PET_PAGE_SIZE +
                      (exoticPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..",
                             GOSSIP_SENDER_MAIN,
                             PET_PAGE_START_EXOTIC_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS + page);

        AddPetsToGossip(player, exoticPets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_RARE_PETS &&
             action < PET_PAGE_START_RARE_EXOTIC_PETS)
    {
        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN,
                         PET_MAIN_MENU);
        int page = action - PET_PAGE_START_RARE_PETS + 1;
        int maxPage = rarePets.size() / PET_PAGE_SIZE +
                      (rarePets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..",
                             GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page);

        AddPetsToGossip(player, rarePets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_RARE_EXOTIC_PETS &&
             action < PET_PAGE_MAX)
    {
        if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
              player->HasTalent(PET_SPELL_BEAST_MASTERY,
                                player->GetActiveSpec())))
        {
            player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
            std::ostringstream messageLearn;
            messageLearn << "I have taught you the art of Beast Mastery, "
                         << player->GetName() << ".";
            creature->Whisper(messageLearn.str().c_str(), LANG_UNIVERSAL, player);
        }

        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN,
                         PET_MAIN_MENU);
        int page = action - PET_PAGE_START_RARE_EXOTIC_PETS + 1;
        int maxPage = rareExoticPets.size() / PET_PAGE_SIZE +
                      (rareExoticPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..",
                             GOSSIP_SENDER_MAIN,
                             PET_PAGE_START_RARE_EXOTIC_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..",
                             GOSSIP_SENDER_MAIN,
                             PET_PAGE_START_RARE_EXOTIC_PETS + page);

        AddPetsToGossip(player, rareExoticPets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action == PET_REMOVE_SKILLS)
    {
        // Remove pet and granted skills for non-hunters.
        for (auto spell : HunterSpells)
            player->removeSpell(spell, SPEC_MASK_ALL, false);

        player->removeSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
        CloseGossipMenuFor(player);
    }
    else if (action == GOSSIP_OPTION_STABLEPET)
    {
        // Open stable window.
        player->GetSession()->SendStablePet(creature->GetGUID());
    }
    else if (action == GOSSIP_OPTION_VENDOR)
    {
        // Open vendor window.
        player->GetSession()->SendListInventory(creature->GetGUID());
    }
    else if (action >= PET_TRACKED_PETS_MENU && action < PET_TRACKED_SUMMON)
    {
        uint32 page = action - PET_TRACKED_PETS_MENU + 1;
        ShowTrackedPetsMenu(player, creature, page);
        return;
    }
    else if (action >= PET_TRACKED_SUMMON && action < PET_TRACKED_RENAME)
    {
        uint32 idx = action - PET_TRACKED_SUMMON;
        auto *petMapWrap =
            player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
        if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
            return;
        uint32 entry = petMapWrap->map[idx];
        // Summon logic: create the pet for the player
        if (player->IsExistPet())
        {
            LOG_INFO("module", "DEBUG: Player {} IsExistPet()=true, Pet GUID: {}",
                     player->GetGUID().GetCounter(),
                     player->GetPet() ? player->GetPet()->GetGUID().GetCounter() : 0);
            creature->Whisper("First you must abandon or stable your current pet!",
                              LANG_UNIVERSAL, player);
            CloseGossipMenuFor(player);
            return;
        }
        else
        {
            Pet *pet = player->CreatePet(entry, PET_SPELL_CALL_PET);
            if (pet)
            {
                // Fetch the custom name from beastmaster_tamed_pets
                QueryResult nameResult = CharacterDatabase.Query(
                    "SELECT name FROM beastmaster_tamed_pets WHERE owner_guid = {} AND entry = {}",
                    player->GetGUID().GetCounter(), entry);
                if (nameResult)
                {
                    std::string customName = (*nameResult)[0].Get<std::string>();
                    pet->SetName(customName);
                }
                pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);
                creature->Whisper("Your tracked pet has been summoned!", LANG_UNIVERSAL, player);
            }
            else
            {
                creature->Whisper("Failed to summon pet.", LANG_UNIVERSAL, player);
            }
        }
        CloseGossipMenuFor(player);
        return;
    }
    else if (action >= PET_TRACKED_RENAME && action < PET_TRACKED_DELETE)
    {
        uint32 idx = action - PET_TRACKED_RENAME;
        auto *petMapWrap =
            player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
        if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
            return;
        uint32 entry = petMapWrap->map[idx];
        // Instantly prompt for new name (no confirmation, just set state)
        player->CustomData.Set("BeastmasterRenamePetEntry",
                               new BeastmasterUInt32(entry));
        player->CustomData.Set("BeastmasterExpectRename",
                               new BeastmasterBool(true));
        ChatHandler(player->GetSession())
            .PSendSysMessage("To rename your pet, type: .petname <newname> in chat. To cancel, type: .cancel");
        if (creature)
            creature->Whisper("To rename your pet, type: .petname <newname> in chat. To cancel, type: .cancel",
                              LANG_UNIVERSAL, player);
        CloseGossipMenuFor(player);
        return;
    }
    else if (action >= PET_TRACKED_DELETE &&
             action < PET_TRACKED_DELETE + 1000)
    {
        uint32 idx = action - PET_TRACKED_DELETE;
        auto *petMapWrap =
            player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
        if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
            return;
        uint32 entry = petMapWrap->map[idx];

        // Instantly delete the pet (no confirmation)
        CharacterDatabase.Execute("DELETE FROM beastmaster_tamed_pets WHERE "
                                  "owner_guid = {} AND entry = {}",
                                  player->GetGUID().GetCounter(), entry);

        sNpcBeastMaster->ClearTrackedPetsCache(player);

        ChatHandler(player->GetSession())
            .PSendSysMessage("Tracked pet deleted (entry {}).", entry);
        LOG_INFO("module", "Beastmaster: Player {} deleted tracked pet (entry {}).",
                 player->GetGUID().GetCounter(), entry);

        // Recalculate the page number to avoid empty pages
        uint32 totalPets = 0;
        QueryResult result = CharacterDatabase.Query(
            "SELECT COUNT(*) FROM beastmaster_tamed_pets WHERE owner_guid = {}",
            player->GetGUID().GetCounter());
        if (result)
            totalPets = (*result)[0].Get<uint32>();

        uint32 page = (idx / PET_TRACKED_PAGE_SIZE) + 1;
        uint32 maxPage =
            (totalPets + PET_TRACKED_PAGE_SIZE - 1) / PET_TRACKED_PAGE_SIZE;
        if (page > maxPage && maxPage > 0)
            page = maxPage;
        if (page == 0)
            page = 1;

        ShowTrackedPetsMenu(player, creature, page);
        return;
    }
    else if (action == 90010) // Give Beastmaster Whistle
    {
        if (!player->HasItemCount(BEASTMASTER_WHISTLE_ITEM, 1))
        {
            player->AddItem(BEASTMASTER_WHISTLE_ITEM, 1);
            creature->Whisper("You have received a Beastmaster Whistle!",
                              LANG_UNIVERSAL, player);
        }
        else
        {
            creature->Whisper("You already have a Beastmaster Whistle.",
                              LANG_UNIVERSAL, player);
        }
        ShowMainMenu(player, creature);
        return;
    }

    // Adopt pet if action is in the correct range.
    if (action >= PET_PAGE_MAX)
        CreatePet(player, creature, action);
}

// Handles the creation/adoption of a pet for the player.
void NpcBeastmaster::CreatePet(Player *player, Creature *creature,
                               uint32 action)
{
    uint32 petEntry = action - PET_PAGE_MAX;
    const PetInfo *info = FindPetInfo(petEntry);

    // Check if player already has a pet.
    if (player->IsExistPet())
    {
        LOG_INFO("module", "DEBUG: Player {} IsExistPet()=true, Pet GUID: {}",
                 player->GetGUID().GetCounter(),
                 player->GetPet() ? player->GetPet()->GetGUID().GetCounter() : 0);
        creature->Whisper("First you must abandon or stable your current pet!",
                          LANG_UNIVERSAL, player);
        CloseGossipMenuFor(player);
        return;
    }

    // Prevent non-hunters from adopting exotic pets unless allowed by config
    if (info && info->rarity == "exotic" && player->getClass() != CLASS_HUNTER &&
        !beastmasterConfig.allowExotic)
    {
        creature->Whisper("Only hunters can adopt exotic pets.", LANG_UNIVERSAL,
                          player);
        CloseGossipMenuFor(player);
        return;
    }

    // For hunters, check Beast Mastery if required
    if (info && info->rarity == "exotic" && player->getClass() == CLASS_HUNTER &&
        beastmasterConfig.hunterBeastMasteryRequired)
    {
        if (!player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
        {
            creature->Whisper(
                "You need the Beast Mastery talent to adopt exotic pets.",
                LANG_UNIVERSAL, player);
            CloseGossipMenuFor(player);
            return;
        }
    }

    // Create tamed creature.
    Pet *pet = player->CreatePet(petEntry, player->getClass() == CLASS_HUNTER
                                               ? PET_SPELL_TAME_BEAST
                                               : PET_SPELL_CALL_PET);
    if (!pet)
    {
        creature->Whisper("First you must abandon or stable your current pet!",
                          LANG_UNIVERSAL, player);
        return;
    }

    // Track tamed pet if enabled in config.
    if (beastmasterConfig.trackTamedPets)
        BeastmasterDB::TrackTamedPet(player, petEntry, pet->GetName());

    // Set Pet Happiness.
    pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);

    // Learn Hunter Abilities (only for non-hunters).
    if (player->getClass() != CLASS_HUNTER)
    {
        if (!player->HasSpell(PET_SPELL_CALL_PET))
        {
            for (auto const &spell : HunterSpells)
                if (!player->HasSpell(spell))
                    player->learnSpell(spell);
        }
    }

    // Farewell message.
    std::string messageAdopt =
        Acore::StringFormat("A fine choice {}! Take good care of your {} and you "
                            "will never face your enemies alone.",
                            player->GetName(), pet->GetName());
    creature->Whisper(messageAdopt.c_str(), LANG_UNIVERSAL, player);
    CloseGossipMenuFor(player);
}

// Adds pets to the gossip menu for the given page.
void NpcBeastmaster::AddPetsToGossip(Player *player,
                                     std::vector<PetInfo> const &pets,
                                     uint32 page)
{
    std::set<uint32> tamedEntries;
    QueryResult result = CharacterDatabase.Query(
        "SELECT entry FROM beastmaster_tamed_pets WHERE owner_guid = {}",
        player->GetGUID().GetCounter());
    if (result)
    {
        do
        {
            Field *fields = result->Fetch();
            tamedEntries.insert(fields[0].Get<uint32>());
        } while (result->NextRow());
    }

    uint32 count = 1;
    for (const auto &pet : pets)
    {
        if (count > (page - 1) * PET_PAGE_SIZE && count <= page * PET_PAGE_SIZE)
        {
            if (tamedEntries.count(pet.entry))
            {
                AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                                 pet.name + " (Already Tamed)", GOSSIP_SENDER_MAIN,
                                 0); // 0 = no action
            }
            else
            {
                AddGossipItemFor(player, pet.icon, pet.name, GOSSIP_SENDER_MAIN,
                                 pet.entry + PET_PAGE_MAX);
            }
        }
        count++;
    }
}

// Clears the tracked pets cache for a specific player
void NpcBeastmaster::ClearTrackedPetsCache(Player *player)
{
    std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
    trackedPetsCache.erase(player->GetGUID().GetRawValue());
    player->CustomData.Erase("BeastmasterMenuPetMap");
}

// Show the tracked pets menu for the player, with pagination and actions
void NpcBeastmaster::ShowTrackedPetsMenu(Player *player, Creature *creature,
                                         uint32 page /*= 1*/)
{
    ClearGossipMenuFor(player);

    uint64 guid = player->GetGUID().GetRawValue();
    std::vector<std::tuple<uint32, std::string, std::string>> *trackedPetsPtr =
        nullptr;

    {
        std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
        auto it = trackedPetsCache.find(guid);
        if (it != trackedPetsCache.end())
        {
            trackedPetsPtr = &it->second;
        }
    }

    if (!trackedPetsPtr)
    {
        std::vector<std::tuple<uint32, std::string, std::string>> trackedPets;
        QueryResult result = CharacterDatabase.Query(
            "SELECT entry, name, date_tamed FROM beastmaster_tamed_pets WHERE "
            "owner_guid = {} ORDER BY date_tamed DESC",
            player->GetGUID().GetCounter());

        if (result)
        {
            do
            {
                Field *fields = result->Fetch();
                uint32 entry = fields[0].Get<uint32>();
                std::string name = fields[1].Get<std::string>();
                std::string date = fields[2].Get<std::string>();
                trackedPets.emplace_back(entry, name, date);
            } while (result->NextRow());
        }
        {
            std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
            trackedPetsCache[guid] = std::move(trackedPets);
            trackedPetsPtr = &trackedPetsCache[guid];
        }
    }

    const auto &trackedPets = *trackedPetsPtr;
    uint32 total = trackedPets.size();
    uint32 offset = (page - 1) * PET_TRACKED_PAGE_SIZE;
    uint32 shown = 0;

    std::map<uint32, uint32> menuPetIndexToEntry;

    // Build the menu for this page
    for (uint32 i = offset; i < total && shown < PET_TRACKED_PAGE_SIZE;
         ++i, ++shown)
    {
        const auto &petTuple = trackedPets[i];
        uint32 entry = std::get<0>(petTuple);
        const std::string &name = std::get<1>(petTuple);
        const PetInfo *info = FindPetInfo(entry);

        std::string label;
        if (info)
            label =
                Acore::StringFormat("{} [{}, {}]", name, info->name, info->rarity);
        else
            label = name;

        // Use shown as the unique index for this page
        uint32 idx = shown;
        menuPetIndexToEntry[idx] = entry;

        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Summon: " + label,
                         GOSSIP_SENDER_MAIN, PET_TRACKED_SUMMON + idx);
        AddGossipItemFor(player, GOSSIP_ICON_TRAINER, "Rename: " + label,
                         GOSSIP_SENDER_MAIN, PET_TRACKED_RENAME + idx);
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Delete: " + label,
                         GOSSIP_SENDER_MAIN, PET_TRACKED_DELETE + idx);
    }
    // Store the mapping for this menu page
    player->CustomData.Set("BeastmasterMenuPetMap",
                           new BeastmasterPetMap(menuPetIndexToEntry));

    // Send the menu to the player
    if (creature)
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature);
    else
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, ObjectGuid::Empty);
}

// Keeps the pet happy if the config option is enabled.
void NpcBeastmaster::PlayerUpdate(Player *player)
{
    if (beastmasterConfig.keepPetHappy && player->GetPet())
    {
        Pet *pet = player->GetPet();
        if (pet->getPetType() == HUNTER_PET)
            pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);
    }
}

// Chat handler to process the rename and delete confirmations
class BeastMaster_CreatureScript : public CreatureScript
{
public:
    BeastMaster_CreatureScript() : CreatureScript("BeastMaster") {}

    bool OnGossipHello(Player *player, Creature *creature) override
    {
        sNpcBeastMaster->ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player *player, Creature *creature, uint32 /*sender*/,
                        uint32 action) override
    {
        sNpcBeastMaster->GossipSelect(player, creature, action);
        return true;
    }

    struct beastmasterAI : public ScriptedAI
    {
        beastmasterAI(Creature *creature) : ScriptedAI(creature) {}

        void Reset() override
        {
            events.ScheduleEvent(BEASTMASTER_EVENT_EAT, urand(30000, 90000));
        }

        void UpdateAI(uint32 diff) override
        {
            events.Update(diff);

            switch (events.ExecuteEvent())
            {
            case BEASTMASTER_EVENT_EAT:
                me->HandleEmoteCommand(EMOTE_ONESHOT_EAT_NO_SHEATHE);
                events.ScheduleEvent(BEASTMASTER_EVENT_EAT, urand(30000, 90000));
                break;
            }
        }

    private:
        EventMap events;
    };

    CreatureAI *GetAI(Creature *creature) const override
    {
        return new beastmasterAI(creature);
    }
};

class BeastMaster_WorldScript : public WorldScript
{
public:
    BeastMaster_WorldScript()
        : WorldScript("BeastMaster_WorldScript",
                      {WORLDHOOK_ON_BEFORE_CONFIG_LOAD}) {}

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        sNpcBeastMaster->LoadSystem();
    }
};

class BeastMaster_PlayerScript : public PlayerScript
{
public:
    BeastMaster_PlayerScript()
        : PlayerScript("BeastMaster_PlayerScript",
                       {PLAYERHOOK_ON_BEFORE_UPDATE,
                        PLAYERHOOK_ON_BEFORE_LOAD_PET_FROM_DB,
                        PLAYERHOOK_ON_BEFORE_GUARDIAN_INIT_STATS_FOR_LEVEL}) {}

    void OnPlayerBeforeUpdate(Player *player, uint32 /*p_time*/) override
    {
        sNpcBeastMaster->PlayerUpdate(player);
    }

    void OnPlayerBeforeLoadPetFromDB(Player * /*player*/, uint32 & /*petentry*/,
                                     uint32 & /*petnumber*/, bool & /*current*/,
                                     bool &forceLoadFromDB) override
    {
        forceLoadFromDB = true;
    }

    void OnPlayerBeforeGuardianInitStatsForLevel(Player * /*player*/,
                                                 Guardian * /*guardian*/,
                                                 CreatureTemplate const *cinfo,
                                                 PetType &petType) override
    {
        if (cinfo->IsTameable(true))
            petType = HUNTER_PET;
    }
};

// Rename and cancel commands for pet renaming
class petname_CommandScript : public CommandScript
{
public:
    petname_CommandScript() : CommandScript("petname_CommandScript") {}

    Acore::ChatCommands::ChatCommandTable GetCommands() const override
    {
        using namespace Acore::ChatCommands;
        static ChatCommandTable commandTable = {
            {"petname", HandlePetnameCommand, SEC_PLAYER, Console::No},
            {"cancel", HandleCancelCommand, SEC_PLAYER, Console::No}};
        return commandTable;
    }

    static bool HandlePetnameCommand(ChatHandler *handler, std::string_view args)
    {
        Player *player = handler->GetSession()->GetPlayer();
        auto *expectRename = player->CustomData.Get<BeastmasterBool>("BeastmasterExpectRename");
        auto *renameEntry = player->CustomData.Get<BeastmasterUInt32>("BeastmasterRenamePetEntry");
        if (!expectRename || !expectRename->value || !renameEntry)
        {
            handler->PSendSysMessage("You are not renaming a pet right now. Use the Beastmaster NPC to start renaming.");
            return true;
        }

        std::string newName(args);
        while (!newName.empty() && std::isspace(newName.front()))
            newName.erase(newName.begin());
        while (!newName.empty() && std::isspace(newName.back()))
            newName.pop_back();

        if (newName.empty())
        {
            handler->PSendSysMessage("Usage: .petname <newname>");
            return true;
        }

        if (!IsValidPetName(newName) || IsProfane(newName))
        {
            handler->PSendSysMessage("Invalid or profane pet name. Please try again with .petname <newname>.");
            return true;
        }

        CharacterDatabase.Execute(
            "UPDATE beastmaster_tamed_pets SET name = '{}' WHERE owner_guid = {} AND entry = {}",
            newName, player->GetGUID().GetCounter(), renameEntry->value);

        player->CustomData.Erase("BeastmasterExpectRename");
        player->CustomData.Erase("BeastmasterRenamePetEntry");

        handler->PSendSysMessage("Pet renamed to '{}'.", newName);
        sNpcBeastMaster->ClearTrackedPetsCache(player);
        return true;
    }

    static bool HandleCancelCommand(ChatHandler *handler, std::string_view /*args*/)
    {
        Player *player = handler->GetSession()->GetPlayer();
        auto *expectRename = player->CustomData.Get<BeastmasterBool>("BeastmasterExpectRename");
        if (!expectRename || !expectRename->value)
        {
            handler->PSendSysMessage("You are not renaming a pet right now.");
            return true;
        }
        player->CustomData.Erase("BeastmasterExpectRename");
        player->CustomData.Erase("BeastmasterRenamePetEntry");
        handler->PSendSysMessage("Pet renaming cancelled.");
        return true;
    }
};

class BeastmasterWhistle_ItemScript : public ItemScript
{
public:
    BeastmasterWhistle_ItemScript()
        : ItemScript("BeastmasterWhistle_ItemScript") {}

    bool OnUse(Player *player, Item *, SpellCastTargets const &) override
    {
        LOG_INFO("module", "DEBUG: Beastmaster Whistle OnUse triggered.");
        // Summon invisible Beastmaster NPC at player's location
        float x = player->GetPositionX();
        float y = player->GetPositionY();
        float z = player->GetPositionZ();
        float o = player->GetOrientation();
        Creature *tempNpc = player->SummonCreature(
            BEASTMASTER_NPC_ENTRY,                       // Your Beastmaster NPC entry
            x, y, z, o, TEMPSUMMON_TIMED_DESPAWN, 120000 // 2 minute
        );
        if (tempNpc)
        {
            player->TalkedToCreature(tempNpc->GetEntry(), tempNpc->GetGUID());
        }
        else
        {
            ChatHandler(player->GetSession())
                .PSendSysMessage(
                    "Failed to summon the Beastmaster. Please contact an admin.");
            LOG_ERROR("module", "BeastmasterWhistle: Failed to summon NPC {}.", BEASTMASTER_NPC_ENTRY);
        }
        return true;
    }
};

// Register the script in your module loader:
void Addmod_npc_beastmasterScripts()
{
    new BeastMaster_WorldScript();
    new BeastMaster_CreatureScript();
    new BeastMaster_PlayerScript();
    new BeastmasterWhistle_ItemScript();
    new petname_CommandScript(); // Register the .petname and .cancel commands
}
