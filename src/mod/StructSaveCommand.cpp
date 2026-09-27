#include "mod/StructSaveCommand.h"

#include "mod/Entry.h"
#include "mod/Gloabl.h"

#include "ll/api/command/CommandHandle.h"
#include "ll/api/command/CommandRegistrar.h"

#include "mc/deps/core/math/Vec3.h"
#include "mc/nbt/CompoundTag.h"
#include "mc/server/commands/CommandOrigin.h"
#include "mc/server/commands/CommandOutput.h"
#include "mc/server/commands/CommandPermissionLevel.h"
#include "mc/server/commands/CommandPosition.h"
#include "mc/world/level/dimension/Dimension.h"
#include "mc/world/level/levelgen/structure/BoundingBox.h"
#include "mc/world/level/levelgen/structure/StructureTemplate.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
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

std::filesystem::path buildOutputPath(std::string_view name) {
    auto base = Entry::getInstance().getSelf().getDataDir();
    base /= "structures";
    std::filesystem::create_directories(base);
    auto fileName = sanitizeName(name);
    if (!fileName.ends_with(".nbt")) {
        fileName += ".nbt";
    }
    base /= fileName;
    return base;
}
} // namespace

void registerStructSaveCommand() {
    auto& cmd = ll::command::CommandRegistrar::getInstance(false).getOrCreateCommand(
        "structsave",
        "Save a selected region to NBT",
        CommandPermissionLevel::Any
    );
    cmd.alias("ssave");

    cmd.overload<StructSaveParam>()
        .required("name")
        .required("from")
        .required("to")
        .optional("includeEntities")
        .optional("includeBlocks")
        .execute([&](CommandOrigin const& origin, CommandOutput& output, StructSaveParam const& param, ::Command const& cmd
                 ) {
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

            auto outPath = buildOutputPath(param.name);
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

} // namespace my_mod
