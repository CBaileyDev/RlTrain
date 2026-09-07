#pragma once

// =============================================================================
//  Practice Arena
//
//  RocketSim needs Rocket League's arena collision geometry to simulate a match.
//  That geometry lives inside the game and cannot be redistributed, so a new user
//  would normally be stuck before they could train anything at all.
//
//  This generates a stand-in arena instead: a simplified box built from published
//  field dimensions, with a floor, a ceiling, four walls, chamfered corners and
//  two goal recesses. It contains no Rocket League data of any kind.
//
//  What you gain: RL Studio works the moment it is installed. Physics, boost,
//  goals and demolitions all behave correctly, so every reinforcement-learning
//  lesson transfers exactly.
//
//  What you lose: fidelity. The real arena has curved walls and corner ramps that
//  change how the ball rolls and how advanced play works. A bot trained only here
//  will not transfer perfectly to the real game. For that, dump the real meshes
//  with RLArenaCollisionDumper. See docs/getting-started.md.
//
//  The output is a `.cmf` file, the format RocketSim reads:
//      int32   triangleCount
//      int32   vertexCount
//      int32   triangleIndices[triangleCount][3]
//      float   vertexPositions[vertexCount][3]
//  All little-endian. RocketSim warns that the mesh hash is unrecognized and then
//  loads it normally, which is exactly what we want.
// =============================================================================

#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>
#include <vector>

namespace rls {

namespace detail {

/// A vertex position quantized to an integer grid, so that welding duplicate
/// vertices can be a hash lookup instead of a distance search against every
/// vertex added so far.
struct VertexKey {
    int64_t x, y, z;
    bool operator==(const VertexKey& other) const {
        return x == other.x && y == other.y && z == other.z;
    }
};

struct VertexKeyHash {
    std::size_t operator()(const VertexKey& k) const noexcept;
};

} // namespace detail

/// A triangle mesh being assembled, and the writer for RocketSim's `.cmf` format.
///
/// Vertices are deduplicated as they are added, so callers can emit quads and
/// boxes freely without worrying about sharing corners.
class CollisionMeshBuilder {
public:
    /// Adds a vertex and returns its index, reusing an existing one if a vertex
    /// at the same position (within a small tolerance) was already added.
    int AddVertex(float x, float y, float z);

    /// Adds one triangle by vertex index. Winding order does not matter to
    /// RocketSim: Bullet treats these as two-sided static collision surfaces.
    void AddTriangle(int a, int b, int c);

    /// Adds a flat quad as two triangles. Corners must be given in order around
    /// the quad, either clockwise or counter-clockwise.
    void AddQuad(
        float ax, float ay, float az,
        float bx, float by, float bz,
        float cx, float cy, float cz,
        float dx, float dy, float dz);

    /// Adds all six faces of an axis-aligned box.
    void AddBox(float minX, float minY, float minZ, float maxX, float maxY, float maxZ);

    std::size_t VertexCount() const { return vertices.size() / 3; }
    std::size_t TriangleCount() const { return triangles.size() / 3; }

    /// Writes the mesh to `path` in RocketSim's `.cmf` format, creating parent
    /// directories as needed. Returns false and sets `errorOut` on failure.
    bool WriteToFile(const std::filesystem::path& path, std::string& errorOut) const;

private:
    std::vector<float> vertices;    // flat xyz triples
    std::vector<int32_t> triangles; // flat index triples

    /// Maps an already-seen vertex position to its index, so AddVertex can weld
    /// duplicates in constant time. Owned per builder so two builders never
    /// share state.
    std::unordered_map<detail::VertexKey, int, detail::VertexKeyHash> weld;
};

/// The field dimensions the Practice Arena is built from, in Rocket League's
/// "unreal units". These are the published soccar measurements, and they match
/// the constants RocketSim itself uses for goal detection and boost pad placement.
struct PracticeArenaDimensions {
    float halfWidth = 4096.0f;      ///< wall-to-wall in x, from the centre
    float halfLength = 5120.0f;     ///< goal-line to goal-line in y, from the centre
    float ceilingHeight = 2044.0f;  ///< floor to ceiling in z

    float goalHalfWidth = 893.0f;   ///< goal mouth half-width in x
    float goalHeight = 642.775f;    ///< goal mouth height in z
    float goalDepth = 880.0f;       ///< how far the net extends beyond the goal line

    /// How far the 45-degree corner chamfers cut into the field. The real arena
    /// has smoothly curved corners; a single flat chamfer is a close-enough
    /// approximation that still stops the ball from sticking in a sharp corner.
    float cornerChamfer = 1152.0f;

    /// The same chamfer applied where the walls meet the floor and ceiling,
    /// approximating the ramps a real arena has there.
    float edgeChamfer = 256.0f;
};

/// Builds the Practice Arena mesh.
CollisionMeshBuilder BuildPracticeArena(const PracticeArenaDimensions& dims = {});

/// Builds the Practice Arena and writes it where RocketSim will find it:
/// `<collisionMeshesDir>/soccar/practice_arena.cmf`.
///
/// Returns false and fills `errorOut` on failure.
bool WritePracticeArena(
    const std::filesystem::path& collisionMeshesDir,
    std::string& errorOut,
    const PracticeArenaDimensions& dims = {});

} // namespace rls
