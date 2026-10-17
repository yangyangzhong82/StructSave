#include "mod/StructSaveCommand.h"

#include "mod/Entry.h"
#include "mod/Gloabl.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/deps/nbt/CompoundTag.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandPosition.h"
#include "mc/world/level/BlockSource.h"
#include "mc/world/level/Level.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureManager.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "mc/world/phys/AABB.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <mutex>
#include <shared_mutex>
#include <string>
#include <utility>

namespace my_mod {

namespace {
struct StructSaveParam {
    std::string     name;
    CommandPosition from;
    CommandPosition to;
    bool            includeEntities{false};
    bool            includeBlocks{true};
};

struct StructPlaceParam {
    std::string     name;
    CommandPosition pos;
    bool            includeEntities{true};
    bool            includeBlocks{true};
};

enum class StructureStorage { memory, disk };

struct StructLoadParam {
    std::string name;
    StructureStorage storage{StructureStorage::memory};
    std::string structureName;
};

struct StructDeleteParam {
    std::string name;
    StructureStorage storage;
};

struct StructExportParam {
    std::string name;
    StructureStorage storage;
    std::string fileName;
};

std::string fullStructureName(std::string name) {
    if (name.find(':') == std::string::npos) {
        name = "mystructure:" + name;
    }
    return name;
}

std::string storageLabel(StructureStorage storage) {
    return tr(storage == StructureStorage::memory ? "structure.storage.memory" : "structure.storage.disk");
}

std::unique_ptr<StructureTemplate> loadDiskStructure(Level& level, std::string const& name) {
    auto manager = level.getStructureManager();
    auto structure = std::make_unique<StructureTemplate>(name, *manager->mUnknownBlockRegistry);
    // Load directly from the world, without reading the memory cache or resource packs.
    if (!manager->load(*structure, nullptr, &level.getLevelStorage())) {
        return nullptr;
    }
    return structure;
}

std::string sanitizeName(std::string_view raw) {
    std::string out;
    out.reserve(raw.size());
    for (char c : raw) {
        if ((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-'
            || c == '.') {
            out.push_back(c);
        } else {
            out.push_back('_');
        }
    }
    if (out.empty()) {
        out = "structure";
    }
    return out;
}

std::filesystem::path buildStructurePath(std::string_view name) {
    auto base  = Entry::getInstance().getSelf().getDataDir();
    base      /= "structures";
    auto fileName = sanitizeName(name);
    if (!fileName.ends_with(".mcstructure")) {
        fileName += ".mcstructure";
    }
    base /= fileName;
    return base;
}
} // namespace

void registerStructSaveCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("structsave", "Save a selected region to mcstructure", CommandPermissionLevel::Any);
    cmd.alias("ssave");

    cmd.overload<StructSaveParam>()
        .required("name")
        .required("from")
        .required("to")
        .optional("includeEntities")
        .optional("includeBlocks")
        .execute([&](CommandOrigin const&   origin,
                     CommandOutput&         output,
                     StructSaveParam const& param,
                     ::Command const&       cmd) {
            auto* dim = origin.getDimension();
            if (!dim) {
                output.error("No dimension found for origin");
                return;
            }

            auto from = param.from.getBlockPos(cmd.mVersion, origin, Vec3{0, 0, 0});
            auto to   = param.to.getBlockPos(cmd.mVersion, origin, Vec3{0, 0, 0});

            BlockPos minPos{
                std::min(from.x, to.x),
                std::min(from.y, to.y),
                std::min(from.z, to.z),
            };
            BlockPos maxPos{
                std::max(from.x, to.x),
                std::max(from.y, to.y),
                std::max(from.z, to.z),
            };
            BoundingBox box{minPos, maxPos};

            bool ignoreBlocks   = !param.includeBlocks;
            bool ignoreEntities = !param.includeEntities;

            auto& region = dim->getBlockSourceFromMainChunkSource();
            auto  temp   = StructureTemplate::create(param.name, region, box, ignoreBlocks, ignoreEntities);
            if (!temp) {
                output.error("Failed to create structure template");
                return;
            }

            auto tag = temp->save();
            if (!tag) {
                output.error("Failed to save structure template");
                return;
            }

            auto outPath = buildStructurePath(param.name);
            std::filesystem::create_directories(outPath.parent_path());
            std::ofstream out(outPath, std::ios::binary);
            if (!out.is_open()) {
                output.error("Failed to open output file");
                return;
            }
            auto bin = tag->toBinaryNbt(true);
            out.write(bin.data(), static_cast<std::streamsize>(bin.size()));
            out.close();

            output.success("Saved structure to " + outPath.string());
        });
}

void registerStructPlaceCommand() {
    auto& command = ll::command::CommandRegistrar::getInstance(false)
                        .getOrCreateCommand("structplace", "Place a saved mcstructure structure", CommandPermissionLevel::Any);
    command.alias("splace");

    command.overload<StructPlaceParam>()
        .required("name")
        .required("pos")
        .optional("includeEntities")
        .optional("includeBlocks")
        .execute([](CommandOrigin const&    origin,
                    CommandOutput&          output,
                    StructPlaceParam const& param,
                    ::Command const&        cmd) {
            auto* dim = origin.getDimension();
            if (!dim) {
                output.error(tr("structure.place.no_dimension"));
                return;
            }
            if (!param.includeBlocks && !param.includeEntities) {
                output.error(tr("structure.place.nothing_selected"));
                return;
            }

            try {
                auto path = buildStructurePath(param.name);
                if (!std::filesystem::is_regular_file(path)) {
                    output.error(tr("structure.place.not_found", path.string()));
                    return;
                }

                std::ifstream in(path, std::ios::binary);
                if (!in.is_open()) {
                    output.error(tr("structure.place.read_failed", path.string()));
                    return;
                }
                std::string binary{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                if (in.bad()) {
                    output.error(tr("structure.place.read_failed", path.string()));
                    return;
                }

                // Match the little-endian binary NBT written by /structsave.
                auto tag = CompoundTag::fromBinaryNbt(binary, true);
                if (!tag) {
                    output.error(tr("structure.place.invalid_nbt", path.string()));
                    return;
                }
                auto structure = StructureTemplate::create(param.name, *tag);
                if (!structure) {
                    output.error(tr("structure.place.invalid_structure", path.string()));
                    return;
                }
                auto size = structure->rawSize();
                if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
                    output.error(tr("structure.place.invalid_structure", path.string()));
                    return;
                }

                auto  pos    = param.pos.getBlockPos(cmd.mVersion, origin, Vec3{0, 0, 0});
                auto& region = dim->getBlockSourceFromMainChunkSource();
                // Validate the entire destination before changing any blocks or entities.
                auto maxX = static_cast<std::int64_t>(pos.x) + size.x - 1;
                auto maxY = static_cast<std::int64_t>(pos.y) + size.y - 1;
                auto maxZ = static_cast<std::int64_t>(pos.z) + size.z - 1;
                if (pos.y < region.getMinHeight() || maxY >= region.getMaxHeight()
                    || maxX >= std::numeric_limits<int>::max() || maxZ >= std::numeric_limits<int>::max()) {
                    output.error(tr("structure.place.out_of_bounds"));
                    return;
                }
                BlockPos maxPos{static_cast<int>(maxX), static_cast<int>(maxY), static_cast<int>(maxZ)};
                if (!region.hasChunksAt(static_cast<AABB>(BoundingBox{pos, maxPos}), true)) {
                    output.error(tr("structure.place.unloaded_chunks"));
                    return;
                }
                structure->placeInWorld(
                    region,
                    pos,
                    Mirror::None,
                    Rotation::None,
                    !param.includeBlocks,
                    !param.includeEntities
                );
                output.success(tr("structure.place.success", path.filename().string(), pos.x, pos.y, pos.z));
            } catch (std::exception const& e) {
                output.error(tr("structure.place.failed", e.what()));
            }
        });
}

void registerStructLoadCommand() {
    auto& command = ll::command::CommandRegistrar::getInstance(false)
                        .getOrCreateCommand("structload", "Import a mcstructure file into memory or the world", CommandPermissionLevel::Any);
    command.alias("sload");

    command.overload<StructLoadParam>()
        .required("name")
        .optional("storage")
        .optional("structureName")
        .execute([](CommandOrigin const& origin, CommandOutput& output, StructLoadParam const& param) {
            auto* level = origin.getLevel();
            if (!level) {
                output.error(tr("structure.load.no_level"));
                return;
            }

            try {
                auto path = buildStructurePath(param.name);
                if (!std::filesystem::is_regular_file(path)) {
                    output.error(tr("structure.place.not_found", path.string()));
                    return;
                }

                std::ifstream in(path, std::ios::binary);
                if (!in.is_open()) {
                    output.error(tr("structure.place.read_failed", path.string()));
                    return;
                }
                std::string binary{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
                if (in.bad()) {
                    output.error(tr("structure.place.read_failed", path.string()));
                    return;
                }
                auto tag = CompoundTag::fromBinaryNbt(binary, true);
                if (!tag) {
                    output.error(tr("structure.place.invalid_nbt", path.string()));
                    return;
                }

                auto name = fullStructureName(param.structureName.empty() ? path.stem().string() : param.structureName);
                auto structure = StructureTemplate::create(name, *tag);
                if (!structure) {
                    output.error(tr("structure.place.invalid_structure", path.string()));
                    return;
                }
                auto size = structure->rawSize();
                if (size.x <= 0 || size.y <= 0 || size.z <= 0) {
                    output.error(tr("structure.place.invalid_structure", path.string()));
                    return;
                }

                auto manager = level->getStructureManager();
                if (param.storage == StructureStorage::disk) {
                    manager->saveToLevel(*structure, level->getLevelStorage());
                }
                // Refresh the cache as well, so structure blocks see disk imports immediately.
                manager->getOrCreate(name) = std::move(*structure);
                output.success(tr("structure.load.success", path.filename().string(), storageLabel(param.storage), name));
            } catch (std::exception const& e) {
                output.error(tr("structure.load.failed", e.what()));
            }
        });
}

void registerStructDeleteCommand() {
    auto& command = ll::command::CommandRegistrar::getInstance(false)
                        .getOrCreateCommand("structdelete", "Delete a structure from memory or the world", CommandPermissionLevel::Any);
    command.alias("sdelete");

    command.overload<StructDeleteParam>()
        .required("name")
        .required("storage")
        .execute([](CommandOrigin const& origin, CommandOutput& output, StructDeleteParam const& param) {
            auto* level = origin.getLevel();
            if (!level) {
                output.error(tr("structure.load.no_level"));
                return;
            }
            try {
                auto name = fullStructureName(param.name);
                auto manager = level->getStructureManager();
                bool deleted = false;
                bool diskCopyRemains = false;
                if (param.storage == StructureStorage::memory) {
                    diskCopyRemains = loadDiskStructure(*level, name) != nullptr;
                    std::unique_lock lock{*manager->mRepositoryMutex};
                    deleted = manager->mStructureRepository->erase(name) != 0;
                } else {
                    if (!loadDiskStructure(*level, name)) {
                        output.error(tr("structure.storage.not_found", storageLabel(param.storage), name));
                        return;
                    }
                    deleted = manager->deleteStructure(name, level->getLevelStorage()) == StructureDeleteResult::Success;
                    if (deleted) {
                        // Do not leave a cached copy available to structure blocks after disk deletion.
                        std::unique_lock lock{*manager->mRepositoryMutex};
                        manager->mStructureRepository->erase(name);
                    }
                }
                if (!deleted) {
                    output.error(tr("structure.storage.not_found", storageLabel(param.storage), name));
                    return;
                }
                if (param.storage == StructureStorage::disk) {
                    output.success(tr("structure.delete.disk_success", name));
                } else if (diskCopyRemains) {
                    output.success(tr("structure.delete.memory_disk_remains", name, name));
                } else {
                    output.success(tr("structure.delete.success", storageLabel(param.storage), name));
                }
            } catch (std::exception const& e) {
                output.error(tr("structure.delete.failed", e.what()));
            }
        });
}

void registerStructExportCommand() {
    auto& command = ll::command::CommandRegistrar::getInstance(false)
                        .getOrCreateCommand("structexport", "Export a memory or world structure to mcstructure", CommandPermissionLevel::Any);
    command.alias("sexport");

    command.overload<StructExportParam>()
        .required("name")
        .required("storage")
        .optional("fileName")
        .execute([](CommandOrigin const& origin, CommandOutput& output, StructExportParam const& param) {
            auto* level = origin.getLevel();
            if (!level) {
                output.error(tr("structure.load.no_level"));
                return;
            }
            try {
                auto name = fullStructureName(param.name);
                std::unique_ptr<CompoundTag> tag;
                if (param.storage == StructureStorage::memory) {
                    auto manager = level->getStructureManager();
                    std::shared_lock lock{*manager->mRepositoryMutex};
                    auto it = manager->mStructureRepository->find(name);
                    if (it != manager->mStructureRepository->end()) {
                        tag = it->second->save();
                    }
                } else {
                    auto structure = loadDiskStructure(*level, name);
                    if (structure) {
                        tag = structure->save();
                    }
                }
                if (!tag) {
                    output.error(tr("structure.storage.not_found", storageLabel(param.storage), name));
                    return;
                }

                auto path = buildStructurePath(param.fileName.empty() ? name : param.fileName);
                auto binary = tag->toBinaryNbt(true);
                std::filesystem::create_directories(path.parent_path());
                std::ofstream out(path, std::ios::binary | std::ios::trunc);
                out.write(binary.data(), static_cast<std::streamsize>(binary.size()));
                out.close();
                if (!out) {
                    output.error(tr("structure.export.write_failed", path.string()));
                    return;
                }
                output.success(tr("structure.export.success", storageLabel(param.storage), name, path.string()));
            } catch (std::exception const& e) {
                output.error(tr("structure.export.failed", e.what()));
            }
        });
}

} // namespace my_mod
