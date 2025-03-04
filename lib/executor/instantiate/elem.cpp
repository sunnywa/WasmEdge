// SPDX-License-Identifier: Apache-2.0
// SPDX-FileCopyrightText: 2019-2022 Second State INC

#include "executor/executor.h"

#include "common/errinfo.h"
#include "common/log.h"
#include <cstdint>
#include <vector>

namespace WasmEdge {
namespace Executor {

// Instantiate element instance. See "include/executor/executor.h".
Expect<void> Executor::instantiate(Runtime::StoreManager &StoreMgr,
                                   Runtime::Instance::ModuleInstance &ModInst,
                                   const AST::ElementSection &ElemSec) {
  // A frame with temp. module is pushed into stack outside.
  // Instantiate and initialize elements.
  for (const auto &ElemSeg : ElemSec.getContent()) {
    std::vector<RefVariant> InitVals;
    for (const auto &Expr : ElemSeg.getInitExprs()) {
      // Run init expr of every elements and get the result reference.
      if (auto Res = runExpression(StoreMgr, Expr.getInstrs()); !Res) {
        spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Expression));
        spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
        return Unexpect(Res);
      }
      // Pop result from stack.
      InitVals.push_back(StackMgr.pop().get<UnknownRef>());
    }

    uint32_t Offset = 0;
    if (ElemSeg.getMode() == AST::ElementSegment::ElemMode::Active) {
      // Run initialize expression.
      if (auto Res = runExpression(StoreMgr, ElemSeg.getExpr().getInstrs());
          !Res) {
        spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Expression));
        spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
        return Unexpect(Res);
      }
      Offset = StackMgr.pop().get<uint32_t>();

      // Check boundary unless ReferenceTypes or BulkMemoryOperations proposal
      // enabled.
      if (!Conf.hasProposal(Proposal::ReferenceTypes) &&
          !Conf.hasProposal(Proposal::BulkMemoryOperations)) {
        // Table index should be 0. Checked in validation phase.
        auto *TabInst = getTabInstByIdx(StoreMgr, ElemSeg.getIdx());
        // Check elements fits.
        assuming(TabInst);
        if (!TabInst->checkAccessBound(
                Offset, static_cast<uint32_t>(InitVals.size()))) {
          spdlog::error(ErrCode::ElemSegDoesNotFit);
          spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
          return Unexpect(ErrCode::ElemSegDoesNotFit);
        }
      }
    }

    // Insert element instance to store manager.
    uint32_t NewElemInstAddr;
    if (InsMode == InstantiateMode::Instantiate) {
      NewElemInstAddr =
          StoreMgr.pushElement(Offset, ElemSeg.getRefType(), InitVals);
    } else {
      NewElemInstAddr =
          StoreMgr.importElement(Offset, ElemSeg.getRefType(), InitVals);
    }
    ModInst.addElemAddr(NewElemInstAddr);
  }
  return {};
}

// Initialize table with Element Instances. See
// "include/executor/executor.h".
Expect<void> Executor::initTable(Runtime::StoreManager &StoreMgr,
                                 Runtime::Instance::ModuleInstance &,
                                 const AST::ElementSection &ElemSec) {
  // Initialize tables.
  uint32_t Idx = 0;
  for (const auto &ElemSeg : ElemSec.getContent()) {
    auto *ElemInst = getElemInstByIdx(StoreMgr, Idx);
    assuming(ElemInst);
    if (ElemSeg.getMode() == AST::ElementSegment::ElemMode::Active) {
      // Table index is checked in validation phase.
      auto *TabInst = getTabInstByIdx(StoreMgr, ElemSeg.getIdx());
      assuming(TabInst);
      const uint32_t Off = ElemInst->getOffset();

      // Replace table[Off : Off + n] with elem[0 : n].
      if (auto Res = TabInst->setRefs(
              ElemInst->getRefs(), Off, 0,
              static_cast<uint32_t>(ElemInst->getRefs().size()));
          !Res) {
        spdlog::error(ErrInfo::InfoAST(ASTNodeAttr::Seg_Element));
        return Unexpect(Res);
      }

      // Drop the element instance.
      ElemInst->clear();

      // Operation above is equal to the following instruction sequence:
      //   expr(init) -> i32.const off
      //   i32.const 0
      //   i32.const n
      //   table.init idx
      //   elem.drop idx
    } else if (ElemSeg.getMode() ==
               AST::ElementSegment::ElemMode::Declarative) {
      // Drop the element instance.
      ElemInst->clear();
    }
    Idx++;
  }
  return {};
}

} // namespace Executor
} // namespace WasmEdge
