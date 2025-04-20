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

#ifndef _NPC_BEAST_MASTER_H_
#define _NPC_BEAST_MASTER_H_

#include "Common.h"
#include <map>
#include <algorithm> // For std::sort
#include <mutex>
#include <unordered_map>
#include <vector>
#include <tuple>

class Player;
class Creature;

/**
 * NpcBeastmaster
 * Main class for the BeastMaster NPC module.
 * Handles pet adoption, tracked pets, menu logic, and thread-safe session cache.
 */
class NpcBeastmaster
{
    NpcBeastmaster() = default;
    ~NpcBeastmaster() = default;

    NpcBeastmaster(NpcBeastmaster const &) = delete;
    NpcBeastmaster(NpcBeastmaster &&) = delete;
    NpcBeastmaster &operator=(NpcBeastmaster const &) = delete;
    NpcBeastmaster &operator=(NpcBeastmaster &&) = delete;

public:
    /**
     * Singleton accessor.
     */
    static NpcBeastmaster *instance();

    /**
     * Loads all configuration options and pets from JSON and config.
     * Thread-safe for global pet lists.
     */
    void LoadSystem(bool reload = false);

    // Gossip menu logic
    void ShowMainMenu(Player *player, Creature *creature);
    void GossipSelect(Player *player, Creature *creature, uint32 action);

    // Player update logic (e.g., keep pet happy)
    void PlayerUpdate(Player *player);

    /**
     * Clears the tracked pets cache for a specific player.
     * Thread-safe.
     */
    void ClearTrackedPetsCache(Player *player);

private:
    // Handles pet creation/adoption for the player.
    void CreatePet(Player *player, Creature *creature, uint32 action);

    // Adds pets to the gossip menu for the given page.
    void AddPetsToGossip(Player *player, std::vector<struct PetInfo> const &pets, uint32 page);

    // Shows the tracked pets menu for the player, with pagination and actions.
    void ShowTrackedPetsMenu(Player *player, Creature *creature, uint32 page = 1);

    // Handles the rename prompt for pets.
    void HandleRenamePet(Player *player, Creature *creature, uint32 entry);

    // Sorts pets by name (utility).
    void SortPetsByName(std::vector<struct PetInfo> &normalPets)
    {
        std::sort(normalPets.begin(), normalPets.end(), [](const PetInfo &a, const PetInfo &b)
                  { return a.name < b.name; });
    }

    // --- Encapsulated tracked pets cache and mutex ---
    // Stores tracked pets per player session for efficient menu display.
    // All access must be protected by trackedPetsCacheMutex.
    std::mutex trackedPetsCacheMutex;
    std::unordered_map<uint64, std::vector<std::tuple<uint32, std::string, std::string>>> trackedPetsCache;
};

#define sNpcBeastMaster NpcBeastmaster::instance()

#endif // _NPC_BEAST_MASTER_H_
