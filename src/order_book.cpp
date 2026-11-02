#include "exchangecore/order_book.hpp"

#include <algorithm>
#include <iterator>
#include <limits>
#include <utility>

namespace exchangecore {
namespace {

bool is_crossing(const Order& incoming, PriceTicks resting_price) {
    return incoming.side == Side::Buy ? incoming.price_ticks >= resting_price
                                      : incoming.price_ticks <= resting_price;
}

} // namespace

void OrderBook::reserve_order_ids(std::size_t expected_live_orders) {
    locations_.reserve(expected_live_orders);
}

Quantity OrderBook::predicted_resting_quantity(const Order& order) const {
    Quantity remaining = order.quantity;
    if (order.side == Side::Buy) {
        for (const auto& [price, level] : asks_) {
            if (!is_crossing(order, price) || remaining == 0) break;
            remaining -= std::min(remaining, level.total_quantity);
        }
    } else {
        for (const auto& [price, level] : bids_) {
            if (!is_crossing(order, price) || remaining == 0) break;
            remaining -= std::min(remaining, level.total_quantity);
        }
    }
    return remaining;
}

Submission OrderBook::submit(const Order& order) {
    const auto rejected = [](SubmitStatus status) {
        return Submission{status, 0, 0, {}};
    };

    if (order.id == 0) return rejected(SubmitStatus::InvalidId);
    if (order.side != Side::Buy && order.side != Side::Sell)
        return rejected(SubmitStatus::InvalidSide);
    if (order.price_ticks <= 0) return rejected(SubmitStatus::InvalidPrice);
    if (order.quantity == 0) return rejected(SubmitStatus::InvalidQuantity);
    if (locations_.contains(order.id)) return rejected(SubmitStatus::DuplicateId);

    // Preflight aggregate-quantity overflow before executing any trade.
    // A pre-existing same-side level can overflow if the incoming order rests.
    // Usually its total fits with the entire incoming quantity; since any
    // resting remainder is <= order.quantity, no opposite-book scan is needed.
    // Only when an overflow is *possible* do we run the exact legacy scan.
    // A build-time switch preserves the original validation algorithm for
    // reproducible A/B benchmarking of this optimization.
#ifdef EXCHANGECORE_LEGACY_PREFLIGHT
    const Quantity predicted_remaining = predicted_resting_quantity(order);
    if (predicted_remaining != 0) {
        if (order.side == Side::Buy) {
            const auto existing = bids_.find(order.price_ticks);
            if (existing != bids_.end() &&
                existing->second.total_quantity >
                    std::numeric_limits<Quantity>::max() - predicted_remaining)
                return rejected(SubmitStatus::QuantityOverflow);
        } else {
            const auto existing = asks_.find(order.price_ticks);
            if (existing != asks_.end() &&
                existing->second.total_quantity >
                    std::numeric_limits<Quantity>::max() - predicted_remaining)
                return rejected(SubmitStatus::QuantityOverflow);
        }
    }
#else
    const Quantity max_quantity = std::numeric_limits<Quantity>::max();
    if (order.side == Side::Buy) {
        const auto existing = bids_.find(order.price_ticks);
        if (existing != bids_.end() &&
            existing->second.total_quantity > max_quantity - order.quantity) {
            const Quantity predicted_remaining = predicted_resting_quantity(order);
            if (existing->second.total_quantity > max_quantity - predicted_remaining)
                return rejected(SubmitStatus::QuantityOverflow);
        }
    } else {
        const auto existing = asks_.find(order.price_ticks);
        if (existing != asks_.end() &&
            existing->second.total_quantity > max_quantity - order.quantity) {
            const Quantity predicted_remaining = predicted_resting_quantity(order);
            if (existing->second.total_quantity > max_quantity - predicted_remaining)
                return rejected(SubmitStatus::QuantityOverflow);
        }
    }
#endif

    Quantity remaining = order.quantity;
    std::vector<Trade> trades;
    if (order.side == Side::Buy) match_buy(order, remaining, trades);
    else match_sell(order, remaining, trades);

    if (remaining != 0) {
        Order passive = order;
        passive.quantity = remaining;
        rest(passive);
    }

    return {SubmitStatus::Accepted, order.quantity - remaining, remaining,
            std::move(trades)};
}

void OrderBook::rest(const Order& order) {
    if (order.side == Side::Buy) {
        auto [level_it, inserted] = bids_.try_emplace(order.price_ticks);
        (void)inserted;
        auto& level = level_it->second;
        level.orders.push_back(order);
        auto order_it = std::prev(level.orders.end());
        level.total_quantity += order.quantity;
        locations_.emplace(order.id, Location{order.side, order.price_ticks, order_it});
    } else {
        auto [level_it, inserted] = asks_.try_emplace(order.price_ticks);
        (void)inserted;
        auto& level = level_it->second;
        level.orders.push_back(order);
        auto order_it = std::prev(level.orders.end());
        level.total_quantity += order.quantity;
        locations_.emplace(order.id, Location{order.side, order.price_ticks, order_it});
    }
}

void OrderBook::match_buy(const Order& incoming, Quantity& remaining,
                          std::vector<Trade>& trades) {
    while (remaining != 0 && !asks_.empty() &&
           is_crossing(incoming, asks_.begin()->first)) {
        auto level_it = asks_.begin();
        PriceLevel& level = level_it->second;
        while (remaining != 0 && !level.orders.empty()) {
            Order& maker = level.orders.front();
            const Quantity executed = std::min(remaining, maker.quantity);
            trades.push_back({maker.id, incoming.id, maker.price_ticks, executed});
            maker.quantity -= executed;
            level.total_quantity -= executed;
            remaining -= executed;
            if (maker.quantity == 0) {
                locations_.erase(maker.id);
                level.orders.pop_front();
            }
        }
        if (level.orders.empty()) asks_.erase(level_it);
    }
}

void OrderBook::match_sell(const Order& incoming, Quantity& remaining,
                           std::vector<Trade>& trades) {
    while (remaining != 0 && !bids_.empty() &&
           is_crossing(incoming, bids_.begin()->first)) {
        auto level_it = bids_.begin();
        PriceLevel& level = level_it->second;
        while (remaining != 0 && !level.orders.empty()) {
            Order& maker = level.orders.front();
            const Quantity executed = std::min(remaining, maker.quantity);
            trades.push_back({maker.id, incoming.id, maker.price_ticks, executed});
            maker.quantity -= executed;
            level.total_quantity -= executed;
            remaining -= executed;
            if (maker.quantity == 0) {
                locations_.erase(maker.id);
                level.orders.pop_front();
            }
        }
        if (level.orders.empty()) bids_.erase(level_it);
    }
}

bool OrderBook::cancel(OrderId id) {
    const auto location_it = locations_.find(id);
    if (location_it == locations_.end()) return false;

    const Location location = location_it->second;
    if (location.side == Side::Buy) {
        auto level_it = bids_.find(location.price_ticks);
        auto& level = level_it->second;
        level.total_quantity -= location.order_it->quantity;
        level.orders.erase(location.order_it);
        if (level.orders.empty()) bids_.erase(level_it);
    } else {
        auto level_it = asks_.find(location.price_ticks);
        auto& level = level_it->second;
        level.total_quantity -= location.order_it->quantity;
        level.orders.erase(location.order_it);
        if (level.orders.empty()) asks_.erase(level_it);
    }

    locations_.erase(location_it);
    return true;
}

LevelSummary OrderBook::summarize(PriceTicks price_ticks, const PriceLevel& level) {
    return LevelSummary{price_ticks, level.total_quantity, level.orders.size()};
}

std::optional<LevelSummary> OrderBook::best_bid() const {
    if (bids_.empty()) return std::nullopt;
    const auto& [price, level] = *bids_.begin();
    return summarize(price, level);
}

std::optional<LevelSummary> OrderBook::best_ask() const {
    if (asks_.empty()) return std::nullopt;
    const auto& [price, level] = *asks_.begin();
    return summarize(price, level);
}

std::vector<Order> OrderBook::orders_at(Side side, PriceTicks price_ticks) const {
    std::vector<Order> result;
    if (side == Side::Buy) {
        const auto it = bids_.find(price_ticks);
        if (it != bids_.end())
            result.assign(it->second.orders.begin(), it->second.orders.end());
    } else if (side == Side::Sell) {
        const auto it = asks_.find(price_ticks);
        if (it != asks_.end())
            result.assign(it->second.orders.begin(), it->second.orders.end());
    }
    return result;
}

std::size_t OrderBook::order_count() const noexcept {
    return locations_.size();
}

} // namespace exchangecore
