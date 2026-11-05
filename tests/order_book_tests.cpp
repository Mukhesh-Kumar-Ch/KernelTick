#include "../include/order_matching/Matcher.hpp"

#include <iostream>

int failures = 0;

void check(bool condition, const char* message) {
    if (condition) {
        std::cout << "[PASS] " << message << '\n';
    } else {
        std::cout << "[FAIL] " << message << '\n';
        ++failures;
    }
}

void testOrderInsertionAndBestPrices() {
    OrderBook book;

    Order buy(1, true, 100, Order::doubleToPrice(100.50), 100);
    Order sell(2, false, 75, Order::doubleToPrice(101.00), 200);

    check(book.addOrder(&buy), "Insert buy order");
    check(book.addOrder(&sell), "Insert sell order");
    check(book.getBestBid() == &buy, "Best bid points to buy order");
    check(book.getBestAsk() == &sell, "Best ask points to sell order");
    check(!book.hasMatch(), "Non-crossed book has no match");
}

void testDuplicateOrderId() {
    OrderBook book;

    Order first(10, true, 100, Order::doubleToPrice(100.00), 100);
    Order duplicate(10, true, 50, Order::doubleToPrice(101.00), 200);

    check(book.addOrder(&first), "Insert original order ID");
    check(!book.addOrder(&duplicate), "Reject duplicate order ID");
    check(book.getOrder(10) == &first,
          "Original order remains registered after duplicate attempt");
}

void testMatchingAndPartialFill() {
    OrderBook book;
    Matcher matcher;

    Order buy1(21, true, 50, Order::doubleToPrice(100.75), 100);
    Order buy2(22, true, 100, Order::doubleToPrice(100.50), 200);
    Order sell(23, false, 75, Order::doubleToPrice(100.25), 300);

    check(book.addOrder(&buy1), "Insert first matching buy");
    check(book.addOrder(&buy2), "Insert second matching buy");
    check(book.addOrder(&sell), "Insert matching sell");

    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.75)) == 50,
          "Initial quantity at 100.75 is 50");
    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.50)) == 100,
          "Initial quantity at 100.50 is 100");
    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.25)) == 75,
          "Initial quantity at 100.25 is 75");
    check(book.getTotalBidQuantity() == 150, "Initial total bid quantity is 150");
    check(book.getTotalAskQuantity() == 75, "Initial total ask quantity is 75");

    size_t trades = matcher.matchOrders(book);

    check(trades == 2, "Matching produces two trades");
    check(buy1.quantity == 0, "First buy is fully filled");
    check(buy2.quantity == 75, "Second buy retains 75 units");
    check(sell.quantity == 0, "Sell order is fully filled");
    check(book.getOrder(21) == nullptr, "Fully filled first buy is removed");
    check(book.getOrder(23) == nullptr, "Fully filled sell is removed");
    check(book.getOrder(22) == &buy2, "Partially filled buy remains registered");

    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.75)) == 0,
          "Quantity at fully filled 100.75 is 0");
    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.50)) == 75,
          "Quantity at partially filled 100.50 is 75");
    check(book.getTotalQuantityAtPrice(Order::doubleToPrice(100.25)) == 0,
          "Quantity at fully filled sell 100.25 is 0");
    check(book.getTotalBidQuantity() == 75, "Total bid quantity updated to 75");
    check(book.getTotalAskQuantity() == 0, "Total ask quantity updated to 0");
}

// 1. Adding orders correctly increases total quantity
void testAddOrdersIncreasesTotalQuantity() {
    OrderBook book;
    Price p100 = Order::doubleToPrice(100.00);
    Price p101 = Order::doubleToPrice(101.00);

    check(book.getTotalQuantityAtPrice(p100) == 0, "Empty level quantity is 0");

    Order buy1(101, true, 100, p100, 100);
    check(book.addOrder(&buy1), "Add first buy order (100 units)");
    check(book.getTotalQuantityAtPrice(p100) == 100, "Level quantity is 100 after first order");
    check(book.getTotalBidQuantity() == 100, "Total bid quantity is 100");

    Order buy2(102, true, 50, p100, 200);
    check(book.addOrder(&buy2), "Add second buy order (50 units) at same price");
    check(book.getTotalQuantityAtPrice(p100) == 150, "Level quantity increases to 150");
    check(book.getTotalBidQuantity() == 150, "Total bid quantity increases to 150");

    Order buy3(103, true, 75, p101, 300);
    check(book.addOrder(&buy3), "Add third buy order at higher price");
    check(book.getTotalQuantityAtPrice(p101) == 75, "Level 101 quantity is 75");
    check(book.getTotalQuantityAtPrice(p100) == 150, "Level 100 quantity unchanged at 150");
    check(book.getTotalBidQuantity() == 225, "Total bid quantity increases to 225");

    Order sell1(104, false, 200, p101, 400);
    check(book.addOrder(&sell1), "Add sell order at 101");
    check(book.getTotalQuantityAtPrice(p101, false) == 200, "Sell level 101 quantity is 200");
    check(book.getTotalAskQuantity() == 200, "Total ask quantity is 200");
}

// 2. Partial filling reduces total quantity correctly
void testPartialFillReducesTotalQuantity() {
    OrderBook book;
    Price p = Order::doubleToPrice(100.00);

    Order buyA(201, true, 100, p, 100);
    Order buyB(202, true, 50, p, 200);

    check(book.addOrder(&buyA), "Insert Order A (100 units)");
    check(book.addOrder(&buyB), "Insert Order B (50 units)");
    check(book.getTotalQuantityAtPrice(p) == 150, "Total quantity before partial fill is 150");

    check(book.reduceOrderQuantity(201, 25), "Partially reduce Order A by 25");
    check(buyA.quantity == 75, "Order A remaining quantity is 75");
    check(buyB.quantity == 50, "Order B quantity remains 50");
    check(book.getTotalQuantityAtPrice(p) == 125, "Total quantity at level reduces to 125");
    check(book.getTotalBidQuantity() == 125, "Total bid quantity reduces to 125");
}

// 3. Multiple partial fills maintain correct totals
void testMultiplePartialFillsMaintainCorrectTotals() {
    OrderBook book;
    Price p = Order::doubleToPrice(100.00);

    Order buyA(301, true, 100, p, 100);
    Order buyB(302, true, 50, p, 200);

    check(book.addOrder(&buyA), "Insert Order A (100 units)");
    check(book.addOrder(&buyB), "Insert Order B (50 units)");
    check(book.getTotalQuantityAtPrice(p) == 150, "Initial total is 150");

    check(book.reduceOrderQuantity(301, 20), "Fill 1: reduce Order A by 20");
    check(buyA.quantity == 80, "Order A has 80 units left");
    check(book.getTotalQuantityAtPrice(p) == 130, "Total is 130 after fill 1");

    check(book.reduceOrderQuantity(301, 30), "Fill 2: reduce Order A by 30");
    check(buyA.quantity == 50, "Order A has 50 units left");
    check(book.getTotalQuantityAtPrice(p) == 100, "Total is 100 after fill 2");

    check(book.reduceOrderQuantity(302, 15), "Fill 3: reduce Order B by 15");
    check(buyB.quantity == 35, "Order B has 35 units left");
    check(book.getTotalQuantityAtPrice(p) == 85, "Total is 85 after fill 3");

    check(book.reduceOrderQuantity(302, 10), "Fill 4: reduce Order B by 10");
    check(buyB.quantity == 25, "Order B has 25 units left");
    check(book.getTotalQuantityAtPrice(p) == 75, "Total is 75 after fill 4");

    check(book.reduceOrderQuantity(301, 25), "Fill 5: reduce Order A by 25");
    check(buyA.quantity == 25, "Order A has 25 units left");
    check(book.getTotalQuantityAtPrice(p) == 50, "Total is 50 after fill 5");
    check(book.getTotalBidQuantity() == 50, "Total bid quantity matches 50");
}

// 4. Completely filling an order removes it correctly
void testCompleteFillRemovesOrderCorrectly() {
    OrderBook book;
    Price p1 = Order::doubleToPrice(100.00);
    Price p2 = Order::doubleToPrice(99.00);

    Order buyA(401, true, 60, p1, 100);
    Order buyB(402, true, 40, p1, 200);
    Order buyC(403, true, 50, p2, 300);

    check(book.addOrder(&buyA), "Insert Order A (60 units @ 100)");
    check(book.addOrder(&buyB), "Insert Order B (40 units @ 100)");
    check(book.addOrder(&buyC), "Insert Order C (50 units @ 99)");
    check(book.getTotalQuantityAtPrice(p1) == 100, "Level 100 initial total is 100");
    check(book.getTotalQuantityAtPrice(p2) == 50, "Level 99 initial total is 50");

    // Completely fill Order A in one reduction
    check(book.reduceOrderQuantity(401, 60), "Completely fill Order A (60 units)");
    check(buyA.quantity == 0, "Order A quantity is 0");
    check(book.getOrder(401) == nullptr, "Order A removed from order book lookup");
    check(book.getTotalQuantityAtPrice(p1) == 40, "Level 100 total is 40 after Order A removed");
    check(book.getBestBid() == &buyB, "Best bid is now Order B");

    // Partially reduce then completely fill Order B
    check(book.reduceOrderQuantity(402, 20), "Partially reduce Order B by 20");
    check(buyB.quantity == 20, "Order B has 20 units remaining");
    check(book.getTotalQuantityAtPrice(p1) == 20, "Level 100 total is 20");

    check(book.reduceOrderQuantity(402, 20), "Completely fill Order B with remaining 20 units");
    check(buyB.quantity == 0, "Order B quantity is 0");
    check(book.getOrder(402) == nullptr, "Order B removed from order book lookup");
    check(book.getTotalQuantityAtPrice(p1) == 0, "Level 100 total is now 0");

    // Level 100 should now be erased, best bid shifts to Level 99 (Order C)
    check(book.getBestBid() == &buyC, "Best bid correctly shifted to Level 99 (Order C)");
    check(book.getBestBidPrice() == p2, "Best bid price is 99.00");
    check(book.getTotalBidQuantity() == 50, "Total bid quantity is 50 (Order C)");
}

// 5. Cancelling a partially filled order subtracts only its remaining quantity
void testCancelPartiallyFilledOrderSubtractsRemainingQuantity() {
    OrderBook book;
    Price p = Order::doubleToPrice(100.00);

    Order buyA(501, true, 100, p, 100);
    Order buyB(502, true, 50, p, 200);

    check(book.addOrder(&buyA), "Insert Order A (100 units)");
    check(book.addOrder(&buyB), "Insert Order B (50 units)");
    check(book.getTotalQuantityAtPrice(p) == 150, "Initial total quantity is 150");

    // Partially reduce Order A by 35 -> 65 left
    check(book.reduceOrderQuantity(501, 35), "Partially fill Order A by 35 units");
    check(buyA.quantity == 65, "Order A has 65 units remaining");
    check(book.getTotalQuantityAtPrice(p) == 115, "Total quantity is 115 after partial fill");

    // Cancel Order A -> must subtract exactly 65 units, leaving 50 units (Order B)
    check(book.cancelOrder(501), "Cancel partially filled Order A");
    check(book.getOrder(501) == nullptr, "Order A removed from order book");
    check(book.getTotalQuantityAtPrice(p) == 50,
          "Total quantity at level is exactly 50 after cancelling Order A");
    check(book.getOrder(502) == &buyB, "Order B remains registered");
    check(buyB.quantity == 50, "Order B retains its 50 units");
    check(book.getTotalBidQuantity() == 50, "Total bid quantity is 50");

    // Cancel Order B
    check(book.cancelOrder(502), "Cancel Order B");
    check(book.getOrder(502) == nullptr, "Order B removed from order book");
    check(book.getTotalQuantityAtPrice(p) == 0, "Total quantity at level is 0");
    check(book.getTotalBidQuantity() == 0, "Total bid quantity is 0");
    check(book.getBestBid() == nullptr, "Best bid is null");
}

// 6. Invalid reductions are rejected without changing the order book
void testInvalidReductionsRejected() {
    OrderBook book;
    Price p = Order::doubleToPrice(100.00);

    Order buy(601, true, 100, p, 100);
    check(book.addOrder(&buy), "Insert Order (100 units)");
    check(book.getTotalQuantityAtPrice(p) == 100, "Initial quantity is 100");

    // Zero reduction rejected
    check(!book.reduceOrderQuantity(601, 0), "Reject zero quantity reduction");
    check(buy.quantity == 100, "Order quantity unchanged after zero reduction attempt");
    check(book.getTotalQuantityAtPrice(p) == 100, "Level quantity unchanged after zero reduction attempt");

    // Reduction greater than remaining rejected
    check(!book.reduceOrderQuantity(601, 101), "Reject reduction greater than remaining quantity (101 > 100)");
    check(buy.quantity == 100, "Order quantity unchanged after excessive reduction attempt");
    check(book.getTotalQuantityAtPrice(p) == 100, "Level quantity unchanged after excessive reduction attempt");

    // Non-existent order ID rejected
    check(!book.reduceOrderQuantity(9999, 10), "Reject reduction on non-existent order ID");
    check(book.getTotalQuantityAtPrice(p) == 100, "Level quantity unchanged after non-existent order attempt");

    // Partial reduction of 40 units -> 60 remaining
    check(book.reduceOrderQuantity(601, 40), "Valid partial reduction of 40 units");
    check(buy.quantity == 60, "Order quantity is now 60");
    check(book.getTotalQuantityAtPrice(p) == 60, "Level quantity is now 60");

    // Reduction greater than remaining 60 rejected
    check(!book.reduceOrderQuantity(601, 61), "Reject reduction greater than remaining (61 > 60)");
    check(buy.quantity == 60, "Order quantity remains 60");
    check(book.getTotalQuantityAtPrice(p) == 60, "Level quantity remains 60");
}

// 7. Best bid and ask remain correct after these operations
void testBestBidAndAskCorrectness() {
    OrderBook book;

    Price pBid1 = Order::doubleToPrice(105.00);
    Price pBid2 = Order::doubleToPrice(104.00);
    Price pAsk1 = Order::doubleToPrice(106.00);
    Price pAsk2 = Order::doubleToPrice(107.00);

    Order buy1(701, true, 100, pBid1, 100);
    Order buy2(702, true, 50, pBid2, 200);
    Order sell1(703, false, 100, pAsk1, 300);
    Order sell2(704, false, 50, pAsk2, 400);

    check(book.addOrder(&buy1), "Add Buy 1 @ 105.00");
    check(book.addOrder(&buy2), "Add Buy 2 @ 104.00");
    check(book.addOrder(&sell1), "Add Sell 1 @ 106.00");
    check(book.addOrder(&sell2), "Add Sell 2 @ 107.00");

    check(book.getBestBidPrice() == pBid1, "Initial best bid price is 105.00");
    check(book.getBestAskPrice() == pAsk1, "Initial best ask price is 106.00");
    check(book.getBestBid() == &buy1, "Best bid points to Buy 1");
    check(book.getBestAsk() == &sell1, "Best ask points to Sell 1");

    // Partial fill of Buy 1 does not change best bid price
    check(book.reduceOrderQuantity(701, 40), "Partially reduce Buy 1 by 40");
    check(book.getBestBidPrice() == pBid1, "Best bid price still 105.00 after partial fill");
    check(book.getBestBid() == &buy1, "Best bid still points to Buy 1");
    check(buy1.quantity == 60, "Buy 1 has 60 remaining");

    // Complete fill of Buy 1 moves best bid to Buy 2
    check(book.reduceOrderQuantity(701, 60), "Completely fill Buy 1");
    check(book.getBestBidPrice() == pBid2, "Best bid price shifted to 104.00");
    check(book.getBestBid() == &buy2, "Best bid points to Buy 2");

    // Partial fill of Sell 1 does not change best ask price
    check(book.reduceOrderQuantity(703, 30), "Partially reduce Sell 1 by 30");
    check(book.getBestAskPrice() == pAsk1, "Best ask price still 106.00 after partial fill");
    check(book.getBestAsk() == &sell1, "Best ask still points to Sell 1");
    check(sell1.quantity == 70, "Sell 1 has 70 remaining");

    // Complete fill of Sell 1 moves best ask to Sell 2
    check(book.reduceOrderQuantity(703, 70), "Completely fill Sell 1");
    check(book.getBestAskPrice() == pAsk2, "Best ask price shifted to 107.00");
    check(book.getBestAsk() == &sell2, "Best ask points to Sell 2");

    // Complete fill of Buy 2 clears bids
    check(book.reduceOrderQuantity(702, 50), "Completely fill Buy 2");
    check(book.getBestBidPrice() == 0, "Best bid price is 0 after all bids filled");
    check(book.getBestBid() == nullptr, "Best bid is null");

    // Complete fill of Sell 2 clears asks
    check(book.reduceOrderQuantity(704, 50), "Completely fill Sell 2");
    check(book.getBestAskPrice() == 0, "Best ask price is 0 after all asks filled");
    check(book.getBestAsk() == nullptr, "Best ask is null");
}

// Additional test: Matching multiple orders at the same price level
void testMatchingMultipleOrdersAtSamePriceLevel() {
    OrderBook book;
    Matcher matcher;
    Price p = Order::doubleToPrice(100.00);

    Order buy1(801, true, 40, p, 100);
    Order buy2(802, true, 60, p, 200);
    Order sell(803, false, 70, p, 300);

    check(book.addOrder(&buy1), "Add Buy 1 (40 units @ 100)");
    check(book.addOrder(&buy2), "Add Buy 2 (60 units @ 100)");
    check(book.addOrder(&sell), "Add Sell (70 units @ 100)");

    check(book.getTotalQuantityAtPrice(p, true) == 100, "Total buy quantity at 100 is 100");
    check(book.getTotalQuantityAtPrice(p, false) == 70, "Total sell quantity at 100 is 70");

    size_t trades = matcher.matchOrders(book);

    check(trades == 2, "Matching executes 2 trades");
    check(buy1.quantity == 0, "Buy 1 fully filled (40 units)");
    check(buy2.quantity == 30, "Buy 2 partially filled (30 remaining)");
    check(sell.quantity == 0, "Sell fully filled (70 units)");

    check(book.getOrder(801) == nullptr, "Buy 1 removed from book");
    check(book.getOrder(803) == nullptr, "Sell removed from book");
    check(book.getOrder(802) == &buy2, "Buy 2 remains in book");

    check(book.getTotalQuantityAtPrice(p, true) == 30, "Total buy quantity at 100 is correctly 30");
    check(book.getTotalQuantityAtPrice(p, false) == 0, "Total sell quantity at 100 is 0");
    check(book.getTotalBidQuantity() == 30, "Total bid quantity is 30");
    check(book.getTotalAskQuantity() == 0, "Total ask quantity is 0");
}

// 8. Atomic two-sided trade execution and failure handling
void testAtomicTradeExecutionAndFailureHandling() {
    OrderBook book;
    Matcher matcher;
    Price p = Order::doubleToPrice(100.00);

    Order buy(901, true, 100, p, 100);
    Order sell(902, false, 80, p, 200);

    check(book.addOrder(&buy), "Add Buy (100 units)");
    check(book.addOrder(&sell), "Add Sell (80 units)");
    check(book.getTotalQuantityAtPrice(p, true) == 100, "Initial buy qty is 100");
    check(book.getTotalQuantityAtPrice(p, false) == 80, "Initial sell qty is 80");

    // Failure case 1: invalid ask ID
    check(!book.executeTrade(901, 9999, 50), "Reject trade with non-existent ask ID");
    check(buy.quantity == 100, "Buy quantity unaffected after invalid ask ID attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after invalid ask ID attempt");
    check(book.getTotalQuantityAtPrice(p, true) == 100, "Buy aggregate quantity unaffected");
    check(book.getTotalQuantityAtPrice(p, false) == 80, "Sell aggregate quantity unaffected");

    // Failure case 2: invalid bid ID
    check(!book.executeTrade(9999, 902, 50), "Reject trade with non-existent bid ID");
    check(buy.quantity == 100, "Buy quantity unaffected after invalid bid ID attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after invalid bid ID attempt");
    check(book.getTotalQuantityAtPrice(p, true) == 100, "Buy aggregate quantity unaffected");
    check(book.getTotalQuantityAtPrice(p, false) == 80, "Sell aggregate quantity unaffected");

    // Failure case 3: excessive bid quantity (requested 120 > available 100)
    check(!book.executeTrade(901, 902, 120), "Reject trade where qty exceeds bid quantity");
    check(buy.quantity == 100, "Buy quantity unaffected after excessive bid qty attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after excessive bid qty attempt");

    // Failure case 4: excessive ask quantity (requested 90 > available 80)
    check(!book.executeTrade(901, 902, 90), "Reject trade where qty exceeds ask quantity");
    check(buy.quantity == 100, "Buy quantity unaffected after excessive ask qty attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after excessive ask qty attempt");

    // Failure case 5: self-match (same ID)
    check(!book.executeTrade(901, 901, 50), "Reject self-match with identical order IDs");
    check(buy.quantity == 100, "Buy quantity unaffected after self-match attempt");

    // Failure case 6: zero quantity
    check(!book.executeTrade(901, 902, 0), "Reject trade with zero quantity");
    check(buy.quantity == 100, "Buy quantity unaffected after zero qty attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after zero qty attempt");

    // Failure case 7: reversed sides (passing sell as bid, buy as ask)
    check(!book.executeTrade(902, 901, 50), "Reject trade when order sides are reversed");
    check(buy.quantity == 100, "Buy quantity unaffected after reversed sides attempt");
    check(sell.quantity == 80, "Sell quantity unaffected after reversed sides attempt");

    // Matcher executeTrade failure handling: should not update last_trade_ on failure
    const Trade& initial_last_trade = matcher.getLastTrade();
    OrderId initial_buy_id = initial_last_trade.buy_order_id;
    Quantity initial_qty = initial_last_trade.quantity;

    check(!matcher.executeTrade(book, &buy, &sell, p, 150),
          "Matcher::executeTrade fails when qty exceeds available");
    check(matcher.getLastTrade().buy_order_id == initial_buy_id,
          "Matcher last_trade_ not updated on failed trade");
    check(matcher.getLastTrade().quantity == initial_qty,
          "Matcher last_trade_ quantity unchanged on failed trade");
    check(buy.quantity == 100, "Buy quantity unchanged after Matcher failure");
    check(sell.quantity == 80, "Sell quantity unchanged after Matcher failure");

    // Success case: valid two-sided atomic reduction of 50 units
    check(book.executeTrade(901, 902, 50), "Valid two-sided atomic reduction of 50 units");
    check(buy.quantity == 50, "Buy order retains 50 units");
    check(sell.quantity == 30, "Sell order retains 30 units");
    check(book.getTotalQuantityAtPrice(p, true) == 50, "Buy aggregate quantity reduced to 50");
    check(book.getTotalQuantityAtPrice(p, false) == 30, "Sell aggregate quantity reduced to 30");

    // Complete fill of sell order with another 30 units
    check(book.executeTrade(901, 902, 30), "Valid two-sided reduction completing sell order");
    check(buy.quantity == 20, "Buy order retains 20 units");
    check(sell.quantity == 0, "Sell order fully filled (0 units)");
    check(book.getOrder(902) == nullptr, "Fully filled sell order removed from book");
    check(book.getOrder(901) == &buy, "Partially filled buy order remains in book");
    check(book.getTotalQuantityAtPrice(p, true) == 20, "Buy aggregate quantity is 20");
    check(book.getTotalQuantityAtPrice(p, false) == 0, "Sell aggregate quantity is 0");
    check(book.getBestAsk() == nullptr, "Best ask is null after sell level cleared");
    check(book.getBestBid() == &buy, "Best bid points to remaining buy order");
}

int main() {
    std::cout << "=== KernelTick Order Book Tests ===\n\n";

    testOrderInsertionAndBestPrices();
    testDuplicateOrderId();
    testMatchingAndPartialFill();
    testAddOrdersIncreasesTotalQuantity();
    testPartialFillReducesTotalQuantity();
    testMultiplePartialFillsMaintainCorrectTotals();
    testCompleteFillRemovesOrderCorrectly();
    testCancelPartiallyFilledOrderSubtractsRemainingQuantity();
    testInvalidReductionsRejected();
    testBestBidAndAskCorrectness();
    testMatchingMultipleOrdersAtSamePriceLevel();
    testAtomicTradeExecutionAndFailureHandling();

    std::cout << "\nFailures: " << failures << '\n';
    return failures == 0 ? 0 : 1;
}
