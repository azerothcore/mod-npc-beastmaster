/*
 * This file is part of the AzerothCore Project. See AUTHORS file for Copyright information
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU Affero General Public License as published by the
 * Free Software Foundation; either version 3 of the License, or (at your
 * option any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE. See the GNU Affero General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program. If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef _NPC_BEAST_MASTER_H_
#define _NPC_BEAST_MASTER_H_

#include "Common.h"
#include <map>
#include <algorithm> // Added for std::sort

class Player;
class Creature;

using PetStore = std::map<std::string, uint32>;

class NpcBeastmaster
{
    NpcBeastmaster() = default;
    ~NpcBeastmaster() = default;

    NpcBeastmaster(NpcBeastmaster const &) = delete;
    NpcBeastmaster(NpcBeastmaster &&) = delete;
    NpcBeastmaster &operator=(NpcBeastmaster const &) = delete;
    NpcBeastmaster &operator=(NpcBeastmaster &&) = delete;

public:
    static NpcBeastmaster *instance();

    void LoadSystem(bool reload = false);

    // Gossip
    void ShowMainMenu(Player *player, Creature *creature);
    void GossipSelect(Player *player, Creature *creature, uint32 action);

    // Player
    void PlayerUpdate(Player *player);

private:
    void CreatePet(Player *player, Creature *creature, uint32 action);
    void AddPetsToGossip(Player *player, std::vector<struct PetInfo> const &pets, uint32 page);
    void ShowTrackedPetsMenu(Player *player, Creature *creature, uint32 page = 1); // Updated: add page param for pagination
    void HandleRenamePet(Player *player, Creature *creature, uint32 entry);        // Added: for rename prompt

    void SortPetsByName(std::vector<struct PetInfo> &normalPets)
    {
        std::sort(normalPets.begin(), normalPets.end(), [](const PetInfo &a, const PetInfo &b)
                  { return a.name < b.name; });
    }
};

#define sNpcBeastMaster NpcBeastmaster::instance()

#endif // _NPC_BEAST_MASTER_H_
