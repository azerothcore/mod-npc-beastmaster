#include "Config.h"
#include "Pet.h"
#include "ScriptPCH.h"
#include "Configuration/Config.h"
#include "ScriptedGossip.h"

std::vector<uint32> HunterSpells = { 883, 982, 2641, 6991, 48990, 1002, 1462, 6197 };
std::map<std::string, uint32> pets;
std::map<std::string, uint32> exoticPets;
std::map<std::string, uint32> rarePets;
std::map<std::string, uint32> rareExoticPets;
bool BeastMasterAnnounceToPlayer;
bool BeastMasterHunterOnly;
bool BeastMasterAllowExotic;
bool BeastMasterKeepPetHappy;
bool BeastMasterCorePatch;
uint32 BeastMasterMinLevel;
bool BeastMasterHunterBeastMasteryRequired;

enum PetGossip
{
    PET_BEASTMASTER_HOWL            =   9036,
    PET_PAGE_SIZE                   =     13,
    PET_PAGE_START_PETS             =    501,
    PET_PAGE_START_EXOTIC_PETS      =    601,
    PET_PAGE_START_RARE_PETS        =    701,
    PET_PAGE_START_RARE_EXOTIC_PETS =    801,
    PET_PAGE_MAX                    =    901,
    PET_MAIN_MENU                   =     50,
    PET_REMOVE_SKILLS               =     80,
    PET_GOSSIP_HELLO                = 601026
};

enum PetSpells
{
    PET_SPELL_CALL_PET      =     883,
    PET_SPELL_BEAST_MASTERY =   53270,
    PET_MAX_HAPPINESS       = 1048000
};

class BeastMasterAnnounce : public PlayerScript
{

public:

    BeastMasterAnnounce() : PlayerScript("BeastMasterAnnounce") {}

    void OnLogin(Player* player)
    {
        // Announce Module
        if (BeastMasterAnnounceToPlayer)
        {
            if (BeastMasterCorePatch)
                ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00BeastMasterNPC |rmodule with core patch.");
            else
                ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00BeastMasterNPC |rmodule without core patch.");
        }
    }
};

class BeastMaster : public CreatureScript
{

public:

    BeastMaster() : CreatureScript("BeastMaster") { }

    void CreatePet(Player *player, Creature * m_creature, uint32 entry)
    {
        // Check if player already has a pet
        if (player->GetPet())
        {
            m_creature->MonsterWhisper("First you must abandon or stable your current pet!", player, false);
            player->CLOSE_GOSSIP_MENU();
            return;
        }

        // Create Tamed Creature
        Pet* pet = player->CreateTamedPetFrom(entry, PET_SPELL_CALL_PET);
        if (!pet) { return; }

        // Set Pet Happiness
        pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);

        // Initialize Pet
        pet->AddUInt64Value(UNIT_FIELD_CREATEDBY, player->GetGUID());
        pet->SetUInt32Value(UNIT_FIELD_FACTIONTEMPLATE, player->getFaction());
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, player->getLevel());

        // Prepare Level-Up Visual
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, player->getLevel() - 1);
        pet->GetMap()->AddToMap(pet->ToCreature());

        // Visual Effect for Level-Up
        pet->SetUInt32Value(UNIT_FIELD_LEVEL, player->getLevel());

        // Initialize Pet Stats
        pet->InitTalentForLevel();
        if (!pet->InitStatsForLevel(player->getLevel()))
        {
            // sLog->outError("Pet Create fail: no init stats for entry %u", entry);
            pet->UpdateAllStats();
        }

        // Caster Pets?
        player->SetMinion(pet, true);

        // Save Pet
        pet->GetCharmInfo()->SetPetNumber(sObjectMgr->GeneratePetNumber(), true);
        player->PetSpellInitialize();
        pet->InitLevelupSpellsForLevel();
        pet->SavePetToDB(PET_SAVE_AS_CURRENT, 0);

        // Learn Hunter Abilities (only for non-hunters)
        if (player->getClass() != CLASS_HUNTER)
        {
            // Assume player has already learned the spells if they have Call Pet
            if (!player->HasSpell(PET_SPELL_CALL_PET))
            {
                for (int i = 0; i < HunterSpells.size(); ++i)
                    player->learnSpell(HunterSpells[i]);
            }
        }

        // Farewell
        std::ostringstream messageAdopt;
        messageAdopt << "A fine choice " << player->GetName() << "! Take good care of your " << pet->GetName() << " and you will never face your enemies alone.";
        m_creature->MonsterWhisper(messageAdopt.str().c_str(), player);
        player->CLOSE_GOSSIP_MENU();
    }

    bool OnGossipHello(Player *player, Creature * m_creature)
    {
        // If enabled for Hunters only..
        if (BeastMasterHunterOnly)
        {
            if (player->getClass() != CLASS_HUNTER)
            {
                m_creature->MonsterWhisper("I am sorry, but pets are for hunters only.", player, false);
                player->CLOSE_GOSSIP_MENU();
                return false;
            }
        }

        // Check level requirement
        if (player->getLevel() < BeastMasterMinLevel && BeastMasterMinLevel != 0)
        {
            std::ostringstream messageExperience;
            messageExperience << "Sorry " << player->GetName() << ", but you must reach level " << BeastMasterMinLevel << " before adopting a pet.";
            m_creature->MonsterWhisper(messageExperience.str().c_str(), player);
            player->CLOSE_GOSSIP_MENU();
            return false;
        }

        // MAIN MENU
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS);

        if (BeastMasterAllowExotic || player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
        {
            if (player->getClass() != CLASS_HUNTER)
            {
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
            }
            else if (!BeastMasterHunterBeastMasteryRequired || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
            {
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
            }
        }

        // remove pet skills (not for hunters)
        if (player->getClass() != CLASS_HUNTER && player->HasSpell(PET_SPELL_CALL_PET))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Unlearn Hunter Abilities", GOSSIP_SENDER_MAIN, PET_REMOVE_SKILLS);

        // Stables for hunters only - Doesn't seem to work for other classes
        if (player->getClass() == CLASS_HUNTER)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Visit Stable", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

        // Pet Food Vendor
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Buy Pet Food", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);

        player->PlayerTalkClass->SendGossipMenu(PET_GOSSIP_HELLO, m_creature->GetGUID());

        // Howl
        player->PlayDirectSound(PET_BEASTMASTER_HOWL);

        return true;
    }

    bool OnGossipSelect(Player *player, Creature * m_creature, uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();

        if (action == PET_MAIN_MENU)
        {
            // MAIN MENU
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS);

            if (BeastMasterAllowExotic || player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
            {
                if (player->getClass() != CLASS_HUNTER)
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
                }
                else if (!BeastMasterHunterBeastMasteryRequired || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec()))
                {
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS);
                    player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Exotic Pets", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS);
                }
            }

            // remove pet skills (not for hunters)
            if (player->getClass() != CLASS_HUNTER && player->HasSpell(PET_SPELL_CALL_PET))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Unlearn Hunter Abilities", GOSSIP_SENDER_MAIN, PET_REMOVE_SKILLS);

            // Stables for hunters only - Doesn't seem to work for other classes
            if (player->getClass() == CLASS_HUNTER)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Visit Stable", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Buy Pet Food", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);
            player->PlayerTalkClass->SendGossipMenu(PET_GOSSIP_HELLO, m_creature->GetGUID());
        }
        else if (action >= PET_PAGE_START_PETS && action < PET_PAGE_START_EXOTIC_PETS)
        {
            // PETS
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
            int page = action - PET_PAGE_START_PETS + 1;
            int maxPage = pets.size() / PET_PAGE_SIZE + (pets.size() % PET_PAGE_SIZE != 0);

            if (page > 1)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page - 2);

            if (page < maxPage)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_PETS + page);

            AddGossip(player, pets, page);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
        }
        else if (action >= PET_PAGE_START_EXOTIC_PETS && action < PET_PAGE_START_RARE_PETS)
        {
            // EXOTIC BEASTS
            // Teach Beast Mastery or Spirit Beasts won't work properly
            if (! (player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())))
            {
                player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
                std::ostringstream messageLearn;
                messageLearn << "I have taught you the art of Beast Mastery, " << player->GetName() << ".";
                m_creature->MonsterWhisper(messageLearn.str().c_str(), player);
            }

            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
            int page = action - PET_PAGE_START_EXOTIC_PETS + 1;
            int maxPage = exoticPets.size() / PET_PAGE_SIZE + (exoticPets.size() % PET_PAGE_SIZE != 0);

            if (page > 1)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS + page - 2);

            if (page < maxPage)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_EXOTIC_PETS + page);

            AddGossip(player, exoticPets, page);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
        }
        else if (action >= PET_PAGE_START_RARE_PETS && action < PET_PAGE_START_RARE_EXOTIC_PETS)
        {
            // RARE PETS
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
            int page = action - PET_PAGE_START_RARE_PETS + 1;
            int maxPage = rarePets.size() / PET_PAGE_SIZE + (rarePets.size() % PET_PAGE_SIZE != 0);

            if (page > 1)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page - 2);

            if (page < maxPage)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_PETS + page);

            AddGossip(player, rarePets, page);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
        }
        else if (action >= PET_PAGE_START_RARE_EXOTIC_PETS && action < PET_PAGE_MAX)
        {
            // RARE EXOTIC BEASTS
            // Teach Beast Mastery or Spirit Beasts won't work properly
            if (! (player->HasSpell(PET_SPELL_BEAST_MASTERY) || player->HasTalent(PET_SPELL_BEAST_MASTERY, player->GetActiveSpec())))
            {
                player->addSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
                std::ostringstream messageLearn;
                messageLearn << "I have taught you the art of Beast Mastery, " << player->GetName() << ".";
                m_creature->MonsterWhisper(messageLearn.str().c_str(), player);
            }

            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, PET_MAIN_MENU);
            int page = action - PET_PAGE_START_RARE_EXOTIC_PETS + 1;
            int maxPage = rareExoticPets.size() / PET_PAGE_SIZE + (rareExoticPets.size() % PET_PAGE_SIZE != 0);

            if (page > 1)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS + page - 2);

            if (page < maxPage)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, PET_PAGE_START_RARE_EXOTIC_PETS + page);

            AddGossip(player, rareExoticPets, page);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
        }
        else if (action == PET_REMOVE_SKILLS)
        {
            // remove pet and granted skills
            for (int i = 0; i < HunterSpells.size(); ++i)
                player->removeSpell(HunterSpells[i], SPEC_MASK_ALL, false);

            player->removeSpell(PET_SPELL_BEAST_MASTERY, SPEC_MASK_ALL, false);
            player->CLOSE_GOSSIP_MENU();
        }
        else if (action == GOSSIP_OPTION_STABLEPET)
        {
            // STABLE
            player->GetSession()->SendStablePet(m_creature->GetGUID());
        }
        else if (action == GOSSIP_OPTION_VENDOR)
        {
            // VENDOR
            player->GetSession()->SendListInventory(m_creature->GetGUID());
        }

        // BEASTS
        if (action > 1000)
            CreatePet(player, m_creature, action);
        return true;
    }

private:
    static void AddGossip(Player *player, std::map<std::string, uint32> &petMap, int page)
    {
        std::map<std::string, uint32>::iterator it;
        int count = 1;

        for (it = petMap.begin(); it != petMap.end(); it++)
        {
            if (count > (page - 1) * PET_PAGE_SIZE && count <= page * PET_PAGE_SIZE)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, it->first, GOSSIP_SENDER_MAIN, it->second);

            count++;
        }
    }
};

class BeastMasterConf : public WorldScript
{
public:
    BeastMasterConf() : WorldScript("BeastMasterConf") { }

    void OnBeforeConfigLoad(bool reload) override
    {
        if (!reload) {
            std::string conf_path = _CONF_DIR;
            std::string cfg_file = conf_path+"/npc_beastmaster.conf";
#ifdef WIN32
            cfg_file = "npc_beastmaster.conf";
#endif
            std::string cfg_def_file = cfg_file + ".dist";

            sConfigMgr->LoadMore(cfg_def_file.c_str());

            sConfigMgr->LoadMore(cfg_file.c_str());

            BeastMasterAnnounceToPlayer = sConfigMgr->GetBoolDefault("BeastMaster.Announce", true);
            BeastMasterHunterOnly = sConfigMgr->GetBoolDefault("BeastMaster.HunterOnly", true);
            BeastMasterAllowExotic = sConfigMgr->GetBoolDefault("BeastMaster.AllowExotic", true);
            BeastMasterKeepPetHappy = sConfigMgr->GetBoolDefault("BeastMaster.KeepPetHappy", false);
            BeastMasterCorePatch = sConfigMgr->GetBoolDefault("BeastMaster.CorePatch", false);
            BeastMasterMinLevel = sConfigMgr->GetIntDefault("BeastMaster.MinLevel", 10);
            BeastMasterHunterBeastMasteryRequired = sConfigMgr->GetIntDefault("BeastMaster.HunterBeastMasteryRequired", false);

            if (BeastMasterMinLevel < 0 || BeastMasterMinLevel > 80)
            {
                BeastMasterMinLevel = 10;
            }

            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.Pets", ""), pets);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.ExoticPets", ""), exoticPets);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.RarePets", ""), rarePets);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.RareExoticPets", ""), rareExoticPets);
        }
    }

private:
    static void LoadPets(std::string pets, std::map<std::string, uint32> &petMap)
    {
        std::string delimitedValue;
        std::stringstream petsStringStream;
        std::string petName;
        int count = 0;

        petsStringStream.str(pets);

        while (std::getline(petsStringStream, delimitedValue, ','))
        {
            if (count % 2 == 0)
            {
                petName = delimitedValue;
            }
            else
            {
                uint32 petId = atoi(delimitedValue.c_str());

                if (petId >= 0)
                {
                    petMap[petName] = petId;
                }
            }

            count++;
        }
    }
};

class BeastMaster_PlayerScript : public PlayerScript
{
    public:
    BeastMaster_PlayerScript()
        : PlayerScript("BeastMaster_PlayerScript")
    {
    }

    void OnBeforeUpdate(Player* player, uint32 /*p_time*/) override
    {
        if (BeastMasterKeepPetHappy && player->GetPet())
        {
            Pet* pet = player->GetPet();

            if (pet->getPetType() == HUNTER_PET)
            {
                pet->SetPower(POWER_HAPPINESS, PET_MAX_HAPPINESS);
            }
        }
    }
};

void AddBeastMasterScripts()
{
    new BeastMasterConf();
    new BeastMasterAnnounce();
    new BeastMaster();
    new BeastMaster_PlayerScript();
}
