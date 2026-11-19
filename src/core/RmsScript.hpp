#pragma once

#include <random>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace galaxy {

// Reads random map scripts written in the syntax Age of Empires II uses: section
// headers such as <LAND_GENERATION>, commands with attribute blocks in braces,
// /* nested */ comments, #define and #const, if / elseif / else / endif,
// start_random / percent_chance / end_random, rnd(min,max), and math in
// parentheses such as (WIDTH * 2 / 10). Like the game, the reader keeps going
// past anything it does not understand, but it says what it skipped and where.

// A note about the script. line is 0 when the note is about the whole script.
struct RmsMessage {
    int line = 0;
    std::string text;
};

// One argument as written, after constants, rnd() and math are replaced.
struct RmsValue {
    std::string text;
    bool numeric = false;
    double number = 0.0;
    // The number truncated toward zero, the way the game reads whole-number arguments.
    int integer() const;
};

struct RmsAttribute {
    std::string name;
    std::vector<RmsValue> args;
    int line = 0;
};

struct RmsCommand {
    // The section the command sits in, such as "LAND_GENERATION", without the brackets.
    std::string section;
    std::string name;
    std::vector<RmsValue> args;
    std::vector<RmsAttribute> attributes;
    int line = 0;
    // The last attribute with this name, or null. A repeated attribute is replaced
    // by the later one, as in the game.
    const RmsAttribute* find(std::string_view name) const;
    // Every attribute with this name, in order, for ones that repeat such as replace_terrain.
    std::vector<const RmsAttribute*> findAll(std::string_view name) const;
};

struct RmsScriptSettings {
    // Conditions that are true before the script starts, such as 2_PLAYER_GAME.
    std::vector<std::string> conditions;
};

struct RmsScript {
    std::vector<RmsCommand> commands;
    // Each section header the script uses, with the line it first appears on.
    std::vector<std::pair<std::string, int>> sections;
    std::vector<RmsMessage> messages;
    bool hasSection(std::string_view name) const;
};

// The sections the game knows, in the order it runs them.
const std::vector<std::string>& rmsSectionOrder();

// Reads a whole script. start_random blocks, rnd() and conditions are settled here,
// so the same text and the same generator state always give the same commands.
RmsScript readRandomMapScript(std::string_view text, const RmsScriptSettings& settings, std::mt19937& rng);

// Splits text into the words the reader works with, and the line each starts on.
// Braces are words of their own even when they touch other text.
std::vector<std::pair<std::string, int>> rmsWords(std::string_view text);

}  // namespace galaxy
