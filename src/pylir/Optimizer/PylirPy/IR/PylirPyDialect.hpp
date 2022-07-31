// Copyright 2022 Markus Böck
//
// Licensed under the Apache License v2.0 with LLVM Exceptions.
// See https://llvm.org/LICENSE.txt for license information.
// SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception

#pragma once

#include <mlir/IR/Dialect.h>

#include "pylir/Optimizer/PylirPy/IR/PylirPyOpsDialect.h.inc"

namespace pylir::Py
{
constexpr llvm::StringLiteral alwaysBoundAttr = "py.always_bound";
} // namespace pylir::Py
