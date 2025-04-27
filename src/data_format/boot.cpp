#include "data_format/boot.h"

off_t INVALID_OFFSET = 0xdeadbeef;
constexpr uint32_t ADDR_LEN_HEX = 16;

off_t ReadOffset(int fd) {
  char buf[ADDR_LEN_HEX];
  ssize_t len = read(fd, buf, ADDR_LEN_HEX);
  if (len > 0)
    return std::stoull(buf, nullptr, 16);
  else
    return INVALID_OFFSET;
}

off_t WriteOffset(int fd, uint64_t offset) {
  char buf[ADDR_LEN_HEX];
  snprintf(buf, ADDR_LEN_HEX, "%lx", offset);
  return write(fd, buf, ADDR_LEN_HEX);
}

int Boot::ParseFromFile(int fd) {
  if (lseek(fd, 0, SEEK_SET) == -1) {
    return -1;
  }
  root_offset = ReadOffset(fd);
  file_size = ReadOffset(fd);
  block_size = ReadOffset(fd);
  off_t offset = 0;
  while ((offset = ReadOffset(fd)) != INVALID_OFFSET) {
    free_blocks.push_back(offset);
  }
  return 0;
}

int Boot::WriteToFile(int fd) {
  if (lseek(fd, 0, SEEK_SET) == -1) {
    return -1;
  }
  WriteOffset(fd, root_offset);
  WriteOffset(fd, file_size);
  WriteOffset(fd, block_size);
  for (auto offset : free_blocks) {
    WriteOffset(fd, offset);
  }
  return 0;
}
