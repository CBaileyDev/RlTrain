#include "sim/PracticeArena.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <fstream>
#include <system_error>
#include <unordered_map>

namespace rls {

namespace {

/// Vertices closer together than this are treated as the same point.
/// Rocket League's units are roughly centimetres, so a hundredth of a unit is
/// far below anything that matters physically but still collapses the shared
/// corners produced when adjacent faces are emitted independently.
constexpr float kWeldTolerance = 0.01f;

/// Quantizes a coordinate into an integer grid so exact-match hashing can stand
/// in for a distance test. Using a hash map keeps mesh building linear rather
/// than quadratic, which matters once a mesh has thousands of vertices.
int64_t Quantize(float v) {
    return static_cast<int64_t>(std::llround(static_cast<double>(v) / kWeldTolerance));
}

/// Writes a value as little-endian bytes.
///
/// RocketSim reads these files by copying raw bytes into an int32 or a float, so
/// the on-disk layout must be the platform's native layout. Every platform this
/// project supports is little-endian; the static_assert makes that assumption
/// explicit rather than silent.
template <typename T>
void WriteLE(std::ostream& out, T value) {
    static_assert(sizeof(T) == 4, "collision mesh fields are all 4 bytes");
    char bytes[4];
    std::memcpy(bytes, &value, 4);
    out.write(bytes, 4);
}

} // namespace

std::size_t detail::VertexKeyHash::operator()(const detail::VertexKey& k) const noexcept {
    // 64-bit FNV-style mix. Any decent spread works here; this is not security-sensitive.
    std::uint64_t h = 1469598103934665603ULL;
    for (int64_t component : { k.x, k.y, k.z }) {
        h ^= static_cast<std::uint64_t>(component);
        h *= 1099511628211ULL;
    }
    return static_cast<std::size_t>(h);
}

int CollisionMeshBuilder::AddVertex(float x, float y, float z) {
    const detail::VertexKey key{ Quantize(x), Quantize(y), Quantize(z) };
    auto found = weld.find(key);
    if (found != weld.end())
        return found->second;

    const int index = static_cast<int>(vertices.size() / 3);
    vertices.push_back(x);
    vertices.push_back(y);
    vertices.push_back(z);
    weld.emplace(key, index);
    return index;
}

void CollisionMeshBuilder::AddTriangle(int a, int b, int c) {
    // Degenerate triangles have no surface and make Bullet's edge-info
    // generation produce garbage normals, so drop them.
    if (a == b || b == c || a == c)
        return;
    triangles.push_back(a);
    triangles.push_back(b);
    triangles.push_back(c);
}

void CollisionMeshBuilder::AddQuad(
    float ax, float ay, float az,
    float bx, float by, float bz,
    float cx, float cy, float cz,
    float dx, float dy, float dz) {
    const int a = AddVertex(ax, ay, az);
    const int b = AddVertex(bx, by, bz);
    const int c = AddVertex(cx, cy, cz);
    const int d = AddVertex(dx, dy, dz);
    AddTriangle(a, b, c);
    AddTriangle(a, c, d);
}

void CollisionMeshBuilder::AddBox(
    float minX, float minY, float minZ,
    float maxX, float maxY, float maxZ) {
    // Bottom and top
    AddQuad(minX, minY, minZ, maxX, minY, minZ, maxX, maxY, minZ, minX, maxY, minZ);
    AddQuad(minX, minY, maxZ, maxX, minY, maxZ, maxX, maxY, maxZ, minX, maxY, maxZ);
    // Sides along x
    AddQuad(minX, minY, minZ, minX, maxY, minZ, minX, maxY, maxZ, minX, minY, maxZ);
    AddQuad(maxX, minY, minZ, maxX, maxY, minZ, maxX, maxY, maxZ, maxX, minY, maxZ);
    // Sides along y
    AddQuad(minX, minY, minZ, maxX, minY, minZ, maxX, minY, maxZ, minX, minY, maxZ);
    AddQuad(minX, maxY, minZ, maxX, maxY, minZ, maxX, maxY, maxZ, minX, maxY, maxZ);
}

bool CollisionMeshBuilder::WriteToFile(const std::filesystem::path& path, std::string& errorOut) const {
    if (TriangleCount() == 0 || VertexCount() == 0) {
        errorOut = "refusing to write an empty collision mesh";
        return false;
    }

    std::error_code ec;
    const std::filesystem::path parent = path.parent_path();
    if (!parent.empty()) {
        std::filesystem::create_directories(parent, ec);
        if (ec) {
            errorOut = "could not create directory " + parent.string() + ": " + ec.message();
            return false;
        }
    }

    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    if (!out) {
        errorOut = "could not open " + path.string() + " for writing";
        return false;
    }

    WriteLE<int32_t>(out, static_cast<int32_t>(TriangleCount()));
    WriteLE<int32_t>(out, static_cast<int32_t>(VertexCount()));
    for (int32_t index : triangles)
        WriteLE<int32_t>(out, index);
    for (float coordinate : vertices)
        WriteLE<float>(out, coordinate);

    out.flush();
    if (!out) {
        errorOut = "failed while writing " + path.string();
        return false;
    }
    return true;
}

CollisionMeshBuilder BuildPracticeArena(const PracticeArenaDimensions& d) {
    CollisionMeshBuilder mesh;

    const float x = d.halfWidth;
    const float y = d.halfLength;
    const float z = d.ceilingHeight;
    const float corner = d.cornerChamfer;
    const float edge = d.edgeChamfer;
    const float gx = d.goalHalfWidth;
    const float gz = d.goalHeight;

    // --- Floor and ceiling ---------------------------------------------------
    // Inset by the edge chamfer so the chamfer strips below can meet them cleanly.
    mesh.AddQuad(-x + edge, -y + edge, 0, x - edge, -y + edge, 0,
                  x - edge,  y - edge, 0, -x + edge,  y - edge, 0);
    mesh.AddQuad(-x + edge, -y + edge, z, x - edge, -y + edge, z,
                  x - edge,  y - edge, z, -x + edge,  y - edge, z);

    // --- Side walls (x = +/- halfWidth) --------------------------------------
    // Full height between the floor and ceiling chamfers, and stopping short of
    // the corner chamfers at each end.
    for (const float sx : { -x, x }) {
        mesh.AddQuad(sx, -y + corner, edge, sx, y - corner, edge,
                     sx,  y - corner, z - edge, sx, -y + corner, z - edge);
        // Chamfer strips where this wall meets the floor and the ceiling.
        mesh.AddQuad(sx, -y + corner, edge, sx, y - corner, edge,
                     sx - (sx > 0 ? edge : -edge), y - corner, 0,
                     sx - (sx > 0 ? edge : -edge), -y + corner, 0);
        mesh.AddQuad(sx, -y + corner, z - edge, sx, y - corner, z - edge,
                     sx - (sx > 0 ? edge : -edge), y - corner, z,
                     sx - (sx > 0 ? edge : -edge), -y + corner, z);
    }

    // --- Back walls (y = +/- halfLength), with a hole for the goal -----------
    for (const float sy : { -y, y }) {
        // Left and right of the goal mouth, full height.
        mesh.AddQuad(-x + corner, sy, 0, -gx, sy, 0, -gx, sy, z, -x + corner, sy, z);
        mesh.AddQuad( gx,         sy, 0,  x - corner, sy, 0, x - corner, sy, z, gx, sy, z);
        // Above the goal mouth.
        mesh.AddQuad(-gx, sy, gz, gx, sy, gz, gx, sy, z, -gx, sy, z);
    }

    // --- Corner chamfers -----------------------------------------------------
    // A single 45-degree plane across each vertical corner. The real arena is
    // curved here; one flat face is a reasonable stand-in and, importantly,
    // removes the sharp corner that a ball can otherwise get wedged into.
    for (const float sx : { -1.0f, 1.0f }) {
        for (const float sy : { -1.0f, 1.0f }) {
            const float x0 = sx * x;
            const float y0 = sy * (y - corner);
            const float x1 = sx * (x - corner);
            const float y1 = sy * y;
            mesh.AddQuad(x0, y0, 0, x1, y1, 0, x1, y1, z, x0, y0, z);
        }
    }

    // --- Goal interiors ------------------------------------------------------
    // A closed box behind each goal mouth so the ball stops instead of flying off
    // into empty space. RocketSim detects the goal by position, not by geometry,
    // so this only needs to contain the ball after it crosses the line.
    for (const float sy : { -1.0f, 1.0f }) {
        const float mouth = sy * y;
        const float back = sy * (y + d.goalDepth);
        const float nearY = std::min(mouth, back);
        const float farY = std::max(mouth, back);

        // Back wall of the net.
        mesh.AddQuad(-gx, back, 0, gx, back, 0, gx, back, gz, -gx, back, gz);
        // Side walls.
        mesh.AddQuad(-gx, nearY, 0, -gx, farY, 0, -gx, farY, gz, -gx, nearY, gz);
        mesh.AddQuad( gx, nearY, 0,  gx, farY, 0,  gx, farY, gz,  gx, nearY, gz);
        // Roof and floor of the net.
        mesh.AddQuad(-gx, nearY, gz, gx, nearY, gz, gx, farY, gz, -gx, farY, gz);
        mesh.AddQuad(-gx, nearY, 0,  gx, nearY, 0,  gx, farY, 0,  -gx, farY, 0);
    }

    return mesh;
}

bool WritePracticeArena(
    const std::filesystem::path& collisionMeshesDir,
    std::string& errorOut,
    const PracticeArenaDimensions& dims) {
    // RocketSim looks for meshes under <dir>/<gamemode>/, and only loads files
    // with a .cmf extension.
    const std::filesystem::path target =
        collisionMeshesDir / "soccar" / "practice_arena.cmf";

    const CollisionMeshBuilder mesh = BuildPracticeArena(dims);
    return mesh.WriteToFile(target, errorOut);
}

} // namespace rls
