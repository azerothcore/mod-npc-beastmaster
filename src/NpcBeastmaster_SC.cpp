/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#include "NpcBeastmaster.h"
#include "ScriptedCreature.h"
#include "ScriptedGossip.h"
#include "ScriptMgr.h"

 // BeastMasterEvents
constexpr auto BEASTMASTER_EVENT_EAT = 1;

class BeastMaster_CreatureScript : public CreatureScript
{
public:
    BeastMaster_CreatureScript() : CreatureScript("BeastMaster") { }

    bool OnGossipHello(Player* player, Creature* creature) override
    {
        sNpcBeastMaster->ShowMainMenu(player, creature);
        return true;
    }

    bool OnGossipSelect(Player *player, Creature* creature, uint32 /*sender*/, uint32 action) override
    {
        sNpcBeastMaster->GossipSelect(player, creature, action);
        return true;
    }

    struct beastmasterAI : public ScriptedAI
    {
        beastmasterAI(Creature* creature) : ScriptedAI(creature) { }

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

    CreatureAI* GetAI(Creature* creature) const override
    {
        return new beastmasterAI(creature);
    }
};

class BeastMaster_WorldScript : public WorldScript
{
public:
    BeastMaster_WorldScript() : WorldScript("BeastMaster_WorldScript", {
        WORLDHOOK_ON_BEFORE_CONFIG_LOAD
    }) { }

    void OnBeforeConfigLoad(bool /*reload*/) override
    {
        sNpcBeastMaster->LoadSystem();
    }
};

class BeastMaster_PlayerScript : public PlayerScript
{
public:
    BeastMaster_PlayerScript() : PlayerScript("BeastMaster_PlayerScript", {
        PLAYERHOOK_ON_BEFORE_UPDATE,
        PLAYERHOOK_ON_BEFORE_LOAD_PET_FROM_DB,
        PLAYERHOOK_ON_BEFORE_GUARDIAN_INIT_STATS_FOR_LEVEL
    }) { }

    void OnPlayerBeforeUpdate(Player* player, uint32 /*p_time*/) override
    {
        sNpcBeastMaster->PlayerUpdate(player);
    }

    void OnPlayerBeforeLoadPetFromDB(Player* /*player*/, uint32& /*petentry*/, uint32& /*petnumber*/, bool& /*current*/, bool& forceLoadFromDB) override
    {
        forceLoadFromDB = true;
    }

    void OnPlayerBeforeGuardianInitStatsForLevel(Player* /*player*/, Guardian* /*guardian*/, CreatureTemplate const* cinfo, PetType& petType) override
    {
        if (cinfo->IsTameable(true))
            petType = HUNTER_PET;
    }
};

void AddBeastMasterScripts()
{
    new BeastMaster_WorldScript();
    new BeastMaster_CreatureScript();
    new BeastMaster_PlayerScript();
}
