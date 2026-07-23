// ============================================================================
// OrderBook 单元测试
// ============================================================================
// 覆盖教材第 1 小节的所有核心场景：
//   - 添加订单（买单降序、卖单升序）
//   - Top of Book 查询（best_bid / best_ask）
//   - 撤单（正常 / 不存在 / 重复）
//   - 价格档位自动创建与清理
//   - 同一价格 FIFO 队列顺序
//   - 行情快照生成
// ============================================================================

#include <cstdio>
#include <cassert>
#include <cstring>
#include "OrderBook.h"

// 辅助：创建订单（从栈分配，仅用于测试）
static Order make_order(uint64_t id, OrderSide side, OrderType type,
                        uint64_t price, uint64_t qty, uint64_t ts = 0) {
    return Order(id, side, type, price, qty, ts);
}

// 辅助：输出测试结果
static int tests_passed = 0;
static int tests_failed = 0;

#define TEST(name, expr) do { \
    if (!(expr)) { \
        printf("  FAIL: %s\n", name); \
        ++tests_failed; \
    } else { \
        printf("  PASS: %s\n", name); \
        ++tests_passed; \
    } \
} while(0)

// ============================================================================
// 测试用例
// ============================================================================

static void test_add_buy_orders() {
    printf("\n[测试] 添加买单 — 价格降序排列\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 500);
    Order o2 = make_order(2, OrderSide::BUY, OrderType::LIMIT, 10100, 300);
    Order o3 = make_order(3, OrderSide::BUY, OrderType::LIMIT, 10050, 200);

    // 按任意顺序插入
    book.add_order(&o1);  // 100.00
    book.add_order(&o2);  // 101.00
    book.add_order(&o3);  // 100.50

    // best bid 应该是最高价格
    TEST("best_bid == 10100", book.best_bid() == 10100);
    TEST("best_bid price level has 300 qty",
         book.buy_head() && book.buy_head()->total_quantity == 300);

    // 验证价格降序：10100 → 10050 → 10000
    TEST("buy chain descending: first 10100",
         book.buy_head() && book.buy_head()->price == 10100);
    TEST("buy chain descending: second 10050",
         book.buy_head() && book.buy_head()->next &&
         book.buy_head()->next->price == 10050);
    TEST("buy chain descending: third 10000",
         book.buy_head() && book.buy_head()->next &&
         book.buy_head()->next->next &&
         book.buy_head()->next->next->price == 10000);
    TEST("buy chain has 3 levels",
         book.buy_head() && book.buy_head()->next &&
         book.buy_head()->next->next &&
         book.buy_head()->next->next->next == nullptr);
}

static void test_add_sell_orders() {
    printf("\n[测试] 添加卖单 — 价格升序排列\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::SELL, OrderType::LIMIT, 10200, 400);
    Order o2 = make_order(2, OrderSide::SELL, OrderType::LIMIT, 10150, 200);
    Order o3 = make_order(3, OrderSide::SELL, OrderType::LIMIT, 10175, 300);

    book.add_order(&o1);  // 102.00
    book.add_order(&o2);  // 101.50
    book.add_order(&o3);  // 101.75

    // best ask 应该是最低价格
    TEST("best_ask == 10150", book.best_ask() == 10150);
    TEST("best_ask price level has 200 qty",
         book.sell_head() && book.sell_head()->total_quantity == 200);

    // 验证价格升序：10150 → 10175 → 10200
    TEST("sell chain ascending: first 10150",
         book.sell_head() && book.sell_head()->price == 10150);
    TEST("sell chain ascending: second 10175",
         book.sell_head() && book.sell_head()->next &&
         book.sell_head()->next->price == 10175);
    TEST("sell chain ascending: third 10200",
         book.sell_head() && book.sell_head()->next &&
         book.sell_head()->next->next &&
         book.sell_head()->next->next->price == 10200);
}

static void test_top_of_book() {
    printf("\n[测试] Top of Book 查询\n");

    OrderBook empty_book;
    TEST("empty book: best_bid == 0", empty_book.best_bid() == 0);
    TEST("empty book: best_ask == 0", empty_book.best_ask() == 0);
    TEST("empty book: bid_quantity == 0", empty_book.bid_quantity() == 0);
    TEST("empty book: ask_quantity == 0", empty_book.ask_quantity() == 0);
    TEST("empty book: get_best_bid_order == nullptr",
         empty_book.get_best_bid_order() == nullptr);
    TEST("empty book: get_best_ask_order == nullptr",
         empty_book.get_best_ask_order() == nullptr);

    OrderBook book;
    Order bid = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 500);
    Order ask = make_order(2, OrderSide::SELL, OrderType::LIMIT, 10150, 300);
    book.add_order(&bid);
    book.add_order(&ask);

    TEST("best_bid == 10000", book.best_bid() == 10000);
    TEST("best_ask == 10150", book.best_ask() == 10150);
    TEST("bid_quantity == 500", book.bid_quantity() == 500);
    TEST("ask_quantity == 300", book.ask_quantity() == 300);
    TEST("get_best_bid_order id==1",
         book.get_best_bid_order() && book.get_best_bid_order()->order_id == 1);
    TEST("get_best_ask_order id==2",
         book.get_best_ask_order() && book.get_best_ask_order()->order_id == 2);
}

static void test_fifo_within_level() {
    printf("\n[测试] 同一价格档位的 FIFO 顺序\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 100, 100);
    Order o2 = make_order(2, OrderSide::BUY, OrderType::LIMIT, 10000, 200, 200);
    Order o3 = make_order(3, OrderSide::BUY, OrderType::LIMIT, 10000, 300, 300);

    book.add_order(&o1);
    book.add_order(&o2);
    book.add_order(&o3);

    // 同一价格只有一个 PriceLevel
    const PriceLevel* level = book.buy_head();
    TEST("single price level", level && level->next == nullptr);
    TEST("total_quantity == 600", level && level->total_quantity == 600);

    // FIFO: 先拿到的应该是 order 1
    Order* front = book.get_best_bid_order();
    TEST("FIFO head is order 1", front && front->order_id == 1);

    // 模拟撮合移除头部
    book.cancel_order(1);
    front = book.get_best_bid_order();
    TEST("FIFO head is order 2 after remove 1", front && front->order_id == 2);
}

static void test_cancel_order() {
    printf("\n[测试] 撤单操作\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 500);
    Order o2 = make_order(2, OrderSide::BUY, OrderType::LIMIT, 10100, 300);
    book.add_order(&o1);
    book.add_order(&o2);

    TEST("before cancel: best_bid == 10100", book.best_bid() == 10100);
    TEST("before cancel: order_count == 2", book.order_count() == 2);

    // 撤单 2（best bid 订单）
    CancelResult r = book.cancel_order(2);
    TEST("cancel result SUCCESS", r == CancelResult::SUCCESS);

    TEST("after cancel: best_bid == 10000", book.best_bid() == 10000);
    TEST("after cancel: order_count == 1", book.order_count() == 1);
    TEST("after cancel: o2 status CANCELED", o2.status == OrderStatus::CANCELED);

    // 重复撤单（订单已从 order_map 移除 → NOT_FOUND）
    r = book.cancel_order(2);
    TEST("cancel again: NOT_FOUND (already removed from map)", r == CancelResult::NOT_FOUND);

    // 撤不存在的订单
    r = book.cancel_order(999);
    TEST("cancel nonexistent: NOT_FOUND", r == CancelResult::NOT_FOUND);

    // 撤单后价格档位自动清理
    book.cancel_order(1);
    TEST("after cancel all: no buy levels", book.buy_head() == nullptr);
    TEST("after cancel all: order_count == 0", book.order_count() == 0);
}

static void test_cancel_middle_order() {
    printf("\n[测试] 撤单中间订单（非头非尾）\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 100);
    Order o2 = make_order(2, OrderSide::BUY, OrderType::LIMIT, 10000, 200);
    Order o3 = make_order(3, OrderSide::BUY, OrderType::LIMIT, 10000, 300);
    book.add_order(&o1);
    book.add_order(&o2);
    book.add_order(&o3);

    // 撤单中间的 o2
    book.cancel_order(2);

    const PriceLevel* level = book.buy_head();
    TEST("level still exists", level != nullptr);
    TEST("total_quantity == 400", level && level->total_quantity == 400);
    TEST("head still o1", level && level->head && level->head->order_id == 1);
    TEST("tail now o3", level && level->tail && level->tail->order_id == 3);
    TEST("o1->next == o3", level && level->head && level->head->next &&
         level->head->next->order_id == 3);
    TEST("o3->prev == o1", level && level->tail && level->tail->prev &&
         level->tail->prev->order_id == 1);
}

static void test_execute_and_remove() {
    printf("\n[测试] 成交执行与订单移除\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::SELL, OrderType::LIMIT, 10150, 500);
    book.add_order(&o1);

    // 模拟成交 200
    bool ok = book.execute_order(1, 200);
    TEST("execute success", ok);
    TEST("o1 filled == 200", o1.filled == 200);
    TEST("o1 remaining == 300", o1.remaining() == 300);
    TEST("o1 status PARTIALLY_FILLED",
         o1.status == OrderStatus::PARTIALLY_FILLED);
    TEST("level total_quantity == 300",
         book.sell_head() && book.sell_head()->total_quantity == 300);

    // 继续成交 300（全部成交）
    book.execute_order(1, 300);
    TEST("o1 filled == 500", o1.filled == 500);
    TEST("o1 is_filled", o1.is_filled());
    TEST("o1 status FILLED", o1.status == OrderStatus::FILLED);
    TEST("level total_quantity == 0",
         book.sell_head() && book.sell_head()->total_quantity == 0);

    // 移除已成交订单
    book.remove_order(1);
    // 取消一个已成交但尚未 remove 的订单 → ALREADY_DONE
    Order o2 = make_order(2, OrderSide::SELL, OrderType::LIMIT, 10150, 100);
    book.add_order(&o2);
    book.execute_order(2, 100);  // 全部成交，状态变为 FILLED
    CancelResult cr = book.cancel_order(2);
    TEST("cancel filled order: ALREADY_DONE", cr == CancelResult::ALREADY_DONE);
    // 清理已成交订单
    book.remove_order(2);

    TEST("after remove: sell_head == nullptr", book.sell_head() == nullptr);
    TEST("after remove: order_count == 0", book.order_count() == 0);
}

static void test_find_order() {
    printf("\n[测试] 订单查找\n");

    OrderBook book;
    Order o1 = make_order(1, OrderSide::BUY, OrderType::LIMIT, 10000, 500);
    book.add_order(&o1);

    Order* found = book.find_order(1);
    TEST("find existing order", found != nullptr && found->order_id == 1);

    found = book.find_order(999);
    TEST("find nonexistent order == nullptr", found == nullptr);

    book.cancel_order(1);
    found = book.find_order(1);
    TEST("find canceled order == nullptr", found == nullptr);
}

static void test_snapshot() {
    printf("\n[测试] 行情快照生成\n");

    OrderBook book;
    Order bids[] = {
        make_order(1, OrderSide::BUY, OrderType::LIMIT, 10100, 500),
        make_order(2, OrderSide::BUY, OrderType::LIMIT, 10050, 300),
        make_order(3, OrderSide::BUY, OrderType::LIMIT, 10000, 1000),
        make_order(4, OrderSide::BUY, OrderType::LIMIT, 9950,  200),
    };
    Order asks[] = {
        make_order(5, OrderSide::SELL, OrderType::LIMIT, 10150, 200),
        make_order(6, OrderSide::SELL, OrderType::LIMIT, 10200, 400),
        make_order(7, OrderSide::SELL, OrderType::LIMIT, 10250, 300),
    };

    for (auto& o : bids) book.add_order(&o);
    for (auto& o : asks) book.add_order(&o);

    SnapshotLevel bid_levels[10];
    SnapshotLevel ask_levels[10];

    auto [bid_depth, ask_depth] = book.snapshot(10, bid_levels, ask_levels);

    TEST("bid depth == 4", bid_depth == 4);
    TEST("ask depth == 3", ask_depth == 3);

    // 买单降序
    TEST("snapshot bid[0] price 10100", bid_levels[0].price == 10100);
    TEST("snapshot bid[1] price 10050", bid_levels[1].price == 10050);
    TEST("snapshot bid[2] price 10000", bid_levels[2].price == 10000);
    TEST("snapshot bid[3] price 9950",  bid_levels[3].price == 9950);

    // 卖单升序
    TEST("snapshot ask[0] price 10150", ask_levels[0].price == 10150);
    TEST("snapshot ask[1] price 10200", ask_levels[1].price == 10200);
    TEST("snapshot ask[2] price 10250", ask_levels[2].price == 10250);

    // 限制深度
    auto [d2, _] = book.snapshot(2, bid_levels, ask_levels);
    TEST("snapshot depth=2 gives 2 bids", d2 == 2);
}

// ============================================================================
// 主函数
// ============================================================================

int main() {
    printf("========================================\n");
    printf("  OrderBook 单元测试\n");
    printf("  教材第六章第 1 小节 — 订单簿数据结构\n");
    printf("========================================\n");

    test_add_buy_orders();
    test_add_sell_orders();
    test_top_of_book();
    test_fifo_within_level();
    test_cancel_order();
    test_cancel_middle_order();
    test_execute_and_remove();
    test_find_order();
    test_snapshot();

    printf("\n========================================\n");
    printf("  结果: %d / %d 通过",
           tests_passed, tests_passed + tests_failed);
    if (tests_failed > 0) {
        printf(", %d 失败", tests_failed);
    }
    printf("\n========================================\n");

    return tests_failed > 0 ? 1 : 0;
}
