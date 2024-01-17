#ifndef LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
#define LLVM_TRANSFORMS_IPO_DYNCASTOPT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/PassManager.h"
#include "llvm/ADT/SetVector.h"

namespace llvm {

class DynCastOPTPass : public PassInfoMixin<DynCastOPTPass> {
  using BaseClass = std::pair<const Value *, int64_t>;

private:
  // Map from class to its base classes and offset pair
  // Value * is the pointer of RTTI descriptor
  DenseMap<const Value *, SmallVector<BaseClass, 2>> CHA;

  // Map from class to its super classes
  // Value * is the pointer of RTTI descriptor
  DenseMap<const Value *, SmallVector<const Value *>> SuperClasses;

  // Maps from class to its virtual table address pointer in itself's virtual
  // table. The key is the pointer of RTTI descriptor
  DenseMap<const Value *, Constant *> VTables;

  // dynamic_cast to these classes can not be optimized.
  SetVector<const Value *> Invalid;

  void buildTypeInfoGraph(Module &M);
  void collectVirtualTables(Module &M);
  bool isUniqueBaseInFullCHA(const Value *Base);
  bool isUniqueBaseForSuper(const Value *Base, const Value *Super);

  bool hasPrevailingVTables(const SetVector<const Value *> &RTTIs);

  // Get all of the super classes of Base, also include itself.
  void getSuperClasses(const Value *Base, SetVector<const Value *> &Supers);

  void getMostDerivedClasses(const Value *Base,
                             SetVector<const Value *> &MostDerivedClasses);
  bool handleDynCastCallSite(CallInst *CI);
  int64_t computeOffset(const Value *Base, const Value *Super);

  // Invalidate the class hierarchy analysis if a class is not internal
  void invalidateExternalClass(const GlobalVariable *RTTI);

  bool invalidToOptimize(const Value *RTTI) const {
    return Invalid.contains(RTTI);
  }

public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
