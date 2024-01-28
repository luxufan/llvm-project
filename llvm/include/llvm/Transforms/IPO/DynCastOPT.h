#ifndef LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
#define LLVM_TRANSFORMS_IPO_DYNCASTOPT_H

#include "llvm/ADT/DenseMap.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"

#include <map>
#include <vector>

namespace llvm {

class DynCastOPTPass : public PassInfoMixin<DynCastOPTPass> {
  using GUID = GlobalValue::GUID;
  using BaseClass = std::pair<GUID, int64_t>;

public:
  using CHAMapType = DenseMap<GUID, SmallVector<BaseClass, 2>>;

private:
  LLVMContext *Context;
  Module *M;
  const DataLayout *Layout;
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

  using TypeIdCompatibleVTableInfo = std::map<StringRef, std::vector<uint64_t>>;
  std::map<StringRef, TypeIdCompatibleVTableInfo> TypeIdCompatibleInfo;

  void insertCompatibleVTableInfo(StringRef TypeID, StringRef VTableName, uint64_t Offset) {
    TypeIdCompatibleInfo[TypeID][VTableName].push_back(Offset);
  }

  std::optional<TypeIdCompatibleVTableInfo> getTypeIdCompatibleVTableInfo(StringRef TypeID) {
      auto Result = TypeIdCompatibleInfo.find(TypeID);
      if (Result == TypeIdCompatibleInfo.end())
        return std::nullopt;
      return Result->second;
  }

  uint64_t getUniqueVTableOffset(StringRef TypeID, StringRef VTableName) {
    auto Info = getTypeIdCompatibleVTableInfo(TypeID);
    assert(Info && "TypeID is not in map");
    auto I = (*Info).find(VTableName);
    assert(I != (*Info).end() && "VTableName is not in map");

    assert(I->second.size() == 1 && "Non unique");
    return I->second[0];
  }

  void buildTypeInfoGraph(Module &M);
  void collectVirtualTables(Module &M);
  bool isUniqueBaseInFullCHA(StringRef Base);
  bool isUniqueBaseForSuper(GUID Base, GUID Super);

  bool hasPrevailingVTables(StringRef RTTIs);

  // Get all of the super classes of Base, also include itself.
  void getSuperClasses(StringRef Base, SetVector<StringRef> &Supers);

  bool handleDynCastCallSite(CallInst *CI);
  Constant *computeOffset(StringRef Base, GlobalVariable *Super);

  // Invalidate the class hierarchy analysis if a class is not internal
  void invalidateExternalClass();

  void recordExternalClass(const GlobalVariable *RTTI);

  bool invalidToOptimize(GUID RTTI) const { return Invalids.contains(RTTI); }

  bool isOffsetToTopMustZero(StringRef Class);

public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
