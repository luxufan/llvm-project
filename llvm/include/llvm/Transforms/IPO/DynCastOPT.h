#ifndef LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
#define LLVM_TRANSFORMS_IPO_DYNCASTOPT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"

namespace llvm {

class DynCastOPTPass : public PassInfoMixin<DynCastOPTPass> {
  using GUID = GlobalValue::GUID;
  using BaseClass = std::pair<GUID, int64_t>;

public:
  using CHAMapType = DenseMap<GUID, SmallVector<BaseClass, 2>>;

private:
  // Map from class to its base classes and offset pair
  // Value * is the pointer of RTTI descriptor
  CHAMapType CHA;

  // Map from class to its super classes
  // Value * is the pointer of RTTI descriptor
  DenseMap<GUID, SmallVector<GUID>> SuperClasses;

  // Maps from class to its virtual table address pointer in itself's virtual
  // table. The key is the pointer of RTTI descriptor
  DenseMap<GUID, Constant *> VTables;

  // dynamic_cast to these classes can not be optimized.
  SetVector<GUID> Invalids;

  // RTTIs that has external linkage.
  SetVector<GUID> ExternalLinkageRTTIs;

  // RTTIs that are external references.
  SetVector<GUID> ExternalReferenceRTTIs;

  void buildTypeInfoGraph(Module &M);
  void collectVirtualTables(Module &M);
  bool isUniqueBaseInFullCHA(GUID Base);
  bool isUniqueBaseForSuper(GUID Base, GUID Super);

  bool hasPrevailingVTables(GUID RTTIs);

  // Get all of the super classes of Base, also include itself.
  void getSuperClasses(GUID Base, SetVector<GUID> &Supers);

  void getMostDerivedClasses(GUID Base, SetVector<GUID> &MostDerivedClasses);
  bool handleDynCastCallSite(CallInst *CI);
  int64_t computeOffset(GUID Base, GUID Super);

  // Invalidate the class hierarchy analysis if a class is not internal
  void invalidateExternalClass();

  void recordExternalClass(const GlobalVariable *RTTI);

  Value *loadRuntimePtr(Value *StaticPtr, IRBuilder<> &IRB,
                        unsigned AddressSpace);

  bool invalidToOptimize(GUID RTTI) const { return Invalids.contains(RTTI); }

  bool isOffsetToTopMustZero(SetVector<GUID> &SuperClasses);

public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
