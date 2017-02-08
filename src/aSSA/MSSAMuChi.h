#ifndef MSSAMUCHI_H
#define MSSAMUCHI_H

#include "MemoryRegion.h"

#include "llvm/IR/Instructions.h"

#include <vector>
#include <set>

class MSSAVar;

class MSSADef {
 public:
  enum TYPE {
    PHI,
    CALL,
    STORE,
    CHI,
    ENTRY,

    // External functions
    EXTVARARG,
    EXTARG,
    EXTRET,
    EXTCALL,
    EXTRETCALL
  };

  MSSADef(MemReg *region, TYPE type)
    : region(region), var(NULL), type(type) {}

  virtual ~MSSADef() {}
  virtual std::string getName() const {
    return region->getName();
  }

  MemReg *region;
  MSSAVar *var;
  TYPE type;
};

class MSSAVar {
public:
  MSSAVar(MSSADef *def, unsigned version, const llvm::BasicBlock *bb)
    : def(def), version(version), bb(bb) {}
  virtual ~MSSAVar() {}

  MSSADef *def;
  unsigned version;
  const llvm::BasicBlock *bb;
  std::string getName() const {
    return def->getName() + std::to_string(version);
  }
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
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
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
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAExtVarArgChi : public MSSAChi {
public:
  MSSAExtVarArgChi(const llvm::Function *func)
    : MSSAChi(NULL, EXTVARARG), func(func) {}

  const llvm::Function *func;

  virtual std::string getName() const {
    return "VarArg";
  }

  static inline bool classeof(const MSSAExtVarArgChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAExtArgChi : public MSSAChi {
public:
  MSSAExtArgChi(const llvm::Function *func, unsigned argNo)
    : MSSAChi(NULL, EXTARG), func(func), argNo(argNo) {}

  const llvm::Function *func;
  unsigned argNo;

  virtual std::string getName() const {
    return "arg" + std::to_string(argNo) + "_";
  }

  static inline bool classeof(const MSSAExtArgChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAExtRetChi : public MSSAChi {
public:
  MSSAExtRetChi(const llvm::Function *func)
    : MSSAChi(NULL, EXTRET), func(func) {}

  const llvm::Function *func;

  virtual std::string getName() const {
    return "retval";
  }

  static inline bool classeof(const MSSAExtRetChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
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
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
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
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSACallChi : public MSSAChi {
public:
  MSSACallChi(MemReg *region, const llvm::Function *called)
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
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAExtCallChi : public MSSAChi {
public:
  MSSAExtCallChi(MemReg *region, const llvm::Function *called,
		 unsigned argNo)
    : MSSAChi(region, EXTCALL), called(called), argNo(argNo) {}
  const llvm::Function *called;
  unsigned argNo;

  static inline bool classof(const MSSAExtCallChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAExtRetCallChi : public MSSAChi {
public:
  MSSAExtRetCallChi(MemReg *region, const llvm::Function *called)
    : MSSAChi(region, EXTRETCALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSAExtRetCallChi *m) {
    return true;
  }

  static inline bool classof(const MSSADef *m) {
    return m->type == PHI ||
      m->type == CALL ||
      m->type == STORE ||
      m->type == CHI ||
      m->type == ENTRY ||
      m->type == EXTVARARG ||
      m->type == EXTARG ||
      m->type == EXTRET ||
      m->type == EXTCALL ||
      m->type == EXTRETCALL;
  }
};

class MSSAMu {
public:
  enum TYPE {
    LOAD,
    CALL,
    RET,
    EXTCALL
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
      m->type == RET ||
      m->type == EXTCALL;
  }
};

class MSSACallMu : public MSSAMu {
 public:
  MSSACallMu(MemReg *region, const llvm::Function *called)
    : MSSAMu(region, CALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSACallMu *m) {
    return true;
  }

  static inline bool classof(const MSSAMu *m) {
    return m->type == LOAD ||
      m->type == CALL ||
      m->type == RET ||
      m->type == EXTCALL;
  }
};

class MSSAExtCallMu : public MSSAMu {
 public:
  MSSAExtCallMu(MemReg *region, const llvm::Function *called, unsigned argNo)
    : MSSAMu(region, CALL), called(called), argNo(argNo) {}
  const llvm::Function *called;

  unsigned argNo;

  static inline bool classof(const MSSAExtCallMu *m) {
    return true;
  }

  static inline bool classof(const MSSAMu *m) {
    return m->type == LOAD ||
      m->type == CALL ||
      m->type == RET ||
      m->type == EXTCALL;
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
      m->type == RET ||
      m->type == EXTCALL;
  }
};

#endif /* MSSAMUCHI */
