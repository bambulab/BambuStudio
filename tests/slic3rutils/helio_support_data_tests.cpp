#include <catch2/catch.hpp>

#include "slic3r/Utils/HelioSupportData.hpp"

#include "nlohmann/json.hpp"

#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace {

using Slic3r::SupportDataCatalogKind;
using Slic3r::SupportDataCatalogStore;
using Slic3r::SupportDataHttpResponse;

SupportDataHttpResponse printer_page(int page,
                                     int total_pages,
                                     bool has_next_page,
                                     std::initializer_list<std::pair<std::string, std::string>> printers)
{
    nlohmann::json objects = nlohmann::json::array();
    for (const auto& printer : printers) {
        objects.push_back({
            {"id", printer.first},
            {"name", printer.second},
            {"heatedChamber", false},
            {"alternativeNames", {{"bambustudio", printer.second}}}
        });
    }

    nlohmann::json body = {
        {"data", {{"printers", {
            {"pages", total_pages},
            {"pageInfo", {{"hasNextPage", has_next_page}}},
            {"objects", std::move(objects)}
        }}}}
    };
    return {200, body.dump(), {}, "trace-printers-" + std::to_string(page)};
}

SupportDataHttpResponse material_page(int page,
                                      int total_pages,
                                      bool has_next_page,
                                      std::initializer_list<std::pair<std::string, std::string>> materials)
{
    nlohmann::json objects = nlohmann::json::array();
    for (const auto& material : materials) {
        objects.push_back({
            {"id", material.first},
            {"name", material.second},
            {"feedstock", "FILAMENT"},
            {"alternativeNames", {{"bambustudio", material.second}}}
        });
    }

    nlohmann::json body = {
        {"data", {{"materials", {
            {"pages", total_pages},
            {"pageInfo", {{"hasNextPage", has_next_page}}},
            {"objects", std::move(objects)}
        }}}}
    };
    return {200, body.dump(), {}, "trace-materials-" + std::to_string(page)};
}

SupportDataHttpResponse graphql_error(const std::string& message,
                                      const std::string& trace_id = "trace-graphql")
{
    return {200, nlohmann::json({{"errors", {{{"message", message}}}}}).dump(), {}, trace_id};
}

void seed_printers(SupportDataCatalogStore& store,
                   const std::string& id = "printer-old",
                   const std::string& name = "Bambu Lab Old")
{
    REQUIRE(store.try_begin());
    REQUIRE(store.run([&](SupportDataCatalogKind kind, int page) {
        REQUIRE(kind == SupportDataCatalogKind::Printers);
        REQUIRE(page == 1);
        return printer_page(1, 1, false, {{id, name}});
    }));
}

void seed_materials(SupportDataCatalogStore& store,
                    const std::string& id = "material-old",
                    const std::string& name = "Old PLA")
{
    REQUIRE(store.try_begin());
    REQUIRE(store.run([&](SupportDataCatalogKind kind, int page) {
        REQUIRE(kind == SupportDataCatalogKind::Materials);
        REQUIRE(page == 1);
        return material_page(1, 1, false, {{id, name}});
    }));
}

void require_failed_response(const SupportDataHttpResponse& response, int expected_fetches)
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    int fetches = 0;
    REQUIRE(store.try_begin());
    REQUIRE_FALSE(store.run([&](SupportDataCatalogKind, int) {
        ++fetches;
        return response;
    }));
    REQUIRE(fetches == expected_fetches);
    REQUIRE(store.state() == SupportDataLoadState::Failed);
    REQUIRE_FALSE(store.snapshot());
    REQUIRE_FALSE(store.last_error().empty());
}

bool contains_native_name(const SupportDataCatalogStore::Snapshot& snapshot, const std::string& name)
{
    return snapshot && std::any_of(snapshot->begin(), snapshot->end(), [&](const Slic3r::HelioSupportedData& item) {
        return item.native_name == name;
    });
}

} // namespace

TEST_CASE("Helio support catalog publishes complete pagination atomically", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    std::vector<int> requested_pages;

    REQUIRE(store.try_begin());
    REQUIRE(store.state() == SupportDataLoadState::Loading);
    REQUIRE_FALSE(store.snapshot());

    REQUIRE(store.run([&](SupportDataCatalogKind kind, int page) {
        REQUIRE(kind == SupportDataCatalogKind::Printers);
        requested_pages.push_back(page);
        return page == 1
            ? printer_page(1, 2, true, {{"printer-1", "Bambu Lab P1S"}})
            : printer_page(2, 2, false, {{"printer-2", "Bambu Lab X1C"}});
    }));

    REQUIRE(requested_pages == std::vector<int>{1, 2});
    REQUIRE(store.state() == SupportDataLoadState::Ready);
    REQUIRE(store.last_error().empty());

    const auto snapshot = store.snapshot();
    REQUIRE(snapshot);
    REQUIRE(snapshot->size() == 2);
    REQUIRE((*snapshot)[0].id == "printer-1");
    REQUIRE((*snapshot)[1].id == "printer-2");
}

TEST_CASE("Helio support catalog recovers transient pages with bounded backoff", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Materials);
    std::vector<int> sleeps;
    std::vector<SupportDataLoadAttempt> attempts;
    int fetches = 0;

    REQUIRE(store.try_begin());
    REQUIRE(store.run(
        [&](SupportDataCatalogKind, int) {
            ++fetches;
            if (fetches == 1) return SupportDataHttpResponse{503, {}, "service unavailable", "trace-503"};
            if (fetches == 2) return graphql_error("Service temporarily unavailable");
            if (fetches == 3) return graphql_error("Request deadline exceeded");
            return material_page(1, 1, false, {{"material-1", "Bambu PLA Basic"}});
        },
        [&](int seconds) { sleeps.push_back(seconds); },
        [&](const SupportDataLoadAttempt& attempt) { attempts.push_back(attempt); }));

    REQUIRE(fetches == 4);
    REQUIRE(sleeps == std::vector<int>{2, 4, 6});
    REQUIRE(attempts.size() == 3);
    REQUIRE(attempts[0].attempt == 1);
    REQUIRE(attempts[0].status == 503);
    REQUIRE(attempts[0].retry_kind == HelioRetryKind::Transient);
    REQUIRE(attempts[0].trace_id == "trace-503");
    REQUIRE(attempts[2].attempt == 3);
    REQUIRE(attempts[2].will_retry);
    REQUIRE(store.state() == SupportDataLoadState::Ready);
}

TEST_CASE("Helio support catalog recovers the reported subgraph deserialization failure",
          "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Materials);
    std::vector<int> sleeps;
    std::vector<SupportDataLoadAttempt> attempts;
    int fetches = 0;

    REQUIRE(store.try_begin());
    REQUIRE(store.run(
        [&](SupportDataCatalogKind kind, int page) {
            REQUIRE(kind == SupportDataCatalogKind::Materials);
            REQUIRE(page == 1);
            ++fetches;
            if (fetches == 1) {
                return graphql_error("Failed to deserialize subgraph response", "trace-subgraph-first");
            }
            return material_page(1, 1, false, {{"material-1", "Bambu PLA Basic"}});
        },
        [&](int seconds) { sleeps.push_back(seconds); },
        [&](const SupportDataLoadAttempt& attempt) { attempts.push_back(attempt); }));

    REQUIRE(fetches == 2);
    REQUIRE(sleeps == std::vector<int>{2});
    REQUIRE(attempts.size() == 1);
    REQUIRE(attempts[0].attempt == 1);
    REQUIRE(attempts[0].retry_kind == HelioRetryKind::Transient);
    REQUIRE(attempts[0].will_retry);
    REQUIRE(attempts[0].error == "Failed to deserialize subgraph response");
    REQUIRE(attempts[0].trace_id == "trace-subgraph-first");
    REQUIRE(store.state() == SupportDataLoadState::Ready);
    REQUIRE(store.last_error().empty());
    REQUIRE(store.snapshot());
    REQUIRE(store.snapshot()->size() == 1);
    REQUIRE((*store.snapshot())[0].id == "material-1");
}

TEST_CASE("Helio support catalog rejects malformed and incomplete successful responses", "[Helio][SupportData]")
{
    SECTION("malformed JSON is terminal")
    {
        require_failed_response({200, "not-json", {}, "trace-malformed"}, 1);
    }

    SECTION("recognized transient GraphQL errors retry")
    {
        require_failed_response(graphql_error("Request temporarily unavailable"), 4);
    }

    SECTION("unrecognized GraphQL errors are terminal")
    {
        require_failed_response(graphql_error("Invalid printer selection"), 1);
    }

    SECTION("missing payload is transient")
    {
        require_failed_response({200, R"({"data":{}})", {}, "trace-missing"}, 4);
    }

    SECTION("empty GraphQL errors without a payload are malformed and transient")
    {
        require_failed_response({200, R"({"errors":[]})", {}, "trace-empty-errors"}, 4);
        require_failed_response({200, R"({"errors":null})", {}, "trace-null-errors"}, 4);
    }

    SECTION("empty object pages are transient")
    {
        require_failed_response(printer_page(1, 1, false, {}), 4);
    }

    SECTION("missing page metadata is transient")
    {
        const std::string body = R"({"data":{"printers":{"objects":[{"id":"p1","name":"P1"}]}}})";
        require_failed_response({200, body, {}, "trace-metadata"}, 4);
    }

    SECTION("inconsistent pagination is transient")
    {
        require_failed_response(printer_page(1, 2, false, {{"p1", "P1"}}), 4);
    }
}

TEST_CASE("Helio support catalog never retries terminal HTTP authentication statuses", "[Helio][SupportData]")
{
    for (const unsigned status : {401u, 403u, 404u}) {
        DYNAMIC_SECTION("HTTP " << status)
        {
            require_failed_response({status, "unauthorized", "request failed", "trace-terminal"}, 1);
        }
    }
}

TEST_CASE("Helio support catalog does not publish a successful partial prefix", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    int page_one_fetches = 0;
    int page_two_fetches = 0;

    REQUIRE(store.try_begin());
    REQUIRE_FALSE(store.run([&](SupportDataCatalogKind, int page) {
        if (page == 1) {
            ++page_one_fetches;
            return printer_page(1, 2, true, {{"partial-printer", "Partial Printer"}});
        }
        ++page_two_fetches;
        return SupportDataHttpResponse{503, {}, "unavailable", "trace-page-two"};
    }));

    REQUIRE(page_one_fetches == 1);
    REQUIRE(page_two_fetches == 4);
    REQUIRE(store.state() == SupportDataLoadState::Failed);
    REQUIRE_FALSE(store.snapshot());
}

TEST_CASE("Helio support catalog retains a good snapshot after forced refresh failure", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    seed_printers(store);
    const auto original = store.snapshot();

    REQUIRE(store.try_begin(true));
    REQUIRE(store.state() == SupportDataLoadState::Loading);
    REQUIRE(store.snapshot() == original);

    REQUIRE_FALSE(store.run([](SupportDataCatalogKind, int) {
        return SupportDataHttpResponse{401, "unauthorized", {}, "trace-refresh"};
    }));

    REQUIRE(store.state() == SupportDataLoadState::Failed);
    REQUIRE(store.snapshot() == original);
    REQUIRE(store.has_usable_snapshot());
    REQUIRE((*store.snapshot())[0].id == "printer-old");
}

TEST_CASE("Helio support catalog exhausts subgraph retries without publishing a forced refresh prefix",
          "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    seed_printers(store);
    const auto original = store.snapshot();
    std::vector<int> sleeps;
    std::vector<SupportDataLoadAttempt> attempts;
    int page_one_fetches = 0;
    int page_two_fetches = 0;

    REQUIRE(store.try_begin(true));
    REQUIRE_FALSE(store.run(
        [&](SupportDataCatalogKind kind, int page) {
            REQUIRE(kind == SupportDataCatalogKind::Printers);
            if (page == 1) {
                ++page_one_fetches;
                return printer_page(1, 2, true, {{"printer-new", "Bambu Lab New"}});
            }
            ++page_two_fetches;
            return graphql_error("FAILED TO DESERIALIZE SUBGRAPH RESPONSE",
                                 "trace-subgraph-" + std::to_string(page_two_fetches));
        },
        [&](int seconds) { sleeps.push_back(seconds); },
        [&](const SupportDataLoadAttempt& attempt) { attempts.push_back(attempt); }));

    REQUIRE(page_one_fetches == 1);
    REQUIRE(page_two_fetches == SupportDataCatalogStore::MAX_ATTEMPTS_PER_PAGE);
    REQUIRE(sleeps == std::vector<int>{2, 4, 6});
    REQUIRE(attempts.size() == SupportDataCatalogStore::MAX_ATTEMPTS_PER_PAGE);
    for (std::size_t index = 0; index < attempts.size(); ++index) {
        INFO("attempt " << index + 1);
        REQUIRE(attempts[index].page == 2);
        REQUIRE(attempts[index].attempt == static_cast<int>(index + 1));
        REQUIRE(attempts[index].max_attempts == SupportDataCatalogStore::MAX_ATTEMPTS_PER_PAGE);
        REQUIRE(attempts[index].retry_kind == HelioRetryKind::Transient);
        REQUIRE(attempts[index].error == "FAILED TO DESERIALIZE SUBGRAPH RESPONSE");
        REQUIRE(attempts[index].trace_id == "trace-subgraph-" + std::to_string(index + 1));
        REQUIRE(attempts[index].will_retry == (index + 1 < attempts.size()));
    }
    REQUIRE(store.state() == SupportDataLoadState::Failed);
    REQUIRE(store.last_error() == "FAILED TO DESERIALIZE SUBGRAPH RESPONSE");
    REQUIRE(store.snapshot() == original);
    REQUIRE(store.snapshot()->size() == 1);
    REQUIRE((*store.snapshot())[0].id == "printer-old");
}

TEST_CASE("Helio support catalog requires force to replace a completed snapshot", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
    seed_printers(store);
    const auto original = store.snapshot();

    REQUIRE_FALSE(store.try_begin(false));
    REQUIRE(store.snapshot() == original);

    REQUIRE(store.try_begin(true));
    REQUIRE(store.run([](SupportDataCatalogKind, int) {
        return printer_page(1, 1, false, {{"printer-new", "Bambu Lab New"}});
    }));

    REQUIRE(store.state() == SupportDataLoadState::Ready);
    REQUIRE(store.snapshot() != original);
    REQUIRE(store.snapshot()->size() == 1);
    REQUIRE((*store.snapshot())[0].id == "printer-new");
}

TEST_CASE("Helio support catalog suppresses overlapping normal and forced requests", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore store(SupportDataCatalogKind::Materials);
    REQUIRE(store.try_begin());
    REQUIRE(store.state() == SupportDataLoadState::Loading);
    REQUIRE_FALSE(store.try_begin(false));
    REQUIRE_FALSE(store.try_begin(true));

    REQUIRE(store.run([](SupportDataCatalogKind, int) {
        return material_page(1, 1, false, {{"material-1", "PLA"}});
    }));
    REQUIRE_FALSE(store.try_begin(false));
}

TEST_CASE("Helio support catalog retries exact backend auth only once", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SECTION("one exact backend auth response recovers")
    {
        SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
        std::vector<int> sleeps;
        int fetches = 0;

        REQUIRE(store.try_begin());
        REQUIRE(store.run(
            [&](SupportDataCatalogKind, int) {
                ++fetches;
                if (fetches == 1) return graphql_error(HELIO_BACKEND_AUTH_401_MESSAGE);
                return printer_page(1, 1, false, {{"p1", "P1"}});
            },
            [&](int seconds) { sleeps.push_back(seconds); }));

        REQUIRE(fetches == 2);
        REQUIRE(sleeps == std::vector<int>{2});
        REQUIRE(store.state() == SupportDataLoadState::Ready);
    }

    SECTION("a repeated exact backend auth response stops")
    {
        SupportDataCatalogStore store(SupportDataCatalogKind::Printers);
        std::vector<int> sleeps;
        int fetches = 0;

        REQUIRE(store.try_begin());
        REQUIRE_FALSE(store.run(
            [&](SupportDataCatalogKind, int) {
                ++fetches;
                return graphql_error(HELIO_BACKEND_AUTH_401_MESSAGE);
            },
            [&](int seconds) { sleeps.push_back(seconds); }));

        REQUIRE(fetches == 2);
        REQUIRE(sleeps == std::vector<int>{2});
        REQUIRE(store.state() == SupportDataLoadState::Failed);
    }
}

TEST_CASE("Helio catalog availability distinguishes unavailable from genuinely unsupported", "[Helio][SupportData]")
{
    using namespace Slic3r;

    SupportDataCatalogStore printers(SupportDataCatalogKind::Printers);
    SupportDataCatalogStore materials(SupportDataCatalogKind::Materials);

    REQUIRE(support_data_availability(printers.view(), materials.view()) ==
            SupportDataAvailability::Synchronizing);

    seed_printers(printers, "unrelated-printer", "Unrelated Printer");
    REQUIRE(support_data_availability(printers.view(), materials.view()) ==
            SupportDataAvailability::Synchronizing);

    seed_materials(materials, "unrelated-material", "Unrelated Material");
    REQUIRE(support_data_availability(printers.view(), materials.view()) ==
            SupportDataAvailability::Usable);
    REQUIRE_FALSE(contains_native_name(materials.snapshot(), "Bambu PLA Basic"));

    const auto stale_materials = materials.snapshot();
    REQUIRE(materials.try_begin(true));
    REQUIRE_FALSE(materials.run([](SupportDataCatalogKind, int) {
        return SupportDataHttpResponse{403, "forbidden", {}, "trace-stale"};
    }));
    REQUIRE(materials.snapshot() == stale_materials);
    REQUIRE(materials.state() == SupportDataLoadState::Failed);
    REQUIRE(support_data_availability(printers.view(), materials.view()) ==
            SupportDataAvailability::Usable);

    SupportDataCatalogStore failed_without_snapshot(SupportDataCatalogKind::Materials);
    REQUIRE(failed_without_snapshot.try_begin());
    REQUIRE_FALSE(failed_without_snapshot.run([](SupportDataCatalogKind, int) {
        return SupportDataHttpResponse{404, "not found", {}, "trace-failed"};
    }));
    REQUIRE(support_data_availability(printers.view(), failed_without_snapshot.view()) ==
            SupportDataAvailability::LoadFailed);
}

TEST_CASE("Helio support catalog pair publishes one source atomically", "[Helio][SupportData]")
{
    using namespace Slic3r;
    SupportDataCatalogPairCoordinator coordinator;
    bool saw_no_half = false;

    REQUIRE(coordinator.try_begin("A", "key-A"));
    const auto generation_a = coordinator.active_generation();
    const auto loading = coordinator.view();
    REQUIRE(loading.availability == SupportDataAvailability::Synchronizing);
    REQUIRE_FALSE(loading.printers);
    REQUIRE_FALSE(loading.materials);
    REQUIRE(coordinator.run(generation_a, [&](SupportDataCatalogKind kind, int) {
        if (kind == SupportDataCatalogKind::Materials) {
            const auto during_load = coordinator.view();
            saw_no_half = !during_load.printers && !during_load.materials;
            return material_page(1, 1, false, {{"material-A", "material-A"}});
        }
        return printer_page(1, 1, false, {{"printer-A", "printer-A"}});
    }));
    const auto ready_a = coordinator.view();
    REQUIRE(ready_a.availability == SupportDataAvailability::Usable);
    REQUIRE((*ready_a.printers)[0].id == "printer-A");
    REQUIRE((*ready_a.materials)[0].id == "material-A");
    REQUIRE(saw_no_half);
    REQUIRE(coordinator.try_begin("A", "key-A", true));
    REQUIRE_FALSE(coordinator.run(coordinator.active_generation(), [](SupportDataCatalogKind kind, int) {
        return kind == SupportDataCatalogKind::Printers
            ? printer_page(1, 1, false, {{"printer-new", "printer-new"}})
            : SupportDataHttpResponse{401, "unauthorized", {}, "trace"};
    }));
    const auto rollback = coordinator.view();
    REQUIRE(rollback.availability == SupportDataAvailability::Usable);
    REQUIRE((*rollback.printers)[0].id == "printer-A");
    REQUIRE((*rollback.materials)[0].id == "material-A");
    REQUIRE(coordinator.try_begin("A", "key-B"));
    const auto generation_b = coordinator.active_generation();
    const auto hidden_a = coordinator.view();
    REQUIRE(hidden_a.availability == SupportDataAvailability::Synchronizing);
    REQUIRE_FALSE(hidden_a.printers);
    REQUIRE_FALSE(hidden_a.materials);
    REQUIRE_FALSE(coordinator.run(generation_b, [](SupportDataCatalogKind, int) {
        return SupportDataHttpResponse{403, "forbidden", {}, "trace"};
    }));
    const auto failed_b = coordinator.view();
    REQUIRE(failed_b.availability == SupportDataAvailability::LoadFailed);
    REQUIRE_FALSE(failed_b.printers);
    REQUIRE_FALSE(failed_b.materials);
}

TEST_CASE("Helio support catalog pair rejects stale generations and never mixes catalogs", "[Helio][SupportData]")
{
    using namespace Slic3r;
    SupportDataCatalogPairCoordinator coordinator;
    REQUIRE(coordinator.try_begin("A", "key-A"));
    const auto generation_a = coordinator.active_generation();
    REQUIRE(coordinator.try_begin("B", "key-B"));
    const auto generation_b = coordinator.active_generation();
    REQUIRE_FALSE(coordinator.run(generation_a, [](SupportDataCatalogKind kind, int) {
        return kind == SupportDataCatalogKind::Printers
            ? printer_page(1, 1, false, {{"printer-A", "printer-A"}})
            : material_page(1, 1, false, {{"material-A", "material-A"}});
    }));
    const auto before_b = coordinator.view();
    REQUIRE_FALSE(before_b.printers);
    REQUIRE_FALSE(before_b.materials);
    REQUIRE(coordinator.run(generation_b, [](SupportDataCatalogKind kind, int) {
        return kind == SupportDataCatalogKind::Printers
            ? printer_page(1, 1, false, {{"printer-B", "printer-B"}})
            : material_page(1, 1, false, {{"material-B", "material-B"}});
    }));
    const auto ready_b = coordinator.view();
    REQUIRE(ready_b.availability == SupportDataAvailability::Usable);
    REQUIRE((*ready_b.printers)[0].id == "printer-B");
    REQUIRE((*ready_b.materials)[0].id == "material-B");
}
