#include "Config.h"
#include "Pet.h"
#include "ScriptPCH.h"
#include "Configuration/Config.h"
#include "ScriptedGossip.h"

std::vector<uint32> HunterSpells = { 883, 982, 2641, 6991, 48990, 1002, 1462, 6197 };
std::map<std::string, uint32> petsPage1;
std::map<std::string, uint32> petsPage2;
std::map<std::string, uint32> petsPage3;
std::map<std::string, uint32> exoticPetsPage1;
std::map<std::string, uint32> rarePetsPage1;
bool BeastMasterAnnounceToPlayer;
bool BeastMasterHunterOnly;
bool BeastMasterExoticNoSpec;
uint32 BeastMasterPetScale;
bool BeastMasterKeepPetHappy;

class BeastMasterAnnounce : public PlayerScript
{

public:

    BeastMasterAnnounce() : PlayerScript("BeastMasterAnnounce") {}

    void OnLogin(Player* player)
    {
        // Announce Module
        if (BeastMasterAnnounceToPlayer)
        {
            ChatHandler(player->GetSession()).SendSysMessage("This server is running the |cff4CFF00BeastMasterNPC |rmodule");
        }
    }
};

class BeastMaster : public CreatureScript
{

public:

    BeastMaster() : CreatureScript("BeastMaster") { }

    void CreatePet(Player *player, Creature * m_creature, uint32 entry)
    {

        // If enabled for Hunters only..
        if (BeastMasterHunterOnly)
        {
            if (player->getClass() != CLASS_HUNTER)
            {
                m_creature->MonsterWhisper("Silly fool, Pets are for Hunters!", player, false);
                m_creature->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
                player->CLOSE_GOSSIP_MENU();
                return;
            }
        }

        // Check if player already has a pet
        if (player->GetPet())
        {
            m_creature->MonsterWhisper("First you must abandon or stable your current pet!", player, false);
            player->CLOSE_GOSSIP_MENU();
            return;
        }

        // Create Tamed Creature
        Pet* pet = player->CreateTamedPetFrom(entry, 883);
        if (!pet) { return; }

        // Set Pet Happiness
        pet->SetPower(POWER_HAPPINESS, 1048000);

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

        // Scale Pet
        pet->SetObjectScale(BeastMasterPetScale);

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
            if (!player->HasSpell(883))
            {
                for (int i = 0; i < HunterSpells.size(); ++i)
                    player->learnSpell(HunterSpells[i]);
            }
        }

        // Farewell
        std::ostringstream messageAdopt;
        messageAdopt << "A fine choice " << player->GetName() << "! Your " << pet->GetName() << " shall know no law but that of the club and fang.";
        m_creature->MonsterWhisper(messageAdopt.str().c_str(), player);
        player->CLOSE_GOSSIP_MENU();
        m_creature->HandleEmoteCommand(EMOTE_ONESHOT_POINT);
    }

    bool OnGossipHello(Player *player, Creature * m_creature)
    {
        // Howl
        m_creature->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);

        // If enabled for Hunters only..
        if (BeastMasterHunterOnly)
        {
            if (player->getClass() != CLASS_HUNTER)
            {
                m_creature->MonsterWhisper("Silly fool, Pets are for Hunters!", player, false);
                m_creature->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
                player->CLOSE_GOSSIP_MENU();
                return false;
            }
        }

        if (player->getLevel() < 10)
        {
            m_creature->MonsterWhisper("Pets are not for the inexperienced!", player, false);
            m_creature->HandleEmoteCommand(EMOTE_ONESHOT_LAUGH);
            player->CLOSE_GOSSIP_MENU();
            return false;
        }

        // MAIN MENU
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Pets", GOSSIP_SENDER_MAIN, 51);
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Pets", GOSSIP_SENDER_MAIN, 70);

        // Allow Exotic Pets For hunters if they can tame
        if (!BeastMasterExoticNoSpec && player->getClass() == CLASS_HUNTER && (player->HasSpell(53270) || player->HasTalent(53270, player->GetActiveSpec())))
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, 60);
        }

        // Allow Exotic Pets regardless of spec
        // Hunters should spec Beast Mastery, all other classes get it for free
        if (BeastMasterExoticNoSpec && (player->getClass() != CLASS_HUNTER || player->HasSpell(53270) || player->HasTalent(53270, player->GetActiveSpec())))
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, 60);

        // remove pet skills (not for hunters)
        if (player->getClass() != CLASS_HUNTER)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Remove Pet Skills", GOSSIP_SENDER_MAIN, 80);

        // Stables for hunters only - Doesn't seem to work for other classes
        if (player->getClass() == CLASS_HUNTER)
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Visit Stable", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

        // Pet Food Vendor
        player->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Buy Pet Food", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);

        player->PlayerTalkClass->SendGossipMenu(601026, m_creature->GetGUID());

        // Howl/Roar
        player->PlayDirectSound(9036);
        m_creature->HandleEmoteCommand(EMOTE_ONESHOT_ROAR);

        return true;
    }

    bool OnGossipSelect(Player *player, Creature * m_creature, uint32 sender, uint32 action)
    {
        player->PlayerTalkClass->ClearMenus();

        switch (action)
        {
            // MAIN MENU
        case 50:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Pets", GOSSIP_SENDER_MAIN, 51);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Rare Pets", GOSSIP_SENDER_MAIN, 70);

            // Allow Exotics for all players
            // Allow Exotic Pets regardless of spec
            if (!BeastMasterExoticNoSpec && player->getClass() == CLASS_HUNTER && player->HasSpell(53270))
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, 60);

            // Allow Exotic Pets regardless of spec
            // Hunters should spec Beast Mastery, all other classes get it for free
            if (BeastMasterExoticNoSpec && player->getClass() != CLASS_HUNTER)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Browse Exotic Pets", GOSSIP_SENDER_MAIN, 60);

            // remove pet skills (not for hunters)
            if (player->getClass() != CLASS_HUNTER)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_BATTLE, "Remove Pet Skills", GOSSIP_SENDER_MAIN, 80);

            // Stables for hunters only - Doesn't seem to work for other classes
            if (player->getClass() == CLASS_HUNTER)
                player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TAXI, "Visit Stable", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_STABLEPET);

            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_MONEY_BAG, "Buy Pet Food", GOSSIP_SENDER_MAIN, GOSSIP_OPTION_VENDOR);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // PETS PAGE 1
        case 51:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, 50);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, 52);
            AddGossip(player, petsPage1);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // PETS PAGE 2
        case 52:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, 50);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, 51);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Next..", GOSSIP_SENDER_MAIN, 53);
            AddGossip(player, petsPage2);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // PETS PAGE 3
        case 53:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, 50);
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_INTERACT_1, "Previous..", GOSSIP_SENDER_MAIN, 52);
            AddGossip(player, petsPage3);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // EXOTIC BEASTS
        case 60:

            // Teach Beast Mastery or Spirit Beasts won't work properly
            if (! (player->HasSpell(53270) || player->HasTalent(53270, player->GetActiveSpec())))
            {
                player->addSpell(53270, SPEC_MASK_ALL, false);
                std::ostringstream messageLearn;
                messageLearn << "I have taught you the art of Beast Mastery " << player->GetName() << ".";
                m_creature->MonsterWhisper(messageLearn.str().c_str(), player);
            }

            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, 50);
            AddGossip(player, exoticPetsPage1);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // RARE PETS
        case 70:
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_TALK, "Back..", GOSSIP_SENDER_MAIN, 50);
            AddGossip(player, rarePetsPage1);
            player->PlayerTalkClass->SendGossipMenu(DEFAULT_GOSSIP_MESSAGE, m_creature->GetGUID());
            break;

            // remove pet and granted skills
        case 80:
            for (int i = 0; i < HunterSpells.size(); ++i)
                player->removeSpell(HunterSpells[i], SPEC_MASK_ALL, false);

            player->removeSpell(53270, SPEC_MASK_ALL, false);
            player->CLOSE_GOSSIP_MENU();
            break;

            // STABLE
        case GOSSIP_OPTION_STABLEPET:
            player->GetSession()->SendStablePet(m_creature->GetGUID());
            break;

            // VENDOR
        case GOSSIP_OPTION_VENDOR:
            player->GetSession()->SendListInventory(m_creature->GetGUID());
            break;
        }

        // BEASTS
        if (action > 1000)
            CreatePet(player, m_creature, action);
        return true;
    }

private:
    static void AddGossip(Player *player, std::map<std::string, uint32> &petMap)
    {
        std::map<std::string, uint32>::iterator it;

        for (it = petMap.begin(); it != petMap.end(); it++)
        {
            player->ADD_GOSSIP_ITEM(GOSSIP_ICON_VENDOR, it->first, GOSSIP_SENDER_MAIN, it->second);
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
            BeastMasterExoticNoSpec = sConfigMgr->GetBoolDefault("BeastMaster.ExoticNoSpec", true);
            BeastMasterPetScale = sConfigMgr->GetIntDefault("BeastMaster.PetScale", 1);
            BeastMasterKeepPetHappy = sConfigMgr->GetBoolDefault("BeastMaster.KeepPetHappy", false);

            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.PetsPage1", ""), petsPage1);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.PetsPage2", ""), petsPage2);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.PetsPage3", ""), petsPage3);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.ExoticPetsPage1", ""), exoticPetsPage1);
            LoadPets(sConfigMgr->GetStringDefault("BeastMaster.RarePetsPage1", ""), rarePetsPage1);
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
                pet->SetPower(POWER_HAPPINESS, 1048000);
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
