#ifndef MSSAMUCHI_H
#define MSSAMUCHI_H

#include "parcoach/MemoryRegion.h"

#include "llvm/IR/Instructions.h"

#include <set>
#include <vector>

class MSSAVar;

class MSSADef {
public:
  enum TYPE {
    PHI,
    CALL,
    STORE,
    SYNC,
    CHI,
    ENTRY,

    // External functions
    EXTVARARG,
    EXTARG,
    EXTRET,
    EXTCALL,
    EXTRETCALL
  };

  MSSADef(MemRegEntry *region, TYPE type)
      : region(region), var(nullptr), type(type) {}

  virtual ~MSSADef() {}
  virtual std::string getName() const { return region->getName().str(); }

  MemRegEntry *region;
  std::unique_ptr<MSSAVar> var;
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
  MSSAPhi(MemRegEntry *region) : MSSADef(region, PHI) {}

  std::map<int, MSSAVar *> opsVar;
  std::set<const llvm::Value *> preds;

  static inline bool classof(const MSSADef *m) { return m->type == PHI; }
};

class MSSAChi : public MSSADef {
public:
  MSSAChi(MemRegEntry *region, TYPE type)
      : MSSADef(region, type), opVar(NULL) {}

  MSSAVar *opVar;

  static inline bool classof(const MSSADef *m) { return m->type == CHI; }
};

class MSSAExtVarArgChi : public MSSAChi {
public:
  MSSAExtVarArgChi(const llvm::Function *func)
      : MSSAChi(NULL, EXTVARARG), func(func) {}

  const llvm::Function *func;

  virtual std::string getName() const { return "VarArg"; }

  static inline bool classof(const MSSADef *m) { return m->type == EXTVARARG; }
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

  static inline bool classof(const MSSADef *m) { return m->type == EXTARG; }
};

class MSSAExtRetChi : public MSSAChi {
public:
  MSSAExtRetChi(const llvm::Function *func)
      : MSSAChi(NULL, EXTRET), func(func) {}

  const llvm::Function *func;

  virtual std::string getName() const { return "retval"; }

  static inline bool classof(const MSSADef *m) { return m->type == EXTRET; }
};

class MSSAEntryChi : public MSSAChi {
public:
  MSSAEntryChi(MemRegEntry *region, const llvm::Function *func)
      : MSSAChi(region, ENTRY), func(func) {}
  const llvm::Function *func;

  static inline bool classof(const MSSADef *m) { return m->type == ENTRY; }
};

class MSSAStoreChi : public MSSAChi {
public:
  MSSAStoreChi(MemRegEntry *region, const llvm::StoreInst *inst)
      : MSSAChi(region, STORE), inst(inst) {}
  const llvm::StoreInst *inst;

  static inline bool classof(const MSSADef *m) { return m->type == STORE; }
};

class MSSASyncChi : public MSSAChi {
public:
  MSSASyncChi(MemRegEntry *region, const llvm::Instruction *inst)
      : MSSAChi(region, SYNC), inst(inst) {}
  const llvm::Instruction *inst;

  static inline bool classof(const MSSADef *m) { return m->type == SYNC; }
};

class MSSACallChi : public MSSAChi {
public:
  MSSACallChi(MemRegEntry *region, const llvm::Function *called,
              const llvm::Instruction *inst)
      : MSSAChi(region, CALL), called(called), inst(inst) {}
  const llvm::Function *called;
  const llvm::Instruction *inst;

  static inline bool classof(const MSSADef *m) { return m->type == CALL; }
};

class MSSAExtCallChi : public MSSAChi {
public:
  MSSAExtCallChi(MemRegEntry *region, const llvm::Function *called,
                 unsigned argNo, const llvm::Instruction *inst)
      : MSSAChi(region, EXTCALL), called(called), argNo(argNo), inst(inst) {}
  const llvm::Function *called;
  unsigned argNo;
  const llvm::Instruction *inst;

  static inline bool classof(const MSSADef *m) { return m->type == EXTCALL; }
};

class MSSAExtRetCallChi : public MSSAChi {
public:
  MSSAExtRetCallChi(MemRegEntry *region, const llvm::Function *called)
      : MSSAChi(region, EXTRETCALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSADef *m) { return m->type == EXTRETCALL; }
};

class MSSAMu {
public:
  enum TYPE { LOAD, CALL, RET, EXTCALL };

  MSSAMu(MemRegEntry *region, TYPE type)
      : region(region), var(nullptr), type(type) {}
  virtual ~MSSAMu() {}
  MemRegEntry *region;
  MSSAVar *var;
  TYPE type;
};

class MSSALoadMu : public MSSAMu {
public:
  MSSALoadMu(MemRegEntry *region, const llvm::LoadInst *inst)
      : MSSAMu(region, LOAD), inst(inst) {}
  const llvm::LoadInst *inst;

  static inline bool classof(const MSSAMu *m) { return m->type == LOAD; }
};

class MSSACallMu : public MSSAMu {
public:
  MSSACallMu(MemRegEntry *region, const llvm::Function *called)
      : MSSAMu(region, CALL), called(called) {}
  const llvm::Function *called;

  static inline bool classof(const MSSAMu *m) { return m->type == CALL; }
};

class MSSAExtCallMu : public MSSAMu {
public:
  MSSAExtCallMu(MemRegEntry *region, const llvm::Function *called,
                unsigned argNo)
      : MSSAMu(region, EXTCALL), called(called), argNo(argNo) {}
  const llvm::Function *called;

  unsigned argNo;

  static inline bool classof(const MSSAMu *m) { return m->type == EXTCALL; }
};

class MSSARetMu : public MSSAMu {
public:
  MSSARetMu(MemRegEntry *region, const llvm::Function *func)
      : MSSAMu(region, RET), func(func) {}

  const llvm::Function *func;

  static inline bool classof(const MSSAMu *m) { return m->type == RET; }
};

#endif /* MSSAMUCHI */
