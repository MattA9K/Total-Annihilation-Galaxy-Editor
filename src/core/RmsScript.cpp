#include "core/RmsScript.hpp"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <map>
#include <set>

namespace galaxy {
namespace {

struct Word {
    std::string text;
    int line = 0;
};

bool isSpace(char character) {
    return character == ' ' || character == '\t' || character == '\n' || character == '\r' || character == '\f' || character == '\v';
}

// A whole word that reads as a number: 12, -3, 2.5, inf.
bool readNumber(const std::string& text, double& value) {
    if (text.empty()) {
        return false;
    }
    const char first = text.front();
    if (!(std::isdigit(static_cast<unsigned char>(first)) || first == '-' || first == '+' || first == '.' || first == 'i')) {
        return false;
    }
    char* end = nullptr;
    const double parsed = std::strtod(text.c_str(), &end);
    if (end == text.c_str() || *end != '\0' || std::isnan(parsed)) {
        return false;
    }
    value = parsed;
    return true;
}

std::string numberText(double value) {
    char buffer[64];
    if (std::isfinite(value) && std::floor(value) == value && std::fabs(value) < 1e15) {
        std::snprintf(buffer, sizeof(buffer), "%lld", static_cast<long long>(value));
    } else {
        std::snprintf(buffer, sizeof(buffer), "%.9g", value);
    }
    return buffer;
}

RmsValue makeValue(const std::string& text) {
    RmsValue value;
    value.text = text;
    value.numeric = readNumber(text, value.number);
    return value;
}

bool isSectionHeader(const std::string& word) {
    return word.size() > 2 && word.front() == '<' && word.back() == '>';
}

// rnd(MIN,MAX), written without spaces.
bool readRnd(const std::string& word, int& low, int& high) {
    if (word.size() < 8 || word.compare(0, 4, "rnd(") != 0 || word.back() != ')') {
        return false;
    }
    const std::string inside = word.substr(4, word.size() - 5);
    const std::size_t comma = inside.find(',');
    if (comma == std::string::npos) {
        return false;
    }
    double a = 0.0;
    double b = 0.0;
    if (!readNumber(inside.substr(0, comma), a) || !readNumber(inside.substr(comma + 1), b)) {
        return false;
    }
    low = static_cast<int>(a);
    high = static_cast<int>(b);
    return true;
}

// ---------------------------------------------------------------------------
// First pass: comments, #define, #const, conditions, random blocks, rnd() and
// math. What is left is the plain list of words the commands are read from.

class Preprocessor {
public:
    Preprocessor(std::vector<Word> words, const RmsScriptSettings& settings, std::mt19937& rng, std::vector<RmsMessage>& messages)
        : words_(std::move(words)), rng_(rng), messages_(messages) {
        for (const std::string& condition : settings.conditions) {
            defined_.insert(condition);
        }
    }

    std::vector<Word> run() {
        std::vector<Word> out;
        std::size_t i = 0;
        int commentDepth = 0;
        int commentLine = 0;
        while (i < words_.size()) {
            const Word& word = words_[i];
            if (word.text == "/*") {
                if (commentDepth == 0) {
                    commentLine = word.line;
                }
                ++commentDepth;
                ++i;
                continue;
            }
            if (commentDepth > 0) {
                if (word.text == "*/") {
                    --commentDepth;
                }
                ++i;
                continue;
            }
            if (word.text == "*/") {
                note(word.line, "*/ closes a comment that was never opened.");
                ++i;
                continue;
            }
            if (word.text.size() > 2 && (word.text.compare(0, 2, "/*") == 0 || word.text.compare(word.text.size() - 2, 2, "*/") == 0)) {
                if (active()) {
                    note(word.line, "\"" + word.text + "\" is not a comment. Put a space after /* and before */.");
                }
                ++i;
                continue;
            }

            // Conditions and random blocks are followed even in skipped code, so
            // their ends still match up.
            if (word.text == "if" || word.text == "elseif") {
                const std::string condition = i + 1 < words_.size() ? words_[i + 1].text : std::string();
                if (word.text == "if") {
                    Frame frame;
                    frame.random = false;
                    frame.parentActive = active();
                    frame.taken = frame.parentActive && isDefined(condition);
                    frame.active = frame.taken;
                    frame.line = word.line;
                    frames_.push_back(frame);
                } else if (frames_.empty() || frames_.back().random) {
                    note(word.line, "elseif without a matching if.");
                } else {
                    Frame& frame = frames_.back();
                    frame.active = frame.parentActive && !frame.taken && isDefined(condition);
                    frame.taken = frame.taken || frame.active;
                }
                i += 2;
                continue;
            }
            if (word.text == "else") {
                if (frames_.empty() || frames_.back().random) {
                    note(word.line, "else without a matching if.");
                } else {
                    Frame& frame = frames_.back();
                    frame.active = frame.parentActive && !frame.taken;
                    frame.taken = true;
                }
                ++i;
                continue;
            }
            if (word.text == "endif") {
                if (frames_.empty() || frames_.back().random) {
                    note(word.line, "endif without a matching if.");
                } else {
                    frames_.pop_back();
                }
                ++i;
                continue;
            }
            if (word.text == "start_random") {
                Frame frame;
                frame.random = true;
                frame.parentActive = active();
                frame.active = false;
                frame.roll = std::uniform_int_distribution<int>(0, 99)(rng_);
                frame.line = word.line;
                frames_.push_back(frame);
                ++i;
                continue;
            }
            if (word.text == "percent_chance") {
                std::size_t next = i + 1;
                const RmsValue chance = readValue(next);
                if (frames_.empty() || !frames_.back().random) {
                    note(word.line, "percent_chance outside start_random and end_random.");
                } else {
                    Frame& frame = frames_.back();
                    const int percent = chance.numeric ? std::max(0, chance.integer()) : 0;
                    if (!chance.numeric) {
                        note(word.line, "percent_chance needs a number.");
                    }
                    frame.active = frame.parentActive && frame.roll >= frame.sum && frame.roll < frame.sum + percent;
                    frame.sum += percent;
                }
                i = next;
                continue;
            }
            if (word.text == "end_random") {
                if (frames_.empty() || !frames_.back().random) {
                    note(word.line, "end_random without a matching start_random.");
                } else {
                    frames_.pop_back();
                }
                ++i;
                continue;
            }

            if (!active()) {
                ++i;
                continue;
            }

            if (word.text == "#define") {
                if (i + 1 < words_.size()) {
                    defined_.insert(words_[i + 1].text);
                } else {
                    note(word.line, "#define needs a name.");
                }
                i += 2;
                continue;
            }
            if (word.text == "#const") {
                if (i + 2 >= words_.size()) {
                    note(word.line, "#const needs a name and a value.");
                    i = words_.size();
                    continue;
                }
                const std::string name = words_[i + 1].text;
                std::size_t next = i + 2;
                const RmsValue value = readValue(next);
                if (constants_.count(name) != 0 && fixed_.count(name) != 0) {
                    note(word.line, name + " was set with math and cannot be changed.");
                } else {
                    constants_[name] = value;
                    if (words_[i + 2].text.front() == '(') {
                        fixed_.insert(name);
                    }
                }
                i = next;
                continue;
            }
            if (word.text == "#include_drs" || word.text == "#includeXS" || word.text == "#include") {
                note(word.line, word.text + " is not supported. Paste what the file holds into the script instead.");
                i += 2;
                continue;
            }
            if (!word.text.empty() && word.text.front() == '#') {
                note(word.line, word.text + " is not a directive the script reader knows.");
                ++i;
                continue;
            }

            if (isSectionHeader(word.text) || word.text == "{" || word.text == "}") {
                out.push_back(word);
                ++i;
                continue;
            }
            std::size_t next = i;
            const RmsValue value = readValue(next);
            out.push_back(Word{value.text, word.line});
            i = next;
        }
        if (commentDepth > 0) {
            note(commentLine, "This comment is never closed. Everything after /* was skipped.");
        }
        for (const Frame& frame : frames_) {
            note(frame.line, frame.random ? "start_random has no end_random." : "if has no endif.");
        }
        return out;
    }

private:
    struct Frame {
        bool random = false;
        bool parentActive = true;
        bool active = true;
        bool taken = false;
        int roll = 0;
        int sum = 0;
        int line = 0;
    };

    bool active() const { return frames_.empty() || frames_.back().active; }
    bool isDefined(const std::string& name) const { return defined_.count(name) != 0; }

    void note(int line, std::string text) { messages_.push_back(RmsMessage{line, std::move(text)}); }

    // One value: a plain word, a constant, rnd(min,max), or math in parentheses
    // spread over several words. index moves past everything it used.
    RmsValue readValue(std::size_t& index) {
        if (index >= words_.size()) {
            return RmsValue{};
        }
        const Word& word = words_[index];
        if (!word.text.empty() && word.text.front() == '(') {
            std::vector<std::string> parts;
            const int line = word.line;
            while (index < words_.size()) {
                parts.push_back(words_[index].text);
                const bool closes = !words_[index].text.empty() && words_[index].text.back() == ')';
                ++index;
                if (closes) {
                    break;
                }
            }
            parts.front().erase(0, 1);
            if (!parts.back().empty() && parts.back().back() == ')') {
                parts.back().pop_back();
            } else {
                note(line, "This math has no closing parenthesis.");
            }
            parts.erase(std::remove(parts.begin(), parts.end(), std::string()), parts.end());
            const double result = evaluate(parts, line);
            RmsValue value = makeValue(numberText(result));
            return value;
        }
        ++index;
        int low = 0;
        int high = 0;
        if (readRnd(word.text, low, high)) {
            if (high < low) {
                std::swap(low, high);
            }
            return makeValue(std::to_string(std::uniform_int_distribution<int>(low, high)(rng_)));
        }
        if (word.text.compare(0, 4, "rnd(") == 0) {
            note(word.line, "Write rnd with two whole numbers and no spaces, such as rnd(2,5).");
        }
        const auto constant = constants_.find(word.text);
        if (constant != constants_.end()) {
            return constant->second;
        }
        return makeValue(word.text);
    }

    // Math the way the game does it: strictly left to right, + - * / %.
    double evaluate(const std::vector<std::string>& parts, int line) {
        const auto operand = [&](const std::string& text) {
            double number = 0.0;
            if (readNumber(text, number)) {
                return number;
            }
            const auto constant = constants_.find(text);
            if (constant != constants_.end() && constant->second.numeric) {
                return constant->second.number;
            }
            note(line, "\"" + text + "\" is not a number in this math.");
            return 0.0;
        };
        if (parts.empty()) {
            note(line, "Empty parentheses.");
            return 0.0;
        }
        double total = operand(parts[0]);
        for (std::size_t i = 1; i + 1 < parts.size(); i += 2) {
            const std::string& op = parts[i];
            const double right = operand(parts[i + 1]);
            if (op == "+") {
                total += right;
            } else if (op == "-") {
                total -= right;
            } else if (op == "*") {
                total *= right;
            } else if (op == "/") {
                total = right == 0.0 ? 0.0 : total / right;
            } else if (op == "%") {
                const long long left = static_cast<long long>(total);
                const long long divisor = static_cast<long long>(right);
                total = divisor == 0 ? static_cast<double>(left) : static_cast<double>(left % divisor);
            } else {
                note(line, "\"" + op + "\" is not + - * / or %. Put spaces around each operator.");
            }
        }
        if (parts.size() % 2 == 0) {
            note(line, "This math ends with an operator.");
        }
        return total;
    }

    std::vector<Word> words_;
    std::mt19937& rng_;
    std::vector<RmsMessage>& messages_;
    std::set<std::string> defined_;
    std::map<std::string, RmsValue> constants_;
    std::set<std::string> fixed_;
    std::vector<Frame> frames_;
};

// ---------------------------------------------------------------------------
// Second pass: sections, commands and their attribute blocks.

struct CommandSpec {
    const char* name;
    const char* section;  // "*" for anywhere
    int args;
    bool block;
};

const CommandSpec kCommands[] = {
    {"behavior_version", "*", 1, false},
    {"random_placement", "PLAYER_SETUP", 0, false},
    {"grouped_by_team", "PLAYER_SETUP", 0, false},
    {"direct_placement", "PLAYER_SETUP", 0, false},
    {"nomad_resources", "PLAYER_SETUP", 0, false},
    {"force_nomad_treaty", "PLAYER_SETUP", 0, false},
    {"override_map_size", "PLAYER_SETUP", 1, false},
    {"set_gaia_civilization", "PLAYER_SETUP", 1, false},
    {"water_definition", "PLAYER_SETUP", 1, false},
    {"ai_info_map_type", "PLAYER_SETUP", 4, false},
    {"effect_amount", "PLAYER_SETUP", 4, false},
    {"effect_percent", "PLAYER_SETUP", 4, false},
    {"guard_state", "PLAYER_SETUP", 4, false},
    {"terrain_state", "PLAYER_SETUP", 4, false},
    {"weather_type", "PLAYER_SETUP", 4, false},
    {"base_terrain", "LAND_GENERATION", 1, false},
    {"base_layer", "LAND_GENERATION", 1, false},
    {"enable_waves", "LAND_GENERATION", 1, false},
    {"create_player_lands", "LAND_GENERATION", 0, true},
    {"create_land", "LAND_GENERATION", 0, true},
    {"create_elevation", "ELEVATION_GENERATION", 1, true},
    {"cliff_type", "CLIFF_GENERATION", 1, false},
    {"min_number_of_cliffs", "CLIFF_GENERATION", 1, false},
    {"max_number_of_cliffs", "CLIFF_GENERATION", 1, false},
    {"min_length_of_cliff", "CLIFF_GENERATION", 1, false},
    {"max_length_of_cliff", "CLIFF_GENERATION", 1, false},
    {"cliff_curliness", "CLIFF_GENERATION", 1, false},
    {"min_distance_cliffs", "CLIFF_GENERATION", 1, false},
    {"min_terrain_distance", "CLIFF_GENERATION", 1, false},
    {"color_correction", "TERRAIN_GENERATION", 1, false},
    {"create_terrain", "TERRAIN_GENERATION", 1, true},
    {"accumulate_connections", "CONNECTION_GENERATION", 0, false},
    {"create_connect_all_players_land", "CONNECTION_GENERATION", 0, true},
    {"create_connect_teams_lands", "CONNECTION_GENERATION", 0, true},
    {"create_connect_all_lands", "CONNECTION_GENERATION", 0, true},
    {"create_connect_same_land_zones", "CONNECTION_GENERATION", 0, true},
    {"create_connect_land_zones", "CONNECTION_GENERATION", 2, true},
    {"create_connect_to_nonplayer_land", "CONNECTION_GENERATION", 0, true},
};

// Which blocks an attribute belongs in. Lands are create_land and
// create_player_lands, connections are every create_connect_ command.
enum Use : unsigned {
    kLand = 1,
    kElevation = 2,
    kTerrain = 4,
    kConnection = 8,
};

struct AttributeSpec {
    const char* name;
    int minArgs;
    int maxArgs;
    unsigned use;
};

const AttributeSpec kAttributes[] = {
    {"terrain_type", 1, 1, kLand},
    {"land_percent", 1, 1, kLand | kTerrain},
    {"number_of_tiles", 1, 1, kLand | kElevation | kTerrain},
    {"base_size", 1, 1, kLand},
    {"set_circular_base", 0, 0, kLand},
    {"generate_mode", 1, 1, kLand},
    {"land_position", 2, 2, kLand},
    {"circle_radius", 1, 2, kLand},
    {"left_border", 1, 1, kLand},
    {"right_border", 1, 1, kLand},
    {"top_border", 1, 1, kLand},
    {"bottom_border", 1, 1, kLand},
    {"border_fuzziness", 1, 1, kLand},
    {"clumping_factor", 1, 1, kLand | kTerrain},
    {"land_conformity", 1, 1, kLand},
    {"base_elevation", 1, 1, kLand},
    {"assign_to_player", 1, 1, kLand},
    {"assign_to", 4, 4, kLand},
    {"zone", 1, 1, kLand},
    {"set_zone_by_team", 0, 0, kLand},
    {"set_zone_randomly", 0, 0, kLand},
    {"other_zone_avoidance_distance", 1, 1, kLand},
    {"min_placement_distance", 1, 1, kLand},
    {"land_id", 1, 1, kLand},
    {"base_terrain", 1, 1, kElevation | kTerrain},
    {"base_layer", 1, 1, kLand | kElevation | kTerrain},
    {"number_of_clumps", 1, 1, kElevation | kTerrain},
    {"set_scale_by_size", 0, 0, kElevation | kTerrain},
    {"set_scale_by_groups", 0, 0, kElevation | kTerrain},
    {"spacing", 1, 1, kElevation},
    {"enable_balanced_elevation", 0, 0, kElevation},
    {"beach_terrain", 1, 1, kTerrain},
    {"terrain_mask", 1, 1, kTerrain},
    {"spacing_to_other_terrain_types", 1, 1, kTerrain},
    {"spacing_to_specific_terrain", 2, 2, kTerrain},
    {"set_flat_terrain_only", 0, 0, kTerrain},
    {"set_avoid_player_start_areas", 0, 1, kTerrain},
    {"height_limits", 2, 2, kTerrain},
    {"default_terrain_replacement", 1, 1, kConnection},
    {"replace_terrain", 2, 2, kConnection},
    {"terrain_cost", 2, 2, kConnection},
    {"terrain_size", 3, 3, kConnection},
};

const CommandSpec* findCommand(const std::string& name) {
    for (const CommandSpec& spec : kCommands) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

const AttributeSpec* findAttribute(const std::string& name) {
    for (const AttributeSpec& spec : kAttributes) {
        if (name == spec.name) {
            return &spec;
        }
    }
    return nullptr;
}

unsigned useOf(const std::string& command) {
    if (command == "create_land" || command == "create_player_lands") {
        return kLand;
    }
    if (command == "create_elevation") {
        return kElevation;
    }
    if (command == "create_terrain") {
        return kTerrain;
    }
    if (command.compare(0, 15, "create_connect_") == 0) {
        return kConnection;
    }
    return 0;
}

bool knownSection(const std::string& name) {
    const std::vector<std::string>& order = rmsSectionOrder();
    return std::find(order.begin(), order.end(), name) != order.end();
}

class Parser {
public:
    Parser(std::vector<Word> words, RmsScript& script) : words_(std::move(words)), script_(script) {}

    void run() {
        bool quiet = false;
        while (pos_ < words_.size()) {
            const Word word = words_[pos_];
            if (isSectionHeader(word.text)) {
                section_ = word.text.substr(1, word.text.size() - 2);
                if (!knownSection(section_)) {
                    note(word.line, word.text + " is not a section the game knows. Its commands are skipped.");
                }
                bool seen = false;
                for (const auto& entry : script_.sections) {
                    seen = seen || entry.first == section_;
                }
                if (!seen) {
                    script_.sections.emplace_back(section_, word.line);
                }
                quiet = false;
                ++pos_;
                continue;
            }
            if (section_ == "OBJECTS_GENERATION" || (!section_.empty() && !knownSection(section_))) {
                // Objects come later. Their commands are skipped whole, braces and all.
                if (word.text == "{") {
                    skipBlock();
                } else {
                    ++pos_;
                }
                continue;
            }
            if (word.text == "{") {
                if (!quiet) {
                    note(word.line, "A { with no command before it. The block was skipped.");
                }
                skipBlock();
                quiet = false;
                continue;
            }
            if (word.text == "}") {
                note(word.line, "A } with no block to close.");
                ++pos_;
                continue;
            }
            const CommandSpec* spec = findCommand(word.text);
            if (spec == nullptr) {
                if (!quiet) {
                    note(word.line, "\"" + word.text + "\" is not a command the script reader knows. It was skipped.");
                }
                quiet = true;
                ++pos_;
                continue;
            }
            quiet = false;
            ++pos_;
            RmsCommand command;
            command.section = section_;
            command.name = word.text;
            command.line = word.line;
            for (int i = 0; i < spec->args; ++i) {
                if (pos_ >= words_.size() || words_[pos_].text == "{" || words_[pos_].text == "}" || isSectionHeader(words_[pos_].text)) {
                    note(word.line, command.name + " needs " + std::to_string(spec->args) + (spec->args == 1 ? " value." : " values."));
                    break;
                }
                command.args.push_back(makeValue(words_[pos_].text));
                ++pos_;
            }
            if (spec->block) {
                if (pos_ < words_.size() && words_[pos_].text == "{") {
                    ++pos_;
                    readBlock(command);
                } else {
                    note(word.line, command.name + " needs a { } block. It runs with its defaults.");
                }
            }
            const std::string wanted = spec->section;
            if (section_.empty() && wanted != "*") {
                note(word.line, command.name + " comes before any section header. Put it under <" + (wanted == "*" ? std::string("PLAYER_SETUP") : wanted) + ">.");
                continue;
            }
            if (wanted != "*" && wanted != section_) {
                note(word.line, command.name + " belongs under <" + wanted + ">, so it was skipped.");
                continue;
            }
            script_.commands.push_back(std::move(command));
        }
    }

private:
    void note(int line, std::string text) { script_.messages.push_back(RmsMessage{line, std::move(text)}); }

    void skipBlock() {
        int depth = 0;
        while (pos_ < words_.size()) {
            const std::string& text = words_[pos_].text;
            ++pos_;
            if (text == "{") {
                ++depth;
            } else if (text == "}") {
                if (--depth <= 0) {
                    return;
                }
            }
        }
    }

    void readBlock(RmsCommand& command) {
        const unsigned use = useOf(command.name);
        const int opened = command.line;
        bool quiet = false;
        while (pos_ < words_.size()) {
            const Word word = words_[pos_];
            if (word.text == "}") {
                ++pos_;
                return;
            }
            if (isSectionHeader(word.text)) {
                note(opened, "The block for " + command.name + " is never closed with }.");
                return;
            }
            if (word.text == "{") {
                note(word.line, "A { inside the block for " + command.name + ". The inner block was skipped.");
                skipBlock();
                continue;
            }
            const AttributeSpec* spec = findAttribute(word.text);
            if (spec == nullptr) {
                if (!quiet) {
                    note(word.line, "\"" + word.text + "\" is not an attribute of " + command.name + ". It was skipped.");
                }
                quiet = true;
                ++pos_;
                continue;
            }
            quiet = false;
            ++pos_;
            RmsAttribute attribute;
            attribute.name = word.text;
            attribute.line = word.line;
            const auto boundary = [&]() {
                return pos_ >= words_.size() || words_[pos_].text == "{" || words_[pos_].text == "}" || isSectionHeader(words_[pos_].text);
            };
            for (int i = 0; i < spec->minArgs; ++i) {
                if (boundary()) {
                    break;
                }
                attribute.args.push_back(makeValue(words_[pos_].text));
                ++pos_;
            }
            // Optional values, such as the distance after set_avoid_player_start_areas, are numbers.
            while (static_cast<int>(attribute.args.size()) < spec->maxArgs && !boundary()) {
                const RmsValue next = makeValue(words_[pos_].text);
                if (!next.numeric) {
                    break;
                }
                attribute.args.push_back(next);
                ++pos_;
            }
            if (static_cast<int>(attribute.args.size()) < spec->minArgs) {
                note(word.line, attribute.name + " needs " + std::to_string(spec->minArgs) + (spec->minArgs == 1 ? " value." : " values."));
                continue;
            }
            if ((spec->use & use) == 0) {
                note(word.line, attribute.name + " does nothing in " + command.name + ".");
                continue;
            }
            command.attributes.push_back(std::move(attribute));
        }
        note(opened, "The block for " + command.name + " is never closed with }.");
    }

    std::vector<Word> words_;
    RmsScript& script_;
    std::size_t pos_ = 0;
    std::string section_;
};

}  // namespace

int RmsValue::integer() const {
    if (!numeric || !std::isfinite(number)) {
        return number > 0 ? 2147483647 : (number < 0 ? -2147483647 : 0);
    }
    return static_cast<int>(std::clamp(std::trunc(number), -2147483647.0, 2147483647.0));
}

const RmsAttribute* RmsCommand::find(std::string_view name) const {
    const RmsAttribute* found = nullptr;
    for (const RmsAttribute& attribute : attributes) {
        if (attribute.name == name) {
            found = &attribute;
        }
    }
    return found;
}

std::vector<const RmsAttribute*> RmsCommand::findAll(std::string_view name) const {
    std::vector<const RmsAttribute*> found;
    for (const RmsAttribute& attribute : attributes) {
        if (attribute.name == name) {
            found.push_back(&attribute);
        }
    }
    return found;
}

bool RmsScript::hasSection(std::string_view name) const {
    for (const auto& entry : sections) {
        if (entry.first == name) {
            return true;
        }
    }
    return false;
}

const std::vector<std::string>& rmsSectionOrder() {
    static const std::vector<std::string> order = {
        "PLAYER_SETUP",
        "LAND_GENERATION",
        "ELEVATION_GENERATION",
        "CLIFF_GENERATION",
        "TERRAIN_GENERATION",
        "CONNECTION_GENERATION",
        "OBJECTS_GENERATION",
    };
    return order;
}

std::vector<std::pair<std::string, int>> rmsWords(std::string_view text) {
    std::vector<std::pair<std::string, int>> words;
    std::string current;
    int line = 1;
    int start = 1;
    const auto flush = [&]() {
        if (!current.empty()) {
            words.emplace_back(current, start);
            current.clear();
        }
    };
    for (const char character : text) {
        if (isSpace(character)) {
            flush();
            if (character == '\n') {
                ++line;
            }
            continue;
        }
        if (character == '{' || character == '}') {
            flush();
            words.emplace_back(std::string(1, character), line);
            continue;
        }
        if (current.empty()) {
            start = line;
        }
        current.push_back(character);
    }
    flush();
    return words;
}

RmsScript readRandomMapScript(std::string_view text, const RmsScriptSettings& settings, std::mt19937& rng) {
    RmsScript script;
    std::vector<Word> words;
    for (auto& [word, line] : rmsWords(text)) {
        words.push_back(Word{std::move(word), line});
    }
    Preprocessor preprocessor(std::move(words), settings, rng, script.messages);
    std::vector<Word> plain = preprocessor.run();
    Parser parser(std::move(plain), script);
    parser.run();
    std::stable_sort(script.messages.begin(), script.messages.end(), [](const RmsMessage& left, const RmsMessage& right) {
        return left.line < right.line;
    });
    return script;
}

}  // namespace galaxy
