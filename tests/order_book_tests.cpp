#include "exchangecore/order_book.hpp"

#include <iostream>
#include <limits>
#include <stdexcept>
#include <vector>

using namespace exchangecore;

namespace {
using AddResult = SubmitStatus;

// Compatibility helper preserves the original v0.2 passive-order tests.
struct TestBook : OrderBook {
    SubmitStatus add(const Order& order) { return submit(order).status; }
};

void require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}

void empty_book() {
    TestBook book;
    require(book.order_count() == 0, "initial order count");
    require(!book.best_bid(), "empty best bid");
    require(!book.best_ask(), "empty best ask");
    require(!book.cancel(99), "cancel missing order");
}

void validate_order_fields() {
    TestBook book;
    require(book.add({0, Side::Buy, 100, 1}) == AddResult::InvalidId, "id zero");
    require(book.add({1, Side::Buy, 0, 1}) == AddResult::InvalidPrice, "price zero");
    require(book.add({1, Side::Buy, -1, 1}) == AddResult::InvalidPrice, "negative price");
    require(book.add({1, Side::Buy, 100, 0}) == AddResult::InvalidQuantity, "quantity zero");
    require(book.order_count() == 0, "invalid orders not inserted");
}

void best_bid_price_priority() {
    TestBook book;
    require(book.add({1, Side::Buy, 100, 4}) == AddResult::Accepted, "add bid 100");
    require(book.add({2, Side::Buy, 105, 3}) == AddResult::Accepted, "add bid 105");
    require(book.add({3, Side::Buy, 102, 2}) == AddResult::Accepted, "add bid 102");
    require(book.best_bid()->price_ticks == 105, "highest bid first");
}

void best_ask_price_priority() {
    TestBook book;
    require(book.add({1, Side::Sell, 110, 4}) == AddResult::Accepted, "add ask 110");
    require(book.add({2, Side::Sell, 108, 3}) == AddResult::Accepted, "add ask 108");
    require(book.add({3, Side::Sell, 112, 2}) == AddResult::Accepted, "add ask 112");
    require(book.best_ask()->price_ticks == 108, "lowest ask first");
}

void fifo_within_price_level() {
    TestBook book;
    require(book.add({4, Side::Buy, 100, 1}) == AddResult::Accepted, "first at price");
    require(book.add({2, Side::Buy, 100, 2}) == AddResult::Accepted, "second at price");
    require(book.add({9, Side::Buy, 100, 3}) == AddResult::Accepted, "third at price");
    const auto orders = book.orders_at(Side::Buy, 100);
    require(orders.size() == 3, "all orders at level");
    require(orders[0].id == 4 && orders[1].id == 2 && orders[2].id == 9,
            "insertion order preserved");
}

void aggregate_quantity() {
    TestBook book;
    require(book.add({1, Side::Buy, 100, 12}) == AddResult::Accepted, "add first");
    require(book.add({2, Side::Buy, 100, 5}) == AddResult::Accepted, "add second");
    require(book.best_bid()->total_quantity == 17, "aggregate quantity");
    require(book.best_bid()->order_count == 2, "level order count");
    require(book.order_count() == 2, "book order count");
}

void cancel_middle_preserves_fifo() {
    TestBook book;
    require(book.add({1, Side::Sell, 105, 2}) == AddResult::Accepted, "first");
    require(book.add({2, Side::Sell, 105, 3}) == AddResult::Accepted, "second");
    require(book.add({3, Side::Sell, 105, 4}) == AddResult::Accepted, "third");
    require(book.cancel(2), "cancel middle");
    const auto orders = book.orders_at(Side::Sell, 105);
    require(orders.size() == 2, "two orders remain");
    require(orders[0].id == 1 && orders[1].id == 3, "survivor order");
    require(book.best_ask()->total_quantity == 6, "quantity after cancel");
}

void cancel_best_level_updates_best_price() {
    TestBook book;
    require(book.add({1, Side::Buy, 101, 1}) == AddResult::Accepted, "best bid");
    require(book.add({2, Side::Buy, 100, 1}) == AddResult::Accepted, "second bid");
    require(book.cancel(1), "cancel best bid");
    require(book.best_bid()->price_ticks == 100, "new best bid");
    require(book.orders_at(Side::Buy, 101).empty(), "deleted empty level");
    require(book.cancel(2), "cancel final bid");
    require(!book.best_bid(), "no bids left");
}

void reject_duplicate_ids_across_sides() {
    TestBook book;
    require(book.add({1, Side::Buy, 100, 1}) == AddResult::Accepted, "first id");
    require(book.add({1, Side::Sell, 110, 1}) == AddResult::DuplicateId,
            "duplicate across sides");
    require(book.order_count() == 1, "duplicate not counted");
}

void crossing_orders_execute_instead_of_resting() {
    TestBook book;
    require(book.add({1, Side::Sell, 105, 1}) == AddResult::Accepted, "rest ask");
    const auto execution = book.submit({2, Side::Buy, 105, 1});
    require(execution.status == AddResult::Accepted, "marketable buy accepted");
    require(execution.trades.size() == 1, "one trade executed");
    require(execution.trades[0].maker_id == 1 && execution.trades[0].taker_id == 2,
            "maker and taker identified");
    require(book.order_count() == 0, "both fully filled orders removed");
}

void reject_quantity_overflow() {
    TestBook book;
    const auto max = std::numeric_limits<Quantity>::max();
    require(book.add({1, Side::Buy, 100, max}) == AddResult::Accepted, "add max quantity");
    require(book.add({2, Side::Buy, 100, 1}) == AddResult::QuantityOverflow, "reject overflow");
    require(book.best_bid()->total_quantity == max, "total unchanged");
    require(book.order_count() == 1, "order not inserted");
}

void canceled_id_can_be_reused() {
    TestBook book;
    require(book.add({7, Side::Buy, 100, 2}) == AddResult::Accepted, "initial id");
    require(book.cancel(7), "cancel id");
    require(!book.cancel(7), "cannot cancel twice");
    require(book.add({7, Side::Sell, 102, 3}) == AddResult::Accepted, "reuse id");
    require(book.order_count() == 1, "one active order");
}

void missing_price_level_returns_empty() {
    TestBook book;
    require(book.add({1, Side::Buy, 100, 1}) == AddResult::Accepted, "add bid");
    require(book.orders_at(Side::Buy, 99).empty(), "missing buy level");
    require(book.orders_at(Side::Sell, 100).empty(), "wrong side level");
}


void no_trade_when_prices_do_not_cross() {
    OrderBook book;
    const auto first = book.submit({1, Side::Sell, 105, 2});
    const auto second = book.submit({2, Side::Buy, 104, 3});
    require(first.trades.empty() && second.trades.empty(), "no crossing executions");
    require(second.resting_quantity == 3 && second.executed_quantity == 0, "full bid rests");
    require(book.order_count() == 2, "both orders rest");
    require(book.best_bid()->price_ticks < book.best_ask()->price_ticks, "book uncrossed");
}

void buy_executes_at_maker_price() {
    OrderBook book;
    (void)book.submit({10, Side::Sell, 101, 4});
    const auto result = book.submit({20, Side::Buy, 110, 4});
    require(result.trades.size() == 1, "one fill");
    require(result.trades[0].price_ticks == 101, "resting ask determines trade price");
    require(result.executed_quantity == 4 && result.resting_quantity == 0,
            "full buy fill");
    require(!book.best_ask() && !book.best_bid(), "both sides empty");
}

void sell_executes_at_maker_price() {
    OrderBook book;
    (void)book.submit({10, Side::Buy, 110, 4});
    const auto result = book.submit({20, Side::Sell, 101, 4});
    require(result.trades.size() == 1, "one fill");
    require(result.trades[0].price_ticks == 110, "resting bid determines trade price");
    require(result.executed_quantity == 4 && result.resting_quantity == 0,
            "full sell fill");
}

void fifo_fills_first_order_then_second() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 2});
    (void)book.submit({2, Side::Sell, 100, 3});
    (void)book.submit({3, Side::Sell, 100, 4});
    const auto result = book.submit({4, Side::Buy, 101, 4});
    require(result.trades.size() == 2, "fills exactly two makers");
    require(result.trades[0].maker_id == 1 && result.trades[0].quantity == 2,
            "first arrived filled first");
    require(result.trades[1].maker_id == 2 && result.trades[1].quantity == 2,
            "second maker partly filled");
    const auto remaining = book.orders_at(Side::Sell, 100);
    require(remaining.size() == 2, "two remaining makers");
    require(remaining[0].id == 2 && remaining[0].quantity == 1,
            "partially filled maker retains FIFO place");
    require(remaining[1].id == 3 && remaining[1].quantity == 4,
            "third maker untouched");
    require(book.best_ask()->total_quantity == 5, "level sum decremented");
}

void buy_sweeps_multiple_price_levels() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 101, 50});
    (void)book.submit({2, Side::Sell, 102, 30});
    const auto result = book.submit({3, Side::Buy, 103, 70});
    require(result.trades.size() == 2, "one trade per maker");
    require(result.trades[0].price_ticks == 101 && result.trades[0].quantity == 50,
            "first fill at best ask");
    require(result.trades[1].price_ticks == 102 && result.trades[1].quantity == 20,
            "second fill at next ask");
    require(result.executed_quantity == 70 && result.resting_quantity == 0,
            "buy fully consumed");
    require(book.order_count() == 1 && book.best_ask()->total_quantity == 10,
            "remaining ask is 10");
    require(!book.best_bid(), "no buy order rests");
}

void sell_sweeps_multiple_price_levels() {
    OrderBook book;
    (void)book.submit({1, Side::Buy, 103, 50});
    (void)book.submit({2, Side::Buy, 102, 30});
    const auto result = book.submit({3, Side::Sell, 101, 70});
    require(result.trades.size() == 2, "one trade per maker");
    require(result.trades[0].price_ticks == 103 && result.trades[0].quantity == 50,
            "best bid filled first");
    require(result.trades[1].price_ticks == 102 && result.trades[1].quantity == 20,
            "next best bid filled second");
    require(book.best_bid()->total_quantity == 10 && book.best_bid()->price_ticks == 102,
            "remaining bid is correct");
}

void partial_incoming_order_rests() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 2});
    const auto result = book.submit({2, Side::Buy, 102, 5});
    require(result.executed_quantity == 2 && result.resting_quantity == 3,
            "incoming leftover rests");
    require(book.best_bid()->price_ticks == 102 && book.best_bid()->total_quantity == 3,
            "unfilled remainder at original limit price");
    require(book.order_count() == 1 && !book.best_ask(), "only remaining buy rests");
}

void partial_incoming_sell_order_rests() {
    OrderBook book;
    (void)book.submit({1, Side::Buy, 100, 3});
    const auto result = book.submit({2, Side::Sell, 98, 5});
    require(result.executed_quantity == 3 && result.resting_quantity == 2,
            "sell leftover rests");
    require(book.best_ask()->price_ticks == 98 && book.best_ask()->total_quantity == 2,
            "unfilled ask at its limit price");
}

void price_limit_prevents_worse_fills() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 99, 2});
    (void)book.submit({2, Side::Sell, 101, 3});
    const auto result = book.submit({3, Side::Buy, 100, 4});
    require(result.trades.size() == 1, "cannot trade worse than limit");
    require(result.executed_quantity == 2 && result.resting_quantity == 2,
            "remainder rests without crossing at 101");
    require(book.best_bid()->price_ticks == 100 && book.best_ask()->price_ticks == 101,
            "book uncrossed");
}

void cancel_partially_filled_resting_order() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 10});
    (void)book.submit({2, Side::Buy, 100, 3});
    require(book.best_ask()->total_quantity == 7, "maker remaining qty");
    require(book.cancel(1), "cancel maker remainder");
    require(book.order_count() == 0 && !book.best_ask(), "cancel cleans up level");
}

void filled_order_id_is_reusable() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 1});
    (void)book.submit({2, Side::Buy, 100, 1});
    require(book.submit({1, Side::Buy, 95, 2}).status == SubmitStatus::Accepted,
            "filled maker id reused");
    require(book.submit({2, Side::Sell, 110, 2}).status == SubmitStatus::Accepted,
            "filled taker id reused");
}

void duplicate_id_cannot_trade_against_book() {
    OrderBook book;
    (void)book.submit({1, Side::Buy, 95, 2});
    (void)book.submit({2, Side::Sell, 105, 2});
    const auto result = book.submit({1, Side::Buy, 105, 2});
    require(result.status == SubmitStatus::DuplicateId && result.trades.empty(),
            "active duplicate rejected before matching");
    require(book.best_ask()->total_quantity == 2 && book.order_count() == 2,
            "book preserved after duplicate");
}

void invalid_side_is_rejected() {
    OrderBook book;
    const auto result = book.submit({1, static_cast<Side>(99), 100, 3});
    require(result.status == SubmitStatus::InvalidSide, "invalid enum rejected");
    require(book.order_count() == 0, "nothing added");
}

void matching_equal_price_executes() {
    OrderBook book;
    (void)book.submit({1, Side::Buy, 100, 2});
    const auto result = book.submit({2, Side::Sell, 100, 2});
    require(result.trades.size() == 1 && result.trades[0].price_ticks == 100,
            "equal price marketable");
}

void exhausted_level_is_removed_before_next_match() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 1});
    (void)book.submit({2, Side::Sell, 102, 1});
    const auto result = book.submit({3, Side::Buy, 102, 1});
    require(result.trades.size() == 1 && result.trades[0].maker_id == 1,
            "first price level removed when empty");
    require(book.best_ask()->price_ticks == 102, "next best ask visible");
}

void cancelled_order_cannot_be_filled() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 2});
    (void)book.submit({2, Side::Sell, 100, 2});
    require(book.cancel(1), "cancel first ask");
    const auto result = book.submit({3, Side::Buy, 100, 1});
    require(result.trades.size() == 1 && result.trades[0].maker_id == 2,
            "only live maker filled");
}

void very_large_quantities_do_not_overflow_matching() {
    OrderBook book;
    const auto max = std::numeric_limits<Quantity>::max();
    (void)book.submit({1, Side::Sell, 100, max});
    const auto result = book.submit({2, Side::Buy, 100, max});
    require(result.status == SubmitStatus::Accepted && result.executed_quantity == max,
            "full max trade");
    require(result.resting_quantity == 0 && book.order_count() == 0,
            "max quantity fully removed");
}

void trade_totals_match_executed_quantity() {
    OrderBook book;
    (void)book.submit({1, Side::Sell, 100, 2});
    (void)book.submit({2, Side::Sell, 100, 3});
    (void)book.submit({3, Side::Sell, 101, 5});
    const auto result = book.submit({4, Side::Buy, 101, 8});
    Quantity aggregate = 0;
    for (const auto& trade : result.trades) aggregate += trade.quantity;
    require(aggregate == result.executed_quantity, "fills sum to executed quantity");
    require(result.resting_quantity + aggregate == 8, "submitted qty conserved");
    require(book.best_ask()->total_quantity == 2, "remaining book quantity correct");
}

void reserved_ids_preserve_matching_and_cancellation() {
    OrderBook book;
    book.reserve_order_ids(10'000);
    require(book.order_count() == 0, "reserve does not insert orders");
    require(book.submit({1, Side::Sell, 101, 8}).status == SubmitStatus::Accepted,
            "rest order with reserved index");
    const auto result = book.submit({2, Side::Buy, 101, 3});
    require(result.trades.size() == 1 && result.trades[0].quantity == 3,
            "reserved index still matches");
    require(book.best_ask()->total_quantity == 5, "reserved index partial maker");
    require(book.cancel(1), "reserved index cancellation works");
    require(book.submit({1, Side::Buy, 100, 4}).status == SubmitStatus::Accepted,
            "reserved index allows reused id");
    require(book.order_count() == 1, "reserved index final count");
}

void overflow_on_sell_level_is_atomic() {
    OrderBook book;
    const auto max = std::numeric_limits<Quantity>::max();
    require(book.submit({1, Side::Sell, 110, max - 3}).status == SubmitStatus::Accepted,
            "insert near-maximum ask");
    require(book.submit({2, Side::Sell, 110, 3}).status == SubmitStatus::Accepted,
            "fill capacity without overflow");
    const auto result = book.submit({3, Side::Sell, 110, 1});
    require(result.status == SubmitStatus::QuantityOverflow && result.trades.empty(),
            "overflow must reject sell");
    require(book.best_ask()->total_quantity == max && book.order_count() == 2,
            "sell overflow leaves state unchanged");
}

void reservation_does_not_cap_order_capacity() {
    OrderBook book;
    book.reserve_order_ids(1);
    for (OrderId id = 1; id <= 100; ++id) {
        require(book.submit({id, Side::Buy, 100, 1}).status == SubmitStatus::Accepted,
                "reserve is not a hard order limit");
    }
    require(book.order_count() == 100, "more orders than reserved capacity accepted");
    require(book.best_bid()->total_quantity == 100, "order quantity consistent");
}

struct TestCase { const char* name; void (*run)(); };
} // namespace

int main() {
    const std::vector<TestCase> tests = {
        {"empty book", empty_book},
        {"order validation", validate_order_fields},
        {"best bid priority", best_bid_price_priority},
        {"best ask priority", best_ask_price_priority},
        {"FIFO insertion order", fifo_within_price_level},
        {"aggregated quantity", aggregate_quantity},
        {"cancel preserves FIFO", cancel_middle_preserves_fifo},
        {"cancel updates best", cancel_best_level_updates_best_price},
        {"duplicate IDs", reject_duplicate_ids_across_sides},
        {"crossing orders execute", crossing_orders_execute_instead_of_resting},
        {"quantity overflow", reject_quantity_overflow},
        {"reuse canceled ID", canceled_id_can_be_reused},
        {"missing price level", missing_price_level_returns_empty},
        {"noncrossing order", no_trade_when_prices_do_not_cross},
        {"buy at maker price", buy_executes_at_maker_price},
        {"sell at maker price", sell_executes_at_maker_price},
        {"FIFO partial maker fills", fifo_fills_first_order_then_second},
        {"buy sweep", buy_sweeps_multiple_price_levels},
        {"sell sweep", sell_sweeps_multiple_price_levels},
        {"partially filled incoming buy", partial_incoming_order_rests},
        {"partially filled incoming sell", partial_incoming_sell_order_rests},
        {"limit price boundary", price_limit_prevents_worse_fills},
        {"cancel partially filled maker", cancel_partially_filled_resting_order},
        {"reuse filled IDs", filled_order_id_is_reusable},
        {"duplicate cannot match", duplicate_id_cannot_trade_against_book},
        {"invalid side", invalid_side_is_rejected},
        {"equal-price trade", matching_equal_price_executes},
        {"exhausted level removed", exhausted_level_is_removed_before_next_match},
        {"cancelled order cannot fill", cancelled_order_cannot_be_filled},
        {"max quantity fill", very_large_quantities_do_not_overflow_matching},
        {"conservation of quantity", trade_totals_match_executed_quantity},
        {"reserve does not change matching", reserved_ids_preserve_matching_and_cancellation},
        {"sell overflow rejects atomically", overflow_on_sell_level_is_atomic},
        {"reserve is a hint", reservation_does_not_cap_order_capacity},
    };

    std::size_t failures = 0;
    for (const auto& test : tests) {
        try {
            test.run();
            std::cout << "[PASS] " << test.name << '\n';
        } catch (const std::exception& error) {
            ++failures;
            std::cerr << "[FAIL] " << test.name << ": " << error.what() << '\n';
        }
    }

    std::cout << "Result: " << (tests.size() - failures) << "/"
              << tests.size() << " tests passed\n";
    return failures == 0 ? 0 : 1;
}
