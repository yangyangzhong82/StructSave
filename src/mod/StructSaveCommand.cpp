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
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"
#include "mc/world/phys/AABB.h"

#include <algorithm>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <limits>
#include <string>

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
    if (!fileName.ends_with(".nbt")) {
        fileName += ".nbt";
    }
    base /= fileName;
    return base;
}
} // namespace

void registerStructSaveCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false)
                    .getOrCreateCommand("structsave", "Save a selected region to NBT", CommandPermissionLevel::Any);
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
                        .getOrCreateCommand("structplace", "Place a saved NBT structure", CommandPermissionLevel::Any);
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

} // namespace my_mod
