#ifndef MSSAMUCHI_H
#define MSSAMUCHI_H

#include "Region.h"

#include "llvm/IR/Instructions.h"

#include <vector>

class MSSADef {
 public:
  enum TYPE {
    PHI,
    CALL,
    STORE,
    CHI,
    ENTRY
  };

  MSSADef(Region *region, TYPE type)
    : region(region), version(0), type(type) {}

  virtual ~MSSADef() {}
  Region *region;
  unsigned version;
  TYPE type;
};

class MSSAPhi : public MSSADef {
public:
  MSSAPhi(Region *region) : MSSADef(region, PHI) {}

  llvm::DenseMap<int, int> opsVersion;

  static inline bool classof(const MSSAPhi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAChi : public MSSADef {
public:
  MSSAChi(Region *region, TYPE type) : MSSADef(region, type) {}

  unsigned opVersion;

  static inline bool classof(const MSSAChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == ENTRY;
  }
};

class MSSAEntryChi : public MSSAChi {
public:
  MSSAEntryChi(Region *region, const llvm::Value *value)
    : MSSAChi(region, ENTRY), value(value) {}
  const llvm::Value *value;

  static inline bool classof(const MSSAEntryChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAStoreChi : public MSSAChi {
public:
  MSSAStoreChi(Region *region, const llvm::StoreInst *inst)
    : MSSAChi(region, STORE), inst(inst) {}
  const llvm::StoreInst *inst;

  static inline bool classof(const MSSAStoreChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSACallChi : public MSSAChi {
public:
  MSSACallChi(Region *region, const llvm::Function *called)
    : MSSAChi(region, CALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSACallChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAMu {
public:
  enum TYPE {
    LOAD,
    CALL,
    RET
  };

  MSSAMu(Region *region, TYPE type) : region(region), version(0), type(type) {}
  virtual ~MSSAMu() {}
  Region *region;
  unsigned version;
  TYPE type;
};

class MSSALoadMu : public MSSAMu {
 public:
  MSSALoadMu(Region *region, const llvm::LoadInst *inst)
    : MSSAMu(region, LOAD), inst(inst) {}
  const llvm::LoadInst *inst;

  static inline bool classof(const MSSALoadMu *m) {
    return true;
  }

  static inline bool classof(const MSSAMu *m) {
    return m->type == LOAD ||
      m->type == CALL ||
      m->type == RET;
  }
};

class MSSACallMu : public MSSAMu {
 public:
  MSSACallMu(Region *region, const llvm::Function *called)
    : MSSAMu(region, CALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSACallMu *m) {
    return true;
  }

  static inline bool classof(const MSSAMu *m) {
    return m->type == LOAD ||
      m->type == CALL ||
      m->type == RET;
  }
};

class MSSARetMu : public MSSAMu {
 public:
  MSSARetMu(Region *region)
    : MSSAMu(region, RET) {}

  static inline bool classof(const MSSARetMu *m) {
    return true;
  }

  static inline bool classof(const MSSAMu *m) {
    return m->type == LOAD ||
      m->type == CALL ||
      m->type == RET;
  }
};

#endif /* MSSAMUCHI */
