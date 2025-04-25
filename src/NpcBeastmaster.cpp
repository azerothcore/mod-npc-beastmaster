/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but without
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NpcBeastmaster.h"
#include "Config.h"
#include "Pet.h"
#include "Player.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"
#include "WorldSession.h"
#include "Chat.h"
#include <map>
#include <vector>
#include <sstream>
#include <fstream>
#include <regex>
#include <unordered_set>
#include <locale>
#include <unordered_map>
#include <mutex>
#include <sys/stat.h>
#include "Common.h"

namespace BeastmasterDB
{
    // Handles all database operations related to the Beastmaster module.
    bool TrackTamedPet(Player *player, uint32 creatureEntry, std::string const &petName)
    {
        // Check if already tracked
        QueryResult result = CharacterDatabase.Query(
            "SELECT 1 FROM beastmaster_tamed_pets WHERE owner_guid = {} AND entry = {}",
            player->GetGUID().GetCounter(), creatureEntry);

        if (result)
            return false; // Already tracked

        CharacterDatabase.Execute(
            "INSERT INTO beastmaster_tamed_pets (owner_guid, entry, name) VALUES ({}, {}, '{}')",
            player->GetGUID().GetCounter(), creatureEntry, petName.c_str());
        return true;
    }
}

namespace
{
    // List of hunter spells to grant/remove for non-hunters adopting pets.
    std::vector<uint32> HunterSpells = {883, 982, 2641, 6991, 48990, 1002, 1462, 6197};

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
    std::unordered_map<uint64, std::vector<std::tuple<uint32, std::string, std::string>>> trackedPetsCache;
    std::mutex trackedPetsCacheMutex;
}

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
        LOG_WARN("module", "Beastmaster: Could not open profanity.txt, skipping profanity filter.");
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
    LOG_INFO("module", "Beastmaster: Loaded %zu profane words (mtime=%ld)", sProfanityList.size(), long(mtime));
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
static std::set<uint32>
ParseEntryList(const std::string &csv)
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

/*static*/ NpcBeastmaster *
NpcBeastmaster::instance()
{
    static NpcBeastmaster instance;
    return &instance;
}

// Loads all configuration options and pets from SQL and config.
void NpcBeastmaster::LoadSystem(bool /*reload = false*/)
{
    std::lock_guard<std::mutex> lock(petsMutex);

    beastmasterConfig.hunterOnly = sConfigMgr->GetOption<bool>("BeastMaster.HunterOnly", true);
    beastmasterConfig.allowExotic = sConfigMgr->GetOption<bool>("BeastMaster.AllowExotic", false);
    beastmasterConfig.keepPetHappy = sConfigMgr->GetOption<bool>("BeastMaster.KeepPetHappy", false);
    beastmasterConfig.minLevel = sConfigMgr->GetOption<uint32>("BeastMaster.MinLevel", 10);
    beastmasterConfig.hunterBeastMasteryRequired = sConfigMgr->GetOption<uint32>("BeastMaster.HunterBeastMasteryRequired", true);
    beastmasterConfig.trackTamedPets = sConfigMgr->GetOption<bool>("BeastMaster.TrackTamedPets", false);

    // Parse rare pet entry lists from config
    rarePetEntries = ParseEntryList(sConfigMgr->GetOption<std::string>("BeastMaster.RarePets", ""));
    rareExoticPetEntries = ParseEntryList(sConfigMgr->GetOption<std::string>("BeastMaster.RareExoticPets", ""));

    // Load pets from SQL
    allPets.clear();
    normalPets.clear();
    exoticPets.clear();
    rarePets.clear();
    rareExoticPets.clear();
    allPetsByEntry.clear();

    QueryResult result = WorldDatabase.Query("SELECT entry, name, family, rarity FROM beastmaster_tames");
    if (!result)
    {
        LOG_ERROR("module", "Beastmaster: Could not load tames from beastmaster_tames table!");
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
        creature->Whisper("I am sorry, but pets are for hunters only.", LANG_UNIVERSAL, player);
        return;
    }

    // Enforce minimum level requirement.
    if (player->GetLevel() < beastmasterConfig.minLevel && beastmasterConfig.minLevel != 0)
    {
        std::string messageExperience = Acore::StringFormat("Sorry {}, but you must reach level {} before adopting a pet.", player->GetName(), beastmasterConfig.minLevel);
        creature->Whisper(messageExperience.c_str(), LANG_UNIVERSAL, player);
        return;
    }

    ClearGossipMenuFor(player);

    // Main menu options.
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS);
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS);

    // Exotic pets menu logic.
    if (beastmasterConfig.allowExotic || player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
    {
        if (player->getClass() != CLASS_HUNTER)
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
        }
        else if (!beastmasterConfig.hunterBeastMasteryRequired || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
        {
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
            AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
        }
    }

    // Remove pet skills (not for hunters).
    if (player->getClass() != CLASS_HUNTER && player->HasSpell(PET_SPELL_CALL_PET))
        AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Unlearn Hunter Abilities", GOSSIP_SENDER_MAIN, PET_REMOVE_SKILLS);

    // Add tracked pets menu if enabled (for all classes now)
    if (beastmasterConfig.trackTamedPets)
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "My Tamed Pets", GOSSIP_SENDER_MAIN, PET_TRACKED_PETS_MENU);

    // Stables for hunters only.
    if (player->getClass() == CLASS_HUNTER)
        AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Visit Stable", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

    // Pet Food Vendor.
    AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Buy Pet Food", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);

    SendGossipMenuFor(player, PET_GOSSIP_HELLO, creature->GetGUID());

    // Play sound effect.
    player->PlayDirectSound(PET_BEASTMASTER_HOWL);
}

// Handles gossip menu selections.
void NpcBeastmaster::GossipSelect(Player *player, Creature *creature, uint32 action)
{
    ClearGossipMenuFor(player);

    if (action == PET_MAIN_MENU)
    {
        ShowMainMenu(player, creature);
    }
    else if (action >= PET_PAGE_START_PETS && action < PET_PAGE_START_EXOTIC_PETS)
    {
        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
        int page = action - PET_PAGE_START_PETS + 1;
        int maxPage = normalPets.size() / PET_PAGE_SIZE + (normalPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page);

        AddPetsToGossip(player, normalPets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_EXOTIC_PETS && action < PET_PAGE_START_RARE_PETS)
    {
        if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())))
        {
            player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
            std::ostringstream messageLearn;
            messageLearn << "I have taught you the art of Beast Mastery, " << player->GetName() << ".";
            creature->Whisper(messageLearn.str().c_str(), LANG_UNIVERSAL, player);
        }

        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
        int page = action - PET_PAGE_START_EXOTIC_PETS + 1;
        int maxPage = exoticPets.size() / PET_PAGE_SIZE + (exoticPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS + page);

        AddPetsToGossip(player, exoticPets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_RARE_PETS && action < PET_PAGE_START_RARE_EXOTIC_PETS)
    {
        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
        int page = action - PET_PAGE_START_RARE_PETS + 1;
        int maxPage = rarePets.size() / PET_PAGE_SIZE + (rarePets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page);

        AddPetsToGossip(player, rarePets, page);
        SendGossipMenuFor(player, PET_GOSSIP_BROWSE, creature->GetGUID());
    }
    else if (action >= PET_PAGE_START_RARE_EXOTIC_PETS && action < PET_PAGE_MAX)
    {
        if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())))
        {
            player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
            std::ostringstream messageLearn;
            messageLearn << "I have taught you the art of Beast Mastery, " << player->GetName() << ".";
            creature->Whisper(messageLearn.str().c_str(), LANG_UNIVERSAL, player);
        }

        AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
        int page = action - PET_PAGE_START_RARE_EXOTIC_PETS + 1;
        int maxPage = rareExoticPets.size() / PET_PAGE_SIZE + (rareExoticPets.size() % PET_PAGE_SIZE != 0);

        if (page > 1)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS + page - 2);

        if (page < maxPage)
            AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS + page);

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
        uint32 entry = action - PET_TRACKED_SUMMON;
        // Summon logic: create the pet for the player
        if (player->IsExistPet())
        {
            creature->Whisper("First you must abandon or stable your current pet!", LANG_UNIVERSAL, player);
        }
        else
        {
            Pet *pet = player->CreatePet(entry, PET_SPELL_CALL_PET);
            if (pet)
            {
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
        uint32 entry = action - PET_TRACKED_RENAME;
        player->CustomData.Set("BeastmasterRenamePetEntry", new BeastmasterUInt32(entry));
        player->CustomData.Set("BeastmasterExpectRename", new BeastmasterBool(true));
        this->HandleRenamePet(player, creature, entry);
        CloseGossipMenuFor(player);
        return;
    }
    else if (action >= PET_TRACKED_DELETE && action < PET_TRACKED_DELETE + 1000)
    {
        uint32 entry = action - PET_TRACKED_DELETE;
        player->CustomData.Set("BeastmasterDeletePetEntry", new BeastmasterUInt32(entry));
        player->CustomData.Set("BeastmasterExpectDeleteConfirm", new BeastmasterBool(true));
        this->HandleDeletePet(player, creature, entry);
        CloseGossipMenuFor(player);
        return;
    }

    // Adopt pet if action is in the correct range.
    if (action >= PET_PAGE_MAX)
        CreatePet(player, creature, action);
}

// Handles the creation/adoption of a pet for the player.
void NpcBeastmaster::CreatePet(Player *player, Creature *creature, uint32 action)
{
    // Check if player already has a pet.
    if (player->IsExistPet())
    {
        creature->Whisper("First you must abandon or stable your current pet!", LANG_UNIVERSAL, player);
        CloseGossipMenuFor(player);
        return;
    }

    // Create tamed creature.
    Pet *pet = player->CreatePet(action - PET_PAGE_MAX, player->getClass() == CLASS_HUNTER ? PET_SPELL_TAME_BEAST : PET_SPELL_CALL_PET);
    if (!pet)
    {
        creature->Whisper("First you must abandon or stable your current pet!", LANG_UNIVERSAL, player);
        return;
    }

    // Track tamed pet if enabled in config.
    if (beastmasterConfig.trackTamedPets)
        BeastmasterDB::TrackTamedPet(player, action - PET_PAGE_MAX, pet->GetName());

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
    std::string messageAdopt = Acore::StringFormat("A fine choice {}! Take good care of your {} and you will never face your enemies alone.", player->GetName(), pet->GetName());
    creature->Whisper(messageAdopt.c_str(), LANG_UNIVERSAL, player);
    CloseGossipMenuFor(player);
}

// Adds pets to the gossip menu for the given page.
void NpcBeastmaster::AddPetsToGossip(Player *player, std::vector<PetInfo> const &pets, uint32 page)
{
    uint32 count = 1;
    for (const auto &pet : pets)
    {
        if (count > (page - 1) * PET_PAGE_SIZE && count <= page * PET_PAGE_SIZE)
            AddGossipItemFor(player, GOSSIP_ICON_VENDOR, pet.name, GOSSIP_SENDER_MAIN, pet.entry + PET_PAGE_MAX);
        count++;
    }
}

// Clears the tracked pets cache for a specific player
void NpcBeastmaster::ClearTrackedPetsCache(Player *player)
{
    std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
    trackedPetsCache.erase(player->GetGUID().GetRawValue());
}

// Show the tracked pets menu for the player, with pagination and actions
void NpcBeastmaster::ShowTrackedPetsMenu(Player *player, Creature *creature, uint32 page /*= 1*/)
{
    ClearGossipMenuFor(player);

    uint64 guid = player->GetGUID().GetRawValue();
    std::vector<std::tuple<uint32, std::string, std::string>> *trackedPetsPtr = nullptr;

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
            "SELECT entry, name, date_tamed FROM beastmaster_tamed_pets WHERE owner_guid = {} ORDER BY date_tamed DESC",
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

    for (uint32 i = offset; i < total && shown < PET_TRACKED_PAGE_SIZE; ++i, ++shown)
    {
        const auto &petTuple = trackedPets[i];
        uint32 entry = std::get<0>(petTuple);
        const std::string &name = std::get<1>(petTuple);
        const std::string &date = std::get<2>(petTuple);
        const PetInfo *info = FindPetInfo(entry);

        std::string label;
        if (info)
            label = Acore::StringFormat("{} ({}) | Family: {} | Rarity: {}", name, date, info->family, info->rarity);
        else
            label = Acore::StringFormat("{} ({})", name, date);

        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Summon: " + label, GOSSIP_SENDER_MAIN, PET_TRACKED_SUMMON + entry);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Rename: " + label, GOSSIP_SENDER_MAIN, PET_TRACKED_RENAME + entry);
        AddGossipItemFor(player, GOSSIP_ICON_CHAT, "Delete: " + label, GOSSIP_SENDER_MAIN, PET_TRACKED_DELETE + entry);
    }

    // Pagination controls
    if (page > 1)
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Previous Page", GOSSIP_SENDER_MAIN, PET_TRACKED_PETS_MENU + (page - 1));
    if (offset + PET_TRACKED_PAGE_SIZE < total)
        AddGossipItemFor(player, GOSSIP_ICON_INTERACT_1, "Next Page", GOSSIP_SENDER_MAIN, PET_TRACKED_PETS_MENU + (page + 1));

    AddGossipItemFor(player, GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
    SendGossipMenuFor(player, PET_GOSSIP_HELLO, creature->GetGUID());
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

// Handles the rename prompt for pets
void NpcBeastmaster::HandleRenamePet(Player *player, Creature *creature, uint32 entry)
{
    // Store the entry in a player variable for the next step
    player->CustomData.Set("BeastmasterRenamePetEntry", new BeastmasterUInt32(entry));
    player->CustomData.Set("BeastmasterExpectRename", new BeastmasterBool(true));

    // Prompt the player for a new name
    ChatHandler(player->GetSession()).PSendSysMessage("Please type the new name for your pet in chat. (Type .cancel to abort)");
    creature->Whisper("Please type the new name for your pet in chat. (Type .cancel to abort)", LANG_UNIVERSAL, player);

    // Set a flag or state to expect the next chat message as the new name
    player->CustomData.Set("BeastmasterExpectRename", new BeastmasterBool(true));
}

// Handles the delete confirmation for pets
void NpcBeastmaster::HandleDeletePet(Player *player, Creature *creature, uint32 entry)
{
    player->CustomData.Set("BeastmasterDeletePetEntry", new BeastmasterUInt32(entry));
    player->CustomData.Set("BeastmasterExpectDeleteConfirm", new BeastmasterBool(true));
    creature->Whisper("Are you sure you want to delete this tracked pet? Type .yes to confirm or .cancel to abort.", LANG_UNIVERSAL, player);
}

// Chat handler to process the rename and delete confirmations
class BeastmasterRenameChatHandler : public PlayerScript
{
public:
    BeastmasterRenameChatHandler() : PlayerScript("BeastmasterRenameChatHandler") {}

    void OnChat(Player *player, uint32 /*type*/, uint32 /*lang*/, std::string &msg)
    {
        bool expectRename = false;
        if (auto ptr = player->CustomData.Get<BeastmasterBool>("BeastmasterExpectRename"))
            expectRename = ptr->value;

        uint32 entry = 0;
        if (auto entryPtr = player->CustomData.Get<BeastmasterUInt32>("BeastmasterRenamePetEntry"))
            entry = entryPtr->value;

        if (expectRename)
        {
            if (msg == ".cancel")
            {
                player->CustomData.Set("BeastmasterExpectRename", new BeastmasterBool(false));
                player->CustomData.Set("BeastmasterRenamePetEntry", new BeastmasterUInt32(0));
                ChatHandler(player->GetSession()).PSendSysMessage("Pet renaming cancelled.");
                LOG_INFO("module", "Beastmaster: Player %u cancelled pet rename.", player->GetGUID().GetCounter());
                return;
            }

            // Validate name (length, allowed chars, profanity)
            if (msg.length() < 2 || msg.length() > 16)
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Pet name must be between 2 and 16 characters.");
                return;
            }
            if (IsProfane(msg))
            {
                ChatHandler(player->GetSession()).PSendSysMessage("That name is not allowed.");
                LOG_INFO("module", "Beastmaster: Player %u attempted profane pet name: %s", player->GetGUID().GetCounter(), msg.c_str());
                return;
            }
            if (!IsValidPetName(msg))
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Invalid name (use 2–16 letters, spaces/dashes allowed).");
                return;
            }

            if (!entry)
                return;

            // Update DB
            CharacterDatabase.Execute(
                "UPDATE beastmaster_tamed_pets SET name = '{}' WHERE owner_guid = {} AND entry = {}",
                msg, player->GetGUID().GetCounter(), entry);

            player->CustomData.Set("BeastmasterExpectRename", new BeastmasterBool(false));
            player->CustomData.Set("BeastmasterRenamePetEntry", new BeastmasterUInt32(0));

            // Clear cache
            sNpcBeastMaster->ClearTrackedPetsCache(player);

            ChatHandler(player->GetSession()).PSendSysMessage("Pet renamed to %s.", msg.c_str());
            LOG_INFO("module", "Beastmaster: Player %u renamed pet (entry %u) to %s.",
                     player->GetGUID().GetCounter(), entry, msg.c_str());
            return;
        }

        // now handle delete confirm
        bool expectDelete = false;
        if (auto ptr = player->CustomData.Get<BeastmasterBool>("BeastmasterExpectDeleteConfirm"))
            expectDelete = ptr->value;

        uint32 delEntry = 0;
        if (auto delEntryPtr = player->CustomData.Get<BeastmasterUInt32>("BeastmasterDeletePetEntry"))
            delEntry = delEntryPtr->value;

        if (expectDelete)
        {
            if (msg == ".cancel")
            {
                player->CustomData.Set("BeastmasterExpectDeleteConfirm", new BeastmasterBool(false));
                player->CustomData.Set("BeastmasterDeletePetEntry", new BeastmasterUInt32(0));
                ChatHandler(player->GetSession()).PSendSysMessage("Pet deletion cancelled.");
                LOG_INFO("module", "Beastmaster: Player %u cancelled pet deletion.", player->GetGUID().GetCounter());
                return;
            }
            if (msg == ".yes")
            {
                if (!delEntry)
                    return;

                CharacterDatabase.Execute(
                    "DELETE FROM beastmaster_tamed_pets WHERE owner_guid = {} AND entry = {}",
                    player->GetGUID().GetCounter(), delEntry);

                // Clear cache
                sNpcBeastMaster->ClearTrackedPetsCache(player);

                ChatHandler(player->GetSession()).PSendSysMessage("Tracked pet deleted.");
                LOG_INFO("module", "Beastmaster: Player %u deleted tracked pet (entry %u).",
                         player->GetGUID().GetCounter(), delEntry);

                player->CustomData.Set("BeastmasterExpectDeleteConfirm", new BeastmasterBool(false));
                player->CustomData.Set("BeastmasterDeletePetEntry", new BeastmasterUInt32(0));
            }
            else
            {
                ChatHandler(player->GetSession()).PSendSysMessage("Type .yes to confirm deletion or .cancel to abort.");
            }
            return;
        }
    }
};

// --- Begin moved script classes from NpcBeastmaster_SC.cpp ---

class BeastMaster_CreatureScript : public CreatureScript
{
public:
    BeastMaster_CreatureScript() : CreatureScript("BeastMaster") {}

    bool OnGossipHello(Player *player, Creature *creature) override
    {
        LOG_INFO("module", "BeastMaster OnGossipHello called!");
        sNpcBeastMaster->ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player *player, Creature *creature, uint32 /*sender*/, uint32 action) override
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
    BeastMaster_WorldScript() : WorldScript("BeastMaster_WorldScript", {WORLDHOOK_ON_BEFORE_CONFIG_LOAD}) {}

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        sNpcBeastMaster->LoadSystem();
    }
};

class BeastMaster_PlayerScript : public PlayerScript
{
public:
    BeastMaster_PlayerScript() : PlayerScript("BeastMaster_PlayerScript", {PLAYERHOOK_ON_BEFORE_UPDATE,
                                                                           PLAYERHOOK_ON_BEFORE_LOAD_PET_FROM_DB,
                                                                           PLAYERHOOK_ON_BEFORE_GUARDIAN_INIT_STATS_FOR_LEVEL}) {}

    void OnPlayerBeforeUpdate(Player *player, uint32 /*p_time*/) override
    {
        sNpcBeastMaster->PlayerUpdate(player);
    }

    void OnPlayerBeforeLoadPetFromDB(Player * /*player*/, uint32 & /*petentry*/, uint32 & /*petnumber*/, bool & /*current*/, bool &forceLoadFromDB) override
    {
        forceLoadFromDB = true;
    }

    void OnPlayerBeforeGuardianInitStatsForLevel(Player * /*player*/, Guardian * /*guardian*/, CreatureTemplate const *cinfo, PetType &petType) override
    {
        if (cinfo->IsTameable(true))
            petType = HUNTER_PET;
    }
};

// --- End moved script classes ---

// AzerothCore module loader entry point
void Addmod_npc_beastmasterScripts()
{
    new BeastMaster_WorldScript();
    new BeastMaster_CreatureScript();
    new BeastMaster_PlayerScript();
}
