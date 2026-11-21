#pragma once

// The scripts the Random Map Generator's Example Scripts menu loads, one for each
// base tileset, each mixing a little of two other tilesets into its own, and
// Frostfall Reach, a continent of six.

namespace galaxy {

struct ExampleRandomMapScript {
    const char* world;
    const char* title;
    const char* text;
};

inline constexpr ExampleRandomMapScript kExampleRandomMapScripts[] = {
    {"GreenWorld", "Continental", R"(/* GREENWORLD: Continental
   After Age of Empires II's Continental: one great GreenWorld continent in a
   green sea, every player on it, with forests, Lava dirt patches, Lush grass,
   hills and a few cliffs.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as GREENWORLD_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. Sizes from the
   original are scaled down to match, since its tiles are far smaller. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain GREENWORLD_BASE-0

/* The heart of the continent. */
create_land {
  terrain_type GREENWORLD_BASE-85
  land_percent 10
  base_size 2
  left_border 25
  right_border 25
  top_border 25
  bottom_border 25
}

/* The players spread out from it, kept a little way from the map's edges so the
   sea runs all the way round. The original keeps 6 to 8 percent of the map
   clear with border_fuzziness 10; squares are so much bigger than its tiles
   that the borders here are a little wider and kept more strictly. */
create_player_lands {
  terrain_type GREENWORLD_BASE-85
  land_percent 50
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  start_random
    percent_chance 50
      left_border 12
      right_border 9
    percent_chance 50
      left_border 9
      right_border 12
  end_random
  start_random
    percent_chance 50
      top_border 12
      bottom_border 9
    percent_chance 50
      top_border 9
      bottom_border 12
  end_random
  border_fuzziness 60
  set_zone_randomly
  other_zone_avoidance_distance 1
}

<TERRAIN_GENERATION>
/* Mixing the water: broad shallows out at sea, and a scatter of small ones. */
create_terrain GREENWORLD_BASE-68 {
  base_terrain GREENWORLD_BASE-0
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  land_percent 8
}
create_terrain GREENWORLD_BASE-68 {
  base_terrain GREENWORLD_BASE-0
  number_of_clumps 6
  spacing_to_other_terrain_types 1
  land_percent 2
}

/* Dirt: bare Lava ground in broad patches. Players still start on grass. */
create_terrain LAVA_BASE-86 {
  base_terrain GREENWORLD_BASE-85
  number_of_clumps 6
  spacing_to_other_terrain_types 1
  land_percent 6
  set_avoid_player_start_areas 1
}

/* Grass3: richer Lush grass in small patches. */
create_terrain LUSH_BASE-77 {
  base_terrain GREENWORLD_BASE-85
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  land_percent 3
  set_avoid_player_start_areas 1
}

/* Dirt3: small specks of Lava ground. */
create_terrain LAVA_BASE-86 {
  base_terrain GREENWORLD_BASE-85
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  land_percent 2
  set_avoid_player_start_areas 1
}

<ELEVATION_GENERATION>
/* Hills: GreenWorld's own hill stamps, out of the players' start areas. */
create_elevation 7 {
  base_terrain GREENWORLD_BASE-85
  number_of_clumps 5
  set_scale_by_groups
}

<CLIFF_GENERATION>
/* Short cliffs: ridges of GREENWORLD_BASE-219 with cliff faces. Lengths are
   squares, and the count grows with the map. */
min_number_of_cliffs 2
max_number_of_cliffs 3
min_length_of_cliff 2
max_length_of_cliff 4
cliff_curliness 10
min_distance_cliffs 1

<CONNECTION_GENERATION>
/* Shallow fords where the sea divides the players. */
create_connect_all_players_land {
  replace_terrain GREENWORLD_BASE-0 GREENWORLD_BASE-68
  terrain_cost GREENWORLD_BASE-0 7
  terrain_cost GREENWORLD_BASE-68 3
  terrain_cost GREENWORLD_BASE-85 2
  terrain_cost LAVA_BASE-86 2
  terrain_cost LUSH_BASE-77 2
  terrain_size GREENWORLD_BASE-0 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start for each player. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Gold and stone become metal: some near each start, more out on the continent. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 26
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 1
  set_scaling_to_map_size
  min_distance_to_players 36
  temp_min_distance_group_placement 20
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* The forests: dense stands of GreenWorld trees out from the starts, a square
   or so across. */
create_object TREES {
  number_of_objects 160
  number_of_groups 0.5
  group_placement_radius 18
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on GREENWORLD_BASE-85
  min_distance_to_players 16
  min_distance_group_placement 10
}

/* Palm groves on the dirt: Archipelago's palms on Lava ground. */
create_object ARCHIPELAGO_PALMS {
  number_of_objects 20
  number_of_groups 0.3
  group_placement_radius 6
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on LAVA_BASE-86
  min_distance_to_players 12
}

/* Lush trees in the Lush grass. */
create_object TREES {
  number_of_objects 6
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  terrain_to_place_on LUSH_BASE-77
  min_distance_to_players 12
}

/* Straggler trees and forage bushes by each start. */
create_object TREES {
  number_of_objects 4
  set_place_for_every_player
  terrain_to_place_on GREENWORLD_BASE-85
  min_distance_to_players 8
  max_distance_to_players 16
}
create_object SHRUBS {
  number_of_objects 6
  number_of_groups 1
  group_placement_radius 2
  set_tight_grouping
  set_place_for_every_player
  min_distance_to_players 8
  max_distance_to_players 14
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}
)"},
    {"Lava", "Coastal", R"(/* LAVA: Coastal
   After Age of Empires II's Coastal: one great shared land of black Lava ground,
   every player on it, ringed by a sea of molten lava. Groves stand back from
   the starts, with red Mars dirt and pale Desert ash in patches.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as LAVA_BASE-86, and one tile
   is one square of the map's game size: 16 x 16 map tiles. Sizes from the
   original are scaled down to match, since its tiles are far smaller. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain LAVA_BASE-41

/* All the players share one land, zone 1, so it grows into a single shore.
   The original leaves one side's coast a little wider than the other three. */
create_player_lands {
  terrain_type LAVA_BASE-86
  start_random
    percent_chance 33
      land_percent 52
    percent_chance 33
      land_percent 60
    percent_chance 34
      land_percent 65
  end_random
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  /* The sea all round. On tiny maps a square is a big share of the map, so
     the borders there are a little wider to keep a square of sea. */
  if TINY_MAP
    start_random
      percent_chance 25
        left_border 12
        top_border 17
        bottom_border 12
        right_border 12
      percent_chance 25
        left_border 12
        top_border 12
        right_border 17
        bottom_border 12
      percent_chance 25
        right_border 12
        left_border 12
        bottom_border 17
        top_border 12
      percent_chance 25
        right_border 17
        top_border 12
        bottom_border 12
        left_border 12
    end_random
  else
    start_random
      percent_chance 25
        left_border 9
        top_border 12
        bottom_border 9
        right_border 9
      percent_chance 25
        left_border 9
        top_border 9
        right_border 12
        bottom_border 9
      percent_chance 25
        right_border 9
        left_border 9
        bottom_border 12
        top_border 9
      percent_chance 25
        right_border 12
        top_border 9
        bottom_border 9
        left_border 9
    end_random
  endif
  border_fuzziness 70
  zone 1
  other_zone_avoidance_distance 2
  clumping_factor 15
}

<TERRAIN_GENERATION>
/* Dirt: red Mars ground in broad patches. */
create_terrain MARS_BASE-85 {
  base_terrain LAVA_BASE-86
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  land_percent 7
  set_avoid_player_start_areas 1
}

/* Grass3 and Dirt3: small specks of pale Desert ash. */
create_terrain DESERT_BASE-80 {
  base_terrain LAVA_BASE-86
  number_of_clumps 4
  spacing_to_other_terrain_types 0
  land_percent 2
  set_avoid_player_start_areas 1
}

/* A little high ground behind cliffs. */
create_terrain LAVA_BASE-190 {
  base_terrain LAVA_BASE-86
  number_of_clumps 2
  spacing_to_other_terrain_types 1
  land_percent 2
  set_avoid_player_start_areas 1
}

/* Lava pools inland. */
create_terrain LAVA_POOL-86_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<ELEVATION_GENERATION>
/* A few of Lava's own hill stamps, out of the players' start areas. */
create_elevation 4 {
  base_terrain LAVA_BASE-86
  number_of_clumps 4
  set_scale_by_groups
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as ROCKS pick what suits the ground. */

/* A start for each player. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Gold and stone become metal: some near each start, more out on the land. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 26
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 1
  set_scaling_to_map_size
  min_distance_to_players 36
  temp_min_distance_group_placement 20
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* The forests: palm groves on the black ground, out from the starts. */
create_object ARCHIPELAGO_PALMS {
  number_of_objects 120
  number_of_groups 0.4
  group_placement_radius 14
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on LAVA_BASE-86
  min_distance_to_players 16
  min_distance_group_placement 10
}

/* Straggler palms by each start. */
create_object ARCHIPELAGO_PALMS {
  number_of_objects 4
  set_place_for_every_player
  terrain_to_place_on LAVA_BASE-86
  min_distance_to_players 8
  max_distance_to_players 16
}

/* Lava spires and nodes strewn across the land. */
create_object LAVA_SPIRES {
  number_of_objects 4
  set_scaling_to_map_size
  min_distance_to_players 10
}
create_object LAVA_NODES {
  number_of_objects 4
  set_scaling_to_map_size
  min_distance_to_players 10
}

/* Hardy plants on the Mars dirt. */
create_object PLANTS {
  number_of_objects 4
  set_scaling_to_map_size
  terrain_to_place_on MARS_BASE-85
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}
)"},
    {"Mars", "Mountain Pass", R"(/* MARS: Mountain Pass
   After Age of Empires II's Mountain Pass: Mars ground crowded with mountains,
   with narrow passes winding between them, a little Crystal and Lava ground,
   crystal groves and a few pools.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as MARS_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. Sizes from the
   original are scaled down to match, since its tiles are far smaller. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain MARS_BASE-85

/* The whole map is the players' land; mountains fill it below. */
create_player_lands {
  terrain_type MARS_BASE-85
  land_percent 100
  base_size 1
  border_fuzziness 20
  clumping_factor 20
  other_zone_avoidance_distance 0
}

<ELEVATION_GENERATION>
/* Foothills: Mars's own hill stamps, out of the players' start areas. */
create_elevation 5 {
  base_terrain MARS_BASE-85
  number_of_clumps 8
  set_scale_by_groups
}

<TERRAIN_GENERATION>
/* The mountains. The original raises its peaks with mountain objects; here
   they are MARS_BASE-255 high ground behind cliffs, which no unit can climb:
   a few short ranges, then many lone peaks between them. */
create_terrain MARS_BASE-255 {
  base_terrain MARS_BASE-85
  land_percent 10
  number_of_clumps 2
  set_scale_by_groups
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 2
}
create_terrain MARS_BASE-255 {
  base_terrain MARS_BASE-85
  land_percent 4
  number_of_clumps 6
  set_scale_by_groups
  spacing_to_other_terrain_types 2
  set_avoid_player_start_areas 2
}

/* Rocky outcrops: Mars's own feature stamps, where the original has smaller
   mountain objects. */
create_terrain MARS_FEATURE-85 {
  number_of_clumps 3
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

/* Crystal ground where the original has snow forests. */
create_terrain CRYSTAL_BASE-85 {
  base_terrain MARS_BASE-85
  land_percent 4
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Lava ground where the original has snowy grass. */
create_terrain LAVA_BASE-86 {
  base_terrain MARS_BASE-85
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Pools where the original has frozen ponds, and one of lava. */
create_terrain MARS_POOL-85_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}
create_terrain LAVA_POOL-86_8X8 {
  number_of_clumps 1
  set_scale_by_groups
}

<CONNECTION_GENERATION>
/* The passes: where the mountains shut one player off from another, a way is
   cut through them. Open ground is far cheaper, so most passes wind round. */
create_connect_all_players_land {
  replace_terrain MARS_BASE-255 MARS_BASE-85
  terrain_cost MARS_BASE-255 12
  terrain_cost MARS_BASE-85 1
  terrain_cost CRYSTAL_BASE-85 1
  terrain_cost LAVA_BASE-86 1
  terrain_size MARS_BASE-255 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as ROCKS pick what suits the ground. */

/* A start for each player. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Gold and stone become metal: some near each start, more in the passes. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 24
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 1
  set_scaling_to_map_size
  min_distance_to_players 36
  temp_min_distance_group_placement 20
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Crystal groves on the Crystal ground, where the original has its forests. */
create_object CRYSTAL_SPIRES {
  number_of_objects 30
  number_of_groups 0.3
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on CRYSTAL_BASE-85
}

/* Boulders strewn through the passes. */
create_object ROCKS {
  number_of_objects 4
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  terrain_to_place_on MARS_BASE-85
  min_distance_group_placement 10
  min_distance_to_players 12
}

/* Rocks up on the mountains. */
create_object ROCKS {
  number_of_objects 3
  set_scaling_to_map_size
  terrain_to_place_on MARS_BASE-255
}
)"},
    {"Ice", "Frost Teeth", R"(/* ICE: Frost Teeth
   Frozen islands with ice ridges, patches of GreenWorld tundra and
   Crystal fields glinting through the snow.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as ICE_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain ICE_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type ICE_BASE-85
  land_percent 50
  /* Small maps have room for smaller starting areas only. */
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  other_zone_avoidance_distance 2
  border_fuzziness 30
  clumping_factor 15
}

<TERRAIN_GENERATION>
/* Patches of tundra grass. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain ICE_BASE-85
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Crystal fields. */
create_terrain CRYSTAL_BASE-85 {
  base_terrain ICE_BASE-85
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain ICE_BASE-170 {
  base_terrain ICE_BASE-85
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Ice: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain ICE_HILLS-85_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ICE_HILLS-85_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ICE_POOLS-85 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ICE_ISLAND-0_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain ICE_BASE-0 ICE_BASE-85
  terrain_size ICE_BASE-0 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start on each player's island. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Metal near each start, and more out in the open. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 28
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 2
  set_scaling_to_map_size
  min_distance_to_players 40
  temp_min_distance_group_placement 24
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Ice chunks. */
create_object ICECHUNKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}

/* Trees on the tundra. */
create_object TREES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Crystals in the fields. */
create_object CRYSTAL_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
}
)"},
    {"Crystal", "Prism Reach", R"(/* CRYSTAL: Prism Reach
   Purple Crystal islands, with frosted Ice slopes and pools of green
   Acid ground.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as CRYSTAL_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain CRYSTAL_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type CRYSTAL_BASE-85
  land_percent 50
  /* Small maps have room for smaller starting areas only. */
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  other_zone_avoidance_distance 2
  border_fuzziness 30
  clumping_factor 15
}

<TERRAIN_GENERATION>
/* Frosted Ice slopes. */
create_terrain ICE_BASE-85 {
  base_terrain CRYSTAL_BASE-85
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Acid ground. */
create_terrain ACID_BASE-85 {
  base_terrain CRYSTAL_BASE-85
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Crystal: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain CRYSTAL_CRATER-85_8X8 {
  number_of_clumps 3
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain CRYSTAL_HILLS-85_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain CRYSTAL_POND-85 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain CRYSTAL_REEF-85_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain CRYSTAL_BASE-0 CRYSTAL_BASE-85
  terrain_size CRYSTAL_BASE-0 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start on each player's island. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Metal near each start, and more out in the open. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 28
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 2
  set_scaling_to_map_size
  min_distance_to_players 40
  temp_min_distance_group_placement 24
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Crystal spires. */
create_object CRYSTAL_SPIRES {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}

/* Ice chunks on the slopes. */
create_object ICECHUNKS {
  number_of_objects 1
  set_scaling_to_map_size
}
)"},
    {"Acid", "Toxic Delta", R"(/* ACID: Toxic Delta
   Islands of olive Acid ground in an acid sea, with Desert sand and a
   little GreenWorld grass where it has not been eaten away.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as ACID_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain ACID_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type ACID_BASE-85
  land_percent 50
  /* Small maps have room for smaller starting areas only. */
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  other_zone_avoidance_distance 2
  border_fuzziness 30
  clumping_factor 15
}

<TERRAIN_GENERATION>
/* Desert sand. */
create_terrain DESERT_BASE-80 {
  base_terrain ACID_BASE-85
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Grass the acid has not reached. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain ACID_BASE-85
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Acid: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain ACID_MOUNDS-85_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ACID_PUDDLES-85_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ACID_HILLS-85_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ACID_ISLANDS-0_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain ACID_BASE-0 ACID_BASE-85
  terrain_size ACID_BASE-0 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start on each player's island. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Metal near each start, and more out in the open. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 28
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 2
  set_scaling_to_map_size
  min_distance_to_players 40
  temp_min_distance_group_placement 24
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}

/* Trees on the grass. */
create_object TREES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Plants on the sand. */
create_object PLANTS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}
)"},
    {"Archipelago", "Islands", R"(/* ARCHIPELAGO: Islands
   After Age of Empires II's Islands: every player on an island of their own,
   with small resource islands out at sea between them. Sandy Archipelago
   islands with palm groves, a little GreenWorld grass and Desert ground, and
   shallows in the deep water. No land joins the islands: this is a naval map.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as ARCHIPELAGO_BASE-78, and one tile
   is one square of the map's game size: 16 x 16 map tiles. Sizes from the
   original are scaled down to match, since its tiles are far smaller. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain ARCHIPELAGO_BASE-0

/* An island for each player, with open water between every two. */
create_player_lands {
  terrain_type ARCHIPELAGO_BASE-78
  land_percent 30
  base_size 1
  left_border 7
  right_border 7
  top_border 7
  bottom_border 7
  border_fuzziness 50
  clumping_factor 15
  other_zone_avoidance_distance 2
}

/* Resource islands in the middle of the map: two, three on a 20 x 20 map and
   four on bigger ones. The original has more, but its sea is roomier. Each is
   its own land, so the metal below can be put on it by land_id. */
create_land {
  terrain_type ARCHIPELAGO_BASE-78
  land_percent 2
  base_size 1
  border_fuzziness 10
  other_zone_avoidance_distance 2
  land_id 20
  left_border 25
  right_border 25
  top_border 25
  bottom_border 25
  zone 56
}
create_land {
  terrain_type ARCHIPELAGO_BASE-78
  land_percent 2
  base_size 1
  border_fuzziness 10
  other_zone_avoidance_distance 2
  land_id 23
  left_border 25
  right_border 25
  top_border 25
  bottom_border 25
  zone 57
}
if HUGE_MAP
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 21
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 58
  }
elseif GIGANTIC_MAP
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 21
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 58
  }
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 24
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 59
  }
elseif MAPSIZE_GIANT
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 21
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 58
  }
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 24
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 59
  }
elseif LUDIKRIS_MAP
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 21
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 58
  }
  create_land {
    terrain_type ARCHIPELAGO_BASE-78
    land_percent 2
    base_size 1
    border_fuzziness 10
    other_zone_avoidance_distance 2
    land_id 24
    left_border 25
    right_border 25
    top_border 25
    bottom_border 25
    zone 59
  }
endif

<TERRAIN_GENERATION>
/* Mixing the water: shallows here and there in the deep sea. */
create_terrain ARCHIPELAGO_BASE-68 {
  base_terrain ARCHIPELAGO_BASE-0
  number_of_clumps 8
  spacing_to_other_terrain_types 0
  land_percent 8
}

/* A little GreenWorld grass and Desert ground on the islands. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain ARCHIPELAGO_BASE-78
  number_of_clumps 2
  spacing_to_other_terrain_types 1
  land_percent 2
  set_avoid_player_start_areas 1
}
create_terrain DESERT_BASE-80 {
  base_terrain ARCHIPELAGO_BASE-78
  number_of_clumps 2
  spacing_to_other_terrain_types 1
  land_percent 2
  set_avoid_player_start_areas 1
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start on each player's island. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Metal on each player's island, and more on the resource islands. */
create_object METAL_NODES {
  number_of_objects 4
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 26
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 4
  place_on_specific_land_id 20
  temp_min_distance_group_placement 4
}
create_object METAL_NODES {
  number_of_objects 4
  place_on_specific_land_id 23
  temp_min_distance_group_placement 4
}
if HUGE_MAP
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 21
    temp_min_distance_group_placement 4
  }
elseif GIGANTIC_MAP
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 21
    temp_min_distance_group_placement 4
  }
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 24
    temp_min_distance_group_placement 4
  }
elseif MAPSIZE_GIANT
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 21
    temp_min_distance_group_placement 4
  }
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 24
    temp_min_distance_group_placement 4
  }
elseif LUDIKRIS_MAP
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 21
    temp_min_distance_group_placement 4
  }
  create_object METAL_NODES {
    number_of_objects 4
    place_on_specific_land_id 24
    temp_min_distance_group_placement 4
  }
endif

/* Metal on the sea floor in the shallows, where the original has its fish. */
create_object METAL_NODES {
  number_of_objects 1
  set_scaling_to_map_size
  terrain_to_place_on ARCHIPELAGO_BASE-68
  temp_min_distance_group_placement 12
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Forests: groves of Archipelago trees and palms, out from the starts. */
create_object TREES {
  number_of_objects 60
  number_of_groups 0.6
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on ARCHIPELAGO_BASE-78
  min_distance_to_players 16
  min_distance_group_placement 8
}

/* Straggler palms by each start. */
create_object PALMS {
  number_of_objects 4
  set_place_for_every_player
  min_distance_to_players 8
  max_distance_to_players 16
}

/* A few trees on the grass. */
create_object TREES {
  number_of_objects 4
  set_scaling_to_map_size
  terrain_to_place_on GREENWORLD_BASE-85
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}
)"},
    {"Lush", "Black Forest", R"(/* LUSH: Black Forest
   After Age of Empires II's Black Forest: a dark Lush forest over the whole map,
   with a GreenWorld clearing for each player and narrow paths cut between them.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as LUSH_BASE-77, and one tile
   is one square of the map's game size: 16 x 16 map tiles.
   Age of Empires II paints its forest as terrain. Here the forest floor is
   LUSH_BASE-77 and the trees are features, placed thickly on it below. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
/* The forest floor covers the map. */
base_terrain LUSH_BASE-77

/* A grass clearing for each player. Age of Empires II gives the players half
   the map and lets forest grow back over most of it; here the clearings are
   kept small to begin with. */
create_player_lands {
  terrain_type GREENWORLD_BASE-85
  land_percent 8
  base_size 0
  other_zone_avoidance_distance 2
  border_fuzziness 30
}

<TERRAIN_GENERATION>
/* Small glades deep in the forest. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain LUSH_BASE-77
  land_percent 5
  number_of_clumps 9
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Leaf-strewn swamp hollows under the trees. */
create_terrain LUSH_SWAMP-77 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

/* Ponds for the fish. */
create_terrain LUSH_POND-77_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

/* Age of Empires II raises the ground with create_elevation; here hills do. */
create_terrain LUSH_HILL-77_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}
create_terrain GREENWORLD_HILL-85_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Paths cut through the forest from every player to every other. */
create_connect_all_players_land {
  replace_terrain LUSH_BASE-77 GREENWORLD_BASE-85
  terrain_size LUSH_BASE-77 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start in each player's clearing. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Gold becomes metal: some near each start, more hidden in the forest. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 24
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 40
  temp_min_distance_group_placement 24
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* The Black Forest itself: trees packed onto the forest floor. */
create_object TREES {
  number_of_objects 16
  number_of_groups 70
  group_placement_radius 5
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on LUSH_BASE-77
  min_distance_to_players 14
}

/* Undergrowth among the trees. */
create_object FOLIAGE {
  number_of_objects 6
  number_of_groups 8
  set_scaling_to_map_size
  terrain_to_place_on LUSH_BASE-77
  min_distance_to_players 14
}
create_object VINES {
  number_of_objects 4
  set_scaling_to_map_size
  terrain_to_place_on LUSH_BASE-77
  min_distance_to_players 14
}

/* A few straggler trees and berry-bush shrubs in each clearing. */
create_object TREES {
  number_of_objects 4
  set_place_for_every_player
  min_distance_to_players 8
  max_distance_to_players 16
}
create_object SHRUBS {
  number_of_objects 5
  number_of_groups 1
  group_placement_radius 2
  set_place_for_every_player
  min_distance_to_players 8
  max_distance_to_players 14
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}
)"},
    {"Desert", "Arabia", R"(/* DESERT: Arabia
   After Age of Empires II's Arabia: open Desert land with no sea, scattered
   palm groves, sandy Archipelago patches, a few Lush oases and ponds, and
   plenty of high ground and cliffs to fight over.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as DESERT_BASE-80, and one tile
   is one square of the map's game size: 16 x 16 map tiles. Sizes from the
   original are scaled down to match, since its tiles are far smaller. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain DESERT_BASE-80

/* A clear start for each player, spread across the open desert. */
create_player_lands {
  terrain_type DESERT_BASE-80
  land_percent 25
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  border_fuzziness 15
}

<ELEVATION_GENERATION>
/* Rolling hills: Desert's own hill stamps, out of the players' start areas. */
create_elevation 7 {
  base_terrain DESERT_BASE-80
  number_of_clumps 6
  set_scale_by_groups
}

<CLIFF_GENERATION>
/* Cliffs: short ridges of DESERT_BASE-155 with cliff faces. Lengths are
   squares, and the count grows with the map. */
min_number_of_cliffs 3
max_number_of_cliffs 5
min_length_of_cliff 2
max_length_of_cliff 4
cliff_curliness 25
min_distance_cliffs 1

<TERRAIN_GENERATION>
/* Elevated ground: broad rises of DESERT_BASE-155 behind cliffs. */
create_terrain DESERT_BASE-155 {
  base_terrain DESERT_BASE-80
  land_percent 8
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Peaks of DESERT_BASE-230 on the biggest rises. */
create_terrain DESERT_BASE-230 {
  base_terrain DESERT_BASE-155
  land_percent 1
  number_of_clumps 2
  spacing_to_other_terrain_types 1
}

/* Desert: a few patches of lighter Archipelago sand across the dirt. */
create_terrain ARCHIPELAGO_BASE-78 {
  base_terrain DESERT_BASE-80
  land_percent 3
  number_of_clumps 4
  spacing_to_other_terrain_types 1
}

/* Grass3: small Lush oases. */
create_terrain LUSH_BASE-77 {
  base_terrain DESERT_BASE-80
  land_percent 1
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Ponds out in the desert. */
create_terrain DESERT_BASE-0 {
  base_terrain DESERT_BASE-80
  land_percent 1
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 2
}

/* Rough ground: Desert's rough patch stamps. */
create_terrain DESERT_ROUGHS-80_8X8 {
  number_of_clumps 3
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground; on
   Desert ground TREES are Archipelago's palms and trees. */

/* A start for each player. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Gold and stone become metal: some near each start, more out in the desert. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 24
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 1
  set_scaling_to_map_size
  min_distance_to_players 36
  temp_min_distance_group_placement 20
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Palm groves: Arabia's forests, a square or so across, out from the starts. */
create_object TREES {
  number_of_objects 160
  number_of_groups 0.5
  group_placement_radius 12
  set_tight_grouping
  set_scaling_to_map_size
  terrain_to_place_on DESERT_BASE-80
  min_distance_to_players 18
  min_distance_group_placement 12
}

/* Straggler palms by each start. */
create_object PALMS {
  number_of_objects 4
  set_place_for_every_player
  terrain_to_place_on DESERT_BASE-80
  min_distance_to_players 8
  max_distance_to_players 16
}

/* Trees around the oases. */
create_object LUSH_TREES {
  number_of_objects 6
  set_scaling_to_map_size
  terrain_to_place_on LUSH_BASE-77
}

/* Rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}
)"},
    {"Metal", "Foundry Grid", R"(/* METAL: Foundry Grid
   Metal plating islands, with overgrown GreenWorld parks and Lava
   leaking from broken vents.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as METAL_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain METAL_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type METAL_BASE-85
  land_percent 50
  /* Small maps have room for smaller starting areas only. */
  if TINY_MAP
    base_size 1
  else
    base_size 2
  endif
  other_zone_avoidance_distance 2
  border_fuzziness 30
  clumping_factor 15
}

<TERRAIN_GENERATION>
/* Overgrown parks. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain METAL_BASE-85
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Lava leaking through. */
create_terrain LAVA_BASE-86 {
  base_terrain METAL_BASE-85
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain METAL_BASE-139 {
  base_terrain METAL_BASE-85
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Shallow water between the players' islands. */
create_connect_all_players_land {
  replace_terrain METAL_BASE-0 METAL_BASE-68
  terrain_size METAL_BASE-0 0 0
}

<OBJECTS_GENERATION>
/* Doodads. Here one tile is one feature cell, 16 x 16 pixels: a game-size
   square is 32 of them. Kinds such as TREES pick what suits the ground. */

/* A start on each player's island. */
create_object START_POSITION {
  set_place_for_every_player
  max_distance_to_players 0
}

/* Metal near each start, and more out in the open. */
create_object METAL_NODES {
  number_of_objects 3
  set_place_for_every_player
  min_distance_to_players 10
  max_distance_to_players 28
  temp_min_distance_group_placement 6
}
create_object METAL_NODES {
  number_of_objects 2
  set_scaling_to_map_size
  min_distance_to_players 40
  temp_min_distance_group_placement 24
}

/* A geothermal vent near each start. */
create_object GEO_VENTS {
  number_of_objects 1
  set_place_for_every_player
  min_distance_to_players 12
  max_distance_to_players 32
}

/* Towers. */
create_object METAL_TOWERS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}

/* Pipes. */
create_object PIPES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Trees in the parks. */
create_object TREES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Spires in the leaks. */
create_object LAVA_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
}
)"},
    {"Mixed", "Frostfall Reach", R"(/* ==================================================================
   FROSTFALL REACH  -  a Galaxy Editor random map script
   A continent in a cold green sea: glaciers and ice ridges in the
   north, a tundra belt, a dry desert heart with mesas and oases,
   temperate grassland in the south, a jungle in the south-west and
   an active volcano in the south-east.
   Best on maps of 16 x 16 game size or larger.
   ================================================================== */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
/* The sea. */
base_terrain GREENWORLD_BASE-0

/* The northern glacier, kept in the top third of the map. */
create_land {
  terrain_type ICE_BASE-85
  land_position 50 14
  base_size 2
  land_percent 18
  top_border 5
  bottom_border 67
  left_border 7
  right_border 7
  border_fuzziness 25
  clumping_factor 12
}

/* Tundra: a belt of cold grassland below the ice. */
create_land {
  terrain_type GREENWORLD_BASE-85
  land_position 50 34
  base_size 1
  land_percent 9
  top_border 27
  bottom_border 57
  left_border 9
  right_border 9
  border_fuzziness 30
}

/* The desert heart of the continent. */
create_land {
  terrain_type DESERT_BASE-80
  land_position 50 52
  base_size 2
  land_percent 16
  top_border 40
  bottom_border 35
  left_border 11
  right_border 11
  border_fuzziness 20
  clumping_factor 14
}

/* Southern grassland. */
create_land {
  terrain_type GREENWORLD_BASE-85
  land_position 50 78
  base_size 2
  land_percent 17
  top_border 61
  bottom_border 6
  left_border 8
  right_border 8
  border_fuzziness 30
}

/* A jungle in the south-west. */
create_land {
  terrain_type LUSH_BASE-77
  land_position 17 81
  base_size 1
  land_percent 7
  top_border 63
  bottom_border 5
  left_border 5
  right_border 62
  border_fuzziness 35
}

/* The volcano in the south-east. */
create_land {
  terrain_type LAVA_BASE-86
  land_position 83 82
  base_size 1
  land_percent 5
  top_border 65
  bottom_border 6
  left_border 64
  right_border 5
  border_fuzziness 35
}

<TERRAIN_GENERATION>
/* Ice ridges: long, ragged high ground behind cliffs. */
create_terrain ICE_BASE-170 {
  base_terrain ICE_BASE-85
  land_percent 6
  number_of_clumps 5
  clumping_factor 4
  spacing_to_other_terrain_types 1
}

/* Mesas rising out of the sand. */
create_terrain DESERT_BASE-155 {
  base_terrain DESERT_BASE-80
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
}

/* Oases: shallow pools in the desert. */
create_terrain DESERT_BASE-68 {
  base_terrain DESERT_BASE-80
  number_of_tiles 4
  number_of_clumps 2
  spacing_to_other_terrain_types 1
}

/* Rolling hills in the southern grassland. */
create_terrain GREENWORLD_BASE-219 {
  base_terrain GREENWORLD_BASE-85
  land_percent 2
  number_of_clumps 3
  spacing_to_other_terrain_types 2
}

/* Lakes. */
create_terrain GREENWORLD_BASE-68 {
  base_terrain GREENWORLD_BASE-85
  number_of_tiles 4
  number_of_clumps 2
  spacing_to_other_terrain_types 1
}

/* Jungle highlands. */
create_terrain LUSH_BASE-183 {
  base_terrain LUSH_BASE-77
  land_percent 1
  number_of_clumps 1
  spacing_to_other_terrain_types 1
}

/* The volcano's cone, with a lava lake below it. */
create_terrain LAVA_BASE-190 {
  base_terrain LAVA_BASE-86
  land_percent 1
  number_of_clumps 1
  spacing_to_other_terrain_types 1
}
create_terrain LAVA_BASE-41 {
  base_terrain LAVA_BASE-86
  number_of_tiles 2
  number_of_clumps 1
}

<OBJECTS_GENERATION>
/* Doodads. Distances here are in feature cells: 32 to a game-size square. */

/* Forests: tight groves, wherever trees suit the ground. */
create_object TREES {
  number_of_objects 8
  number_of_groups 2
  group_variance 3
  set_tight_grouping
  set_scaling_to_map_size
  min_distance_group_placement 10
  avoid_cliff_zone 2
}

/* Lone trees and shrubs over the grass. */
create_object TREES {
  number_of_objects 3
  set_scaling_to_map_size
}
create_object SHRUBS {
  number_of_objects 3
  set_scaling_to_map_size
  terrain_to_place_on GREENWORLD_BASE-85
}

/* Ice chunks scattered over the glacier, and piled on the ridges. */
create_object ICECHUNKS {
  number_of_objects 4
  number_of_groups 1
  group_placement_radius 4
  set_scaling_to_map_size
}
create_object ICECHUNKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  terrain_to_place_on ICE_BASE-170
}

/* Rocks everywhere: dry rocks in the desert, ice in the north. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 8
}

/* Desert plants around the sand. */
create_object PLANTS {
  number_of_objects 2
  set_scaling_to_map_size
  terrain_to_place_on DESERT_BASE-80
}

/* Jungle undergrowth. */
create_object FOLIAGE {
  number_of_objects 6
  set_scaling_to_map_size
}
create_object VINES {
  number_of_objects 3
  set_scaling_to_map_size
}

/* The volcano. */
create_object LAVA_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
}
create_object LAVA_NODES {
  number_of_objects 1
  set_scaling_to_map_size
}
)"},
};

}  // namespace galaxy
