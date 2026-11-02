#include "exchangecore/order_book.hpp"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cmath>
#include <cstdlib>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <limits>
#include <numeric>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace exchangecore;

namespace {
using Clock = std::chrono::steady_clock;

struct Options {
    std::size_t orders = 100'000;
    std::uint64_t seed = 31;
    std::string workload = "mixed";
    unsigned repeats = 3;
    std::size_t reserve_ids = 0;
    std::string csv;
    std::string latency_csv;
    bool profile_grid = false;
    unsigned profile_rounds = 5;
    std::string profile_csv;
    std::string profile_latency_csv;
};

// A small, fully specified PRNG avoids implementation-dependent distributions.
class SplitMix64 {
public:
    explicit SplitMix64(std::uint64_t seed) : state_(seed) {}
    std::uint64_t next() {
        std::uint64_t z = (state_ += 0x9E3779B97F4A7C15ULL);
        z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ULL;
        z = (z ^ (z >> 27)) * 0x94D049BB133111EBULL;
        return z ^ (z >> 31);
    }
    std::uint64_t below(std::uint64_t bound) { return next() % bound; }
private:
    std::uint64_t state_;
};

struct Event {
    bool is_cancel = false;
    Order order{};
    OrderId cancel_id = 0;
};

// Tracks *remaining* quantity so generated cancellations always target a live ID.
class LiveOrders {
public:
    [[nodiscard]] bool empty() const { return ids_.empty(); }
    [[nodiscard]] std::size_t size() const { return ids_.size(); }
    OrderId random_id(SplitMix64& rng) const {
        return ids_.at(static_cast<std::size_t>(rng.below(ids_.size())));
    }
    void add(OrderId id, Quantity quantity) {
        if (!positions_.emplace(id, Live{ids_.size(), quantity}).second)
            throw std::runtime_error("generator added duplicate live ID");
        ids_.push_back(id);
    }
    void remove(OrderId id) {
        auto it = positions_.find(id);
        if (it == positions_.end()) throw std::runtime_error("generator removed unknown ID");
        const std::size_t pos = it->second.index;
        const OrderId last_id = ids_.back();
        ids_[pos] = last_id;
        positions_.at(last_id).index = pos;
        ids_.pop_back();
        positions_.erase(it);
    }
    void fill(OrderId id, Quantity quantity) {
        auto it = positions_.find(id);
        if (it == positions_.end() || quantity > it->second.quantity)
            throw std::runtime_error("generator fill exceeds maker's live quantity");
        it->second.quantity -= quantity;
        if (it->second.quantity == 0) remove(id);
    }
private:
    struct Live { std::size_t index; Quantity quantity; };
    std::vector<OrderId> ids_;
    std::unordered_map<OrderId, Live> positions_;
};

Order make_order(OrderId id, bool aggressive, SplitMix64& rng, const OrderBook& book) {
    const Side side = rng.below(2) == 0 ? Side::Buy : Side::Sell;
    PriceTicks price = 0;
    if (aggressive && side == Side::Buy) {
        if (const auto ask = book.best_ask())
            price = ask->price_ticks + static_cast<PriceTicks>(rng.below(4));
    } else if (aggressive) {
        if (const auto bid = book.best_bid())
            price = bid->price_ticks - static_cast<PriceTicks>(rng.below(4));
    }
    // Passive prices are intentionally separated to generate non-crossing liquidity.
    if (price <= 0)
        price = side == Side::Buy ? 9'900 + static_cast<PriceTicks>(rng.below(90))
                                  : 10'100 + static_cast<PriceTicks>(rng.below(90));
    return {id, side, price, 1 + rng.below(100)};
}

std::vector<Event> generate_events(const Options& options, std::size_t* peak_live = nullptr) {
    SplitMix64 rng(options.seed);
    OrderBook reference;
    LiveOrders live;
    std::vector<Event> events;
    events.reserve(options.orders);
    OrderId next_id = 1;

    for (std::size_t i = 0; i < options.orders; ++i) {
        const std::uint64_t roll = rng.below(100);
        if ((options.workload == "mixed" || options.workload == "cancel-heavy") &&
            roll < (options.workload == "mixed" ? 15U : 45U) && !live.empty()) {
            const OrderId id = live.random_id(rng);
            if (!reference.cancel(id)) throw std::runtime_error("generator cancel failed");
            live.remove(id);
            events.push_back(Event{true, {}, id});
        } else {
            const bool aggressive = options.workload == "crossing" ||
                (options.workload == "mixed" && roll < 60) ||
                (options.workload == "cancel-heavy" && roll < 55);
            const Order order = make_order(next_id++, aggressive, rng, reference);
            const Submission result = reference.submit(order);
            if (result.status != SubmitStatus::Accepted ||
                result.executed_quantity + result.resting_quantity != order.quantity)
                throw std::runtime_error("generator submitted invalid order");
            for (const Trade& trade : result.trades) live.fill(trade.maker_id, trade.quantity);
            if (result.resting_quantity != 0) live.add(order.id, result.resting_quantity);
            events.push_back(Event{false, order, 0});
        }
        if (live.size() != reference.order_count())
            throw std::runtime_error("generator live ID count inconsistent with book");
        if (peak_live != nullptr) *peak_live = std::max(*peak_live, live.size());
    }
    return events;
}

struct Metrics {
    std::uint64_t submitted = 0;
    std::uint64_t cancelled = 0;
    std::uint64_t trades = 0;
    std::uint64_t units_executed = 0;
    std::uint64_t remaining_orders = 0;
    std::uint64_t checksum = 14695981039346656037ULL;
    [[nodiscard]] bool operator==(const Metrics&) const = default;
};

void mix(Metrics& metrics, std::uint64_t value) {
    metrics.checksum ^= value;
    metrics.checksum *= 1099511628211ULL;
}

void apply_event(OrderBook& book, const Event& event, Metrics& metrics) {
    if (event.is_cancel) {
        if (!book.cancel(event.cancel_id))
            throw std::runtime_error("replay cancel failed");
        ++metrics.cancelled;
        mix(metrics, event.cancel_id);
    } else {
        const Submission result = book.submit(event.order);
        if (result.status != SubmitStatus::Accepted ||
            result.executed_quantity + result.resting_quantity != event.order.quantity)
            throw std::runtime_error("replay submission failed validation");
        ++metrics.submitted;
        metrics.trades += result.trades.size();
        metrics.units_executed += result.executed_quantity;
        mix(metrics, event.order.id);
        mix(metrics, result.resting_quantity);
        for (const auto& trade : result.trades) {
            mix(metrics, trade.maker_id);
            mix(metrics, trade.price_ticks);
            mix(metrics, trade.quantity);
        }
    }
}

struct ThroughputRun { double seconds; Metrics metrics; };
ThroughputRun run_throughput(const std::vector<Event>& events, std::size_t reserve_ids) {
    OrderBook book;
    if (reserve_ids != 0) book.reserve_order_ids(reserve_ids);
    Metrics metrics;
    const auto begin = Clock::now();
    for (const auto& event : events) apply_event(book, event, metrics);
    const auto finish = Clock::now();
    metrics.remaining_orders = book.order_count();
    return {std::chrono::duration<double>(finish - begin).count(), metrics};
}

struct LatencyRun { std::vector<std::uint64_t> nanoseconds; Metrics metrics; };
LatencyRun run_latency(const std::vector<Event>& events, std::size_t reserve_ids) {
    OrderBook book;
    if (reserve_ids != 0) book.reserve_order_ids(reserve_ids);
    Metrics metrics;
    std::vector<std::uint64_t> samples;
    samples.reserve(events.size());
    for (const auto& event : events) {
        // Latency pass measures only the book operation; metrics are recorded outside.
        if (event.is_cancel) {
            const auto start = Clock::now();
            const bool ok = book.cancel(event.cancel_id);
            const auto end = Clock::now();
            if (!ok) throw std::runtime_error("latency replay cancel failed");
            samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            ++metrics.cancelled;
            mix(metrics, event.cancel_id);
        } else {
            const auto start = Clock::now();
            const Submission result = book.submit(event.order);
            const auto end = Clock::now();
            if (result.status != SubmitStatus::Accepted ||
                result.executed_quantity + result.resting_quantity != event.order.quantity)
                throw std::runtime_error("latency replay submit failed");
            samples.push_back(static_cast<std::uint64_t>(
                std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count()));
            ++metrics.submitted;
            metrics.trades += result.trades.size();
            metrics.units_executed += result.executed_quantity;
            mix(metrics, event.order.id);
            mix(metrics, result.resting_quantity);
            for (const auto& trade : result.trades) {
                mix(metrics, trade.maker_id);
                mix(metrics, trade.price_ticks);
                mix(metrics, trade.quantity);
            }
        }
    }
    metrics.remaining_orders = book.order_count();
    return {std::move(samples), metrics};
}

std::uint64_t percentile(const std::vector<std::uint64_t>& sorted, std::uint64_t num,
                         std::uint64_t den) {
    // Nearest rank, with integer arithmetic and no floating-point ambiguity.
    const std::size_t rank = static_cast<std::size_t>(
        (sorted.size() * num + den - 1) / den);
    return sorted.at(std::max<std::size_t>(1, rank) - 1);
}

std::uint64_t parse_uint(std::string_view input, const char* flag) {
    std::uint64_t result = 0;
    const auto [end, error] = std::from_chars(input.data(), input.data() + input.size(), result);
    if (error != std::errc{} || end != input.data() + input.size())
        throw std::runtime_error(std::string("invalid value for ") + flag);
    return result;
}

void print_usage() {
    std::cout << "Usage: exchangecore_bench [--orders N] [--seed N] "
                 "[--workload passive|crossing|mixed|cancel-heavy] [--repeats N] "
                 "[--reserve-ids N] [--csv PATH] [--latency-csv PATH]\n"
                 "  Profiling: [--profile-grid auto] [--profile-rounds N] "
                 "[--profile-csv PATH] [--profile-latency-csv PATH]\n"
                 "Defaults: --orders 100000 --seed 31 --workload mixed --repeats 3\n";
}

Options parse_options(int argc, char** argv) {
    Options options;
    for (int i = 1; i < argc; ++i) {
        const std::string_view flag(argv[i]);
        if (flag == "--help" || flag == "-h") { print_usage(); std::exit(0); }
        if (i + 1 == argc) throw std::runtime_error("missing value for " + std::string(flag));
        const std::string_view value(argv[++i]);
        if (flag == "--orders") options.orders = static_cast<std::size_t>(parse_uint(value, "--orders"));
        else if (flag == "--seed") options.seed = parse_uint(value, "--seed");
        else if (flag == "--repeats") {
            const auto requested = parse_uint(value, "--repeats");
            if (requested < 1 || requested > 10)
                throw std::runtime_error("--repeats must be between 1 and 10");
            options.repeats = static_cast<unsigned>(requested);
        }
        else if (flag == "--reserve-ids") {
            const auto count = parse_uint(value, "--reserve-ids");
            if (count > 2'000'000) throw std::runtime_error("--reserve-ids must be <= 2000000");
            options.reserve_ids = static_cast<std::size_t>(count);
        }
        else if (flag == "--workload") options.workload = std::string(value);
        else if (flag == "--csv") options.csv = std::string(value);
        else if (flag == "--latency-csv") options.latency_csv = std::string(value);
        else if (flag == "--profile-grid") {
            if (value != "auto") throw std::runtime_error("--profile-grid requires auto");
            options.profile_grid = true;
        }
        else if (flag == "--profile-rounds") {
            const auto n = parse_uint(value, "--profile-rounds");
            if (n < 1 || n > 10) throw std::runtime_error("--profile-rounds must be 1..10");
            options.profile_rounds = static_cast<unsigned>(n);
        }
        else if (flag == "--profile-csv") options.profile_csv = std::string(value);
        else if (flag == "--profile-latency-csv") options.profile_latency_csv = std::string(value);
        else throw std::runtime_error("unknown argument " + std::string(flag));
    }
    if (options.orders == 0 || options.orders > 2'000'000)
        throw std::runtime_error("--orders must be between 1 and 2000000");
    if (options.repeats < 1 || options.repeats > 10)
        throw std::runtime_error("--repeats must be between 1 and 10");
    if (options.workload != "passive" && options.workload != "crossing" && options.workload != "mixed" && options.workload != "cancel-heavy")
        throw std::runtime_error("--workload must be passive, crossing, mixed, or cancel-heavy");
    return options;
}

std::string compiler_name() {
#if defined(_MSC_VER)
    return "MSVC " + std::to_string(_MSC_VER);
#elif defined(__clang__)
    return std::string("Clang ") + __clang_version__;
#elif defined(__GNUC__)
    return std::string("GCC ") + __VERSION__;
#else
    return "unknown";
#endif
}

void save_csv(const Options& options, const Metrics& m, double median_seconds,
              double median_throughput, double mean_ns, std::uint64_t p50,
              std::uint64_t p95, std::uint64_t p99, std::uint64_t p999) {
    if (options.csv.empty()) return;
    const std::filesystem::path path(options.csv);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const bool add_header = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream file(path, std::ios::app);
    if (!file) throw std::runtime_error("cannot open CSV output path");
    if (add_header)
        file << "compiler,build_type,workload,seed,operations,repeats,median_seconds,median_ops_per_sec,"
                "mean_latency_ns,p50_ns,p95_ns,p99_ns,p999_ns,submitted,cancelled,trades,"
                "units_executed,remaining_orders,checksum,preflight_mode,reserve_ids,cancel_mode\n";
    file << compiler_name() << ',';
#ifdef NDEBUG
    file << "Release";
#else
    file << "Debug";
#endif
    file << ',' << options.workload << ',' << options.seed << ',' << options.orders << ','
         << options.repeats << ',' << std::fixed << std::setprecision(6)
         << median_seconds << ',' << std::setprecision(2) << median_throughput << ','
         << mean_ns << ',' << p50 << ',' << p95 << ',' << p99 << ',' << p999 << ','
         << m.submitted << ',' << m.cancelled << ',' << m.trades << ',' << m.units_executed
         << ',' << m.remaining_orders << ',' << m.checksum << ','
#ifdef EXCHANGECORE_LEGACY_PREFLIGHT
         << "legacy"
#else
         << "selective"
#endif
         << ',' << options.reserve_ids << ','
#ifdef EXCHANGECORE_LEGACY_CANCEL
         << "legacy"
#else
         << "cached-level"
#endif
         << '\n';
    if (!file) throw std::runtime_error("failed writing benchmark CSV");
}

// Diagnostic output only; throughput measurement remains unchanged.
// Each decile is reported in chronological order rather than sorting the entire replay.
// A separate cancel/submit split helps distinguish expensive operation types.
void save_latency_diagnostics(const Options& options, const std::vector<Event>& events,
                              const std::vector<std::uint64_t>& samples) {
    if (options.latency_csv.empty()) return;
    const std::filesystem::path path(options.latency_csv);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const bool header = !std::filesystem::exists(path) || std::filesystem::file_size(path) == 0;
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open latency diagnostics CSV");
    if (header) out << "workload,seed,operations,reserve_ids,decile,operation,count,mean_ns,p50_ns,p99_ns,p999_ns,max_ns,over_10us\n";
    for (std::size_t decile = 0; decile < 10; ++decile) {
        const std::size_t begin = events.size() * decile / 10;
        const std::size_t end = events.size() * (decile + 1) / 10;
        for (int kind = 0; kind < 3; ++kind) {
            std::vector<std::uint64_t> group;
            group.reserve(end - begin);
            for (std::size_t i = begin; i < end; ++i) {
                if (kind == 0 || (kind == 1 && !events[i].is_cancel) ||
                    (kind == 2 && events[i].is_cancel)) group.push_back(samples[i]);
            }
            if (group.empty()) continue;
            const auto expensive = static_cast<std::size_t>(std::count_if(
                group.begin(), group.end(), [](std::uint64_t ns) { return ns > 10000; }));
            const long double sum = std::accumulate(group.begin(), group.end(), 0.0L);
            std::sort(group.begin(), group.end());
            out << options.workload << ',' << options.seed << ',' << events.size() << ','
                << options.reserve_ids << ',' << decile + 1 << ','
                << (kind == 0 ? "all" : kind == 1 ? "submit" : "cancel") << ','
                << group.size() << ',' << std::fixed << std::setprecision(2)
                << static_cast<double>(sum / group.size()) << ','
                << percentile(group, 50, 100) << ',' << percentile(group, 99, 100)
                << ',' << percentile(group, 999, 1000) << ',' << group.back() << ','
                << expensive << '\n';
        }
    }
    if (!out) throw std::runtime_error("failed writing latency diagnostics CSV");
}


// v0.7 experiment: interleave 0, observed-peak and maximum-op reservation.
// The event stream is generated only once and is identical for every configuration.
struct ProfileSample {
    std::uint64_t ns;
    std::size_t live;
    std::size_t buckets;
    std::size_t price_levels;
    bool cancel;
    bool removed_level;
    bool rehash;
};


std::vector<ProfileSample> profile_latency(const std::vector<Event>& events,
                                           std::size_t reserve_ids, Metrics& outcome) {
    OrderBook book;
    if (reserve_ids) book.reserve_order_ids(reserve_ids);
    std::vector<ProfileSample> samples;
    samples.reserve(events.size());
    for (const Event& event : events) {
        const BookStats before = book.stats(); // Always outside timed interval.
        if (event.is_cancel) {
            const auto start = Clock::now();
            const bool ok = book.cancel(event.cancel_id);
            const auto stop = Clock::now();
            if (!ok) throw std::runtime_error("profile cancel failed");
            const BookStats after = book.stats();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
            samples.push_back({static_cast<std::uint64_t>(ns), after.live_orders,
                after.id_buckets, after.bid_levels + after.ask_levels, true,
                after.bid_levels + after.ask_levels < before.bid_levels + before.ask_levels,
                after.id_buckets != before.id_buckets});
            ++outcome.cancelled;
            mix(outcome, event.cancel_id);
        } else {
            const auto start = Clock::now();
            const Submission result = book.submit(event.order);
            const auto stop = Clock::now();
            if (result.status != SubmitStatus::Accepted ||
                result.executed_quantity + result.resting_quantity != event.order.quantity)
                throw std::runtime_error("profile submit failed");
            const BookStats after = book.stats();
            const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(stop-start).count();
            samples.push_back({static_cast<std::uint64_t>(ns), after.live_orders,
                after.id_buckets, after.bid_levels + after.ask_levels, false, false,
                after.id_buckets != before.id_buckets});
            ++outcome.submitted;
            outcome.trades += result.trades.size();
            outcome.units_executed += result.executed_quantity;
            mix(outcome, event.order.id);
            mix(outcome, result.resting_quantity);
            for (const auto& trade : result.trades) {
                mix(outcome, trade.maker_id);
                mix(outcome, trade.price_ticks);
                mix(outcome, trade.quantity);
            }
        }
    }
    outcome.remaining_orders = book.order_count();
    return samples;
}

// The diagnostics are descriptive, not attributed CPU profiles or allocation counts.
void save_profile_latency(const Options& options, const std::vector<ProfileSample>& samples,
                          std::size_t reserve, std::size_t peak_live) {
    if (options.profile_latency_csv.empty()) return;
    const std::filesystem::path path(options.profile_latency_csv);
    if (!path.parent_path().empty()) std::filesystem::create_directories(path.parent_path());
    const bool header = !std::filesystem::exists(path) || std::filesystem::file_size(path)==0;
    std::ofstream out(path, std::ios::app);
    if (!out) throw std::runtime_error("cannot open profile latency CSV");
    if (header)
        out << "workload,seed,operations,peak_live,reserve_ids,decile,kind,count,mean_ns,p50_ns,p99_ns,p999_ns,max_ns,over10us,"
               "observed_rehashes,cancel_level_removals,start_live,end_live,end_price_levels,end_buckets\n";
    for (std::size_t d = 0; d < 10; ++d) {
        const std::size_t first = samples.size() * d / 10;
        const std::size_t end = samples.size() * (d+1) / 10;
        if (first == end) continue;
        const auto rehashes = static_cast<std::size_t>(std::count_if(samples.begin()+first, samples.begin()+end,
            [](const ProfileSample& x){return x.rehash;}));
        const auto prunes = static_cast<std::size_t>(std::count_if(samples.begin()+first, samples.begin()+end,
            [](const ProfileSample& x){return x.cancel && x.removed_level;}));
        for (int kind=0; kind<5; ++kind) {
            std::vector<std::uint64_t> group;
            group.reserve(end-first);
            for (std::size_t i=first; i<end; ++i) {
                const ProfileSample& x=samples[i];
                const bool selected = kind == 0 || (kind == 1 && !x.cancel) ||
                    (kind == 2 && x.cancel && !x.removed_level) ||
                    (kind == 3 && x.cancel && x.removed_level) ||
                    (kind == 4 && x.rehash);
                if (selected) group.push_back(x.ns);
            }
            if (group.empty()) continue;
            const auto over10us = static_cast<std::size_t>(std::count_if(group.begin(), group.end(),
                [](std::uint64_t ns){return ns > 10'000;}));
            const long double sum = std::accumulate(group.begin(), group.end(), 0.0L);
            std::sort(group.begin(), group.end());
            static const char* names[] = {"all","submit","cancel_kept_level","cancel_removed_level","index_rehash"};
            out << options.workload << ',' << options.seed << ',' << samples.size() << ',' << peak_live
                << ',' << reserve << ',' << d+1 << ',' << names[kind] << ',' << group.size() << ','
                << std::fixed << std::setprecision(2) << static_cast<double>(sum/group.size()) << ','
                << percentile(group,50,100) << ',' << percentile(group,99,100) << ','
                << percentile(group,999,1000) << ',' << group.back() << ',' << over10us << ','
                << rehashes << ',' << prunes << ',' << (first==0 ? 0 : samples[first-1].live)
                << ',' << samples[end-1].live << ',' << samples[end-1].price_levels
                << ',' << samples[end-1].buckets << '\n';
        }
    }
    if (!out) throw std::runtime_error("failed writing profile latency CSV");
}

int run_profile(const Options& options, const std::vector<Event>& events, std::size_t peak_live) {
    std::vector<std::size_t> reserve_choices = {0, peak_live, options.orders};
    // De-duplicate when an entirely passive workload has peak_live == orders.
    std::sort(reserve_choices.begin(), reserve_choices.end());
    reserve_choices.erase(std::unique(reserve_choices.begin(), reserve_choices.end()), reserve_choices.end());
    std::vector<std::vector<double>> rates(reserve_choices.size());
    const std::filesystem::path raw_path(options.profile_csv);
    std::ofstream raw;
    if (!options.profile_csv.empty()) {
        if (!raw_path.parent_path().empty()) std::filesystem::create_directories(raw_path.parent_path());
        raw.open(raw_path, std::ios::trunc); // fresh run, not an accumulated prior CSV.
        if (!raw) throw std::runtime_error("cannot open profile throughput CSV");
        raw << "compiler,workload,seed,operations,peak_live,round,sequence_position,reserve_ids,seconds,ops_per_sec,checksum\n";
    }
    Metrics reference;
    bool have_reference = false;
    std::cout << "ExchangeCore v0.8 - interleaved capacity experiment\n"
              << "Workload: " << options.workload << " | Seed: " << options.seed
              << " | Operations: " << events.size() << " | Peak live orders: " << peak_live
              << " | Rounds: " << options.profile_rounds << '\n';
    for (unsigned round=0; round<options.profile_rounds; ++round) {
        std::vector<std::size_t> order(reserve_choices.size());
        std::iota(order.begin(), order.end(), 0);
        std::rotate(order.begin(), order.begin() + round % order.size(), order.end());
        if (round % 2 == 1) std::reverse(order.begin(), order.end());
        for (std::size_t position=0; position<order.size(); ++position) {
            const std::size_t i=order[position];
            const auto run = run_throughput(events, reserve_choices[i]);
            if (run.seconds <= 0) throw std::runtime_error("throughput interval too short");
            if (!have_reference) { reference=run.metrics; have_reference=true; }
            else if (run.metrics != reference)
                throw std::runtime_error("replay mismatch between reservation settings");
            const double rate = static_cast<double>(events.size()) / run.seconds;
            rates[i].push_back(rate);
            if (raw) raw << compiler_name() << ',' << options.workload << ',' << options.seed << ','
                << events.size() << ',' << peak_live << ',' << round+1 << ',' << position+1 << ','
                << reserve_choices[i] << ',' << std::fixed << std::setprecision(8) << run.seconds
                << ',' << std::setprecision(2) << rate << ',' << run.metrics.checksum << '\n';
        }
    }
    if (raw) raw.flush();
    for (std::size_t i=0; i<reserve_choices.size(); ++i) {
        Metrics diagnostic_outcome;
        const auto samples = profile_latency(events, reserve_choices[i], diagnostic_outcome);
        if (diagnostic_outcome != reference)
            throw std::runtime_error("diagnostic and throughput replay outcomes differ");
        save_profile_latency(options, samples, reserve_choices[i], peak_live);
        const auto rehashes = static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
            [](const ProfileSample& s){return s.rehash;}));
        const auto removed = static_cast<std::size_t>(std::count_if(samples.begin(), samples.end(),
            [](const ProfileSample& s){return s.cancel && s.removed_level;}));
        auto& r = rates[i];
        std::sort(r.begin(), r.end());
        const double median = (r.size() % 2 == 1) ? r[r.size()/2]
            : (r[r.size()/2-1] + r[r.size()/2]) / 2.0;
        std::cout << "  reserve=" << reserve_choices[i] << " median=" << std::fixed
                  << std::setprecision(0) << median << " ops/s"
                  << " observed_index_rehashes=" << rehashes
                  << " cancel_level_removals=" << removed << '\n';
    }
    std::cout << "Matching replay checksum: " << reference.checksum << '\n'
              << "Profiles measure bucket growth and price-level removal, not all heap allocations.\n";
    return 0;
}

int run(int argc, char** argv) {
    const Options options = parse_options(argc, argv);
    std::size_t peak_live = 0;
    const auto events = generate_events(options, &peak_live); // Excluded from timings.
    if (options.profile_grid) return run_profile(options, events, peak_live);
    std::vector<double> rates;
    std::vector<double> seconds;
    rates.reserve(options.repeats);
    seconds.reserve(options.repeats);
    Metrics reference;
    for (unsigned i = 0; i < options.repeats; ++i) {
        const auto result = run_throughput(events, options.reserve_ids);
        if (i == 0) reference = result.metrics;
        else if (result.metrics != reference)
            throw std::runtime_error("throughput trials produced different outcomes");
        if (result.seconds <= 0) throw std::runtime_error("clock interval too short");
        seconds.push_back(result.seconds);
        rates.push_back(static_cast<double>(events.size()) / result.seconds);
    }
    std::sort(rates.begin(), rates.end());
    std::sort(seconds.begin(), seconds.end());
    const auto median = [](const std::vector<double>& sorted) {
        const auto center = sorted.size() / 2;
        return sorted.size() % 2 == 1 ? sorted[center]
            : (sorted[center - 1] + sorted[center]) / 2.0;
    };
    const double median_rate = median(rates);
    const double median_seconds = median(seconds);
    auto latency = run_latency(events, options.reserve_ids);
    if (latency.metrics != reference)
        throw std::runtime_error("latency and throughput replay outcomes differ");
    save_latency_diagnostics(options, events, latency.nanoseconds);
    std::sort(latency.nanoseconds.begin(), latency.nanoseconds.end());
    const long double total_ns = std::accumulate(latency.nanoseconds.begin(),
        latency.nanoseconds.end(), 0.0L);
    const double mean_ns = static_cast<double>(total_ns / latency.nanoseconds.size());
    const auto p50 = percentile(latency.nanoseconds, 50, 100);
    const auto p95 = percentile(latency.nanoseconds, 95, 100);
    const auto p99 = percentile(latency.nanoseconds, 99, 100);
    const auto p999 = percentile(latency.nanoseconds, 999, 1000);

    std::cout << "ExchangeCore v0.8 - Performance Benchmarks\n"
              << "Preflight: " <<
#ifdef EXCHANGECORE_LEGACY_PREFLIGHT
                 "legacy (v0.4)\n"
#else
                 "selective (v0.5)\n"
#endif
              << "Cancel path: " <<
#ifdef EXCHANGECORE_LEGACY_CANCEL
                 "legacy tree lookup\n"
#else
                 "cached level pointer\n"
#endif
              << "Order-ID reserve hint: " << options.reserve_ids << '\n' 
              << "Compiler: " << compiler_name() << '\n'
              << "Build: " <<
#ifdef NDEBUG
                 "Release\n"
#else
                 "Debug (not suitable for performance comparisons)\n"
#endif
              << "Workload: " << options.workload << " | Seed: " << options.seed
              << " | Operations: " << options.orders << " | Repeats: " << options.repeats << '\n'
              << "Median throughput: " << std::fixed << std::setprecision(0)
              << median_rate << " operations/sec\n"
              << "Median duration: " << std::setprecision(4) << median_seconds << " sec\n"
              << "Per-operation latency (separate instrumented replay):\n"
              << "  Mean: " << std::setprecision(1) << mean_ns << " ns\n"
              << "  p50: " << p50 << " ns | p95: " << p95
              << " ns | p99: " << p99 << " ns | p99.9: " << p999 << " ns\n"
              << "Submits: " << reference.submitted << " | Cancels: " << reference.cancelled
              << " | Trades: " << reference.trades << '\n'
              << "Executed units: " << reference.units_executed
              << " | Remaining orders: " << reference.remaining_orders << '\n'
              << "Replay checksum: " << reference.checksum << '\n';
    if (!options.latency_csv.empty())
        std::cout << "Latency diagnostics CSV appended: " << options.latency_csv << '\n';
    if (!options.csv.empty()) {
        save_csv(options, reference, median_seconds, median_rate, mean_ns,
                 p50, p95, p99, p999);
        std::cout << "CSV appended: " << options.csv << '\n';
    }
    return 0;
}
} // namespace

int main(int argc, char** argv) {
    try { return run(argc, argv); }
    catch (const std::exception& e) {
        std::cerr << "Benchmark error: " << e.what() << '\n';
        return 1;
    }
}
