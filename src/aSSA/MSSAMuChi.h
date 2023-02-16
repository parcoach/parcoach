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
  MSSAVar(MSSADef *def, unsigned version, llvm::BasicBlock const *bb)
      : def(def), version(version), bb(bb) {}
  virtual ~MSSAVar() {}

  MSSADef *def;
  unsigned version;
  llvm::BasicBlock const *bb;
  std::string getName() const {
    return def->getName() + std::to_string(version);
  }
};

class MSSAPhi : public MSSADef {
public:
  MSSAPhi(MemRegEntry *region) : MSSADef(region, PHI) {}

  std::map<int, MSSAVar *> opsVar;
  std::set<llvm::Value const *> preds;

  static inline bool classof(MSSADef const *m) { return m->type == PHI; }
};

class MSSAChi : public MSSADef {
public:
  MSSAChi(MemRegEntry *region, TYPE type)
      : MSSADef(region, type), opVar(NULL) {}

  MSSAVar *opVar;

  static inline bool classof(MSSADef const *m) { return m->type == CHI; }
};

class MSSAExtVarArgChi : public MSSAChi {
public:
  MSSAExtVarArgChi(llvm::Function const *func)
      : MSSAChi(NULL, EXTVARARG), func(func) {}

  llvm::Function const *func;

  virtual std::string getName() const { return "VarArg"; }

  static inline bool classof(MSSADef const *m) { return m->type == EXTVARARG; }
};

class MSSAExtArgChi : public MSSAChi {
public:
  MSSAExtArgChi(llvm::Function const *func, unsigned argNo)
      : MSSAChi(NULL, EXTARG), func(func), argNo(argNo) {}

  llvm::Function const *func;
  unsigned argNo;

  virtual std::string getName() const {
    return "arg" + std::to_string(argNo) + "_";
  }

  static inline bool classof(MSSADef const *m) { return m->type == EXTARG; }
};

class MSSAExtRetChi : public MSSAChi {
public:
  MSSAExtRetChi(llvm::Function const *func)
      : MSSAChi(NULL, EXTRET), func(func) {}

  llvm::Function const *func;

  virtual std::string getName() const { return "retval"; }

  static inline bool classof(MSSADef const *m) { return m->type == EXTRET; }
};

class MSSAEntryChi : public MSSAChi {
public:
  MSSAEntryChi(MemRegEntry *region, llvm::Function const *func)
      : MSSAChi(region, ENTRY), func(func) {}
  llvm::Function const *func;

  static inline bool classof(MSSADef const *m) { return m->type == ENTRY; }
};

class MSSAStoreChi : public MSSAChi {
public:
  MSSAStoreChi(MemRegEntry *region, llvm::StoreInst const *inst)
      : MSSAChi(region, STORE), inst(inst) {}
  llvm::StoreInst const *inst;

  static inline bool classof(MSSADef const *m) { return m->type == STORE; }
};

class MSSASyncChi : public MSSAChi {
public:
  MSSASyncChi(MemRegEntry *region, llvm::Instruction const *inst)
      : MSSAChi(region, SYNC), inst(inst) {}
  llvm::Instruction const *inst;

  static inline bool classof(MSSADef const *m) { return m->type == SYNC; }
};

class MSSACallChi : public MSSAChi {
public:
  MSSACallChi(MemRegEntry *region, llvm::Function const *called,
              llvm::Instruction const *inst)
      : MSSAChi(region, CALL), called(called), inst(inst) {}
  llvm::Function const *called;
  llvm::Instruction const *inst;

  static inline bool classof(MSSADef const *m) { return m->type == CALL; }
};

class MSSAExtCallChi : public MSSAChi {
public:
  MSSAExtCallChi(MemRegEntry *region, llvm::Function const *called,
                 unsigned argNo, llvm::Instruction const *inst)
      : MSSAChi(region, EXTCALL), called(called), argNo(argNo), inst(inst) {}
  llvm::Function const *called;
  unsigned argNo;
  llvm::Instruction const *inst;

  static inline bool classof(MSSADef const *m) { return m->type == EXTCALL; }
};

class MSSAExtRetCallChi : public MSSAChi {
public:
  MSSAExtRetCallChi(MemRegEntry *region, llvm::Function const *called)
      : MSSAChi(region, EXTRETCALL), called(called) {}
  llvm::Function const *called;

  static inline bool classof(MSSADef const *m) { return m->type == EXTRETCALL; }
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
  MSSALoadMu(MemRegEntry *region, llvm::LoadInst const *inst)
      : MSSAMu(region, LOAD), inst(inst) {}
  llvm::LoadInst const *inst;

  static inline bool classof(MSSAMu const *m) { return m->type == LOAD; }
};

class MSSACallMu : public MSSAMu {
public:
  MSSACallMu(MemRegEntry *region, llvm::Function const *called)
      : MSSAMu(region, CALL), called(called) {}
  llvm::Function const *called;

  static inline bool classof(MSSAMu const *m) { return m->type == CALL; }
};

class MSSAExtCallMu : public MSSAMu {
public:
  MSSAExtCallMu(MemRegEntry *region, llvm::Function const *called,
                unsigned argNo)
      : MSSAMu(region, EXTCALL), called(called), argNo(argNo) {}
  llvm::Function const *called;

  unsigned argNo;

  static inline bool classof(MSSAMu const *m) { return m->type == EXTCALL; }
};

class MSSARetMu : public MSSAMu {
public:
  MSSARetMu(MemRegEntry *region, llvm::Function const *func)
      : MSSAMu(region, RET), func(func) {}

  llvm::Function const *func;

  static inline bool classof(MSSAMu const *m) { return m->type == RET; }
};

#endif /* MSSAMUCHI */
