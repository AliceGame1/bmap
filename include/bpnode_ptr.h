#pragma once

#include "data_format/boot.h"
#include "page_cache.h"
#include <stdint.h>
#include <string>
#include <unistd.h>
#include <utility>

class BMap;

class BpNodePtr {
public:
  friend class BMap;

  BpNodePtr(BMap *cache, PageCacheIter cache_iter);
  BpNodePtr() = default;
  ~BpNodePtr();
  BpNodePtr(const BpNodePtr &other) = delete;
  BpNodePtr &operator=(const BpNodePtr &other) = delete;
  BpNodePtr(BpNodePtr &&other);
  BpNodePtr &operator=(BpNodePtr &&other);

  BpNode *operator->();
  const BpNode *operator->() const;
  BpNode &operator*();
  const BpNode &operator*() const;

  bool operator==(std::nullptr_t) const;
  bool operator!=(std::nullptr_t) const;
  bool operator==(const BpNodePtr &other) const;
  bool operator!=(const BpNodePtr &other) const;

  const key_t *Key() const;
  key_t *Key();
  const off_t *Sub() const;
  off_t *Sub();
  const long *Data() const;
  long *Data();

private:
  BMap *bmap_ = nullptr;
  PageCacheIter cache_iter_;
  bool dirty_ = false;
};