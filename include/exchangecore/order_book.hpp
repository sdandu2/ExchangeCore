#pragma once

#include "exchangecore/order.hpp"

#include <cstddef>
#include <functional>
#include <list>
#include <map>
#include <optional>
#include <unordered_map>
#include <vector>

namespace exchangecore {

enum class SubmitStatus {
    Accepted,
    DuplicateId,
    InvalidId,
    InvalidSide,
    InvalidPrice,
    InvalidQuantity,
    QuantityOverflow
};

// A taker (new order) trades against a maker (existing resting order).
// The execution price is always the maker's resting price.
struct Trade {
    OrderId maker_id;
    OrderId taker_id;
    PriceTicks price_ticks;
    Quantity quantity;
};

struct Submission {
    SubmitStatus status;
    Quantity executed_quantity;
    Quantity resting_quantity;
    std::vector<Trade> trades;
};

struct LevelSummary {
    PriceTicks price_ticks;
    Quantity total_quantity;
    std::size_t order_count;
};

// Constant-time diagnostic snapshot; use outside timed operations in benchmarks.
// Bucket count is an observable proxy for rehashes, NOT total allocation tracking.
struct BookStats {
    std::size_t live_orders;
    std::size_t bid_levels;
    std::size_t ask_levels;
    std::size_t id_buckets;
    float id_load_factor;
};

// Single-threaded limit order book with price-time (FIFO) priority.
// New limit orders match immediately when marketable; unfilled quantity rests.
class OrderBook {
public:
    // Optional performance hint. Does not limit order count or change matching behavior.
    // Reserving order IDs can reduce hash-table rehashes during book growth.
    void reserve_order_ids(std::size_t expected_live_orders);

    [[nodiscard]] Submission submit(const Order& order);
    [[nodiscard]] bool cancel(OrderId id);
    [[nodiscard]] std::optional<LevelSummary> best_bid() const;
    [[nodiscard]] std::optional<LevelSummary> best_ask() const;
    [[nodiscard]] std::vector<Order> orders_at(Side side, PriceTicks price_ticks) const;
    [[nodiscard]] std::size_t order_count() const noexcept;
    [[nodiscard]] BookStats stats() const noexcept;

private:
    struct PriceLevel {
        std::list<Order> orders;
        Quantity total_quantity = 0;
    };

    struct Location {
        Side side;
        PriceTicks price_ticks;
        std::list<Order>::iterator order_it;
    };

    std::map<PriceTicks, PriceLevel, std::greater<PriceTicks>> bids_;
    std::map<PriceTicks, PriceLevel> asks_;
    std::unordered_map<OrderId, Location> locations_;

    [[nodiscard]] static LevelSummary summarize(PriceTicks price_ticks,
                                                const PriceLevel& level);
    [[nodiscard]] Quantity predicted_resting_quantity(const Order& order) const;
    void rest(const Order& order);
    void match_buy(const Order& incoming, Quantity& remaining,
                   std::vector<Trade>& trades);
    void match_sell(const Order& incoming, Quantity& remaining,
                    std::vector<Trade>& trades);
};

} // namespace exchangecore
