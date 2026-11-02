#pragma once

#include <cstdint>

namespace exchangecore {

using OrderId = std::uint64_t;
using PriceTicks = std::int64_t;
using Quantity = std::uint64_t;

enum class Side { Buy, Sell };

// Prices are integer ticks to avoid floating-point comparison errors.
struct Order {
    OrderId id;
    Side side;
    PriceTicks price_ticks;
    Quantity quantity;
};

} // namespace exchangecore
