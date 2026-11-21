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
    {"GreenWorld", "Verdant Isles", R"(/* GREENWORLD: Verdant Isles
   Grassy islands in a green sea, with sandy Archipelago beaches and
   a few Lava scars.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as GREENWORLD_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain GREENWORLD_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type GREENWORLD_BASE-85
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
/* Sandy Archipelago beaches on the islands. */
create_terrain ARCHIPELAGO_BASE-78 {
  base_terrain GREENWORLD_BASE-85
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* A few Lava scars where the ground has burnt. */
create_terrain LAVA_BASE-86 {
  base_terrain GREENWORLD_BASE-85
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain GREENWORLD_BASE-219 {
  base_terrain GREENWORLD_BASE-85
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for GreenWorld: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain GREENWORLD_HILL-85_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain GREENWORLD_HILL-85_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain GREENWORLD_ROUGH-85 {
  number_of_clumps 3
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain GREENWORLD_ISLAND_8X8 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Shallow water between the players' islands. */
create_connect_all_players_land {
  replace_terrain GREENWORLD_BASE-0 GREENWORLD_BASE-68
  terrain_size GREENWORLD_BASE-0 0 0
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

/* Groves of trees. */
create_object TREES {
  number_of_objects 6
  number_of_groups 2
  group_variance 2
  set_tight_grouping
  set_scaling_to_map_size
  min_distance_to_players 20
  avoid_cliff_zone 2
}

/* Shrubs. */
create_object SHRUBS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
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

/* Fronds on the sand. */
create_object FRONDS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Spires on the burnt ground. */
create_object LAVA_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
}
)"},
    {"Lava", "Ember Shelf", R"(/* LAVA: Ember Shelf
   Black volcanic islands in a sea of molten lava, with red Mars plains
   and pale Desert ash.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as LAVA_BASE-86, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain LAVA_BASE-41

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type LAVA_BASE-86
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
/* Red Mars plains between the cones. */
create_terrain MARS_BASE-85 {
  base_terrain LAVA_BASE-86
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Pale Desert ash fields. */
create_terrain DESERT_BASE-80 {
  base_terrain LAVA_BASE-86
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain LAVA_BASE-190 {
  base_terrain LAVA_BASE-86
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Lava: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain LAVA_HILL-86_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LAVA_HILL-86_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LAVA_POOL-86_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LAVA_POOL-86_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain LAVA_BASE-41 LAVA_BASE-86
  terrain_size LAVA_BASE-41 0 0
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

/* Lava spires. */
create_object LAVA_SPIRES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Lava nodes. */
create_object LAVA_NODES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
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

/* Hardy plants on the Mars plains. */
create_object PLANTS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}
)"},
    {"Mars", "Red Basin", R"(/* MARS: Red Basin
   A red Mars continent around inland seas, with Desert dunes and a
   few live Lava flows.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as MARS_BASE-85, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain MARS_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type MARS_BASE-85
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
/* Desert dunes on the plains. */
create_terrain DESERT_BASE-80 {
  base_terrain MARS_BASE-85
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Lava flows. */
create_terrain LAVA_BASE-86 {
  base_terrain MARS_BASE-85
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain MARS_BASE-255 {
  base_terrain MARS_BASE-85
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Mars: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain MARS_HILLS-85_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain MARS_ROUGH-85_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain MARS_FEATURE-85 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain MARS_POOL-85_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain MARS_ISLAND-0_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain MARS_BASE-0 MARS_BASE-85
  terrain_size MARS_BASE-0 0 0
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

/* Mars plants. */
create_object PLANTS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
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

/* Spires on the lava flows. */
create_object LAVA_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
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
    {"Archipelago", "Coral Chain", R"(/* ARCHIPELAGO: Coral Chain
   Sandy Archipelago islands in turquoise shallows, with Lush jungle and
   GreenWorld meadows inland.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as ARCHIPELAGO_BASE-78, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain ARCHIPELAGO_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type ARCHIPELAGO_BASE-78
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
/* Lush jungle inland. */
create_terrain LUSH_BASE-77 {
  base_terrain ARCHIPELAGO_BASE-78
  land_percent 5
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* GreenWorld meadows. */
create_terrain GREENWORLD_BASE-85 {
  base_terrain ARCHIPELAGO_BASE-78
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain ARCHIPELAGO_BASE-194 {
  base_terrain ARCHIPELAGO_BASE-78
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Archipelago: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain ARCHIPELAGO_HILLS-78_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ARCHIPELAGO_HILLS-78_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ARCHIPELAGO_ISLAND-0_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain ARCHIPELAGO_ROCKY-0_8X8 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Shallow water between the players' islands. */
create_connect_all_players_land {
  replace_terrain ARCHIPELAGO_BASE-0 ARCHIPELAGO_BASE-68
  terrain_size ARCHIPELAGO_BASE-0 0 0
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

/* Palms and trees. */
create_object TREES {
  number_of_objects 6
  number_of_groups 2
  group_variance 2
  set_tight_grouping
  set_scaling_to_map_size
  min_distance_to_players 20
  avoid_cliff_zone 2
}

/* Fronds. */
create_object FRONDS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Jungle foliage. */
create_object FOLIAGE {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
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
    {"Lush", "Green Hell", R"(/* LUSH: Green Hell
   Dense Lush jungle around dark lagoons, with Archipelago sandbars and
   dry Desert clearings.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as LUSH_BASE-77, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain LUSH_BASE-27

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type LUSH_BASE-77
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
/* Archipelago sandbars. */
create_terrain ARCHIPELAGO_BASE-78 {
  base_terrain LUSH_BASE-77
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Dry Desert clearings. */
create_terrain DESERT_BASE-80 {
  base_terrain LUSH_BASE-77
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain LUSH_BASE-183 {
  base_terrain LUSH_BASE-77
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Lush: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain LUSH_HILL-77_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LUSH_SWAMP-77 {
  number_of_clumps 3
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LUSH_POND-77_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain LUSH_ISLAND-27_16X16 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Land bridges between the players' islands. */
create_connect_all_players_land {
  replace_terrain LUSH_BASE-27 LUSH_BASE-77
  terrain_size LUSH_BASE-27 0 0
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

/* Jungle foliage. */
create_object FOLIAGE {
  number_of_objects 6
  number_of_groups 2
  group_variance 2
  set_tight_grouping
  set_scaling_to_map_size
  min_distance_to_players 20
  avoid_cliff_zone 2
}

/* Vines. */
create_object VINES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Palms on the sand. */
create_object TREES {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
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
    {"Desert", "Sun Anvil", R"(/* DESERT: Sun Anvil
   Desert islands of dunes and mesas, with old Lava flows and Acid
   pans baking in the sun.
   Galaxy Editor random map script, in the syntax Age of Empires II uses.
   Terrain names are the Tile Brush bases, such as DESERT_BASE-80, and one tile
   is one square of the map's game size: 16 x 16 map tiles. */

<PLAYER_SETUP>
random_placement

<LAND_GENERATION>
base_terrain DESERT_BASE-0

/* An island for each player, kept two tiles from the others. */
create_player_lands {
  terrain_type DESERT_BASE-80
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
/* Old Lava flows. */
create_terrain LAVA_BASE-86 {
  base_terrain DESERT_BASE-80
  land_percent 4
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* Acid pans. */
create_terrain ACID_BASE-85 {
  base_terrain DESERT_BASE-80
  land_percent 3
  number_of_clumps 3
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* High ground behind cliffs, kept a tile from the water. */
create_terrain DESERT_BASE-155 {
  base_terrain DESERT_BASE-80
  land_percent 5
  number_of_clumps 4
  spacing_to_other_terrain_types 1
  set_avoid_player_start_areas 1
}

/* The Tile Brush's own stamps for Desert: hills, pools, islands and more.
   Each fills a block of flat ground of the base it needs. */
create_terrain DESERT_HILL-80_16X16 {
  number_of_clumps 2
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain DESERT_ROUGHS-80_8X8 {
  number_of_clumps 4
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

create_terrain DESERT_HUGE-HILL-80 {
  number_of_clumps 1
  set_avoid_player_start_areas 1
}

create_terrain DESERT_PLATEAUS-80 {
  number_of_clumps 1
  set_scale_by_groups
  set_avoid_player_start_areas 1
}

<CONNECTION_GENERATION>
/* Shallow water between the players' islands. */
create_connect_all_players_land {
  replace_terrain DESERT_BASE-0 DESERT_BASE-68
  terrain_size DESERT_BASE-0 0 0
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

/* Dry rocks. */
create_object ROCKS {
  number_of_objects 3
  number_of_groups 1
  group_placement_radius 3
  set_scaling_to_map_size
  min_distance_group_placement 12
  min_distance_to_players 12
}

/* Desert plants. */
create_object PLANTS {
  number_of_objects 3
  set_scaling_to_map_size
  min_distance_to_players 8
}

/* Spires on the lava flows. */
create_object LAVA_SPIRES {
  number_of_objects 1
  set_scaling_to_map_size
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
