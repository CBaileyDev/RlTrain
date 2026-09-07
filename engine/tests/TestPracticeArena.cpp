// Tests for the Practice Arena generator.
//
// These matter more than they look. The Practice Arena is the first thing a new
// user runs, and a malformed collision mesh does not fail loudly: RocketSim
// loads it, the simulation runs, and cars quietly fall through the floor. So we
// check the file format byte for byte against what RocketSim's reader expects,
// and we check the geometry actually encloses the play area.

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include "sim/PracticeArena.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <limits>
#include <vector>

using namespace rls;

namespace {

/// A parsed .cmf file, read back with the same layout RocketSim uses:
///   int32 triangleCount, int32 vertexCount, then the indices, then the floats.
struct ParsedMesh {
    int32_t triangleCount = 0;
    int32_t vertexCount = 0;
    std::vector<int32_t> indices;
    std::vector<float> coords;
};

ParsedMesh ReadCmf(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());

    auto readI32 = [&in]() {
        char b[4];
        in.read(b, 4);
        int32_t v = 0;
        std::memcpy(&v, b, 4);
        return v;
    };
    auto readF32 = [&in]() {
        char b[4];
        in.read(b, 4);
        float v = 0;
        std::memcpy(&v, b, 4);
        return v;
    };

    ParsedMesh mesh;
    mesh.triangleCount = readI32();
    mesh.vertexCount = readI32();

    mesh.indices.reserve(static_cast<std::size_t>(mesh.triangleCount) * 3);
    for (int32_t i = 0; i < mesh.triangleCount * 3; ++i)
        mesh.indices.push_back(readI32());

    mesh.coords.reserve(static_cast<std::size_t>(mesh.vertexCount) * 3);
    for (int32_t i = 0; i < mesh.vertexCount * 3; ++i)
        mesh.coords.push_back(readF32());

    REQUIRE(in.good());
    return mesh;
}

/// A scratch directory that cleans itself up.
struct TempDir {
    std::filesystem::path path;
    explicit TempDir(const char* name)
        : path(std::filesystem::temp_directory_path() / name) {
        std::filesystem::remove_all(path);
        std::filesystem::create_directories(path);
    }
    ~TempDir() {
        std::error_code ec;
        std::filesystem::remove_all(path, ec);
    }
};

} // namespace

TEST_CASE("CollisionMeshBuilder welds duplicate vertices", "[practice-arena]") {
    CollisionMeshBuilder mesh;

    const int a = mesh.AddVertex(1.0f, 2.0f, 3.0f);
    const int b = mesh.AddVertex(1.0f, 2.0f, 3.0f);
    CHECK(a == b);
    CHECK(mesh.VertexCount() == 1);

    // Distinct enough to be a separate vertex.
    const int c = mesh.AddVertex(1.0f, 2.0f, 4.0f);
    CHECK(c != a);
    CHECK(mesh.VertexCount() == 2);
}

TEST_CASE("CollisionMeshBuilder drops degenerate triangles", "[practice-arena]") {
    CollisionMeshBuilder mesh;
    const int a = mesh.AddVertex(0, 0, 0);
    const int b = mesh.AddVertex(1, 0, 0);

    mesh.AddTriangle(a, a, b);  // two shared corners: no surface
    CHECK(mesh.TriangleCount() == 0);

    const int c = mesh.AddVertex(0, 1, 0);
    mesh.AddTriangle(a, b, c);
    CHECK(mesh.TriangleCount() == 1);
}

TEST_CASE("A quad becomes two triangles over four vertices", "[practice-arena]") {
    CollisionMeshBuilder mesh;
    mesh.AddQuad(0, 0, 0,
                 1, 0, 0,
                 1, 1, 0,
                 0, 1, 0);
    CHECK(mesh.VertexCount() == 4);
    CHECK(mesh.TriangleCount() == 2);
}

TEST_CASE("A box has six faces and eight shared corners", "[practice-arena]") {
    CollisionMeshBuilder mesh;
    mesh.AddBox(-1, -1, -1, 1, 1, 1);
    // Welding must collapse the 24 emitted corners down to the cube's real 8.
    CHECK(mesh.VertexCount() == 8);
    CHECK(mesh.TriangleCount() == 12);
}

TEST_CASE("The practice arena is a well-formed mesh", "[practice-arena]") {
    const CollisionMeshBuilder mesh = BuildPracticeArena();

    CHECK(mesh.VertexCount() > 0);
    CHECK(mesh.TriangleCount() > 0);

    // RocketSim rejects a file whose counts fall outside this range.
    CHECK(mesh.TriangleCount() < 1000 * 1000);
    CHECK(mesh.VertexCount() < 1000 * 1000);
}

TEST_CASE("The practice arena writes a file RocketSim can parse", "[practice-arena]") {
    TempDir tmp("rlstudio_practice_arena_test");
    std::string error;

    REQUIRE(WritePracticeArena(tmp.path, error));
    CHECK(error.empty());

    // RocketSim only loads meshes from <dir>/<gamemode>/ with a .cmf extension.
    const std::filesystem::path written = tmp.path / "soccar" / "practice_arena.cmf";
    REQUIRE(std::filesystem::exists(written));

    const ParsedMesh parsed = ReadCmf(written);

    SECTION("counts are positive and within RocketSim's accepted range") {
        CHECK(parsed.triangleCount > 0);
        CHECK(parsed.vertexCount > 0);
        CHECK(parsed.triangleCount <= 1000 * 1000);
        CHECK(parsed.vertexCount <= 1000 * 1000);
    }

    SECTION("the file is exactly the size the header implies") {
        const auto expected =
            static_cast<std::uintmax_t>(8)
            + static_cast<std::uintmax_t>(parsed.triangleCount) * 3 * 4
            + static_cast<std::uintmax_t>(parsed.vertexCount) * 3 * 4;
        CHECK(std::filesystem::file_size(written) == expected);
    }

    SECTION("every triangle index is in range") {
        // RocketSim validates this and calls RS_ERR_CLOSE, which terminates the
        // process, so getting it wrong is fatal rather than merely incorrect.
        for (int32_t index : parsed.indices) {
            CHECK(index >= 0);
            CHECK(index < parsed.vertexCount);
        }
    }

    SECTION("no coordinate is NaN or infinite") {
        for (float c : parsed.coords)
            CHECK(std::isfinite(c));
    }
}

TEST_CASE("The practice arena encloses the play area", "[practice-arena]") {
    TempDir tmp("rlstudio_practice_arena_bounds");
    std::string error;
    REQUIRE(WritePracticeArena(tmp.path, error));

    const ParsedMesh parsed = ReadCmf(tmp.path / "soccar" / "practice_arena.cmf");

    float minX = std::numeric_limits<float>::max(), maxX = -minX;
    float minY = minX, maxY = -minX;
    float minZ = minX, maxZ = -minX;
    for (std::size_t i = 0; i + 2 < parsed.coords.size(); i += 3) {
        minX = std::min(minX, parsed.coords[i]);      maxX = std::max(maxX, parsed.coords[i]);
        minY = std::min(minY, parsed.coords[i + 1]);  maxY = std::max(maxY, parsed.coords[i + 1]);
        minZ = std::min(minZ, parsed.coords[i + 2]);  maxZ = std::max(maxZ, parsed.coords[i + 2]);
    }

    const PracticeArenaDimensions dims;

    // Walls sit at the field edges.
    CHECK_THAT(minX, Catch::Matchers::WithinAbs(-dims.halfWidth, 1.0));
    CHECK_THAT(maxX, Catch::Matchers::WithinAbs(dims.halfWidth, 1.0));

    // The mesh extends past the goal line by the depth of the net, at both ends.
    CHECK_THAT(minY, Catch::Matchers::WithinAbs(-(dims.halfLength + dims.goalDepth), 1.0));
    CHECK_THAT(maxY, Catch::Matchers::WithinAbs(dims.halfLength + dims.goalDepth, 1.0));

    // Floor at zero, ceiling at the stated height.
    CHECK_THAT(minZ, Catch::Matchers::WithinAbs(0.0, 1.0));
    CHECK_THAT(maxZ, Catch::Matchers::WithinAbs(dims.ceilingHeight, 1.0));
}

TEST_CASE("Writing fails cleanly rather than crashing", "[practice-arena]") {
    CollisionMeshBuilder empty;
    std::string error;

    // An empty mesh would make RocketSim's reader abort the process, so the
    // builder must refuse to write one.
    CHECK_FALSE(empty.WriteToFile(
        std::filesystem::temp_directory_path() / "rlstudio_should_not_exist.cmf", error));
    CHECK_FALSE(error.empty());
}
