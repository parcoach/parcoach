#ifndef REGION_H
#define REGION_H

#include "llvm/IR/Value.h"

class Region {
 public:
  enum regionType {
    STACK,
    HEAP,
    PARAM
  };

  Region(regionType type, const llvm::Value *value);
  ~Region();

  std::string getName() const;
  regionType getType() const;
  const llvm::Value *getValue() const;
  void print() const;

 private:
  regionType type;
  const llvm::Value *value;
  std::string name;
};

#endif /* REGION_H */
