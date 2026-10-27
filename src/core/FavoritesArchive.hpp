#pragma once

#include "format/SctSection.hpp"

#include <filesystem>
#include <string>
#include <string_view>

namespace galaxy {

// Favorites live in ordinary tileset archives. Only a file named
// GE-Favorites-<name>.hpi is a favorites archive, and only those appear on the
// Favorites tab. Inside, a tile sits at sections/<category>/<subcategory>/<tile>.sct,
// the same layout as the game's own tilesets.
inline constexpr std::string_view kFavoritesPrefix = "GE-Favorites-";

// True for GE-Favorites-<name>.hpi with a name after the prefix. The prefix and
// the extension are matched without regard to case.
bool isFavoritesArchiveName(std::string_view filename);

// The file name for a favorites archive called name, such as GE-Favorites-FirstOne.hpi.
std::string favoritesArchiveFilename(std::string_view name);

// The name part of a favorites archive's file name, such as "FirstOne". Empty
// when the file is not a favorites archive.
std::string favoritesArchiveName(std::string_view filename);

// Checks a name typed for an archive, a category, a subcategory, or a tile. The
// text is trimmed first. Returns an empty string when the name is usable and
// otherwise a short sentence that says why not.
std::string favoritesNameProblem(std::string_view name);

// Writes a new favorites archive with no tiles. The archive holds one small
// note file because an HPI cannot be empty. Refuses to replace an existing file.
void createFavoritesArchive(const std::filesystem::path& path);

// Adds a tile under sections/<category>/<subcategory>/ and rewrites the archive.
// The tile is named after the subcategory with a number, such as Rivers03, and
// that name is returned. The archive is left as it was if anything fails.
std::string addFavoriteSection(
    const std::filesystem::path& archivePath,
    std::string_view category,
    std::string_view subcategory,
    const SectionStamp& section);

// Removes one file from an archive, by the path the archive lists, and rewrites it.
void removeFavoriteSection(const std::filesystem::path& archivePath, std::string_view sectionPath);

}  // namespace galaxy
