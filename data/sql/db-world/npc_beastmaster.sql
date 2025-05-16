-- ############################################################
-- BEASTMASTER NPC & VENDOR SETUP (Entry: 601026)
-- ############################################################

-- Variables for easy editing
SET
@Entry      := 601026,
@Model      := 26314, -- Northrend Worgen White
@Name       := "White Fang",
@Title      := "BeastMaster",
@Icon       := "",
@GossipMenu := 0, -- Let script handle gossip, not DB
@MinLevel   := 80,
@MaxLevel   := 80,
@Faction    := 35,
@NPCFlag    := 4194433, -- Vendor + Gossip + Stable + Trainer, etc.
@Scale      := 1.0,
@Rank       := 0,
@Type       := 7,
@TypeFlags  := 0,
@FlagsExtra := 2,
@AIName     := "",
@Script     := "BeastMaster";

-- Remove any existing NPC with this entry
DELETE FROM `creature_template` WHERE `entry` = @Entry;
INSERT INTO `creature_template` (
    `entry`, `name`, `subname`, `IconName`, `gossip_menu_id`, `minlevel`, `maxlevel`, `faction`, `npcflag`, `speed_walk`, `speed_run`, `scale`, `rank`, `unit_class`, `unit_flags`, `type`, `type_flags`, `RegenHealth`, `flags_extra`, `AiName`, `ScriptName`
) VALUES
(@Entry, @Name, @Title, @Icon, @GossipMenu, @MinLevel, @MaxLevel, @Faction, @NPCFlag, 1, 1.14286, 1, @Rank, 1, 2, @Type, @TypeFlags, 1, @FlagsExtra, @AIName, @Script);

-- Set display/model
DELETE FROM `creature_template_model` WHERE `CreatureID` = @Entry;
INSERT INTO `creature_template_model` (`CreatureID`, `Idx`, `CreatureDisplayID`, `DisplayScale`, `Probability`) VALUES
(@Entry, 0, @Model, @Scale, 1);

-- Equip the NPC with items (optional flavor)
DELETE FROM `creature_equip_template` WHERE `CreatureID`=@Entry AND `ID`=1;
INSERT INTO `creature_equip_template` VALUES (@Entry, 1, 2196, 1906, 0, 18019); -- Haunch of Meat, Torch

-- NPC greeting and menu text
DELETE FROM `npc_text` WHERE `ID` IN (601026, 601027);
INSERT INTO `npc_text` (`ID`, `text0_0`) VALUES 
(601026, 'Greetings, $N.$b$bIf you''re looking for a trustful companion to take on your travels, you have come to the right place. I can offer you a variety of tamed pets to choose from. I can also supply you with food so that you can take good care of your new friend.'),
(601027, 'What kind of pet are you interested in?');

-- Dummy gossip_menu to suppress DB warnings (not used by script)
DELETE FROM `gossip_menu` WHERE `MenuID` = 60102;
INSERT INTO `gossip_menu` VALUES (60102, 68);

-- ############################################################
-- BEASTMASTER VENDOR ITEMS
-- ############################################################

DELETE FROM npc_vendor WHERE entry = @Entry;

-- Add pet food and special items to vendor
INSERT INTO npc_vendor (entry, item) VALUES
(@Entry,35953),(@Entry,33454),(@Entry,27854),(@Entry,8952),(@Entry,4599),(@Entry,3771),(@Entry,3770),(@Entry,2287),(@Entry,117),
(@Entry,35947),(@Entry,33452),(@Entry,27859),(@Entry,8948),(@Entry,4608),(@Entry,4607),(@Entry,4606),(@Entry,4605),(@Entry,4604),
(@Entry,35950),(@Entry,33449),(@Entry,27855),(@Entry,8950),(@Entry,4601),(@Entry,4544),(@Entry,4542),(@Entry,4541),(@Entry,4540),
(@Entry,35948),(@Entry,35949),(@Entry,27856),(@Entry,8953),(@Entry,4602),(@Entry,4539),(@Entry,4538),(@Entry,4537),(@Entry,4536),
(@Entry,35951),(@Entry,33451),(@Entry,27858),(@Entry,8957),(@Entry,21552),(@Entry,4594),(@Entry,4593),(@Entry,4592),(@Entry,787),
(@Entry,35952),(@Entry,33443),(@Entry,27857),(@Entry,8932),(@Entry,3927),(@Entry,1707),(@Entry,422),(@Entry,414),(@Entry,2070),
(@Entry,33875),(@Entry,21024);


-- MEAT
-- 35953: Mead Blasted Caribou (75)
-- 33454: Salted Venison (65)
-- 27854: Smoked Talbuk Venison (55)
-- 8952: Roasted Quail (45)
-- 4599: Cured Ham Steak (35)
-- 3771: Wild Hog Shank (25)
-- 3770: Mutton Chop (15)
-- 2287: Haunch of Meat (5)
-- 117: Tough Jerky (1)

-- FUNGUS
-- 35947: Sparkling Frostcap (75)
-- 33452: Honey-Spiced Lichen (65)
-- 27859: Zangar Caps (55)
-- 8948: Dried King Bolete (45)
-- 4608: Raw Black Truffle (35)
-- 4607: Delicious Cave Mold (25)
-- 4606: Spongy Morel (15)
-- 4605: Red-Speckled Mushroom (5)
-- 4604: Forest Mushroom Cap (1)

-- BREAD
-- 35950: Sweet Potato Bread (75)
-- 33449: Crusty Flatbread (65)
-- 27855: Mag'har Grainbread (55)
-- 8950: Homemade Cherry Pie (45)
-- 4601: Soft Banana Bread (35)
-- 4544: Mulgore Spice Bread (25)
-- 4542: Moist Cornbread (15)
-- 4541: Freshly Baked Bread (5)
-- 4540: Tough Hunk of Bread (1)

-- FRUIT
-- 35948: Savory Snowplum (75)
-- 35949: Tundra Berries (65)
-- 27856: Sklethyl Berries (55)
-- 8953: Deep Fried Plantains (45)
-- 4602: Moon Harvest Pumpkin (35)
-- 4539: Goldenbark Apple (25)
-- 4538: Snapvine Watermelon (15)
-- 4537: Tel'Abim Banana (5)
-- 4536: Shiny Red Apple (1)

-- FISH
-- 35951: Poached Emperor Salmon (75)
-- 33451: Filet of Icefin (65)
-- 27858: Sunspring Carp (55)
-- 8957: Spinefin Halibut (45)
-- 21552: Striped Yellowtail (35)
-- 4594: Rockscale Cod (25)
-- 4593: Bristle Whisker Catfish (15)
-- 4592: Longjaw Mud Snapper (5)
-- 787: Slitherskin Mackeral (1)

-- CHEESE
-- 35952: Briny Hardcheese (75)
-- 33443: Sour Goat Cheese (65)
-- 27857: Gradar Sharp (55)
-- 8932: Alterac Swiss (45)
-- 3927: Fine Aged Cheddar (35)
-- 1707: Stormwind Brie (25)
-- 422: Dwarven Mild (15)
-- 414: Dalaran Sharp (5)
-- 2070: Darnassian Bleu (1)

-- BUFF/RARE
-- 33875: Kibler's Bits
-- 21024: Chimaerok Tenderloin

-- ############################################################
-- BEASTMASTER'S WHISTLE ITEM (Lucky Rocket Cluster repurposed)
-- ############################################################

-- This will update Lucky Rocket Cluster (entry 21744) to be the Beastmaster's Whistle.
-- If you want to use a different item, change the entry here and in your C++ code.

UPDATE item_template
SET
    name = "Beastmaster's Whistle",
    description = "Summons the Beastmaster menu.",
    displayid = 133706,      -- Whistle icon
    flags = 64,              -- Usable
    spellid_1 = 0,           -- No spell
    spelltrigger_1 = 0,      -- No spell trigger
    ScriptName = 'BeastmasterWhistle_ItemScript',
    maxcount = 1,            -- Only one per player
    bonding = 1              -- Bind on pickup
WHERE entry = 21744;

-- ############################################################
-- END OF FILE
-- ############################################################

-- After running this SQL:
-- 1. Restart your worldserver (required for item_template changes).
-- 2. Delete any old Lucky Rocket Cluster from your bags and use `.additem 21744` to get the whistle.
-- 3. Clear your WoW client Cache folder if the icon or name does not update.