#include "page_cache.h"
#include <assert.h>

int main() {
  PageList list;
  if (list.Init(20, 4096))
    return -1;
  for (int i = 0; i < 20; i++) {
    auto iter = list.PushFront();
    char *buff = *iter;
    buff[0] = 'a' + i;
  }
  int i = 0;
  for (auto iter = list.Begin(); iter != list.End(); iter++) {
    char *buff = *iter;
    assert(buff[0] == 'a' + 19 - i);
    i++;
  }
  for (int i = 0; i < 5; i++) {
    auto iter = list.Tail();
    char *buff = *iter;
    assert(buff[0] == 'a' + i);
    list.PopBack();
  }
  for (int i = 0; i < 5; i++) {
    auto iter = list.PushFront();
    char *buff = *iter;
    assert(buff[0] == 'a' + 4 - i);
  }
  for (int i = 0; i < 5; i++) {
    auto iter = list.Begin();
    iter = list.MoveToBack(iter);
    char *buff = *iter;
    assert(buff[0] == 'a' + i);
  }
  {
    auto iter_f = list.End();
    auto iter_b = list.End();
    for (auto iter = list.Begin(); iter != list.End(); iter++) {
      if ((*iter)[0] == 'b') {
        iter_b = iter;
      }
      if ((*iter)[0] == 'f') {
        iter_f = iter;
      }
    }
    assert(iter_f != list.End() && iter_b != list.End());
    iter_f = list.MoveBeforeIter(iter_f, iter_b);
    ++iter_f;
    assert((*iter_f)[0] == 'b');
  }
  {
    auto iter_f = list.End();
    auto iter_a = list.End();
    for (auto iter = list.Begin(); iter != list.End(); iter++) {
      if ((*iter)[0] == 'a') {
        iter_a = iter;
      }
      if ((*iter)[0] == 'f') {
        iter_f = iter;
      }
    }
    assert(iter_f != list.End() && iter_a != list.End());
    iter_f = list.MoveBeforeIter(iter_f, iter_a);
    ++iter_f;
    assert((*iter_f)[0] == 'a');
  }
  i = 0;
  for (auto iter = list.Tail(); iter != list.End() && i < 5; iter--) {
    assert((*iter)[0] == 'a' + 4 - i);
    i++;
  }
  return 0;
}
