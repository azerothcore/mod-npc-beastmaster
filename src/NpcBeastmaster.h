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
 * PetInfo
 * Structure to hold information about pets.
 */
struct PetInfo
{
    uint32 entry;
    std::string name;
    uint32 family;
    std::string rarity;
    uint32 icon; // e.g. "Ability_Hunter_Pet_Wolf"
};

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

    /**
     * Shows the tracked pets menu for the player, with pagination and actions.
     */
    void ShowTrackedPetsMenu(Player *player, Creature *creature, uint32 page = 1);

private:
    // Handles pet creation/adoption for the player.
    void CreatePet(Player *player, Creature *creature, uint32 action);

    // Adds pets to the gossip menu for the given page.
    void AddPetsToGossip(Player *player, std::vector<PetInfo> const &pets, uint32 page);

    // Handles the rename prompt for pets.
    void HandleRenamePet(Player *player, Creature *creature, uint32 entry);

    // Handles the delete confirmation for pets.
    void HandleDeletePet(Player *player, Creature *creature, uint32 entry);

    // Sorts pets by name (utility).
    void SortPetsByName(std::vector<PetInfo> &normalPets)
    {
        std::sort(normalPets.begin(), normalPets.end(), [](const PetInfo &a, const PetInfo &b)
                  { return a.name < b.name; });
    }

    std::mutex trackedPetsCacheMutex;
    std::unordered_map<uint64, std::vector<std::tuple<uint32, std::string, std::string>>> trackedPetsCache;
};

#define sNpcBeastMaster NpcBeastmaster::instance()

#endif // _NPC_BEAST_MASTER_H_
