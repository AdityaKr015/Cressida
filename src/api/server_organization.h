#pragma once

#include <cstdint>
#include <nlohmann/json_fwd.hpp>
#include <optional>
#include <string>

namespace api {
  enum class FolderPosition { Top, Up, Down, Bottom };
  struct GuildFolderChange {
    std::string folder;
    std::optional<std::string> name;
    std::optional<std::uint32_t> color;
    std::optional<FolderPosition> position;
  };
  bool validFolderChange(const GuildFolderChange& change);
  nlohmann::json guildFolderPatch(const nlohmann::json& snapshot, const GuildFolderChange& change);
  // Empty folderId removes the server from its folder. A newFolderName creates
  // a folder instead; an existing destination must be present in the snapshot.
  // The returned settings-proto/1 PATCH includes the account's data version and
  // only the complete organization group, retaining opaque account metadata.
  nlohmann::json guildMovePatch(
      const nlohmann::json& snapshot, const std::string& guild, const std::string& folderId,
      const std::optional<std::string>& newFolderName = {}
  );
} // namespace api
