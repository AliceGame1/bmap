#pragma once
#include <list>
#include <stdint.h>
#include <string>
#include <unistd.h>

extern off_t INVALID_OFFSET;

extern off_t ReadOffset(int fd);
extern off_t WriteOffset(int fd, uint64_t offset);

struct Boot {
  uint64_t root_offset = INVALID_OFFSET;
  uint64_t file_size = 0;
  uint64_t block_size = 0;
  std::list<uint64_t> free_blocks;

  int ParseFromFile(int fd);
  int WriteToFile(int fd);
};

enum NodeType { LEAF = 0, NON_LEAF = 1 };

struct BpNode {
  off_t self;        // 在文件中的偏移
  off_t parent;      // 父节点在文件中的偏移
  off_t prev;        // 前一个节点在文件中的偏移
  off_t next;        // 后一个节点在文件中的偏移
  NodeType type;     // 节点类型
  uint32_t children; // 节点中包含的子节点数量
};