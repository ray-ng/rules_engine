#pragma once

#include <stdio.h>
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <map>
#include <memory>
#include <queue>
#include <set>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

#include "slice.h"

namespace regex {

typedef std::string UChar;
typedef std::string ACSMPattern;

struct RegTreeNode {
  RegTreeNode *parent;
  Slice oprt;
  Slice dire;
  RegTreeNode() : parent(nullptr), oprt(), dire() {}
  RegTreeNode(RegTreeNode *p, const Slice &o, const Slice &d) : parent(p), oprt(o), dire(d) {}
};

struct pattern_node {
  RegTreeNode *node;
  int type;
  pattern_node(RegTreeNode *n, int t) : node(n), type(t) {}
  pattern_node(const pattern_node &p) = default;
};

class ACSMStateNode {
 public:
  ACSMStateNode() {
    depth = 0;
    fail = nullptr;
    output.clear();
  }

  ~ACSMStateNode() {
    output.clear();
  }
  int depth;  // state depth
  std::vector<RegTreeNode *> output;
  ACSMStateNode *fail;  // state transfer when fails
  std::map<Slice, ACSMStateNode *> next;
};

class ACSM {
 public:
  ~ACSM();
  void Init();
  void AddPattern(const std::string &pattern);
  void Compile();
  bool Match(const std::string &dst, std::vector<std::string> &patterns) const;    // NOLINT
  bool UTF8Decode(const char *str, size_t len, std::vector<Slice> &uchars) const;  // NOLINT
 private:
  bool BuildRegTree(const std::set<ACSMPattern>::iterator set_it, const std::vector<Slice> &uchars,
                    std::vector<RegTreeNode *> &node_index,
                    std::vector<std::pair<std::pair<int, int>, RegTreeNode *>> &pattern_index);
  bool RegCheck(const RegTreeNode *node, std::unordered_map<const RegTreeNode *, int> &reg_match) const;
  std::set<ACSMPattern> m_patterns;
  std::vector<ACSMStateNode *> m_states;
  std::vector<RegTreeNode *> reg_trees;
};

}  // namespace regex
