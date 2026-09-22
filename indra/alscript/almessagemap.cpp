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

#include <cstring>

namespace
{
    // The tables, from the engines' own sources: Tailslide's logger.cc by
    // code, Luau's Linter.cpp by lint name, Luau's Error.cpp by shape.
    // The same English is in strings.xml under the same keys, for a
    // translator to work from.
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
        { "UnknownGlobal", "LuauLintUnknownGlobal", "Unknown global '[1]'" },
        { "UnknownGlobal", "LuauLintUnknownGlobalAssign", "Unknown global '[1]'; consider assigning to it first" },
        { "DeprecatedGlobal", "LuauLintDeprecatedGlobal", "Global '[1]' is deprecated, use '[2]' instead" },
        { "GlobalUsedAsLocal", "LuauLintGlobalUsedAsLocalFunction", "Global '[1]' is only used in the enclosing function '[2]'; consider changing it to local" },
        { "GlobalUsedAsLocal", "LuauLintGlobalUsedAsLocalLine", "Global '[1]' is only used in the enclosing function defined at line [2]; consider changing it to local" },
        { "GlobalUsedAsLocal", "LuauLintGlobalNeverRead", "Global '[1]' is never read before being written. Consider changing it to local" },
        { "LocalShadow", "LuauLintLocalShadow", "Variable '[1]' shadows previous declaration at line [2]" },
        { "LocalShadow", "LuauLintLocalShadowGlobal", "Variable '[1]' shadows a global variable used at line [2]" },
        { "SameLineStatement", "LuauLintSameLineStatement", "A new statement is on the same line; add semi-colon on previous statement to silence" },
        { "LocalUnused", "LuauLintLocalUnused", "Variable '[1]' is never used; prefix with '_' to silence" },
        { "FunctionUnused", "LuauLintFunctionUnused", "Function '[1]' is never used; prefix with '_' to silence" },
        { "ImportUnused", "LuauLintImportUnused", "Import '[1]' is never used; prefix with '_' to silence" },
        { "BuiltinGlobalWrite", "LuauLintBuiltinGlobalWrite", "Built-in global '[1]' is overwritten here; consider using a local or changing the name" },
        { "UnreachableCode", "LuauLintUnreachableCode", "Unreachable code (previous statement always [1]s)" },
        { "UnknownType", "LuauLintUnknownType", "Unknown type '[1]'" },
        { "UnknownType", "LuauLintUnknownTypeExpected", "Unknown type '[1]' (expected [2])" },
        { "ForRange", "LuauLintForRangeEnd", "For loop ends at [1] instead of [2]; did you forget to specify step?" },
        { "ForRange", "LuauLintForRangeBackwards", "For loop should iterate backwards; did you forget to specify -1 as step? Also consider changing 0 to 1 since arrays start at 1" },
        { "UnbalancedAssignment", "LuauLintUnbalancedAssignmentNil", "Assigning [1] values to [2] variables initializes extra variables with nil; add 'nil' to value list to silence" },
        { "UnbalancedAssignment", "LuauLintUnbalancedAssignmentUnused", "Assigning [1] values to [2] variables leaves some values unused" },
        { "ImplicitReturn", "LuauLintImplicitReturnNamed", "Function '[1]' can implicitly return no values even though there's an explicit return at line [2]; add explicit return to silence" },
        { "ImplicitReturn", "LuauLintImplicitReturn", "Function can implicitly return no values even though there's an explicit return at line [1]; add explicit return to silence" },
        { "DuplicateLocal", "LuauLintDuplicateLocalLine", "Variable '[1]' already defined on line [2]" },
        { "DuplicateLocal", "LuauLintDuplicateLocalColumn", "Variable '[1]' already defined on column [2]" },
        { "DuplicateLocal", "LuauLintDuplicateParameterLine", "Function parameter '[1]' already defined on line [2]" },
        { "DuplicateLocal", "LuauLintDuplicateParameterColumn", "Function parameter '[1]' already defined on column [2]" },
        { "UninitializedLocal", "LuauLintUninitializedLocal", "Variable '[1]' defined at line [2] is never initialized or assigned; initialize with 'nil' to silence" },
        { "DuplicateFunction", "LuauLintDuplicateFunction", "Duplicate function definition: '[1]' also defined on line [2]" },
        { "DuplicateCondition", "LuauLintDuplicateConditionLine", "Condition has already been checked on line [1]" },
        { "DuplicateCondition", "LuauLintDuplicateConditionColumn", "Condition has already been checked on column [1]" },
        { "MisleadingAndOr", "LuauLintMisleadingAndOr", "The and-or expression always evaluates to the second alternative because the first alternative is [1]; consider using if-then-else expression instead" },
        { "IntegerParsing", "LuauLintIntegerParsingDecimal", "Number literal exceeded available precision and was truncated to closest representable number" },
        { "IntegerParsing", "LuauLintIntegerParsingHex", "Hexadecimal number literal exceeded available precision and was truncated to 2^64" },
        { "IntegerParsing", "LuauLintIntegerParsingBinary", "Binary number literal exceeded available precision and was truncated to 2^64" },
        { "TableOperations", "LuauLintTableInsertZero", "table.insert uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableInsertAppend", "table.insert will append the value to the table; consider removing the second argument for efficiency" },
        { "TableOperations", "LuauLintTableRemoveZero", "table.remove uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableMoveZero", "table.move uses index 0 but arrays are 1-based; did you mean 1 instead?" },
        { "TableOperations", "LuauLintTableCreateLiteral", "table.create with a table literal will reuse the same object for all elements; consider using a for loop instead" },
        { "ComparisonPrecedence", "LuauLintNotPrecedence", "not X [1] Y is equivalent to (not X) [1] Y; add parentheses to silence" },
        { "ComparisonPrecedence", "LuauLintNotPrecedenceEquality", "not X [1] Y is equivalent to (not X) [1] Y; consider using X [2] Y, or add parentheses to silence" },
        { "ComparisonPrecedence", "LuauLintComparisonChain", "X [1] Y [2] Z is equivalent to (X [1] Y) [2] Z; did you mean X [1] Y and Y [2] Z?" },
        { "ComparisonPrecedence", "LuauLintComparisonPrecedence", "X [1] Y [2] Z is equivalent to (X [1] Y) [2] Z; add parentheses to silence" },
        { "RedundantNativeAttribute", "LuauLintRedundantNativeAttribute", "native attribute on a function is redundant in a native module; consider removing it" },
    };

    struct ErrorRow
    {
        const char* key;
        const char* text;
    };
    const ErrorRow ERROR_ROWS[] = {
        { "LuauUnknownGlobal", "Unknown global '[1]'" },
        { "LuauUnknownGlobalAssign", "Unknown global '[1]'; consider assigning to it first" },
        { "LuauUnknownType", "Unknown type '[1]'" },
        { "LuauTypeMismatch", "Type '[1]' could not be converted into '[2]'" },
        { "LuauArgumentCount", "Argument count mismatch. Function expects [1] arguments, but [2] are specified" },
        { "LuauArgumentCountAtLeast", "Argument count mismatch. Function expects at least [1] arguments, but [2] are specified" },
        { "LuauArgumentCountRange", "Argument count mismatch. Function expects [1] to [2] arguments, but [3] are specified" },
        { "LuauCannotCall", "Cannot call a value of type [1]" },
        { "LuauCouldBeNil", "Value of type '[1]' could be nil" },
        { "LuauUnknownRequire", "Unknown require: [1]" },
        { "LuauNotTakeSelf", "This function does not take self. Did you mean to use a dot instead of a colon?" },
        { "LuauMissingProperty", "Key '[1]' not found in [2] '[3]'" },
        { "LuauRedefinedType", "Redefinition of type '[1]'" },
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
        Cut    out;
        size_t at = 0;
        while (at <= text.size())
        {
            const size_t open = text.find('[', at);
            if (open == std::string_view::npos || open + 2 >= text.size() || text[open + 1] < '1' || text[open + 1] > '9' || text[open + 2] != ']')
            {
                if (open != std::string_view::npos && open + 2 < text.size())
                {
                    // A bracket that is no mark: on past it.
                    const size_t next = text.find('[', open + 1);
                    if (next != std::string_view::npos)
                    {
                        // Look again from the next bracket, with this one
                        // in the literal.
                        at = open + 1;
                        continue;
                    }
                }
                out.literals.push_back(text.substr(at));
                break;
            }
            out.literals.push_back(text.substr(at, open - at));
            out.marks.push_back(text[open + 1] - '0');
            at = open + 3;
        }
        return out;
    }
}

// static
bool ALMessageMap::match(std::string_view text, std::string_view message, std::vector<std::string>& args)
{
    // Cut at the marks; the literal before a mark is found where the
    // message is, and the literal after it is found next -- a mark's
    // word is what lies between.
    Cut    c = cut(text);
    size_t at = 0;
    std::vector<std::string> words(9);
    std::vector<bool>        seen(9, false);
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
            const int         mark = c.marks[i - 1];
            const std::string word(message.substr(at, found - at));
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

// static
bool ALMessageMap::lsl(int code, std::string_view message, Match& out)
{
    for (const LSLRow& row : LSL_ROWS)
    {
        if (row.code == code && match(row.text, message, out.args))
        {
            out.key = row.key;
            return true;
        }
    }
    return false;
}

// static
bool ALMessageMap::luauLint(std::string_view name, std::string_view message, Match& out)
{
    for (const LintRow& row : LINT_ROWS)
    {
        if (name == row.name && match(row.text, message, out.args))
        {
            out.key = row.key;
            return true;
        }
    }
    return false;
}

// static
bool ALMessageMap::luauError(std::string_view message, Match& out)
{
    for (const ErrorRow& row : ERROR_ROWS)
    {
        if (match(row.text, message, out.args))
        {
            out.key = row.key;
            return true;
        }
    }
    return false;
}
