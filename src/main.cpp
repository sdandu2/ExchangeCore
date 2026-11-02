#include "exchangecore/order_book.hpp"

#include <iostream>

using namespace exchangecore;

int main() {
    OrderBook book;
    const auto ask1 = book.submit({1, Side::Sell, 10'100, 50});
    const auto ask2 = book.submit({2, Side::Sell, 10'200, 30});
    const auto taker = book.submit({3, Side::Buy, 10'300, 70});

    if (ask1.status != SubmitStatus::Accepted ||
        ask2.status != SubmitStatus::Accepted ||
        taker.status != SubmitStatus::Accepted) {
        std::cerr << "Failed to submit demonstration orders.\n";
        return 1;
    }

    std::cout << "ExchangeCore v0.3 - Price-Time Priority Matching\n";
    for (const auto& trade : taker.trades) {
        std::cout << "Trade: maker=" << trade.maker_id
                  << " taker=" << trade.taker_id
                  << " qty=" << trade.quantity
                  << " price=" << trade.price_ticks << " ticks\n";
    }

    std::cout << "Executed quantity: " << taker.executed_quantity << '\n';
    std::cout << "Remaining resting orders: " << book.order_count() << '\n';
    if (const auto ask = book.best_ask()) {
        std::cout << "Best ask: " << ask->price_ticks << " ticks, "
                  << ask->total_quantity << " units\n";
    }
    return 0;
}
