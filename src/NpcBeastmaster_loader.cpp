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

#include "Config.h"
#include "NpcBeastmaster.h"
#include "ScriptMgr.h"

// forward‐declare your chat handler (if not already in header)
class BeastmasterRenameChatHandler;

// register all scripts & handlers
static void AddBeastMasterScripts()
{
    // register the NPC gossip/player‐update script
    ScriptMgr::AddScript(new NpcBeastmaster());
    // register the chat handler for rename/delete confirmations
    ScriptMgr::AddScript(new BeastmasterRenameChatHandler());
}

// this is the one function that SC loader will call
void AddSC_mod_npc_beastmaster()
{
    // respect the enable flag
    if (!sConfigMgr->GetOption<bool>("BeastMaster.Enable", true))
        return;

    AddBeastMasterScripts();
}
