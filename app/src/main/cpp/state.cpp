#include "state.h"

void RoomSet::assign_roles() {
    for (Entity &e : entities)
        if (!e.anchor && e.ext_semantic < 0) e.role.clear();
    for (const Entity &room : entities) {
        if (!room.is_room) continue;
        auto set_role = [&](const XrUuidEXT &u, const std::string &role) {
            int i = find(u);
            if (i >= 0 && entities[i].role.empty()) entities[i].role = role;
        };
        set_role(room.floor, "floor");
        set_role(room.ceiling, "ceiling");
        for (size_t w = 0; w < room.walls.size(); ++w)
            set_role(room.walls[w], "wall " + std::to_string(w + 1) + "/" + std::to_string(room.walls.size()));
    }
    for (Entity &e : entities) {
        if (e.is_room) e.role = "room";
        else if (!e.role.empty()) continue;
        else if (e.anchor) e.role = "anchor";
        else if (e.labels.empty() && !e.has_b2d && !e.has_b3d && e.mesh_indices.empty()) e.role = "anchor?";
        else if (!e.mesh_indices.empty()) e.role = "mesh";
        else if (e.has_b3d) e.role = "volume";
        else if (e.has_b2d) e.role = "plane";
    }
}

const char *source_mode_name(int mode) {
    switch (mode) {
        case kSourceFb: return "FB query";
        case kSourceMeta: return "META discovery";
        case kSourceBoth: return "FB + META";
        case kSourceExt: return "EXT planes";
        default: return "?";
    }
}

const char *mesh_mode_name(int mode) {
    switch (mode) {
        case kMeshOff: return "off";
        case kMeshShaded: return "shaded";
        case kMeshWire: return "wire";
        case kMeshBoth: return "shaded+wire";
        default: return "?";
    }
}
