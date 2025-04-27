#include "bmap.h"
#include <assert.h>
#include <ctype.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>

int BMap::BCheckConfig(const BConfig &conf) const {
  // 文件名不能太长
  if (conf.file_name.length() > 1024) {
    return -1;
  }
  // block_size必须是系统文件块大小的整数倍
  struct stat fi;
  std::string file_name = conf.file_name + ".boot";
  if (stat(file_name.c_str(), &fi) != 0) {
    return -1;
  }

  blksize_t sys_block_size = fi.st_blksize;
  // Check if block_size is multiple of system block size
  if (conf.block_size % sys_block_size != 0) {
    fprintf(stderr, "Block size must be multiple of system block size (%lu)\n",
            (unsigned long)sys_block_size);
    return -1;
  }
  return 0;
}

off_t BMap::ReadOffset(int fd) {
  constexpr uint32_t ADDR_LEN_HEX = 16;
  char buf[ADDR_LEN_HEX];
  ssize_t len = read(fd, buf, ADDR_LEN_HEX);
  if (len > 0)
    return std::stoull(buf, nullptr, 16);
  else
    return INVALID_OFFSET;
  return 0;
}

int BMap::BOpen() {
  std::string boot_file = conf_.file_name + ".boot";
  boot_fd_ = open(boot_file.c_str(), O_RDWR, 0644);
  if (boot_fd_ > 0) {
    boot_.ParseFromFile(boot_fd_);
  } else {
    boot_.root_offset = INVALID_OFFSET;
    boot_.file_size = 0;
    boot_.block_size = conf_.block_size;
    boot_fd_ = open(boot_file.c_str(), O_CREAT | O_RDWR, 0644);
    assert(boot_fd_ > 0);
    if (BCheckConfig(conf_)) {
      close(boot_fd_);
      unlink(boot_file.c_str());
      return -1;
    }
    boot_.WriteToFile(boot_fd_);
  }

  if (cache_.Init(conf_.file_name, conf_.block_size, conf_.cache_size)) {
    return -1;
  }
  tree_fd_ = cache_.Fd();
  max_index_num_ =
      (boot_.block_size - sizeof(BpNode)) / (sizeof(key_t) + sizeof(off_t));
  max_data_num_ =
      (boot_.block_size - sizeof(BpNode)) / (sizeof(key_t) + sizeof(long));
  return 0;
}

int BMap::BClose() {
  boot_.WriteToFile(boot_fd_);
  if (boot_fd_ > 0) {
    close(boot_fd_);
  }
  if (tree_fd_ > 0) {
    close(tree_fd_);
  }
  return 0;
}

// 是否叶子节点
int BMap::IsLeaf(const BpNodePtr &node) const { return node->type == LEAF; }

// 二分查找，如果找到了，返回index
// 如果没找到，返回比target大的index的负数-1
int BMap::BNodeBinarySearch(const BpNodePtr &node, key_t target) const {
  const key_t *arr = node.Key();
  int len = IsLeaf(node) ? node->children : node->children - 1;
  int low = -1;
  int high = len;

  while (low + 1 < high) {
    int mid = low + (high - low) / 2;
    if (target > arr[mid]) {
      low = mid;
    } else {
      high = mid;
    }
  }

  if (high >= len || arr[high] != target) {
    return -high - 1;
  } else {
    return high;
  }
}

// 当前key在父节点中的index
// 如果找到了,直接返回index
// 如果没找到,返回-index - 2
// 因为BNodeBinarySearch返回的是比target大的index的负数-1
// -index - 2相当于index - 1,也就是比key小的index
int BMap::ParentKeyIndex(BpNodePtr &parent, key_t key) const {
  int index = BNodeBinarySearch(parent, key);
  return index >= 0 ? index : -index - 2;
}

void BMap::NodeNew(NodeType type, BpNodePtr &node) {
  node->parent = INVALID_OFFSET;
  node->prev = INVALID_OFFSET;
  node->next = INVALID_OFFSET;
  node->children = 0;
  node->type = type;
  return;
}

// 需要改
BpNodePtr BMap::NodeFetch(off_t offset) {
  if (offset == INVALID_OFFSET) {
    return BpNodePtr();
  }
  auto iter = cache_.GetPage(offset, false);
  return BpNodePtr(this, iter);
}

BpNodePtr BMap::NodeSeek(off_t offset) {
  if (offset == INVALID_OFFSET) {
    return BpNodePtr();
  }

  auto iter = cache_.GetPage(offset, false);
  return BpNodePtr(this, iter);
}

void BMap::NodeFlush(BpNodePtr &node) {
  if (node != NULL) {
    int len = pwrite(tree_fd_, &*node, conf_.block_size, node->self);
    assert(len == conf_.block_size);
    // 临时写法
    node.dirty_ = false;
  }
}

BpNodePtr BMap::GetFreeNode() {
  PageCacheIter iter;
  BpNodePtr node_ptr;
  if (boot_.free_blocks.empty()) {
    iter = cache_.GetPage(boot_.file_size, true);
    node_ptr = BpNodePtr(this, iter);
    node_ptr->self = boot_.file_size;
    boot_.file_size += conf_.block_size;
  } else {
    auto block = boot_.free_blocks.front();
    boot_.free_blocks.pop_front();
    iter = cache_.GetPage(block, false);
    node_ptr = BpNodePtr(this, iter);
  }
  return node_ptr;
}

void BMap::NodeDelete(BpNodePtr &node, BpNodePtr &left, BpNodePtr &right) {
  // 先修改左右相邻节点的prev和next指针
  if (left != NULL) {
    if (right != NULL) {
      left->next = right->self;
      right->prev = left->self;
      NodeFlush(right);
    } else {
      left->next = INVALID_OFFSET;
    }
    NodeFlush(left);
  } else {
    if (right != NULL) {
      right->prev = INVALID_OFFSET;
      NodeFlush(right);
    }
  }
  // 再修改boot的free_blocks
  assert(node->self != INVALID_OFFSET);
  boot_.free_blocks.push_back(node->self);
}

void BMap::SubNodeUpdate(BpNodePtr &parent, int index, BpNodePtr &sub_node) {
  assert(sub_node->self != INVALID_OFFSET);
  parent.Sub()[index] = sub_node->self;
  sub_node->parent = parent->self;
  NodeFlush(sub_node);
}

void BMap::SubNodeFlush(BpNodePtr &parent, off_t sub_offset) {
  BpNodePtr sub_node = NodeFetch(sub_offset);
  assert(sub_node != NULL);
  sub_node->parent = parent->self;
  NodeFlush(sub_node);
}

std::pair<long, bool> BMap::BplusTreeSearch(key_t key) {
  long vaule;
  bool find = false;
  BpNodePtr node = NodeSeek(boot_.root_offset);
  while (node != NULL) {
    // 找到第一个大于等于key的
    int i = BNodeBinarySearch(node, key);
    if (IsLeaf(node)) {
      if (i >= 0) {
        find = true;
        vaule = node.Data()[i];
      }
      break;
    } else {
      if (i >= 0) {
        // 非叶子节点,key的index比data的index少1
        node = NodeSeek(node.Sub()[i + 1]);
      } else {
        i = -i - 1; // 找到第一个大于key的
        // 非叶子节点,key的index比data的index少1,所以i是小于key的最大的节点
        node = NodeSeek(node.Sub()[i]);
      }
    }
  }

  return {vaule, find};
}

void BMap::LeftNodeAdd(BpNodePtr &node, BpNodePtr &left) {
  BpNodePtr prev = NodeFetch(node->prev);
  if (prev != NULL) {
    prev->next = left->self;
    left->prev = prev->self;
  } else {
    left->prev = INVALID_OFFSET;
  }
  left->next = node->self;
  node->prev = left->self;
}

void BMap::RightNodeAdd(BpNodePtr &node, BpNodePtr &right) {
  BpNodePtr next = NodeFetch(node->next);
  if (next != NULL) {
    next->prev = right->self;
    right->next = next->self;
    NodeFlush(next);
  } else {
    right->next = INVALID_OFFSET;
  }
  right->prev = node->self;
  node->next = right->self;
}

int BMap::ParentNodeBuild(BpNodePtr &l_ch, BpNodePtr &r_ch, key_t key) {
  if (l_ch->parent == INVALID_OFFSET && r_ch->parent == INVALID_OFFSET) {
    /* new parent */
    BpNodePtr parent = GetFreeNode();
    NodeNew(NON_LEAF, parent);
    parent.Key()[0] = key;
    parent.Sub()[0] = l_ch->self;
    parent.Sub()[1] = r_ch->self;

    parent->children = 2;
    /* write new parent and update root */
    boot_.root_offset = parent->self;
    l_ch->parent = parent->self;
    r_ch->parent = parent->self;
    /* flush parent, left and right child */
    cache_.SyncPage(l_ch->self);
    cache_.SyncPage(r_ch->self);
    cache_.SyncPage(parent->self);
    return 0;
  } else if (r_ch->parent == INVALID_OFFSET) {
    BpNodePtr tmp_pa = NodeFetch(l_ch->parent);
    return NonLeafInsert(tmp_pa, l_ch, r_ch, key);
  } else {
    BpNodePtr tmp_pa = NodeFetch(l_ch->parent);
    return NonLeafInsert(tmp_pa, l_ch, r_ch, key);
  }
}

key_t BMap::NonLeafSplitLeft(BpNodePtr &node, BpNodePtr &left, BpNodePtr &l_ch,
                             BpNodePtr &r_ch, key_t key, int insert,
                             int split) {
  int i;

  /* split key is key[split - 1] */
  key_t split_key = node.Key()[split - 1];

  /* split as left sibling */
  LeftNodeAdd(node, left);

  /* calculate split nodes' children (sum as (order + 1))*/
  int pivot = insert;
  left->children = split + 1;
  node->children = max_index_num_ - split;

  /* sum = left->children = pivot + (split - pivot) + 1 */
  /* replicate from key[0] to key[insert] in original node */
  memmove(&left.Key()[0], &node.Key()[0], pivot * sizeof(key_t));
  memmove(&left.Sub()[0], &node.Sub()[0], pivot * sizeof(off_t));

  /* replicate from key[insert] to key[split] in original node */
  memmove(&left.Key()[pivot + 1], &node.Key()[pivot],
          (split - pivot) * sizeof(key_t));
  memmove(&left.Sub()[pivot + 1], &node.Sub()[pivot],
          (split - pivot) * sizeof(off_t));

  /* flush sub-nodes of the new splitted left node */
  for (i = 0; i < left->children; i++) {
    if (i != pivot && i != pivot + 1) {
      SubNodeFlush(left, left.Sub()[i]);
    }
  }

  /* insert new key and sub-nodes and locate the split key */
  left.Key()[pivot] = key;
  SubNodeUpdate(left, pivot, l_ch);
  SubNodeUpdate(left, pivot + 1, r_ch);

  /* sum = node->children = 1 + (node->children - 1) */
  /* right node left shift from key[split] to key[children - 2] */
  memmove(&node.Key()[0], &node.Key()[split],
          (node->children - 1) * sizeof(key_t));
  memmove(&node.Sub()[0], &node.Sub()[split], (node->children) * sizeof(off_t));

  return split_key;
}

key_t BMap::NonLeafSplitMiddle(BpNodePtr &node, BpNodePtr &right,
                               BpNodePtr &l_ch, BpNodePtr &r_ch, key_t key,
                               int insert, int split) {
  int i;

  /* split as right sibling */
  RightNodeAdd(node, right);

  /* split key is key[split - 1] */
  key_t split_key = node.Key()[split - 1];

  /* calculate split nodes' children (sum as (order + 1))*/
  int pivot = 0;
  node->children = split;
  right->children = max_index_num_ - split + 1;

  /* insert new key and sub-nodes */
  right.Key()[pivot] = key;
  SubNodeUpdate(right, pivot, l_ch);
  SubNodeUpdate(right, pivot + 1, r_ch);

  /* sum = right->children = 2 + (right->children - 2) */
  /* replicate from key[split] to key[_max_order - 2] */
  memmove(&right.Key()[pivot + 1], &node.Key()[split],
          (right->children - 2) * sizeof(key_t));
  memmove(&right.Sub()[pivot + 2], &node.Sub()[split + 1],
          (right->children - 2) * sizeof(off_t));

  /* flush sub-nodes of the new splitted right node */
  for (i = pivot + 2; i < right->children; i++) {
    SubNodeFlush(right, right.Sub()[i]);
  }

  return split_key;
}

key_t BMap::NonLeafSplitRight(BpNodePtr &node, BpNodePtr &right,
                              BpNodePtr &l_ch, BpNodePtr &r_ch, key_t key,
                              int insert, int split) {
  int i;

  /* split as right sibling */
  RightNodeAdd(node, right);

  /* split key is key[split] */
  key_t split_key = node.Key()[split];

  /* calculate split nodes' children (sum as (order + 1))*/
  int pivot = insert - split - 1;
  node->children = split + 1;
  right->children = max_index_num_ - split;

  /* sum = right->children = pivot + 2 + (_max_order - insert - 1) */
  /* replicate from key[split + 1] to key[insert] */
  memmove(&right.Key()[0], &node.Key()[split + 1], pivot * sizeof(key_t));
  memmove(&right.Sub()[0], &node.Sub()[split + 1], pivot * sizeof(off_t));

  /* insert new key and sub-node */
  right.Key()[pivot] = key;
  SubNodeUpdate(right, pivot, l_ch);
  SubNodeUpdate(right, pivot + 1, r_ch);

  /* replicate from key[insert] to key[order - 1] */
  memmove(&right.Key()[pivot + 1], &node.Key()[insert],
          (max_index_num_ - insert - 1) * sizeof(key_t));
  memmove(&right.Sub()[pivot + 2], &node.Sub()[insert + 1],
          (max_index_num_ - insert - 1) * sizeof(off_t));

  /* flush sub-nodes of the new splitted right node */
  for (i = 0; i < right->children; i++) {
    if (i != pivot && i != pivot + 1) {
      SubNodeFlush(right, right.Sub()[i]);
    }
  }

  return split_key;
}

void BMap::NonLeafSimpleInsert(BpNodePtr &node, BpNodePtr &l_ch,
                               BpNodePtr &r_ch, key_t key, int insert) {
  memmove(&node.Key()[insert + 1], &node.Key()[insert],
          (node->children - 1 - insert) * sizeof(key_t));
  memmove(&node.Sub()[insert + 2], &node.Sub()[insert + 1],
          (node->children - 1 - insert) * sizeof(off_t));
  /* insert new key and sub-nodes */
  node.Key()[insert] = key;
  SubNodeUpdate(node, insert, l_ch);
  SubNodeUpdate(node, insert + 1, r_ch);
  node->children++;
}

int BMap::NonLeafInsert(BpNodePtr &node, BpNodePtr &l_ch, BpNodePtr &r_ch,
                        key_t key) {
  /* Search key location */
  int insert = BNodeBinarySearch(node, key);
  assert(insert < 0);
  insert = -insert - 1;

  /* node is full */
  if (node->children == max_index_num_) {
    key_t split_key;
    /* split = [m/2] */
    // 这里split是分裂后右边的第一个位置
    int split = node->children / 2;
    BpNodePtr sibling = GetFreeNode();
    NodeNew(NON_LEAF, sibling);
    if (insert < split) {
      split_key =
          NonLeafSplitLeft(node, sibling, l_ch, r_ch, key, insert, split);
    } else if (insert == split) {
      split_key =
          NonLeafSplitMiddle(node, sibling, l_ch, r_ch, key, insert, split);
    } else {
      split_key =
          NonLeafSplitRight(node, sibling, l_ch, r_ch, key, insert, split);
    }

    /* build new parent */
    if (insert < split) {
      return ParentNodeBuild(sibling, node, split_key);
    } else {
      return ParentNodeBuild(node, sibling, split_key);
    }
  } else {
    NonLeafSimpleInsert(node, l_ch, r_ch, key, insert);
    NodeFlush(node);
  }
  return 0;
}

key_t BMap::LeafSplitLeft(BpNodePtr &leaf, BpNodePtr &left, key_t key,
                          long ldata, int insert) {
  /* split = [m/2] */
  int split = (leaf->children + 1) / 2;

  /* split as left sibling */
  LeftNodeAdd(leaf, left);

  /* calculate split leaves' children (sum as (entries + 1)) */
  int pivot = insert;
  left->children = split;
  leaf->children = max_index_num_ - split + 1;

  /* sum = left->children = pivot + 1 + (split - pivot - 1) */
  /* replicate from key[0] to key[insert] */
  memmove(&left.Key()[0], &leaf.Key()[0], pivot * sizeof(key_t));
  memmove(&left.Data()[0], &leaf.Data()[0], pivot * sizeof(long));

  /* insert new key and data */
  left.Key()[pivot] = key;
  left.Data()[pivot] = ldata;

  /* replicate from key[insert] to key[split - 1] */
  memmove(&left.Key()[pivot + 1], &leaf.Key()[pivot],
          (split - pivot - 1) * sizeof(key_t));
  memmove(&left.Data()[pivot + 1], &leaf.Data()[pivot],
          (split - pivot - 1) * sizeof(long));

  /* original leaf left shift */
  memmove(&leaf.Key()[0], &leaf.Key()[split - 1],
          leaf->children * sizeof(key_t));
  memmove(&leaf.Data()[0], &leaf.Data()[split - 1],
          leaf->children * sizeof(long));

  return leaf.Key()[0];
}

key_t BMap::LeafSplitRight(BpNodePtr &leaf, BpNodePtr &right, key_t key,
                           long ldata, int insert) {
  /* split = [m/2] */
  int split = (leaf->children + 1) / 2;

  /* split as right sibling */
  RightNodeAdd(leaf, right);

  /* calculate split leaves' children (sum as (entries + 1)) */
  int pivot = insert - split;
  leaf->children = split;
  right->children = max_data_num_ - split + 1;

  /* sum = right->children = pivot + 1 + (_max_entries - pivot - split) */
  /* replicate from key[split] to key[children - 1] in original leaf */
  memmove(&right.Key()[0], &leaf.Key()[split], pivot * sizeof(key_t));
  memmove(&right.Data()[0], &leaf.Data()[split], pivot * sizeof(long));

  /* insert new key and data */
  right.Key()[pivot] = key;
  right.Data()[pivot] = ldata;

  /* replicate from key[insert] to key[children - 1] in original leaf */
  memmove(&right.Key()[pivot + 1], &leaf.Key()[insert],
          (max_index_num_ - insert) * sizeof(key_t));
  memmove(&right.Data()[pivot + 1], &leaf.Data()[insert],
          (max_index_num_ - insert) * sizeof(long));

  return right.Key()[0];
}

// insert是key要插入的index(从0开始)
void BMap::LeafSimpleInsert(BpNodePtr &leaf, key_t key, long ldata,
                            int insert_idx) {
  memmove(&leaf.Key()[insert_idx + 1], &leaf.Key()[insert_idx],
          (leaf->children - insert_idx) * sizeof(key_t));
  memmove(&leaf.Data()[insert_idx + 1], &leaf.Data()[insert_idx],
          (leaf->children - insert_idx) * sizeof(long));
  leaf.Key()[insert_idx] = key;
  leaf.Data()[insert_idx] = ldata;
  leaf->children++;
}

int BMap::LeafInsert(BpNodePtr &leaf, key_t key, long data) {
  /* Search key location */
  int insert = BNodeBinarySearch(leaf, key);
  if (insert >= 0) {
    /* Already exists */
    return -1;
  }
  // insert是第一个比key大的index,也就是要插入的index
  insert = -insert - 1;

  /* leaf is full */
  if (leaf->children == max_data_num_) {
    key_t split_key;
    /* split = [m/2] */
    int split = (max_data_num_ + 1) / 2;
    BpNodePtr sibling = GetFreeNode();
    NodeNew(LEAF, sibling);
    /* sibling leaf replication due to location of insertion */
    if (insert < split) {
      split_key = LeafSplitLeft(leaf, sibling, key, data, insert);
    } else {
      split_key = LeafSplitRight(leaf, sibling, key, data, insert);
    }

    /* build new parent */
    if (insert < split) {
      return ParentNodeBuild(sibling, leaf, split_key);
    } else {
      return ParentNodeBuild(leaf, sibling, split_key);
    }
  } else {
    LeafSimpleInsert(leaf, key, data, insert);
    NodeFlush(leaf);
  }

  return 0;
}

int BMap::BplusTreeInsert(key_t key, long ldata) {
  BpNodePtr node = NodeSeek(boot_.root_offset);
  while (node != NULL) {
    if (IsLeaf(node)) {
      return LeafInsert(node, key, ldata);
    } else {
      int i = BNodeBinarySearch(node, key);
      if (i >= 0) {
        node = NodeSeek(node.Sub()[i + 1]);
      } else {
        i = -i - 1;
        node = NodeSeek(node.Sub()[i]);
      }
    }
  }

  /* new root */
  BpNodePtr root = GetFreeNode();
  NodeNew(LEAF, root);

  root.Key()[0] = key;
  root.Data()[0] = ldata;
  root->children = 1;
  boot_.root_offset = root->self;
  cache_.SyncPage(root->self);
  return 0;
}
enum SIBLING { RIGHT_SIBLING, LEFT_SIBLING };

int BMap::SiblingSelect(BpNodePtr &l_sib, BpNodePtr &r_sib, BpNodePtr &parent,
                        int i) {
  if (i == -1) {
    /* the frist sub-node, no left sibling, choose the right one */
    return RIGHT_SIBLING;
  } else if (i == parent->children - 2) {
    /* the last sub-node, no right sibling, choose the left one */
    return LEFT_SIBLING;
  } else {
    /* if both left and right sibling found, choose the one with more children
     */
    return l_sib->children >= r_sib->children ? LEFT_SIBLING : RIGHT_SIBLING;
  }
}

void BMap::NonLeafShiftFromLeft(BpNodePtr &node, BpNodePtr &left,
                                BpNodePtr &parent, int parent_key_index,
                                int remove) {
  /* node's elements right shift */
  memmove(&node.Key()[1], &node.Key()[0], remove * sizeof(key_t));
  memmove(&node.Sub()[1], &node.Sub()[0], (remove + 1) * sizeof(off_t));

  /* parent key right rotation */
  node.Key()[0] = parent.Key()[parent_key_index];
  parent.Key()[parent_key_index] = left.Key()[left->children - 2];

  /* borrow the last sub-node from left sibling */
  node.Sub()[0] = left.Sub()[left->children - 1];
  SubNodeFlush(node, node.Sub()[0]);

  left->children--;
}

void BMap::NonLeafMergeIntoLeft(BpNodePtr &node, BpNodePtr &left,
                                BpNodePtr &parent, int parent_key_index,
                                int remove) {
  /* move parent key down */
  left.Key()[left->children - 1] = parent.Key()[parent_key_index];

  /* merge into left sibling */
  /* key sum = node->children - 2 */
  memmove(&left.Key()[left->children], &node.Key()[0], remove * sizeof(key_t));
  memmove(&left.Sub()[left->children], &node.Sub()[0],
          (remove + 1) * sizeof(off_t));

  /* sub-node sum = node->children - 1 */
  memmove(&left.Key()[left->children + remove], &node.Key()[remove + 1],
          (node->children - remove - 2) * sizeof(key_t));
  memmove(&left.Sub()[left->children + remove + 1], &node.Sub()[remove + 2],
          (node->children - remove - 2) * sizeof(off_t));

  /* flush sub-nodes of the new merged left node */
  int i, j;
  for (i = left->children, j = 0; j < node->children - 1; i++, j++) {
    SubNodeFlush(left, left.Sub()[i]);
  }

  left->children += node->children - 1;
}

void BMap::NonLeafShiftFromRight(BpNodePtr &node, BpNodePtr &right,
                                 BpNodePtr &parent, int parent_key_index) {
  /* parent key left rotation */
  node.Key()[node->children - 1] = parent.Key()[parent_key_index];
  parent.Key()[parent_key_index] = right.Key()[0];

  /* borrow the frist sub-node from right sibling */
  node.Sub()[node->children] = right.Sub()[0];
  SubNodeFlush(node, node.Sub()[node->children]);
  node->children++;

  /* right sibling left shift*/
  memmove(&right.Key()[0], &right.Key()[1],
          (right->children - 2) * sizeof(key_t));
  memmove(&right.Sub()[0], &right.Sub()[1],
          (right->children - 1) * sizeof(off_t));

  right->children--;
}

void BMap::NonLeafMergeFromRight(BpNodePtr &node, BpNodePtr &right,
                                 BpNodePtr &parent, int parent_key_index) {
  /* move parent key down */
  node.Key()[node->children - 1] = parent.Key()[parent_key_index];
  node->children++;

  /* merge from right sibling */
  memmove(&node.Key()[node->children - 1], &right.Key()[0],
          (right->children - 1) * sizeof(key_t));
  memmove(&node.Sub()[node->children - 1], &right.Sub()[0],
          right->children * sizeof(off_t));

  /* flush sub-nodes of the new merged node */
  int i, j;
  for (i = node->children - 1, j = 0; j < right->children; i++, j++) {
    SubNodeFlush(node, node.Sub()[i]);
  }

  node->children += right->children - 1;
}

void BMap::NonLeafSimpleRemove(BpNodePtr &node, int remove) {
  assert(node->children >= 2);
  memmove(&node.Key()[remove], &node.Key()[remove + 1],
          (node->children - remove - 2) * sizeof(key_t));
  memmove(&node.Sub()[remove + 1], &node.Sub()[remove + 2],
          (node->children - remove - 2) * sizeof(off_t));
  node->children--;
}

void BMap::NonLeafRemove(BpNodePtr &node, int remove) {
  if (node->parent == INVALID_OFFSET) {
    /* node is the root */
    if (node->children == 2) {
      /* replace old root with the first sub-node */
      BpNodePtr root = NodeFetch(node.Sub()[0]);
      root->parent = INVALID_OFFSET;
      boot_.root_offset = root->self;
      BpNodePtr null_node;
      NodeDelete(node, null_node, null_node);
      NodeFlush(root);
    } else {
      NonLeafSimpleRemove(node, remove);
      NodeFlush(node);
    }
  } else if (node->children <= (max_index_num_ + 1) / 2) {
    BpNodePtr l_sib = NodeFetch(node->prev);
    BpNodePtr r_sib = NodeFetch(node->next);
    BpNodePtr parent = NodeFetch(node->parent);

    int i = ParentKeyIndex(parent, node.Key()[0]);

    /* decide which sibling to be borrowed from */
    if (SiblingSelect(l_sib, r_sib, parent, i) == LEFT_SIBLING) {
      if (l_sib->children > (max_index_num_ + 1) / 2) {
        NonLeafShiftFromLeft(node, l_sib, parent, i, remove);
        /* flush nodes */
        NodeFlush(node);
        NodeFlush(l_sib);
        NodeFlush(r_sib);
        NodeFlush(parent);
      } else {
        NonLeafMergeIntoLeft(node, l_sib, parent, i, remove);
        /* delete empty node and flush */
        NodeDelete(node, l_sib, r_sib);
        /* trace upwards */
        NonLeafRemove(parent, i);
      }
    } else {
      /* remove at first in case of overflow during merging with sibling */
      NonLeafSimpleRemove(node, remove);

      if (r_sib->children > (max_index_num_ + 1) / 2) {
        NonLeafShiftFromRight(node, r_sib, parent, i + 1);
        /* flush nodes */
        NodeFlush(node);
        NodeFlush(l_sib);
        NodeFlush(r_sib);
        NodeFlush(parent);
      } else {
        NonLeafMergeFromRight(node, r_sib, parent, i + 1);
        /* delete empty right sibling and flush */
        BpNodePtr rr_sib = NodeFetch(r_sib->next);
        NodeDelete(r_sib, node, rr_sib);
        NodeFlush(l_sib);
        /* trace upwards */
        NonLeafRemove(parent, i + 1);
      }
    }
  } else {
    NonLeafSimpleRemove(node, remove);
    NodeFlush(node);
  }
}

void BMap::LeafShiftFromLeft(BpNodePtr &leaf, BpNodePtr &left,
                             BpNodePtr &parent, int parent_key_index,
                             int remove) {
  /* right shift in leaf node */
  memmove(&leaf.Key()[1], &leaf.Key()[0], remove * sizeof(key_t));
  memmove(&leaf.Data()[1], &leaf.Data()[0], remove * sizeof(off_t));

  /* borrow the last element from left sibling */
  leaf.Key()[0] = left.Key()[left->children - 1];
  leaf.Data()[0] = left.Data()[left->children - 1];
  left->children--;

  /* update parent key */
  parent.Key()[parent_key_index] = leaf.Key()[0];
}

void BMap::LeafMergeIntoLeft(BpNodePtr &leaf, BpNodePtr &left,
                             int parent_key_index, int remove) {
  /* merge into left sibling, sum = leaf->children - 1*/
  memmove(&left.Key()[left->children], &leaf.Key()[0], remove * sizeof(key_t));
  memmove(&left.Data()[left->children], &leaf.Data()[0],
          remove * sizeof(off_t));
  memmove(&left.Key()[left->children + remove], &leaf.Key()[remove + 1],
          (leaf->children - remove - 1) * sizeof(key_t));
  memmove(&left.Data()[left->children + remove], &leaf.Data()[remove + 1],
          (leaf->children - remove - 1) * sizeof(off_t));
  left->children += leaf->children - 1;
}

void BMap::LeafShiftFromRight(BpNodePtr &leaf, BpNodePtr &right,
                              BpNodePtr &parent, int parent_key_index) {
  /* borrow the first element from right sibling */
  leaf.Key()[leaf->children] = right.Key()[0];
  leaf.Data()[leaf->children] = right.Data()[0];
  leaf->children++;

  /* left shift in right sibling */
  memmove(&right.Key()[0], &right.Key()[1],
          (right->children - 1) * sizeof(key_t));
  memmove(&right.Data()[0], &right.Data()[1],
          (right->children - 1) * sizeof(off_t));
  right->children--;

  /* update parent key */
  parent.Key()[parent_key_index] = right.Key()[0];
}

void BMap::LeafMergeFromRight(BpNodePtr &leaf, BpNodePtr &right) {
  memmove(&leaf.Key()[leaf->children], &right.Key()[0],
          right->children * sizeof(key_t));
  memmove(&leaf.Data()[leaf->children], &right.Data()[0],
          right->children * sizeof(off_t));
  leaf->children += right->children;
}

void BMap::LeafSimpleRemove(BpNodePtr &leaf, int remove) {
  memmove(&leaf.Key()[remove], &leaf.Key()[remove + 1],
          (leaf->children - remove - 1) * sizeof(key_t));
  memmove(&leaf.Data()[remove], &leaf.Data()[remove + 1],
          (leaf->children - remove - 1) * sizeof(off_t));
  leaf->children--;
}

int BMap::LeafRemove(BpNodePtr &leaf, key_t key) {
  int remove = BNodeBinarySearch(leaf, key);
  if (remove < 0) {
    /* Not exist */
    return -1;
  }

  if (leaf->parent == INVALID_OFFSET) {
    /* leaf as the root */
    if (leaf->children == 1) {
      /* delete the only last node */
      assert(key == leaf.Key()[0]);
      boot_.root_offset = INVALID_OFFSET;
      BpNodePtr null_node;
      NodeDelete(leaf, null_node, null_node);
    } else {
      LeafSimpleRemove(leaf, remove);
      NodeFlush(leaf);
    }
  } else if (leaf->children <= (max_data_num_ + 1) / 2) {
    BpNodePtr l_sib = NodeFetch(leaf->prev);
    BpNodePtr r_sib = NodeFetch(leaf->next);
    BpNodePtr parent = NodeFetch(leaf->parent);

    int i = ParentKeyIndex(parent, leaf.Key()[0]);

    /* decide which sibling to be borrowed from */
    if (SiblingSelect(l_sib, r_sib, parent, i) == LEFT_SIBLING) {
      if (l_sib->children > (max_data_num_ + 1) / 2) {
        LeafShiftFromLeft(leaf, l_sib, parent, i, remove);
        /* flush leaves */
        NodeFlush(leaf);
        NodeFlush(l_sib);
        NodeFlush(r_sib);
        NodeFlush(parent);
      } else {
        LeafMergeIntoLeft(leaf, l_sib, i, remove);
        /* delete empty leaf and flush */
        NodeDelete(leaf, l_sib, r_sib);
        /* trace upwards */
        NonLeafRemove(parent, i);
      }
    } else {
      /* remove at first in case of overflow during merging with sibling */
      LeafSimpleRemove(leaf, remove);

      if (r_sib->children > (max_data_num_ + 1) / 2) {
        LeafShiftFromRight(leaf, r_sib, parent, i + 1);
        /* flush leaves */
        NodeFlush(leaf);
        NodeFlush(l_sib);
        NodeFlush(r_sib);
        NodeFlush(parent);
      } else {
        LeafMergeFromRight(leaf, r_sib);
        /* delete empty right sibling flush */
        BpNodePtr rr_sib = NodeFetch(r_sib->next);
        NodeDelete(r_sib, leaf, rr_sib);
        NodeFlush(l_sib);
        /* trace upwards */
        NonLeafRemove(parent, i + 1);
      }
    }
  } else {
    LeafSimpleRemove(leaf, remove);
    NodeFlush(leaf);
  }

  return 0;
}

int BMap::BplusTreeDelete(key_t key) {
  BpNodePtr node = NodeSeek(boot_.root_offset);
  while (node != NULL) {
    if (IsLeaf(node)) {
      return LeafRemove(node, key);
    } else {
      int i = BNodeBinarySearch(node, key);
      if (i >= 0) {
        node = NodeSeek(node.Sub()[i + 1]);
      } else {
        i = -i - 1;
        node = NodeSeek(node.Sub()[i]);
      }
    }
  }
  return -1;
}

void BMapVisualizer::NodeKeyDraw(const BpNodePtr &node) {
  int i;
  if (bmap_.IsLeaf(node)) {
    printf("leaf:");
    for (i = 0; i < node->children; i++) {
      printf(" %d", node.Key()[i]);
    }
  } else {
    printf("node:");
    for (i = 0; i < node->children - 1; i++) {
      printf(" %d", node.Key()[i]);
    }
  }
  printf("\n");
}

void BMapVisualizer::Draw(const BpNodePtr &node, NodeBackLog *stack,
                          int level) {
  int i;
  for (i = 1; i < level; i++) {
    if (i == level - 1) {
      printf("%-8s", "+-------");
    } else {
      if (stack[i - 1].offset != INVALID_OFFSET) {
        printf("%-8s", "|");
      } else {
        printf("%-8s", " ");
      }
    }
  }
  NodeKeyDraw(node);
}

void BMapVisualizer::Visualize() {
  int level = 0;
  BpNodePtr node = bmap_.NodeSeek(bmap_.boot_.root_offset);
  // 下一个需要访问的指针,null代表继续深度,非null代表同层的
  NodeBackLog *p_nbl = nullptr;
  NodeBackLog nbl_stack[10];    // 回溯用的栈
  NodeBackLog *top = nbl_stack; // 当前栈顶指针

  for (;;) {
    if (node != nullptr) {
      int sub_idx = p_nbl != nullptr ? p_nbl->next_sub_idx : 0;
      p_nbl = nullptr;

      /* Backlog the node */
      if (bmap_.IsLeaf(node) || sub_idx + 1 >= node->children) {
        top->offset = INVALID_OFFSET;
        top->next_sub_idx = 0;
      } else {
        top->offset = node->self;
        top->next_sub_idx = sub_idx + 1;
      }
      top++;
      level++;

      /* Draw the node when first passed through */
      if (sub_idx == 0) {
        Draw(node, nbl_stack, level);
      }

      /* Move deep down */
      node = bmap_.IsLeaf(node) ? BpNodePtr()
                                : bmap_.NodeSeek(node.Sub()[sub_idx]);
    } else {
      p_nbl = top == nbl_stack ? nullptr : --top;
      if (p_nbl == nullptr) {
        /* End of traversal */
        break;
      }
      node = bmap_.NodeSeek(p_nbl->offset);
      level--;
    }
  }
}