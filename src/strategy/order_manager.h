// Copyright [2020] <Copyright Kevin, kevin.lau.gd@gmail.com>

#ifndef FT_SRC_STRATEGY_ORDER_MANAGER_H_
#define FT_SRC_STRATEGY_ORDER_MANAGER_H_

#include <map>

#include "core/protocol.h"
#include "utils/misc.h"

namespace ft {

struct OrderInfo {
  uint32_t user_order_id = 0;
  uint32_t order_id = 0;
  uint32_t ticker_index = 0;
  uint32_t direction = 0;
  uint32_t offset = 0;
  int original_volume = 0;
  int traded_volume = 0;
};

class OrderManager {
 public:
  void update_order_status(const OrderResponse& rsp);

  void for_each(std::function<bool(const OrderInfo&)>&& f) {
    for (const auto& [order_id, order_info] : order_map_) {
      UNUSED(order_id);
      if (!f(order_info)) return;
    }
  }

 private:
  std::map<uint64_t, OrderInfo> order_map_;
};

}  // namespace ft

#endif  // FT_SRC_STRATEGY_ORDER_MANAGER_H_
