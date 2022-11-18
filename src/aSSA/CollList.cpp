#include "CollList.h"

#include "llvm/Support/raw_ostream.h"

using namespace llvm;

CollList::CollList(StringRef coll, BasicBlock const *src)
    : navs{coll == "NAVS"}, Sources{src}, names{coll.str()} {}

CollList::CollList(CollList const *coll, BasicBlock const *src)
    : navs{false}, Sources{src} {
  if (coll) {
    append_range(names, coll->getNames());
  }
}

void CollList::push(StringRef Collective, BasicBlock const *Source,
                    bool ForcePush) {
  if (!isNAVS() || ForcePush) {
    if (!isSource(Source)) {
      Sources.push_back(Source);
    }
    names.push_back(Collective.str());
    navs |= Collective == "NAVS";
  }
}

void CollList::push(CollList const *CL, BasicBlock const *Source,
                    bool ForcePush) {
  if (!isNAVS() || ForcePush) {
    if (!isSource(Source)) {
      Sources.push_back(Source);
    }
    if (CL) {
      append_range(names, CL->getNames());
      navs |= CL->isNAVS();
    }
  }
}

#ifndef NDEBUG
std::string CollList::toString() const {
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "{" << this << ", d: " << getDepth() << ", navs: " << navs
     << ", names:" << toCollMap() << "}";
  return Out;
}
#endif

std::string CollList::toCollMap() const {
  std::string Out;
  raw_string_ostream OS(Out);
  bool First = true;
  for (auto &Name : names) {
    if (!First) {
      OS << " ";
    }
    OS << Name;
    First = false;
  }
  return Out;
}
