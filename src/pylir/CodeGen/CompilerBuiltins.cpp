//  Copyright 2022 Markus Böck
//
//  Licensed under the Apache License v2.0 with LLVM Exceptions.
//  See https://llvm.org/LICENSE.txt for license information.
//  SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#include <mlir/Dialect/Arithmetic/IR/Arithmetic.h>
#include <mlir/Dialect/ControlFlow/IR/ControlFlowOps.h>

#include <pylir/Optimizer/PylirPy/IR/PylirPyDialect.hpp>
#include <pylir/Optimizer/PylirPy/Util/Util.hpp>

#include "CodeGen.hpp"

namespace
{

void implementBlock(mlir::OpBuilder& builder, mlir::Block* block)
{
    PYLIR_ASSERT(block);
    if (auto* next = builder.getBlock()->getNextNode())
    {
        block->insertBefore(next);
    }
    else
    {
        builder.getBlock()->getParent()->push_back(block);
    }
    builder.setInsertionPointToStart(block);
}

mlir::Value binOp(pylir::Py::PyBuilder& builder, llvm::StringRef method, llvm::StringRef revMethod, mlir::Value lhs,
                  mlir::Value rhs)
{
    auto trueC = builder.create<mlir::arith::ConstantIntOp>(true, 1);
    auto falseC = builder.create<mlir::arith::ConstantIntOp>(false, 1);
    auto* endBlock = new mlir::Block;
    endBlock->addArgument(builder.getDynamicType(), builder.getCurrentLoc());
    if (method == "__eq__" || method == "__ne__")
    {
        auto isSame = builder.createIs(lhs, rhs);
        auto* continueNormal = new mlir::Block;
        builder.create<mlir::cf::CondBranchOp>(isSame, endBlock,
                                               mlir::ValueRange{builder.createConstant(method == "__eq__")},
                                               continueNormal, mlir::ValueRange{});
        implementBlock(builder, continueNormal);
    }
    auto lhsType = builder.createTypeOf(lhs);
    auto rhsType = builder.createTypeOf(rhs);
    auto sameType = builder.createIs(lhsType, rhsType);
    auto* normalMethodBlock = new mlir::Block;
    normalMethodBlock->addArgument(builder.getI1Type(), builder.getCurrentLoc());
    auto* differentTypeBlock = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(sameType, normalMethodBlock, mlir::ValueRange{trueC}, differentTypeBlock,
                                           mlir::ValueRange{});

    implementBlock(builder, differentTypeBlock);
    auto mro = builder.createTypeMRO(rhsType);
    auto subclass = builder.createTupleContains(mro, lhsType);
    auto* isSubclassBlock = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(subclass, isSubclassBlock, normalMethodBlock, mlir::ValueRange{falseC});

    implementBlock(builder, isSubclassBlock);
    auto rhsMroTuple = builder.createTypeMRO(rhsType);
    auto lookup = builder.createMROLookup(rhsMroTuple, revMethod);
    auto* hasReversedBlock = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(lookup.getSuccess(), hasReversedBlock, normalMethodBlock,
                                           mlir::ValueRange{falseC});

    implementBlock(builder, hasReversedBlock);
    auto lhsMroTuple = builder.createTypeMRO(lhsType);
    auto lhsLookup = builder.createMROLookup(lhsMroTuple, revMethod);
    auto* callReversedBlock = new mlir::Block;
    auto* lhsHasReversedBlock = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(lhsLookup.getSuccess(), lhsHasReversedBlock, callReversedBlock);

    implementBlock(builder, lhsHasReversedBlock);
    auto sameImplementation = builder.createIs(lookup.getResult(), lhsLookup.getResult());
    builder.create<mlir::cf::CondBranchOp>(sameImplementation, normalMethodBlock, mlir::ValueRange{falseC},
                                           callReversedBlock, mlir::ValueRange{});

    implementBlock(builder, callReversedBlock);
    auto tuple = builder.createMakeTuple({rhs, lhs});
    auto reverseResult =
        pylir::Py::buildSpecialMethodCall(builder.getCurrentLoc(), builder, revMethod, tuple, {}, nullptr);
    auto isNotImplemented = builder.createIs(reverseResult, builder.createNotImplementedRef());
    builder.create<mlir::cf::CondBranchOp>(isNotImplemented, normalMethodBlock, mlir::ValueRange{trueC}, endBlock,
                                           mlir::ValueRange{reverseResult});

    implementBlock(builder, normalMethodBlock);
    tuple = builder.createMakeTuple({lhs, rhs});
    auto* typeErrorBlock = new mlir::Block;
    auto result = pylir::Py::buildTrySpecialMethodCall(builder.getCurrentLoc(), builder, method, tuple, {},
                                                       typeErrorBlock, nullptr);
    isNotImplemented = builder.createIs(result, builder.createNotImplementedRef());
    auto* maybeTryReverse = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(isNotImplemented, maybeTryReverse, endBlock, mlir::ValueRange{result});

    implementBlock(builder, maybeTryReverse);
    auto* actuallyTryReverse = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(normalMethodBlock->getArgument(0), typeErrorBlock, actuallyTryReverse);

    implementBlock(builder, actuallyTryReverse);
    tuple = builder.createMakeTuple({rhs, lhs});
    reverseResult = pylir::Py::buildTrySpecialMethodCall(builder.getCurrentLoc(), builder, revMethod, tuple, {},
                                                         typeErrorBlock, nullptr);
    isNotImplemented = builder.createIs(reverseResult, builder.createNotImplementedRef());
    builder.create<mlir::cf::CondBranchOp>(isNotImplemented, typeErrorBlock, endBlock, mlir::ValueRange{reverseResult});

    implementBlock(builder, typeErrorBlock);
    if (method != "__eq__" && method != "__ne__")
    {
        auto typeError =
            pylir::Py::buildException(builder.getCurrentLoc(), builder, pylir::Builtins::TypeError.name, {}, nullptr);
        builder.createRaise(typeError);
    }
    else
    {
        mlir::Value isEqual = builder.createIs(lhs, rhs);
        if (method == "__ne__")
        {
            isEqual = builder.create<mlir::arith::XOrIOp>(isEqual, trueC);
        }
        mlir::Value boolean = builder.createBoolFromI1(isEqual);
        builder.create<mlir::cf::BranchOp>(endBlock, boolean);
    }

    implementBlock(builder, endBlock);
    return endBlock->getArgument(0);
}

void buildRevBinOpCompilerBuiltin(pylir::Py::PyBuilder& builder, llvm::StringRef functionName, llvm::StringRef method,
                                  llvm::StringRef revMethod)
{
    auto func = builder.create<mlir::func::FuncOp>(
        functionName,
        builder.getFunctionType({builder.getDynamicType(), builder.getDynamicType()}, builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());
    auto result = binOp(builder, method, revMethod, func.getArgument(0), func.getArgument(1));
    builder.create<mlir::func::ReturnOp>(result);
}

void buildBinOpCompilerBuiltin(pylir::Py::PyBuilder& builder, llvm::StringRef functionName, llvm::StringRef method)
{
    auto func = builder.create<mlir::func::FuncOp>(
        functionName,
        builder.getFunctionType({builder.getDynamicType(), builder.getDynamicType()}, builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());
    mlir::Value lhs = func.getArgument(0);
    mlir::Value rhs = func.getArgument(1);
    auto tuple = builder.createMakeTuple({lhs, rhs});
    auto result = pylir::Py::buildSpecialMethodCall(builder.getCurrentLoc(), builder, method, tuple, {}, nullptr);
    builder.create<mlir::func::ReturnOp>(result);
}

void buildUnaryOpCompilerBuiltin(pylir::Py::PyBuilder& builder, llvm::StringRef functionName, llvm::StringRef method)
{
    auto func = builder.create<mlir::func::FuncOp>(
        functionName, builder.getFunctionType({builder.getDynamicType()}, builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());
    auto tuple = builder.createMakeTuple({func.getArgument(0)});
    auto result = pylir::Py::buildSpecialMethodCall(builder.getCurrentLoc(), builder, method, tuple, {}, nullptr);
    builder.create<mlir::func::ReturnOp>(result);
}

void buildCallOpCompilerBuiltin(pylir::Py::PyBuilder& builder, llvm::StringRef functionName, llvm::StringRef method)
{
    auto func = builder.create<mlir::func::FuncOp>(
        functionName,
        builder.getFunctionType({builder.getDynamicType(), builder.getDynamicType(), builder.getDynamicType()},
                                builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());
    auto tuple = builder.createTuplePrepend(func.getArgument(0), func.getArgument(1));
    auto result = pylir::Py::buildSpecialMethodCall(builder.getCurrentLoc(), builder, method, tuple,
                                                    func.getArgument(2), nullptr);
    builder.create<mlir::func::ReturnOp>(result);
}

void buildSetItemOpCompilerBuiltin(pylir::Py::PyBuilder& builder, llvm::StringRef functionName, llvm::StringRef method)
{
    auto func = builder.create<mlir::func::FuncOp>(
        functionName,
        builder.getFunctionType({builder.getDynamicType(), builder.getDynamicType(), builder.getDynamicType()},
                                builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());
    auto tuple = builder.createMakeTuple({func.getArgument(0), func.getArgument(1), func.getArgument(2)});
    auto result = pylir::Py::buildSpecialMethodCall(builder.getCurrentLoc(), builder, method, tuple, {}, nullptr);
    builder.create<mlir::func::ReturnOp>(result);
}

void buildMethodCallOpCompilerBuiltin(pylir::Py::PyBuilder& builder)
{
    auto func = builder.create<mlir::func::FuncOp>(
        "pylirMethodCall",
        builder.getFunctionType({builder.getDynamicType(), builder.getDynamicType(), builder.getDynamicType()},
                                builder.getDynamicType()));
    mlir::OpBuilder::InsertionGuard guard{builder};
    builder.setInsertionPointToStart(func.addEntryBlock());

    auto self = func.getArgument(0);
    auto args = func.getArgument(1);
    auto kws = func.getArgument(2);

    auto* condition = new mlir::Block;
    condition->addArgument(builder.getDynamicType(), builder.getCurrentLoc());
    builder.create<mlir::cf::BranchOp>(condition, self);

    func.push_back(condition);
    builder.setInsertionPointToStart(condition);
    self = condition->getArgument(0);
    auto selfType = builder.createTypeOf(self);
    auto mroTuple = builder.createTypeMRO(selfType);
    auto lookup = builder.createMROLookup(mroTuple, "__call__");
    auto* raiseBlock = new mlir::Block;
    auto* body = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(lookup.getSuccess(), body, raiseBlock);

    func.push_back(raiseBlock);
    builder.setInsertionPointToStart(raiseBlock);
    auto typeError =
        pylir::Py::buildException(builder.getCurrentLoc(), builder, pylir::Builtins::TypeError.name, {}, nullptr);
    builder.createRaise(typeError);

    func.push_back(body);
    builder.setInsertionPointToStart(body);
    auto callableType = builder.createTypeOf(lookup.getResult());
    auto isFunction = builder.createIs(callableType, builder.createFunctionRef());
    auto* isFunctionBlock = new mlir::Block;
    auto* notFunctionBlock = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(isFunction, isFunctionBlock, notFunctionBlock);

    func.push_back(isFunctionBlock);
    builder.setInsertionPointToStart(isFunctionBlock);
    mlir::Value result = builder.createFunctionCall(lookup.getResult(),
                                                    {lookup.getResult(), builder.createTuplePrepend(self, args), kws});
    builder.create<mlir::func::ReturnOp>(result);

    func.push_back(notFunctionBlock);
    builder.setInsertionPointToStart(notFunctionBlock);
    mroTuple = builder.createTypeMRO(callableType);
    auto getMethod = builder.createMROLookup(mroTuple, "__get__");
    auto* isDescriptor = new mlir::Block;
    builder.create<mlir::cf::CondBranchOp>(getMethod.getSuccess(), isDescriptor, condition, lookup.getResult());

    func.push_back(isDescriptor);
    builder.setInsertionPointToStart(isDescriptor);
    selfType = builder.createTypeOf(self);
    auto tuple = builder.createMakeTuple({self, selfType});
    auto emptyDict = builder.createConstant(builder.getDictAttr());
    result =
        builder.create<pylir::Py::CallOp>(func, mlir::ValueRange{getMethod.getResult(), tuple, emptyDict}).getResult(0);
    builder.create<mlir::cf::BranchOp>(condition, result);
}

} // namespace

void pylir::CodeGen::createCompilerBuiltinsImpl()
{
#define COMPILER_BUILTIN_REV_BIN_OP(name, slotName, revSlotName) \
    buildRevBinOpCompilerBuiltin(m_builder, COMPILER_BUILTIN_SLOT_TO_API_NAME(slotName), #slotName, #revSlotName);
#define COMPILER_BUILTIN_BIN_OP(name, slotName) \
    buildBinOpCompilerBuiltin(m_builder, COMPILER_BUILTIN_SLOT_TO_API_NAME(slotName), #slotName);

#define COMPILER_BUILTIN_UNARY_OP(name, slotName) \
    buildUnaryOpCompilerBuiltin(m_builder, COMPILER_BUILTIN_SLOT_TO_API_NAME(slotName), #slotName);

#define COMPILER_BUILTIN_TERNARY_OP(name, slotName) \
    build##name##OpCompilerBuiltin(m_builder, COMPILER_BUILTIN_SLOT_TO_API_NAME(slotName), #slotName);

#include <pylir/Interfaces/CompilerBuiltins.def>
    buildMethodCallOpCompilerBuiltin(m_builder);
}
