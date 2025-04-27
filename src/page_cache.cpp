#include "page_cache.h"
#include <assert.h>
#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

int PageList::Init(uint32_t capacity, uint32_t page_size) {
  constexpr uint32_t kPageSize = 4096;
  if (page_size % kPageSize != 0)
    return -1;
  page_size_ = page_size;
  capacity_ = capacity;

  // 分配内存需要按页对齐,同时第一个节点index是0，和初始化的link
  // index重复了,所以不用了
  page_buffer_ = (char *)aligned_alloc(kPageSize, page_size_ * (capacity_ + 1));
  if (!page_buffer_)
    return -1;
  link_info_ = new LinkInfo[capacity_ + 1]{};
  if (!page_buffer_)
    return -1;
  return 0;
}

PageList::~PageList() {
  if (page_buffer_) {
    free(page_buffer_);
  }
  if (link_info_) {
    delete[] link_info_;
  }
}

PageList::Iterator PageList::PushFront() {
  if (Full()) {
    return End();
  }
  uint32_t return_idx = free_head_;
  LinkInfo &free_node = link_info_[free_head_];
  size_++;
  // 后面没有节点了,需要扩容
  if (free_node.next_idx == kInvaildIndex) {
    free_head_ = (size_ + 1) % (capacity_ + 1);
  } else {
    free_head_ = free_node.next_idx;
  }

  free_node.next_idx = using_head_;
  free_node.prev_idx = kInvaildIndex;
  using_head_ = return_idx;
  if (using_tail_ == kInvaildIndex) {
    using_tail_ = return_idx;
  }

  if (free_node.next_idx != kInvaildIndex) {
    LinkInfo &next_node = link_info_[free_node.next_idx];
    next_node.prev_idx = using_head_;
  }
  return Iterator(*this, return_idx);
}

PageList::Iterator PageList::PushFront(std::string &data) {
  if (data.size() != page_size_ || Full()) {
    return End();
  }
  auto iter = PushFront();
  // 复制数据
  memcpy(*iter, data.data(), page_size_);
  return iter;
}

PageList::Iterator PageList::Erase(Iterator iter) {
  if (Empty())
    return End();
  LinkInfo &erase_node = link_info_[iter.idx_];
  // 如果删除的是头节点,需要更新头节点
  if (iter.idx_ == using_head_) {
    using_head_ = erase_node.next_idx;
  }
  // 如果删除的是尾节点,需要更新尾节点
  if (iter.idx_ == using_tail_) {
    using_tail_ = erase_node.prev_idx;
  }
  // 不是第一个节点
  if (erase_node.prev_idx != kInvaildIndex) {
    LinkInfo &prev_node = link_info_[erase_node.prev_idx];
    prev_node.next_idx = erase_node.next_idx;
  }
  // 不是最后一个节点
  if (erase_node.next_idx != kInvaildIndex) {
    LinkInfo &next_node = link_info_[erase_node.next_idx];
    next_node.prev_idx = erase_node.prev_idx;
  }

  // 放到空闲列表头部
  uint32_t tmp_idx = erase_node.next_idx;
  erase_node.prev_idx = kInvaildIndex;
  erase_node.next_idx = free_head_;

  free_head_ = iter.idx_;
  size_--;

  return Iterator(*this, tmp_idx);
}

std::pair<PageList::Iterator, bool> PageList::Insert(Iterator iter) {
  if (Full())
    return std::make_pair(End(), false);
  if (iter.idx_ == using_head_) // 放在第一个前面
  {
    PushFront();
    return std::make_pair(Iterator(*this, using_head_), true);
  }
  LinkInfo &node = link_info_[iter.idx_];
  LinkInfo &prev_node = link_info_[node.prev_idx];
  uint32_t new_node_idx = free_head_;

  LinkInfo &free_node = link_info_[free_head_];
  size_++;
  // 需要扩容了
  if (free_node.next_idx == kInvaildIndex) {
    free_head_ = (size_ + 1) % (capacity_ + 1);
  } else {
    free_head_ = free_node.next_idx;
  }
  free_node.next_idx = iter.idx_;
  free_node.prev_idx = node.prev_idx;

  node.prev_idx = new_node_idx;
  prev_node.next_idx = new_node_idx;

  return std::make_pair(Iterator(*this, new_node_idx), true);
}

std::pair<PageList::Iterator, bool> PageList::Insert(Iterator iter,
                                                     std::string &data) {
  if (Full() || data.size() != page_size_)
    return std::make_pair(End(), false);
  auto new_iter = Insert(iter).first;
  memcpy(*new_iter, data.data(), page_size_);
  return std::make_pair(new_iter, true);
}

void PageList::PopBack() { Erase(Iterator(*this, using_tail_)); }

PageList::Iterator PageList::MoveToHead(Iterator iter) {
  Erase(iter);
  return PushFront();
}

PageList::Iterator PageList::MoveBeforeIter(Iterator iter, Iterator dest_iter) {
  Erase(iter);
  return Insert(dest_iter).first;
}

PageList::Iterator PageList::MoveToBack(Iterator iter) {
  Erase(iter);
  return PushBack();
}

PageList::Iterator PageList::PushBack(std::string &data) {
  if (Full() || data.size() != page_size_)
    return End();
  auto iter = PushBack();
  memcpy(*iter, data.data(), page_size_);
  return iter;
}

PageList::Iterator PageList::PushBack() {
  if (Full())
    return End();
  uint32_t new_node_idx = free_head_;
  LinkInfo &new_node = link_info_[new_node_idx];
  if (new_node.next_idx != kInvaildIndex) {
    free_head_ = new_node.next_idx;
  } else {
    free_head_ = (size_ + 1) % (capacity_ + 1);
  }
  new_node.next_idx = kInvaildIndex;
  new_node.prev_idx = using_tail_;
  if (Empty()) {
    using_head_ = new_node_idx;
  } else {
    LinkInfo &tail_node = link_info_[using_tail_];
    tail_node.next_idx = new_node_idx;
  }
  using_tail_ = new_node_idx;
  size_++;
  return Iterator(*this, new_node_idx);
}

PageLruCache::~PageLruCache() {
  if (fd_ >= 0) {
    close(fd_);
  }
}

int PageLruCache::CheckAlignMem(uint32_t page_size) const {
  long page_size_sys = sysconf(_SC_PAGESIZE);
  if (page_size % page_size_sys != 0) {
    return -1;
  }
  return 0;
}

int PageLruCache::CheckAlignFile(uint32_t page_size) const {
  struct stat st;
  if (fstat(fd_, &st) < 0) {
    return -1;
  }

  long blk_size = st.st_blksize;
  if (page_size % blk_size != 0) {
    return -1;
  }
  return 0;
}

int PageLruCache::Init(const std::string &file_name, uint32_t page_size,
                       uint32_t capacity) {
  fd_ = open(file_name.c_str(), O_RDWR | O_CREAT | O_DIRECT, 0644);
  if (fd_ < 0 || CheckAlignMem(page_size) || CheckAlignFile(page_size)) {
    return -1;
  }

  if (page_list_.Init(capacity, page_size) != 0) {
    return -1;
  }

  page_info_.reserve(capacity);
  return 0;
}

PageCacheIter PageLruCache::GetPage(off_t offset, bool is_new) {
  auto iter = page_info_.find(offset);
  if (iter != page_info_.end()) {
    assert(is_new == false);
    // 如果找到了,放在最前面
    PageInfo &page_info = iter->second;
    page_info.iter = page_list_.MoveToHead(page_info.iter);
    page_info.in_use_count++;
    // 如果刚好是空闲头节点,需要改变一下
    if (unused_head_ == page_info.iter) {
      unused_head_ = page_info.iter++;
    }
    return iter;
  } else {
    // 满了且没有空余
    if (page_list_.Full() && unused_head_ == page_list_.End()) {
      return page_info_.end();
    }
    if (unused_head_ != page_list_.End()) {
      // 有空余淘汰一个，如果剩最后一个空余，把空余头节点改一下
      if (page_list_.Tail() == unused_head_) {
        unused_head_ = page_list_.End();
      }
      page_list_.PopBack();
    }
    PageList::Iterator iter = page_list_.PushFront();
    if (!is_new) {
      uint32_t page_size = page_list_.GetPageSize();
      // 如果没找到,从磁盘中读取
      ssize_t size = pread(fd_, *iter, page_size, offset);
      if (size != page_size) {
        return page_info_.end();
      }
    }
    auto &&[new_iter, insert] =
        page_info_.emplace(offset, PageInfo{offset, iter, 1, is_new});
    std::ignore = insert;
    return new_iter;
  }
}

int PageLruCache::UnusePage(off_t offset) {
  auto iter = page_info_.find(offset);
  if (iter == page_info_.end()) {
    return 0;
  }
  PageInfo &page_info = iter->second;
  if (page_info.in_use_count == 0) {
    return 0;
  }
  page_info.in_use_count--;
  if (page_info.in_use_count == 0 && !page_info.dirty) {
    if (unused_head_ == page_list_.End()) {
      unused_head_ = page_list_.MoveToBack(page_info.iter);
    } else {
      unused_head_ = page_list_.MoveBeforeIter(page_info.iter, unused_head_);
    }
  }
  return page_info.in_use_count;
}

int PageLruCache::SyncPage(off_t page_offset) {
  auto iter = page_info_.find(page_offset);
  if (iter == page_info_.end()) {
    return 0;
  }
  PageInfo &page_info = iter->second;
  if (page_info.in_use_count == 0 || page_info.dirty == false) {
    return -1;
  }
  ssize_t size =
      pwrite(fd_, *page_info.iter, page_list_.GetPageSize(), page_offset);
  if (size != page_list_.GetPageSize()) {
    return -1;
  }
  if (fsync(fd_) == -1) {
    return -1;
  }
  page_info.dirty = false;
  return 0;
}