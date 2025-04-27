#pragma once

#include "bpnode_ptr.h"
#include "data_format/boot.h"
#include "page_cache.h"
#include <stdint.h>
#include <string>
#include <unistd.h>
#include <utility>

#define offset_ptr(node) ((char *)(node) + sizeof(*node))

struct BConfig {
  uint32_t block_size = 0; // 块大小
  std::string file_name;   // 文件名
  uint32_t cache_size = 0; // 缓存大小
};

class BMap {
public:
  friend class BMapVisualizer;
  BMap(const BConfig &conf) : conf_(conf) {}
  int BOpen();
  int BClose();
  int BplusTreeInsert(key_t key, long ldata);
  std::pair<long, bool> BplusTreeSearch(key_t key);
  int BplusTreeDelete(key_t key);
  uint32_t GetMaxIndexNum() const { return max_index_num_; }
  uint32_t GetMaxDataNum() const { return max_data_num_; }

private:
  static constexpr uint64_t INVALID_OFFSET = 0xdeadbeef;
  off_t ReadOffset(int fd);
  int BCheckConfig(const BConfig &conf) const;
  int BNodeBinarySearch(const BpNodePtr &node, key_t target) const;
  int IsLeaf(const BpNodePtr &node) const;
  int ParentKeyIndex(BpNodePtr &parent, key_t key) const;
  void NodeNew(NodeType type, BpNodePtr &node);
  BpNodePtr NodeFetch(off_t offset);
  BpNodePtr NodeSeek(off_t offset);
  void NodeFlush(BpNodePtr &node);
  BpNodePtr GetFreeNode();
  void NodeDelete(BpNodePtr &node, BpNodePtr &left, BpNodePtr &right);
  void SubNodeUpdate(BpNodePtr &parent, int index, BpNodePtr &sub_node);
  void SubNodeFlush(BpNodePtr &parent, off_t sub_offset);
  void LeftNodeAdd(BpNodePtr &node, BpNodePtr &left);
  void RightNodeAdd(BpNodePtr &node, BpNodePtr &right);
  int ParentNodeBuild(BpNodePtr &l_ch, BpNodePtr &r_ch, key_t key);
  key_t NonLeafSplitLeft(BpNodePtr &node, BpNodePtr &left, BpNodePtr &l_ch,
                         BpNodePtr &r_ch, key_t key, int insert, int split);
  key_t NonLeafSplitMiddle(BpNodePtr &node, BpNodePtr &right, BpNodePtr &l_ch,
                           BpNodePtr &r_ch, key_t key, int insert, int split);
  key_t NonLeafSplitRight(BpNodePtr &node, BpNodePtr &right, BpNodePtr &l_ch,
                          BpNodePtr &r_ch, key_t key, int insert, int split);
  void NonLeafSimpleInsert(BpNodePtr &node, BpNodePtr &l_ch, BpNodePtr &r_ch,
                           key_t key, int insert);
  int NonLeafInsert(BpNodePtr &node, BpNodePtr &l_ch, BpNodePtr &r_ch,
                    key_t key);
  key_t LeafSplitLeft(BpNodePtr &leaf, BpNodePtr &left, key_t key, long ldata,
                      int insert);
  key_t LeafSplitRight(BpNodePtr &leaf, BpNodePtr &right, key_t key, long ldata,
                       int insert);
  void LeafSimpleInsert(BpNodePtr &leaf, key_t key, long ldata, int insert);
  int LeafInsert(BpNodePtr &leaf, key_t key, long data);
  int SiblingSelect(BpNodePtr &l_sib, BpNodePtr &r_sib, BpNodePtr &parent,
                    int i);
  void NonLeafShiftFromLeft(BpNodePtr &node, BpNodePtr &left, BpNodePtr &parent,
                            int parent_key_index, int remove);
  void NonLeafMergeIntoLeft(BpNodePtr &node, BpNodePtr &left, BpNodePtr &parent,
                            int parent_key_index, int remove);
  void NonLeafShiftFromRight(BpNodePtr &node, BpNodePtr &right,
                             BpNodePtr &parent, int parent_key_index);
  void NonLeafMergeFromRight(BpNodePtr &node, BpNodePtr &right,
                             BpNodePtr &parent, int parent_key_index);
  void NonLeafSimpleRemove(BpNodePtr &node, int remove);
  void NonLeafRemove(BpNodePtr &node, int remove);
  int LeafRemove(BpNodePtr &leaf, key_t key);
  void LeafSimpleRemove(BpNodePtr &leaf, int remove);
  void LeafMergeFromRight(BpNodePtr &leaf, BpNodePtr &right);
  void LeafShiftFromRight(BpNodePtr &leaf, BpNodePtr &right, BpNodePtr &parent,
                          int parent_key_index);
  void LeafMergeIntoLeft(BpNodePtr &leaf, BpNodePtr &left, int parent_key_index,
                         int remove);
  void LeafShiftFromLeft(BpNodePtr &leaf, BpNodePtr &left, BpNodePtr &parent,
                         int parent_key_index, int remove);

private:
  BConfig conf_;
  Boot boot_;
  uint32_t max_index_num_ = 0;
  uint32_t max_data_num_ = 0;
  int tree_fd_ = -1;
  int boot_fd_ = -1;
  PageLruCache cache_;
};

struct NodeBackLog {
  /* Node的偏移量 */
  off_t offset;
  /* 下一个还没有输出的子节点 >= 1 */
  int next_sub_idx;
};

class BMapVisualizer {
public:
  BMapVisualizer(BMap &bmap) : bmap_(bmap){};
  ~BMapVisualizer() = default;

  void Visualize();
  void NodeKeyDraw(const BpNodePtr &node);
  void Draw(const BpNodePtr &node, NodeBackLog *stack, int level);

private:
  BMap &bmap_;
};