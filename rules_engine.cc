#include "rules_engine.h"

#include <stdio.h>
#include <sys/time.h>
#include <fstream>
#include <iostream>
#include <map>
#include <queue>
#include <set>
#include <stack>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace regex {

static inline bool starts_with(const std::string &value, const std::string &start) {
  if (start.length() > value.length()) return false;
  return std::equal(start.begin(), start.end(), value.begin());
}

static inline bool ends_with(const std::string &value, const std::string &ending) {
  if (ending.length() > value.length()) return false;
  return std::equal(ending.rbegin(), ending.rend(), value.rbegin());
}

bool ACSM::UTF8Decode(const char *str, size_t len, std::vector<Slice> &uchars) const {
  char c;
  int code_len;

  uchars.clear();
  for (int i = 0; i < len;) {
    if (!str[i]) {
      break;
    }

    if ((str[i] & 0x80) == 0) {
      // ASCII char
      uchars.push_back(Slice(str + i, 1));
      i++;
      continue;
    }

    c = str[i];
    for (code_len = 1; code_len <= 6; code_len++) {
      c <<= 1;
      if (c & 0x80) {
        if ((str[i + code_len] & 0xC0) != 0x80) {
          return false;
        }
      } else {
        break;
      }
    }

    if (code_len == 1) {
      // illegal
      return false;
    }
    uchars.push_back(Slice(str + i, code_len));
    i += code_len;
  }
  return true;
}

ACSM::~ACSM() {
  m_patterns.clear();
  std::vector<ACSMStateNode *>::iterator acsm_it;
  for (acsm_it = m_states.begin(); acsm_it != m_states.end(); acsm_it++) {
    if (*acsm_it) {
      delete *acsm_it;
    }
  }
  std::vector<RegTreeNode *>::iterator reg_it;
  for (reg_it = reg_trees.begin(); reg_it != reg_trees.end(); reg_it++) {
    if (*reg_it) {
      delete *reg_it;
    }
  }
}

void ACSM::Init() {
  ACSMStateNode *s = new ACSMStateNode;
  s->fail = nullptr;
  m_states.push_back(s);
}

static std::set<char> reg_operator = {'&', '|', '!'};
static std::set<char> pri_operator = {'(', ')'};

static const Slice AND = Slice("&");
static const Slice OR = Slice("|");
static const Slice NOT = Slice("!");
static const Slice LP = Slice("(");
static const Slice RP = Slice(")");
static const Slice LC = Slice("l");
static const Slice RC = Slice("r");

static inline bool is_reg_oprt(const Slice &s) {
  return s == AND || s == OR || s == NOT;
}
static inline bool is_pri_oprt(const Slice &s) {
  return s == LP || s == RP;
}

bool ACSM::BuildRegTree(const std::set<ACSMPattern>::iterator set_it, const std::vector<Slice> &uchars,
                        std::vector<RegTreeNode *> &node_index,
                        std::vector<std::pair<std::pair<int, int>, RegTreeNode *>> &pattern_index) {
  RegTreeNode _LP = {nullptr, LP, LC};
  std::stack<RegTreeNode *> reg_stk;
  int i = 0, j = 1;
  while (i < uchars.size()) {
    if (is_reg_oprt(uchars[i])) {
      if (reg_stk.empty() || reg_stk.top()->oprt == LP) {
        return false;
      }
      auto tmp = new RegTreeNode();
      node_index.push_back(tmp);
      tmp->oprt = uchars[i];
      reg_stk.top()->parent = tmp;
      reg_stk.top()->dire = LC;
      reg_stk.pop();
      reg_stk.push(tmp);
      ++i;
      ++j;
    } else if (uchars[i] == LP) {
      reg_stk.push(&_LP);
      ++i;
      ++j;
    } else if (uchars[i] == RP) {
      RegTreeNode *tmp = nullptr;
      int cnt = 0;
      while (!reg_stk.empty() && reg_stk.top()->oprt != LP) {
        ++cnt;
        tmp = reg_stk.top();
        reg_stk.pop();
      }
      if (reg_stk.empty() || cnt > 1) {
        return false;
      }
      reg_stk.pop();
      if (tmp != nullptr) {
        if (reg_stk.empty() || reg_stk.top()->oprt == LP) {
          reg_stk.push(tmp);
        } else if (is_reg_oprt(reg_stk.top()->oprt)) {
          tmp->parent = reg_stk.top();
          tmp->dire = RC;
        } else {
          return false;
        }
      }
      ++i;
      ++j;
    } else {
      if (j >= uchars.size() || is_reg_oprt(uchars[j]) || is_pri_oprt(uchars[j])) {
        auto tmp = new RegTreeNode();
        node_index.push_back(tmp);
        if (!reg_stk.empty() && reg_stk.top()->oprt != LP) {
          if (reg_stk.top()->oprt.empty()) {
            return false;
          }
          tmp->parent = reg_stk.top();
          tmp->dire = RC;
        } else {
          reg_stk.push(tmp);
        }
        pattern_index.emplace_back(std::pair<int, int>{i, j}, tmp);
        i = j++;
      } else {
        ++j;
      }
    }
  }
  if (reg_stk.size() == 1 && reg_stk.top()->oprt != LP) {
    reg_stk.top()->dire = Slice(*set_it);
    return true;
  }
  return false;
}

void ACSM::AddPattern(const std::string &pattern) {
  auto s = m_states[0];
  std::pair<std::set<ACSMPattern>::iterator, bool> ret = m_patterns.insert(pattern);
  std::vector<Slice> uchars;
  if (!UTF8Decode(ret.first->data(), ret.first->size(), uchars)) {
    return;
  }
  std::vector<RegTreeNode *> node_index;
  std::vector<std::pair<std::pair<int, int>, RegTreeNode *>> pattern_index;
  if (!BuildRegTree(ret.first, uchars, node_index, pattern_index)) {
    for (auto node : node_index) {
      if (node) {
        delete node;
      }
    }
    return;
  }
  for (auto node : node_index) {
    reg_trees.push_back(node);
  }

  for (int idx = 0; idx < pattern_index.size(); ++idx) {
    auto s = m_states[0];
    const auto &node_idx = pattern_index[idx].first;
    for (int i = node_idx.first; i < uchars.size() && i < node_idx.second; ++i) {
      // const std::string uchar = uchars[i].ToString();
      std::map<Slice, ACSMStateNode *>::iterator it = s->next.find(uchars[i]);
      if (it != s->next.end()) {
        s = it->second;
      } else {
        ACSMStateNode *new_state = new ACSMStateNode;
        new_state->depth = s->depth + 1;
        m_states.push_back(new_state);
        s->next[uchars[i]] = new_state;
        s = new_state;
      }
    }
    if (s != m_states[0]) {
      s->output.push_back(pattern_index[idx].second);
    }
  }
}

void ACSM::Compile() {
  std::queue<ACSMStateNode *> q;
  auto s = m_states[0];
  q.push(s);

  // build fail table and output table
  while (!q.empty()) {
    s = q.front();
    q.pop();

    // std::map<char, ACSMStateNode *>::iterator it;
    for (auto it = s->next.begin(); it != s->next.end(); it++) {
      if (s != m_states[0]) {
        auto p = s->fail;
        // ACSMStateNode *p = it->second->fail;
        while (p != nullptr) {
          // std::map<char, ACSMStateNode *>::iterator _it;  // NOLINT
          auto _it = p->next.find(it->first);                   // NOLINT
          if (_it != p->next.end()) {                           // NOLINT
            it->second->fail = _it->second;                     // NOLINT
            for (auto output_it = _it->second->output.begin();  // NOLINT
                 output_it != _it->second->output.end();        // NOLINT
                 output_it++) {
              it->second->output.push_back(*output_it);
            }
            break;
          }
          p = p->fail;
        }
        if (p == nullptr) {
          it->second->fail = m_states[0];
        }
      } else {
        // s->fail = m_states[0];
        it->second->fail = m_states[0];
      }
      q.push(it->second);
    }
  }
}

#define SET_BIT()                      \
  pre = reg_match[node];               \
  if (is_right_child) {                \
    if (set_bit) {                     \
      now = (reg_match[node] |= 1);    \
    } else {                           \
      now = (reg_match[node] &= (~1)); \
    }                                  \
  } else {                             \
    if (set_bit) {                     \
      now = (reg_match[node] |= 2);    \
    } else {                           \
      now = (reg_match[node] &= (~2)); \
    }                                  \
  }                                    \
  is_right_child = (node->dire == RC); \
  node = node->parent;

bool ACSM::RegCheck(const RegTreeNode *node, std::unordered_map<const RegTreeNode *, int> &reg_match) const {
  if (node == nullptr || !node->oprt.empty()) {
    return false;
  }
  if (reg_match[node]++ > 0) {
    return true;
  }
  if (node->parent == nullptr) {
    return true;
  }

  bool is_right_child = (node->dire == RC);
  bool set_bit = true;
  node = node->parent;
  int pre = 0, now = 0;
  while (node) {
    if (node->oprt == AND) {
      SET_BIT();
      if ((now & 2) >> 1 && (now & 1)) {
        set_bit = true;
      } else if ((pre & 2) >> 1 && (pre & 1)) {
        set_bit = false;
      } else {
        break;
      }
    } else if (node->oprt == OR) {
      SET_BIT();
      if (!((pre & 2) >> 1 || (pre & 1))) {
        if ((now & 2) >> 1 || (now & 1)) {
          set_bit = true;
        } else {
          break;
        }
      } else if (!((pre & 2) >> 1 && (pre & 1))) {
        if (!((now & 2) >> 1 || (now & 1))) {
          set_bit = false;
        } else {
          break;
        }
      } else {
        break;
      }
    } else if (node->oprt == NOT) {
      SET_BIT();
      if ((pre & 2) >> 1 && !(pre & 1)) {
        if ((now & 2) >> 1 && !(now & 1)) {
          break;
        } else {
          set_bit = false;
        }
      } else {
        if ((now & 2) >> 1 && !(now & 1)) {
          set_bit = true;
        } else {
          break;
        }
      }
    } else {
      return false;
    }
  }
  return true;
}

bool ACSM::Match(const std::string &dst, std::vector<std::string> &patterns) const {
  auto s = m_states[0];
  patterns.clear();
  std::vector<Slice> uchars;
  if (!UTF8Decode(dst.c_str(), dst.length(), uchars)) {
    return false;
  }
  std::unordered_map<const RegTreeNode *, int> reg_match;
  for (int i = 0; i < uchars.size(); i++) {
    std::map<Slice, ACSMStateNode *>::iterator it = s->next.find(uchars[i]);
    if (it != s->next.end()) {
      s = it->second;
    } else if (s->fail) {
      std::map<Slice, ACSMStateNode *>::iterator it2;
      while (s->fail) {
        s = s->fail;
        it2 = s->next.find(uchars[i]);
        if (it2 != s->next.end()) {
          s = it2->second;
          break;
        }
      }
    } else {
      s = m_states[0];
    }

    // reach output node and a pattern is matched
    if (s->output.size()) {
      for (auto output_it = s->output.begin(); output_it != s->output.end(); output_it++) {
        if (!RegCheck(*output_it, reg_match)) {
          // patterns.push_back(*output_it->iter);
          return false;
        }
      }
      // return true;
    }
  }
  for (auto it = reg_match.begin(); it != reg_match.end(); ++it) {
    if (it->first->parent == nullptr) {
      if (it->first->oprt == AND && it->second == 3) {
        patterns.push_back(it->first->dire.ToString());
      } else if (it->first->oprt == OR && it->second > 0) {
        patterns.push_back(it->first->dire.ToString());
      } else if (it->first->oprt == NOT && it->second == 2) {
        patterns.push_back(it->first->dire.ToString());
      } else if (it->first->oprt == Slice() && it->second > 0) {
        patterns.push_back(it->first->dire.ToString());
      }
    }
  }
  return patterns.size() > 0;
}

}  // namespace regex
