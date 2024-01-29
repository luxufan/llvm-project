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

struct AddressPoint {
  StringRef VTableName;
  uint64_t Offset;

  AddressPoint(StringRef Name, uint64_t Offset)
    : VTableName(Name), Offset(Offset) {}
};

class DynCastOPTPass : public PassInfoMixin<DynCastOPTPass> {
private:
  LLVMContext *Context;
  Module *M;
  const DataLayout *Layout;

  // dynamic_cast to these classes can not be optimized.
  SetVector<StringRef> Invalids;

  using AddressPointsVector = std::vector<AddressPoint>;
  std::map<StringRef, AddressPointsVector> TypeIdCompatibleAddressPoints;

  std::map<StringRef, uint64_t> PrimaryVTableAddressPoint;

  void insertTypeIdCompatibleAddressPoint(StringRef TypeId, StringRef VTableName, uint64_t Offset) {
    TypeIdCompatibleAddressPoints[TypeId].push_back(AddressPoint(VTableName, Offset));
    if (VTableName.substr(4) == TypeId.substr(4))
      PrimaryVTableAddressPoint.insert(std::make_pair(VTableName, Offset));
  }

  std::optional<AddressPointsVector> getTypeIdCompatibleVTableInfo(StringRef TypeID) {
    auto Result = TypeIdCompatibleAddressPoints.find(TypeID);
    if (Result == TypeIdCompatibleAddressPoints.end())
      return std::nullopt;
    return Result->second;
  }

  uint64_t getPrimaryAddressPoint(StringRef VTableName) {
    assert(PrimaryVTableAddressPoint.count(VTableName));
    return PrimaryVTableAddressPoint[VTableName];
  }

  void collectVirtualTables(Module &M);
  bool doesAllAddressPointHaveDifferentVTable(std::vector<AddressPoint> AddressPoints);

  bool allCompatibleAddressPointPrevailing(StringRef RTTIs);

  bool handleDynCastCallSite(CallInst *CI);
  Constant *getOffsetToTop(GlobalVariable *Super, uint64_t Offset);

  bool invalidToOptimize(StringRef TypeId) const { return Invalids.contains(TypeId); }

  bool isOffsetToTopMustZero(StringRef Class);

public:
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
