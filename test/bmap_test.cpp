#include "bmap.h"
#include <iostream>

constexpr uint32_t kLoopNum = 20000;

int main() {
  BConfig conf{4096, "test.db", 2000};
  BMap bmap(conf);
  if (bmap.BOpen()) {
    return -1;
  }
  // 插入
  for (int i = 0; i < kLoopNum; i++) {
    if (bmap.BplusTreeInsert(i, i)) {
      std::cout << "insert error " << i << std::endl;
    }
  }
  // 查找
  for (int i = 0; i < kLoopNum; i++) {
    auto [value, find] = bmap.BplusTreeSearch(i);
    if (!find || value != i) {
      std::cout << "not find " << i << std::endl;
    }
  }
  BMapVisualizer visualizer(bmap);
  visualizer.Visualize();
  // 删除
  for (int i = 0; i < kLoopNum; i++) {
    if (bmap.BplusTreeDelete(i)) {
      std::cout << "delete error " << i << std::endl;
    }
  }
  // 再次查找
  for (int i = 0; i < kLoopNum; i++) {
    auto [value, find] = bmap.BplusTreeSearch(i);
    if (find) {
      std::cout << "error find " << i << std::endl;
    }
  }

  bmap.BClose();
  return 0;
}