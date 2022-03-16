#include "CollList.h"

int CollList::nb_alloc = 0;
std::set<CollList *> CollList::listsHeads;

bool CollList::isNAVS() const { return this->navs; }
bool CollList::isEmpty() const { return this->next == nullptr; }
bool CollList::isSource(const llvm::BasicBlock *src) const { return this->source == src; }

unsigned CollList::getDepth() const { return this->depth; }
const std::vector<std::string> CollList::getNames() const {
  return (const std::vector<std::string>)this->names;
}
CollList * CollList::getNext() const {
  if (this->next->isEmpty())
    return nullptr;

  return this->next;
}

void CollList::pushColl(std::string coll) {
  navs |= (coll == "NAVS");
  names.push_back(coll);
}

void CollList::pushColl(CollList * l) {
  if (!l || l->isEmpty())
    return;

  pushColl(l->getNext());

  const std::vector<std::string> lNames = l->getNames();

  auto end = lNames.end();
  auto start = lNames.begin();
  if (start != end)
    for (auto i = end - 1; i >= start; i--)
      this->pushColl(*i);
}

std::string CollList::toString() {
  if (isEmpty())
    return "X";

  std::string stack = "";
  auto end = this->names.end();
  auto start = this->names.begin();
  for (auto i = end - 1; i >= start; i--) {
    char node[32] = "";
    // snprintf(node, 32, "%d:%d:%s", nb_alloc, this->rc, this->name.c_str());
    snprintf(node, 32, "%d:%s", this->rc, i->c_str());

    std::string snode = node;

    stack = (navs?"[":"(")
      + snode
      + (navs?"]":")")
      + " -> ";
  }

  std::string out = "";
  if (next)
    out = next->toString();

  return stack + out;
}

std::string CollList::toCollMap() const {
  if (isNAVS())
    return "NAVS";

  if (isEmpty())
    return "";

  std::string out = "";
  bool first = true;
  auto end = this->names.end();
  auto start = this->names.begin();
  if (start != end)
    for (auto i = end - 1; i >= start; i--) {
      if (!first)
        out = out + " ";
      out = out + *i;
      first = false;
    }

  if (!this->next->isEmpty())
    out = out + " " + this->next->toCollMap();

  return out;
}
