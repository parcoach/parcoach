#include "Region.h"
#include "Utils.h"

#include "llvm/Support/raw_ostream.h"

using namespace llvm;

Region::Region(regionType type, const llvm::Value *value)
  : type(type), value(value) {
  name = getValueLabel(value);
}

Region::~Region() {}

std::string
Region::getName() const {
  return name;
}

Region::regionType Region::getType() const {
  return type;
}

const llvm::Value *
Region::getValue() const {
  return value;
}

void
Region::print() const {
  errs() << "Region " << name << "{\n"
	 << "value=" << *value << "}\n";
}
