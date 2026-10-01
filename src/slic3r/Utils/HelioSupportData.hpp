#ifndef slic3r_HelioSupportData_hpp_
#define slic3r_HelioSupportData_hpp_

#include "HelioRetryPolicy.hpp"

#include <functional>
#include <memory>
#include <mutex>
#include <cstdint>
#include <string>
#include <vector>

namespace Slic3r {

struct HelioSupportedData
{
    std::string id;
    std::string name;
    std::string native_name;
    std::string feedstock;
    bool        heated_chamber{false};
};

enum class SupportDataLoadState
{
    NotLoaded,
    Loading,
    Ready,
    Failed
};

enum class SupportDataCatalogKind
{
    Printers,
    Materials
};

struct SupportDataHttpResponse
{
    unsigned    status{0};
    std::string body;
    std::string error;
    std::string trace_id;
};

struct SupportDataPageResult
{
    bool                            success{false};
    bool                            has_next_page{false};
    int                             total_pages{0};
    unsigned                        status{0};
    HelioRetryKind                  retry_kind{HelioRetryKind::None};
    std::vector<HelioSupportedData> items;
    std::string                     error;
    std::string                     trace_id;
};

SupportDataPageResult parse_support_data_page(SupportDataCatalogKind       kind,
                                              int                          page,
                                              const SupportDataHttpResponse& response);

struct SupportDataCatalogView
{
    using Snapshot = std::shared_ptr<const std::vector<HelioSupportedData>>;

    SupportDataLoadState state{SupportDataLoadState::NotLoaded};
    Snapshot             snapshot;
    std::string          last_error;

    bool has_usable_snapshot() const noexcept { return snapshot && !snapshot->empty(); }
};

enum class SupportDataAvailability
{
    Usable,
    Synchronizing,
    LoadFailed
};

SupportDataAvailability support_data_availability(const SupportDataCatalogView& printers,
                                                  const SupportDataCatalogView& materials) noexcept;


struct SupportDataLoadAttempt
{
    SupportDataCatalogKind kind{SupportDataCatalogKind::Printers};
    int                    page{1};
    int                    attempt{1};
    int                    max_attempts{4};
    unsigned               status{0};
    HelioRetryKind         retry_kind{HelioRetryKind::None};
    bool                   will_retry{false};
    std::string            error;
    std::string            trace_id;
};

class SupportDataCatalogStore
{
public:
    using Snapshot     = SupportDataCatalogView::Snapshot;
    using PageFetcher  = std::function<SupportDataHttpResponse(SupportDataCatalogKind, int)>;
    using RetrySleeper = std::function<void(int)>;
    using Logger       = std::function<void(const SupportDataLoadAttempt&)>;

    static constexpr int MAX_ATTEMPTS_PER_PAGE = 4;

    explicit SupportDataCatalogStore(SupportDataCatalogKind kind);

    SupportDataCatalogStore(const SupportDataCatalogStore&) = delete;
    SupportDataCatalogStore& operator=(const SupportDataCatalogStore&) = delete;

    // Starts a load if one is needed. A previous immutable snapshot is retained
    // while a forced refresh is in progress. Overlapping loads are always
    // rejected, including overlapping forced refreshes.
    bool try_begin(bool force_refresh = false);

    // Runs a load accepted by try_begin(). The fetcher is synchronous so the
    // production caller can choose its background execution mechanism while
    // tests can supply deterministic responses and a no-op sleeper.
    bool run(const PageFetcher&  fetcher,
             const RetrySleeper& sleeper = RetrySleeper(),
             const Logger&       logger = Logger());

    SupportDataCatalogKind kind() const noexcept { return m_kind; }
    SupportDataLoadState   state() const;
    Snapshot               snapshot() const;
    std::string            last_error() const;
    bool                   has_usable_snapshot() const;
    SupportDataCatalogView view() const;

private:
    bool claim_run();
    bool run_impl(const PageFetcher& fetcher, const RetrySleeper& sleeper, const Logger& logger);
    void publish(std::vector<HelioSupportedData>&& complete_snapshot);
    void fail(std::string error);

    const SupportDataCatalogKind m_kind;
    mutable std::mutex           m_mutex;
    SupportDataLoadState         m_state{SupportDataLoadState::NotLoaded};
    Snapshot                     m_snapshot;
    std::string                  m_last_error;
    bool                         m_run_claimed{false};
};

// A coherent, immutable observation of both support-data catalogs. Source and
// generation are intentionally opaque: callers can use them only to compare
// views, never to expose credentials.
struct SupportDataCatalogPairView
{
    using Snapshot = SupportDataCatalogView::Snapshot;

    Snapshot                printers;
    Snapshot                materials;
    SupportDataLoadState    printers_state{SupportDataLoadState::NotLoaded};
    SupportDataLoadState    materials_state{SupportDataLoadState::NotLoaded};
    std::string             printers_last_error;
    std::string             materials_last_error;
    SupportDataAvailability availability{SupportDataAvailability::Synchronizing};
    std::uint64_t           source_generation{0};
};

class SupportDataCatalogPairCoordinator
{
public:
    using PageFetcher  = SupportDataCatalogStore::PageFetcher;
    using RetrySleeper = SupportDataCatalogStore::RetrySleeper;
    using Logger       = SupportDataCatalogStore::Logger;

    bool try_begin(const std::string& api_url, const std::string& api_key, bool force_refresh = false,
                   std::uint64_t* generation = nullptr);
    bool run(std::uint64_t generation, const PageFetcher& fetcher,
             const RetrySleeper& sleeper = RetrySleeper(), const Logger& logger = Logger());
    SupportDataCatalogPairView view() const;
    std::uint64_t active_generation() const;

private:
    bool is_active_locked(std::uint64_t generation) const noexcept;
    void fail_active(std::uint64_t generation, std::string error);

    mutable std::mutex         m_mutex;
    std::string                m_api_url;
    std::string                m_api_key;
    std::uint64_t              m_generation{0};
    bool                       m_loading{false};
    SupportDataCatalogPairView m_view;
};

} // namespace Slic3r

#endif // slic3r_HelioSupportData_hpp_
