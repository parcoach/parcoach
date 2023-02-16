#include "CollList.h"

#include "llvm/Support/raw_ostream.h"

using namespace llvm;

CollList::CollList(StringRef Coll, BasicBlock const *Src)
    : Navs{Coll == "NAVS"}, Sources{Src}, Names{Coll.str()} {}

CollList::CollList(CollList const *Coll, BasicBlock const *Src)
    : Navs{false}, Sources{Src} {
  if (Coll != nullptr) {
    append_range(Names, Coll->getNames());
  }
}

void CollList::push(StringRef Collective, BasicBlock const *Source,
                    bool ForcePush) {
  if (!isNAVS() || ForcePush) {
    if (!isSource(Source)) {
      Sources.push_back(Source);
    }
    Names.push_back(Collective.str());
    Navs |= Collective == "NAVS";
  }
}

void CollList::push(CollList const *CL, BasicBlock const *Source,
                    bool ForcePush) {
  if (!isNAVS() || ForcePush) {
    if (!isSource(Source)) {
      Sources.push_back(Source);
    }
    if (CL != nullptr) {
      append_range(Names, CL->getNames());
      Navs |= CL->isNAVS();
    }
  }
}

#ifndef NDEBUG
std::string CollList::toString() const {
  std::string Out;
  raw_string_ostream OS(Out);
  OS << "{" << this << ", d: " << getDepth()
     << ", navs: " << static_cast<int>(Navs) << ", names:" << toCollMap()
     << "}";
  return Out;
}
#endif

std::string CollList::toCollMap() const {
  std::string Out;
  raw_string_ostream OS(Out);
  bool First = true;
  for (auto const &Name : Names) {
    if (!First) {
      OS << " ";
    }
    OS << Name;
    First = false;
  }
  return Out;
}
