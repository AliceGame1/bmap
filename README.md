# BMap  --Fast BPlusTree DB

## 概述
这是一个基于Direct I/O的高性能B+树数据库实现，内置LRU页面缓存机制，适用于需要高效磁盘存储和检索的场景。

## 核心特性

1. **B+树索引**
   - 支持高效的插入、查找和删除操作
   - 自动平衡树结构
   - 可视化调试接口

2. **LRU页面缓存**
   - 可配置的缓存大小
   - 高效的页面替换策略
   - 支持缓存命中统计

3. **Direct I/O支持**
   - 绕过系统缓存直接读写磁盘
   - 减少内存拷贝开销
   - 适合高吞吐场景

## 快速开始

### 编译安装
```bash
mkdir build && cd build
cmake ..
make
```

### 基本使用示例
```cpp
#include "bmap.h"

int main() {
    // 初始化配置
    BConfig conf{4096, "mydb.db", 2000}; // 页大小, 文件名, 缓存页数
    
    // 创建数据库实例
    BMap db(conf);
    db.BOpen();
    
    // 插入数据
    db.BplusTreeInsert(123, "value");
    
    // 查询数据
    auto [value, found] = db.BplusTreeSearch(123);
    
    // 删除数据
    db.BplusTreeDelete(123);
    
    // 关闭数据库
    db.BClose();
    return 0;
}
```

## 性能测试

测试代码见 `test/bmap_test.cpp`，支持：
- 批量插入/查询测试
- 删除操作验证
- 可视化树结构检查

运行测试：
```bash
./build/test/bmap_test
```

## 缓存配置

通过 `PageList` 类管理LRU缓存，关键参数：
- 最大缓存页数
- 页面大小（需与B+树页大小匹配）

测试示例见 `test/page_cache_test.cpp`

## 可视化调试

```cpp
BMapVisualizer visualizer(db);
visualizer.Visualize();  // 打印树形结构
```

输出示例：
```
node: 50
+-------node: 30 40
|       +-------leaf: 10 20 30
|       +-------leaf: 40
+-------leaf: 50 60
```

## 注意事项

1. 文件系统需要支持Direct I/O
2. 页大小配置需与磁盘扇区对齐
3. 首次使用前需初始化数据文件

## 贡献指南

欢迎提交PR，请确保：
- 通过所有单元测试
- 保持代码风格一致
- 更新相关文档
