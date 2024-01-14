#include "llvm/Analysis/ClassHierarchyAnalysis.h"
#include "llvm/IR/Constants.h"
#include "llvm/IR/ModuleSummaryIndex.h"
#include "llvm/IR/Instructions.h"

using namespace llvm;

AnalysisKey ClassHierarchyAnalysis::Key;

void ClassHierarchyInfo::init(const ModuleSummaryIndex *ImportSummary, bool Optimize) {
  if (ImportSummary) {
    GlobalValue *DyncastDecl = M.getNamedValue("__dynamic_cast");
    if (!DyncastDecl)
      return;

    DenseSet<StringRef> TypeIds;
    for (auto User : DyncastDecl->users()) {
      if (CallInst *C = dyn_cast<CallInst>(User)) {
        TypeIds.insert(C->getArgOperand(2)->getName());
        TypeIds.insert(C->getArgOperand(1)->getName());
      }
    }

    for (auto TypeId : TypeIds) {
      std::string TypeS = ABIManager::GetTypeIdFromTypeInfo(TypeId);
      auto TypeIdIter = ImportSummary->typeIdCompatibleVtableMap().find(TypeS);
      if (TypeIdIter == ImportSummary->typeIdCompatibleVtableMap().end())
        continue;
      if (llvm::any_of(TypeIdIter->second, [&](const TypeIdOffsetVtableInfo &AP) {
        ValueInfo VI = ImportSummary->getValueInfo(GlobalValue::getGUID(AP.VTableVI.name()));
        if (!VI)
          return false;
        if (VI.getSummaryList().empty())
          return false;

        auto &Summary = VI.getSummaryList()[0];
        auto *GVS = dyn_cast<GlobalVarSummary>(Summary.get());

        return GVS->getVCallVisibility() == GlobalValue::VCallVisibilityPublic || ImportSummary->virtualInherts.count(AP.VTableVI.name().str());}))
        mayChangeInLinkTime.insert(TypeIdIter->first);

      for (const TypeIdOffsetVtableInfo &AP : TypeIdIter->second)
        insertTypeIdCompatibleAddressPoint(TypeIdIter->first, AP.VTableVI.name(),
                                           AP.AddressPointOffset);
    }
    return;
  }

  TypeIdCompatibleAddressPoints.clear();

  SmallVector<MDNode *> Types;
  SmallVector<StringRef, 10> UnOptimizableVTables;
  for (GlobalVariable &GV : M.globals()) {
    if (!GV.getName().starts_with(ABIManager::GetVTablePrefix()))
      continue;

    // TODO: support virtual inheritance
    if (GV.hasMetadata(LLVMContext::MD_virtual_inherit))
      UnOptimizableVTables.push_back(GV.getName());

    Types.clear();
    GV.getMetadata(LLVMContext::MD_type, Types);
    for (MDNode *Type : Types) {
      auto TypeID = Type->getOperand(1).get();
      uint64_t Offset =
          cast<ConstantInt>(
              cast<ConstantAsMetadata>(Type->getOperand(0))->getValue())
              ->getZExtValue();

      if (!GV.hasInternalLinkage() && GV.getVCallVisibility() == GlobalObject::VCallVisibilityPublic)
        UnOptimizableVTables.push_back(GV.getName());

      if (auto *TypeId = dyn_cast<MDString>(TypeID)) {
        if (TypeId->getString().ends_with(".virtual"))
          continue;
        insertTypeIdCompatibleAddressPoint(TypeId->getString(), GV.getName(),
                                           Offset);
      }
    }
  }

  while (!UnOptimizableVTables.empty()) {
    StringRef VTable = UnOptimizableVTables.pop_back_val();

    SmallVector<StringRef, 8> ToDelete;

    for (auto It : TypeIdCompatibleAddressPoints)
      for (auto &AP : It.second)
        if (AP.VTableName == VTable)
          ToDelete.push_back(It.first);

    llvm::for_each(ToDelete, [&](StringRef Name) {
      mayChangeInLinkTime.insert(Name);
    });
  }
}
