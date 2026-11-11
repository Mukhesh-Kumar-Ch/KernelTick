#pragma once

#include <cstdint>
#include <vector>
#include <unordered_map>
#include <map>
#include <array>
#include <atomic>
#include "Order.hpp"
#include "../utils/Timestamp.hpp"

// Price ladder structure for HFT - O(1) price level access
// Each price level maintains a FIFO queue of orders
class PriceLevel {
public:
    PriceLevel() noexcept : total_quantity_(0), order_count_(0) {}
    
    // Add order to this price level (FIFO)
    void addOrder(Order* order) noexcept {
        orders_.push_back(order);
        total_quantity_ += order->quantity;
        order_count_++;
    }
    
    // Remove order by ID
    bool removeOrder(OrderId id) noexcept {
        for (auto it = orders_.begin(); it != orders_.end(); ++it) {
            if ((*it)->id == id) [[likely]] {
                total_quantity_ -= (*it)->quantity;
                orders_.erase(it);
                order_count_--;
                return true;
            }
        }
        return false;
    }
    // Check if an order exists at this level with at least the specified quantity
    [[nodiscard]] bool hasOrderWithQuantity(OrderId id, Quantity min_qty) const noexcept {
        if (min_qty == 0) [[unlikely]] {
            return false;
        }
        for (const Order* order : orders_) {
            if (order && order->id == id) [[likely]] {
                return order->quantity >= min_qty;
            }
        }
        return false;
    }

    // Reduce an order's quantity and keep the level total consistent
    bool reduceOrderQuantity(OrderId id, Quantity amount) noexcept {
        if (amount == 0) [[unlikely]] {
            return false;
        }

        for (Order* order : orders_) {
            if (order && order->id == id) [[likely]] {
                // Reject invalid reductions
                if (amount > order->quantity) [[unlikely]] {
                    return false;
                }

                order->quantity -= amount;
                total_quantity_ -= amount;
                return true;
            }
        }

        return false;
    }
    // Get best order (FIFO - first in queue)
    [[nodiscard]] Order* getBestOrder() const noexcept {
        if (orders_.empty()) [[unlikely]] {
            return nullptr;
        }
        return orders_.front();
    }
    
    // Get total quantity at this level
    [[nodiscard]] Quantity getTotalQuantity() const noexcept {
        return total_quantity_;
    }
    
    // Check if level is empty
    [[nodiscard]] bool isEmpty() const noexcept {
        return orders_.empty();
    }
    
    // Get all orders (for iteration)
    [[nodiscard]] const std::vector<Order*>& getOrders() const noexcept {
        return orders_;
    }

private:
    std::vector<Order*> orders_;  // FIFO queue of orders at this price
    Quantity total_quantity_;
    size_t order_count_;
};

// HFT-optimized OrderBook using price ladder structure
// Provides O(1) price level access and O(log P) best price lookup
class OrderBook {
public:
    static constexpr Price MIN_PRICE = 0;
    static constexpr Price MAX_PRICE = 10'000'000'000LL; // $10,000 scaled
    static constexpr Price TICK_SIZE = 1LL; // Minimum price increment (1 micro-dollar)
    static constexpr size_t MAX_PRICE_LEVELS = (MAX_PRICE - MIN_PRICE) / TICK_SIZE;
    
    OrderBook() noexcept {
        // Maps don't support reserve(), but we can hint the allocator
        // The map will grow as needed
    }
    
    // Add order to the book
    // Returns true if order was added, false if invalid
    [[nodiscard]] bool addOrder(Order* order) noexcept {
        if (!order || order->quantity == 0) [[unlikely]] {
            return false;
        }
        
        // Set timestamp if not already set
        if (order->timestamp_ns == 0) [[unlikely]] {
            order->timestamp_ns = hft::getTimestampNs();
        }
        
        // Reject duplicate order IDs before modifying the book
        if (order_map_.find(order->id) != order_map_.end()) [[unlikely]] {
            return false;
        }

        // Store order for lookup
        order_map_[order->id] = order;
        
        // Add to appropriate price ladder
        Price price = order->price_scaled;
        if (order->isBuy()) [[likely]] {
            auto& level = buy_levels_[price];
            level.addOrder(order);
            updateBestBid();
        } else {
            auto& level = sell_levels_[price];
            level.addOrder(order);
            updateBestAsk();
        }
        
        return true;
    }
    
    // Cancel order by ID
    [[nodiscard]] bool cancelOrder(OrderId id) noexcept {
        auto it = order_map_.find(id);
        if (it == order_map_.end()) [[unlikely]] {
            return false;
        }
        
        Order* order = it->second;
        Price price = order->price_scaled;
        
        bool removed = false;
        if (order->isBuy()) [[likely]] {
            auto level_it = buy_levels_.find(price);
            if (level_it != buy_levels_.end()) [[likely]] {
                removed = level_it->second.removeOrder(id);
                if (level_it->second.isEmpty()) [[unlikely]] {
                    buy_levels_.erase(level_it);
                }
                updateBestBid();
            }
        } else {
            auto level_it = sell_levels_.find(price);
            if (level_it != sell_levels_.end()) [[likely]] {
                removed = level_it->second.removeOrder(id);
                if (level_it->second.isEmpty()) [[unlikely]] {
                    sell_levels_.erase(level_it);
                }
                updateBestAsk();
            }
        }
        
        if (removed) [[likely]] {
            order_map_.erase(it);
        }

        return removed;
    }

    // Reduce order quantity by ID
    // If order becomes completely filled, removes it from the order book without subtracting its quantity twice
    [[nodiscard]] bool reduceOrderQuantity(OrderId id, Quantity amount) noexcept {
        if (amount == 0) [[unlikely]] {
            return false;
        }

        auto it = order_map_.find(id);
        if (it == order_map_.end()) [[unlikely]] {
            return false;
        }

        Order* order = it->second;
        if (!order || amount > order->quantity) [[unlikely]] {
            return false;
        }

        Price price = order->price_scaled;
        if (order->isBuy()) [[likely]] {
            auto level_it = buy_levels_.find(price);
            if (level_it == buy_levels_.end()) [[unlikely]] {
                return false;
            }

            if (!level_it->second.reduceOrderQuantity(id, amount)) [[unlikely]] {
                return false;
            }

            if (order->quantity == 0) {
                level_it->second.removeOrder(id);
                if (level_it->second.isEmpty()) [[unlikely]] {
                    buy_levels_.erase(level_it);
                }
                updateBestBid();
                order_map_.erase(it);
            }
        } else {
            auto level_it = sell_levels_.find(price);
            if (level_it == sell_levels_.end()) [[unlikely]] {
                return false;
            }

            if (!level_it->second.reduceOrderQuantity(id, amount)) [[unlikely]] {
                return false;
            }

            if (order->quantity == 0) {
                level_it->second.removeOrder(id);
                if (level_it->second.isEmpty()) [[unlikely]] {
                    sell_levels_.erase(level_it);
                }
                updateBestAsk();
                order_map_.erase(it);
            }
        }

        return true;
    }

    // Execute a two-sided trade reduction atomically
    // Validates both orders before mutating any book state to prevent partial updates
    [[nodiscard]] bool executeTrade(OrderId bid_id, OrderId ask_id, Quantity qty) noexcept {
        if (qty == 0 || bid_id == ask_id) [[unlikely]] {
            return false;
        }

        // Validate bid order in map
        auto it_bid = order_map_.find(bid_id);
        if (it_bid == order_map_.end()) [[unlikely]] {
            return false;
        }
        Order* bid = it_bid->second;
        if (!bid || !bid->isBuy() || bid->quantity < qty) [[unlikely]] {
            return false;
        }

        // Validate ask order in map
        auto it_ask = order_map_.find(ask_id);
        if (it_ask == order_map_.end()) [[unlikely]] {
            return false;
        }
        Order* ask = it_ask->second;
        if (!ask || !ask->isSell() || ask->quantity < qty) [[unlikely]] {
            return false;
        }

        // Validate bid exists in buy price levels with sufficient quantity
        auto buy_level_it = buy_levels_.find(bid->price_scaled);
        if (buy_level_it == buy_levels_.end()) [[unlikely]] {
            return false;
        }
        if (!buy_level_it->second.hasOrderWithQuantity(bid_id, qty)) [[unlikely]] {
            return false;
        }

        // Validate ask exists in sell price levels with sufficient quantity
        auto sell_level_it = sell_levels_.find(ask->price_scaled);
        if (sell_level_it == sell_levels_.end()) [[unlikely]] {
            return false;
        }
        if (!sell_level_it->second.hasOrderWithQuantity(ask_id, qty)) [[unlikely]] {
            return false;
        }

        // Both orders fully validated! Perform reductions atomically.
        buy_level_it->second.reduceOrderQuantity(bid_id, qty);
        if (bid->quantity == 0) {
            buy_level_it->second.removeOrder(bid_id);
            if (buy_level_it->second.isEmpty()) [[unlikely]] {
                buy_levels_.erase(buy_level_it);
            }
            updateBestBid();
            order_map_.erase(it_bid);
        }

        sell_level_it->second.reduceOrderQuantity(ask_id, qty);
        if (ask->quantity == 0) {
            sell_level_it->second.removeOrder(ask_id);
            if (sell_level_it->second.isEmpty()) [[unlikely]] {
                sell_levels_.erase(sell_level_it);
            }
            updateBestAsk();
            order_map_.erase(it_ask);
        }

        return true;
    }
    
    // Modify order (cancel + add)
    [[nodiscard]] bool modifyOrder(OrderId id, Price new_price, Quantity new_quantity) noexcept {
        auto it = order_map_.find(id);
        if (it == order_map_.end()) [[unlikely]] {
            return false;
        }
        
        Order* order = it->second;
        
        // Cancel old order
        if (!cancelOrder(id)) [[unlikely]] {
            return false;
        }
        
        // Update order fields
        order->price_scaled = new_price;
        order->quantity = new_quantity;
        order->timestamp_ns = hft::getTimestampNs(); // Update timestamp
        
        // Re-add with new parameters
        return addOrder(order);
    }
    
    // Get best bid (highest buy price)
    [[nodiscard]] Order* getBestBid() const noexcept {
        if (buy_levels_.empty()) [[unlikely]] {
            return nullptr;
        }
        return buy_levels_.rbegin()->second.getBestOrder();
    }
    
    // Get best ask (lowest sell price)
    [[nodiscard]] Order* getBestAsk() const noexcept {
        if (sell_levels_.empty()) [[unlikely]] {
            return nullptr;
        }
        return sell_levels_.begin()->second.getBestOrder();
    }
    
    // Get best bid price
    [[nodiscard]] Price getBestBidPrice() const noexcept {
        return best_bid_price_;
    }
    
    // Get best ask price
    [[nodiscard]] Price getBestAskPrice() const noexcept {
        return best_ask_price_;
    }
    
    // Get spread (ask - bid)
    [[nodiscard]] Price getSpread() const noexcept {
        if (best_bid_price_ == 0 || best_ask_price_ == 0) [[unlikely]] {
            return 0;
        }
        return best_ask_price_ - best_bid_price_;
    }
    
    // Check if there's a match (bid >= ask)
    [[nodiscard]] bool hasMatch() const noexcept {
        return best_bid_price_ > 0 && best_ask_price_ > 0 && 
               best_bid_price_ >= best_ask_price_;
    }
    
    // Get order by ID
    [[nodiscard]] Order* getOrder(OrderId id) const noexcept {
        auto it = order_map_.find(id);
        if (it != order_map_.end()) [[likely]] {
            return it->second;
        }
        return nullptr;
    }

    // Get total aggregate quantity at a specific price across both sides
    [[nodiscard]] Quantity getTotalQuantityAtPrice(Price price) const noexcept {
        auto it_buy = buy_levels_.find(price);
        if (it_buy != buy_levels_.end()) {
            return it_buy->second.getTotalQuantity();
        }
        auto it_sell = sell_levels_.find(price);
        if (it_sell != sell_levels_.end()) {
            return it_sell->second.getTotalQuantity();
        }
        return 0;
    }

    // Get total aggregate quantity at a specific price and side
    [[nodiscard]] Quantity getTotalQuantityAtPrice(Price price, bool is_buy) const noexcept {
        if (is_buy) {
            auto it = buy_levels_.find(price);
            return (it != buy_levels_.end()) ? it->second.getTotalQuantity() : 0;
        } else {
            auto it = sell_levels_.find(price);
            return (it != sell_levels_.end()) ? it->second.getTotalQuantity() : 0;
        }
    }

    // Get total bid quantity across all buy levels
    [[nodiscard]] Quantity getTotalBidQuantity() const noexcept {
        Quantity total = 0;
        for (const auto& [price, level] : buy_levels_) {
            total += level.getTotalQuantity();
        }
        return total;
    }

    // Get total ask quantity across all sell levels
    [[nodiscard]] Quantity getTotalAskQuantity() const noexcept {
        Quantity total = 0;
        for (const auto& [price, level] : sell_levels_) {
            total += level.getTotalQuantity();
        }
        return total;
    }

private:
    // Price ladder: sorted map from price to price level
    // Using std::map for O(log n) insertion but O(1) best price access via rbegin()/begin()
    std::map<Price, PriceLevel> buy_levels_;  // Sorted descending (highest first)
    std::map<Price, PriceLevel> sell_levels_; // Sorted ascending (lowest first)
    
    // Order lookup map
    std::unordered_map<OrderId, Order*> order_map_;
    
    // Cached best prices for O(1) access
    Price best_bid_price_ = 0;
    Price best_ask_price_ = 0;
    
    // Update cached best bid
    void updateBestBid() noexcept {
        if (buy_levels_.empty()) [[unlikely]] {
            best_bid_price_ = 0;
            return;
        }
        // Highest price is at rbegin()
        best_bid_price_ = buy_levels_.rbegin()->first;
    }
    
    // Update cached best ask
    void updateBestAsk() noexcept {
        if (sell_levels_.empty()) [[unlikely]] {
            best_ask_price_ = 0;
            return;
        }
        // Lowest price is at begin()
        best_ask_price_ = sell_levels_.begin()->first;
    }
};
