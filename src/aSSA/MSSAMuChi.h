#ifndef MSSAMUCHI_H
#define MSSAMUCHI_H

#include "MemoryRegion.h"

#include "llvm/IR/Instructions.h"

#include <vector>
#include <set>

class MSSADef;

class MSSAVar {
public:
  MSSAVar(MSSADef *def, unsigned version, const llvm::BasicBlock *bb)
    : def(def), version(version), bb(bb) {}
  virtual ~MSSAVar() {}

  MSSADef *def;
  unsigned version;
  const llvm::BasicBlock *bb;
};

class MSSADef {
 public:
  enum TYPE {
    PHI,
    CALL,
    RETCALL,
    STORE,
    CHI,
    ENTRY
  };

  MSSADef(MemReg *region, TYPE type)
    : region(region), var(NULL), type(type) {}

  virtual ~MSSADef() {}
  MemReg *region;
  //  unsigned version;
  MSSAVar *var;
  TYPE type;
};

class MSSAPhi : public MSSADef {
public:
  MSSAPhi(MemReg *region) : MSSADef(region, PHI) {}

  llvm::DenseMap<int, MSSAVar *> opsVar;
  std::set<const llvm::Value *> preds;

  static inline bool classof(const MSSAPhi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAChi : public MSSADef {
public:
  MSSAChi(MemReg *region, TYPE type) : MSSADef(region, type), opVar(NULL) {}

  MSSAVar *opVar;

  static inline bool classof(const MSSAChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAEntryChi : public MSSAChi {
public:
  MSSAEntryChi(MemReg *region, const llvm::Function *func)
    : MSSAChi(region, ENTRY), func(func) {}
  const llvm::Function *func;

  static inline bool classof(const MSSAEntryChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSAStoreChi : public MSSAChi {
public:
  MSSAStoreChi(MemReg *region, const llvm::StoreInst *inst)
    : MSSAChi(region, STORE), inst(inst) {}
  const llvm::StoreInst *inst;

  static inline bool classof(const MSSAStoreChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSACallChi : public MSSAChi {
public:
  MSSACallChi(MemReg *region, const llvm::Function *called, unsigned argNo)
    : MSSAChi(region, CALL), called(called), argNo(argNo) {}
  const llvm::Function *called;

  unsigned argNo;
  static inline bool classof(const MSSACallChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY;
  }
};

class MSSARetCallChi : public MSSAChi {
public:
  MSSARetCallChi(MemReg *region, const llvm::Function *called)
    : MSSAChi(region, RETCALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSARetCallChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == RETCALL ||
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

  MSSAMu(MemReg *region, TYPE type) : region(region), var(NULL), type(type) {}
  virtual ~MSSAMu() {}
  MemReg *region;
  MSSAVar *var;
  TYPE type;
};

class MSSALoadMu : public MSSAMu {
 public:
  MSSALoadMu(MemReg *region, const llvm::LoadInst *inst)
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
  MSSACallMu(MemReg *region, const llvm::Function *called, unsigned argNo)
    : MSSAMu(region, CALL), called(called), argNo(argNo) {}
  const llvm::Function *called;

  unsigned argNo;

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
  MSSARetMu(MemReg *region, const llvm::Function *func)
    : MSSAMu(region, RET), func(func) {}

  const llvm::Function *func;

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
