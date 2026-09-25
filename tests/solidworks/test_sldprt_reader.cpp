#define CATCH_CONFIG_MAIN
#include "../catch2/catch.hpp"
#include "fixture.hpp"
#include "SLDPRTReader.hpp"
#include <algorithm>
#include <fstream>
#include <limits>
#include <random>
#include <sstream>

using namespace Slic3r::SolidWorks;
using namespace SldprtFixture;

TEST_CASE("Native mesh dimensions, strip winding, alignment and name encoding", "[sldprt]") {
    for (unsigned rotation = 0; rotation < 8; ++rotation) {
        for (unsigned padding = 0; padding < 4; ++padding) {
            const Mesh m = decode(document(face(padding), rotation));
            REQUIRE(m.vertices.size() == 4);
            REQUIRE(m.indices == std::vector<uint32_t>{0, 1, 2, 2, 1, 3});
            REQUIRE(m.vertices[3][0] == Approx(20));
            REQUIRE(m.vertices[3][1] == Approx(10));
            REQUIRE(m.vertices[3][2] == 0);
        }
    }
}

TEST_CASE("Multiple faces and duplicate container records", "[sldprt]") {
    Bytes a = face(), b = face(3, .04f);
    a.insert(a.end(), b.begin(), b.end());
    Bytes file = document(a);
    const auto original = file;
    file.insert(file.end(), original.begin() + 16, original.end());
    REQUIRE(decode(file).indices.size() == 12);
    REQUIRE(decode(file).vertices.size() == 8);
    const auto different = document(face());
    file.insert(file.end(), different.begin() + 16, different.end());
    REQUIRE_THROWS_WITH(decode(file), Catch::Contains("multiple different"));
}

TEST_CASE("Empty, unsupported and damaged documents fail clearly", "[sldprt]") {
    REQUIRE_THROWS_AS(decode({}), std::runtime_error);
    REQUIRE_THROWS_AS(decode(Bytes(80, 0)), std::runtime_error);
    REQUIRE_THROWS_WITH(decode({0xd0,0xcf,0x11,0xe0,0xa1,0xb1,0x1a,0xe1}), Catch::Contains("legacy OLE"));
    REQUIRE_THROWS_WITH(decode(document({}, 4, "Contents/Other")), Catch::Contains("no saved display"));
    REQUIRE_THROWS_WITH(decode(document(Bytes(100, 0))), Catch::Contains("no supported triangles"));
    Bytes file = document();
    file.pop_back();
    REQUIRE_THROWS_WITH(decode(file), Catch::Contains("truncated"));
    file = document(); set_word(file, 16 + 22, 1); // output exceeds declaration
    REQUIRE_THROWS_WITH(decode(file), Catch::Contains("damaged"));
    file = document(); set_word(file, 16 + 22, 0xffffffffu);
    REQUIRE_THROWS_WITH(decode(file), Catch::Contains("decompression limit"));
    file = document(); set_word(file, 16 + 18, 0xffffffffu);
    REQUIRE_THROWS_WITH(decode(file), Catch::Contains("truncated"));
    file = document(); file.back() ^= 0xff;
    REQUIRE_THROWS_AS(decode(file), std::runtime_error);
}

TEST_CASE("Validate geometry channels without returning partial geometry", "[sldprt]") {
    Bytes b = face(); b.resize(36 + 48 + 8); // incomplete normal descriptor
    REQUIRE_THROWS_AS(decode(document(b)), std::runtime_error);
    b = face(); set_word(b, 32, 5); // position count != strip lengths
    REQUIRE_THROWS_WITH(decode(document(b)), Catch::Contains("position count"));
    Bytes mixed = face(); mixed.insert(mixed.end(), b.begin(), b.end());
    REQUIRE_THROWS_WITH(decode(document(mixed)), Catch::Contains("position count"));
    b = face(); set_word(b, 16, 2); // an invalid strip must not omit a face
    mixed = face(); mixed.insert(mixed.end(), b.begin(), b.end());
    REQUIRE_THROWS_WITH(decode(document(mixed)), Catch::Contains("triangle strip"));
    b = face(); b.resize(18); // truncated strip array after a valid face
    mixed = face(); mixed.insert(mixed.end(), b.begin(), b.end());
    REQUIRE_THROWS_WITH(decode(document(mixed)), Catch::Contains("display array"));
    b = face(); set_word(b, 84 + 12, 5); // normal count != position count
    REQUIRE_THROWS_WITH(decode(document(b)), Catch::Contains("normal count"));
    b = face(); set_word(b, 36, 0x7fc00000); // NaN position
    REQUIRE_THROWS_WITH(decode(document(b)), Catch::Contains("invalid coordinates"));
    b = face(); set_word(b, 100, 0x7f800000); // infinite normal
    REQUIRE_THROWS_WITH(decode(document(b)), Catch::Contains("invalid coordinates"));
    // Normal *directions* are not a reliable geometry discriminator. Empty
    // or zero normals still have a valid positions/strips mesh.
    b = face(); std::fill(b.begin() + 100, b.begin() + 148, 0);
    REQUIRE(decode(document(b)).indices.size() == 6);
    b = face(); b.resize(148); // auxiliary edge channels may be absent
    REQUIRE(decode(document(b)).indices.size() == 6);
    b = face(); const Bytes valid = b; b.resize(70);
    Bytes combined = valid; combined.insert(combined.end(), b.begin(), b.end());
    REQUIRE_THROWS_AS(decode(document(combined)), std::runtime_error);
}

TEST_CASE("Cancellation and stream reads", "[sldprt]") {
    unsigned previous = 0;
    REQUIRE(decode(document(), [&](unsigned p) { REQUIRE(p >= previous); previous = p; return true; }).indices.size() == 6);
    REQUIRE(previous == 100);
    for (unsigned threshold : {0u, 40u, 100u})
        REQUIRE_THROWS_AS(decode(document(), [=](unsigned p) { return p < threshold; }), Cancelled);
    const Bytes bytes = document();
    std::istringstream in(std::string(bytes.begin(), bytes.end()));
    REQUIRE(read(in).vertices.size() == 4);
    std::istringstream empty;
    REQUIRE_THROWS_AS(read(empty), std::runtime_error);
}

TEST_CASE("Real SolidWorks 2018 part from the public NIST corpus", "[sldprt]") {
    std::ifstream file(std::string(SLDPRT_TEST_DATA_DIR) + "/nist_ftc_06_asme1_rd_sw1802.SLDPRT", std::ios::binary);
    const auto mesh = read(file);
    REQUIRE(mesh.indices.size() / 3 == 7098);
    auto lo = mesh.vertices.front(), hi = lo;
    for (const auto &v : mesh.vertices)
        for (unsigned j = 0; j < 3; ++j) { lo[j] = std::min(lo[j], v[j]); hi[j] = std::max(hi[j], v[j]); }
    REQUIRE(hi[0] - lo[0] == Approx(304.8).margin(.001));
    REQUIRE(hi[1] - lo[1] == Approx(97.79).margin(.001));
    REQUIRE(hi[2] - lo[2] == Approx(247.65).margin(.001));
}

TEST_CASE("Truncated inputs and deterministic mutations stay bounded", "[sldprt]") {
    const Bytes valid = document();
    for (size_t i = 0; i < valid.size(); ++i)
        REQUIRE_THROWS_AS(decode(Bytes(valid.begin(), valid.begin() + i)), std::runtime_error);
    std::mt19937 random(173);
    for (unsigned i = 0; i < 1000; ++i) {
        Bytes bytes = valid;
        bytes[random() % bytes.size()] ^= static_cast<unsigned char>(1 + random() % 255);
        try { decode(bytes); } catch (const std::runtime_error &) {}
    }
}
