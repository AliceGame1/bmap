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

BpNodePtr::BpNodePtr(BMap *bmap, PageCacheIter cache_iter)
    : bmap_(bmap), cache_iter_(cache_iter) {
  if (bmap_) {
    cache_iter_->second.in_use_count++;
  }
}

BpNodePtr::~BpNodePtr() {
  if (bmap_ && cache_iter_->second.in_use_count > 0) {
    cache_iter_->second.in_use_count--;
    if (dirty_) {
      cache_iter_->second.dirty = true;
    }
  }
}

BpNodePtr::BpNodePtr(BpNodePtr &&other) {
  bmap_ = other.bmap_;
  cache_iter_ = other.cache_iter_;
  other.bmap_ = nullptr;
}

BpNodePtr &BpNodePtr::operator=(BpNodePtr &&other) {
  bmap_ = other.bmap_;
  cache_iter_ = other.cache_iter_;
  other.bmap_ = nullptr;
  return *this;
}

BpNode *BpNodePtr::operator->() {
  dirty_ = true;
  return const_cast<BpNode *>(std::as_const(*this).operator->());
}

const BpNode *BpNodePtr::operator->() const {
  PageInfo &info = cache_iter_->second;
  return (BpNode *)*info.iter;
}

BpNode &BpNodePtr::operator*() {
  dirty_ = true;
  return const_cast<BpNode &>(std::as_const(*this).operator*());
}

const BpNode &BpNodePtr::operator*() const {
  PageInfo &info = cache_iter_->second;
  return *(BpNode *)*info.iter;
}

bool BpNodePtr::operator==(std::nullptr_t) const { return bmap_ == nullptr; }
bool BpNodePtr::operator!=(std::nullptr_t) const { return !(*this == nullptr); }

bool BpNodePtr::operator==(const BpNodePtr &other) const {
  if (bmap_ != other.bmap_)
    return false;
  if (bmap_ == nullptr)
    return true; // both are null
  return cache_iter_ == other.cache_iter_;
}

bool BpNodePtr::operator!=(const BpNodePtr &other) const {
  return !(*this == other);
}

const key_t *BpNodePtr::Key() const {
  PageInfo &info = cache_iter_->second;
  BpNode *node = (BpNode *)*info.iter;
  return (key_t *)(offset_ptr(node));
};

key_t *BpNodePtr::Key() {
  dirty_ = true;
  return const_cast<key_t *>(std::as_const(*this).Key());
};

const off_t *BpNodePtr::Sub() const {
  PageInfo &info = cache_iter_->second;
  BpNode *node = (BpNode *)*info.iter;
  return (off_t *)(offset_ptr(node) +
                   (bmap_->GetMaxIndexNum() - 1) * sizeof(key_t));
}

off_t *BpNodePtr::Sub() {
  dirty_ = true;
  return const_cast<off_t *>(std::as_const(*this).Sub());
}

const long *BpNodePtr::Data() const {
  PageInfo &info = cache_iter_->second;
  BpNode *node = (BpNode *)*info.iter;
  return (long *)(offset_ptr(node) + bmap_->GetMaxDataNum() * sizeof(key_t));
}

long *BpNodePtr::Data() {
  dirty_ = true;
  return const_cast<long *>(std::as_const(*this).Data());
}