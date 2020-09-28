// Copyright (c) 2020 by the Zeek Project. See LICENSE for details.

#pragma once

#include <hilti/ast/operators/common.h>
#include <hilti/ast/types/bool.h>
#include <hilti/ast/types/string.h>

namespace hilti {
namespace operator_ {

STANDARD_OPERATOR_2(bool_, Equal, type::Bool(), type::Bool(), type::Bool(), "Compares two boolean values.")
STANDARD_OPERATOR_2(bool_, Unequal, type::Bool(), type::Bool(), type::Bool(), "Compares two boolean values.")

} // namespace operator_
} // namespace hilti
