#include "gca/dialect.hpp"
#include "gca/knowledge.hpp"
#include "gca/modal_state.hpp"
#include "test_helpers.hpp"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

using namespace gca;
using Catch::Approx;

namespace {

struct Fixture {
    Profile profile{*findDialect("fanuc-mill"), nullptr};
    ModalMachine m{profile};
    std::size_t line = 0;
    BlockEffect run(const char* text) { return m.apply(lexLine(text, ++line)); }
    const ModalState& st() const { return m.state(); }
};

} // namespace

TEST_CASE("dialect registry and code keys", "[dialect]") {
    CHECK(findDialect("fanuc-mill") != nullptr);
    CHECK(findDialect("nope") == nullptr);
    CHECK(canonicalCode('G', 5.1) == "G5.1");
    CHECK(canonicalCode('G', 0.0) == "G0");
    CHECK(canonicalCode('G', 54.1) == "G54.1");
    CHECK(canonicalCode('M', 6.0) == "M6");
    CHECK(displayCode("G5.1") == "G05.1");
    CHECK(displayCode("M6") == "M06");
    CHECK(displayCode("G54.1") == "G54.1");
    CHECK(displayCode("G83") == "G83");
}

TEST_CASE("profile knows Fanuc codes and machine overlays", "[dialect]") {
    const IDialect& d = *findDialect("fanuc-mill");
    CHECK(d.g(81.0)->cls == GClass::Cycle);
    CHECK(d.g(5.1).has_value());
    CHECK_FALSE(d.g(123.0).has_value());
    CHECK_FALSE(d.m(15).has_value());
    CHECK_FALSE(d.m(53).has_value());

    const MachineProfile* sky = detectMachine(d, "Sky Master VL1100A");
    REQUIRE(sky != nullptr);
    const MachineProfile* bp = detectMachine(d, "Bridgeport 1300/480 3-Axis");
    REQUIRE(bp != nullptr);
    CHECK(detectMachine(d, "Unknown machine") == nullptr);

    const Profile ps(d, sky);
    CHECK(ps.describe('M', 15.0).has_value());
    CHECK(ps.describe('M', 15.0)->machineSpecific);
    CHECK_FALSE(ps.describe('M', 53.0).has_value());
    const Profile pb(d, bp);
    CHECK(pb.describe('M', 53.0)->machineSpecific);
    CHECK_FALSE(pb.describe('M', 15.0).has_value());
}

TEST_CASE("knowledge chain: builtin answers, online stub does not", "[knowledge]") {
    const IDialect& d = *findDialect("fanuc-mill");
    KnowledgeChain chain;
    chain.add(std::make_unique<BuiltinKnowledgeProvider>(Profile(d, nullptr)));
    chain.add(std::make_unique<OnlineKnowledgeProviderStub>());
    CHECK(chain.lookup("fanuc-mill", "G83").has_value());
    CHECK_FALSE(chain.lookup("fanuc-mill", "G999").has_value());
    CHECK_FALSE(chain.lookup("other", "G83").has_value());
    CHECK_FALSE(OnlineKnowledgeProviderStub{}.lookup("fanuc-mill", "G83").has_value());
}

TEST_CASE("modal: distance, feed mode, plane, units", "[modal]") {
    Fixture f;
    CHECK(f.st().dist == DistanceMode::Absolute);
    f.run("G91");
    CHECK(f.st().dist == DistanceMode::Incremental);
    f.run("G90 G95 G18");
    CHECK(f.st().dist == DistanceMode::Absolute);
    CHECK(f.st().feedMode == FeedMode::PerRev);
    CHECK(f.st().plane == Plane::ZX);
    f.run("G94 G17 G20");
    CHECK(f.st().feedMode == FeedMode::PerMinute);
    CHECK(f.st().plane == Plane::XY);
    CHECK(f.st().inch);
    f.run("G21");
    CHECK_FALSE(f.st().inch);
}

TEST_CASE("modal: work coordinate systems", "[modal]") {
    Fixture f;
    CHECK_FALSE(f.st().wcsSelected);
    f.run("G90 G54 G0 X1. Y1.");
    CHECK(f.st().wcs == "G54");
    f.run("G55");
    CHECK(f.st().wcs == "G55");
    f.run("G54.1 P3");
    CHECK(f.st().wcs == "G54.1 P3");
    CHECK(f.st().wcsP == 3);
    f.run("G54 P7");
    CHECK(f.st().wcs == "G54.1 P7");
    f.run("G54.1 P0");
    CHECK(f.st().wcsP == 0);
    f.run("G54");
    CHECK(f.st().wcs == "G54");
    CHECK(f.st().wcsP == -1);
}

TEST_CASE("modal: tool change, preselect, order of T and M6", "[modal][tools]") {
    Fixture f;
    auto e = f.run("T1M6");
    REQUIRE(e.toolEvents.size() == 1);
    CHECK(e.toolEvents[0].kind == ToolEvent::Kind::Change);
    CHECK(e.toolEvents[0].tool == 1);
    CHECK(e.toolEvents[0].tBeforeM6);
    CHECK(f.st().activeTool == 1);

    e = f.run("T3");
    REQUIRE(e.toolEvents.size() == 1);
    CHECK(e.toolEvents[0].kind == ToolEvent::Kind::Preselect);
    CHECK(f.st().activeTool == 1);
    CHECK(f.st().preselectedTool == 3);

    e = f.run("M6");
    REQUIRE(e.toolEvents.size() == 1);
    CHECK(e.toolEvents[0].tool == 3);
    CHECK_FALSE(e.toolEvents[0].hadTWord);
    CHECK(f.st().activeTool == 3);

    e = f.run("M6T4");
    REQUIRE(e.toolEvents.size() == 1);
    CHECK(e.toolEvents[0].tool == 4);
    CHECK_FALSE(e.toolEvents[0].tBeforeM6);
    CHECK(e.toolEvents[0].previousTool == 3);
}

TEST_CASE("modal: S, F, spindle, coolant", "[modal]") {
    Fixture f;
    f.run("G43 Z32. H1 D1 S5305.165 M3");
    CHECK(f.st().s == 5305.165);
    CHECK(f.st().spindle == SpindleDir::Cw);
    CHECK(f.st().h == 1);
    CHECK(f.st().d == 1);
    CHECK(f.st().lengthComp);
    f.run("G1 Z11.5 F1764. M8");
    CHECK(f.st().f == Approx(1764.0));
    CHECK(f.st().coolant.count("M8") == 1);
    f.run("M9");
    CHECK(f.st().coolant.empty());
    f.run("G0 M5");
    CHECK(f.st().spindle == SpindleDir::Off);
    f.run("M4");
    CHECK(f.st().spindle == SpindleDir::Ccw);
    f.run("G49 H0");
    CHECK_FALSE(f.st().lengthComp);
}

TEST_CASE("modal: position, modality of G1 and coordinates", "[modal][position]") {
    Fixture f;
    for (int a = 0; a < kAxisCount; ++a) CHECK_FALSE(f.st().pos.known[static_cast<std::size_t>(a)]);
    auto e = f.run("G0 X10. Y20.");
    CHECK(e.segments.size() == 1);
    CHECK(f.st().pos.known[kAxisX]);
    CHECK_FALSE(f.st().pos.known[kAxisZ]);
    f.run("Z5.");
    CHECK(f.st().pos.v[kAxisZ] == Approx(5.0));
    e = f.run("G1 Y30. F100.");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::Linear);
    CHECK(e.segments[0].from.v[kAxisY] == Approx(20.0));
    CHECK(e.segments[0].to.v[kAxisY] == Approx(30.0));
    e = f.run("X15.");     // G1 stays modal
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::Linear);
    f.run("G91");
    f.run("G1 X-5. Z1.");
    CHECK(f.st().pos.v[kAxisX] == Approx(10.0));
    CHECK(f.st().pos.v[kAxisZ] == Approx(6.0));
}

TEST_CASE("modal: G91 G28 makes axes unknown and flags dangerous G91", "[modal][position]") {
    Fixture f;
    f.run("G90 G0 X10. Y20. Z30.");
    auto e = f.run("G91 G28 Z0");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::Home);
    CHECK_FALSE(f.st().pos.known[kAxisZ]);
    CHECK(f.st().pos.known[kAxisX]);
    CHECK(f.st().g91FromG28);
    f.run("G91 G28 Y0");
    CHECK_FALSE(f.st().pos.known[kAxisY]);
    f.run("G90");
    CHECK_FALSE(f.st().g91FromG28);
    f.run("Z5.");
    CHECK(f.st().pos.known[kAxisZ]);
}

TEST_CASE("modal: G80 returns group 01 to rapid, cycles are modal", "[modal][cycle]") {
    Fixture f;
    f.run("G90 G0 X5. Y5. Z20.");
    f.run("G1 F100.");
    auto e = f.run("G98 G81 Z-5. R2. F50.");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::CycleHole);
    CHECK(e.segments[0].cycle.zInit == Approx(20.0));
    CHECK(e.segments[0].cycle.zR == Approx(2.0));
    CHECK(e.segments[0].cycle.zBottom == Approx(-5.0));
    CHECK(f.st().pos.v[kAxisZ] == Approx(20.0));
    e = f.run("X10.");     // modal cycle: next hole
    REQUIRE(e.segments.size() == 2);
    CHECK(e.segments[0].kind == SegmentKind::Rapid);
    CHECK(e.segments[1].kind == SegmentKind::CycleHole);
    f.run("G80");
    CHECK(f.st().cycleCode.empty());
    e = f.run("X20.");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::Rapid);
}

TEST_CASE("modal: arc segment keeps I J K", "[modal][arc]") {
    Fixture f;
    f.run("G90 G17 G0 X0. Y0. Z0.");
    const auto e = f.run("G3 X-10. Y10. Z1. I-10. J0. F300.");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].kind == SegmentKind::Arc);
    CHECK_FALSE(e.segments[0].arc.cw);
    CHECK(e.segments[0].arc.i.value() == Approx(-10.0));
    CHECK(e.segments[0].arc.j.value() == Approx(0.0));
    CHECK_FALSE(e.segments[0].arc.k.has_value());
}

TEST_CASE("modal: dwell", "[modal]") {
    Fixture f;
    auto e = f.run("G4 P500");
    REQUIRE(e.segments.size() == 1);
    CHECK(e.segments[0].dwellSeconds == Approx(0.5));
    e = f.run("G4 X2.");
    CHECK(e.segments[0].dwellSeconds == Approx(2.0));
    e = f.run("G4 P1.5");
    CHECK(e.segments[0].dwellSeconds == Approx(1.5));
}
