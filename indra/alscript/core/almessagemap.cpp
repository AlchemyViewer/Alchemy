/**
 * @file almessagemap.cpp
 * @brief An engine's message taken apart again, for another language to say.
 *
 * $LicenseInfo:firstyear=2026&license=viewerlgpl$
 * Alchemy Viewer Source Code
 * Copyright (C) 2026, Rye <rye@alchemyviewer.org>
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation;
 * version 2.1 of the License only.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301  USA
 * $/LicenseInfo$
 */

#include "linden_common.h"

#include "almessagemap.h"

#include <array>
#include <cstring>

namespace
{
    // The tables, from the engines' own sources: Tailslide's logger.cc by
    // code, Luau's Linter.cpp by lint name, Luau's Error.cpp by shape.
    // The same English is in strings.xml under the same keys, for a
    // translator to work from; scripts/content_tools/check_script_strings.py
    // holds the three to the sources and to each other, and is what to
    // run when either engine is upgraded.
    struct LSLRow
    {
        int         code;
        const char* key;
        const char* text;
    };
    const LSLRow LSL_ROWS[] = {
        { 10001, "LSLDuplicateDeclaration", "Duplicate declaration of `[1]'; previously declared at ([2], [3])." },
        { 10002, "LSLInvalidOperator", "Invalid operator: [1] [2] [3]." },
        { 10005, "LSLWrongType", "Attempting to use `[1]' as a [2], but it is a [3]." },
        { 10006, "LSLUndeclared", "`[1]' is undeclared." },
        { 10007, "LSLUndeclaredWithSuggestion", "`[1]' is undeclared; did you mean [2]?" },
        { 10008, "LSLInvalidMember", "Invalid member: `[1].[2]'." },
        { 10009, "LSLMemberNotVariable", "Trying to access `[1].[2]', but `[1]' is a [3]" },
        { 10010, "LSLMemberWrongType", "Attempting to access `[1].[2]', but `[1]' is not a vector or rotation." },
        { 10011, "LSLArgumentWrongType", "Passing [1] as argument [2] of `[3]' which is declared as `[4] [5]'." },
        { 10012, "LSLTooManyArguments", "Too many arguments to function `[1]'." },
        { 10013, "LSLTooFewArguments", "Too few arguments to function `[1]'." },
        { 10014, "LSLChangeStateInFunction", "Functions cannot change state." },
        { 10015, "LSLWrongTypeInAssignment", "`[1] [2]' assigned a [3] value." },
        { 10016, "LSLWrongTypeInMemberAssignment", "[1] member assigned [2] value (must be float or integer)." },
        { 10017, "LSLReturnValueInEventHandler", "Event handlers cannot return a value." },
        { 10018, "LSLBadReturnType", "Returning a [1] value from a [2] function." },
        { 10019, "LSLNotAllPathsReturn", "Not all code paths return a value." },
        { 10021, "LSLGlobalInitializerNotConstant", "Global initializer must be constant." },
        { 10023, "LSLNoEventHandlers", "State must have at least one event handler." },
        { 10024, "LSLParserStackDepth", "Parser stack depth exceeded; SL will throw a syntax error here." },
        { 10025, "LSLBuiltinLvalue", "`[1]' is a constant and cannot be used as an lvalue." },
        { 10026, "LSLShadowConstant", "`[1]' is a constant and cannot be used in a variable declaration." },
        { 10027, "LSLArgumentWrongTypeEvent", "Declaring `[1]' as parameter [2] of `[3]' which should be `[4] [5]'." },
        { 10028, "LSLTooManyArgumentsEvent", "Too many parameters for event `[1]'." },
        { 10029, "LSLTooFewArgumentsEvent", "Too few parameters for event `[1]'." },
        { 10030, "LSLInvalidEvent", "`[1]' is not a valid event name." },
        { 10031, "LSLEventAsIdentifier", "`[1]' is an event name, and cannot be used as an identifier." },
        { 10032, "LSLDeclarationInvalidHere", "`[1]' may not be declared here, create a new scope with { }." },
        { 10033, "LSLMultipleEventHandlers", "Multiple handlers for event `[1]'" },
        { 10034, "LSLListInList", "Lists may not contain other lists" },
        { 10035, "LSLIllegalCast", "May not cast [1] to [2]" },
        { 10036, "LSLNullInList", "Lists may not contain nulls" },
        { 10037, "LSLStackHeapCollision", "Stack-heap collision" },
        { 10038, "LSLVoidInCondition", "Void expression used as condition" },
        { 20001, "LSLShadowDeclaration", "Declaration of `[1]' in this scope shadows previous declaration at ([2], [3])" },
        { 20002, "LSLAssignmentInComparison", "Suggest parentheses around assignment used as truth value." },
        { 20003, "LSLChangeToCurrentState", "Changing state to current state acts the same as return, use return instead." },
        { 20004, "LSLChangeStateHackCorrupt", "Changing state in a list or string function will corrupt the stack" },
        { 20005, "LSLChangeStateHack", "Using an if statement to change state in a function is a hack and may have unintended side-effects." },
        { 20007, "LSLEmptyIf", "Empty if statement." },
        { 20009, "LSLDeclaredButNotUsed", "[1] `[2]' declared but never used." },
        { 20010, "LSLUnusedEventParameter", "Unused event parameter `[1]'." },
        { 20011, "LSLListCompare", "Using == on lists only compares lengths." },
        { 20012, "LSLConditionAlwaysTrue", "Condition is always true." },
        { 20013, "LSLConditionAlwaysFalse", "Condition is always false." },
        { 20014, "LSLEmptyLoop", "Empty loop body." },
        { 20015, "LSLIntFloatMulAssign", "`i_val *= f_val' can have unpredictable runtime behavior, prefer `i_val = (integer)(i_val * f_val)'" },
        { 20016, "LSLJumpToWrongLabel", "`jump [1];' may jump to the wrong label due to label name clashes within function" },
        { 20017, "LSLDuplicateLabelName", "label `@[1]' is declared multiple times in the same function, which may cause undesired behavior" },
        { 20018, "LSLEqAsStatement", "== comparison used as a statement" },
        { 20019, "LSLDeprecated", "`[1]' is deprecated." },
        { 20020, "LSLDeprecatedWithReplacement", "`[1]' is deprecated, use [2] instead." },
    };

    struct LintRow
    {
        const char* name;
        const char* key;
        const char* text;
    };
    const LintRow LINT_ROWS[] = {
        { "UnknownGlobal", "LuauLintUnknownGlobalAssign", "Unknown global '[1]'; consider assigning to it first" },
        { "DeprecatedGlobal", "LuauLintDeprecatedGlobal", "Global '[1]' is deprecated, use '[2]' instead" },
        { "DeprecatedGlobal", "LuauLintDeprecatedGlobalPlain", "Global '[1]' is deprecated" },
        { "GlobalUsedAsLocal", "LuauLintGlobalUsedAsLocalFunction", "Global '[1]' is only used in the enclosing function '[2]'; consider changing it to local" },
        { "GlobalUsedAsLocal", "LuauLintGlobalUsedAsLocalLine", "Global '[1]' is only used in the enclosing function defined at line [2]; consider changing it to local" },
        { "GlobalUsedAsLocal", "LuauLintGlobalNeverRead", "Global '[1]' is never read before being written. Consider changing it to local" },
        { "LocalShadow", "LuauLintLocalShadow", "Variable '[1]' shadows previous declaration at line [2]" },
        { "LocalShadow", "LuauLintLocalShadowGlobal", "Variable '[1]' shadows a global variable used at line [2]" },
        { "LocalShadow", "LuauLintLocalShadowGlobalPlain", "Variable '[1]' shadows a global variable" },
        { "SameLineStatement", "LuauLintSameLineStatement", "A new statement is on the same line; add semi-colon on previous statement to silence" },
        { "MultiLineStatement", "LuauLintMultiLineStatement", "Statement spans multiple lines; use indentation to silence" },
        { "LocalUnused", "LuauLintLocalUnused", "Variable '[1]' is never used; prefix with '_' to silence" },
        { "FunctionUnused", "LuauLintFunctionUnused", "Function '[1]' is never used; prefix with '_' to silence" },
        { "ImportUnused", "LuauLintImportUnused", "Import '[1]' is never used; prefix with '_' to silence" },
        { "BuiltinGlobalWrite", "LuauLintBuiltinGlobalWrite", "Built-in global '[1]' is overwritten here; consider using a local or changing the name" },
        { "PlaceholderRead", "LuauLintPlaceholderRead", "Placeholder value '_' is read here; consider using a named variable" },
        { "UnreachableCode", "LuauLintUnreachableCode", "Unreachable code (previous statement always [1]s)" },
        { "UnknownType", "LuauLintUnknownType", "Unknown type '[1]'" },
        { "UnknownType", "LuauLintUnknownTypeExpected", "Unknown type '[1]' (expected [2])" },
        { "ForRange", "LuauLintForRangeEnd", "For loop ends at [1] instead of [2]; did you forget to specify step?" },
        { "ForRange", "LuauLintForRangeBackwards", "For loop should iterate backwards; did you forget to specify -1 as step? Also consider changing 0 to 1 since arrays start at 1" },
        { "ForRange", "LuauLintForRangeBackwardsPlain", "For loop should iterate backwards; did you forget to specify -1 as step?" },
        { "ForRange", "LuauLintForRangeZero", "For loop starts at 0, but arrays start at 1" },
        { "UnbalancedAssignment", "LuauLintUnbalancedAssignmentNil", "Assigning [1] values to [2] variables initializes extra variables with nil; add 'nil' to value list to silence" },
        { "UnbalancedAssignment", "LuauLintUnbalancedAssignmentUnused", "Assigning [1] values to [2] variables leaves some values unused" },
        { "ImplicitReturn", "LuauLintImplicitReturnNamed", "Function '[1]' can implicitly return no values even though there's an explicit return at line [2]; add explicit return to silence" },
        { "ImplicitReturn", "LuauLintImplicitReturn", "Function can implicitly return no values even though there's an explicit return at line [1]; add explicit return to silence" },
        { "DuplicateLocal", "LuauLintDuplicateLocalLine", "Variable '[1]' already defined on line [2]" },
        { "DuplicateLocal", "LuauLintDuplicateLocalColumn", "Variable '[1]' already defined on column [2]" },
        { "DuplicateLocal", "LuauLintDuplicateParameterLine", "Function parameter '[1]' already defined on line [2]" },
        { "DuplicateLocal", "LuauLintDuplicateParameterColumn", "Function parameter '[1]' already defined on column [2]" },
        { "DuplicateLocal", "LuauLintDuplicateSelf", "Function parameter 'self' already defined implicitly" },
        { "UninitializedLocal", "LuauLintUninitializedLocal", "Variable '[1]' defined at line [2] is never initialized or assigned; initialize with 'nil' to silence" },
        { "DuplicateFunction", "LuauLintDuplicateFunction", "Duplicate function definition: '[1]' also defined on line [2]" },
        { "DuplicateCondition", "LuauLintDuplicateConditionLine", "Condition has already been checked on line [1]" },
        { "DuplicateCondition", "LuauLintDuplicateConditionColumn", "Condition has already been checked on column [1]" },
        { "MisleadingAndOr", "LuauLintMisleadingAndOr", "The and-or expression always evaluates to the second alternative because the first alternative is [1]; consider using if-then-else expression instead" },
        { "IntegerParsing", "LuauLintIntegerParsingDecimal", "Number literal exceeded available precision and was truncated to closest representable number" },
        { "IntegerParsing", "LuauLintIntegerParsingHex", "Hexadecimal number literal exceeded available precision and was truncated to 2^64" },
        { "IntegerParsing", "LuauLintIntegerParsingBinary", "Binary number literal exceeded available precision and was truncated to 2^64" },
        { "IntegerParsing", "LuauLintIntegerParsingClamped", "Integer number literal was clamped because it was out of range" },
        { "FormatString", "LuauLintFormatString", "Invalid format string: [1]" },
        { "FormatString", "LuauLintPackFormat", "Invalid pack format: [1]" },
        { "FormatString", "LuauLintMatchPattern", "Invalid match pattern: [1]" },
        { "FormatString", "LuauLintMatchReplacement", "Invalid match replacement: [1]" },
        { "FormatString", "LuauLintDateFormat", "Invalid date format: [1]" },
        { "TableLiteral", "LuauLintTableFieldDuplicate", "Table field '[1]' is a duplicate; previously defined at line [2]" },
        { "TableLiteral", "LuauLintTableIndexDuplicateList", "Table index [1] is a duplicate; previously defined as a list entry" },
        { "TableLiteral", "LuauLintTableIndexDuplicate", "Table index [1] is a duplicate; previously defined at line [2]" },
        { "TableLiteral", "LuauLintTableTypeFieldRead", "Table type field '[1]' already has a read type defined at line [2]" },
        { "TableLiteral", "LuauLintTableTypeFieldWrite", "Table type field '[1]' already has a write type defined at line [2]" },
        { "TableLiteral", "LuauLintTableTypeFieldDuplicate", "Table type field '[1]' is a duplicate; previously defined at line [2]" },
        { "TableLiteral", "LuauLintTableTypeFieldReadWrite", "Table type field '[1]' is already read-write; previously defined at line [2]" },
        { "TableOperations", "LuauLintTableInsertZero", "table.insert uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableInsertAppend", "table.insert will append the value to the table; consider removing the second argument for efficiency" },
        { "TableOperations", "LuauLintTableInsertBeforeLast", "table.insert will insert the value before the last element, which is likely a bug; consider removing the second argument or wrap it in parentheses to silence" },
        { "TableOperations", "LuauLintTableInsertMultiple", "table.insert may change behavior if the call returns more than one result; consider adding parentheses around second argument" },
        { "TableOperations", "LuauLintTableRemoveZero", "table.remove uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableRemoveBeforeLast", "table.remove will remove the value before the last element, which is likely a bug; consider removing the second argument or wrap it in parentheses to silence" },
        { "TableOperations", "LuauLintTableMoveZero", "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableCreateLiteral", "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead" },
        { "TableOperations", "LuauLintTableStringKeys", "Using '[1]' on a table with string keys is likely a bug" },
        { "TableOperations", "LuauLintTableNoArrayPart", "Using '[1]' on a table without an array part is likely a bug" },
        { "ComparisonPrecedence", "LuauLintNotPrecedence", "not X [1] Y is equivalent to (not X) [2] Y; add parentheses to silence" },
        { "ComparisonPrecedence", "LuauLintNotPrecedenceEquality", "not X [1] Y is equivalent to (not X) [2] Y; consider using X [3] Y, or add parentheses to silence" },
        { "ComparisonPrecedence", "LuauLintComparisonChain", "X [1] Y [2] Z is equivalent to (X [3] Y) [4] Z; did you mean X [5] Y and Y [6] Z?" },
        { "ComparisonPrecedence", "LuauLintComparisonPrecedence", "X [1] Y [2] Z is equivalent to (X [3] Y) [4] Z; add parentheses to silence" },
        { "CommentDirective", "LuauLintDirectiveUnknown", "Unknown comment directive '[1]'" },
        { "CommentDirective", "LuauLintDirectiveUnknownDidYouMean", "Unknown comment directive '[1]'; did you mean '[2]'?" },
        { "CommentDirective", "LuauLintDirectiveNolintUnknown", "nolint directive refers to unknown lint rule '[1]'" },
        { "CommentDirective", "LuauLintDirectiveNolintUnknownDidYouMean", "nolint directive refers to unknown lint rule '[1]'; did you mean '[2]'?" },
        { "CommentDirective", "LuauLintDirectiveLate", "Comment directive is ignored because it is placed after the first non-comment token" },
        { "CommentDirective", "LuauLintDirectiveModeTwice", "Comment directive with the type checking mode has already been used" },
        { "CommentDirective", "LuauLintDirectiveModeExtra", "Comment directive with the type checking mode has extra symbols at the end of the line" },
        { "CommentDirective", "LuauLintDirectiveNativeExtra", "native directive has extra symbols at the end of the line" },
        { "CommentDirective", "LuauLintDirectiveOptimizeLevel", "optimize directive requires an optimization level" },
        { "CommentDirective", "LuauLintDirectiveOptimizeUnknown", "optimize directive uses unknown optimization level '[1]', 0..2 expected" },
        { "RedundantNativeAttribute", "LuauLintRedundantNativeAttribute", "native attribute on a function is redundant in a native module; consider removing it" },
    };

    // A lint whose template has marks that stand for whole clauses --
    // DeprecatedApi's ", use 'x' instead" and ". reason" -- by the shapes
    // its message takes once built, the fuller first.
    const LintRow LINT_SHAPE_ROWS[] = {
        { "DeprecatedApi", "LuauLintDeprecatedMemberUseReason", "Member '[1]' is deprecated, use '[2]' instead. [3]" },
        { "DeprecatedApi", "LuauLintDeprecatedMemberUse", "Member '[1]' is deprecated, use '[2]' instead" },
        { "DeprecatedApi", "LuauLintDeprecatedMemberReason", "Member '[1]' is deprecated. [2]" },
        { "DeprecatedApi", "LuauLintDeprecatedMember", "Member '[1]' is deprecated" },
        { "DeprecatedApi", "LuauLintDeprecatedFunctionUseReason", "Function '[1]' is deprecated, use '[2]' instead. [3]" },
        { "DeprecatedApi", "LuauLintDeprecatedFunctionUse", "Function '[1]' is deprecated, use '[2]' instead" },
        { "DeprecatedApi", "LuauLintDeprecatedFunctionReason", "Function '[1]' is deprecated. [2]" },
        { "DeprecatedApi", "LuauLintDeprecatedFunction", "Function '[1]' is deprecated" },
    };

    struct ErrorRow
    {
        const char* key;
        const char* text;
    };
    // The type errors by their shape, from Error.cpp, which builds them
    // rather than formats them: the fuller shape before the one it
    // begins with, since a mark at the end takes whatever is left.
    const ErrorRow ERROR_ROWS[] = {
        // The parser's, where it knows what was meant: an LSL habit -- `!=`,
        // `&&`, `||`, `!` -- and SLua's way of saying it.
        { "LuauUnexpectedDidYouMean", "Unexpected '[1]'; did you mean '[2]'?" },
        { "LuauUnknownGlobalAssign", "Unknown global '[1]'; consider assigning to it first" },
        { "LuauUnknownType", "Unknown type '[1]'" },
        { "LuauTypeMismatchUnreachableReason", "Expected this to be unreachable, but got [1]; [2]" },
        { "LuauTypeMismatchUnreachable", "Expected this to be unreachable, but got [1]" },
        { "LuauTypeMismatchExactlyReason", "Expected this to be exactly [1], but got [2]; [3]" },
        { "LuauTypeMismatchExactly", "Expected this to be exactly [1], but got [2]" },
        { "LuauTypeMismatchReason", "Expected this to be [1], but got [2]; [3]" },
        { "LuauTypeMismatch", "Expected this to be [1], but got [2]" },
        { "LuauArgumentCountRangeOnlyOne", "Argument count mismatch. Function '[1]' expects [2] to [3] arguments, but only 1 is specified" },
        { "LuauArgumentCountRangeOnly", "Argument count mismatch. Function '[1]' expects [2] to [3] arguments, but only [4] are specified" },
        { "LuauArgumentCountRangeNone", "Argument count mismatch. Function '[1]' expects [2] to [3] arguments, but none are specified" },
        { "LuauArgumentCountRange", "Argument count mismatch. Function '[1]' expects [2] to [3] arguments, but [4] are specified" },
        { "LuauArgumentCountAtLeastOnlyOne", "Argument count mismatch. Function '[1]' expects at least [2] arguments, but only 1 is specified" },
        { "LuauArgumentCountAtLeastOnly", "Argument count mismatch. Function '[1]' expects at least [2] arguments, but only [3] are specified" },
        { "LuauArgumentCountAtLeastNone", "Argument count mismatch. Function '[1]' expects at least [2] arguments, but none are specified" },
        { "LuauArgumentCountAtLeastOneNone", "Argument count mismatch. Function '[1]' expects at least 1 argument, but none are specified" },
        { "LuauArgumentCountAtLeast", "Argument count mismatch. Function '[1]' expects at least [2] arguments, but [3] are specified" },
        { "LuauArgumentCountOneNone", "Argument count mismatch. Function '[1]' expects 1 argument, but none are specified" },
        { "LuauArgumentCountOne", "Argument count mismatch. Function '[1]' expects 1 argument, but [2] are specified" },
        { "LuauArgumentCountOnlyOne", "Argument count mismatch. Function '[1]' expects [2] arguments, but only 1 is specified" },
        { "LuauArgumentCountOnly", "Argument count mismatch. Function '[1]' expects [2] arguments, but only [3] are specified" },
        { "LuauArgumentCountNone", "Argument count mismatch. Function '[1]' expects [2] arguments, but none are specified" },
        { "LuauArgumentCountOneGiven", "Argument count mismatch. Function '[1]' expects [2] arguments, but 1 is specified" },
        { "LuauArgumentCount", "Argument count mismatch. Function '[1]' expects [2] arguments, but [3] are specified" },
        { "LuauReturnCountOne", "Expected to return 1 value, but [1] are returned here" },
        { "LuauReturnCountOneGiven", "Expected to return [1] values, but 1 is returned here" },
        { "LuauReturnCount", "Expected to return [1] values, but [2] are returned here" },
        { "LuauFunctionResultOne", "Function only returns 1 value, but [1] are required here" },
        { "LuauFunctionResult", "Function only returns [1] values, but [2] are required here" },
        { "LuauCannotCallUnion", "Cannot call a value of type [1] in union:\n  [2]" },
        { "LuauCannotCall", "Cannot call a value of type [1]" },
        { "LuauCouldBeNil", "Value of type '[1]' could be nil" },
        { "LuauUnknownRequireUnsupported", "Unknown require: unsupported path" },
        { "LuauUnknownRequire", "Unknown require: [1]" },
        { "LuauNotTakeSelf", "This function does not take self. Did you mean to use a dot instead of a colon?" },
        { "LuauRequiresSelf", "This function must be called with self. Did you mean to use a colon instead of a dot?" },
        // A key not found with nothing like it the service says in its
        // own words (LuauKeyNotFound) before the map is asked.
        { "LuauMissingPropertyDidYouMeanOneOf", "Key '[1]' not found in table '[2]'.  Did you mean one of [3]?" },
        { "LuauMissingPropertyDidYouMean", "Key '[1]' not found in table '[2]'.  Did you mean '[3]'?" },
        { "LuauMissingExternPropertyDidYouMeanOneOf", "Key '[1]' not found in external type '[2]'.  Did you mean one of [3]?" },
        { "LuauMissingExternPropertyDidYouMean", "Key '[1]' not found in external type '[2]'.  Did you mean '[3]'?" },
        { "LuauNoSuchKey", "Type '[1]' does not have key '[2]'" },
        { "LuauNotATable", "Expected type table, got '[1]' instead" },
        { "LuauCannotAddProperty", "Cannot add property '[1]' to table '[2]'" },
        { "LuauCannotCompare", "Cannot compare unrelated types '[1]' and '[2]' with '[3]'" },
        { "LuauRedefinedTypeAt", "Redefinition of type '[1]', previously defined at line [2]" },
        { "LuauRedefinedType", "Redefinition of type '[1]'" },
        // The new solver's nonstrict mode, which says only what is sure to
        // fail as the script runs.
        { "LuauCheckedCall", "the function '[1]' expects to get a [2] as its [3] argument, but is being given a [4]" },
        { "LuauFailsAtRuntimeIn", "in the function '[1]', 'the argument '[2]' is used in a way that will error at runtime" },
        { "LuauFailsAtRuntime", "the argument '[1]' is used in a way that will error at runtime" },
    };

    // Why a SLua require found nothing: Luau's navigator's words
    // (Require/src/RequireNavigator.cpp), then the studio's own
    // (ALRequireNavigation). Each key's text in a skin is the whole
    // problem, the module's name [1]; here the reason alone, its words from
    // [2]. The fuller shape before the one it begins with.
    const ErrorRow REQUIRE_ROWS[] = {
        { "PreprocRequireNoChildAmbiguous", "could not resolve child component \"[2]\" (ambiguous)" },
        { "PreprocRequireNoChild", "could not resolve child component \"[2]\"" },
        { "PreprocRequireNotAlias", "@[2] is not a valid alias" },
        { "PreprocRequireAliasCycle", "detected alias cycle ([2])" },
        { "PreprocRequireAliasUnresolved", "could not resolve alias \"[2]\"" },
        { "PreprocRequireNoParentOf", "could not get parent of component \"[2]\"" },
        { "PreprocRequireNoParent", "could not get parent of requiring context" },
        { "PreprocRequireBadPrefix", "require path must start with a valid prefix: ./, ../, or @" },
        { "PreprocRequireReserved", "the alias '@[2]' is reserved: aliases starting @sl- are Second Life's" },
        { "PreprocRequireClimbs", "a require through an alias may not climb out of its folder: '[2]'" },
        { "PreprocRequireAliasTarget", "the alias stands for '[2]', which is not there" },
        { "PreprocRequireNoModule", "could not find a module at '[2]'" },
    };

    // A template cut at its marks: the literal stretches, and the number
    // of the mark after each but the last.
    struct Cut
    {
        std::vector<std::string_view> literals;
        std::vector<int>              marks;
    };

    Cut cut(std::string_view text)
    {
        // A bracket that is no mark -- `[a]`, `[10]` -- stays in the
        // literal it is in, which goes on to the next mark or the end.
        Cut    out;
        size_t at   = 0;
        size_t look = 0;
        while (true)
        {
            const size_t open = text.find('[', look);
            if (open == std::string_view::npos)
            {
                out.literals.push_back(text.substr(at));
                break;
            }
            if (open + 2 < text.size() && text[open + 1] >= '1' && text[open + 1] <= '9' && text[open + 2] == ']')
            {
                out.literals.push_back(text.substr(at, open - at));
                out.marks.push_back(text[open + 1] - '0');
                at = look = open + 3;
            }
            else
            {
                look = open + 1;
            }
        }
        return out;
    }

    // Each table's templates cut once, in its rows' order, the first time
    // it is looked through.
    template <class Row, size_t N>
    std::vector<Cut> cutAll(const Row (&rows)[N])
    {
        std::vector<Cut> out;
        out.reserve(N);
        for (const Row& row : rows)
        {
            out.push_back(cut(row.text));
        }
        return out;
    }

    // A message matched to a template already cut, nothing made unless it
    // matches: the words then made, in the marks' order.
    bool matchCut(const Cut& c, std::string_view message, std::vector<std::string>& args)
    {
        // The literal before a mark is found where the message is, and the
        // literal after it is found next -- a mark's word is what lies between.
        size_t                          at = 0;
        std::array<std::string_view, 9> words{};
        std::array<bool, 9>             seen{};
        for (size_t i = 0; i < c.literals.size(); ++i)
        {
            const std::string_view literal = c.literals[i];
            const bool             first   = i == 0;
            const bool             last    = i + 1 == c.literals.size();
            size_t                 found;
            if (first)
            {
                if (message.compare(0, literal.size(), literal) != 0)
                {
                    return false;
                }
                // A template with no marks is its message whole, not the
                // start of a longer one.
                if (last && message.size() != literal.size())
                {
                    return false;
                }
                found = 0;
            }
            else if (last)
            {
                // The last literal ends the message; the word is what is
                // left before it.
                if (message.size() < at + literal.size() || message.compare(message.size() - literal.size(), literal.size(), literal) != 0)
                {
                    return false;
                }
                found = message.size() - literal.size();
            }
            else
            {
                found = literal.empty() ? at : message.find(literal, at);
                if (found == std::string_view::npos)
                {
                    return false;
                }
            }
            if (!first)
            {
                const int              mark = c.marks[i - 1];
                const std::string_view word = message.substr(at, found - at);
                if (seen[mark - 1] && words[mark - 1] != word)
                {
                    return false;
                }
                words[mark - 1] = word;
                seen[mark - 1]  = true;
            }
            at = found + literal.size();
        }
        // The words in the marks' order, as many as the highest mark.
        size_t count = 0;
        for (size_t i = 0; i < 9; ++i)
        {
            if (seen[i])
            {
                count = i + 1;
            }
        }
        args.assign(words.begin(), words.begin() + count);
        return true;
    }
}

// static
bool ALMessageMap::match(std::string_view text, std::string_view message, std::vector<std::string>& args)
{
    return matchCut(cut(text), message, args);
}

// static
bool ALMessageMap::lsl(int code, std::string_view message, Match& out)
{
    static const std::vector<Cut> CUTS = cutAll(LSL_ROWS);
    for (size_t i = 0; i < CUTS.size(); ++i)
    {
        if (LSL_ROWS[i].code == code && matchCut(CUTS[i], message, out.args))
        {
            out.key = LSL_ROWS[i].key;
            return true;
        }
    }
    return false;
}

// static
bool ALMessageMap::luauLint(std::string_view name, std::string_view message, Match& out)
{
    static const std::vector<Cut> CUTS       = cutAll(LINT_ROWS);
    static const std::vector<Cut> SHAPE_CUTS = cutAll(LINT_SHAPE_ROWS);
    for (size_t i = 0; i < CUTS.size(); ++i)
    {
        if (name == LINT_ROWS[i].name && matchCut(CUTS[i], message, out.args))
        {
            out.key = LINT_ROWS[i].key;
            return true;
        }
    }
    for (size_t i = 0; i < SHAPE_CUTS.size(); ++i)
    {
        if (name == LINT_SHAPE_ROWS[i].name && matchCut(SHAPE_CUTS[i], message, out.args))
        {
            out.key = LINT_SHAPE_ROWS[i].key;
            return true;
        }
    }
    return false;
}

// static
bool ALMessageMap::luauRequire(std::string_view message, Match& out)
{
    static const std::vector<Cut> CUTS = cutAll(REQUIRE_ROWS);
    for (size_t i = 0; i < CUTS.size(); ++i)
    {
        if (matchCut(CUTS[i], message, out.args))
        {
            out.key = REQUIRE_ROWS[i].key;
            return true;
        }
    }
    return false;
}

// static
bool ALMessageMap::requireReason(std::string_view key)
{
    return std::any_of(std::begin(REQUIRE_ROWS), std::end(REQUIRE_ROWS), [key](const ErrorRow& row) { return key == row.key; });
}

// static
bool ALMessageMap::luauError(std::string_view message, Match& out)
{
    static const std::vector<Cut> CUTS = cutAll(ERROR_ROWS);
    for (size_t i = 0; i < CUTS.size(); ++i)
    {
        if (matchCut(CUTS[i], message, out.args))
        {
            out.key = ERROR_ROWS[i].key;
            return true;
        }
    }
    return false;
}
