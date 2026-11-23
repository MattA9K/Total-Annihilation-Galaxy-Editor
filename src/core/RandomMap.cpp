#include "core/RandomMap.hpp"

#include <algorithm>
#include <cstdint>
#include <cctype>
#include <cmath>
#include <limits>
#include <map>
#include <queue>
#include <set>
#include <unordered_map>

namespace galaxy {
namespace {

std::string lower(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    }
    return out;
}

std::string upper(std::string_view text) {
    std::string out(text);
    for (char& character : out) {
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    }
    return out;
}

int trailingHeight(std::string_view name) {
    const std::size_t dash = name.rfind('-');
    if (dash == std::string_view::npos || dash + 1 >= name.size()) {
        return -1;
    }
    int value = 0;
    for (std::size_t i = dash + 1; i < name.size(); ++i) {
        if (name[i] < '0' || name[i] > '9') {
            return -1;
        }
        value = value * 10 + (name[i] - '0');
    }
    return value;
}

double numberAt(const RmsCommand& command, std::string_view name, double fallback, std::size_t index = 0) {
    const RmsAttribute* attribute = command.find(name);
    if (attribute == nullptr || index >= attribute->args.size() || !attribute->args[index].numeric) {
        return fallback;
    }
    return attribute->args[index].number;
}

bool has(const RmsCommand& command, std::string_view name) {
    return command.find(name) != nullptr;
}

// Which of two attributes comes last in a block, for pairs where the later one wins.
int lastIndexOf(const RmsCommand& command, std::string_view name) {
    int found = -1;
    for (std::size_t i = 0; i < command.attributes.size(); ++i) {
        if (command.attributes[i].name == name) {
            found = static_cast<int>(i);
        }
    }
    return found;
}

int chebyshev(int ax, int ay, int bx, int by) {
    return std::max(std::abs(ax - bx), std::abs(ay - by));
}

// Scaling in the game compares the map to a 100 x 100 tile map. Script tiles here
// are game-size squares, so the reference is a 10 x 10 map.
constexpr double kScaleReference = 100.0;

struct Land {
    int terrain = 0;
    int player = 0;
    int zone = -10;
    int avoid = 0;
    int minDistance = 0;
    int baseSize = 3;
    bool circular = false;
    int clumping = 8;
    int fuzziness = 20;
    double borders[4] = {0.0, 0.0, 0.0, 0.0};  // left, right, top, bottom, in percent
    bool positioned = false;
    double positionX = 0.0;
    double positionY = 0.0;
    bool hasTiles = false;
    int tiles = 0;
    double percent = 100.0;
    bool playerShare = false;
    int generateMode = 0;
    double circleRadius = -1.0;
    double circleVariance = 0.0;
    bool hasLandId = false;
    int landId = 0;
    int line = 0;

    int originX = 0;
    int originY = 0;
    int target = 0;
    std::vector<int> nodes;
    std::vector<int> frontier;
    std::vector<signed char> borderVerdict;
    bool done = false;
};

class Generator {
public:
    Generator(const RmsScript& script, RmsCanvas& canvas, const RmsTerrainNames& names, const RandomMapOptions& options,
              std::mt19937& rng, RandomMapOutcome& outcome)
        : script_(script), canvas_(canvas), names_(names), options_(options), rng_(rng), outcome_(outcome) {
        width_ = std::max(1, canvas.width());
        height_ = std::max(1, canvas.height());
        count_ = width_ * height_;
    }

    void run() {
        noteUnsupportedSections();
        for (const RmsCommand& command : script_.commands) {
            if (command.name == "direct_placement") {
                directPlacement_ = true;
            } else if (command.name == "behavior_version") {
                behaviorVersion_ = command.args.empty() ? 0 : command.args[0].integer();
            } else if (command.name == "base_layer" || command.name == "enable_waves" || command.name == "color_correction") {
                note(command.line, command.name + " changes how the game draws terrain, which Total Annihilation maps do not have. It was skipped.");
            }
        }
        // The game's order: land, elevation, cliffs, terrain, connections, objects.
        report(RandomMapStage::Lands, 0.0);
        landGeneration();
        report(RandomMapStage::Elevation, 0.0);
        elevationGeneration();
        report(RandomMapStage::Cliffs, 0.0);
        cliffGeneration();
        report(RandomMapStage::Terrain, 0.0);
        terrainGeneration();
        report(RandomMapStage::Connections, 0.0);
        connectionGeneration();
        report(RandomMapStage::Objects, 0.0);
        objectGeneration();
        report(RandomMapStage::Objects, 1.0);
    }

private:
    void note(int line, std::string text) { outcome_.messages.push_back(RmsMessage{line, std::move(text)}); }

    // Tells the caller how far the run has got, and stops it when it was cancelled.
    void report(RandomMapStage stage, double fraction) {
        if (options_.cancel != nullptr && options_.cancel->load(std::memory_order_relaxed)) {
            throw RandomMapCancelled();
        }
        if (options_.progress) {
            options_.progress(stage, std::clamp(fraction, 0.0, 1.0));
        }
    }

    void noteUnsupportedSections() {
        for (const auto& [section, line] : script_.sections) {
            if (section == "CLIFF_GENERATION" && !names_.cliffAbove) {
                note(line, "Cliffs cannot be drawn here, so this section was skipped.");
            } else if (section == "OBJECTS_GENERATION" && options_.objects == nullptr) {
                note(line, "Features cannot be placed here, so this section was skipped.");
            }
        }
    }

    int terrainOf(const RmsValue& value, int line) {
        const int terrain = names_.find ? names_.find(value.text) : -1;
        if (terrain < 0) {
            note(line, "\"" + value.text + "\" is not a terrain. Terrain names look like GREENWORLD_BASE-85.");
        }
        return terrain;
    }

    int index(int x, int y) const { return y * width_ + x; }
    int clampX(int x) const { return std::clamp(x, 0, width_ - 1); }
    int clampY(int y) const { return std::clamp(y, 0, height_ - 1); }

    // ---------------------------------------------------------------- lands

    void readLand(const RmsCommand& command, Land& land) {
        land.line = command.line;
        land.terrain = names_.fallback;
        if (const RmsAttribute* type = command.find("terrain_type"); type != nullptr && !type->args.empty()) {
            const int terrain = terrainOf(type->args[0], type->line);
            if (terrain >= 0) {
                land.terrain = terrain;
            }
        }
        land.baseSize = static_cast<int>(numberAt(command, "base_size", 3.0));
        land.circular = has(command, "set_circular_base");
        land.clumping = static_cast<int>(numberAt(command, "clumping_factor", 8.0));
        land.fuzziness = static_cast<int>(numberAt(command, "border_fuzziness", 20.0));
        land.borders[0] = numberAt(command, "left_border", 0.0);
        land.borders[1] = numberAt(command, "right_border", 0.0);
        land.borders[2] = numberAt(command, "top_border", 0.0);
        land.borders[3] = numberAt(command, "bottom_border", 0.0);
        land.generateMode = static_cast<int>(numberAt(command, "generate_mode", 0.0));
        if (const RmsAttribute* position = command.find("land_position"); position != nullptr && position->args.size() >= 2) {
            land.positioned = true;
            land.positionX = position->args[0].number;
            land.positionY = position->args[1].number;
        }
        const int percentAt = lastIndexOf(command, "land_percent");
        const int tilesAt = lastIndexOf(command, "number_of_tiles");
        if (tilesAt > percentAt) {
            land.hasTiles = true;
            land.tiles = std::max(0, static_cast<int>(numberAt(command, "number_of_tiles", 0.0)));
        } else {
            land.percent = std::clamp(numberAt(command, "land_percent", 100.0), 0.0, 100.0);
        }
        land.avoid = std::max(0, static_cast<int>(numberAt(command, "other_zone_avoidance_distance", 0.0)));
        land.minDistance = std::max(0, static_cast<int>(numberAt(command, "min_placement_distance", static_cast<double>(land.avoid))));
        if (const RmsAttribute* circle = command.find("circle_radius"); circle != nullptr && !circle->args.empty()) {
            land.circleRadius = circle->args[0].number;
            land.circleVariance = circle->args.size() > 1 ? circle->args[1].number : 0.0;
        }
        if (const RmsAttribute* id = command.find("land_id"); id != nullptr && !id->args.empty()) {
            land.hasLandId = true;
            land.landId = id->args[0].integer();
        }
        if (has(command, "base_elevation")) {
            note(command.find("base_elevation")->line, "base_elevation is skipped: elevation is not generated yet.");
        }
        if (has(command, "land_conformity")) {
            note(command.find("land_conformity")->line, "land_conformity is skipped. The game's own guide says it is unreliable.");
        }
    }

    // The zone a land gets. Player lands each have their own zone unless a zone is set.
    void readZone(const RmsCommand& command, Land& land) {
        if (const RmsAttribute* zone = command.find("zone"); zone != nullptr && !zone->args.empty()) {
            land.zone = zone->args[0].integer();
        } else if (has(command, "set_zone_randomly")) {
            land.zone = std::uniform_int_distribution<int>(-8, std::max(-8, options_.players - 9))(rng_);
        } else if (land.player > 0) {
            // set_zone_by_team has no teams to join, so each player keeps a zone of their own.
            land.zone = land.player - 10;
        } else {
            land.zone = -10;
        }
    }

    void borderRect(const Land& land, int& left, int& right, int& top, int& bottom) const {
        left = static_cast<int>(std::lround(land.borders[0] / 100.0 * width_));
        right = width_ - 1 - static_cast<int>(std::lround(land.borders[1] / 100.0 * width_));
        top = static_cast<int>(std::lround(land.borders[2] / 100.0 * height_));
        bottom = height_ - 1 - static_cast<int>(std::lround(land.borders[3] / 100.0 * height_));
    }

    int clampedBase(const Land& land) const {
        return std::clamp(land.baseSize, 0, std::max(0, (std::min(width_, height_) - 1) / 2));
    }

    void computePlayerCircle(const Land& sample, std::vector<std::pair<int, int>>& spots) {
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;
        borderRect(sample, left, right, top, bottom);
        if (right < left) {
            std::swap(left, right);
        }
        if (bottom < top) {
            std::swap(top, bottom);
        }
        const double centerX = (left + right) * 0.5;
        const double centerY = (top + bottom) * 0.5;
        const double span = std::min(right - left, bottom - top) + 1.0;
        double radius = span * 0.36;
        double variance = 20.0;
        if (sample.circleRadius > 0.0) {
            radius = sample.circleRadius / 100.0 * width_;
            variance = sample.circleVariance;
        }
        const int margin = clampedBase(sample);
        const int players = std::max(1, options_.players);
        std::vector<int> order(static_cast<std::size_t>(players));
        for (int i = 0; i < players; ++i) {
            order[static_cast<std::size_t>(i)] = i;
        }
        // random_placement: players take the seats around the circle in a random order.
        std::shuffle(order.begin(), order.end(), rng_);
        const double start = std::uniform_real_distribution<double>(0.0, 6.283185307179586)(rng_);
        spots.assign(static_cast<std::size_t>(players), {0, 0});
        for (int seat = 0; seat < players; ++seat) {
            const double angle = start + 6.283185307179586 * seat / players;
            const double wobble = std::uniform_real_distribution<double>(-1.0, 1.0)(rng_) * variance / 100.0 * 0.5;
            const double r = std::max(0.0, radius * (1.0 + wobble));
            int x = static_cast<int>(std::lround(centerX + std::cos(angle) * r));
            int y = static_cast<int>(std::lround(centerY + std::sin(angle) * r));
            x = std::clamp(x, std::min(margin, width_ - 1), std::max(0, width_ - 1 - margin));
            y = std::clamp(y, std::min(margin, height_ - 1), std::max(0, height_ - 1 - margin));
            spots[static_cast<std::size_t>(order[static_cast<std::size_t>(seat)])] = {x, y};
        }
    }

    bool placeRandomOrigin(Land& land, std::size_t landIndex) {
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;
        borderRect(land, left, right, top, bottom);
        const int base = clampedBase(land);
        const double centerX = (width_ - 1) * 0.5;
        const double centerY = (height_ - 1) * 0.5;
        // First try to keep this land's base, and the gap its zone keeps, clear of every
        // land placed so far, player lands included; failing that, only of other non-player lands.
        std::vector<int> candidates;
        for (int pass = 0; pass < 2 && candidates.empty(); ++pass) {
            for (int y = 0; y < height_; ++y) {
                for (int x = 0; x < width_; ++x) {
                    if (x < base || y < base || x > width_ - 1 - base || y > height_ - 1 - base) {
                        continue;
                    }
                    if (x < left || x > right || y < top || y > bottom) {
                        continue;
                    }
                    // The game keeps random lands out of the corners unless generate_mode is 1.
                    if (land.generateMode == 0 && std::fabs(x - centerX) > width_ * 0.3 && std::fabs(y - centerY) > height_ * 0.3) {
                        continue;
                    }
                    bool clear = true;
                    for (std::size_t other = 0; other < landIndex && clear; ++other) {
                        const Land& placed = lands_[other];
                        const int distance = chebyshev(x, y, placed.originX, placed.originY);
                        const int gap = placed.zone == land.zone ? 0 : std::min(land.avoid, placed.avoid);
                        if (distance < land.minDistance) {
                            clear = false;
                        } else if (pass == 0 && distance <= base + clampedBase(placed) + gap) {
                            clear = false;
                        } else if (placed.player == 0 && land.player == 0 && distance <= base + clampedBase(placed)) {
                            clear = false;
                        }
                    }
                    if (clear) {
                        candidates.push_back(index(x, y));
                    }
                }
            }
        }
        if (candidates.empty()) {
            land.originX = width_ / 2;
            land.originY = height_ / 2;
            return false;
        }
        const int chosen = candidates[std::uniform_int_distribution<std::size_t>(0, candidates.size() - 1)(rng_)];
        land.originX = chosen % width_;
        land.originY = chosen / width_;
        return true;
    }

    void claim(std::size_t landIndex, int node) {
        Land& land = lands_[landIndex];
        owner_[static_cast<std::size_t>(node)] = static_cast<int>(landIndex);
        land.nodes.push_back(node);
        const int x = node % width_;
        const int y = node / width_;
        static constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& step : kSteps) {
            const int nx = x + step[0];
            const int ny = y + step[1];
            if (nx >= 0 && ny >= 0 && nx < width_ && ny < height_ && owner_[static_cast<std::size_t>(index(nx, ny))] < 0) {
                land.frontier.push_back(index(nx, ny));
            }
        }
    }

    // How far outside its borders a node is, in nodes. 0 inside.
    int outside(const Land& land, int x, int y) const {
        int left = 0;
        int right = 0;
        int top = 0;
        int bottom = 0;
        borderRect(land, left, right, top, bottom);
        const int dx = std::max({left - x, x - right, 0});
        const int dy = std::max({top - y, y - bottom, 0});
        return std::max(dx, dy);
    }

    // True when a node of another zone's land lies within both lands' avoidance distance.
    bool tooCloseToOtherZone(std::size_t landIndex, int x, int y) const {
        const Land& land = lands_[landIndex];
        if (land.avoid <= 0) {
            return false;
        }
        for (int dy = -land.avoid; dy <= land.avoid; ++dy) {
            for (int dx = -land.avoid; dx <= land.avoid; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                    continue;
                }
                const int other = owner_[static_cast<std::size_t>(index(nx, ny))];
                if (other < 0 || other == static_cast<int>(landIndex)) {
                    continue;
                }
                const Land& neighbour = lands_[static_cast<std::size_t>(other)];
                if (neighbour.zone == land.zone || neighbour.avoid <= 0) {
                    continue;
                }
                if (std::max(std::abs(dx), std::abs(dy)) <= std::min(land.avoid, neighbour.avoid)) {
                    return true;
                }
            }
        }
        return false;
    }

    bool landMayTake(std::size_t landIndex, int node) {
        Land& land = lands_[landIndex];
        if (owner_[static_cast<std::size_t>(node)] >= 0) {
            return false;
        }
        const int x = node % width_;
        const int y = node / width_;
        const int beyond = outside(land, x, y);
        if (beyond > 0) {
            signed char& verdict = land.borderVerdict[static_cast<std::size_t>(node)];
            if (verdict == 0) {
                // border_fuzziness 0 ignores the borders, 100 keeps them exactly, and
                // values between let the land spill a little way past them.
                bool keep = false;
                if (land.fuzziness == 0) {
                    keep = true;
                } else if (land.fuzziness > 0 && land.fuzziness < 100) {
                    const double chance = std::pow((100.0 - land.fuzziness) / 100.0, 4.0 * beyond);
                    keep = std::uniform_real_distribution<double>(0.0, 1.0)(rng_) < chance;
                }
                verdict = keep ? 1 : -1;
            }
            if (verdict < 0) {
                return false;
            }
        }
        if (land.avoid > 0) {
            for (int dy = -land.avoid; dy <= land.avoid; ++dy) {
                for (int dx = -land.avoid; dx <= land.avoid; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                        continue;
                    }
                    const int other = owner_[static_cast<std::size_t>(index(nx, ny))];
                    if (other < 0 || other == static_cast<int>(landIndex)) {
                        continue;
                    }
                    const Land& neighbour = lands_[static_cast<std::size_t>(other)];
                    if (neighbour.zone == land.zone || neighbour.avoid <= 0) {
                        continue;
                    }
                    if (std::max(std::abs(dx), std::abs(dy)) <= std::min(land.avoid, neighbour.avoid)) {
                        return false;
                    }
                }
            }
        }
        return true;
    }

    int sameNeighbours(const std::vector<int>& owners, int node, int who) const {
        const int x = node % width_;
        const int y = node / width_;
        int count = 0;
        for (int dy = -1; dy <= 1; ++dy) {
            for (int dx = -1; dx <= 1; ++dx) {
                const int nx = x + dx;
                const int ny = y + dy;
                if ((dx != 0 || dy != 0) && nx >= 0 && ny >= 0 && nx < width_ && ny < height_
                    && owners[static_cast<std::size_t>(index(nx, ny))] == who) {
                    ++count;
                }
            }
        }
        return count;
    }

    // Picks the next node to grow into. clumping_factor sets how many frontier
    // nodes are compared: more samples favour nodes with more of the same land
    // around them, which grows rounder shapes. A negative factor favours the fewest,
    // which grows snakes.
    template <typename Valid>
    int pickFrontier(std::vector<int>& frontier, int clumping, const std::vector<int>& owners, int who, Valid valid) {
        const int samples = std::clamp(1 + std::abs(clumping) / 3, 1, 12);
        int best = -1;
        int bestScore = 0;
        int found = 0;
        int tries = 0;
        while (!frontier.empty() && found < samples && tries < samples * 6) {
            ++tries;
            const std::size_t slot = std::uniform_int_distribution<std::size_t>(0, frontier.size() - 1)(rng_);
            const int node = frontier[slot];
            if (!valid(node)) {
                frontier[slot] = frontier.back();
                frontier.pop_back();
                continue;
            }
            ++found;
            const int score = sameNeighbours(owners, node, who) * (clumping >= 0 ? 1 : -1);
            if (best < 0 || score > bestScore) {
                best = node;
                bestScore = score;
            }
        }
        return best;
    }

    void landGeneration() {
        int base = names_.fallback;
        std::vector<const RmsCommand*> landCommands;
        for (const RmsCommand& command : script_.commands) {
            if (command.section != "LAND_GENERATION") {
                continue;
            }
            if (command.name == "base_terrain" && !command.args.empty()) {
                const int terrain = terrainOf(command.args[0], command.line);
                if (terrain >= 0) {
                    base = terrain;
                }
            } else if (command.name == "create_land" || command.name == "create_player_lands") {
                landCommands.push_back(&command);
            }
        }
        canvas_.fill(base);
        owner_.assign(static_cast<std::size_t>(count_), -1);

        // Read every land first. create_player_lands makes one land per player.
        for (const RmsCommand* command : landCommands) {
            if (command->name == "create_player_lands") {
                for (int player = 1; player <= std::max(1, options_.players); ++player) {
                    Land land;
                    readLand(*command, land);
                    land.player = player;
                    land.playerShare = true;
                    readZone(*command, land);
                    lands_.push_back(land);
                }
                continue;
            }
            Land land;
            readLand(*command, land);
            if (const RmsAttribute* assign = command->find("assign_to_player"); assign != nullptr && !assign->args.empty()) {
                const int player = assign->args[0].integer();
                if (player > options_.players) {
                    // The game skips lands for players who are not playing.
                    continue;
                }
                land.player = std::max(0, player);
            } else if (const RmsAttribute* target = command->find("assign_to"); target != nullptr && target->args.size() >= 2) {
                const int player = target->args[0].integer() == 0 ? target->args[1].integer() : 0;
                if (player > options_.players) {
                    continue;
                }
                land.player = std::max(0, player);
                if (target->args[0].integer() != 0) {
                    note(target->line, "assign_to by colour or team is read as unassigned: Total Annihilation has no teams here.");
                }
            }
            readZone(*command, land);
            lands_.push_back(land);
        }
        if (lands_.empty()) {
            return;
        }

        // Origins, in script order.
        std::vector<std::pair<int, int>> circle;
        for (std::size_t i = 0; i < lands_.size(); ++i) {
            Land& land = lands_[i];
            land.borderVerdict.assign(static_cast<std::size_t>(count_), 0);
            const bool onCircle = land.player > 0 && !(directPlacement_ && land.positioned);
            if (onCircle) {
                if (circle.empty()) {
                    computePlayerCircle(land, circle);
                }
                const std::size_t seat = static_cast<std::size_t>(land.player - 1);
                land.originX = circle[std::min(seat, circle.size() - 1)].first;
                land.originY = circle[std::min(seat, circle.size() - 1)].second;
            } else if (land.positioned) {
                land.originX = clampX(static_cast<int>(std::floor(land.positionX / 100.0 * width_)));
                land.originY = clampY(static_cast<int>(std::floor(land.positionY / 100.0 * height_)));
            } else if (!placeRandomOrigin(land, i)) {
                note(land.line, "There was no room for this land's origin, so it starts at the middle of the map.");
            }
        }

        // Bases first, then every land grows at once, one node per land per round.
        for (std::size_t i = 0; i < lands_.size(); ++i) {
            Land& land = lands_[i];
            const int radius = clampedBase(land);
            int claimed = 0;
            for (int dy = -radius; dy <= radius; ++dy) {
                for (int dx = -radius; dx <= radius; ++dx) {
                    if (land.circular && radius > 3 && dx * dx + dy * dy > (radius + 0.5) * (radius + 0.5)) {
                        continue;
                    }
                    const int x = land.originX + dx;
                    const int y = land.originY + dy;
                    if (x < 0 || y < 0 || x >= width_ || y >= height_ || owner_[static_cast<std::size_t>(index(x, y))] >= 0) {
                        continue;
                    }
                    // The base keeps the same distance from other zones as the land's growth does,
                    // so two islands' bases never run together. The origin itself is always claimed.
                    if ((dx != 0 || dy != 0) && tooCloseToOtherZone(i, x, y)) {
                        continue;
                    }
                    claim(i, index(x, y));
                    ++claimed;
                }
            }
            if (land.hasTiles) {
                land.target = behaviorVersion_ >= 1 ? std::max(claimed, land.tiles) : claimed + land.tiles;
            } else {
                double share = land.percent / 100.0 * count_;
                if (land.playerShare) {
                    share /= std::max(1, options_.players);
                }
                land.target = std::max(claimed, static_cast<int>(std::lround(share)));
            }
        }
        bool growing = true;
        while (growing) {
            growing = false;
            for (std::size_t i = 0; i < lands_.size(); ++i) {
                Land& land = lands_[i];
                if (land.done || static_cast<int>(land.nodes.size()) >= land.target) {
                    continue;
                }
                const int next = pickFrontier(land.frontier, land.clumping, owner_, static_cast<int>(i),
                                              [&](int node) { return landMayTake(i, node); });
                if (next < 0) {
                    land.done = true;
                    continue;
                }
                claim(i, next);
                growing = true;
            }
        }

        // Paint in script order, so a later land's brush edges go over an earlier one.
        outcome_.playerOrigins.assign(static_cast<std::size_t>(std::max(0, options_.players)), {-1, -1});
        for (const Land& land : lands_) {
            for (const int node : land.nodes) {
                canvas_.paint(node % width_, node / width_, land.terrain);
            }
            if (land.player > 0 && land.player <= options_.players) {
                outcome_.playerOrigins[static_cast<std::size_t>(land.player - 1)] = {land.originX, land.originY};
                playerOrigins_.emplace_back(land.originX, land.originY);
            }
            ++outcome_.lands;
        }
    }

    // ---------------------------------------------------------------- terrains

    std::vector<int> snapshot() const {
        std::vector<int> nodes(static_cast<std::size_t>(count_));
        for (int y = 0; y < height_; ++y) {
            for (int x = 0; x < width_; ++x) {
                nodes[static_cast<std::size_t>(index(x, y))] = canvas_.at(x, y);
            }
        }
        return nodes;
    }

    void terrainGeneration() {
        for (std::size_t index = 0; index < script_.commands.size(); ++index) {
            const RmsCommand& command = script_.commands[index];
            if (command.section != "TERRAIN_GENERATION" || command.name != "create_terrain") {
                continue;
            }
            if (command.args.empty()) {
                continue;
            }
            if (names_.isStamp && names_.isStamp(command.args[0].text)) {
                // Hills and islands are stamped once the map is painted.
                outcome_.stampCommands.push_back(index);
                ++outcome_.terrains;
                continue;
            }
            const int terrain = terrainOf(command.args[0], command.line);
            if (terrain < 0) {
                continue;
            }
            createTerrain(command, terrain);
        }
    }

    // ------------------------------------------------------------- elevation

    // create_elevation raises hills. Total Annihilation hills are the Tile Brush's own
    // hill stamps, so each one is set aside for planStampBrushes, which places the
    // hills of its base_terrain once the map is painted.
    void elevationGeneration() {
        for (std::size_t index = 0; index < script_.commands.size(); ++index) {
            const RmsCommand& command = script_.commands[index];
            if (command.section == "ELEVATION_GENERATION" && command.name == "create_elevation") {
                outcome_.elevationCommands.push_back(index);
            }
        }
    }

    // ---------------------------------------------------------------- cliffs

    // <CLIFF_GENERATION> draws short ridges of high ground, such as GREENWORLD_BASE-219
    // across GREENWORLD_BASE-85, so the Tile Brush gives each one cliff faces. Lengths
    // and distances are script tiles, game-size squares, like every other distance.
    // As in the game, cliffs come before terrain, which keeps its spacing from them,
    // and connections go round them. They stay off player start areas and coasts.
    void cliffGeneration() {
        bool present = false;
        for (const auto& [section, line] : script_.sections) {
            present = present || section == "CLIFF_GENERATION";
        }
        if (!present || !names_.cliffAbove) {
            return;
        }
        int minCount = 3;
        int maxCount = 8;
        int minLength = 2;
        int maxLength = 4;
        int curliness = 36;
        int minDistance = 2;
        int terrainDistance = 1;
        int line = 0;
        for (const RmsCommand& command : script_.commands) {
            if (command.section != "CLIFF_GENERATION") {
                continue;
            }
            line = line == 0 ? command.line : line;
            const int value = command.args.empty() ? 0 : command.args[0].integer();
            if (command.name == "min_number_of_cliffs") {
                minCount = std::max(0, value);
            } else if (command.name == "max_number_of_cliffs") {
                maxCount = std::max(0, value);
            } else if (command.name == "min_length_of_cliff") {
                minLength = std::max(1, value);
            } else if (command.name == "max_length_of_cliff") {
                maxLength = std::max(1, value);
            } else if (command.name == "cliff_curliness") {
                curliness = std::clamp(value, 0, 100);
            } else if (command.name == "min_distance_cliffs") {
                minDistance = std::max(0, value);
            } else if (command.name == "min_terrain_distance") {
                terrainDistance = std::max(1, value);
            } else if (command.name == "cliff_type") {
                note(command.line, "cliff_type is skipped: each ground has its own cliffs in Total Annihilation.");
            }
        }
        maxCount = std::max(minCount, maxCount);
        maxLength = std::max(minLength, maxLength);
        // Like the game, more cliffs on a bigger map.
        const double ratio = std::max(1.0, count_ / kScaleReference);
        const int wanted = static_cast<int>(std::lround(std::uniform_int_distribution<int>(minCount, maxCount)(rng_) * ratio));
        if (wanted <= 0) {
            return;
        }
        const std::vector<int> ground = snapshot();
        const int avoidStarts = std::max(1, static_cast<int>(std::lround(std::max(width_, height_) / 9.0)));
        cliff_.assign(static_cast<std::size_t>(count_), 0);
        std::vector<char>& cliff = cliff_;
        const auto fits = [&](int x, int y, int terrain) {
            if (x < 1 || y < 1 || x >= width_ - 1 || y >= height_ - 1) {
                return false;
            }
            for (const auto& [ox, oy] : playerOrigins_) {
                if (chebyshev(x, y, ox, oy) <= avoidStarts) {
                    return false;
                }
            }
            const int reach = std::max(terrainDistance, minDistance);
            for (int dy = -reach; dy <= reach; ++dy) {
                for (int dx = -reach; dx <= reach; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                        if (std::max(std::abs(dx), std::abs(dy)) <= terrainDistance) {
                            return false;
                        }
                        continue;
                    }
                    const int distance = std::max(std::abs(dx), std::abs(dy));
                    if (distance <= terrainDistance && ground[static_cast<std::size_t>(index(nx, ny))] != terrain) {
                        return false;
                    }
                    if (distance <= minDistance && cliff[static_cast<std::size_t>(index(nx, ny))]) {
                        return false;
                    }
                }
            }
            return true;
        };
        static constexpr int kSteps[4][2] = {{1, 0}, {0, 1}, {-1, 0}, {0, -1}};
        int made = 0;
        for (int attempt = 0; attempt < wanted * 40 && made < wanted; ++attempt) {
            const int start = std::uniform_int_distribution<int>(0, count_ - 1)(rng_);
            const int terrain = ground[static_cast<std::size_t>(start)];
            const int high = terrain >= 0 ? names_.cliffAbove(terrain) : -1;
            if (high < 0 || !fits(start % width_, start / width_, terrain)) {
                continue;
            }
            const int length = std::uniform_int_distribution<int>(minLength, maxLength)(rng_);
            std::vector<int> path = {start};
            int direction = std::uniform_int_distribution<int>(0, 3)(rng_);
            while (static_cast<int>(path.size()) < length) {
                if (std::uniform_int_distribution<int>(0, 99)(rng_) < curliness) {
                    direction = (direction + (std::uniform_int_distribution<int>(0, 1)(rng_) == 0 ? 1 : 3)) % 4;
                }
                const int x = path.back() % width_ + kSteps[direction][0];
                const int y = path.back() / width_ + kSteps[direction][1];
                if (!fits(x, y, terrain) || std::find(path.begin(), path.end(), index(x, y)) != path.end()) {
                    break;
                }
                path.push_back(index(x, y));
            }
            if (static_cast<int>(path.size()) < std::min(minLength, 2)) {
                continue;
            }
            // Two nodes that touch only at a corner leave a brush cell with no piece.
            std::vector<char> mine(static_cast<std::size_t>(count_), 0);
            for (const int node : path) {
                mine[static_cast<std::size_t>(node)] = 1;
            }
            bool saddle = false;
            for (const int node : path) {
                const int x = node % width_;
                const int y = node / width_;
                for (int cy = y - 1; cy <= y && !saddle; ++cy) {
                    for (int cx = x - 1; cx <= x && !saddle; ++cx) {
                        const bool a = mine[static_cast<std::size_t>(index(cx, cy))];
                        const bool b = mine[static_cast<std::size_t>(index(cx + 1, cy))];
                        const bool c = mine[static_cast<std::size_t>(index(cx, cy + 1))];
                        const bool d = mine[static_cast<std::size_t>(index(cx + 1, cy + 1))];
                        saddle = (a && d && !b && !c) || (b && c && !a && !d);
                    }
                }
            }
            if (saddle) {
                continue;
            }
            for (const int node : path) {
                cliff[static_cast<std::size_t>(node)] = 1;
                canvas_.paint(node % width_, node / width_, high);
            }
            ++made;
        }
        outcome_.cliffs = made;
        if (made < wanted) {
            note(line, "<CLIFF_GENERATION> drew " + std::to_string(made) + " of " + std::to_string(wanted)
                           + (made == 0 ? " cliffs: it needs open ground that has high ground of its own, such as GREENWORLD_BASE-85."
                                        : " cliffs: there was no more room."));
        }
    }

    void createTerrain(const RmsCommand& command, int terrain) {
        int base = -1;
        if (const RmsAttribute* attribute = command.find("base_terrain"); attribute != nullptr && !attribute->args.empty()) {
            base = terrainOf(attribute->args[0], attribute->line);
            if (base < 0) {
                return;
            }
        }
        for (const char* skipped : {"height_limits", "set_flat_terrain_only", "beach_terrain", "terrain_mask", "base_layer"}) {
            if (const RmsAttribute* attribute = command.find(skipped)) {
                note(attribute->line, std::string(skipped) + " is skipped: it has no meaning for Total Annihilation terrain yet.");
            }
        }
        const std::vector<int> ground = snapshot();

        double total = 0.0;
        const int percentAt = lastIndexOf(command, "land_percent");
        const int tilesAt = lastIndexOf(command, "number_of_tiles");
        if (percentAt > tilesAt) {
            total = std::clamp(numberAt(command, "land_percent", 0.0), 0.0, 100.0) / 100.0 * count_;
        } else if (tilesAt >= 0) {
            total = std::max(0.0, numberAt(command, "number_of_tiles", 0.0));
        } else {
            // The game's default of 122 tiles on a 120 x 120 tiny map, as a share of this map.
            total = std::max(1.0, count_ * 122.0 / 14400.0);
        }
        double clumps = std::max(1.0, numberAt(command, "number_of_clumps", 1.0));
        const int bySize = lastIndexOf(command, "set_scale_by_size");
        const int byGroups = lastIndexOf(command, "set_scale_by_groups");
        const double ratio = count_ / kScaleReference;
        if (byGroups > bySize) {
            clumps *= ratio;
            total *= ratio;
        } else if (bySize >= 0) {
            total *= ratio;
        }
        const int clumpCount = std::max(1, static_cast<int>(std::lround(clumps)));
        const int perClump = std::max(1, static_cast<int>(std::lround(total / clumpCount)));
        if (total < 0.5) {
            return;
        }

        const int spacing = std::max(0, static_cast<int>(numberAt(command, "spacing_to_other_terrain_types", 0.0)));
        std::vector<std::pair<int, int>> specific;
        for (const RmsAttribute* attribute : command.findAll("spacing_to_specific_terrain")) {
            const int other = terrainOf(attribute->args[0], attribute->line);
            if (other >= 0) {
                specific.emplace_back(other, std::max(0, attribute->args[1].integer()));
            }
        }
        int avoidStarts = -1;
        if (const RmsAttribute* attribute = command.find("set_avoid_player_start_areas")) {
            // Without a distance the game keeps 13 tiles away on maps about 120 tiles
            // across. Here that is about a ninth of the map.
            avoidStarts = attribute->args.empty() ? std::max(1, static_cast<int>(std::lround(std::max(width_, height_) / 9.0)))
                                                  : std::max(0, attribute->args[0].integer());
        }
        const int clumping = static_cast<int>(numberAt(command, "clumping_factor", 20.0));

        std::vector<int> taken(static_cast<std::size_t>(count_), -1);
        const auto mayTake = [&](int node) {
            if (taken[static_cast<std::size_t>(node)] >= 0) {
                return false;
            }
            const int here = ground[static_cast<std::size_t>(node)];
            if (here == terrain || (base >= 0 && here != base)) {
                return false;
            }
            const int x = node % width_;
            const int y = node / width_;
            int widest = spacing;
            for (const auto& [other, distance] : specific) {
                widest = std::max(widest, distance);
            }
            for (int dy = -widest; dy <= widest; ++dy) {
                for (int dx = -widest; dx <= widest; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                        continue;
                    }
                    const int distance = std::max(std::abs(dx), std::abs(dy));
                    const int there = ground[static_cast<std::size_t>(index(nx, ny))];
                    if (distance <= spacing && there != here && there != terrain) {
                        return false;
                    }
                    for (const auto& [other, limit] : specific) {
                        if (there == other && distance <= limit) {
                            return false;
                        }
                    }
                }
            }
            if (avoidStarts >= 0) {
                for (const auto& [ox, oy] : playerOrigins_) {
                    if (chebyshev(x, y, ox, oy) <= avoidStarts) {
                        return false;
                    }
                }
            }
            return true;
        };

        std::vector<int> candidates;
        for (int node = 0; node < count_; ++node) {
            if (mayTake(node)) {
                candidates.push_back(node);
            }
        }
        if (candidates.empty()) {
            note(command.line, "create_terrain " + command.args[0].text + " found no room on this map.");
            return;
        }
        std::shuffle(candidates.begin(), candidates.end(), rng_);
        struct Clump {
            std::vector<int> nodes;
            std::vector<int> frontier;
            bool done = false;
        };
        std::vector<Clump> clumpList;
        std::vector<int> order;
        for (const int node : candidates) {
            if (static_cast<int>(clumpList.size()) >= clumpCount) {
                break;
            }
            if (taken[static_cast<std::size_t>(node)] >= 0) {
                continue;
            }
            clumpList.emplace_back();
            const int who = static_cast<int>(clumpList.size()) - 1;
            taken[static_cast<std::size_t>(node)] = who;
            clumpList.back().nodes.push_back(node);
            order.push_back(node);
            pushNeighbours(clumpList.back().frontier, node);
        }
        bool growing = true;
        while (growing) {
            growing = false;
            for (std::size_t c = 0; c < clumpList.size(); ++c) {
                Clump& clump = clumpList[c];
                if (clump.done || static_cast<int>(clump.nodes.size()) >= perClump) {
                    continue;
                }
                const int next = pickFrontier(clump.frontier, clumping, taken, static_cast<int>(c), mayTake);
                if (next < 0) {
                    clump.done = true;
                    continue;
                }
                taken[static_cast<std::size_t>(next)] = static_cast<int>(c);
                clump.nodes.push_back(next);
                order.push_back(next);
                pushNeighbours(clump.frontier, next);
                growing = true;
            }
        }
        for (const int node : order) {
            canvas_.paint(node % width_, node / width_, terrain);
        }
        ++outcome_.terrains;
    }

    void pushNeighbours(std::vector<int>& frontier, int node) const {
        const int x = node % width_;
        const int y = node / width_;
        static constexpr int kSteps[4][2] = {{1, 0}, {-1, 0}, {0, 1}, {0, -1}};
        for (const auto& step : kSteps) {
            const int nx = x + step[0];
            const int ny = y + step[1];
            if (nx >= 0 && ny >= 0 && nx < width_ && ny < height_) {
                frontier.push_back(index(nx, ny));
            }
        }
    }

    // ---------------------------------------------------------------- connections

    std::vector<int> findPath(int from, int to, const std::vector<int>& ground, const std::map<int, double>& costs) const {
        const auto costOf = [&](int node) {
            const auto found = costs.find(ground[static_cast<std::size_t>(node)]);
            // Cliffs are walls: a path goes round one unless the script prices it.
            if (found == costs.end() && static_cast<std::size_t>(node) < cliff_.size() && cliff_[static_cast<std::size_t>(node)]) {
                return 25.0;
            }
            return found == costs.end() ? 1.0 : found->second;
        };
        if (costOf(from) <= 0.0 || costOf(to) <= 0.0) {
            return {};
        }
        std::vector<double> best(static_cast<std::size_t>(count_), std::numeric_limits<double>::infinity());
        std::vector<int> previous(static_cast<std::size_t>(count_), -1);
        using Entry = std::pair<double, int>;
        std::priority_queue<Entry, std::vector<Entry>, std::greater<Entry>> open;
        best[static_cast<std::size_t>(from)] = 0.0;
        open.emplace(0.0, from);
        while (!open.empty()) {
            const auto [distance, node] = open.top();
            open.pop();
            if (distance > best[static_cast<std::size_t>(node)]) {
                continue;
            }
            if (node == to) {
                break;
            }
            const int x = node % width_;
            const int y = node / width_;
            for (int dy = -1; dy <= 1; ++dy) {
                for (int dx = -1; dx <= 1; ++dx) {
                    const int nx = x + dx;
                    const int ny = y + dy;
                    if ((dx == 0 && dy == 0) || nx < 0 || ny < 0 || nx >= width_ || ny >= height_) {
                        continue;
                    }
                    const int next = index(nx, ny);
                    const double step = costOf(next);
                    if (step <= 0.0) {
                        continue;
                    }
                    const double length = distance + step * (dx != 0 && dy != 0 ? 1.4142 : 1.0);
                    if (length < best[static_cast<std::size_t>(next)]) {
                        best[static_cast<std::size_t>(next)] = length;
                        previous[static_cast<std::size_t>(next)] = node;
                        open.emplace(length, next);
                    }
                }
            }
        }
        if (previous[static_cast<std::size_t>(to)] < 0 && from != to) {
            return {};
        }
        std::vector<int> path;
        for (int node = to; node >= 0; node = previous[static_cast<std::size_t>(node)]) {
            path.push_back(node);
            if (node == from) {
                break;
            }
        }
        std::reverse(path.begin(), path.end());
        return path;
    }

    void connectionGeneration() {
        bool accumulate = false;
        std::vector<int> start;
        bool started = false;
        for (const RmsCommand& command : script_.commands) {
            if (command.section != "CONNECTION_GENERATION") {
                continue;
            }
            if (!started) {
                start = snapshot();
                started = true;
            }
            if (command.name == "accumulate_connections") {
                accumulate = true;
                continue;
            }
            if (command.name.compare(0, 15, "create_connect_") != 0) {
                continue;
            }
            if (lands_.empty()) {
                note(command.line, command.name + " has no lands to connect.");
                continue;
            }
            const std::vector<int> ground = accumulate ? snapshot() : start;

            std::map<int, double> costs;
            std::map<int, std::pair<int, int>> sizes;
            std::map<int, int> replace;
            int replaceAll = -1;
            for (const RmsAttribute& attribute : command.attributes) {
                if (attribute.name == "terrain_cost") {
                    const int terrain = terrainOf(attribute.args[0], attribute.line);
                    if (terrain >= 0) {
                        costs[terrain] = attribute.args[1].number;
                    }
                } else if (attribute.name == "terrain_size") {
                    const int terrain = terrainOf(attribute.args[0], attribute.line);
                    if (terrain >= 0) {
                        sizes[terrain] = {attribute.args[1].integer(), attribute.args[2].integer()};
                    }
                } else if (attribute.name == "replace_terrain") {
                    const int from = terrainOf(attribute.args[0], attribute.line);
                    const int to = terrainOf(attribute.args[1], attribute.line);
                    if (from >= 0 && to >= 0) {
                        replace[from] = to;
                    }
                } else if (attribute.name == "default_terrain_replacement") {
                    const int to = terrainOf(attribute.args[0], attribute.line);
                    if (to >= 0) {
                        // It replaces everything, over the replace_terrain lines before it.
                        replaceAll = to;
                        replace.clear();
                    }
                }
            }

            std::vector<std::pair<std::size_t, std::size_t>> pairs;
            const auto connectAll = [&](const std::function<bool(const Land&)>& include) {
                for (std::size_t a = 0; a < lands_.size(); ++a) {
                    for (std::size_t b = a + 1; b < lands_.size(); ++b) {
                        if (include(lands_[a]) && include(lands_[b])) {
                            pairs.emplace_back(a, b);
                        }
                    }
                }
            };
            if (command.name == "create_connect_all_players_land") {
                connectAll([](const Land& land) { return land.player > 0; });
            } else if (command.name == "create_connect_all_lands" || command.name == "create_connect_same_land_zones") {
                connectAll([](const Land&) { return true; });
            } else if (command.name == "create_connect_land_zones") {
                const int first = command.args.size() > 0 ? command.args[0].integer() : 0;
                const int second = command.args.size() > 1 ? command.args[1].integer() : 0;
                connectAll([&](const Land& land) { return land.zone == first || land.zone == second; });
            } else if (command.name == "create_connect_to_nonplayer_land") {
                for (std::size_t a = 0; a < lands_.size(); ++a) {
                    for (std::size_t b = 0; b < lands_.size(); ++b) {
                        if (lands_[a].player > 0 && lands_[b].player == 0) {
                            pairs.emplace_back(a, b);
                        }
                    }
                }
            } else if (command.name == "create_connect_teams_lands") {
                note(command.line, "create_connect_teams_lands connects nothing: Total Annihilation maps have no teams here.");
                continue;
            }

            std::map<int, int> changes;
            int made = 0;
            for (const auto& [a, b] : pairs) {
                const std::vector<int> path = findPath(index(lands_[a].originX, lands_[a].originY),
                                                       index(lands_[b].originX, lands_[b].originY), ground, costs);
                if (path.empty()) {
                    continue;
                }
                ++made;
                for (const int node : path) {
                    const int here = ground[static_cast<std::size_t>(node)];
                    const auto sized = sizes.find(here);
                    const int radius = sized == sizes.end() ? 1 : sized->second.first;
                    const int variance = sized == sizes.end() ? 0 : std::abs(sized->second.second);
                    const int reach = radius + (variance > 0 ? std::uniform_int_distribution<int>(-variance, variance)(rng_) : 0);
                    if (reach < 0) {
                        continue;
                    }
                    const int x = node % width_;
                    const int y = node / width_;
                    for (int dy = -reach; dy <= reach; ++dy) {
                        for (int dx = -reach; dx <= reach; ++dx) {
                            const int nx = x + dx;
                            const int ny = y + dy;
                            if (nx < 0 || ny < 0 || nx >= width_ || ny >= height_ || dx * dx + dy * dy > reach * reach + reach) {
                                continue;
                            }
                            const int target = index(nx, ny);
                            const int old = ground[static_cast<std::size_t>(target)];
                            const auto found = replace.find(old);
                            const int to = found != replace.end() ? found->second : replaceAll;
                            if (to >= 0 && to != old) {
                                changes[target] = to;
                            }
                        }
                    }
                }
            }
            for (const auto& [node, terrain] : changes) {
                canvas_.paint(node % width_, node / width_, terrain);
            }
            if (made > 0) {
                ++outcome_.connections;
            } else if (!pairs.empty()) {
                note(command.line, command.name + " found no path between the lands.");
            }
        }
    }

    // ---------------------------------------------------------------- objects

    // What a create_object name stands for: an exact feature, a doodad kind that
    // follows the land under each spot, or a kind from one world.
    struct ObjectChoice {
        std::vector<const RmsFeature*> features;
        bool followsLand = false;
        // True for a kind, such as TREES or LUSH_TREES, rather than one named feature.
        bool byKind = false;
        DoodadKind kind = DoodadKind::Trees;
        // Grounds that have features of their own in this choice.
        std::set<std::string> lands;
        // Whether each feature suits each ground, worked out once: the checks read names
        // and would otherwise run for every cell a feature tries.
        mutable std::unordered_map<std::uint64_t, bool> suits;
        // The features that may stand on each ground, by terrain.
        mutable std::unordered_map<int, std::vector<const RmsFeature*>> byGround;
        // 1 when every feature is a tree one cell across, -1 until worked out.
        mutable int smallTrees = -1;
    };

    static bool allWorlds(const RmsFeature& feature) {
        const std::string world = lower(feature.world);
        return world == "all worlds" || world == "allworlds" || world == "allworld" || world == "all";
    }

    // Metal and vents are resources every map needs, so ground with none of its own,
    // such as Lush or Acid, uses the ones made for all worlds.
    static bool sharedFallback(DoodadKind kind) {
        return kind == DoodadKind::MetalNodes || kind == DoodadKind::GeoVents;
    }

    static std::string scriptWorld(std::string_view world) {
        std::string out = upper(world);
        for (char& character : out) {
            if (character == ' ' || character == '-') {
                character = '_';
            }
        }
        return out;
    }

    static std::string landScriptName(const std::string& land) {
        if (land == "Green") {
            return "GREENWORLD";
        }
        if (land == "Archi") {
            return "ARCHIPELAGO";
        }
        return upper(land);
    }

    bool resolveObject(const std::string& name, ObjectChoice& choice) const {
        const RmsObjectSettings& objects = *options_.objects;
        const std::string wanted = upper(name);
        for (const RmsFeature& feature : objects.features) {
            if (upper(feature.name) == wanted) {
                choice.features.push_back(&feature);
                return true;
            }
        }
        std::vector<DoodadKindInfo> kinds = doodadKinds();
        // LAVA_SPIRES before SPIRES, so the longer name wins.
        std::stable_sort(kinds.begin(), kinds.end(), [](const DoodadKindInfo& left, const DoodadKindInfo& right) {
            return std::string_view(left.name).size() > std::string_view(right.name).size();
        });
        for (const DoodadKindInfo& info : kinds) {
            if (wanted == info.name) {
                choice.followsLand = true;
                choice.byKind = true;
                choice.kind = info.kind;
                for (const RmsFeature& feature : objects.features) {
                    if (featureIsDoodad(feature, info.kind)) {
                        choice.features.push_back(&feature);
                        for (const std::string& land : featureLands(feature, info.kind)) {
                            choice.lands.insert(land);
                        }
                    }
                }
                return true;
            }
        }
        for (const DoodadKindInfo& info : kinds) {
            const std::string suffix = std::string("_") + info.name;
            if (wanted.size() <= suffix.size() || wanted.compare(wanted.size() - suffix.size(), suffix.size(), suffix) != 0) {
                continue;
            }
            const std::string world = wanted.substr(0, wanted.size() - suffix.size());
            choice.byKind = true;
            choice.kind = info.kind;
            for (const RmsFeature& feature : objects.features) {
                if (!featureIsDoodad(feature, info.kind)) {
                    continue;
                }
                const std::vector<std::string> lands = featureLands(feature, info.kind);
                if (scriptWorld(feature.world) == world
                    || std::any_of(lands.begin(), lands.end(), [&](const std::string& land) { return landScriptName(land) == world; })) {
                    choice.features.push_back(&feature);
                }
            }
            return true;
        }
        return false;
    }

    struct ObjectRules;

    void objectGeneration() {
        if (options_.objects == nullptr) {
            return;
        }
        const RmsObjectSettings& objects = *options_.objects;
        bool any = false;
        const RmsCommand* startCommand = nullptr;
        for (const RmsCommand& command : script_.commands) {
            any = any || command.section == "OBJECTS_GENERATION";
            if (command.section == "OBJECTS_GENERATION" && command.name == "create_object" && !command.args.empty() && isStartName(command.args[0].text)) {
                startCommand = startCommand == nullptr ? &command : startCommand;
            }
        }
        const bool wantsObjects = any || script_.hasSection("OBJECTS_GENERATION");
        // Player lands bring their starts with them, the way the game puts a town
        // centre on each one. START_POSITION places them by its own rules instead.
        const bool wantsStarts = startCommand != nullptr || !playerOrigins_.empty();
        if (!wantsObjects && !wantsStarts) {
            return;
        }
        outcome_.placesObjects = wantsObjects;
        cellsWide_ = std::max(1, objects.cellsWide);
        cellsHigh_ = std::max(1, objects.cellsHigh);
        perNode_ = std::max(1, objects.cellsPerNode);
        occupied_.assign(static_cast<std::size_t>(cellsWide_) * static_cast<std::size_t>(cellsHigh_), 0);
        tilesWide_ = (cellsWide_ + 1) / 2;
        treesOnTile_.assign(static_cast<std::size_t>(tilesWide_) * static_cast<std::size_t>((cellsHigh_ + 1) / 2), 0);
        treeCells_.assign(occupied_.size(), 0);
        // The ground under each brush cell: its paint when all four corners agree,
        // otherwise -1 for a coast, cliff or blend, where features stay off.
        const int latticeX = std::max(1, width_ - 1);
        const int latticeY = std::max(1, height_ - 1);
        flat_.assign(static_cast<std::size_t>(latticeX) * static_cast<std::size_t>(latticeY), -1);
        for (int y = 0; y < latticeY; ++y) {
            for (int x = 0; x < latticeX; ++x) {
                const int corner = canvas_.at(x, y);
                const bool same = width_ > 1 && height_ > 1
                    ? corner == canvas_.at(x + 1, y) && corner == canvas_.at(x, y + 1) && corner == canvas_.at(x + 1, y + 1)
                    : true;
                flat_[static_cast<std::size_t>(y) * static_cast<std::size_t>(latticeX) + static_cast<std::size_t>(x)] = same ? corner : -1;
            }
        }
        latticeX_ = latticeX;
        latticeY_ = latticeY;
        if (wantsStarts && startCommand == nullptr) {
            placeStarts(nullptr);
        }
        int commandIndex = 0;
        bool startsDone = false;
        int objectCommands = 0;
        for (const RmsCommand& command : script_.commands) {
            objectCommands += command.section == "OBJECTS_GENERATION" && command.name == "create_object" && !command.args.empty() ? 1 : 0;
        }
        int objectCommand = 0;
        for (const RmsCommand& command : script_.commands) {
            if (command.section != "OBJECTS_GENERATION" || command.name != "create_object" || command.args.empty()) {
                continue;
            }
            // Each create_object is an even share of the step.
            commandShare_ = 1.0 / std::max(1, objectCommands);
            commandStart_ = commandShare_ * objectCommand++;
            report(RandomMapStage::Objects, commandStart_);
            if (isStartName(command.args[0].text)) {
                if (startsDone) {
                    note(command.line, "Start positions were already placed by an earlier " + command.args[0].text + ".");
                } else {
                    placeStarts(&command);
                    startsDone = true;
                }
                continue;
            }
            createObject(command, commandIndex++);
        }
    }

    static bool isStartName(const std::string& name) {
        const std::string wanted = upper(name);
        return wanted == "START_POSITION" || wanted == "TOWN_CENTER";
    }

    // A start needs a flat, dry square around it with nothing on it, so the
    // commander has room. kStartClear is half that square, in feature cells.
    static constexpr int kStartClear = 3;
    // Doodads placed afterwards keep this far from every start.
    static constexpr int kStartKeep = 6;

    bool startFits(const ObjectRules& rules, int x, int y) const {
        const RmsObjectSettings& objects = *options_.objects;
        for (int cy = y - kStartClear; cy < y + kStartClear; ++cy) {
            for (int cx = x - kStartClear; cx < x + kStartClear; ++cx) {
                if (cx < 0 || cy < 0 || cx >= cellsWide_ || cy >= cellsHigh_
                    || occupied_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(cx)]) {
                    return false;
                }
                const int ground = groundAt(cx, cy);
                if (ground < 0) {
                    return false;
                }
                if (rules.terrain >= 0 ? ground != rules.terrain
                                       : (static_cast<std::size_t>(ground) < objects.terrainLiquid.size() && objects.terrainLiquid[static_cast<std::size_t>(ground)])) {
                    return false;
                }
            }
        }
        return true;
    }

    void placeStarts(const RmsCommand* command) {
        outcome_.placesStarts = true;
        ObjectRules rules;
        bool exact = command == nullptr;
        if (command != nullptr) {
            if (const RmsAttribute* attribute = command->find("terrain_to_place_on"); attribute != nullptr && !attribute->args.empty()) {
                rules.terrain = terrainOf(attribute->args[0], attribute->line);
            }
            if (const RmsAttribute* attribute = command->find("place_on_specific_land_id"); attribute != nullptr && !attribute->args.empty()) {
                rules.hasLandId = true;
                rules.landId = attribute->args[0].integer();
            }
            rules.minPlayers = has(*command, "min_distance_to_players") ? std::max(0, static_cast<int>(numberAt(*command, "min_distance_to_players", 0.0))) : -1;
            rules.maxPlayers = has(*command, "max_distance_to_players") ? std::max(0, static_cast<int>(numberAt(*command, "max_distance_to_players", 0.0))) : -1;
            rules.edge = std::max(0, static_cast<int>(numberAt(*command, "min_distance_to_map_edge", 0.0)));
            if (const RmsAttribute* attribute = command->find("avoid_cliff_zone")) {
                rules.cliff = attribute->args.empty() ? 4 : std::max(0, attribute->args[0].integer());
            }
            exact = rules.maxPlayers == 0;
        }
        const int spacing = command == nullptr ? 0
            : std::max(static_cast<int>(numberAt(*command, "min_distance_group_placement", 0.0)),
                       static_cast<int>(numberAt(*command, "temp_min_distance_group_placement", 0.0)));
        const auto farEnough = [&](int x, int y) {
            for (const RmsStart& start : outcome_.starts) {
                if (spacing > 0 && (start.x - x) * (start.x - x) + (start.y - y) * (start.y - y) < spacing * spacing) {
                    return false;
                }
            }
            return true;
        };
        const auto nearestTo = [&](int ox, int oy, ObjectRules round) {
            // The nearest place with room, however far: max_distance_to_players 0 only
            // means "on the origin if it fits".
            round.maxPlayers = -1;
            const int reach = std::max(cellsWide_, cellsHigh_);
            for (int r = 0; r <= reach; ++r) {
                std::vector<std::pair<int, int>> ring;
                for (int dy = -r; dy <= r; ++dy) {
                    for (int dx = -r; dx <= r; ++dx) {
                        if (std::max(std::abs(dx), std::abs(dy)) == r) {
                            ring.emplace_back(ox + dx, oy + dy);
                        }
                    }
                }
                std::shuffle(ring.begin(), ring.end(), rng_);
                for (const auto& [x, y] : ring) {
                    if (startFits(round, x, y) && farEnough(x, y) && cellAllowed(round, x, y, -1, -1)) {
                        return std::pair<int, int>{x, y};
                    }
                }
            }
            return std::pair<int, int>{-1, -1};
        };
        const bool haveOrigins = !playerOrigins_.empty();
        for (int player = 1; player <= std::max(1, options_.players); ++player) {
            ObjectRules round = rules;
            const std::size_t slot = static_cast<std::size_t>(player - 1);
            const bool origin = slot < outcome_.playerOrigins.size() && outcome_.playerOrigins[slot].first >= 0;
            round.player = origin ? player : 0;
            if (!origin) {
                round.minPlayers = -1;
                round.maxPlayers = -1;
            }
            std::pair<int, int> spot{-1, -1};
            if (origin && exact) {
                // On the land's origin, or the nearest place there is room.
                spot = nearestTo(outcome_.playerOrigins[slot].first * perNode_, outcome_.playerOrigins[slot].second * perNode_, round);
            } else {
                // Anywhere the rules allow. Without player lands, each start goes as far
                // from the ones before it as the samples allow.
                double best = -1.0;
                for (int attempt = 0, found = 0; attempt < 4000 && found < (haveOrigins ? 1 : 200); ++attempt) {
                    const auto [x, y] = randomCell(round);
                    if (!startFits(round, x, y) || !farEnough(x, y) || !cellAllowed(round, x, y, -1, -1)) {
                        continue;
                    }
                    ++found;
                    double nearest = 1e18;
                    for (const RmsStart& start : outcome_.starts) {
                        nearest = std::min(nearest, static_cast<double>((start.x - x) * (start.x - x) + (start.y - y) * (start.y - y)));
                    }
                    if (nearest > best) {
                        best = nearest;
                        spot = {x, y};
                    }
                }
                if (spot.first < 0 && origin) {
                    spot = nearestTo(outcome_.playerOrigins[slot].first * perNode_, outcome_.playerOrigins[slot].second * perNode_, round);
                }
            }
            if (spot.first < 0) {
                note(command != nullptr ? command->line : 0, "There was no room for player " + std::to_string(player) + "'s start position.");
                continue;
            }
            outcome_.starts.push_back(RmsStart{player, spot.first, spot.second});
            for (int dy = -kStartKeep; dy <= kStartKeep; ++dy) {
                for (int dx = -kStartKeep; dx <= kStartKeep; ++dx) {
                    const int x = spot.first + dx;
                    const int y = spot.second + dy;
                    if (x >= 0 && y >= 0 && x < cellsWide_ && y < cellsHigh_ && dx * dx + dy * dy <= kStartKeep * kStartKeep) {
                        occupied_[static_cast<std::size_t>(y) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(x)] = 1;
                    }
                }
            }
        }
    }

    // The paint under a feature cell, or -1 on a transition or off the map.
    int groundAt(int x, int y) const {
        if (x < 0 || y < 0 || x >= cellsWide_ || y >= cellsHigh_) {
            return -1;
        }
        const int lx = std::min(x / perNode_, latticeX_ - 1);
        const int ly = std::min(y / perNode_, latticeY_ - 1);
        return flat_[static_cast<std::size_t>(ly) * static_cast<std::size_t>(latticeX_) + static_cast<std::size_t>(lx)];
    }

    // True when the brush cell at lattice (lx, ly) is a gentle blend between dry
    // grounds: no water or lava at any corner, and no cliff between them. With
    // steep, cliffs and coasts count too: any cell with a dry corner.
    bool gentleBlend(int lx, int ly, bool steep = false) const {
        const RmsObjectSettings& objects = *options_.objects;
        int low = std::numeric_limits<int>::max();
        int high = std::numeric_limits<int>::min();
        for (int dy = 0; dy <= 1; ++dy) {
            for (int dx = 0; dx <= 1; ++dx) {
                const int corner = canvas_.at(lx + dx, ly + dy);
                const std::size_t slot = static_cast<std::size_t>(corner);
                if (corner < 0 || slot >= objects.terrainHeights.size()) {
                    return false;
                }
                if (slot < objects.terrainLiquid.size() && objects.terrainLiquid[slot]) {
                    if (steep) {
                        continue;
                    }
                    return false;
                }
                low = std::min(low, objects.terrainHeights[slot]);
                high = std::max(high, objects.terrainHeights[slot]);
            }
        }
        // With steep, at least one corner must be dry ground.
        return steep ? high >= low : high - low <= kBlendDoodadRise;
    }

    // The ground under a feature cell for doodads such as trees: as groundAt, but on a
    // gentle blend each quarter of the brush cell takes the ground of its own corner,
    // so a forest's trees reach out to its edge. With steep, cliff and coast cells count
    // too, but a quarter whose corner is water or lava has no ground.
    int doodadGroundAt(int x, int y, bool steep = false) const {
        const int flat = groundAt(x, y);
        if (flat >= 0 || x < 0 || y < 0 || x >= cellsWide_ || y >= cellsHigh_) {
            return flat;
        }
        const int lx = std::min(x / perNode_, latticeX_ - 1);
        const int ly = std::min(y / perNode_, latticeY_ - 1);
        if (!gentleBlend(lx, ly, steep)) {
            return -1;
        }
        const int qx = (x - lx * perNode_) * 2 >= perNode_ ? 1 : 0;
        const int qy = (y - ly * perNode_) * 2 >= perNode_ ? 1 : 0;
        const int ground = canvas_.at(lx + qx, ly + qy);
        const RmsObjectSettings& objects = *options_.objects;
        const std::size_t slot = static_cast<std::size_t>(ground);
        if (ground < 0 || (slot < objects.terrainLiquid.size() && objects.terrainLiquid[slot])) {
            return -1;
        }
        return ground;
    }

    // Doodad kinds may stand on blends; metal, vents, starts and named features keep
    // to one flat ground.
    static bool usesBlends(const ObjectChoice& choice) {
        return choice.byKind && choice.kind != DoodadKind::MetalNodes && choice.kind != DoodadKind::GeoVents;
    }

    // Trees may also stand on the dry side of cliff and coast tiles: once the ground is
    // built, a tree whose tile is not level is left out, so only the flat parts keep theirs.
    static bool usesCliffs(const ObjectChoice& choice) {
        return choice.byKind && (choice.kind == DoodadKind::Trees || choice.kind == DoodadKind::Palms);
    }

    // True when a coast, cliff or blend lies within distance feature cells.
    bool nearEdge(int x, int y, int distance) const {
        if (distance <= 0) {
            return false;
        }
        const int x0 = std::max(0, (x - distance) / perNode_);
        const int x1 = std::min(latticeX_ - 1, (x + distance) / perNode_);
        const int y0 = std::max(0, (y - distance) / perNode_);
        const int y1 = std::min(latticeY_ - 1, (y + distance) / perNode_);
        for (int ly = y0; ly <= y1; ++ly) {
            for (int lx = x0; lx <= x1; ++lx) {
                if (flat_[static_cast<std::size_t>(ly) * static_cast<std::size_t>(latticeX_) + static_cast<std::size_t>(lx)] >= 0) {
                    continue;
                }
                const int dx = std::max({lx * perNode_ - x, x - ((lx + 1) * perNode_ - 1), 0});
                const int dy = std::max({ly * perNode_ - y, y - ((ly + 1) * perNode_ - 1), 0});
                if (dx * dx + dy * dy <= distance * distance) {
                    return true;
                }
            }
        }
        return false;
    }

    struct Placed {
        int x = 0;
        int y = 0;
        int group = 0;
        int command = 0;
    };

    struct ObjectRules {
        int terrain = -1;
        bool hasLandId = false;
        int landId = 0;
        int minPlayers = -1;
        int maxPlayers = -1;
        int player = 0;  // set_place_for_every_player: the player this round is for
        int minGroup = 0;
        int tempMinGroup = 0;
        int edge = 0;
        int cliff = 0;
    };

    bool cellAllowed(const ObjectRules& rules, int x, int y, int group, int command) const {
        if (x < rules.edge || y < rules.edge || x >= cellsWide_ - rules.edge || y >= cellsHigh_ - rules.edge) {
            return false;
        }
        if (rules.cliff > 0 && nearEdge(x, y, rules.cliff)) {
            return false;
        }
        if (rules.hasLandId || (rules.player > 0 && rules.maxPlayers < 0)) {
            const int nx = std::clamp((x + perNode_ / 2) / perNode_, 0, width_ - 1);
            const int ny = std::clamp((y + perNode_ / 2) / perNode_, 0, height_ - 1);
            const int land = owner_.empty() ? -1 : owner_[static_cast<std::size_t>(index(nx, ny))];
            if (land < 0) {
                return false;
            }
            const Land& owned = lands_[static_cast<std::size_t>(land)];
            if (rules.hasLandId && (!owned.hasLandId || owned.landId != rules.landId)) {
                return false;
            }
            if (!rules.hasLandId && rules.player > 0 && owned.player != rules.player) {
                return false;
            }
        }
        if (rules.minPlayers >= 0 || rules.maxPlayers >= 0) {
            for (std::size_t p = 0; p < outcome_.playerOrigins.size(); ++p) {
                if (rules.player > 0 && static_cast<int>(p) + 1 != rules.player) {
                    continue;
                }
                const auto& [ox, oy] = outcome_.playerOrigins[p];
                if (ox < 0) {
                    continue;
                }
                const double dx = x - ox * perNode_;
                const double dy = y - oy * perNode_;
                const double distance = std::sqrt(dx * dx + dy * dy);
                if (rules.minPlayers >= 0 && distance < rules.minPlayers) {
                    return false;
                }
                if (rules.maxPlayers >= 0 && distance > rules.maxPlayers) {
                    return false;
                }
            }
        }
        const int spacing = std::max(rules.minGroup, rules.tempMinGroup);
        if (spacing > 0) {
            for (const Placed& other : placed_) {
                if (other.group == group && other.command == command) {
                    continue;
                }
                const int limit = other.command == command ? spacing : rules.minGroup;
                if (limit > 0 && (other.x - x) * (other.x - x) + (other.y - y) * (other.y - y) < limit * limit) {
                    return false;
                }
            }
        }
        return true;
    }

    // True when the feature fits with its top-left cell here.
    bool featureFits(const RmsFeature& feature, const ObjectChoice& choice, const ObjectRules& rules, int x, int y) const {
        const RmsObjectSettings& objects = *options_.objects;
        const int width = std::max(1, feature.footprintX);
        const int depth = std::max(1, feature.footprintZ);
        int ground = -2;
        for (int dy = 0; dy < depth; ++dy) {
            for (int dx = 0; dx < width; ++dx) {
                const int cx = x + dx;
                const int cy = y + dy;
                if (cx >= cellsWide_ || cy >= cellsHigh_ || occupied_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(cx)]) {
                    return false;
                }
                const int here = usesBlends(choice) ? doodadGroundAt(cx, cy, usesCliffs(choice)) : groundAt(cx, cy);
                if (here < 0 || (ground != -2 && here != ground)) {
                    return false;
                }
                ground = here;
            }
        }
        if (ground < 0) {
            return false;
        }
        const std::size_t slot = static_cast<std::size_t>(ground);
        const bool liquid = slot < objects.terrainLiquid.size() && objects.terrainLiquid[slot];
        if (rules.terrain >= 0) {
            if (ground != rules.terrain) {
                return false;
            }
        } else if (liquid) {
            return false;
        }
        // Sea-floor features only in water, everything else only on dry land.
        if (liquid != isUnderwater(feature)) {
            return false;
        }
        return !choice.followsLand || suitsGround(feature, choice, slot);
    }

    // True when the feature belongs on this ground's land and Base tile, or is a shared
    // feature standing in where the choice has none of its own.
    bool suitsGround(const RmsFeature& feature, const ObjectChoice& choice, std::size_t slot) const {
        const std::uint64_t key = (static_cast<std::uint64_t>(reinterpret_cast<std::uintptr_t>(&feature)) << 12) ^ static_cast<std::uint64_t>(slot);
        if (const auto found = choice.suits.find(key); found != choice.suits.end()) {
            return found->second;
        }
        const RmsObjectSettings& objects = *options_.objects;
        const std::string land = slot < objects.terrainLands.size() ? objects.terrainLands[slot] : std::string();
        const std::string base = slot < objects.terrainBases.size() ? objects.terrainBases[slot] : std::string();
        const bool own = featureSuitsLand(feature, choice.kind, land) && featureSuitsBase(feature, base);
        const bool fallback = sharedFallback(choice.kind) && allWorlds(feature) && !ownsGround(choice, slot);
        const bool suits = own || fallback;
        choice.suits.emplace(key, suits);
        return suits;
    }

    bool isUnderwater(const RmsFeature& feature) const {
        if (const auto found = underwater_.find(&feature); found != underwater_.end()) {
            return found->second;
        }
        const bool underwater = underwaterFeature(feature);
        underwater_.emplace(&feature, underwater);
        return underwater;
    }

    // True when the choice has a feature of its own for this terrain's land and Base tile.
    bool ownsGround(const ObjectChoice& choice, std::size_t slot) const {
        const RmsObjectSettings& objects = *options_.objects;
        const std::string land = slot < objects.terrainLands.size() ? objects.terrainLands[slot] : std::string();
        if (land.empty() || choice.lands.count(land) == 0) {
            return false;
        }
        const std::string base = slot < objects.terrainBases.size() ? objects.terrainBases[slot] : std::string();
        for (const RmsFeature* feature : choice.features) {
            if (featureSuitsLand(*feature, choice.kind, land) && featureSuitsBase(*feature, base)) {
                return true;
            }
        }
        return false;
    }

    // Places one feature at this cell if any of the choice fits there.
    bool placeAt(const ObjectChoice& choice, const ObjectRules& rules, int x, int y, int group, int command) {
        if (!cellAllowed(rules, x, y, group, command)) {
            return false;
        }
        // Every feature's top-left cell is this one, so none fits if it is taken, and no
        // tree fits where a tree may not stand.
        if (x < 0 || y < 0 || x >= cellsWide_ || y >= cellsHigh_
            || occupied_[static_cast<std::size_t>(y) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(x)]) {
            return false;
        }
        if (allSmallTrees(choice)) {
            const std::size_t tile = static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(tilesWide_) + static_cast<std::size_t>(x / 2);
            if ((tile < treesOnTile_.size() && treesOnTile_[tile] >= kMaxTreesPerTile) || treeBeside(x, y, 1, 1)) {
                return false;
            }
        }
        // Features are tried in a fresh random order each time, so every feature of a kind
        // is as likely as any other, whichever ones fail to fit here. The order is drawn one
        // feature at a time, a shuffle that stops at the first feature that fits.
        // A feature takes the ground of its top-left cell, so only those that suit it are tried.
        const int ground = usesBlends(choice) ? doodadGroundAt(x, y, usesCliffs(choice)) : groundAt(x, y);
        if (ground < 0) {
            return false;
        }
        const std::vector<const RmsFeature*>& suited = featuresFor(choice, ground);
        order_.assign(suited.begin(), suited.end());
        for (std::size_t next = 0; next < order_.size(); ++next) {
            const std::size_t pick = next + std::uniform_int_distribution<std::size_t>(0, order_.size() - 1 - next)(rng_);
            std::swap(order_[next], order_[pick]);
            const RmsFeature& feature = *order_[next];
            if (!featureFits(feature, choice, rules, x, y)) {
                continue;
            }
            // Trees are spread out: no more than kMaxTreesPerTile on a map tile.
            const bool tree = isTree(feature);
            const std::size_t tile = static_cast<std::size_t>(y / 2) * static_cast<std::size_t>(tilesWide_) + static_cast<std::size_t>(x / 2);
            if (tree && tile < treesOnTile_.size() && treesOnTile_[tile] >= kMaxTreesPerTile) {
                continue;
            }
            // ...and no two trees stand side by side: only corner to corner.
            if (tree && treeBeside(x, y, std::max(1, feature.footprintX), std::max(1, feature.footprintZ))) {
                continue;
            }
            if (tree && tile < treesOnTile_.size()) {
                ++treesOnTile_[tile];
            }
            for (int dy = 0; dy < std::max(1, feature.footprintZ); ++dy) {
                for (int dx = 0; dx < std::max(1, feature.footprintX); ++dx) {
                    const std::size_t cell = static_cast<std::size_t>(y + dy) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(x + dx);
                    occupied_[cell] = 1;
                    if (tree) {
                        treeCells_[cell] = 1;
                    }
                }
            }
            outcome_.features.push_back(RmsPlacedFeature{x, y, feature.name});
            placed_.push_back(Placed{x, y, group, command});
            return true;
        }
        return false;
    }

    // True when a tree stands on a cell sharing a side with the w x h footprint at (x, y).
    bool treeBeside(int x, int y, int w, int h) const {
        const auto treeAt = [&](int cx, int cy) {
            return cx >= 0 && cy >= 0 && cx < cellsWide_ && cy < cellsHigh_
                && treeCells_[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cellsWide_) + static_cast<std::size_t>(cx)] != 0;
        };
        for (int dx = 0; dx < w; ++dx) {
            if (treeAt(x + dx, y - 1) || treeAt(x + dx, y + h)) {
                return true;
            }
        }
        for (int dy = 0; dy < h; ++dy) {
            if (treeAt(x - 1, y + dy) || treeAt(x + w, y + dy)) {
                return true;
            }
        }
        return false;
    }

    // The features of the choice that may stand on this ground, worked out once for each ground.
    const std::vector<const RmsFeature*>& featuresFor(const ObjectChoice& choice, int ground) const {
        if (const auto found = choice.byGround.find(ground); found != choice.byGround.end()) {
            return found->second;
        }
        const RmsObjectSettings& objects = *options_.objects;
        const std::size_t slot = static_cast<std::size_t>(ground);
        const bool liquid = slot < objects.terrainLiquid.size() && objects.terrainLiquid[slot];
        std::vector<const RmsFeature*> suited;
        for (const RmsFeature* feature : choice.features) {
            if (liquid == isUnderwater(*feature) && (!choice.followsLand || suitsGround(*feature, choice, slot))) {
                suited.push_back(feature);
            }
        }
        return choice.byGround.emplace(ground, std::move(suited)).first->second;
    }

    // True when every feature of the choice is a tree one cell across.
    bool allSmallTrees(const ObjectChoice& choice) {
        if (choice.smallTrees < 0) {
            bool all = !choice.features.empty();
            for (const RmsFeature* feature : choice.features) {
                all = all && isTree(*feature) && std::max(1, feature->footprintX) == 1 && std::max(1, feature->footprintZ) == 1;
            }
            choice.smallTrees = all ? 1 : 0;
        }
        return choice.smallTrees == 1;
    }

    bool isTree(const RmsFeature& feature) {
        const auto found = trees_.find(&feature);
        if (found != trees_.end()) {
            return found->second;
        }
        const bool tree = featureIsDoodad(feature, DoodadKind::Trees);
        trees_.emplace(&feature, tree);
        return tree;
    }

    // A random cell, near one player's origin when the round is for that player.
    // The brush cells whose flat ground this choice can use: the right terrain, not a
    // liquid unless terrain_to_place_on asks for one, and a land the features suit.
    std::vector<int> usableGround(const ObjectChoice& choice, const ObjectRules& rules) const {
        const RmsObjectSettings& objects = *options_.objects;
        bool anyAllWorlds = false;
        for (const RmsFeature* feature : choice.features) {
            anyAllWorlds = anyAllWorlds || allWorlds(*feature);
        }
        const auto usable = [&](int ground) {
            const std::size_t slot = static_cast<std::size_t>(ground);
            const bool liquid = slot < objects.terrainLiquid.size() && objects.terrainLiquid[slot];
            if (rules.terrain >= 0 ? ground != rules.terrain : liquid) {
                return false;
            }
            if (choice.followsLand) {
                const std::string land = slot < objects.terrainLands.size() ? objects.terrainLands[slot] : std::string();
                const bool own = !land.empty() && ownsGround(choice, slot);
                const bool fallback = sharedFallback(choice.kind) && anyAllWorlds && !own;
                if (!own && !fallback) {
                    return false;
                }
            }
            return true;
        };
        const bool blends = usesBlends(choice);
        std::vector<int> cells;
        for (int ly = 0; ly < latticeY_; ++ly) {
            for (int lx = 0; lx < latticeX_; ++lx) {
                const int ground = flat_[static_cast<std::size_t>(ly) * static_cast<std::size_t>(latticeX_) + static_cast<std::size_t>(lx)];
                bool any = false;
                if (ground >= 0) {
                    any = usable(ground);
                } else if (blends && gentleBlend(lx, ly, usesCliffs(choice))) {
                    // A blend counts when any of its corners is ground the choice can use.
                    for (int corner = 0; corner < 4 && !any; ++corner) {
                        any = usable(canvas_.at(lx + corner % 2, ly + corner / 2));
                    }
                }
                if (any) {
                    cells.push_back(ly * latticeX_ + lx);
                }
            }
        }
        return cells;
    }

    // A random feature cell inside one of these brush cells.
    std::pair<int, int> cellIn(const std::vector<int>& ground) {
        const int picked = ground[std::uniform_int_distribution<std::size_t>(0, ground.size() - 1)(rng_)];
        const int x = (picked % latticeX_) * perNode_ + std::uniform_int_distribution<int>(0, perNode_ - 1)(rng_);
        const int y = (picked / latticeX_) * perNode_ + std::uniform_int_distribution<int>(0, perNode_ - 1)(rng_);
        return {std::min(x, cellsWide_ - 1), std::min(y, cellsHigh_ - 1)};
    }

    std::pair<int, int> randomCell(const ObjectRules& rules) {
        int x0 = 0;
        int y0 = 0;
        int x1 = cellsWide_ - 1;
        int y1 = cellsHigh_ - 1;
        if (rules.player > 0 && rules.maxPlayers >= 0 && static_cast<std::size_t>(rules.player - 1) < outcome_.playerOrigins.size()) {
            const auto& [ox, oy] = outcome_.playerOrigins[static_cast<std::size_t>(rules.player - 1)];
            if (ox >= 0) {
                x0 = std::max(0, ox * perNode_ - rules.maxPlayers);
                x1 = std::min(cellsWide_ - 1, ox * perNode_ + rules.maxPlayers);
                y0 = std::max(0, oy * perNode_ - rules.maxPlayers);
                y1 = std::min(cellsHigh_ - 1, oy * perNode_ + rules.maxPlayers);
            }
        }
        return {std::uniform_int_distribution<int>(x0, std::max(x0, x1))(rng_), std::uniform_int_distribution<int>(y0, std::max(y0, y1))(rng_)};
    }

    void createObject(const RmsCommand& command, int commandIndex) {
        const std::string& name = command.args[0].text;
        ObjectChoice choice;
        if (!resolveObject(name, choice)) {
            note(command.line, "\"" + name + "\" is not a feature or a doodad kind. Kinds are names such as TREES, ROCKS and LAVA_SPIRES.");
            return;
        }
        if (choice.features.empty()) {
            note(command.line, "No loaded feature is a " + name + ". Check that TA_Features_2013.ccx is next to Galaxy Editor.");
            return;
        }
        ObjectRules rules;
        if (const RmsAttribute* attribute = command.find("terrain_to_place_on"); attribute != nullptr && !attribute->args.empty()) {
            rules.terrain = terrainOf(attribute->args[0], attribute->line);
            if (rules.terrain < 0) {
                return;
            }
        }
        if (const RmsAttribute* attribute = command.find("place_on_specific_land_id"); attribute != nullptr && !attribute->args.empty()) {
            rules.hasLandId = true;
            rules.landId = attribute->args[0].integer();
        }
        rules.minPlayers = has(command, "min_distance_to_players") ? std::max(0, static_cast<int>(numberAt(command, "min_distance_to_players", 0.0))) : -1;
        rules.maxPlayers = has(command, "max_distance_to_players") ? std::max(0, static_cast<int>(numberAt(command, "max_distance_to_players", 0.0))) : -1;
        rules.minGroup = std::max(0, static_cast<int>(numberAt(command, "min_distance_group_placement", 0.0)));
        rules.tempMinGroup = std::max(0, static_cast<int>(numberAt(command, "temp_min_distance_group_placement", 0.0)));
        rules.edge = std::max(0, static_cast<int>(numberAt(command, "min_distance_to_map_edge", 0.0)));
        if (const RmsAttribute* attribute = command.find("avoid_cliff_zone")) {
            rules.cliff = attribute->args.empty() ? 4 : std::max(0, attribute->args[0].integer());
        }
        for (const char* skipped : {"layer_to_place_on", "avoid_forest_zone", "find_closest", "resource_delta", "second_object", "generate_for_first_land_only"}) {
            if (const RmsAttribute* attribute = command.find(skipped)) {
                note(attribute->line, std::string(skipped) + " is skipped: it has no meaning for Total Annihilation features yet.");
            }
        }

        const std::vector<int> ground = usableGround(choice, rules);
        if (ground.empty()) {
            note(command.line, "create_object " + name + " placed nothing: there is no ground on this map that it suits.");
            return;
        }
        // A round for one player near their origin searches around that origin; every
        // other search tries only the ground this choice can use.
        const auto pickCell = [&](const ObjectRules& round) {
            return round.player > 0 && round.maxPlayers >= 0 ? randomCell(round) : cellIn(ground);
        };
        const bool grouped = has(command, "number_of_groups");
        double objectsPerGroup = std::max(0.0, numberAt(command, "number_of_objects", 1.0));
        double groups = grouped ? std::max(0.0, numberAt(command, "number_of_groups", 1.0)) : 1.0;
        // Scaling multiplies the groups when there are groups, otherwise the objects.
        double& scaled = grouped ? groups : objectsPerGroup;
        if (has(command, "set_scaling_to_map_size")) {
            scaled *= static_cast<double>(cellsWide_) * static_cast<double>(cellsHigh_) / 10000.0;
        }
        if (has(command, "set_scaling_to_player_number")) {
            scaled *= std::max(1, options_.players);
        }
        const int groupCount = std::max(0, static_cast<int>(std::lround(groups)));
        const int perGroup = std::max(0, static_cast<int>(std::lround(objectsPerGroup)));
        const int variance = std::abs(static_cast<int>(numberAt(command, "group_variance", 0.0)));
        const int radius = std::max(0, static_cast<int>(numberAt(command, "group_placement_radius", 3.0)));
        const bool tight = lastIndexOf(command, "set_tight_grouping") > lastIndexOf(command, "set_loose_grouping");
        // Trees keep a cell apart, so a tight grove of them steps two cells at a time.
        const bool trees = std::any_of(choice.features.begin(), choice.features.end(), [&](const RmsFeature* feature) { return isTree(*feature); });
        const int reach = tight && trees ? 2 : 1;

        std::vector<int> rounds = {0};
        if (has(command, "set_place_for_every_player")) {
            rounds.clear();
            for (int player = 1; player <= options_.players; ++player) {
                rounds.push_back(player);
            }
        }
        int wanted = 0;
        int made = 0;
        int groupId = 0;
        // Progress through this command: one unit for each object, or for each group.
        const double units = static_cast<double>(rounds.size()) * std::max(1, grouped ? groupCount : perGroup);
        int unit = 0;
        const auto step = [&]() { report(RandomMapStage::Objects, commandStart_ + commandShare_ * std::min(1.0, ++unit / units)); };
        for (const int player : rounds) {
            rules.player = player;
            if (!grouped) {
                // Separate objects, each anywhere it fits.
                wanted += perGroup;
                for (int object = 0; object < perGroup; ++object) {
                    step();
                    bool found = false;
                    for (int attempt = 0; attempt < 400 && !found; ++attempt) {
                        const auto [x, y] = pickCell(rules);
                        found = placeAt(choice, rules, x, y, ++groupId, commandIndex);
                    }
                    if (!found) {
                        // No room for this one, so there is none for the rest either.
                        break;
                    }
                    ++made;
                }
                continue;
            }
            for (int group = 0; group < groupCount; ++group) {
                step();
                const int size = std::max(1, perGroup + (variance > 0 ? std::uniform_int_distribution<int>(-variance, variance)(rng_) : 0));
                wanted += size;
                ++groupId;
                int centerX = -1;
                int centerY = -1;
                for (int attempt = 0; attempt < 2000 && centerX < 0; ++attempt) {
                    const auto [x, y] = pickCell(rules);
                    if (placeAt(choice, rules, x, y, groupId, commandIndex)) {
                        centerX = x;
                        centerY = y;
                    }
                }
                if (centerX < 0) {
                    // No room for this group, so there is none for the ones after it.
                    for (int rest = group + 1; rest < groupCount; ++rest) {
                        wanted += std::max(1, perGroup);
                    }
                    break;
                }
                int inGroup = 1;
                std::vector<std::pair<int, int>> members = {{centerX, centerY}};
                int attempts = 0;
                while (inGroup < size && attempts < size * 60) {
                    ++attempts;
                    int x = 0;
                    int y = 0;
                    if (tight) {
                        // Tight groups grow from a member to the cell beside it. Trees reach two
                        // cells: one cell leaves only the diagonals free, and the grove would grow
                        // as a thin diagonal line.
                        const auto& from = members[std::uniform_int_distribution<std::size_t>(0, members.size() - 1)(rng_)];
                        x = from.first + std::uniform_int_distribution<int>(-reach, reach)(rng_);
                        y = from.second + std::uniform_int_distribution<int>(-reach, reach)(rng_);
                    } else {
                        x = centerX + std::uniform_int_distribution<int>(-radius, radius)(rng_);
                        y = centerY + std::uniform_int_distribution<int>(-radius, radius)(rng_);
                        if ((x - centerX) * (x - centerX) + (y - centerY) * (y - centerY) > radius * radius) {
                            continue;
                        }
                    }
                    if (placeAt(choice, rules, x, y, groupId, commandIndex)) {
                        members.emplace_back(x, y);
                        ++inGroup;
                    }
                }
                made += inGroup;
            }
        }
        if (made < wanted) {
            note(command.line, "create_object " + name + " placed " + std::to_string(made) + " of " + std::to_string(wanted)
                                   + (made == 0 ? ": nothing it can go on was found." : ": there was no more room."));
        }
    }

    int cellsWide_ = 1;
    int cellsHigh_ = 1;
    int perNode_ = 32;
    int latticeX_ = 1;
    int latticeY_ = 1;
    std::vector<int> flat_;
    std::vector<char> occupied_;
    // Trees standing on each map tile, two feature cells square, tilesWide_ to a row.
    std::vector<unsigned char> treesOnTile_;
    // 1 on each feature cell a tree stands on.
    std::vector<unsigned char> treeCells_;
    int tilesWide_ = 0;
    std::unordered_map<const RmsFeature*, bool> trees_;
    mutable std::unordered_map<const RmsFeature*, bool> underwater_;
    // Where the create_object under way starts in the objects step, and its share of it.
    double commandStart_ = 0.0;
    double commandShare_ = 1.0;
    // placeAt's order of features to try, kept to save an allocation for every cell.
    std::vector<const RmsFeature*> order_;
    std::vector<Placed> placed_;

    const RmsScript& script_;
    RmsCanvas& canvas_;
    const RmsTerrainNames& names_;
    const RandomMapOptions& options_;
    std::mt19937& rng_;
    RandomMapOutcome& outcome_;
    int width_ = 1;
    int height_ = 1;
    int count_ = 1;
    bool directPlacement_ = false;
    int behaviorVersion_ = 0;
    std::vector<Land> lands_;
    std::vector<int> owner_;
    // Nodes <CLIFF_GENERATION> raised, which connections go round.
    std::vector<char> cliff_;
    std::vector<std::pair<int, int>> playerOrigins_;
};

bool isTransitionFolder(std::string_view world) {
    const std::string name = lower(world);
    return name == "clifftiles" || name == "coasttiles" || name == "shallowcoasttiles" || name == "shallowdeeptiles"
        || name == "coastdsl" || name == "coastalcliff" || name == "blendtiles";
}

bool sharedCategory(std::string_view category) {
    const std::string name = lower(category);
    return name == "shared" || name == "3-way";
}

// GreenWorld's rivers are filed under GreenLand.
bool categoryIsWorld(std::string_view category, std::string_view world) {
    const std::string left = lower(category);
    const std::string right = lower(world);
    return left == right || (right == "greenworld" && left == "greenland");
}

}  // namespace

// ------------------------------------------------------------------- doodads

const std::vector<DoodadKindInfo>& doodadKinds() {
    static const std::vector<DoodadKindInfo> kinds = {
        {DoodadKind::Trees, "TREES"},
        {DoodadKind::Rocks, "ROCKS"},
        {DoodadKind::Plants, "PLANTS"},
        {DoodadKind::Shrubs, "SHRUBS"},
        {DoodadKind::Foliage, "FOLIAGE"},
        {DoodadKind::Fronds, "FRONDS"},
        {DoodadKind::Palms, "PALMS"},
        {DoodadKind::Vines, "VINES"},
        {DoodadKind::IceChunks, "ICECHUNKS"},
        {DoodadKind::LavaNodes, "LAVA_NODES"},
        {DoodadKind::LavaSpires, "LAVA_SPIRES"},
        {DoodadKind::MetalTowers, "METAL_TOWERS"},
        {DoodadKind::Pipes, "PIPES"},
        {DoodadKind::CrystalSpires, "CRYSTAL_SPIRES"},
        {DoodadKind::Spires, "SPIRES"},
        {DoodadKind::MetalNodes, "METAL_NODES"},
        {DoodadKind::GeoVents, "GEO_VENTS"},
    };
    return kinds;
}

namespace {

bool contains(const std::string& text, std::string_view part) {
    return text.find(part) != std::string::npos;
}

bool endsWith(const std::string& text, std::string_view part) {
    return text.size() >= part.size() && text.compare(text.size() - part.size(), part.size(), part) == 0;
}

// Features that are never part of a kind: marks on the ground, wreckage, markers
// for map makers, and the burnt and dead copies of living plants.
bool leftOutOfKinds(const RmsFeature& feature) {
    const std::string category = lower(feature.category);
    for (const char* skip : {"scar", "smudge", "corpse", "heap", "do not fix", "discontinued", "unplaceable", "missing",
                             "invisible", "decal", "duplicate", "wreck"}) {
        if (contains(category, skip)) {
            return true;
        }
    }
    const std::string name = lower(feature.name);
    // Evergreen files its smudges, such as smudge02_01, with its snow trees.
    if (name.compare(0, 6, "smudge") == 0) {
        return true;
    }
    // Ground Zero's CrysBIGvar and Crysvar crystals are never used.
    if (name.compare(0, 10, "crysbigvar") == 0 || name.compare(0, 7, "crysvar") == 0) {
        return true;
    }
    return endsWith(name, "crisp") || endsWith(name, "dead") || contains(name, "dead0");
}

// Metal deposits: features a player mines, such as RockMetal1 or GreenAquaOre1. Some
// archives file them under rocks, so the name decides as well as the category.
bool metalDeposit(const RmsFeature& feature) {
    if (contains(lower(feature.category), "metal")) {
        return true;
    }
    const std::string name = lower(feature.name);
    if (contains(name, "metal")) {
        return true;
    }
    // "ore" at the end of the name or before a number: GreenAquaOre1, but not Shoreline.
    for (std::size_t at = name.find("ore"); at != std::string::npos; at = name.find("ore", at + 1)) {
        const std::size_t after = at + 3;
        if (after == name.size() || std::isdigit(static_cast<unsigned char>(name[after]))) {
            return true;
        }
    }
    return false;
}

}  // namespace

bool featureIsDoodad(const RmsFeature& feature, DoodadKind kind) {
    if (leftOutOfKinds(feature)) {
        return false;
    }
    // Metal towers and pipes are buildings, matched by name. Every other kind leaves
    // out metal deposits, which are resources rather than doodads.
    if (kind != DoodadKind::MetalTowers && kind != DoodadKind::Pipes && kind != DoodadKind::MetalNodes && metalDeposit(feature)) {
        return false;
    }
    const std::string category = lower(feature.category);
    const std::string name = lower(feature.name);
    const std::string world = lower(feature.world);
    switch (kind) {
    case DoodadKind::Trees:
        return contains(category, "tree") || name.compare(0, 4, "palm") == 0;
    case DoodadKind::Rocks:
        return contains(category, "rock") || (contains(name, "rock") && !contains(name, "spire"));
    case DoodadKind::Plants:
        return contains(category, "plant") || contains(name, "plant");
    case DoodadKind::Shrubs:
        return contains(category, "shrub") || contains(name, "shrub") || contains(name, "bush");
    case DoodadKind::Foliage:
        return contains(category, "foliage");
    case DoodadKind::Palms:
        return name.compare(0, 4, "palm") == 0;
    case DoodadKind::Fronds:
        return contains(name, "frond");
    case DoodadKind::Vines:
        return contains(name, "vine");
    case DoodadKind::IceChunks:
        return contains(name, "icechunk");
    case DoodadKind::LavaNodes:
        return contains(name, "lavanode") || (contains(world, "lava") && category == "nodes");
    case DoodadKind::LavaSpires:
        return contains(name, "lavaspire") || (contains(world, "lava") && contains(category, "spire"));
    case DoodadKind::MetalTowers:
        return contains(name, "metaltower");
    case DoodadKind::Pipes:
        return name.compare(0, 4, "pipe") == 0 || category == "pipes";
    case DoodadKind::CrystalSpires:
        return name.compare(0, 4, "crys") == 0 || contains(category, "crystal");
    case DoodadKind::Spires:
        return contains(category, "spire") || contains(name, "spire");
    case DoodadKind::MetalNodes: {
        if (feature.metal <= 0 || !metalDeposit(feature)) {
            return false;
        }
        // The third-party gwmetal, TAVMetal and GZMetal deposits look out of place, so they
        // are never used. On GreenWorld's dry land only Cavedog's own RockMetal deposits are.
        if (name.compare(0, 7, "gwmetal") == 0 || name.compare(0, 8, "tavmetal") == 0 || name.compare(0, 7, "gzmetal") == 0) {
            return false;
        }
        if (doodadLand(feature, kind) == "Green" && !underwaterFeature(feature)) {
            return name.compare(0, 9, "rockmetal") == 0;
        }
        // On Desert dry land only DryMetal01 to 03, and no Death Valley or Clay Desert
        // deposits at all, even under water.
        if (doodadLand(feature, kind) == "Desert") {
            const std::string world = lower(feature.world);
            if (world == "deathvalley" || world == "claydesert") {
                return false;
            }
            return underwaterFeature(feature) || name.compare(0, 9, "drymetal0") == 0;
        }
        return true;
    }
    case DoodadKind::GeoVents:
        if (!feature.geothermal) {
            return false;
        }
        // Desert ground takes only Desert's own steamvents. The Death Valley vents, DVVent01
        // to 03, are bright orange-red for that third-party tileset and look wrong on Desert.
        if (doodadLand(feature, kind) == "Desert") {
            return world == "desert" && category == "steamvents";
        }
        return true;
    }
    return false;
}

bool underwaterFeature(const RmsFeature& feature) {
    const std::string name = lower(feature.name);
    const std::string category = lower(feature.category);
    for (const char* mark : {"aqua", "underwater", "seaweed", "seagrass", "coral", "kelp", "water"}) {
        if (contains(name, mark)) {
            return true;
        }
    }
    if (name.compare(0, 2, "uw") == 0) {
        return true;
    }
    for (const char* mark : {"aqua", "water", "coral", "kelp", "seagrass"}) {
        if (contains(category, mark)) {
            return true;
        }
    }
    return false;
}

namespace {

// Metal deposits drawn to match particular Base tiles, by the start of their names.
struct FeatureHome {
    const char* prefix;
    const char* land;
    std::vector<const char*> bases;
};

const FeatureHome* featureHome(const RmsFeature& feature) {
    static const std::vector<FeatureHome> homes = {
        {"archmetalb", "Archi", {"base-194"}},
        // ArchMetalC is filed under Archipelago but drawn for Lava.
        {"archmetalc", "Lava", {"base-86", "base-190"}},
        {"archmetal", "Archi", {"base-78"}},
    };
    const std::string name = lower(feature.name);
    for (const FeatureHome& home : homes) {
        const std::size_t length = std::string_view(home.prefix).size();
        if (name.compare(0, length, home.prefix) == 0) {
            // ArchMetal1 to 3, but not some longer name such as ArchMetalD1.
            if (std::string_view(home.prefix) == "archmetal" && name.size() > length && !std::isdigit(static_cast<unsigned char>(name[length]))) {
                return nullptr;
            }
            return &home;
        }
    }
    return nullptr;
}

}  // namespace

bool featureSuitsBase(const RmsFeature& feature, std::string_view base) {
    const FeatureHome* home = featureHome(feature);
    if (home == nullptr) {
        return true;
    }
    const std::string wanted = lower(base);
    return std::any_of(home->bases.begin(), home->bases.end(), [&](const char* each) { return wanted == each; });
}

std::string doodadLand(const RmsFeature& feature, DoodadKind kind) {
    if (const FeatureHome* home = featureHome(feature)) {
        return home->land;
    }
    switch (kind) {
    case DoodadKind::IceChunks:
        return "Ice";
    case DoodadKind::LavaNodes:
    case DoodadKind::LavaSpires:
        return "Lava";
    case DoodadKind::MetalTowers:
    case DoodadKind::Pipes:
        return "Metal";
    case DoodadKind::CrystalSpires:
        return "Crystal";
    default:
        break;
    }
    const std::string world = lower(feature.world);
    const std::string name = lower(feature.name);
    if (world == "greenworld" || world == "green" || world == "greenland") {
        return "Green";
    }
    if (world == "lava" || world == "lavaworld") {
        return "Lava";
    }
    if (world == "mars") {
        return "Mars";
    }
    // Ice has no trees of its own, so its trees are Evergreen's snow trees. Evergreen's
    // other features are a third-party set of their own, placed by name or as
    // EVERGREEN_ kinds.
    if (world == "ice" || (world == "evergreen" && kind == DoodadKind::Trees && name.compare(0, 8, "snowtree") == 0)) {
        return "Ice";
    }
    if (world == "metal" || world == "metalworld") {
        return "Metal";
    }
    if (world == "desert" || world == "wetdesert" || world == "deathvalley" || world == "claydesert") {
        return "Desert";
    }
    if (world == "archipelago") {
        return "Archi";
    }
    if (world == "lush") {
        return "Lush";
    }
    if (world == "acid") {
        return "Acid";
    }
    if (world == "crystal") {
        return "Crystal";
    }
    return {};
}

std::vector<std::string> featureLands(const RmsFeature& feature, DoodadKind kind) {
    std::vector<std::string> lands;
    const std::string land = doodadLand(feature, kind);
    if (land.empty()) {
        return lands;
    }
    lands.push_back(land);
    if (kind == DoodadKind::Rocks && land == "Lush") {
        lands.push_back("Acid");
    }
    if ((kind == DoodadKind::Trees || kind == DoodadKind::Palms) && land == "Archi") {
        lands.push_back("Desert");
    }
    return lands;
}

bool featureSuitsLand(const RmsFeature& feature, DoodadKind kind, const std::string& land) {
    if (land.empty()) {
        return false;
    }
    const std::vector<std::string> lands = featureLands(feature, kind);
    return std::find(lands.begin(), lands.end(), land) != lands.end();
}

int footprintRise(const std::vector<std::uint8_t>& heights, int width, int height, int x, int y, int w, int h) {
    int low = 255;
    int high = 0;
    for (int cy = std::max(0, y); cy <= std::min(height - 1, y + h); ++cy) {
        for (int cx = std::max(0, x); cx <= std::min(width - 1, x + w); ++cx) {
            const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
            if (index < heights.size()) {
                low = std::min<int>(low, heights[index]);
                high = std::max<int>(high, heights[index]);
            }
        }
    }
    return high >= low ? high - low : 0;
}

bool tileIsLevel(const std::vector<std::uint8_t>& heights, int width, int height, int x, int y) {
    const int left = x & ~1;
    const int top = y & ~1;
    int first = -1;
    for (int cy = top; cy < top + 2 && cy < height; ++cy) {
        for (int cx = left; cx < left + 2 && cx < width; ++cx) {
            if (cx < 0 || cy < 0) {
                continue;
            }
            const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
            if (index >= heights.size()) {
                continue;
            }
            if (first < 0) {
                first = heights[index];
            } else if (heights[index] != first) {
                return false;
            }
        }
    }
    return true;
}

bool findFlatSpot(const std::vector<std::uint8_t>& heights, const std::vector<std::uint8_t>& occupied, int width, int height, int w, int h,
                  int x, int y, int level, int maxRise, int radius, int& outX, int& outY) {
    w = std::max(1, w);
    h = std::max(1, h);
    const auto fits = [&](int px, int py) {
        if (px < 0 || py < 0 || px + w > width || py + h > height) {
            return false;
        }
        for (int cy = py; cy < py + h; ++cy) {
            for (int cx = px; cx < px + w; ++cx) {
                const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
                if (index < occupied.size() && occupied[index] != 0) {
                    return false;
                }
            }
        }
        int low = 255;
        int high = 0;
        for (int cy = py; cy <= std::min(height - 1, py + h); ++cy) {
            for (int cx = px; cx <= std::min(width - 1, px + w); ++cx) {
                const std::size_t index = static_cast<std::size_t>(cy) * static_cast<std::size_t>(width) + static_cast<std::size_t>(cx);
                if (index >= heights.size()) {
                    return false;
                }
                low = std::min<int>(low, heights[index]);
                high = std::max<int>(high, heights[index]);
            }
        }
        return high - low <= maxRise && std::abs(low - level) <= maxRise && std::abs(high - level) <= maxRise;
    };
    for (int ring = 1; ring <= radius; ++ring) {
        // The ring's cells, nearest to the middle of each side first.
        int bestX = 0;
        int bestY = 0;
        long bestDistance = -1;
        for (int dy = -ring; dy <= ring; ++dy) {
            for (int dx = -ring; dx <= ring; ++dx) {
                if (std::max(std::abs(dx), std::abs(dy)) != ring) {
                    continue;
                }
                const long distance = static_cast<long>(dx) * dx + static_cast<long>(dy) * dy;
                if ((bestDistance < 0 || distance < bestDistance) && fits(x + dx, y + dy)) {
                    bestDistance = distance;
                    bestX = x + dx;
                    bestY = y + dy;
                }
            }
        }
        if (bestDistance >= 0) {
            outX = bestX;
            outY = bestY;
            return true;
        }
    }
    return false;
}

// ------------------------------------------------------------------- canvases

PlainCanvas::PlainCanvas(int width, int height, int terrain)
    : width_(std::max(1, width)), height_(std::max(1, height)), nodes_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), terrain) {}

int PlainCanvas::at(int x, int y) const {
    return nodes_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

void PlainCanvas::fill(int terrain) {
    std::fill(nodes_.begin(), nodes_.end(), terrain);
}

void PlainCanvas::paint(int x, int y, int terrain) {
    if (x >= 0 && y >= 0 && x < width_ && y < height_) {
        nodes_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)] = terrain;
    }
}

BrushCanvas::BrushCanvas(int width, int height, BrushGraph graph)
    : width_(std::max(1, width)), height_(std::max(1, height)), graph_(std::move(graph)),
      nodes_(static_cast<std::size_t>(width_) * static_cast<std::size_t>(height_), 0) {}

int BrushCanvas::at(int x, int y) const {
    return nodes_[static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x)];
}

void BrushCanvas::fill(int terrain) {
    std::fill(nodes_.begin(), nodes_.end(), std::clamp(terrain, 0, std::max(0, graph_.size() - 1)));
}

void BrushCanvas::paint(int x, int y, int terrain) {
    if (x < 0 || y < 0 || x >= width_ || y >= height_ || terrain < 0 || terrain >= graph_.size()) {
        return;
    }
    const std::size_t slot = static_cast<std::size_t>(y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(x);
    if (nodes_[slot] == terrain) {
        return;
    }
    const int cellsX = width_ - 1;
    const int cellsY = height_ - 1;
    if (cellsX <= 0 || cellsY <= 0) {
        nodes_[slot] = terrain;
        return;
    }
    const BrushPlan plan = planBrush(cellsX, cellsY, x, y, 0, terrain, graph_, [this](int nodeX, int nodeY) {
        return nodes_[static_cast<std::size_t>(nodeY) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(nodeX)];
    });
    for (const BrushNode& node : plan.nodes) {
        if (node.x >= 0 && node.y >= 0 && node.x < width_ && node.y < height_) {
            nodes_[static_cast<std::size_t>(node.y) * static_cast<std::size_t>(width_) + static_cast<std::size_t>(node.x)] = node.level;
        }
    }
}

std::array<int, 4> BrushCanvas::cellCorners(int cellX, int cellY) const {
    return {at(cellX, cellY), at(cellX + 1, cellY), at(cellX, cellY + 1), at(cellX + 1, cellY + 1)};
}

int BrushCanvas::repair(const std::function<bool(const std::array<int, 4>&)>& hasPicture, int passes) {
    const int cellsX = width_ - 1;
    const int cellsY = height_ - 1;
    int bad = 0;
    for (int pass = 0; pass <= passes; ++pass) {
        bad = 0;
        for (int cy = 0; cy < cellsY; ++cy) {
            for (int cx = 0; cx < cellsX; ++cx) {
                const std::array<int, 4> corners = cellCorners(cx, cy);
                if ((corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3]) || hasPicture(corners)) {
                    continue;
                }
                ++bad;
                if (pass == passes) {
                    continue;
                }
                // The paint most corners share; on a tie, the top-left corner's if it is one of them.
                int target = corners[0];
                int bestCount = 0;
                for (int k = 0; k < 4; ++k) {
                    const int count = static_cast<int>(std::count(corners.begin(), corners.end(), corners[static_cast<std::size_t>(k)]));
                    if (count > bestCount) {
                        bestCount = count;
                        target = corners[static_cast<std::size_t>(k)];
                    }
                }
                for (int k = 0; k < 4; ++k) {
                    if (corners[static_cast<std::size_t>(k)] != target) {
                        paint(cx + (k & 1), cy + (k >> 1), target);
                    }
                }
            }
        }
        if (bad == 0) {
            return 0;
        }
    }
    return bad;
}

// ------------------------------------------------------------------- running

RandomMapOutcome runRandomMapScript(const RmsScript& script, RmsCanvas& canvas, const RmsTerrainNames& names,
                                    const RandomMapOptions& options, std::mt19937& rng) {
    RandomMapOutcome outcome;
    Generator generator(script, canvas, names, options, rng, outcome);
    generator.run();
    std::stable_sort(outcome.messages.begin(), outcome.messages.end(), [](const RmsMessage& left, const RmsMessage& right) {
        return left.line < right.line;
    });
    return outcome;
}

std::vector<std::string> rmsTerrainNames(const RmsScript& script, const std::string& fallback) {
    std::vector<std::string> names;
    const auto add = [&](const std::string& name) {
        if (!name.empty() && std::find(names.begin(), names.end(), name) == names.end()) {
            names.push_back(name);
        }
    };
    bool sawBase = false;
    bool usesFallback = false;
    for (const RmsCommand& command : script.commands) {
        if (command.section == "LAND_GENERATION" && command.name == "base_terrain" && !command.args.empty()) {
            add(command.args[0].text);
            sawBase = true;
        } else if (command.name == "create_land" || command.name == "create_player_lands") {
            const RmsAttribute* type = command.find("terrain_type");
            if (type != nullptr && !type->args.empty()) {
                add(type->args[0].text);
            } else {
                usesFallback = true;
            }
        } else if (command.name == "create_terrain") {
            if (!command.args.empty()) {
                add(command.args[0].text);
            }
            for (const char* attribute : {"base_terrain", "spacing_to_specific_terrain"}) {
                for (const RmsAttribute* found : command.findAll(attribute)) {
                    if (!found->args.empty()) {
                        add(found->args[0].text);
                    }
                }
            }
        } else if (command.name == "create_object") {
            if (const RmsAttribute* ground = command.find("terrain_to_place_on"); ground != nullptr && !ground->args.empty()) {
                add(ground->args[0].text);
            }
        } else if (command.name.compare(0, 15, "create_connect_") == 0) {
            for (const RmsAttribute& attribute : command.attributes) {
                if (attribute.name == "replace_terrain" && attribute.args.size() >= 2) {
                    add(attribute.args[0].text);
                    add(attribute.args[1].text);
                } else if ((attribute.name == "terrain_cost" || attribute.name == "terrain_size" || attribute.name == "default_terrain_replacement")
                           && !attribute.args.empty()) {
                    add(attribute.args[0].text);
                }
            }
        }
    }
    if (!sawBase || usesFallback) {
        add(fallback);
    }
    return names;
}

std::vector<std::string> rmsConditions(int players, int gameWidth, int gameHeight) {
    std::vector<std::string> conditions = {"RANDOM_MAP", "DE_AVAILABLE", "GALAXY_EDITOR", "0_TEAM_GAME"};
    conditions.push_back(std::to_string(std::clamp(players, 1, 10)) + "_PLAYER_GAME");
    // Game sizes stand in for the game's map sizes: 8 x 8 and under is tiny, up to
    // 10 small, 12 medium, 16 normal (LARGE_MAP), 20 large (HUGE_MAP), 24 huge
    // (GIGANTIC_MAP), 32 giant, and anything bigger ludicrous.
    const int size = std::max(gameWidth, gameHeight);
    if (size <= 8) {
        conditions.insert(conditions.end(), {"TINY_MAP", "MAPSIZE_TINY"});
    } else if (size <= 10) {
        conditions.insert(conditions.end(), {"SMALL_MAP", "MAPSIZE_SMALL"});
    } else if (size <= 12) {
        conditions.insert(conditions.end(), {"MEDIUM_MAP", "MAPSIZE_MEDIUM"});
    } else if (size <= 16) {
        conditions.insert(conditions.end(), {"LARGE_MAP", "MAPSIZE_NORMAL"});
    } else if (size <= 20) {
        conditions.insert(conditions.end(), {"HUGE_MAP", "MAPSIZE_LARGE"});
    } else if (size <= 24) {
        conditions.insert(conditions.end(), {"GIGANTIC_MAP", "MAPSIZE_HUGE"});
    } else if (size <= 32) {
        conditions.push_back("MAPSIZE_GIANT");
    } else {
        conditions.insert(conditions.end(), {"LUDIKRIS_MAP", "MAPSIZE_LUDICROUS"});
    }
    return conditions;
}

// ------------------------------------------------------------------- tileset

RandomMapTiles::RandomMapTiles(const std::vector<TilesetSection>& sections) {
    for (const TilesetSection& section : sections) {
        const std::string world = lower(section.world);
        if (world == "basetiles" && lower(section.name).compare(0, 4, "base") == 0) {
            const int height = trailingHeight(section.name);
            if (height < 0) {
                continue;
            }
            RandomMapPaint paint;
            paint.world = section.category;
            paint.name = section.name;
            paint.height = height;
            paint.terrain = upper(section.category) + "_" + upper(section.name);
            paint.sectionPath = section.archivePath;
            paints_.push_back(std::move(paint));
        } else if (world == "basetilebrushstrokes") {
            strokes_.push_back(RandomMapSection{section.name, section.category, section.archivePath});
        } else if (isTransitionFolder(world)) {
            pieces_.push_back(RandomMapSection{section.name, section.category, section.archivePath});
            if (world == "coasttiles") {
                // Coast-Lava-T names the sea Lava. Coast-Land and Coast-Water are the usual names.
                const std::string name = lower(section.name);
                if (name.compare(0, 6, "coast-") == 0 && name.compare(0, 11, "coast-land-") != 0 && name.compare(0, 12, "coast-water-") != 0) {
                    const std::size_t dash = section.name.find('-', 6);
                    if (dash != std::string::npos && dash > 6) {
                        bool known = false;
                        for (const auto& entry : liquids_) {
                            known = known || lower(entry.first) == lower(section.category);
                        }
                        if (!known) {
                            liquids_.emplace_back(section.category, section.name.substr(6, dash - 6));
                        }
                    }
                }
            }
        }
    }
    std::stable_sort(paints_.begin(), paints_.end(), [](const RandomMapPaint& left, const RandomMapPaint& right) {
        const std::string a = lower(left.world);
        const std::string b = lower(right.world);
        return a != b ? a < b : left.height < right.height;
    });
}

int RandomMapTiles::findPaint(std::string_view terrain) const {
    const std::string wanted = upper(terrain);
    for (std::size_t i = 0; i < paints_.size(); ++i) {
        if (paints_[i].terrain == wanted) {
            return static_cast<int>(i);
        }
    }
    return -1;
}

std::vector<int> RandomMapTiles::levelsFor(const std::vector<int>& paints) const {
    std::set<std::string> worlds;
    for (const int paint : paints) {
        if (paint >= 0 && static_cast<std::size_t>(paint) < paints_.size()) {
            worlds.insert(lower(paints_[static_cast<std::size_t>(paint)].world));
        }
    }
    std::vector<int> levels;
    for (std::size_t i = 0; i < paints_.size(); ++i) {
        if (worlds.count(lower(paints_[i].world)) != 0) {
            levels.push_back(static_cast<int>(i));
        }
    }
    // Lowest height first, as the brush stores its levels. Lands that share a
    // height keep the world order.
    std::stable_sort(levels.begin(), levels.end(), [&](int left, int right) {
        return paints_[static_cast<std::size_t>(left)].height < paints_[static_cast<std::size_t>(right)].height;
    });
    return levels;
}

BrushGraph RandomMapTiles::graphFor(const std::vector<int>& levels) const {
    std::vector<int> heights;
    std::vector<std::string> worlds;
    for (const int level : levels) {
        const RandomMapPaint& paint = paints_[static_cast<std::size_t>(level)];
        heights.push_back(paint.height);
        worlds.push_back(blendWorldName(paint.world));
    }
    return BrushGraph(heights, worlds, true);
}

std::string RandomMapTiles::liquidOf(std::string_view world) const {
    for (const auto& [category, liquid] : liquids_) {
        if (lower(category) == lower(world)) {
            return liquid;
        }
    }
    return "Water";
}

std::vector<const RandomMapSection*> RandomMapTiles::strokes(const RandomMapPaint& paint, int tiles) const {
    std::vector<const RandomMapSection*> found;
    for (const RandomMapSection& stroke : strokes_) {
        if (categoryIsWorld(stroke.category, paint.world) && brushStrokeVariation(stroke.name, paint.name, tiles) >= 0) {
            found.push_back(&stroke);
        }
    }
    return found;
}

std::string RandomMapTiles::pieceName(const BrushGraph& graph, const std::vector<int>& levels, const std::array<int, 4>& corners) const {
    std::string world;
    bool oneWorld = true;
    for (const int corner : corners) {
        const std::string here = lower(paints_[static_cast<std::size_t>(levels[static_cast<std::size_t>(corner)])].world);
        if (world.empty()) {
            world = here;
        } else if (world != here) {
            oneWorld = false;
        }
    }
    return brushPieceName(graph, corners, oneWorld ? liquidOf(world) : std::string("Water"));
}

std::vector<const RandomMapSection*> RandomMapTiles::pieces(const BrushGraph& graph, const std::vector<int>& levels,
                                                            const std::array<int, 4>& corners) const {
    std::vector<const RandomMapSection*> found;
    const std::string piece = pieceName(graph, levels, corners);
    if (piece.empty()) {
        return found;
    }
    // A cell inside one world uses that world's pieces. Blends between worlds
    // live in BlendTiles/Shared and BlendTiles/3-Way.
    std::string world;
    bool oneWorld = true;
    for (const int corner : corners) {
        const std::string here = lower(paints_[static_cast<std::size_t>(levels[static_cast<std::size_t>(corner)])].world);
        if (world.empty()) {
            world = here;
        } else if (world != here) {
            oneWorld = false;
        }
    }
    for (const RandomMapSection& section : pieces_) {
        const bool usable = sharedCategory(section.category) || (oneWorld && categoryIsWorld(section.category, world));
        if (usable && transitionVariation(section.name, piece) >= 0) {
            found.push_back(&section);
        }
    }
    return found;
}

RandomMapStampPlan planRandomMapStamps(const RandomMapTiles& tiles, const std::vector<int>& levels,
                                       const BrushCanvas& canvas, std::mt19937& rng) {
    RandomMapStampPlan plan;
    const int cellsX = canvas.width() - 1;
    const int cellsY = canvas.height() - 1;
    if (cellsX <= 0 || cellsY <= 0 || levels.empty()) {
        return plan;
    }
    const auto pick = [&](const std::vector<const RandomMapSection*>& choices) {
        return choices[std::uniform_int_distribution<std::size_t>(0, choices.size() - 1)(rng)];
    };
    const auto noteMissing = [&](const std::string& what) {
        if (std::find(plan.missing.begin(), plan.missing.end(), what) == plan.missing.end()) {
            plan.missing.push_back(what);
        }
    };
    const auto flatLevel = [&](int cx, int cy) {
        const std::array<int, 4> corners = canvas.cellCorners(cx, cy);
        return corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3] ? corners[0] : -1;
    };
    const auto paintOf = [&](int level) -> const RandomMapPaint& {
        return tiles.paints()[static_cast<std::size_t>(levels[static_cast<std::size_t>(level)])];
    };

    std::vector<char> covered(static_cast<std::size_t>(cellsX) * static_cast<std::size_t>(cellsY), 0);
    const auto cell = [&](int cx, int cy) -> char& {
        return covered[static_cast<std::size_t>(cy) * static_cast<std::size_t>(cellsX) + static_cast<std::size_t>(cx)];
    };
    // Large flat areas take the largest stroke that fits, aligned to its own size,
    // so the ground repeats less often.
    for (const int blockCells : {8, 4, 2}) {
        const int tilesAcross = blockCells * kBrushCellTiles;
        for (int by = 0; by + blockCells <= cellsY; by += blockCells) {
            for (int bx = 0; bx + blockCells <= cellsX; bx += blockCells) {
                const int level = flatLevel(bx, by);
                if (level < 0) {
                    continue;
                }
                bool uniform = true;
                for (int cy = by; cy < by + blockCells && uniform; ++cy) {
                    for (int cx = bx; cx < bx + blockCells && uniform; ++cx) {
                        uniform = !cell(cx, cy) && flatLevel(cx, cy) == level;
                    }
                }
                if (!uniform) {
                    continue;
                }
                const std::vector<const RandomMapSection*> strokes = tiles.strokes(paintOf(level), tilesAcross);
                if (strokes.empty()) {
                    continue;
                }
                plan.stamps.push_back(RandomMapStamp{pick(strokes)->sectionPath, bx * kBrushCellTiles, by * kBrushCellTiles, 0});
                for (int cy = by; cy < by + blockCells; ++cy) {
                    for (int cx = bx; cx < bx + blockCells; ++cx) {
                        cell(cx, cy) = 1;
                    }
                }
            }
        }
    }

    const BrushGraph& graph = canvas.graph();
    for (int cy = 0; cy < cellsY; ++cy) {
        for (int cx = 0; cx < cellsX; ++cx) {
            if (cell(cx, cy)) {
                continue;
            }
            const std::array<int, 4> corners = canvas.cellCorners(cx, cy);
            int level = flatLevel(cx, cy);
            if (level < 0) {
                const std::vector<const RandomMapSection*> pieces = tiles.pieces(graph, levels, corners);
                if (!pieces.empty()) {
                    plan.stamps.push_back(RandomMapStamp{pick(pieces)->sectionPath, cx * kBrushCellTiles, cy * kBrushCellTiles, 0});
                    continue;
                }
                const std::string name = tiles.pieceName(graph, levels, corners);
                noteMissing(name.empty() ? "a piece joining " + paintOf(*std::min_element(corners.begin(), corners.end())).terrain + " and "
                                               + paintOf(*std::max_element(corners.begin(), corners.end())).terrain
                                         : name);
                ++plan.missingCells;
                int bestCount = 0;
                for (const int corner : corners) {
                    const int count = static_cast<int>(std::count(corners.begin(), corners.end(), corner));
                    if (count > bestCount) {
                        bestCount = count;
                        level = corner;
                    }
                }
            }
            const RandomMapPaint& paint = paintOf(level);
            std::vector<const RandomMapSection*> strokes = tiles.strokes(paint, kBrushCellTiles);
            if (!strokes.empty()) {
                plan.stamps.push_back(RandomMapStamp{pick(strokes)->sectionPath, cx * kBrushCellTiles, cy * kBrushCellTiles, 0});
            } else {
                // Worlds without brush strokes use their base section, repeated to fill the cell.
                plan.stamps.push_back(RandomMapStamp{paint.sectionPath, cx * kBrushCellTiles, cy * kBrushCellTiles, kBrushCellTiles});
            }
        }
    }
    return plan;
}

}  // namespace galaxy

namespace galaxy {

std::string stampBrushScriptName(std::string_view world, std::string_view button) {
    std::string name = upper(world) + "_" + upper(button);
    for (char& character : name) {
        if (character == ' ') {
            character = '_';
        }
    }
    return name;
}

namespace {

// Where a stamp of this size may start along one axis, the way the Tile Brush snaps
// it: a stamp wider than a cell is centred on a cell corner, a cell-sized one fills
// a cell, and a smaller one fills the first or second part of a cell.
std::vector<int> stampAxisOrigins(int size, int cells, int mapTiles) {
    std::vector<int> origins;
    const int cell = kBrushCellTiles;
    const auto fits = [&](int origin) { return origin >= 0 && origin + size <= mapTiles; };
    if (size > cell) {
        for (int node = 1; node < cells; ++node) {
            if (fits(node * cell - size / 2)) {
                origins.push_back(node * cell - size / 2);
            }
        }
    } else if (size == cell) {
        for (int c = 0; c < cells; ++c) {
            if (fits(c * cell)) {
                origins.push_back(c * cell);
            }
        }
    } else if (size > 0) {
        for (int c = 0; c < cells; ++c) {
            if (fits(c * cell)) {
                origins.push_back(c * cell);
            }
            if (size * 2 <= cell && fits(c * cell + size)) {
                origins.push_back(c * cell + size);
            }
        }
    }
    return origins;
}

}  // namespace

std::vector<RandomMapStamp> planStampBrushes(const RmsScript& script, const RandomMapOutcome& outcome,
                                             const std::vector<RandomMapStampBrush>& brushes, const BrushCanvas& canvas,
                                             const RmsTerrainNames& names, int tileWidth, int tileHeight, std::mt19937& rng,
                                             std::vector<RmsMessage>& messages) {
    std::vector<RandomMapStamp> stamps;
    const int cellsX = canvas.width() - 1;
    const int cellsY = canvas.height() - 1;
    if (cellsX <= 0 || cellsY <= 0 || tileWidth <= 0 || tileHeight <= 0) {
        return stamps;
    }
    const auto flatAt = [&](int cx, int cy) {
        if (cx < 0 || cy < 0 || cx >= cellsX || cy >= cellsY) {
            return -2;
        }
        const std::array<int, 4> corners = canvas.cellCorners(cx, cy);
        return corners[0] == corners[1] && corners[0] == corners[2] && corners[0] == corners[3] ? corners[0] : -1;
    };
    // Tiles already covered by a stamp, so no two overlap.
    std::vector<unsigned char> covered(static_cast<std::size_t>(tileWidth) * static_cast<std::size_t>(tileHeight), 0);
    const auto free = [&](int x, int y, int w, int h) {
        for (int ty = y; ty < y + h; ++ty) {
            for (int tx = x; tx < x + w; ++tx) {
                if (covered[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tileWidth) + static_cast<std::size_t>(tx)]) {
                    return false;
                }
            }
        }
        return true;
    };
    const double ratio = static_cast<double>(canvas.width()) * canvas.height() / kScaleReference;
    // Each job is a command and the stamps it draws from: one named brush for
    // create_terrain, or every hill of its base_terrain for create_elevation.
    struct Job {
        std::size_t command;
        RandomMapStampBrush brush;
        bool elevation;
    };
    std::vector<Job> jobs;
    for (const std::size_t index : outcome.stampCommands) {
        const std::string wanted = upper(script.commands[index].args[0].text);
        for (const RandomMapStampBrush& candidate : brushes) {
            if (candidate.name == wanted) {
                jobs.push_back(Job{index, candidate, false});
                break;
            }
        }
    }
    for (const std::size_t index : outcome.elevationCommands) {
        const RmsCommand& command = script.commands[index];
        const RmsAttribute* attribute = command.find("base_terrain");
        if (attribute == nullptr || attribute->args.empty()) {
            messages.push_back(RmsMessage{command.line, "create_elevation needs base_terrain here, such as GREENWORLD_BASE-85: its hills are the ground's own."});
            continue;
        }
        Job job{index, RandomMapStampBrush{"create_elevation", upper(attribute->args[0].text), {}}, true};
        // Higher elevation in the game reaches higher; here it adds the big hills.
        const int elevation = command.args.empty() ? 1 : command.args[0].integer();
        for (const RandomMapStampBrush& candidate : brushes) {
            if (candidate.base != job.brush.base || candidate.name.find("HILL") == std::string::npos) {
                continue;
            }
            for (const RandomMapStampSection& section : candidate.sections) {
                if (elevation >= 4 || std::max(section.width, section.height) <= 8) {
                    job.brush.sections.push_back(section);
                }
            }
        }
        if (job.brush.sections.empty()) {
            messages.push_back(RmsMessage{command.line, "create_elevation found no hills for " + job.brush.base + "."});
            continue;
        }
        jobs.push_back(std::move(job));
    }
    for (const Job& job : jobs) {
        const RmsCommand& command = script.commands[job.command];
        const RandomMapStampBrush* brush = &job.brush;
        if (brush->sections.empty()) {
            continue;
        }
        const int base = names.find ? names.find(brush->base) : -1;
        if (const RmsAttribute* attribute = command.find("base_terrain"); !job.elevation && attribute != nullptr && !attribute->args.empty()
            && upper(attribute->args[0].text) != brush->base) {
            messages.push_back(RmsMessage{attribute->line, brush->name + " only goes on " + brush->base + ", so base_terrain was skipped."});
        }
        // How many: number_of_clumps, or land_percent and number_of_tiles as an area.
        double area = 0.0;
        for (const RandomMapStampSection& section : brush->sections) {
            area += static_cast<double>(section.width) * section.height;
        }
        area = std::max(1.0, area / static_cast<double>(brush->sections.size()));
        double count = std::max(0.0, numberAt(command, "number_of_clumps", 1.0));
        const int percentAt = lastIndexOf(command, "land_percent");
        const int tilesAt = lastIndexOf(command, "number_of_tiles");
        if (job.elevation) {
            // A hill stamp has its own size, so only number_of_clumps counts.
        } else if (percentAt > tilesAt) {
            count = std::clamp(numberAt(command, "land_percent", 0.0), 0.0, 100.0) / 100.0 * tileWidth * tileHeight / area;
        } else if (tilesAt >= 0) {
            // Script tiles are game-size squares, 16 x 16 map tiles each.
            count = std::max(0.0, numberAt(command, "number_of_tiles", 0.0)) * 256.0 / area;
        }
        if (has(command, "set_scale_by_size") || has(command, "set_scale_by_groups")) {
            count *= ratio;
        }
        const int wantedCount = std::max(0, static_cast<int>(std::lround(count)));
        const int spacing = std::max(0, static_cast<int>(numberAt(command, "spacing_to_other_terrain_types", 0.0)));
        int avoidStarts = -1;
        const int startArea = std::max(1, static_cast<int>(std::lround(std::max(canvas.width(), canvas.height()) / 9.0)));
        if (const RmsAttribute* attribute = command.find("set_avoid_player_start_areas")) {
            avoidStarts = attribute->args.empty() ? startArea : std::max(0, attribute->args[0].integer());
        } else if (job.elevation) {
            // Hills keep out of the players' start areas, where they would be in the way of building.
            avoidStarts = startArea;
        }
        for (const char* skipped : {"clumping_factor", "height_limits", "spacing_to_specific_terrain", "set_flat_terrain_only"}) {
            if (const RmsAttribute* attribute = command.find(skipped)) {
                messages.push_back(RmsMessage{attribute->line, std::string(skipped) + " does nothing for " + brush->name + "."});
            }
        }

        // True when every cell under the stamp, and every cell within spacing of
        // them, is flat ground of the stamp's base.
        const auto groundFits = [&](int x, int y, int w, int h) {
            const int cx0 = x / kBrushCellTiles;
            const int cy0 = y / kBrushCellTiles;
            const int cx1 = (x + w - 1) / kBrushCellTiles;
            const int cy1 = (y + h - 1) / kBrushCellTiles;
            for (int cy = cy0 - spacing; cy <= cy1 + spacing; ++cy) {
                for (int cx = cx0 - spacing; cx <= cx1 + spacing; ++cx) {
                    const int ground = flatAt(cx, cy);
                    const bool under = cx >= cx0 && cx <= cx1 && cy >= cy0 && cy <= cy1;
                    if ((under && ground != base) || (!under && ground != base && ground != -2)) {
                        return false;
                    }
                }
            }
            if (avoidStarts >= 0) {
                for (const auto& [ox, oy] : outcome.playerOrigins) {
                    if (ox < 0) {
                        continue;
                    }
                    for (int cy = cy0; cy <= cy1; ++cy) {
                        for (int cx = cx0; cx <= cx1; ++cx) {
                            if (std::max(std::abs(cx - ox), std::abs(cy - oy)) <= avoidStarts) {
                                return false;
                            }
                        }
                    }
                }
            }
            return true;
        };
        // Places for each stamp size, found the first time that size is drawn.
        struct Places {
            std::vector<std::pair<int, int>> origins;
            std::size_t next = 0;
        };
        std::map<std::pair<int, int>, Places> places;
        const auto placesFor = [&](int w, int h) -> Places& {
            const auto key = std::make_pair(w, h);
            auto found = places.find(key);
            if (found != places.end()) {
                return found->second;
            }
            Places list;
            if (base >= 0) {
                for (const int y : stampAxisOrigins(h, cellsY, tileHeight)) {
                    for (const int x : stampAxisOrigins(w, cellsX, tileWidth)) {
                        if (groundFits(x, y, w, h)) {
                            list.origins.emplace_back(x, y);
                        }
                    }
                }
            }
            std::shuffle(list.origins.begin(), list.origins.end(), rng);
            return places.emplace(key, std::move(list)).first->second;
        };
        int placed = 0;
        std::set<std::pair<int, int>> spent;
        std::set<std::pair<int, int>> sizes;
        for (const RandomMapStampSection& section : brush->sections) {
            sizes.insert({section.width, section.height});
        }
        while (placed < wantedCount && spent.size() < sizes.size()) {
            // A random piece first, so every piece of a brush that mixes sizes gets its share.
            const RandomMapStampSection& section = brush->sections[std::uniform_int_distribution<std::size_t>(0, brush->sections.size() - 1)(rng)];
            const auto key = std::make_pair(section.width, section.height);
            if (spent.count(key) != 0 || section.width <= 0 || section.height <= 0) {
                spent.insert(key);
                continue;
            }
            Places& list = placesFor(section.width, section.height);
            bool done = false;
            while (list.next < list.origins.size()) {
                const auto [x, y] = list.origins[list.next++];
                if (!free(x, y, section.width, section.height)) {
                    continue;
                }
                for (int ty = y; ty < y + section.height; ++ty) {
                    for (int tx = x; tx < x + section.width; ++tx) {
                        covered[static_cast<std::size_t>(ty) * static_cast<std::size_t>(tileWidth) + static_cast<std::size_t>(tx)] = 1;
                    }
                }
                stamps.push_back(RandomMapStamp{section.path, x, y, 0});
                ++placed;
                done = true;
                break;
            }
            if (!done) {
                spent.insert(key);
            }
        }
        if (placed < wantedCount) {
            messages.push_back(RmsMessage{command.line, (job.elevation ? "create_elevation" : "create_terrain " + brush->name) + " placed " + std::to_string(placed) + " of "
                                                            + std::to_string(wantedCount)
                                                            + (placed == 0 ? ": it needs flat " + brush->base + " and found none." : ": there was no more room.")});
        }
    }
    return stamps;
}

}  // namespace galaxy
