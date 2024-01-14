#ifndef LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
#define LLVM_TRANSFORMS_IPO_DYNCASTOPT_H

#include "llvm/ADT/Hashing.h"
#include "llvm/ADT/SetVector.h"
#include "llvm/ADT/SmallVector.h"
#include "llvm/ADT/StringRef.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/GlobalVariable.h"
#include "llvm/IR/IRBuilder.h"
#include "llvm/IR/PassManager.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/Analysis/ClassHierarchyAnalysis.h"

#include <map>
#include <set>
#include <vector>

namespace llvm {

class DynCastInfo {
public:
  BasicBlock *Block;
  Value *StaticPtr;
  ConstantInt *Src2DstHint;
  std::string StaticTypeIdName;
  std::string DestTypeIdName;
  bool isNegativeHint() const { return Src2DstHint->getValue().isNegative(); }
};

class DynCastOPTPass : public PassInfoMixin<DynCastOPTPass> {
  static constexpr unsigned OffsetToTypeName = 4;
  static constexpr int OffsetFromOffsetToTopToAddressPoint = 2;

  bool UseCommandLine = false;
  using ABIManager = CXXABIManager<Itanium>;

private:
  // Map from the destination type id of __dynamic_cast to the global variable
  // and its type hierarch subtree index range in the merged virtual table group.
  std::map<std::string, std::pair<std::string, std::pair<uint64_t, uint64_t>>>
      RangeMap;
  using AddressPointsSet = std::set<AddressPoint>;

  LLVMContext *Context;
  ClassHierarchyInfo *CHAInfo;
  Module *M;
  const DataLayout *DL;
  Type *Int64Ty;
  Type *Int32Ty;
  Type *Int8Ty;
  Type *Int1Ty;
  Type *VoidTy;
  Type *PTy;

  const ModuleSummaryIndex *ImportSummary;
  ModuleSummaryIndex *ExportSummary;

  // dynamic_cast to these classes can not be optimized.
  SetVector<StringRef> Invalids;

  bool getDynCastInfo(CallInst *CI, DynCastInfo *Info);

  bool layoutVTableImpl(std::string DestTypeIdName, raw_fd_ostream *OS);

  void analyzeTypeHierarchy();

  bool layoutVTables();

  bool doRangeTest(CallInst *CI);

  bool determineLayoutOrder(StringRef VTableName,
                            SmallVectorImpl<StringRef> &VTables,
                            StringMap<std::pair<unsigned, unsigned>> &Ranges);

  // Create a reference to the virtual table that its definition is the
  // out side of the current module.
  void createVTables(SmallVectorImpl<AddressPoint> &AddressPoints);

  bool handleDynCastCallSite(CallInst *CI);
  Constant *getOffsetToTop(GlobalValue *Super, uint64_t Offset);

  bool offsetToTopMustBeZero(StringRef Class);

  Constant *getPrimaryVTable(GlobalValue *VTableGroup) {
    return ConstantExpr::getInBoundsGetElementPtr(Int8Ty, VTableGroup,
                    ConstantInt::get(Int64Ty, getPrimaryVTableOffset()));
  }

  void reduceTest(StringRef VTable);

  void recordTypeMetadata(ModuleSummaryIndex *Summary);

  // FIXME: Here we assume the offset from virtual table group variable to
  // the address point of the primary virtual table is 2 * sizeof(pointer).
  // Once we support the virtual inheritance, this function needs to be changed.
  uint64_t getPrimaryVTableOffset() {
    return 2 * DL->getPointerSize();
  }

  Constant *getAddressPointExpr(AddressPoint AP) {
    GlobalValue *VTableGroup = M->getNamedValue(AP.VTableName);
    return ConstantExpr::getInBoundsGetElementPtr(Int8Ty, VTableGroup,
                                ConstantInt::get(Int64Ty, AP.Offset));
  }

  // Speculate the address that will be returned if dynamic_cast success.
  // If the address could not be speculated, it returns nullptr.
  Value *speculateAddress(ConstantInt *Src2DstHint, Value *StaticPtr,
                          StringRef DestTypeIdName,
                          Type *PTy, IRBuilder<> &IRB);

  Value *loadRuntimePtr(Value *StaticPtr, IRBuilder<> &IRB, Type *PTy,
                        StringRef StaticTypeInfo, bool OffsetToTopMustBeZero);

  Value *loadRuntimeVPtr(Value *RuntimePtr, IRBuilder<> &IRB, Type *PTy);

  // Construct a series of comparation and return the first check basic block
  // and the last check basic block.
  std::pair<BasicBlock *, BasicBlock *>
  compareAddressPoints(SmallVectorImpl<AddressPoint> &AddressPoints,
                       Value *RuntimeVPtr, BasicBlock *InsertPt,
                       BasicBlock *CheckSuccess, BasicBlock *Landing,
                       bool NegativeTest);

  BasicBlock *compareAddressPoint(Constant *AddressPoint, Value *RuntimeVPtr,
    BasicBlock *CheckSuccess, BasicBlock *CheckFail, BasicBlock *InsertPt, ICmpInst::Predicate Pred);

public:
    DynCastOPTPass(ModuleSummaryIndex *ExportSummary, const ModuleSummaryIndex *ImportSummary) : ImportSummary(ImportSummary), ExportSummary(ExportSummary) {}
    DynCastOPTPass() : UseCommandLine(true), ImportSummary(nullptr), ExportSummary(nullptr) {}
  PreservedAnalyses run(Module &M, ModuleAnalysisManager &);
};

} // namespace llvm

#endif // LLVM_TRANSFORMS_IPO_DYNCASTOPT_H
