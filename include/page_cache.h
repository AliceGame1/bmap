#pragma once

#include <stdint.h>
#include <string>
#include <unistd.h>
#include <unordered_map>

class PageList {
public:
  struct Iterator {
    Iterator() = default;
    Iterator(PageList &page_list, uint32_t idx)
        : idx_(idx), page_list_(&page_list){};

    Iterator(const Iterator &other) = default;
    Iterator &operator=(const Iterator &other) {
      if (this == &other) {
        return *this;
      }
      idx_ = other.idx_;
      page_list_ = other.page_list_;
      return *this;
    };

    bool operator!=(const Iterator &iter) const { return idx_ != iter.idx_; }
    bool operator==(const Iterator &iter) const { return idx_ == iter.idx_; }
    Iterator &operator++() {
      idx_ = page_list_->NextIdx(idx_);
      return *this;
    }
    Iterator &operator++(int) {
      idx_ = page_list_->NextIdx(idx_);
      return *this;
    }
    void operator--() { idx_ = page_list_->PrevIdx(idx_); }
    void operator--(int) { idx_ = page_list_->PrevIdx(idx_); }
    char *operator*() const { return page_list_->GetPage(idx_); }

    uint32_t idx_ = 0;
    PageList *page_list_ = nullptr;
  };

public:
  PageList() = default;
  PageList(const PageList &) = delete;
  int Init(uint32_t capacity, uint32_t page_size);
  ~PageList();
  Iterator PushFront(std::string &data);
  Iterator PushFront();
  Iterator PushBack(std::string &data);
  Iterator PushBack();

  bool Full() const { return size_ >= capacity_; };
  bool Empty() const { return size_ == 0; }
  Iterator Erase(Iterator iter);
  Iterator End() { return Iterator(*this, 0); }
  uint32_t GetPageSize() const { return page_size_; }
  std::pair<Iterator, bool> Insert(Iterator iter,
                                   std::string &data); // 插入到iter的前面
  std::pair<Iterator, bool> Insert(Iterator iter);     // 插入到iter的前面
  void PopBack();
  Iterator Begin() { return Iterator(*this, using_head_); }
  Iterator Tail() { return Iterator(*this, using_tail_); }
  Iterator MoveToHead(Iterator iter);
  Iterator MoveBeforeIter(Iterator iter, Iterator dest_iter);
  Iterator MoveToBack(Iterator iter);

private:
  char *GetPage(uint32_t idx) { return page_buffer_ + idx * page_size_; }
  uint32_t NextIdx(uint32_t idx) const { return link_info_[idx].next_idx; }
  uint32_t PrevIdx(uint32_t idx) const { return link_info_[idx].prev_idx; }

  static constexpr uint32_t kInvaildIndex = 0;
  struct LinkInfo {
    uint32_t prev_idx = 0;
    uint32_t next_idx = 0;
  };

  uint32_t capacity_ = 0;       // 最大容量
  char *page_buffer_ = nullptr; // 数据数组，地址必须是已经对齐的
  LinkInfo *link_info_ = nullptr;       // 数据的link关系
  uint32_t using_head_ = kInvaildIndex; // 正在使用的头部idx
  uint32_t free_head_ = 1;              // 空闲的头部idx
  uint32_t size_ = 0;                   // 已经使用的size
  uint32_t page_size_ = 0;              // 一个page的大小
  uint32_t using_tail_ = kInvaildIndex; // 正在使用的尾部idx
};

using PageIter = PageList::Iterator;

struct PageInfo {
  off_t page_offset = 0;
  PageList::Iterator iter;
  uint32_t in_use_count = 0;
  bool dirty = false;
};

using PageCacheIter = std::unordered_map<off_t, PageInfo>::iterator;

class PageLruCache {
public:
public:
  PageLruCache() : unused_head_(page_list_.End()){};
  ~PageLruCache();
  int Init(const std::string &file_name, uint32_t page_size, uint32_t capacity);
  PageCacheIter GetPage(off_t page_offset, bool is_new);
  int UnusePage(off_t page_offset);
  int SyncPage(off_t page_offset);
  uint32_t GetPageSize() const { return page_list_.GetPageSize(); }
  int Fd() const { return fd_; }

private:
  int CheckAlignMem(uint32_t page_size) const;
  int CheckAlignFile(uint32_t page_size) const;

private:
  int fd_ = -1;
  std::unordered_map<off_t, PageInfo> page_info_;
  PageList page_list_;
  PageIter unused_head_;
};
