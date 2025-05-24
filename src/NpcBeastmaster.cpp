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

// Helper to get Beastmaster NPC entry from config
static uint32 GetBeastmasterNpcEntry() {
  return sConfigMgr->GetOption<uint32>("BeastMaster.NpcEntry", 601026);
}

namespace BeastmasterDB {
bool TrackTamedPet(Player *player, uint32 creatureEntry,
                   std::string const &petName) {
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

namespace {
std::vector<uint32> HunterSpells = {883,   982,  2641, 6991,
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
struct BeastmasterConfig {
  bool hunterOnly = true;
  bool allowExotic = false;
  bool keepPetHappy = false;
  uint32 minLevel = 10;
  uint32 maxLevel = 0;
  bool hunterBeastMasteryRequired = true;
  bool trackTamedPets = false;
  uint32 maxTrackedPets = 20;
  std::set<uint8> allowedRaces;
  std::set<uint8> allowedClasses;
} beastmasterConfig;

enum PetGossip {
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
  PET_TRACKED_PETS_MENU = 1000
};

constexpr auto PET_SPELL_CALL_PET = 883;
constexpr auto PET_SPELL_TAME_BEAST = 13481;
constexpr auto PET_SPELL_BEAST_MASTERY = 53270;
constexpr auto PET_MAX_HAPPINESS = 1048000;

std::unordered_map<uint32, PetInfo> allPetsByEntry;
std::mutex petsMutex;

std::unordered_map<uint64,
                   std::vector<std::tuple<uint32, std::string, std::string>>>
    trackedPetsCache;
} // namespace

enum BeastmasterEvents { BEASTMASTER_EVENT_EAT = 1 };

enum TrackedPetActions {
  PET_TRACKED_SUMMON = 2000,
  PET_TRACKED_RENAME = 3000,
  PET_TRACKED_DELETE = 4000,
  PET_TRACKED_PAGE_SIZE = 10
};

enum { PET_TRACKED_RENAME_PROMPT = 5000 };

static std::unordered_set<std::string> sProfanityList;
static time_t sProfanityListMTime = 0;

static time_t GetFileMTime(const std::string &path) {
  struct stat statbuf;
  if (stat(path.c_str(), &statbuf) == 0)
    return statbuf.st_mtime;
  return 0;
}

static std::set<uint8> ParseAllowedRaces(const std::string &csv) {
  std::set<uint8> result;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    try {
      uint8 race = static_cast<uint8>(std::stoul(item));
      if (race > 0)
        result.insert(race);
    } catch (...) {
    }
  }
  return result;
}

static std::set<uint8> ParseAllowedClasses(const std::string &csv) {
  std::set<uint8> result;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    try {
      uint8 cls = static_cast<uint8>(std::stoul(item));
      if (cls > 0)
        result.insert(cls);
    } catch (...) {
    }
  }
  return result;
}

static void LoadProfanityListIfNeeded() {
  const std::string path = "modules/mod-npc-beastmaster/conf/profanity.txt";
  time_t mtime = GetFileMTime(path);
  if (mtime == 0)
    return;
  if (mtime == sProfanityListMTime && !sProfanityList.empty())
    return;
  sProfanityList.clear();
  std::ifstream f(path);
  if (!f.is_open()) {
    LOG_WARN("module", "Beastmaster: Could not open profanity.txt, skipping "
                       "profanity filter.");
    return;
  }
  std::string word;
  while (std::getline(f, word)) {
    std::transform(word.begin(), word.end(), word.begin(), ::tolower);
    if (!word.empty())
      sProfanityList.insert(word);
  }
  sProfanityListMTime = mtime;
  LOG_INFO("module", "Beastmaster: Loaded {} profane words (mtime={})",
           sProfanityList.size(), long(mtime));
}

static bool IsProfane(const std::string &name) {
  if (!sConfigMgr->GetOption<bool>("BeastMaster.ProfanityFilter", true))
    return false;
  LoadProfanityListIfNeeded();
  std::string lower = name;
  std::transform(lower.begin(), lower.end(), lower.begin(), ::tolower);
  for (auto const &bad : sProfanityList)
    if (lower.find(bad) != std::string::npos)
      return true;
  return false;
}

static bool IsValidPetName(const std::string &name) {
  if (name.size() < 2 || name.size() > 16)
    return false;
  if (std::isspace(name.front()) || std::isspace(name.back()))
    return false;
  static const std::regex allowed("^[A-Za-z][A-Za-z \\-']*[A-Za-z]$");
  return std::regex_match(name, allowed);
}

static std::set<uint32> ParseEntryList(const std::string &csv) {
  std::set<uint32> result;
  std::stringstream ss(csv);
  std::string item;
  while (std::getline(ss, item, ',')) {
    try {
      result.insert(std::stoul(item));
    } catch (...) {
    }
  }
  return result;
}

static const PetInfo *FindPetInfo(uint32 entry) {
  std::lock_guard<std::mutex> lock(petsMutex);
  auto it = allPetsByEntry.find(entry);
  return it != allPetsByEntry.end() ? &it->second : nullptr;
}

class BeastmasterBool : public DataMap::Base {
public:
  explicit BeastmasterBool(bool v) : value(v) {}
  bool value;
};

class BeastmasterUInt32 : public DataMap::Base {
public:
  explicit BeastmasterUInt32(uint32 v) : value(v) {}
  uint32 value;
};

class BeastmasterPetMap : public DataMap::Base {
public:
  std::map<uint32, uint32> map;
  BeastmasterPetMap(const std::map<uint32, uint32> &m) : map(m) {}
};

/*static*/ NpcBeastmaster *NpcBeastmaster::instance() {
  static NpcBeastmaster instance;
  return &instance;
}

void NpcBeastmaster::LoadSystem(bool /*reload = false*/) {
  std::lock_guard<std::mutex> lock(petsMutex);

  beastmasterConfig.hunterOnly =
      sConfigMgr->GetOption<bool>("BeastMaster.HunterOnly", true);
  beastmasterConfig.allowExotic =
      sConfigMgr->GetOption<bool>("BeastMaster.AllowExotic", false);
  beastmasterConfig.keepPetHappy =
      sConfigMgr->GetOption<bool>("BeastMaster.KeepPetHappy", false);
  beastmasterConfig.minLevel =
      sConfigMgr->GetOption<uint32>("BeastMaster.MinLevel", 10);
  beastmasterConfig.maxLevel =
      sConfigMgr->GetOption<uint32>("BeastMaster.MaxLevel", 0);
  beastmasterConfig.hunterBeastMasteryRequired = sConfigMgr->GetOption<uint32>(
      "BeastMaster.HunterBeastMasteryRequired", true);
  beastmasterConfig.trackTamedPets =
      sConfigMgr->GetOption<bool>("BeastMaster.TrackTamedPets", false);
  beastmasterConfig.maxTrackedPets =
      sConfigMgr->GetOption<uint32>("BeastMaster.MaxTrackedPets", 20);
  beastmasterConfig.allowedRaces = ParseAllowedRaces(
      sConfigMgr->GetOption<std::string>("BeastMaster.AllowedRaces", "0"));
  beastmasterConfig.allowedClasses = ParseAllowedClasses(
      sConfigMgr->GetOption<std::string>("BeastMaster.AllowedClasses", "0"));

  rarePetEntries = ParseEntryList(
      sConfigMgr->GetOption<std::string>("BeastMaster.RarePets", ""));
  rareExoticPetEntries = ParseEntryList(
      sConfigMgr->GetOption<std::string>("BeastMaster.RareExoticPets", ""));

  allPets.clear();
  normalPets.clear();
  exoticPets.clear();
  rarePets.clear();
  rareExoticPets.clear();
  allPetsByEntry.clear();

  QueryResult result = WorldDatabase.Query(
      "SELECT entry, name, family, rarity FROM beastmaster_tames");
  if (!result) {
    LOG_ERROR(
        "module",
        "Beastmaster: Could not load tames from beastmaster_tames table!");
    return;
  }

  do {
    Field *fields = result->Fetch();
    PetInfo info;
    info.entry = fields[0].Get<uint32>();
    info.name = fields[1].Get<std::string>();
    info.family = fields[2].Get<uint32>();
    info.rarity = fields[3].Get<std::string>();

    static const std::set<uint32> TrainerIconFamilies = {
        1, 2, 3, 4, 7, 8, 9, 10, 15, 20, 21, 30, 24, 31, 25, 34, 27};

    if (TrainerIconFamilies.count(info.family))
      info.icon = GOSSIP_ICON_TRAINER;
    else
      info.icon = GOSSIP_ICON_VENDOR;

    allPets.push_back(info);
    allPetsByEntry[info.entry] = info;

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

void NpcBeastmaster::ShowMainMenu(Player *player, Creature *creature) {
  // Module enable check
  if (!sConfigMgr->GetOption<bool>("BeastMaster.Enable", true))
    return;

  if (beastmasterConfig.hunterOnly && player->getClass() != CLASS_HUNTER) {
    if (creature)
      creature->Whisper("I am sorry, but pets are for hunters only.",
                        LANG_UNIVERSAL, player);
    else
      ChatHandler(player->GetSession())
          .PSendSysMessage("I am sorry, but pets are for hunters only.");
    return;
  }

  if (!beastmasterConfig.allowedClasses.empty() &&
      beastmasterConfig.allowedClasses.find(player->getClass()) ==
          beastmasterConfig.allowedClasses.end()) {
    if (creature)
      creature->Whisper("Your class is not allowed to adopt pets.",
                        LANG_UNIVERSAL, player);
    else
      ChatHandler(player->GetSession())
          .PSendSysMessage("Your class is not allowed to adopt pets.");
    return;
  }

  if (!beastmasterConfig.allowedRaces.empty() &&
      beastmasterConfig.allowedRaces.find(player->getRace()) ==
          beastmasterConfig.allowedRaces.end()) {
    if (creature)
      creature->Whisper("Your race is not allowed to adopt pets.",
                        LANG_UNIVERSAL, player);
    else
      ChatHandler(player->GetSession())
          .PSendSysMessage("Your race is not allowed to adopt pets.");
    return;
  }

  if (player->GetLevel() < beastmasterConfig.minLevel &&
      beastmasterConfig.minLevel != 0) {
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

  if (beastmasterConfig.maxLevel != 0 &&
      player->GetLevel() > beastmasterConfig.maxLevel) {
    std::string message = Acore::StringFormat(
        "Sorry {}, but you must be level {} or lower to adopt a pet.",
        player->GetName(), beastmasterConfig.maxLevel);
    if (creature)
      creature->Whisper(message.c_str(), LANG_UNIVERSAL, player);
    else
      ChatHandler(player->GetSession()).PSendSysMessage("%s", message.c_str());
    return;
  }

  ClearGossipMenuFor(player);

  AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Pets",
                   GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS);
  AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Pets",
                   GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS);

  if (beastmasterConfig.allowExotic ||
      player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
      player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())) {
    if (player->getClass() != CLASS_HUNTER) {
      AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets",
                       GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
      AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets",
                       GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
    } else if (!beastmasterConfig.hunterBeastMasteryRequired ||
               player->HasTalent(PET_SPELL_BEAST_MASTERY,
                                 player->GetActiveSpec())) {
      AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Exotic Pets",
                       GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
      AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets",
                       GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
    }
  }

  if (player->getClass() != CLASS_HUNTER &&
      player->HasSpell(PET_SPELL_CALL_PET))
    AddGossipItemFor(player, GOSSIP_ICON_BATTLE, "Unlearn Hunter Abilities",
                     GOSSIP_SENDER_MAIN, PET_REMOVE_SKILLS);

  if (beastmasterConfig.trackTamedPets)
    AddGossipItemFor(player, GOSSIP_ICON_CHAT, "My Tamed Pets",
                     GOSSIP_SENDER_MAIN, PET_TRACKED_PETS_MENU);

  if (player->getClass() == CLASS_HUNTER)
    AddGossipItemFor(player, GOSSIP_ICON_TAXI, "Visit Stable",
                     GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

  AddGossipItemFor(player, GOSSIP_ICON_MONEY_BAG, "Buy Pet Food",
                   GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);

  if (creature)
    SendGossipMenuFor(player, PET_GOSSIP_HELLO, creature->GetGUID());
  else
    SendGossipMenuFor(player, PET_GOSSIP_HELLO, ObjectGuid::Empty);

  player->PlayDirectSound(PET_BEASTMASTER_HOWL);
}

void NpcBeastmaster::GossipSelect(Player *player, Creature *creature,
                                  uint32 action) {
  if (!sConfigMgr->GetOption<bool>("BeastMaster.Enable", true))
    return;

  ClearGossipMenuFor(player);

  if (action == PET_MAIN_MENU) {
    ShowMainMenu(player, creature);
  } else if (action >= PET_PAGE_START_PETS &&
             action < PET_PAGE_START_EXOTIC_PETS) {
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
  } else if (action >= PET_PAGE_START_EXOTIC_PETS &&
             action < PET_PAGE_START_RARE_PETS) {
    if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
          player->HasTalent(PET_SPELL_BEAST_MASTERY,
                            player->GetActiveSpec()))) {
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
  } else if (action >= PET_PAGE_START_RARE_PETS &&
             action < PET_PAGE_START_RARE_EXOTIC_PETS) {
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
  } else if (action >= PET_PAGE_START_RARE_EXOTIC_PETS &&
             action < PET_PAGE_MAX) {
    if (!(player->HasSpell(PET_SPELL_BEAST_MASTERY) ||
          player->HasTalent(PET_SPELL_BEAST_MASTERY,
                            player->GetActiveSpec()))) {
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
  } else if (action == PET_REMOVE_SKILLS) {
    for (auto spell : HunterSpells)
      player->removeSpell(spell, SPEC_MASK_ALL, false);

    player->removeSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
    CloseGossipMenuFor(player);
  } else if (action == GOSSIP_OPTION_STABLEPET) {
    player->GetSession()->SendStablePet(creature->GetGUID());
  } else if (action == GOSSIP_OPTION_VENDOR) {
    player->GetSession()->SendListInventory(creature->GetGUID());
  } else if (action >= PET_TRACKED_PETS_MENU && action < PET_TRACKED_SUMMON) {
    uint32 page = action - PET_TRACKED_PETS_MENU + 1;
    ShowTrackedPetsMenu(player, creature, page);
    return;
  } else if (action >= PET_TRACKED_SUMMON && action < PET_TRACKED_RENAME) {
    uint32 idx = action - PET_TRACKED_SUMMON;
    auto *petMapWrap =
        player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
    if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
      return;
    uint32 entry = petMapWrap->map[idx];
    if (player->IsExistPet()) {
      creature->Whisper("First you must abandon or stable your current pet!",
                        LANG_UNIVERSAL, player);
      CloseGossipMenuFor(player);
      return;
    } else {
      Pet *pet = player->CreatePet(entry, PET_SPELL_CALL_PET);
      if (pet) {
        QueryResult nameResult =
            CharacterDatabase.Query("SELECT name FROM beastmaster_tamed_pets "
                                    "WHERE owner_guid = {} AND entry = {}",
                                    player->GetGUID().GetCounter(), entry);
        if (nameResult) {
          std::string customName = (*nameResult)[0].Get<std::string>();
          pet->SetName(customName);
        }
        pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);
        creature->Whisper("Your tracked pet has been summoned!", LANG_UNIVERSAL,
                          player);
      } else {
        creature->Whisper("Failed to summon pet.", LANG_UNIVERSAL, player);
      }
    }
    CloseGossipMenuFor(player);
    return;
  } else if (action >= PET_TRACKED_RENAME && action < PET_TRACKED_DELETE) {
    uint32 idx = action - PET_TRACKED_RENAME;
    auto *petMapWrap =
        player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
    if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
      return;
    uint32 entry = petMapWrap->map[idx];
    player->CustomData.Set("BeastmasterRenamePetEntry",
                           new BeastmasterUInt32(entry));
    player->CustomData.Set("BeastmasterExpectRename",
                           new BeastmasterBool(true));
    ChatHandler(player->GetSession())
        .PSendSysMessage("To rename your pet, type: .petname <newname> in "
                         "chat. To cancel, type: .cancel");
    if (creature)
      creature->Whisper("To rename your pet, type: .petname <newname> in chat. "
                        "To cancel, type: .cancel",
                        LANG_UNIVERSAL, player);
    CloseGossipMenuFor(player);
    return;
  } else if (action >= PET_TRACKED_DELETE &&
             action < PET_TRACKED_DELETE + 1000) {
    uint32 idx = action - PET_TRACKED_DELETE;
    auto *petMapWrap =
        player->CustomData.Get<BeastmasterPetMap>("BeastmasterMenuPetMap");
    if (!petMapWrap || petMapWrap->map.find(idx) == petMapWrap->map.end())
      return;
    uint32 entry = petMapWrap->map[idx];

    CharacterDatabase.Execute("DELETE FROM beastmaster_tamed_pets WHERE "
                              "owner_guid = {} AND entry = {}",
                              player->GetGUID().GetCounter(), entry);

    sNpcBeastMaster->ClearTrackedPetsCache(player);

    ChatHandler(player->GetSession())
        .PSendSysMessage("Tracked pet deleted (entry {}).", entry);
    LOG_INFO("module", "Beastmaster: Player {} deleted tracked pet (entry {}).",
             player->GetGUID().GetCounter(), entry);

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

  if (action >= PET_PAGE_MAX)
    CreatePet(player, creature, action);
}

void NpcBeastmaster::CreatePet(Player *player, Creature *creature,
                               uint32 action) {
  if (!sConfigMgr->GetOption<bool>("BeastMaster.Enable", true))
    return;

  uint32 petEntry = action - PET_PAGE_MAX;
  const PetInfo *info = FindPetInfo(petEntry);

  if (player->IsExistPet()) {
    creature->Whisper("First you must abandon or stable your current pet!",
                      LANG_UNIVERSAL, player);
    CloseGossipMenuFor(player);
    return;
  }

  if (info && info->rarity == "exotic" && player->getClass() != CLASS_HUNTER &&
      !beastmasterConfig.allowExotic) {
    creature->Whisper("Only hunters can adopt exotic pets.", LANG_UNIVERSAL,
                      player);
    CloseGossipMenuFor(player);
    return;
  }

  if (info && info->rarity == "exotic" && player->getClass() == CLASS_HUNTER &&
      beastmasterConfig.hunterBeastMasteryRequired) {
    if (!player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())) {
      creature->Whisper(
          "You need the Beast Mastery talent to adopt exotic pets.",
          LANG_UNIVERSAL, player);
      CloseGossipMenuFor(player);
      return;
    }
  }

  // Enforce max tracked pets if enabled
  if (beastmasterConfig.trackTamedPets &&
      beastmasterConfig.maxTrackedPets > 0) {
    QueryResult result = CharacterDatabase.Query(
        "SELECT COUNT(*) FROM beastmaster_tamed_pets WHERE owner_guid = {}",
        player->GetGUID().GetCounter());
    uint32 count = result ? (*result)[0].Get<uint32>() : 0;
    if (count >= beastmasterConfig.maxTrackedPets) {
      creature->Whisper("You have reached the maximum number of tracked pets.",
                        LANG_UNIVERSAL, player);
      CloseGossipMenuFor(player);
      return;
    }
  }

  Pet *pet = player->CreatePet(petEntry, player->getClass() == CLASS_HUNTER
                                             ? PET_SPELL_TAME_BEAST
                                             : PET_SPELL_CALL_PET);
  if (!pet) {
    creature->Whisper("First you must abandon or stable your current pet!",
                      LANG_UNIVERSAL, player);
    return;
  }

  if (beastmasterConfig.trackTamedPets)
    BeastmasterDB::TrackTamedPet(player, petEntry, pet->GetName());

  pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);

  if (player->getClass() != CLASS_HUNTER) {
    if (!player->HasSpell(PET_SPELL_CALL_PET)) {
      for (auto const &spell : HunterSpells)
        if (!player->HasSpell(spell))
          player->learnSpell(spell);
    }
  }

  std::string messageAdopt =
      Acore::StringFormat("A fine choice {}! Take good care of your {} and you "
                          "will never face your enemies alone.",
                          player->GetName(), pet->GetName());
  creature->Whisper(messageAdopt.c_str(), LANG_UNIVERSAL, player);
  CloseGossipMenuFor(player);
}

void NpcBeastmaster::AddPetsToGossip(Player *player,
                                     std::vector<PetInfo> const &pets,
                                     uint32 page) {
  std::set<uint32> tamedEntries;
  QueryResult result = CharacterDatabase.Query(
      "SELECT entry FROM beastmaster_tamed_pets WHERE owner_guid = {}",
      player->GetGUID().GetCounter());
  if (result) {
    do {
      Field *fields = result->Fetch();
      tamedEntries.insert(fields[0].Get<uint32>());
    } while (result->NextRow());
  }

  uint32 count = 1;
  for (const auto &pet : pets) {
    if (count > (page - 1) * PET_PAGE_SIZE && count <= page * PET_PAGE_SIZE) {
      if (tamedEntries.count(pet.entry)) {
        AddGossipItemFor(player, GOSSIP_ICON_CHAT,
                         pet.name + " (Already Tamed)", GOSSIP_SENDER_MAIN,
                         0); // 0 = no action
      } else {
        AddGossipItemFor(player, pet.icon, pet.name, GOSSIP_SENDER_MAIN,
                         pet.entry + PET_PAGE_MAX);
      }
    }
    count++;
  }
}

void NpcBeastmaster::ClearTrackedPetsCache(Player *player) {
  std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
  trackedPetsCache.erase(player->GetGUID().GetRawValue());
  player->CustomData.Erase("BeastmasterMenuPetMap");
}

void NpcBeastmaster::ShowTrackedPetsMenu(Player *player, Creature *creature,
                                         uint32 page /*= 1*/) {
  ClearGossipMenuFor(player);

  uint64 guid = player->GetGUID().GetRawValue();
  std::vector<std::tuple<uint32, std::string, std::string>> *trackedPetsPtr =
      nullptr;

  {
    std::lock_guard<std::mutex> lock(trackedPetsCacheMutex);
    auto it = trackedPetsCache.find(guid);
    if (it != trackedPetsCache.end()) {
      trackedPetsPtr = &it->second;
    }
  }

  if (!trackedPetsPtr) {
    std::vector<std::tuple<uint32, std::string, std::string>> trackedPets;
    QueryResult result = CharacterDatabase.Query(
        "SELECT entry, name, date_tamed FROM beastmaster_tamed_pets WHERE "
        "owner_guid = {} ORDER BY date_tamed DESC",
        player->GetGUID().GetCounter());

    if (result) {
      do {
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
       ++i, ++shown) {
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

void NpcBeastmaster::PlayerUpdate(Player *player) {
  if (beastmasterConfig.keepPetHappy && player->GetPet()) {
    Pet *pet = player->GetPet();
    if (pet->getPetType() == HUNTER_PET)
      pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);
  }
}

// Chat handler to process the rename and delete confirmations
class BeastMaster_CreatureScript : public CreatureScript {
public:
  BeastMaster_CreatureScript() : CreatureScript("BeastMaster") {}

  bool OnGossipHello(Player *player, Creature *creature) override {
    sNpcBeastMaster->ShowMainMenu(player, creature);
    return true;
  }

  bool OnGossipSelect(Player *player, Creature *creature, uint32 /*sender*/,
                      uint32 action) override {
    sNpcBeastMaster->GossipSelect(player, creature, action);
    return true;
  }

  struct beastmasterAI : public ScriptedAI {
    beastmasterAI(Creature *creature) : ScriptedAI(creature) {}

    void Reset() override {
      events.ScheduleEvent(BEASTMASTER_EVENT_EAT, urand(30000, 90000));
    }

    void UpdateAI(uint32 diff) override {
      events.Update(diff);

      switch (events.ExecuteEvent()) {
      case BEASTMASTER_EVENT_EAT:
        me->HandleEmoteCommand(EMOTE_ONESHOT_EAT_NO_SHEATHE);
        events.ScheduleEvent(BEASTMASTER_EVENT_EAT, urand(30000, 90000));
        break;
      }
    }

  private:
    EventMap events;
  };

  CreatureAI *GetAI(Creature *creature) const override {
    return new beastmasterAI(creature);
  }
};

class BeastMaster_WorldScript : public WorldScript {
public:
  BeastMaster_WorldScript()
      : WorldScript("BeastMaster_WorldScript",
                    {WORLDHOOK_ON_BEFORE_CONFIG_LOAD}) {}

  void OnBeforeConfigLoad(bool /*reload*/) override {
    sNpcBeastMaster->LoadSystem();
  }
};

class BeastMaster_PlayerScript : public PlayerScript {
public:
  BeastMaster_PlayerScript()
      : PlayerScript("BeastMaster_PlayerScript",
                     {PLAYERHOOK_ON_BEFORE_UPDATE,
                      PLAYERHOOK_ON_BEFORE_LOAD_PET_FROM_DB,
                      PLAYERHOOK_ON_BEFORE_GUARDIAN_INIT_STATS_FOR_LEVEL}) {}

  void OnPlayerBeforeUpdate(Player *player, uint32 /*p_time*/) override {
    sNpcBeastMaster->PlayerUpdate(player);
  }

  void OnPlayerBeforeLoadPetFromDB(Player * /*player*/, uint32 & /*petentry*/,
                                   uint32 & /*petnumber*/, bool & /*current*/,
                                   bool &forceLoadFromDB) override {
    forceLoadFromDB = true;
  }

  void OnPlayerBeforeGuardianInitStatsForLevel(Player * /*player*/,
                                               Guardian * /*guardian*/,
                                               CreatureTemplate const *cinfo,
                                               PetType &petType) override {
    if (cinfo->IsTameable(true))
      petType = HUNTER_PET;
  }
};

// Rename and cancel commands for pet renaming
class BeastMaster_CommandScript : public CommandScript {
public:
  BeastMaster_CommandScript() : CommandScript("BeastMaster") {}

  Acore::ChatCommands::ChatCommandTable GetCommands() const override;

  // Declare handlers as static
  static bool HandlePetnameCommand(ChatHandler *handler, std::string_view args);
  static bool HandleCancelCommand(ChatHandler *handler, std::string_view args);
  static bool HandleBeastmasterCommand(ChatHandler *handler, const char *args);
};

// Define GetCommands outside the class body
Acore::ChatCommands::ChatCommandTable
BeastMaster_CommandScript::GetCommands() const {
  using namespace Acore::ChatCommands;
  return {{"petname", HandlePetnameCommand, SEC_PLAYER, Console::No},
          {"cancel", HandleCancelCommand, SEC_PLAYER, Console::No},
          {"beastmaster", HandleBeastmasterCommand, SEC_PLAYER, Console::No},
          {"bm", HandleBeastmasterCommand, SEC_PLAYER, Console::No}};
}

// Now define the static handler functions outside the class
bool BeastMaster_CommandScript::HandlePetnameCommand(ChatHandler *handler,
                                                     std::string_view args) {
  Player *player = handler->GetSession()->GetPlayer();
  auto *expectRename =
      player->CustomData.Get<BeastmasterBool>("BeastmasterExpectRename");
  auto *renameEntry =
      player->CustomData.Get<BeastmasterUInt32>("BeastmasterRenamePetEntry");
  if (!expectRename || !expectRename->value || !renameEntry) {
    handler->PSendSysMessage("You are not renaming a pet right now. Use the "
                             "Beastmaster NPC to start renaming.");
    return true;
  }

  std::string newName(args);
  while (!newName.empty() && std::isspace(newName.front()))
    newName.erase(newName.begin());
  while (!newName.empty() && std::isspace(newName.back()))
    newName.pop_back();

  if (newName.empty()) {
    handler->PSendSysMessage("Usage: .petname <newname>");
    return true;
  }

  if (!IsValidPetName(newName) || IsProfane(newName)) {
    handler->PSendSysMessage("Invalid or profane pet name. Please try again "
                             "with .petname <newname>.");
    return true;
  }

  CharacterDatabase.Execute("UPDATE beastmaster_tamed_pets SET name = '{}' "
                            "WHERE owner_guid = {} AND entry = {}",
                            newName, player->GetGUID().GetCounter(),
                            renameEntry->value);

  player->CustomData.Erase("BeastmasterExpectRename");
  player->CustomData.Erase("BeastmasterRenamePetEntry");

  handler->PSendSysMessage("Pet renamed to '{}'.", newName);
  sNpcBeastMaster->ClearTrackedPetsCache(player);
  return true;
}

bool BeastMaster_CommandScript::HandleCancelCommand(ChatHandler *handler,
                                                    std::string_view /*args*/) {
  Player *player = handler->GetSession()->GetPlayer();
  auto *expectRename =
      player->CustomData.Get<BeastmasterBool>("BeastmasterExpectRename");
  if (!expectRename || !expectRename->value) {
    handler->PSendSysMessage("You are not renaming a pet right now.");
    return true;
  }
  player->CustomData.Erase("BeastmasterExpectRename");
  player->CustomData.Erase("BeastmasterRenamePetEntry");
  handler->PSendSysMessage("Pet renaming cancelled.");
  return true;
}

bool BeastMaster_CommandScript::HandleBeastmasterCommand(
    ChatHandler *handler, const char * /*args*/) {
  Player *player = handler->GetSession()->GetPlayer();
  if (!player)
    return false;

  float x = player->GetPositionX();
  float y = player->GetPositionY();
  float z = player->GetPositionZ();
  float o = player->GetOrientation();

  static std::unordered_map<uint64, time_t> lastSummonTime;
  uint64 guid = player->GetGUID().GetRawValue();
  time_t now = time(nullptr);
  uint32 cooldown =
      sConfigMgr->GetOption<uint32>("BeastMaster.SummonCooldown", 120);
  if (lastSummonTime.count(guid) && now - lastSummonTime[guid] < cooldown) {
    handler->PSendSysMessage(
        "You must wait {} seconds before summoning the Beastmaster again.",
        cooldown - (now - lastSummonTime[guid]));
    return true;
  }
  lastSummonTime[guid] = now;

  Creature *npc = player->SummonCreature(GetBeastmasterNpcEntry(), x, y, z, o,
                                         TEMPSUMMON_TIMED_DESPAWN_OUT_OF_COMBAT,
                                         2 * MINUTE * IN_MILLISECONDS);

  if (npc) {
    handler->PSendSysMessage(
        "The Beastmaster has arrived and will remain for 2 minutes.");
  } else {
    handler->PSendSysMessage(
        "Failed to summon the Beastmaster. Please contact an admin.");
  }
  return true;
}

class BeastmasterLoginNotice_PlayerScript : public PlayerScript {
public:
  BeastmasterLoginNotice_PlayerScript()
      : PlayerScript("BeastmasterLoginNotice_PlayerScript") {}

  void OnLogin(Player *player) override {
    if (!sConfigMgr->GetOption<bool>("BeastMaster.ShowLoginNotice", true))
      return;

    if (!sConfigMgr->GetOption<bool>("BeastMaster.Enable", true))
      return;

    // Optionally restrict to hunters if config says so
    if (sConfigMgr->GetOption<bool>("BeastMaster.HunterOnly", true) &&
        player->getClass() != CLASS_HUNTER)
      return;

    ChatHandler ch(player->GetSession());
    std::string msg =
        sConfigMgr->GetOption<std::string>("BeastMaster.LoginMessage", "");
    if (!msg.empty())
      ch.PSendSysMessage("%s", msg.c_str());
    else
      ch.PSendSysMessage("|cff00ff00[Beastmaster]|r Use |cff00ffff.bm|r or "
                         "|cff00ffff.beastmaster|r to summon the Beastmaster "
                         "NPC and manage your pets!");

    // If player is a GM, show extra info
    if (player->GetSession()->GetSecurity() >= SEC_GAMEMASTER) {
      ch.PSendSysMessage(
          "|cffffa500[GM Notice]|r You can also use |cff00ffff.npc add "
          "601026|r to spawn the Beastmaster NPC anywhere, and "
          "|cff00ffff.npc save|r to make it permanent.");
    }
  }
};

void Addmod_npc_beastmasterScripts() {
  new BeastMaster_CommandScript();
  new BeastmasterLoginNotice_PlayerScript();
  new BeastMaster_CreatureScript();
  new BeastMaster_WorldScript();
  new BeastMaster_PlayerScript();
  LOG_INFO(
      "module",
      "Beastmaster: Registered commands: .petname, .cancel, .beastmaster, .bm");
}
