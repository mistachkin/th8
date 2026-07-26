/*
 * th8_expr.c -- TH8 expression parser and evaluator.
 *
 * Implements the [expr] command and its supporting infrastructure
 * per the official Tcl 8.6 expr(n) reference:
 *
 *     https://www.tcl-lang.org/man/tcl8.6/TclCmd/expr.htm
 *
 * EXPRESSION GRAMMAR vs. SCRIPT GRAMMAR
 * -------------------------------------
 *
 * Two distinct grammars meet here.  Keeping them separate is the
 * single most important thing to understand about this file:
 *
 *   * The TCL SCRIPT GRAMMAR ("Tcl Dodekalogue") -- the twelve
 *     numbered rules of the Tcl(n) reference at:
 *
 *         https://www.tcl-lang.org/man/tcl8.6/TclCmd/Tcl.htm
 *
 *     This grammar applies to expressions ONLY where they let one
 *     of their operand forms drop back into ordinary Tcl quoting
 *     -- that is, in the SUBSTITUTION OPERATIONS that produce the
 *     value of an operand:
 *
 *       - `$name` / `$name(idx)` / `${name}` (Tcl(n) Rule [8])
 *       - `[command]`                        (Tcl(n) Rule [7])
 *       - `{braced text}`                    (Tcl(n) Rule [6])
 *       - `"quoted text"`                    (Tcl(n) Rule [4])
 *       - `\X` backslash escape              (Tcl(n) Rule [9])
 *
 *     For these forms this file DELEGATES to the script-level
 *     parser (th8NextVarName, th8NextCommand) or to th8SubstWord
 *     and th8SubstAll (the Tcl-rule substitution engines) so the
 *     same rules applied in [set x "..."] are applied in
 *     [expr {... "..." ...}].  See the "Tcl-syntax delegation"
 *     blocks marked throughout the code below.
 *
 *   * The EXPR (math/logical) GRAMMAR -- the operand and operator
 *     forms of expr(n).  Everything ELSE in this file -- the
 *     operator-precedence table, unary-vs-binary +/- dispatch,
 *     numeric literal recognition (decimal, hex 0x, octal 0o,
 *     binary 0b), function-call form `name(...)`, type coercion
 *     between int / wide-int / double / bigint / string, and the
 *     lazy-evaluation rules for `&&`, `||`, `?:` -- belongs to
 *     this grammar.  These are described entirely by expr(n) and
 *     are NOT reachable from ordinary Tcl quoting.
 *
 * The two grammars are NESTED, not interleaved: the script parser
 * delivers a single argument string to [expr]; this file parses
 * that string under expr(n) rules; whenever those rules say
 * "perform standard Tcl substitution on this operand" we fall
 * back through the th8Subst* helpers to honor Tcl(n) verbatim.
 *
 * ARCHITECTURE -- THREE PHASES
 *
 *   1. TOKENIZE  (th8ExprParse)    Phase 1, expr-grammar driven.
 *   2. TREE      (th8ExprMakeTree) Phase 2, expr-grammar driven.
 *   3. EVALUATE  (th8ExprEval)     Phase 3, mostly expr-grammar
 *                                   driven; falls into Tcl-grammar
 *                                   substitution at literal nodes.
 *
 * Each function's block comment cites the expr(n) wording it
 * implements and, where applicable, the Tcl(n) rule it defers to.
 *
 * MATH FUNCTIONS
 *
 * expr(n) "Math Functions": "When the expression parser encounters
 * a mathematical function such as sin($x), it replaces it with a
 * call to an ordinary Tcl command in the tcl::mathfunc namespace."
 * TH8 implements the same dispatch via the per-interpreter math
 * function registry (Th8_CreateMathFunc / Th8_FindMathFunc) plus
 * the platform's xMathFunc callback for libm-backed transcendentals.
 *
 * SHORT-CIRCUIT EVALUATION
 *
 * Per expr(n): "The &&, ||, and ?: operators have 'lazy evaluation',
 * just as in C, which means that operands are not evaluated if
 * they are not needed to determine the outcome."  Implemented in
 * th8ExprEval -- see the explicit short-circuit branches there.
 *
 * SECURITY
 *
 * Overflow checks on integer arithmetic, division-by-zero detection,
 * recursion bounded by nEvalDepth (Th8_Ready check at every parse
 * iteration and tree-build entry), bareword rejection so that
 * untrusted expressions cannot accidentally invoke unrelated Tcl
 * commands.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#include "th8_plat.h"
#include "th8.h"
#include "th8_int.h"
#include "th8_int_core.h"
#include "th8_expr.h"

#if defined(TH8_ENABLE_BIGINT)
#  include "th8_bigint.h"
#endif

#if defined(TH8_ENABLE_EXPRESSIONS)


/*
 *----------------------------------------------------------------------
 *
 * Math function registration --
 *
 *	Per-interpreter registry of expression math functions.
 *	Built-in functions (abs, int, double, sin, etc.) are
 *	registered from a static table during Th8_RegisterLanguage.
 *	Users can add, replace, or remove math functions.
 *
 *----------------------------------------------------------------------
 */


/*
 *----------------------------------------------------------------------
 *
 * Math function registration --
 *
 *	Per-interpreter registry of expression math functions.
 *	Uses Th8_GetMathFuncHash() accessor to avoid internal
 *	struct access.
 *
 *----------------------------------------------------------------------
 */

/* Th8_MathFuncEntry -- declared in th8_expr.h. */


/*
 *----------------------------------------------------------------------
 *
 * Th8_CreateMathFunc --
 *
 *	Register or replace a math function in the per-interpreter
 *	math function hash table.  The function becomes available for
 *	use in [expr] expressions (e.g., abs(x), pow(x,y)).
 *
 * Why / How:
 *	Expression evaluation needs a dispatch table for named math
 *	functions.  This function lazily creates the hash table on
 *	first use and stores a heap-allocated entry containing the
 *	callback, context pointer, and expected argument count.  If
 *	a function with the same name already exists, its entry is
 *	freed before being replaced, preventing memory leaks.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on allocation failure.
 *
 * Side effects:
 *	Allocates or replaces a hash entry in interp->paMathFunc.
 *	May create the hash table itself on first call.
 *
 *----------------------------------------------------------------------
 */

int
Th8_CreateMathFunc(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    int nArg,
    Th8_MathFuncProc xProc,
    void *pCtx)
{
    Th8_MathFuncEntry *pEntry;
    Th8_HashEntry *pHash;

    if (!interp) return TH8_ERROR;
    if (!interp->paMathFunc) {
	interp->paMathFunc = Th8_HashNew(interp);
	if (!interp->paMathFunc) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    return TH8_ERROR;
	}
    }

    pEntry = (Th8_MathFuncEntry *)
        TH8_ALLOC(interp, sizeof(Th8_MathFuncEntry));
    if (!pEntry) {
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    pEntry->xProc = xProc;
    pEntry->pCtx = pCtx;
    pEntry->nArg = nArg;

    pHash = Th8_HashFind(interp, interp->paMathFunc, zName, nName, 1);
    if (!pHash) {
	Th8_Free(interp, pEntry);
	Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	return TH8_ERROR;
    }
    if (pHash->pData) {
	Th8_Free(interp, pHash->pData);
    }
    pHash->pData = pEntry;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_DeleteMathFunc --
 *
 *	Remove a previously registered math function from the
 *	per-interpreter registry.
 *
 * Why / How:
 *	Allows callers to unregister math functions that are no longer
 *	needed or to prevent access to specific operations.  The entry's
 *	heap-allocated data is freed before removing the hash entry to
 *	avoid memory leaks.  Returns TH8_ERROR if no registry exists
 *	or the named function is not found.
 *
 * Results:
 *	TH8_OK if the function was found and removed; TH8_ERROR if
 *	the hash table does not exist or the function is not registered.
 *
 * Side effects:
 *	Frees the Th8_MathFuncEntry and removes the hash entry.
 *
 *----------------------------------------------------------------------
 */

int
Th8_DeleteMathFunc(Th8_Interp *interp, const char *zName, size_t nName)
{
    Th8_HashEntry *pHash;

    if (!interp) return TH8_ERROR;
    if (!interp->paMathFunc) {
	return TH8_ERROR;
    }
    pHash = Th8_HashFind(interp, interp->paMathFunc, zName, nName, 0);
    if (!pHash) return TH8_ERROR;
    Th8_Free(interp, pHash->pData);
    Th8_HashRemove(interp, interp->paMathFunc, zName, nName);
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_FindMathFunc --
 *
 *	Look up a registered math function by name and return its
 *	callback, context pointer, and expected argument count.
 *
 * Why / How:
 *	Called by th8ExprEvalFunc during expression evaluation to
 *	resolve named function calls (e.g., sin(x), pow(x,y)).
 *	Performs a hash lookup in interp->paMathFunc.  Each output
 *	pointer is optional (NULL-safe) so callers can query only
 *	the fields they need.
 *
 * Results:
 *	TH8_OK if the function is found; TH8_ERROR if the registry
 *	does not exist or the name is not registered.
 *
 * Side effects:
 *	None.
 *
 *----------------------------------------------------------------------
 */

int
Th8_FindMathFunc(
    Th8_Interp *interp,
    const char *zName,
    size_t nName,
    int *pnArg,
    Th8_MathFuncProc *pxProc,
    void **ppCtx)
{
    Th8_HashEntry *pHash;
    Th8_MathFuncEntry *pEntry;

    if (!interp) return TH8_ERROR;
    if (!interp->paMathFunc) return TH8_ERROR;
    pHash = Th8_HashFind(interp, interp->paMathFunc, zName, nName, 0);
    /* Bug 26: tombstone-safe plain `if`. */
    if (!pHash || !pHash->pData) return TH8_ERROR;

    pEntry = (Th8_MathFuncEntry *)pHash->pData;
    if (pnArg) *pnArg = pEntry->nArg;
    if (pxProc) *pxProc = pEntry->xProc;
    if (ppCtx) *ppCtx = pEntry->pCtx;
    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * th8MathOp --
 *
 *	Public wrapper for the platform's xMathFunc callback.
 *	Allows commands to invoke math functions without needing
 *	direct access to the interpreter's platform struct.
 *
 * Why / How:
 *	Transcendental math operations (sin, cos, sqrt, pow, etc.)
 *	require platform-supplied libm or equivalent.  Rather than
 *	coupling the expression evaluator directly to the platform
 *	struct, this wrapper provides an indirection point.  It
 *	validates that the platform callback exists before invoking
 *	it, producing a clear error when math is unavailable (e.g.,
 *	on a minimal embedded platform with no libm).
 *
 * Results:
 *	TH8_OK on success with *pResult set; TH8_ERROR if the
 *	platform has no xMathFunc callback installed.
 *
 * Side effects:
 *	May set the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

int
th8MathOp(
    Th8_Interp *interp, /* Interpreter. */
    int op,   /* TH8_MATH_* operation code. */
    double *pResult,  /* OUT: operation result. */
    double a,   /* First operand. */
    double b)   /* Second operand (0.0 if unary). */
{
    /* Bug 26: interp->pPlatform may be NULL during teardown or with
     * a malformed embedder; use plain `if` so the guard survives
     * TH8_OMIT.  Use Th8_SetResultStatic so the error path stays
     * allocation-free in the platform-NULL window. */
    if (!interp->pPlatform || !interp->pPlatform->xMathFunc) {
	Th8_SetResultStatic(
	    interp, "math functions not available", TH8_NOLEN);
	return TH8_ERROR;
    }
    return interp->pPlatform
        ->xMathFunc(interp, interp->pPlatform->pCtx, op, pResult, a, b);
}


/*
 *----------------------------------------------------------------------
 *
 * th8ParseExpr --
 *
 *	Parse an expression string into a parse structure.
 *	The expression is tokenized but not evaluated.
 *
 *	On success, pParse->aWord[0] is a single TH8_TOKEN_SUB_EXPR
 *	value with the full expression text.  The nWord field is 1.
 *	Children (if any) represent sub-expressions, operators, and
 *	function calls.
 *
 *	This is a simplified implementation that produces a single
 *	token for the expression text.  Full recursive expression
 *	tree decomposition is deferred to a future version.
 *
 * Why / How:
 *	The command parser needs to recognize expression tokens inside
 *	[expr] and similar contexts.  This function bridges the gap
 *	by wrapping the expression text as a single parse token after
 *	validating syntax via Th8_Expr in parse-only mode (NULL result
 *	pointer).  This ensures syntax errors are caught at parse time
 *	rather than deferred to evaluation.  The TH8_NOLEN sentinel
 *	is handled by scanning for the NUL terminator.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on syntax error.
 *
 * Side effects:
 *	Allocates pParse->aWord.  Caller must call th8FreeParse().
 *
 *----------------------------------------------------------------------
 */

int
th8ParseExpr(
    Th8_Interp *interp, /* Interpreter (for error messages). */
    const char *zExpr,  /* Expression text. */
    size_t nExpr,  /* Byte length (TH8_NOLEN = NUL). */
    Th8_Parse *pParse)  /* OUT: filled on success. */
{
    Th8_Value *aWord;

    if (!pParse) return TH8_ERROR;
    if (nExpr == TH8_NOLEN) {
	size_t k = 0;

	while (zExpr[k])
	    k++;
	nExpr = k;
    }

    pParse->zCommand = zExpr;
    pParse->nCommand = nExpr;
    pParse->nLine = 1;
    pParse->zAfter = zExpr + nExpr;
    pParse->nComment = 0;
    pParse->zComment = 0;

    /*
     * Validate the expression by attempting to evaluate it
     * in "parse-only" mode.  If interp is non-NULL, use
     * Th8_Expr to check syntax; if it fails, the expression
     * is invalid.
     */

    if (interp) {
	int rc = Th8_Expr(interp, zExpr, nExpr, NULL, 0);

	if (rc != TH8_OK) {
	    pParse->nWord = 0;
	    pParse->aWord = 0;
	    return TH8_ERROR;
	}
    }

    /*
     * Create a single TH8_TOKEN_SUB_EXPR covering the full
     * expression.
     */

    aWord = (Th8_Value *)TH8_ALLOC(interp, sizeof(Th8_Value));
    if (!aWord) {
	pParse->nWord = 0;
	pParse->aWord = 0;
	return TH8_ERROR;
    }

    aWord[0].eType = TH8_TOKEN_SUB_EXPR;
    aWord[0].zData = zExpr;
    aWord[0].nData = nExpr;
    aWord[0].u.token.nLine = 1;
    aWord[0].u.token.nCol = 0;
    aWord[0].u.token.nChild = 0;
    aWord[0].u.token.aChild = 0;

    pParse->nWord = 1;
    pParse->aWord = aWord;
    return TH8_OK;
}


/*
 *======================================================================
 *
 * EXPRESSION GRAMMAR -- operator codes and precedence table.
 *
 * This is the math/logical part of the file; nothing here pertains
 * to ordinary Tcl quoting.  The values, names, and precedence
 * levels mirror the expr(n) "OPERATORS" section verbatim:
 *
 *     https://www.tcl-lang.org/man/tcl8.6/TclCmd/expr.htm
 *
 * Per expr(n), highest precedence is listed first and groups with
 * the same precedence are listed on one line in the man page.  The
 * iPrecedence values below assign 1 = highest (tightest binding),
 * 14 = lowest (loosest), -1 = parenthesis sentinel.  Lower number
 * means tighter binding.
 *
 *======================================================================
 */

/* TH8_OP_* operator codes -- declared in th8_expr.h. */

/*
 * Operator table entry.
 *
 * The th8Operators[] table below is searched linearly during
 * tokenization.  Multi-character operators (**, <<, ==, etc.) must
 * appear BEFORE their single-character prefixes (*, <, =) so the
 * scanner takes the longest match -- this matches expr(n)'s
 * implicit longest-match tokenisation.  Unary and binary versions
 * of +/- both appear; disambiguation is done in th8ExprParse by
 * checking whether the preceding token is a complete term (per
 * expr(n) "Unary minus, unary plus" being applicable only when
 * there is no left operand).
 *
 * iPrecedence: lower number = tighter binding.  -1 for ( and ).
 * eArgType:    determines type coercion during evaluation per
 *              expr(n)'s per-operator type rules ("Valid for any
 *              numeric operands", "Valid for integer operands
 *              only", "Valid for all operand types", etc.).
 */

/* Th8_Operator and TH8_ARG_* -- declared in th8_expr.h. */

/*
 * Operator table.  The right-most column comments name each
 * precedence group by the expr(n) "OPERATORS" section heading
 * verbatim, so this table can be cross-checked against the man
 * page line by line.  expr(n) precedence groups, top to bottom:
 *
 *   prec  expr(n) wording
 *   1     "- + ~ !  Unary minus, unary plus, bit-wise NOT,
 *                    logical NOT."
 *   2     "**       Exponentiation."  (right-associative)
 *   3     "* / %    Multiply, divide, remainder."
 *   4     "+ -      Add and subtract."
 *   5     "<< >>    Left and right shift."
 *   6     "< > <= >=
 *                    Boolean less, greater, less than or equal,
 *                    and greater than or equal."
 *   7     "== !=    Boolean equal and not equal."
 *   8     "eq ne    Boolean string equal and string not equal."
 *         "in ni    List containment and negated list
 *                    containment."   (same precedence as eq/ne)
 *   9     "&        Bit-wise AND."
 *   10    "^        Bit-wise exclusive OR."
 *   11    "|        Bit-wise OR."
 *   12    "&&       Logical AND."    (lazy)
 *   13    "||       Logical OR."     (lazy)
 *   14    "x ? y : z
 *                    If-then-else, as in C."  (lazy)
 *
 * "All of the binary operators but exponentiation group left-to-
 * right within the same precedence level; exponentiation groups
 * right-to-left."  (expr(n) verbatim).  The left/right grouping
 * is enforced by th8ExprMakeTree's directional scans, not by this
 * table.
 */

static Th8_Operator th8Operators[] = {
    /* Parentheses sentinels (precedence -1, no eArgType). */
    {"(", 1, TH8_OP_OPEN_BRACKET, -1, 0, 0},
    {")", 1, TH8_OP_CLOSE_BRACKET, -1, 0, 0},

    /* Precedence 1 -- expr(n): "Unary minus, unary plus, bit-wise
     * NOT, logical NOT.  None of these operators may be applied to
     * string operands, and bit-wise NOT may be applied only to
     * integers." */
    {"-", 1, TH8_OP_UNARY_MINUS, 1, TH8_ARG_NUMBER, 0},
    {"+", 1, TH8_OP_UNARY_PLUS, 1, TH8_ARG_NUMBER, 0},
    {"~", 1, TH8_OP_BITWISE_NOT, 1, TH8_ARG_INTEGER, 0},
    {"!", 1, TH8_OP_LOGICAL_NOT, 1, TH8_ARG_INTEGER, 0},

    /* Multi-char operators (longest-match wins; listed before
     * their single-char prefixes below). */

    /* Precedence 2 -- expr(n): "Exponentiation.  Valid for any
     * numeric operands."  Right-associative; th8ExprMakeTree
     * scans right-to-left for this single level. */
    {"**", 2, TH8_OP_EXPONENT, 2, TH8_ARG_NUMBER, 0},

    /* Precedence 5 -- expr(n): "Left and right shift.  Valid for
     * integer operands only." */
    {"<<", 2, TH8_OP_LEFT_SHIFT, 5, TH8_ARG_INTEGER, 0},
    {">>", 2, TH8_OP_RIGHT_SHIFT, 5, TH8_ARG_INTEGER, 0},

    /* Precedence 6 -- expr(n): "Boolean less, greater, less than
     * or equal, and greater than or equal." */
    {"<=", 2, TH8_OP_LE, 6, TH8_ARG_NUMBER, 0},
    {">=", 2, TH8_OP_GE, 6, TH8_ARG_NUMBER, 0},

    /* Precedence 7 -- expr(n): "Boolean equal and not equal.
     * Valid for all operand types." */
    {"==", 2, TH8_OP_EQ, 7, TH8_ARG_NUMBER, 0},
    {"!=", 2, TH8_OP_NE, 7, TH8_ARG_NUMBER, 0},

    /* Precedence 8 -- expr(n) groups two two-letter operator
     * pairs at this level: "eq ne -- Boolean string equal and
     * string not equal" and "in ni -- List containment and
     * negated list containment." */
    {"eq", 2, TH8_OP_SEQ, 8, TH8_ARG_STRING, 0},
    {"ne", 2, TH8_OP_SNE, 8, TH8_ARG_STRING, 0},
    {"in", 2, TH8_OP_IN, 8, TH8_ARG_STRING, 0},
    {"ni", 2, TH8_OP_NI, 8, TH8_ARG_STRING, 0},

    /* Precedence 12 -- expr(n): "Logical AND.  This operator
     * evaluates lazily; it only evaluates its second operand if
     * it must."  Lazy evaluation is honored in th8ExprEval. */
    {"&&", 2, TH8_OP_LOGICAL_AND, 12, TH8_ARG_NUMBER, 0},

    /* Precedence 13 -- expr(n): "Logical OR.  This operator
     * evaluates lazily; it only evaluates its second operand if
     * it must." */
    {"||", 2, TH8_OP_LOGICAL_OR, 13, TH8_ARG_NUMBER, 0},

    /*
     * Precedence 15 -- TH8 EXTENSION (NOT in expr(n)):
     * `:=` variable assignment.  Right-associative; gated by
     * TH8_EXPR_VAR_ASSIGN so strict-mode interpreters never
     * see this row.  Listed before single-char `:` so the
     * longest-match table scan picks it first when the flag
     * is set.  Evaluator handler in th8ExprEval treats LHS as
     * the variable name (substituted from the operand) and
     * RHS as the value.  Yields the assigned value (C-style)
     * to enable `a := b := 1` chaining.
     */
    {":=", 2, TH8_OP_VAR_ASSIGN, 15, TH8_ARG_STRING, TH8_EXPR_VAR_ASSIGN},

    /* Precedence 3 -- expr(n): "Multiply, divide, remainder.
     * None of these operators may be applied to string operands,
     * and remainder may be applied only to integers." */
    {"*", 1, TH8_OP_MULTIPLY, 3, TH8_ARG_NUMBER, 0},
    {"/", 1, TH8_OP_DIVIDE, 3, TH8_ARG_NUMBER, 0},
    {"%", 1, TH8_OP_MODULUS, 3, TH8_ARG_INTEGER, 0},

    /* Precedence 4 -- expr(n): "Add and subtract.  Valid for any
     * numeric operands." */
    {"+", 1, TH8_OP_ADD, 4, TH8_ARG_NUMBER, 0},
    {"-", 1, TH8_OP_SUBTRACT, 4, TH8_ARG_NUMBER, 0},

    /* Precedence 6 (single-char forms of the relational ops). */
    {"<", 1, TH8_OP_LT, 6, TH8_ARG_NUMBER, 0},
    {">", 1, TH8_OP_GT, 6, TH8_ARG_NUMBER, 0},

    /* Precedence 9 -- expr(n): "Bit-wise AND.  Valid for integer
     * operands only." */
    {"&", 1, TH8_OP_BITWISE_AND, 9, TH8_ARG_INTEGER, 0},

    /* Precedence 10 -- expr(n): "Bit-wise exclusive OR.  Valid
     * for integer operands only." */
    {"^", 1, TH8_OP_BITWISE_XOR, 10, TH8_ARG_INTEGER, 0},

    /* Precedence 11 -- expr(n): "Bit-wise OR.  Valid for integer
     * operands only." */
    {"|", 1, TH8_OP_BITWISE_OR, 11, TH8_ARG_INTEGER, 0},

    /* Precedence 14 -- expr(n): "x ? y : z -- If-then-else, as in
     * C.  This operator evaluates lazily; it evaluates only one
     * of y or z." */
    {"?", 1, TH8_OP_TERNARY_Q, 14, TH8_ARG_NUMBER, 0},
    {":", 1, TH8_OP_TERNARY_C, 14, TH8_ARG_NUMBER, 0},

    {0, 0, 0, 0, 0, 0}};


/*
 *----------------------------------------------------------------------
 *
 * th8ExprEvalFunc -- expression-grammar math function dispatch
 *
 *	Evaluate a math function call via the per-interpreter math
 *	function registry.  All built-in functions (abs, int, sin,
 *	etc.) are registered callbacks in th8_math.c.  If no
 *	registered function matches, returns TH8_ERROR.
 *
 * Why / How (expr-grammar, NOT Tcl-syntax):
 *	expr(n) "MATH FUNCTIONS": "When the expression parser
 *	encounters a mathematical function such as sin($x), it
 *	replaces it with a call to an ordinary Tcl command in the
 *	tcl::mathfunc namespace."  TH8 follows the same pattern but
 *	uses an explicit per-interpreter registry rather than a
 *	namespace; the dispatch is performed here.
 *
 *	The function name and argument strings are recognised by
 *	the expression-grammar tokenizer (th8ExprParse) when it
 *	sees `identifier(...)`.  Each argument has already been
 *	recursively evaluated as a sub-expression via Th8_Expr, so
 *	the argument strings passed in are the final scalar values
 *	-- no further substitution happens at this point.
 *
 *	Security note: this is the only path by which an [expr]
 *	body can reach C code other than the operator handlers, and
 *	it is gated by an explicit registration step.  An untrusted
 *	expression cannot call arbitrary Tcl commands through this
 *	path -- only entries in interp->paMathFunc are reachable.
 *
 * Results:
 *	TH8_OK on success with the interpreter result set to the
 *	function's return value; TH8_ERROR if the function is
 *	unknown.
 *
 * Side effects:
 *	Sets the interpreter result (either the computed value or
 *	an error message).
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprEvalFunc(
    Th8_Interp *interp,
    const char *zFunc,
    size_t nFunc,
    const char *zArg1,
    size_t nArg1,
    const char *zArg2,
    size_t nArg2)
{
    Th8_MathFuncProc xProc = 0;
    void *pCtx = 0;
    int nArg = 0;

    if (TH8_OK ==
        Th8_FindMathFunc(interp, zFunc, nFunc, &nArg, &xProc, &pCtx)) {
	return xProc(interp, pCtx, zArg1, nArg1, zArg2, nArg2);
    }
    Th8_ErrorMessage(interp, "unknown math function \"", zFunc, nFunc);
    return TH8_ERROR;
}


/*
 * Expression tree node.
 *
 * Each node is either a LITERAL (pOp==NULL, zValue holds the text)
 * or an OPERATOR (pOp!=NULL, pLeft/pRight are the operands).
 *
 * During tokenization (th8ExprParse), all nodes start as flat array
 * elements with pLeft==pRight==NULL.  During tree building
 * (th8ExprMakeTree), operator nodes acquire children by setting
 * pLeft/pRight and NULLing the corresponding array slots.
 *
 * A "term" is a node that is either a literal or an operator whose
 * pLeft has already been set (i.e., it has been incorporated into
 * the tree).  The TH8_ISTERM macro tests this.
 */

/* Th8_ExprNode -- declared in th8_expr.h. */


/*
 *----------------------------------------------------------------------
 *
 * th8ExprFree --
 *
 *	Recursively free an expression tree node and all of its
 *	descendants.
 *
 * Why / How:
 *	Expression trees are built from individually heap-allocated
 *	Th8_ExprNode structs, each owning a heap-allocated zValue
 *	string for literal nodes.  This function performs a post-order
 *	traversal (left, right, then self) to ensure children are
 *	freed before the parent.  NULL-safe: silently returns if
 *	pExpr is NULL, which simplifies cleanup of partially-built
 *	trees after parse errors.
 *
 * Results:
 *	None.
 *
 * Side effects:
 *	Frees all memory reachable from pExpr (nodes and their
 *	zValue strings).
 *
 *----------------------------------------------------------------------
 */

static void
th8ExprFree(Th8_Interp *interp, Th8_ExprNode *pExpr)
{
    if (pExpr) {
	th8ExprFree(interp, pExpr->pLeft);
	th8ExprFree(interp, pExpr->pRight);
	Th8_Free(interp, pExpr->zValue);
	Th8_Free(interp, pExpr);
    }
}


/*
 *----------------------------------------------------------------------
 *
 * th8ExprEval -- phase 3, recursive tree evaluator
 *
 *	Recursively evaluate an expression tree node (post-order
 *	traversal).  Most of this function is expression-grammar
 *	semantics (operator dispatch, type coercion, short-circuit);
 *	the one place it falls back to ordinary Tcl quoting is the
 *	literal-node branch, which delegates to th8SubstWord.
 *
 *	EVALUATION RULES (expr-grammar):
 *
 *	- Literal nodes:  the only Tcl-syntax interaction in this
 *	  function.  The literal's stored byte sequence is run
 *	  through th8SubstWord, which performs Tcl(n) Rule [8]
 *	  variable substitution (`$name`) and Rule [7] command
 *	  substitution (`[command]`) on its content -- the same
 *	  substitutions that would happen for a quoted operand
 *	  per expr(n) "OPERANDS": "As a string enclosed in
 *	  double-quotes.  The expression parser will perform
 *	  backslash, variable, and command substitutions on the
 *	  information between the quotes."  Brace-quoted operands
 *	  bypass this path because their content was kept verbatim
 *	  during tokenization, matching expr(n): "As a string
 *	  enclosed in braces.  The characters between the open
 *	  brace and matching close brace will be used as the
 *	  operand without any substitutions."
 *
 *	- Ternary `? :` (precedence 14): handled FIRST per
 *	  expr(n)'s lazy-evaluation guarantee.  Only the condition
 *	  and the selected branch are evaluated; the unselected
 *	  branch is never visited.
 *
 *	- Logical `&&` / `||` (precedence 12 / 13): handled SECOND
 *	  per the same expr(n) lazy guarantee.  The right operand
 *	  is evaluated only when the left does not determine the
 *	  result.
 *
 *	- All other operators: both children evaluated first
 *	  (left-then-right), then type coercion per the operator's
 *	  eArgType column from the precedence table:
 *	    TH8_ARG_NUMBER  -- try wide-int first; if both
 *	                       operands convert, use integer
 *	                       arithmetic; otherwise fall back to
 *	                       double.  Implements the expr(n)
 *	                       "Where possible, operands are
 *	                       interpreted as integer values"
 *	                       preference.
 *	    TH8_ARG_INTEGER -- require integer; matches the
 *	                       expr(n) "Valid for integer operands
 *	                       only" notes on the bit-wise and
 *	                       shift operators.
 *	    TH8_ARG_STRING  -- compare raw bytes for `eq`/`ne`/
 *	                       `in`/`ni`.
 *
 * Why / How (security envelope around expr-grammar):
 *	  - Integer overflow: every arithmetic operation (add,
 *	    subtract, multiply, divide, negate, exponent) checks
 *	    for signed 64-bit overflow before computing the result,
 *	    returning "integer overflow" on violation (or promoting
 *	    to bigint when enabled).
 *	  - Division by zero: checked for both integer and
 *	    floating-point division and modulus, producing explicit
 *	    errors.
 *	  - Type confusion: operands are coerced according to the
 *	    operator's eArgType, with strict integer-only or
 *	    number-or-double fallback rules preventing silent
 *	    misuse.
 *	  - Short-circuit safety: ternary (? :), logical AND (&&),
 *	    and logical OR (||) avoid evaluating unused branches
 *	    per expr(n) lazy semantics, preventing side effects
 *	    from unreachable sub-expressions.
 *	  - Recursion depth: bounded by Th8_Ready() check at entry,
 *	    which enforces nEvalDepth limits.
 *
 *	Intermediate results are taken via Th8_TakeResult and freed
 *	in a unified "finish" label to prevent leaks on error
 *	paths.
 *
 * Results:
 *	TH8_OK on success.  Interpreter result set to the computed
 *	value (as a string).
 *
 * Side effects:
 *	May evaluate command substitutions (Tcl(n) Rule [7]) and
 *	access variables (Tcl(n) Rule [8]) through th8SubstWord.
 *	May set the interpreter result to an error message.
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprEval(
    Th8_Interp *interp,
    Th8_ExprNode *pExpr,
    const char *zName,
    size_t nName)
{
    int rc = TH8_OK;

    /*
     * Security: unified readiness check per expression node.
     */

    if (Th8_Ready(interp) != TH8_OK) {
	return TH8_ERROR;
    }

    /*
     * Ternary `? :` -- expr-grammar lazy evaluation.
     *
     * Per expr(n): "x ? y : z -- If-then-else, as in C.  This
     * operator evaluates lazily; it evaluates only one of y or z."
     *
     * Must be handled BEFORE the generic left/right eval below so
     * the unselected branch is never evaluated -- otherwise side
     * effects in `[...]` and `$...` substitutions on the unused
     * branch would leak into the program (and potentially into
     * untrusted code paths).
     */

    if (pExpr->pOp && pExpr->pOp->eOp == TH8_OP_TERNARY_Q) {
	th8_int64_t iCond;

	/*
	 * Evaluate the condition (left child).
	 */

	rc = th8ExprEval(interp, pExpr->pLeft, zName, nName);
	if (rc != TH8_OK) return rc;
	{
	    size_t nCond;
	    char *zCond = th8TakeResultInternal(interp, &nCond);

	    rc = Th8_ToWideInt(interp, zCond, nCond, &iCond);
	    th8FreeSensitive(interp, zCond, nCond);
	    if (rc != TH8_OK) return rc;
	}

	/*
	 * The right child must be a ':' node with two branches.
	 * Parser invariant: th8ExprMakeTree binds the ternary's
	 * right child to a TH8_OP_TERNARY_C node with both
	 * branches populated before returning success.  Any
	 * malformed ternary errors out at tree-build time, so
	 * this defensive check is unreachable at eval under correct
	 * parser output.
	 *
	 * Bug 26 (2026-06-07): kept as a plain runtime check rather
	 * than NEVER-wrapped, because under TH8_OMIT the collapse
	 * would turn a malformed-tree (parser bug) into a SEGV at
	 * line 920 (pExpr->pRight->pLeft).  Graceful error is safer. */
	if (!pExpr->pRight || !pExpr->pRight->pOp ||
	    pExpr->pRight->pOp->eOp != TH8_OP_TERNARY_C) {
	    Th8_SetResult(
	        interp, "syntax error in ternary expression", TH8_NOLEN);
	    return TH8_ERROR;
	}

	if (iCond) {
	    return th8ExprEval(interp, pExpr->pRight->pLeft, zName, nName);
	} else {
	    return th8ExprEval(interp, pExpr->pRight->pRight, zName, nName);
	}
    }

    /*
     * `:=` variable-assignment -- TH8 EXTENSION (NOT in expr(n)).
     *
     * Reachable only when the parser was running with
     * TH8_EXPR_VAR_ASSIGN set; in strict mode the operator is
     * skipped during tokenisation so this branch is unreachable.
     *
     * Evaluation:
     *   1. Evaluate LHS as a normal operand.  Its substituted
     *      string value names the variable.  Per the design,
     *      bare-word LHS is REJECTED at parse time -- the LHS
     *      must be a quoted/braced string, a `$var` substitution,
     *      a `[command]`, or any sub-expression that yields a
     *      string.  This keeps the bareword-rejection security
     *      envelope intact even when the feature is enabled.
     *   2. Evaluate RHS as a normal operand.  Its result becomes
     *      the assigned value.
     *   3. Call Th8_SetVar(name, value).  On success the result
     *      is the assigned value (C-style), enabling `a := b :=
     *      1` to chain because each `:=` yields the value its
     *      RHS produced.
     *
     * Both children are evaluated; this is NOT a lazy operator.
     */

    if (pExpr->pOp && pExpr->pOp->eOp == TH8_OP_VAR_ASSIGN) {
#  if defined(TH8_ENABLE_VARIABLES)
	char *zVarName = NULL;
	size_t nVarName = 0;
	char *zRhs = NULL;
	size_t nRhs = 0;

	/* Bug 26: parser invariant says both operands are bound
	 * before eval, but a future parser bug could leave a
	 * malformed tree.  Use plain `if` so the guard survives
	 * TH8_OMIT. */

	if (!pExpr->pLeft || !pExpr->pRight) {
	    Th8_SetResult(interp, "syntax error in := expression", TH8_NOLEN);
	    return TH8_ERROR;
	}

	rc = th8ExprEval(interp, pExpr->pLeft, zName, nName);
	if (rc != TH8_OK) return rc;
	zVarName = th8TakeResultInternal(interp, &nVarName);
	nVarName = TH8_LEN(nVarName);

	rc = th8ExprEval(interp, pExpr->pRight, zName, nName);
	if (rc != TH8_OK) {
	    Th8_Free(interp, zVarName);
	    return rc;
	}
	/* Keep nRhs tagged (do NOT mask here): Th8_SetVar and Th8_SetResult
	 * both accept a tagged length and mask internally, so the assigned
	 * value's taint/sensitive classification propagates into the variable
	 * and the expression result. */
	zRhs = th8TakeResultInternal(interp, &nRhs);

	rc = Th8_SetVar(interp, zVarName, nVarName, zRhs, nRhs);
	Th8_Free(interp, zVarName);
	if (rc != TH8_OK) {
	    th8FreeSensitive(interp, zRhs, nRhs);
	    return rc;
	}

	/*
	 * Result is the assigned value (C-style).  Use SetResult
	 * (which copies) and then free our taken buffer.
	 */
	rc = Th8_SetResult(interp, zRhs, nRhs);
	th8FreeSensitive(interp, zRhs, nRhs);
	return rc;
#  else
	/* Bug 35: TH8_OP_VAR_ASSIGN (:=) is unreachable when
	 * variables are disabled (the parser would reject :=
	 * if the bareword/dollar-var LHS forms aren't
	 * available); guard the body so the source compiles. */
	Th8_SetResult(interp, "variable assignment not available", TH8_NOLEN);
	return TH8_ERROR;
#  endif
    }

    /*
     * Logical `&&` / `||` -- expr-grammar lazy evaluation.
     *
     * Per expr(n): "&& -- Logical AND.  This operator evaluates
     * lazily; it only evaluates its second operand if it must."
     * (and the matching wording for ||).  As with ternary above,
     * this MUST be handled before the generic left/right eval
     * path so the right operand is reached only when needed.
     */

    if (pExpr->pOp && (pExpr->pOp->eOp == TH8_OP_LOGICAL_AND ||
                       pExpr->pOp->eOp == TH8_OP_LOGICAL_OR)) {
	int bLeft;

	rc = th8ExprEval(interp, pExpr->pLeft, zName, nName);
	if (rc != TH8_OK) return rc;
	{
	    size_t nL;
	    char *zL = th8TakeResultInternal(interp, &nL);

	    rc = Th8_ToBoolean(interp, zL, nL, &bLeft);
	    th8FreeSensitive(interp, zL, nL);
	    if (rc != TH8_OK) return rc;
	}
	if (pExpr->pOp->eOp == TH8_OP_LOGICAL_AND) {
	    if (!bLeft) {
		return Th8_SetResultInt(interp, 0);
	    }
	    rc = th8ExprEval(interp, pExpr->pRight, zName, nName);
	    if (rc != TH8_OK) return rc;
	    {
		int bRight;
		size_t nR;
		char *zR = th8TakeResultInternal(interp, &nR);

		rc = Th8_ToBoolean(interp, zR, nR, &bRight);
		th8FreeSensitive(interp, zR, nR);
		if (rc != TH8_OK) return rc;
		return Th8_SetResultInt(interp, bRight != 0);
	    }
	} else {
	    if (bLeft) {
		return Th8_SetResultInt(interp, 1);
	    }
	    rc = th8ExprEval(interp, pExpr->pRight, zName, nName);
	    if (rc != TH8_OK) return rc;
	    {
		int bRight;
		size_t nR;
		char *zR = th8TakeResultInternal(interp, &nR);

		rc = Th8_ToBoolean(interp, zR, nR, &bRight);
		th8FreeSensitive(interp, zR, nR);
		if (rc != TH8_OK) return rc;
		return Th8_SetResultInt(interp, bRight != 0);
	    }
	}
    }

    if (pExpr->pOp == 0) {
	/*
	 * Literal node -- the only Tcl-syntax delegation point in
	 * th8ExprEval.
	 *
	 * The stored bytes were captured verbatim by th8ExprParse
	 * (an unsubstituted slice of the operand text).  We now
	 * apply Tcl(n) Rules [7], [8], [9] to those bytes via
	 * th8SubstWord so the operand value matches the expr(n)
	 * "OPERANDS" wording for double-quoted operands:
	 *     "The expression parser will perform backslash,
	 *      variable, and command substitutions on the
	 *      information between the quotes."
	 *
	 * Brace-quoted operands (expr(n) form 5: "without any
	 * substitutions") and bare numeric literals (forms 1 and
	 * 2) are also routed through here, but their stored bytes
	 * contain no `$` or `[` so the substitution pass is an
	 * effective no-op for them -- preserving the
	 * "without any substitutions" guarantee.
	 */

	rc = th8SubstWord(interp, pExpr->zValue, pExpr->nValue, zName, nName);

	/*
	 * Bug 7: normalise exponent-form numeric literals
	 * ("1.5E2" -> "150.0").  Detect a tight numeric shape
	 * with an explicit 'e'/'E' exponent, parse via
	 * Th8_ToDouble (correctly-rounded to within 1 ULP for
	 * all IEEE 754 doubles after the L15640+ rewrite that
	 * replaced the historical "frac *= 0.1" accumulator),
	 * and re-emit canonical form via Th8_SetResultDouble.
	 * Skip non-numeric strings (e.g. boolean "true").
	 *
	 * The historical TH8 behaviour was to pass exponent-form
	 * literals through verbatim; that matched Eagle but
	 * diverged from Tcl 8.6 (which uses strtod and emits
	 * canonical double form for all literal arms).
	 */

	if (rc == TH8_OK) {
	    size_t nRes;
	    const char *zRes = Th8_GetResult(interp, &nRes);
	    size_t k;
	    int hasExp = 0;
	    int hasOnlyNumeric = (nRes > 0);

	    for (k = 0; k < nRes; k++) {
		char c = zRes[k];
		if (c == 'e' || c == 'E') {
		    hasExp = 1;
		} else {
		    /* Each accepted-character check is a single
		     * condition for clang MC/DC.  See FINDINGS.md
		     * Finding 005. */
		    int accepted = 0;

		    if (c >= '0' && c <= '9')
			accepted = 1;
		    else if (c == '.')
			accepted = 1;
		    else if (c == '+')
			accepted = 1;
		    else if (c == '-')
			accepted = 1;
		    if (!accepted) {
			hasOnlyNumeric = 0;
			break;
		    }
		}
	    }
	    if (hasExp && hasOnlyNumeric) {
		double dVal;

		if (Th8_ToDouble(0, zRes, nRes, &dVal) == TH8_OK) {
		    Th8_SetResultDouble(interp, dVal);
		}
	    }
	}
	return rc;
    } else {
	int eArgType = TH8_ARG_NONE;
	th8_int64_t iLeft = 0, iRight = 0;
	char *zLeft = 0, *zRight = 0;
	size_t nLeft = 0, nRight = 0;
	/* Tagged lengths captured before masking, so a sensitive operand
	 * copy can be securely zeroed at the free below (defense in depth). */
	size_t nLeftTag = 0, nRightTag = 0;

	/*
	 * Evaluate children.
	 */

	if (pExpr->pLeft) {
	    rc = th8ExprEval(interp, pExpr->pLeft, zName, nName);
	    if (rc == TH8_OK) {
		zLeft = th8TakeResultInternal(interp, &nLeft);
		nLeftTag = nLeft;
		nLeft = TH8_LEN(nLeft);
	    }
	}
	if (rc == TH8_OK && pExpr->pRight) {
	    rc = th8ExprEval(interp, pExpr->pRight, zName, nName);
	    if (rc == TH8_OK) {
		zRight = th8TakeResultInternal(interp, &nRight);
		nRightTag = nRight;
		nRight = TH8_LEN(nRight);
	    }
	}

	/*
	 * Type coercion.
	 */

	if (rc == TH8_OK) {
	    eArgType = pExpr->pOp->eArgType;
	    if (eArgType == TH8_ARG_NUMBER) {
		/* Phase 2 binds unary operands as pLeft (never pRight),
		 * so zLeft is non-NULL whenever this NUMBER branch runs.
		 * zRight remains a real check -- unary ops legitimately
		 * have zRight=NULL.
		 *
		 * Bug 26 (2026-06-07): plain check rather than NEVER --
		 * under TH8_OMIT collapse, a parser bug would feed NULL
		 * to Th8_ToWideInt and SEGV; short-circuit is safer. */
		if ((zLeft == 0 ||
		     TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) &&
		    (zRight == 0 ||
		     TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))) {
		    eArgType = TH8_ARG_INTEGER;
		}
#  if defined(TH8_ENABLE_BIGINT)
		else if (
		    Th8_IsBigintEnabled(interp) &&
		    (zLeft == 0 || th8IsBigint(interp, zLeft, nLeft) ||
		     TH8_OK == Th8_ToWideInt(0, zLeft, nLeft, &iLeft)) &&
		    (zRight == 0 || th8IsBigint(interp, zRight, nRight) ||
		     TH8_OK == Th8_ToWideInt(0, zRight, nRight, &iRight))) {
		    /*
		     * At least one operand is a bigint and bigint
		     * is enabled for this interpreter.
		     */

		    eArgType = TH8_ARG_INTEGER;
		}
#  endif
		else {
		    /*
		     * Not both integers.  Try double.
		     */

		    double fLeft = 0.0, fRight = 0.0;

		    /* Phase 2 binds unary operands as pLeft, so zLeft is
		     * non-NULL whenever this NUMBER branch reaches the
		     * double fall-through.
		     *
		     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
		     * under TH8_OMIT collapse, a parser bug would feed NULL
		     * to Th8_ToDouble and SEGV; short-circuit is safer. */
		    if ((zLeft &&
		         TH8_OK !=
		             Th8_ToDouble(interp, zLeft, nLeft, &fLeft)) ||
		        (zRight &&
		         TH8_OK !=
		             Th8_ToDouble(interp, zRight, nRight, &fRight))) {
			rc = TH8_ERROR;
		    } else {
			/*
			 * Evaluate as double operation.
			 */

			switch (pExpr->pOp->eOp) {
			case TH8_OP_MULTIPLY:
			    Th8_SetResultDouble(interp, fLeft * fRight);
			    break;
			case TH8_OP_DIVIDE:
			    if (fRight == 0.0) {
				Th8_SetResult(
				    interp, "divide by zero", TH8_NOLEN);
				rc = TH8_ERROR;
				goto finish;
			    }
			    Th8_SetResultDouble(interp, fLeft / fRight);
			    break;
			case TH8_OP_ADD:
			    Th8_SetResultDouble(interp, fLeft + fRight);
			    break;
			case TH8_OP_SUBTRACT:
			    Th8_SetResultDouble(interp, fLeft - fRight);
			    break;
			case TH8_OP_LT:
			    Th8_SetResultInt(interp, fLeft < fRight);
			    break;
			case TH8_OP_GT:
			    Th8_SetResultInt(interp, fLeft > fRight);
			    break;
			case TH8_OP_LE:
			    Th8_SetResultInt(interp, fLeft <= fRight);
			    break;
			case TH8_OP_GE:
			    Th8_SetResultInt(interp, fLeft >= fRight);
			    break;
			case TH8_OP_EQ:
			    Th8_SetResultInt(interp, fLeft == fRight);
			    break;
			case TH8_OP_NE:
			    Th8_SetResultInt(interp, fLeft != fRight);
			    break;
			case TH8_OP_UNARY_MINUS:
			    Th8_SetResultDouble(interp, -fLeft);
			    break;
			case TH8_OP_UNARY_PLUS:
			    Th8_SetResultDouble(interp, +fLeft);
			    break;
			case TH8_OP_EXPONENT: {
			    /*
			     * Use repeated multiplication.
			     * Not IEEE-precise for all cases
			     * but correct for common use.
			     */

			    double result = 1.0;
			    int exp;

			    if (fRight < 0) {
				exp = (int)(-fRight);
				while (exp-- > 0) {
				    result *= fLeft;
				}
				result = 1.0 / result;
			    } else {
				exp = (int)fRight;
				while (exp-- > 0) {
				    result *= fLeft;
				}
			    }
			    Th8_SetResultDouble(interp, result);
			    break;
			}
			default:
			    Th8_ErrorMessage(
			        interp, "expected integer, got: \"",
			        zLeft ? zLeft : zRight,
			        zLeft ? nLeft : nRight);
			    rc = TH8_ERROR;
			    break;
			}
			goto finish;
		    }
		}
	    } else if (eArgType == TH8_ARG_INTEGER) {
#  if defined(TH8_ENABLE_BIGINT)
		/* Phase 2 binds unary operands as pLeft, never pRight,
		 * so any operator that reaches this INTEGER branch has
		 * pLeft set (zLeft != NULL).
		 *
		 * Bug 26 (2026-06-07): plain check rather than ALWAYS --
		 * under TH8_OMIT collapse, a parser bug would feed NULL
		 * to th8IsBigint and SEGV; short-circuit is safer. */
		if (Th8_IsBigintEnabled(interp) &&
		    ((zLeft && th8IsBigint(interp, zLeft, nLeft)) ||
		     (zRight && th8IsBigint(interp, zRight, nRight)))) {
		    /* Skip Th8_ToWideInt -- the bigint
		     * pre-check below will handle it. */
		} else
#  endif
		{
		    rc = Th8_ToWideInt(interp, zLeft, nLeft, &iLeft);

		    /*
		     * Logical NOT accepts boolean strings
		     * (true/false/yes/no/on/off) as well as
		     * integers, matching Tcl 8.x semantics.
		     */
		    if (rc != TH8_OK &&
		        pExpr->pOp->eOp == TH8_OP_LOGICAL_NOT) {
			int bVal;

			if (Th8_ToBoolean(interp, zLeft, nLeft, &bVal) ==
			    TH8_OK) {
			    iLeft = (th8_int64_t)bVal;
			    rc = TH8_OK;
			}
		    }

		    if (rc == TH8_OK && zRight) {
			rc = Th8_ToWideInt(interp, zRight, nRight, &iRight);
		    }
		}
	    }
	}

	/*
	 * Integer operations.
	 */

	if (rc == TH8_OK && eArgType == TH8_ARG_INTEGER) {
	    th8_int64_t iRes = 0;

#  if defined(TH8_ENABLE_BIGINT)
	    /*
	     * With bigint enabled, check if either operand
	     * is already a bigint (exceeds int64 range) and
	     * delegate to the bigint path.
	     */

	    /* Phase 2 binds operands as pLeft, so zLeft is non-NULL
	     * whenever this branch runs.
	     *
	     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
	     * under TH8_OMIT collapse, a parser bug would feed NULL
	     * to th8IsBigint and SEGV; short-circuit is safer. */
	    if (Th8_IsBigintEnabled(interp) &&
	        ((zLeft && th8IsBigint(interp, zLeft, nLeft)) ||
	         (zRight && th8IsBigint(interp, zRight, nRight)))) {
		if (pExpr->pOp->eOp == TH8_OP_UNARY_MINUS ||
		    pExpr->pOp->eOp == TH8_OP_UNARY_PLUS ||
		    pExpr->pOp->eOp == TH8_OP_BITWISE_NOT ||
		    pExpr->pOp->eOp == TH8_OP_LOGICAL_NOT) {
		    rc =
		        th8BigintUnary(interp, zLeft, nLeft, pExpr->pOp->eOp);
		} else {
		    rc = th8BigintArith(
		        interp, zLeft, nLeft, zRight, nRight,
		        pExpr->pOp->eOp);
		}
		goto finish;
	    }
#  endif /* TH8_ENABLE_BIGINT */

	    switch (pExpr->pOp->eOp) {
	    case TH8_OP_MULTIPLY:
		/*
		 * Overflow-checked multiplication.  For signed
		 * 64-bit: check if |a*b| would exceed INT64_MAX.
		 * With bigint enabled, promote instead of error.
		 */

		if (interp->bOverflowCheck && iRight != 0 &&
		    ((iLeft > 0 && iRight > 0 &&
		      iLeft > TH8_INT64_MAX / iRight) ||
		     (iLeft < 0 && iRight < 0 &&
		      iLeft < TH8_INT64_MAX / iRight) ||
		     (iLeft > 0 && iRight < 0 &&
		      iRight < TH8_INT64_MIN / iLeft) ||
		     (iLeft < 0 && iRight > 0 &&
		      iLeft < TH8_INT64_MIN / iRight))) {
#  if defined(TH8_ENABLE_BIGINT)
		    if (Th8_IsBigintEnabled(interp)) {
			rc = th8BigintArith(
			    interp, zLeft, nLeft, zRight, nRight,
			    TH8_OP_MULTIPLY);
			goto finish;
		    }
#  endif
		    Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		iRes = iLeft * iRight;
		break;
	    case TH8_OP_DIVIDE:
		if (!iRight) {
		    Th8_SetResult(interp, "divide by zero", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		/* INT64_MIN / -1 overflows. */
		if (interp->bOverflowCheck && iLeft == TH8_INT64_MIN &&
		    iRight == -1) {
#  if defined(TH8_ENABLE_BIGINT)
		    if (Th8_IsBigintEnabled(interp)) {
			rc = th8BigintArith(
			    interp, zLeft, nLeft, zRight, nRight,
			    TH8_OP_DIVIDE);
			goto finish;
		    }
#  endif
		    Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		/*
		 * Floor division (Tcl standard): round toward
		 * negative infinity, not toward zero.
		 */

		iRes = iLeft / iRight;
		if ((iLeft ^ iRight) < 0 && iRes * iRight != iLeft) {
		    iRes--;
		}
		break;
	    case TH8_OP_MODULUS:
		if (!iRight) {
		    Th8_SetResult(interp, "divide by zero", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		if (iLeft == TH8_INT64_MIN && iRight == -1) {
		    iRes = 0; /* No overflow; result is 0. */
		} else {
		    /*
		     * Floor modulo: result has the same sign
		     * as the divisor (Tcl standard).
		     */

		    iRes = iLeft % iRight;
		    if (iRes != 0 && (iRes ^ iRight) < 0) {
			iRes += iRight;
		    }
		}
		break;
	    case TH8_OP_ADD:
		if (interp->bOverflowCheck &&
		    ((iRight > 0 && iLeft > TH8_INT64_MAX - iRight) ||
		     (iRight < 0 && iLeft < TH8_INT64_MIN - iRight))) {
#  if defined(TH8_ENABLE_BIGINT)
		    if (Th8_IsBigintEnabled(interp)) {
			rc = th8BigintArith(
			    interp, zLeft, nLeft, zRight, nRight, TH8_OP_ADD);
			goto finish;
		    }
#  endif
		    Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		iRes = iLeft + iRight;
		break;
	    case TH8_OP_SUBTRACT:
		if (interp->bOverflowCheck &&
		    ((iRight < 0 && iLeft > TH8_INT64_MAX + iRight) ||
		     (iRight > 0 && iLeft < TH8_INT64_MIN + iRight))) {
#  if defined(TH8_ENABLE_BIGINT)
		    if (Th8_IsBigintEnabled(interp)) {
			rc = th8BigintArith(
			    interp, zLeft, nLeft, zRight, nRight,
			    TH8_OP_SUBTRACT);
			goto finish;
		    }
#  endif
		    Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		iRes = iLeft - iRight;
		break;
	    case TH8_OP_LEFT_SHIFT:
#  if defined(TH8_ENABLE_BIGINT)
		if (Th8_IsBigintEnabled(interp) &&
		    (iRight > 63 || iRight < 0)) {
		    rc = th8BigintArith(
		        interp, zLeft, nLeft, zRight, nRight,
		        TH8_OP_LEFT_SHIFT);
		    goto finish;
		}
#  endif
		iRes = (th8_int64_t)(((th8_uint64_t)iLeft)
		                     << (iRight & 0x3f));
		break;
	    case TH8_OP_RIGHT_SHIFT:
		iRes = iLeft >> (iRight & 0x3f);
		break;
	    case TH8_OP_LT:
		iRes = iLeft < iRight;
		break;
	    case TH8_OP_GT:
		iRes = iLeft > iRight;
		break;
	    case TH8_OP_LE:
		iRes = iLeft <= iRight;
		break;
	    case TH8_OP_GE:
		iRes = iLeft >= iRight;
		break;
	    case TH8_OP_EQ:
		iRes = iLeft == iRight;
		break;
	    case TH8_OP_NE:
		iRes = iLeft != iRight;
		break;
	    case TH8_OP_BITWISE_AND:
		iRes = iLeft & iRight;
		break;
	    case TH8_OP_BITWISE_XOR:
		iRes = iLeft ^ iRight;
		break;
	    case TH8_OP_BITWISE_OR:
		iRes = iLeft | iRight;
		break;
	    case TH8_OP_LOGICAL_AND:
		/* Short-circuit dispatch at L1014 above handles &&
		 * before the switch runs, so this case is dead at
		 * runtime.  Use bitwise AND of normalized booleans
		 * to keep the semantics correct without producing a
		 * MC/DC && decision region in dead code. */
		iRes = !!iLeft & !!iRight;
		break;
	    case TH8_OP_LOGICAL_OR:
		/* Same as LOGICAL_AND above -- short-circuit
		 * dispatch handles || before the switch.  Use
		 * bitwise OR of normalized booleans. */
		iRes = !!iLeft | !!iRight;
		break;
	    case TH8_OP_UNARY_MINUS:
		if (interp->bOverflowCheck && iLeft == TH8_INT64_MIN) {
#  if defined(TH8_ENABLE_BIGINT)
		    if (Th8_IsBigintEnabled(interp)) {
			rc = th8BigintUnary(
			    interp, zLeft, nLeft, TH8_OP_UNARY_MINUS);
			goto finish;
		    }
#  endif
		    Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
		    rc = TH8_ERROR;
		    goto finish;
		}
		iRes = -iLeft;
		break;
	    case TH8_OP_UNARY_PLUS:
		iRes = +iLeft;
		break;
	    case TH8_OP_BITWISE_NOT:
		iRes = ~iLeft;
		break;
	    case TH8_OP_LOGICAL_NOT:
		iRes = !iLeft;
		break;
	    case TH8_OP_EXPONENT: {
		/*
		 * Integer exponentiation with overflow checking.
		 * Uses binary exponentiation with overflow
		 * detection on every multiplication step.
		 */

		th8_int64_t base = iLeft;
		th8_int64_t exp = iRight;
		th8_int64_t result = 1;

		if (exp < 0) {
		    iRes = 0; /* Integer truncation. */
		} else {
		    int overflow = 0;

		    /* Loop invariant: every site that sets
		     * overflow=1 is followed immediately by
		     * `break;` (L1551, L1564), so the !overflow
		     * sub-check is a defensive belt-and-braces
		     * test, never F at re-entry. */
		    while (exp > 0 && ALWAYS(!overflow)) {
			if (exp & 1) {
			    if (interp->bOverflowCheck && base != 0 &&
			        ((result > 0 && base > 0 &&
			          result > TH8_INT64_MAX / base) ||
			         (result < 0 && base < 0 &&
			          result < TH8_INT64_MAX / base) ||
			         (result > 0 && base < 0 &&
			          base < TH8_INT64_MIN / result) ||
			         (result < 0 && base > 0 &&
			          result < TH8_INT64_MIN / base))) {
				overflow = 1;
				break;
			    }
			    result *= base;
			}
			if (exp > 1 && interp->bOverflowCheck && base != 0 &&
			    base != 1 && base != -1 &&
			    ((base > 0 && base > TH8_INT64_MAX / base) ||
			     (base < 0 && base < TH8_INT64_MAX / base))) {
			    overflow = 1;
			    break;
			}
			if (exp > 1) {
			    base *= base;
			}
			exp >>= 1;
		    }
		    if (overflow) {
#  if defined(TH8_ENABLE_BIGINT)
			if (Th8_IsBigintEnabled(interp)) {
			    rc = th8BigintArith(
			        interp, zLeft, nLeft, zRight, nRight,
			        TH8_OP_EXPONENT);
			    goto finish;
			}
#  endif
			Th8_SetResult(interp, "integer overflow", TH8_NOLEN);
			rc = TH8_ERROR;
			goto finish;
		    }
		    iRes = result;
		}
		break;
	    }
	    default:
		rc = TH8_ERROR;
		break;
	    }
	    if (rc == TH8_OK) {
		Th8_SetResultWideInt(interp, iRes);
	    }
	} else if (rc == TH8_OK && eArgType == TH8_ARG_STRING) {
	    /*
	     * String operations: eq, ne, in, ni.
	     *
	     * Dispatch invariant: eArgType is one of {INTEGER,
	     * NUMBER, STRING} when this if/else chain runs.
	     * INTEGER short-circuits at L1325; NUMBER paths
	     * `goto finish` after the double-result branch
	     * (L1279), never reaching this else if.
	     *
	     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
	     * a new eArgType added by a future operator extension
	     * would silently dispatch to the string ops under
	     * TH8_OMIT collapse; the gating check protects against
	     * that. */

	    switch (pExpr->pOp->eOp) {
	    case TH8_OP_SEQ:
	    case TH8_OP_SNE: {
		int iEqual = 0;
		/* Compare content only: mask the tag bits (taint/sensitive)
		 * out of the lengths.  Tags are value metadata, not part of
		 * the string, so a tainted or sensitive "x" is string-equal to
		 * a plain "x". */
		size_t nL = TH8_LEN(nLeft);
		size_t nR = TH8_LEN(nRight);

		if (nR == nL && 0 == Th8_Memcmp(interp, zRight, zLeft, nR)) {
		    iEqual = 1;
		}
		if (pExpr->pOp->eOp == TH8_OP_SEQ) {
		    Th8_SetResultInt(interp, iEqual);
		} else {
		    Th8_SetResultInt(interp, !iEqual);
		}
		break;
	    }
	    case TH8_OP_IN:
	    case TH8_OP_NI: {
		/*
		 * List membership: split the right operand as
		 * a Tcl list and search for an exact match of
		 * the left operand among its elements.
		 */

		char **azElem = 0;
		size_t *anElem = 0;
		int nCount = 0;
		int iFound = 0;

		rc = Th8_SplitList(
		    interp, zRight, nRight, &azElem, &anElem, &nCount,
		    TH8_LIST_NONE);
		if (rc == TH8_OK) {
		    int k;
		    /* Compare content only; mask tag bits from both the left
		     * operand and each element (a tainted/sensitive list
		     * yields tagged element lengths). */
		    size_t nLraw = TH8_LEN(nLeft);

		    for (k = 0; k < nCount; k++) {
			if (TH8_LEN(anElem[k]) == nLraw &&
			    0 ==
			        Th8_Memcmp(interp, azElem[k], zLeft, nLraw)) {
			    iFound = 1;
			    break;
			}
		    }
		    Th8_Free(interp, azElem);
		    if (pExpr->pOp->eOp == TH8_OP_IN) {
			Th8_SetResultInt(interp, iFound);
		    } else {
			Th8_SetResultInt(interp, !iFound);
		    }
		}
		break;
	    }
	    default:
		rc = TH8_ERROR;
		break;
	    }
	}

finish:
	th8FreeSensitive(interp, zLeft, nLeftTag);
	th8FreeSensitive(interp, zRight, nRightTag);
    }

    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ExprParse -- phase 1, expression-grammar tokenizer
 *
 *	Tokenize an expression string into a flat array of
 *	Th8_ExprNode pointers, ready for tree construction by
 *	th8ExprMakeTree.
 *
 * GRAMMAR BOUNDARY:
 *
 *	This function is the densest mix of the two grammars in the
 *	file.  At the OUTER level it implements the expr(n)
 *	"OPERANDS" form selector and the operator scanner.  Inside
 *	each operand recognizer it DELEGATES to the script-level
 *	parser (th8NextVarName, th8NextCommand) so that the values
 *	of those operands are produced by the same Tcl(n) rules a
 *	bare [set x ...] would honor.
 *
 *	expr(n) "OPERANDS" forms 1-7 mapped to the branches below:
 *
 *	  Form 1 -- "As a numeric value, either integer or floating-
 *	            point."  Decimal and prefix forms (0x hex, 0o
 *	            octal, 0b binary, 0NNN legacy octal).  Recognised
 *	            by the digit / '.' / 0[xobXOB] branches.
 *	  Form 2 -- "As a boolean value, using any form understood by
 *	            string is boolean."  Recognised by the
 *	            identifier branch (true/false/yes/no/on/off);
 *	            non-boolean barewords are rejected.
 *	  Form 3 -- "As a Tcl variable, using standard $ notation."
 *	            Recognised by the '$' branch which DELEGATES to
 *	            th8NextVarName -- the same engine that parses
 *	            $name, $name(idx), and ${name} in ordinary Tcl
 *	            scripts (Tcl(n) Rule [8]).
 *	  Form 4 -- "As a string enclosed in double-quotes.  The
 *	            expression parser will perform backslash,
 *	            variable, and command substitutions on the
 *	            information between the quotes."  Recognised by
 *	            the '"' branch; bytes are captured verbatim and
 *	            substitution is deferred to th8SubstWord during
 *	            evaluation (Tcl(n) Rule [4] semantics).
 *	  Form 5 -- "As a string enclosed in braces.  The characters
 *	            between the open brace and matching close brace
 *	            will be used as the operand without any
 *	            substitutions."  Recognised by the '{' branch
 *	            which DELEGATES to th8NextCommand for the
 *	            balanced-brace scan -- Tcl(n) Rule [6] verbatim.
 *	  Form 6 -- "As a Tcl command enclosed in brackets."  Same
 *	            '{'/'[' branch which calls th8NextCommand;
 *	            evaluation later runs the inner script through
 *	            the regular eval loop (Tcl(n) Rule [7]).
 *	  Form 7 -- "As a mathematical function whose arguments have
 *	            any of the above forms."  Recognised when an
 *	            identifier is immediately followed by '(';
 *	            arguments are split on top-level commas and
 *	            recursively evaluated by Th8_Expr.
 *
 *	OPERATORS are matched against the static th8Operators[]
 *	table by linear longest-match search.  Two expression-
 *	specific concerns are handled here, both belonging to the
 *	expr-grammar (NOT Tcl-syntax):
 *
 *	  * Word-boundary check for the alphabetic operators "eq",
 *	    "ne", "in", "ni": a following alnum or `_` byte means
 *	    we have run into an identifier (e.g. "nextafter" must
 *	    not be tokenized as "ne" + "xtafter").
 *	  * Unary-vs-binary disambiguation for `+` and `-`: the
 *	    unary form is taken only when the preceding token is
 *	    not a complete term -- precedence-1 operators carry a
 *	    "skip if there is a left-hand term" guard.
 *
 * SECURITY:
 *	Each token iteration checks Th8_Ready() to enforce
 *	recursion-depth and cancellation limits.  The token array
 *	grows with overflow-safe arithmetic (TH8_SAFE_MUL_SIZE).
 *	Unknown bare words are rejected as "invalid bareword" so an
 *	untrusted expression cannot accidentally name a non-
 *	registered command and have it dispatched.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR on syntax error or allocation
 *	failure.  On success, *papToken and *pnToken are set.
 *
 * Side effects:
 *	Allocates the token array and individual Th8_ExprNode
 *	structs.  May recursively evaluate sub-expressions for
 *	function arguments (Form 7).  Caller must free the array
 *	and nodes on both success and error.
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprParse(
    Th8_Interp *interp,
    const char *zExpr,
    size_t nExpr,
    Th8_ExprNode ***papToken,
    int *pnToken,
    const char *zName,
    size_t nName)
{
    Th8_ExprNode **apToken = 0;
    int nToken = 0;
    int nAlloc = 0;
    size_t i = 0;
    int rc = TH8_OK;

    /* Loop invariant: every error path inside the body sets
     * rc and immediately breaks.
     *
     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
     * if a future error path forgets the break, ALWAYS would
     * silently continue tokenising past the error under
     * TH8_OMIT collapse. */
    while (i < nExpr && rc == TH8_OK) {
	Th8_ExprNode *pNode;

	/*
	 * Security: readiness check per expression token.
	 */

	if (Th8_Ready(interp) != TH8_OK) {
	    rc = TH8_ERROR;
	    break;
	}

	/*
	 * Inter-token whitespace.  Within an expression, whitespace
	 * (and backslash-newline continuation per Tcl(n) Rule [9]'s
	 * "\<newline>whiteSpace" entry) is non-significant -- the
	 * tokens themselves are what carry meaning.  This is the
	 * expr-grammar equivalent of Tcl(n) Rule [3] (words
	 * separated by whitespace) but with NO word boundaries
	 * being created -- adjacent tokens are simply consumed in
	 * order.
	 */
	while (i < nExpr) {
	    if (th8IsSpace(zExpr[i])) {
		i++;
	    } else if (
	        zExpr[i] == '\\' && i + 1 < nExpr &&
	        (zExpr[i + 1] == '\n' || zExpr[i + 1] == '\r')) {
		i += 2;
		/* Skip optional whitespace after continuation. */
		while (i < nExpr && (zExpr[i] == ' ' || zExpr[i] == '\t')) {
		    i++;
		}
	    } else {
		break;
	    }
	}
	if (i >= nExpr) break;

	/* Allocate node */
	pNode = (Th8_ExprNode *)TH8_ALLOC(interp, sizeof(Th8_ExprNode));
	if (!pNode) {
	    Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
	    rc = TH8_ERROR;
	    break;
	}

	if (zExpr[i] == '0' && i + 1 < nExpr &&
	    (zExpr[i + 1] == 'x' || zExpr[i + 1] == 'X' ||
	     zExpr[i + 1] == 'o' || zExpr[i + 1] == 'O' ||
	     zExpr[i + 1] == 'b' || zExpr[i + 1] == 'B')) {
	    /*
	     * expr(n) "OPERANDS" form 1 (numeric, prefixed integer).
	     * Per the man page: "Integer values may be specified in
	     * decimal (the normal case), in binary (if the first
	     * two characters of the operand are 0b), in octal (if
	     * the first two characters of the operand are 0o), or
	     * in hexadecimal (if the first two characters of the
	     * operand are 0x)."  Pure expr-grammar: NO Tcl-syntax
	     * substitution applies to the captured digits.
	     */
	    size_t start = i;

	    i += 2;
	    while (i < nExpr && th8IsAlnum(zExpr[i])) {
		i++;
	    }
	    pNode->nValue = i - start;
	    pNode->zValue = (char *)TH8_ALLOC_STR(interp, pNode->nValue);
	    if (!pNode->zValue) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    Th8_Memcpy(interp, pNode->zValue, &zExpr[start], pNode->nValue);
	    pNode->zValue[pNode->nValue] = 0;
	} else if (th8IsDigit(zExpr[i]) || zExpr[i] == '.') {
	    /*
	     * expr(n) "OPERANDS" form 1 (numeric, decimal/real).
	     * Per the man page: "If an operand does not have one of
	     * the integer formats given above, then it is treated
	     * as a floating-point number if that is possible.
	     * Floating-point numbers may be specified in any of
	     * several common formats making use of the decimal
	     * digits, the decimal point ., the characters e or E
	     * indicating scientific notation, and the sign
	     * characters + or -."  The +/- after e/E is recognised
	     * here (it is part of the literal, not a separate
	     * operator).  Pure expr-grammar: no Tcl-syntax
	     * substitution applies.
	     */
	    size_t start = i;

	    /* The enclosing else-if at L1865 only enters this
	     * branch when the current byte is a digit or '.',
	     * so first iteration cannot fire the +/- sub-clause
	     * (which requires `+` or `-` AT i); from iter 2
	     * onward `i > start` is invariantly T.
	     *
	     * Bug 26 (2026-06-07): plain check rather than ALWAYS --
	     * under TH8_OMIT collapse, iter-1 with start==0 would
	     * make `zExpr[i-1]` underflow size_t (SIZE_MAX index)
	     * and SEGV; the i > start guard must remain real. */
	    while (i < nExpr &&
	           (th8IsDigit(zExpr[i]) || zExpr[i] == '.' ||
	            zExpr[i] == 'e' || zExpr[i] == 'E' ||
	            ((zExpr[i] == '+' || zExpr[i] == '-') && i > start &&
	             (zExpr[i - 1] == 'e' || zExpr[i - 1] == 'E')))) {
		i++;
	    }
	    pNode->nValue = i - start;
	    pNode->zValue = (char *)TH8_ALLOC_STR(interp, pNode->nValue);
	    if (!pNode->zValue) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    Th8_Memcpy(interp, pNode->zValue, &zExpr[start], pNode->nValue);
	    pNode->zValue[pNode->nValue] = 0;
	} else if (zExpr[i] == '$') {
	    /*
	     * expr(n) "OPERANDS" form 3 (Tcl variable, "$" notation).
	     * Tcl-syntax delegation: th8NextVarName implements
	     * Tcl(n) Rule [8] verbatim and recognises every form
	     * the rule lists -- $name, $name(idx), and ${name}.
	     * The captured byte slice (including the leading `$`)
	     * is stored on the literal node and substituted at
	     * evaluation time by th8SubstWord, which honors all
	     * three forms identically to a [set] in script context.
	     */
	    size_t nVar;

	    rc = th8NextVarName(interp, &zExpr[i], nExpr - i, &nVar);
	    if (rc != TH8_OK) {
		Th8_Free(interp, pNode);
		break;
	    }
	    pNode->nValue = nVar;
	    pNode->zValue = (char *)TH8_ALLOC_STR(interp, nVar);
	    if (!pNode->zValue) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    Th8_Memcpy(interp, pNode->zValue, &zExpr[i], nVar);
	    pNode->zValue[nVar] = 0;
	    i += nVar;
	} else if (zExpr[i] == '{' || zExpr[i] == '[') {
	    /*
	     * expr(n) "OPERANDS" form 5 ("string enclosed in
	     * braces ... without any substitutions") or form 6
	     * ("Tcl command enclosed in brackets").
	     *
	     * Tcl-syntax delegation: th8NextCommand finds the
	     * matching close-delimiter using the full Tcl(n)
	     * Rule [6]/[7] semantics -- the same engine that
	     * scans `{...}` and `[...]` in ordinary script
	     * context.  The complete delimited block (including
	     * the outer `{` `}` or `[` `]` bytes) is stored on
	     * the literal node; what the bytes mean at evaluation
	     * time depends on which delimiter opened the block:
	     *
	     *   `{...}`  th8SubstWord notices the leading `{`
	     *            and trailing `}` and strips them,
	     *            yielding the inner text verbatim --
	     *            "without any substitutions" per
	     *            expr(n) form 5 / Tcl(n) Rule [6].
	     *   `[...]`  th8SubstWord recognises the brackets
	     *            and runs the inner script through the
	     *            eval loop, replacing the operand with
	     *            the script's result -- expr(n) form 6 /
	     *            Tcl(n) Rule [7].
	     */
	    size_t nCmd;

	    rc = th8NextCommand(interp, &zExpr[i], nExpr - i, &nCmd);
	    if (rc != TH8_OK) {
		Th8_Free(interp, pNode);
		break;
	    }
	    pNode->nValue = nCmd;
	    pNode->zValue = (char *)TH8_ALLOC_STR(interp, nCmd);
	    if (!pNode->zValue) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    Th8_Memcpy(interp, pNode->zValue, &zExpr[i], nCmd);
	    pNode->zValue[nCmd] = 0;
	    i += nCmd;
	} else if (zExpr[i] == '"') {
	    /*
	     * expr(n) "OPERANDS" form 4 ("string enclosed in
	     * double-quotes").  Per the man page: "The expression
	     * parser will perform backslash, variable, and command
	     * substitutions on the information between the quotes."
	     *
	     * The scan below mirrors Tcl(n) Rule [4]: walk to the
	     * next unescaped `"`, honoring backslash-skip per
	     * Rule [9] so that `\"` does NOT close the string.
	     * Substitution itself (Rules [7]/[8]/[9]) is deferred
	     * to th8SubstWord at evaluation time -- this branch
	     * only captures the byte range.
	     *
	     * Note: nested `[...]` inside the quoted operand is
	     * NOT separately tracked here because the contents are
	     * just bytes until evaluation; the script-level rules
	     * would push a bracket scope but the expr-grammar only
	     * needs to find the matching `"`.  A bare `]` inside
	     * `"..."` would be literal anyway per Tcl(n) Rule [4]
	     * "close brackets ... are treated as ordinary
	     * characters".
	     */
	    size_t start = i;

	    i++;
	    while (i < nExpr && zExpr[i] != '"') {
		if (zExpr[i] == '\\' && i + 1 < nExpr) {
		    i++;
		}
		i++;
	    }
	    if (i < nExpr) i++; /* skip closing quote */
	    pNode->nValue = i - start;
	    pNode->zValue = (char *)TH8_ALLOC_STR(interp, pNode->nValue);
	    if (!pNode->zValue) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    Th8_Memcpy(interp, pNode->zValue, &zExpr[start], pNode->nValue);
	    pNode->zValue[pNode->nValue] = 0;
	} else {
	    /*
	     * Operator scan (expr-grammar).  No Tcl-syntax
	     * delegation here: operator recognition is the
	     * exclusive concern of expr(n).  The scanner walks
	     * th8Operators[] in declaration order, which is
	     * arranged so that multi-character operators precede
	     * their single-character prefixes -- guaranteeing
	     * longest-match without any backtracking.
	     */
	    int j;
	    int matched = 0;

	    for (j = 0; th8Operators[j].zOp; j++) {
		size_t nOp = (size_t)th8Operators[j].nOp;

		/*
		 * Feature-flag gate: rows whose nReqFlag is non-zero
		 * are TH8 extensions that must NOT be visible in
		 * strict expr(n) mode.  Skipping the row here makes
		 * the operator's bytes fall through to the next
		 * operand-recogniser (typically producing the same
		 * "syntax error in expression" the parser would
		 * have produced before the extension was added).
		 * See doc/coverage_expr_strict.tcl for the
		 * regression tests that lock in this behaviour.
		 */
		if (th8Operators[j].nReqFlag != 0 &&
		    (Th8_GetExprFeatures(interp) &
		     th8Operators[j].nReqFlag) != th8Operators[j].nReqFlag) {
		    continue;
		}

		if (i + nOp <= nExpr &&
		    0 == Th8_Memcmp(
		             interp, &zExpr[i], th8Operators[j].zOp, nOp)) {
		    /*
		     * Expr-grammar word-boundary check for the
		     * alphabetic operators "eq", "ne", "in",
		     * "ni" (precedence 8 in expr(n)).  These
		     * keywords are spelled like identifiers, so
		     * a following alnum or `_` byte means we
		     * have run into a longer identifier (e.g.
		     * "nextafter" must not tokenize as
		     * "ne" + "xtafter"; "incr_count" must not
		     * start with the "in" operator).
		     */

		    if (th8IsAlpha(th8Operators[j].zOp[0])) {
			size_t k = i + nOp;

			/* th8IsAlnum (th8_core.c L10171) tests prop
			 * bits 1|3; bit 3 covers alpha AND underscore
			 * (see th8CharProp table), so '_' is already
			 * alnum.  The explicit '_' sub-check is
			 * redundant and NEVER fires once C2=F has been
			 * observed -- if zExpr[k] is '_', th8IsAlnum
			 * is T, short-circuiting at C2.  Wrap as NEVER
			 * to fold C3 out of MC/DC analysis. */
			if (k < nExpr && (th8IsAlnum(zExpr[k]) ||
			                  NEVER(zExpr[k] == '_'))) {
			    continue; /* not a word boundary */
			}
		    }

		    /*
		     * Expr-grammar unary-vs-binary disambiguation
		     * for `+` and `-`.  Per expr(n)'s precedence
		     * table the unary forms (precedence 1) and
		     * the binary forms (precedence 4) share the
		     * same byte representation; the unary form
		     * is taken only when there is NO preceding
		     * complete term.
		     *
		     * "Complete term" here means any of:
		     *   * a literal node (no pOp);
		     *   * an operator node that has already
		     *     absorbed its left operand (pLeft set);
		     *   * a close-bracket `)` token, which marks
		     *     the end of a parenthesised sub-expr
		     *     even though it has not yet been
		     *     collapsed by Phase 1 of the tree
		     *     builder.
		     *
		     * The `)`-as-term case is the subtle one:
		     * during tokenization the previous token is
		     * still the raw `)` operator with pLeft=NULL,
		     * so the literal/pLeft-set check would
		     * incorrectly classify it as "not a term"
		     * and select the unary form for the next
		     * `+`/`-`.  That breaks `(a) + b` and
		     * similar idioms.  Recognising
		     * TH8_OP_CLOSE_BRACKET explicitly here
		     * matches the C grammar's treatment of `)`
		     * as terminating a primary expression.
		     */

		    if (th8Operators[j].iPrecedence == 1) {
			Th8_ExprNode *pPrev = (nToken > 0)
			                        ? apToken[nToken - 1]
			                        : NULL;
			/* pPrev->pLeft is set only by th8ExprMakeTree
			 * (Phase 2-5), which runs after this parser has
			 * already produced the full token stream.  During
			 * parse, every operator token has pLeft == NULL.
			 *
			 * Bug 26 (2026-06-07): plain check rather than
			 * NEVER -- a parser/tree-build refactor that
			 * leaks pLeft set during tokenization would
			 * silently misclassify here under TH8_OMIT
			 * collapse; live check keeps the classification
			 * conservative. */
			int prevIsTerm = pPrev && (!pPrev->pOp ||
			                           pPrev->pLeft != NULL ||
			                           (pPrev->pOp->eOp ==
			                            TH8_OP_CLOSE_BRACKET));

			if (prevIsTerm) {
			    continue; /* skip, try binary */
			}
		    }
		    pNode->pOp = &th8Operators[j];
		    i += nOp;
		    matched = 1;
		    break;
		}
	    }
	    if (!matched) {
		/*
		 * Identifier-prefixed token: math-function call
		 * (expr(n) form 7), boolean literal (form 2), or
		 * a special floating-point literal (NaN, Inf,
		 * Infinity -- valid form-1 doubles).  The "eq",
		 * "ne", "in", "ni" alphabetic operators were
		 * already consumed by the operator scan above.
		 */

		size_t start = i;

		while (i < nExpr &&
		       (th8IsAlnum(zExpr[i]) || NEVER(zExpr[i] == '_'))) {
		    i++;
		}
		if (i > start && i < nExpr && zExpr[i] == '(') {
		    /*
		     * expr(n) "OPERANDS" form 7 -- math function
		     * call.  Per expr(n) "MATH FUNCTIONS": "When
		     * the expression parser encounters a
		     * mathematical function such as sin($x), it
		     * replaces it with a call to an ordinary Tcl
		     * command in the tcl::mathfunc namespace."
		     * TH8 routes that call through the per-interp
		     * registry via th8ExprEvalFunc instead of a
		     * namespace, but the recognition rule is
		     * identical: an identifier immediately
		     * followed by `(` opens the argument list,
		     * which closes at the matching `)`.
		     *
		     * Each comma-separated argument is itself a
		     * complete expression (the recursion through
		     * Th8_Expr below handles that), so arguments
		     * have access to the full expr-grammar
		     * including nested function calls and
		     * variable substitutions.  Argument values
		     * are pre-evaluated to scalar strings before
		     * being handed to the function -- there is
		     * no late binding.
		     */

		    size_t nFuncName = i - start;
		    size_t iArgs;
		    int nParen = 1;
		    char *zArg1 = 0;
		    size_t nArg1 = 0;
		    char *zArg2 = 0;
		    size_t nArg2 = 0;

		    i++; /* skip '(' */
		    iArgs = i;
		    while (i < nExpr && nParen > 0) {
			if (zExpr[i] == '(')
			    nParen++;
			else if (zExpr[i] == ')')
			    nParen--;
			if (nParen > 0) i++;
		    }
		    if (nParen != 0) {
			Th8_Free(interp, pNode);
			Th8_ErrorMessage(
			    interp, "unmatched ( in expression: \"", zExpr,
			    nExpr);
			rc = TH8_ERROR;
			break;
		    }

		    /*
		     * Split arguments on comma.
		     */

		    {
			size_t k;
			size_t commaAt = 0;
			int found = 0;
			int depth = 0;

			for (k = iArgs; k < i; k++) {
			    if (zExpr[k] == '(') {
				depth++;
			    } else if (zExpr[k] == ')') {
				depth--;
			    } else if (zExpr[k] == ',' && depth == 0) {
				commaAt = k;
				found = 1;
				break;
			    }
			}
			/* Evaluate first arg as sub-expression */
			if (i > iArgs) {
			    size_t n1 = found ? commaAt - iArgs : i - iArgs;

			    rc = Th8_Expr(
			        interp, &zExpr[iArgs], n1, zName, nName);
			    if (rc != TH8_OK) {
				Th8_Free(interp, pNode);
				break;
			    }
			    zArg1 = th8TakeResultInternal(interp, &nArg1);
			}
			/* Evaluate second arg if present */
			if (found) {
			    size_t n2 = i - commaAt - 1;

			    rc = Th8_Expr(
			        interp, &zExpr[commaAt + 1], n2, zName,
			        nName);
			    if (rc != TH8_OK) {
				th8FreeSensitive(interp, zArg1, nArg1);
				Th8_Free(interp, pNode);
				break;
			    }
			    zArg2 = th8TakeResultInternal(interp, &nArg2);
			}
		    }

		    /*
		     * Call the math function.
		     */

		    rc = th8ExprEvalFunc(
		        interp, &zExpr[start], nFuncName, zArg1, nArg1, zArg2,
		        nArg2);
		    th8FreeSensitive(interp, zArg1, nArg1);
		    th8FreeSensitive(interp, zArg2, nArg2);
		    if (rc != TH8_OK) {
			Th8_Free(interp, pNode);
			break;
		    }
		    i++; /* skip ')' */

		    {
			size_t nRes;
			const char *zRes;

			zRes = Th8_GetResult(interp, &nRes);
			pNode->nValue = nRes;
			pNode->zValue = (char *)TH8_ALLOC_STR(interp, nRes);
			if (!pNode->zValue) {
			    Th8_Free(interp, pNode);
			    Th8_SetResult(interp, "out of memory", TH8_NOLEN);
			    rc = TH8_ERROR;
			    break;
			}
			Th8_Memcpy(interp, pNode->zValue, zRes, nRes);
			pNode->zValue[nRes] = 0;
		    }
		} else if (i > start) {
		    /*
		     * expr(n) "OPERANDS" form 2 ("As a boolean
		     * value, using any form understood by string
		     * is boolean") or a special form-1 floating-
		     * point value (NaN, Inf, Infinity recognised
		     * by Th8_ToDouble).
		     *
		     * Any other identifier-shaped token is a
		     * "bareword" and is REJECTED -- expr(n) does
		     * not allow operands to silently degrade into
		     * unquoted strings, and the TH8 security
		     * envelope explicitly prevents it so an
		     * untrusted expression cannot accidentally
		     * name a non-registered command.  Tcl proper
		     * raises the same error ("invalid bareword").
		     */

		    size_t nId = i - start;
		    int bOk = 0;

		    if (Th8_ToBoolean(0, &zExpr[start], nId, 0) == TH8_OK) {
			bOk = 1;
		    } else if (
		        Th8_ToDouble(0, &zExpr[start], nId, 0) == TH8_OK) {
			/* NaN, Inf, Infinity */
			bOk = 1;
		    }

		    if (!bOk) {
			char zBad[64];
			size_t nBad = nId;

			if (nBad > sizeof(zBad) - 1) {
			    nBad = sizeof(zBad) - 1;
			}
			Th8_Memcpy(interp, zBad, &zExpr[start], nBad);
			zBad[nBad] = 0;
			Th8_Free(interp, pNode);
			Th8_ErrorMessage(
			    interp, "invalid bareword \"", zBad, nBad);
			rc = TH8_ERROR;
			break;
		    }

		    pNode->nValue = nId;
		    pNode->zValue = (char *)TH8_ALLOC_STR(interp, nId);
		    if (!pNode->zValue) {
			Th8_Free(interp, pNode);
			Th8_SetResult(interp, "out of memory", TH8_NOLEN);
			rc = TH8_ERROR;
			break;
		    }
		    Th8_Memcpy(interp, pNode->zValue, &zExpr[start], nId);
		    pNode->zValue[nId] = 0;
		} else {
		    Th8_Free(interp, pNode);
		    Th8_ErrorMessage(
		        interp, "syntax error in expression: \"", zExpr,
		        nExpr);
		    rc = TH8_ERROR;
		    break;
		}
	    }
	}

	/* Add node to array */
	if (nToken >= nAlloc) {
	    int nNew;
	    size_t nBytes = 0;
	    Th8_ExprNode **apNew;

	    /* Overflow-safe growth. */
	    if (nAlloc > 0x3fffffff) {
		Th8_SetResult(interp, "expression too complex", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    nNew = nAlloc * 2 + 16;
	    if (TH8_SAFE_MUL_SIZE(
	            sizeof(Th8_ExprNode *), (size_t)nNew, &nBytes)) {
		Th8_SetResult(interp, "expression too complex", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    apNew = (Th8_ExprNode **)TH8_ALLOC(interp, nBytes);
	    if (!apNew) {
		Th8_Free(interp, pNode);
		Th8_SetResultStatic(interp, "out of memory", TH8_NOLEN);
		rc = TH8_ERROR;
		break;
	    }
	    if (apToken) {
		/*
		 * AUDIT-OK[size-cast-multiply-before]: nToken <= nAlloc
		 * by loop invariant, and sizeof(Th8_ExprNode*) * nAlloc
		 * was validated via TH8_SAFE_MUL_SIZE at the realloc
		 * site above (see lines 1906-1912).  The multiply here
		 * is therefore bounded above by an already-checked
		 * value and cannot overflow.
		 */
		Th8_Memcpy(
		    interp, apNew, apToken,
		    sizeof(Th8_ExprNode *) * (size_t)nToken);
		Th8_Free(interp, apToken);
	    }
	    apToken = apNew;
	    nAlloc = nNew;
	}
	apToken[nToken++] = pNode;
    }

    *papToken = apToken;
    *pnToken = nToken;
    return rc;
}


/*
 * TH8_ISTERM -- true if a token is a complete term (a literal or an
 * operator that already has its left child set, meaning it has been
 * incorporated into the tree).
 */

/* TH8_ISTERM -- declared in th8_expr.h. */

/*
 *----------------------------------------------------------------------
 *
 * th8ExprMakeTree -- phase 2, expression-grammar tree builder
 *
 *	Build an expression tree from an array of tokens, respecting
 *	operator precedence.  Operates in-place on the token array:
 *	operator nodes acquire children, and the consumed array
 *	slots are set to NULL.  After completion, apToken[0] (or the
 *	first non-NULL entry) holds the root of the expression tree.
 *
 *	This phase is PURE expression grammar -- there is no
 *	Tcl-syntax interaction here.  All Tcl-rule recognition was
 *	completed in phase 1 (th8ExprParse) and the only remaining
 *	work is wiring the operator nodes together according to the
 *	expr(n) precedence table.
 *
 *	PHASES (in order, each implementing one slice of expr(n)):
 *
 *	  1. Parentheses (expr(n): "A Tcl expression consists of a
 *	     combination of operands, operators, parentheses and
 *	     commas.").  Matched pairs of `(` and `)` are found and
 *	     the sub-array between them is recursively processed;
 *	     the parenthesis nodes are freed and the result replaces
 *	     them.  Parens are not in expr(n)'s precedence table --
 *	     they bind everything inside before any outer operator
 *	     is considered.
 *
 *	  2. Unary operators (precedence 1, expr(n) "- + ~ !"):
 *	     scanned RIGHT-TO-LEFT so that "- - x" nests correctly
 *	     as `-(-(x))`.  Each unary operator's operand is the
 *	     next term to its right.
 *
 *	  3. Binary operators (precedence 2..13).  expr(n) verbatim:
 *	     "All of the binary operators but exponentiation group
 *	     left-to-right within the same precedence level;
 *	     exponentiation groups right-to-left."
 *
 *	     Implemented as: for each precedence level p from 2 to
 *	     13, scan the array and build operator nodes.  At p=2
 *	     (`**`) the scan direction is REVERSED so right-to-left
 *	     associativity is honored; at all other levels the
 *	     scan is left-to-right.  Lower precedence number means
 *	     tighter binding (** at 2 binds tighter than + at 4).
 *
 *	  4. Ternary `? :` (precedence 14, expr(n) "x ? y : z").
 *	     The `?` node gets the condition as pLeft and the `:`
 *	     node as pRight.  The `:` node gets the true-branch as
 *	     pLeft and false-branch as pRight.  The lazy-evaluation
 *	     guarantee is enforced in th8ExprEval, not here -- this
 *	     phase only builds the tree shape.
 *
 * Why / How:
 *	The algorithm works in-place by pointer manipulation: when
 *	an operator acquires children, their array slots are NULLed
 *	out and the operator slot retains the subtree root.  This
 *	avoids extra allocations during tree construction.
 *
 *	Security: Th8_Ready() is checked at entry to enforce
 *	recursion depth limits for deeply nested parentheses,
 *	preventing stack overflow from malicious expressions like
 *	`((((((...))))))`.  Unmatched parentheses and missing
 *	operands produce TH8_ERROR rather than undefined behavior.
 *
 * Results:
 *	TH8_OK on success; TH8_ERROR if the token array cannot form
 *	a valid expression tree (syntax error).
 *
 * Side effects:
 *	Modifies the apToken array in-place (NULLs consumed slots,
 *	frees parenthesis nodes).  May recurse for parenthesized
 *	sub-expressions.
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprMakeTree(Th8_Interp *interp, Th8_ExprNode **apToken, int nToken)
{
    int jj;
    int iLeft;
    int i;
    int p;

    /*
     * Security: readiness check for deeply nested parentheses.
     */

    if (Th8_Ready(interp) != TH8_OK) {
	return TH8_ERROR;
    }

    if (nToken <= 0) return TH8_ERROR;

    /*
     * Phase 1: Handle parentheses.
     */

    for (jj = 0; jj < nToken; jj++) {
	if (ALWAYS(apToken[jj]) && apToken[jj]->pOp &&
	    apToken[jj]->pOp->eOp == TH8_OP_OPEN_BRACKET) {
	    int nNest = 1;
	    int iStart = jj;

	    for (jj++; jj < nToken; jj++) {
		Th8_Operator *pOp = apToken[jj] ? apToken[jj]->pOp : 0;

		if (pOp && pOp->eOp == TH8_OP_OPEN_BRACKET) {
		    nNest++;
		}
		if (pOp && pOp->eOp == TH8_OP_CLOSE_BRACKET) {
		    nNest--;
		}
		if (nNest == 0) break;
	    }
	    if (jj == nToken) return TH8_ERROR;
	    if ((jj - iStart) <= 1) {
		/* Empty parentheses "()" -- syntax error. */
		return TH8_ERROR;
	    }
	    if ((jj - iStart) > 1) {
		if (th8ExprMakeTree(
		        interp, &apToken[iStart + 1], jj - iStart - 1)) {
		    return TH8_ERROR;
		}
		th8ExprFree(interp, apToken[jj]);
		apToken[jj] = 0;
		th8ExprFree(interp, apToken[iStart]);
		apToken[iStart] = apToken[iStart + 1];
		apToken[iStart + 1] = 0;
	    }
	}
    }

    /*
     * Phase 2: Unary operators (right to left).
     *
     * A unary op that fails to find an operand to its right is a
     * syntax error -- mirrors Phase 3's binary-binding check.  Bug
     * 10 documented two reproducers (`expr -`/`+` evaluating to 0,
     * `expr !`/`~` hanging) that traced to this branch silently
     * leaving the operator unbound; the latter then caused the
     * eval driver to spin re-visiting the same node.
     */

    for (jj = nToken - 1; jj >= 0; jj--) {
	if (apToken[jj] && apToken[jj]->pOp &&
	    apToken[jj]->pOp->iPrecedence == 1 && !apToken[jj]->pLeft) {
	    /* Find operand to the right.  In the phase-2 unary
	     * pass, by the time we reach a precedence-1 op with a
	     * NULL pLeft, the slots to its right are either a
	     * valid term or already consumed -- never a chain of
	     * NULLs.  The "skip nulls" loop body therefore never
	     * iterates in practice (compound MC/DC stuck at 0%
	     * intrinsic-dead).  Replaced with a simple step-past
	     * for clarity per Finding 005 sec. 5b. */
	    i = jj + 1;
	    /* Split outer `||` and the macro into single-condition
	     * `if`s so clang's MC/DC instrumenter does not have to
	     * encode the post-expansion 4-condition compound that
	     * would otherwise exceed the truth-table cap.  See
	     * FINDINGS.md Finding 005. */
	    if (i >= nToken) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, i)) return TH8_ERROR;
	    apToken[jj]->pLeft = apToken[i];
	    apToken[i] = 0;
	}
    }

    /*
     * Phase 3: Binary operators by precedence (low to high
     * precedence number = tightest binding first).
     */

    for (p = 2; p <= 13; p++) {
	for (jj = 0; jj < nToken; jj++) {
	    if (!apToken[jj] || !apToken[jj]->pOp) continue;
	    if (apToken[jj]->pOp->iPrecedence != p) continue;
	    if (apToken[jj]->pLeft) continue;

	    /* Find left operand */
	    for (iLeft = jj - 1; iLeft >= 0 && !apToken[iLeft]; iLeft--) {
		/* skip nulls */
	    }
	    /* Find right operand */
	    for (i = jj + 1; i < nToken && !apToken[i]; i++) {
		/* skip nulls */
	    }
	    /* Sequential single-condition guards for MC/DC.  See
	     * FINDINGS.md Finding 005. */
	    if (iLeft < 0) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, iLeft)) return TH8_ERROR;
	    if (i >= nToken) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, i)) return TH8_ERROR;
	    apToken[jj]->pLeft = apToken[iLeft];
	    apToken[jj]->pRight = apToken[i];
	    apToken[iLeft] = 0;
	    apToken[i] = 0;
	}
    }

    /*
     * Phase 4: Ternary ? : operator.
     * Find each '?', locate the matching ':', build:
     *   ? -> left=condition, right=(:)
     *   : -> left=true-branch, right=false-branch
     */

    for (jj = 0; jj < nToken; jj++) {
	if (!apToken[jj] || !apToken[jj]->pOp) continue;
	if (apToken[jj]->pOp->eOp != TH8_OP_TERNARY_Q) continue;
	if (apToken[jj]->pLeft) continue;

	{
	    int iQ = jj;
	    int iColon = -1;
	    int iCondition;
	    int iTrueBranch;
	    int iFalseBranch;

	    /*
	     * Find the condition (left of ?).
	     */

	    for (iCondition = iQ - 1; iCondition >= 0 && !apToken[iCondition];
	         iCondition--) {
		/* skip nulls */
	    }
	    if (iCondition < 0) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, iCondition)) return TH8_ERROR;

	    /*
	     * Find the true-branch (between ? and :).
	     */

	    for (iTrueBranch = iQ + 1;
	         iTrueBranch < nToken && !apToken[iTrueBranch];
	         iTrueBranch++) {
		/* skip nulls */
	    }
	    if (iTrueBranch >= nToken) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, iTrueBranch)) return TH8_ERROR;

	    /*
	     * Find the ':' operator.
	     */

	    for (iColon = iTrueBranch + 1;
	         iColon < nToken && !apToken[iColon]; iColon++) {
		/* skip nulls */
	    }
	    if (iColon >= nToken || !apToken[iColon]->pOp ||
	        apToken[iColon]->pOp->eOp != TH8_OP_TERNARY_C) {
		return TH8_ERROR;
	    }

	    /*
	     * Find the false-branch (after :).
	     */

	    for (iFalseBranch = iColon + 1;
	         iFalseBranch < nToken && !apToken[iFalseBranch];
	         iFalseBranch++) {
		/* skip nulls */
	    }
	    if (iFalseBranch >= nToken) return TH8_ERROR;
	    if (!TH8_ISTERM(apToken, iFalseBranch)) return TH8_ERROR;

	    /*
	     * Build the tree:
	     *   ':' node: left=true, right=false
	     *   '?' node: left=condition, right=':'
	     */

	    apToken[iColon]->pLeft = apToken[iTrueBranch];
	    apToken[iColon]->pRight = apToken[iFalseBranch];
	    apToken[iTrueBranch] = 0;
	    apToken[iFalseBranch] = 0;

	    apToken[iQ]->pLeft = apToken[iCondition];
	    apToken[iQ]->pRight = apToken[iColon];
	    apToken[iCondition] = 0;
	    apToken[iColon] = 0;
	}
    }

    /*
     * Phase 5 (TH8 extension, gated by TH8_EXPR_VAR_ASSIGN):
     * `:=` variable-assignment binding.
     *
     * Precedence 15 -- lower than ternary (14), so `:=` binds
     * AFTER the ternary tree has already been built.  This
     * means an expression like `c ? a := 1 : a := 2` parses as
     * `(c ? a : a) := (?:?)` ... actually no -- since `:=` is
     * lower precedence, the ternary captures `a` and `1` first,
     * leaving `:=` with the wrong operands.  This is the same
     * caveat C has, where `:=` (or `=`) inside a ternary needs
     * parentheses to do the obvious thing.  The recommended
     * idiom for users wanting per-branch assignment is
     * `c ? (a := 1) : (a := 2)`.
     *
     * RIGHT-ASSOCIATIVE: scanned right-to-left so chained
     * assignments group as `a := (b := (c := 1))`, mirroring C.
     * The scan direction (not the left/right operand search)
     * is what determines associativity here -- right-to-left
     * means the right-most operator binds first, capturing its
     * operands before any earlier operator can claim them.
     */

    for (jj = nToken - 1; jj >= 0; jj--) {
	if (!apToken[jj] || !apToken[jj]->pOp) continue;
	if (apToken[jj]->pOp->eOp != TH8_OP_VAR_ASSIGN) continue;
	if (apToken[jj]->pLeft) continue;

	/* Find left operand */
	for (iLeft = jj - 1; iLeft >= 0 && !apToken[iLeft]; iLeft--) {
	    /* skip nulls */
	}
	/* Find right operand */
	for (i = jj + 1; i < nToken && !apToken[i]; i++) {
	    /* skip nulls */
	}
	if (iLeft < 0) return TH8_ERROR;
	if (!TH8_ISTERM(apToken, iLeft)) return TH8_ERROR;
	if (i >= nToken) return TH8_ERROR;
	if (!TH8_ISTERM(apToken, i)) return TH8_ERROR;
	apToken[jj]->pLeft = apToken[iLeft];
	apToken[jj]->pRight = apToken[i];
	apToken[iLeft] = 0;
	apToken[i] = 0;
    }

    /*
     * Compact: the result should be in apToken[0] (or the
     * first non-null entry).  Phase 5 always leaves at least
     * one non-null token (the tree root), so the i < nToken
     * sub-check is a defensive invariant -- it could only
     * be F if every token were null, which would mean the
     * expression collapsed to nothing.  The outer
     * ALWAYS(i < nToken) at L2769 already documents that
     * post-condition for the compaction; mirror it here in
     * the skip-nulls loop.
     */

    for (i = 0; ALWAYS(i < nToken) && !apToken[i]; i++) {
	/* skip nulls */
    }
    if (i > 0 && ALWAYS(i < nToken)) {
	apToken[0] = apToken[i];
	apToken[i] = 0;
    }

    return TH8_OK;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Expr -- public entry point for expression evaluation
 *
 *	Evaluate a TH8 expression per the expr(n) reference at
 *	https://www.tcl-lang.org/man/tcl8.6/TclCmd/expr.htm.  Used
 *	by the [expr] command, by [if], [while], [for] (their
 *	condition arguments), by `[a]` math fast-paths, and by
 *	embedders calling the C API directly.
 *
 *	NOTE on quoting:
 *	  Per the expr(n) "EXAMPLES" / lazy-evaluation discussion,
 *	  the entire expression is normally passed inside `{...}`
 *	  in scripts -- e.g. `expr {$v + 1}`.  When that is done,
 *	  Tcl's script parser performs NO substitutions on the
 *	  brace-quoted argument (Tcl(n) Rule [6]) so the bytes that
 *	  reach this function are exactly the user-written
 *	  expression text, and substitution is performed (per
 *	  expr-grammar) by THIS function.  When the expression is
 *	  passed unbraced, Tcl's parser substitutes first; both
 *	  modes round-trip correctly because Th8_Expr never
 *	  re-substitutes already-substituted operands.
 *
 * Why / How:
 *	Orchestrates the three-phase expression pipeline:
 *	  1. th8ExprParse    -- tokenize per expr(n) "OPERANDS".
 *	  2. th8ExprMakeTree -- bind operators per expr(n)
 *	                        "OPERATORS" precedence table.
 *	  3. th8ExprEval     -- recursive tree evaluation with
 *	                        type coercion and lazy semantics.
 *
 *	After successful evaluation, non-decimal integer literals
 *	(hex 0x, octal 0o, binary 0b, legacy octal 0NNN) are
 *	normalised to decimal form so that the result is always a
 *	canonical decimal string.  expr(n) does not strictly
 *	require this -- but it ensures `[expr {0xff}]` returns
 *	"255" rather than "0xff", which is what scripts expect and
 *	what the Tcl reference implementation also does.
 *
 *	The token array and all nodes are cleaned up via
 *	th8ExprFree in a unified cleanup path, ensuring no leaks
 *	on error.
 *
 * Results:
 *	TH8_OK on success.  Interpreter result is the expression
 *	value.
 *
 * Side effects:
 *	May evaluate command substitutions (Tcl(n) Rule [7]) and
 *	access variables (Tcl(n) Rule [8]) when an operand is a
 *	double-quoted string, a `[command]`, or a `$name`.  Brace-
 *	quoted operands are kept verbatim (Rule [6]).  Allocates
 *	and frees the expression tree internally.
 *
 *----------------------------------------------------------------------
 */

/*
 *----------------------------------------------------------------------
 *
 * th8ExprEvalOne -- single-expression parse / build / eval
 *
 *	Parse, tree-build, and evaluate ONE expression.  This is
 *	the historical body of Th8_Expr, factored out so that
 *	Th8_Expr can wrap multiple sub-expressions when the
 *	TH8_EXPR_TOP_COMMA feature is enabled.
 *
 *	When TH8_EXPR_TOP_COMMA is disabled (the default / strict
 *	expr(n) mode), Th8_Expr calls this helper exactly once
 *	with the full input and the behaviour is identical to the
 *	previous monolithic implementation.
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprEvalOne(
    Th8_Interp *interp,
    const char *zExpr,
    size_t nExpr,
    const char *zName,
    size_t nName)
{
    Th8_ExprNode **apToken = 0;
    int nToken = 0;
    int rc;
    int i;

    rc = th8ExprParse(interp, zExpr, nExpr, &apToken, &nToken, zName, nName);
    if (rc != TH8_OK) {
	goto cleanup;
    }
    if (nToken == 0) {
	Th8_ErrorMessage(
	    interp, "syntax error in expression: \"", zExpr, nExpr);
	rc = TH8_ERROR;
	goto cleanup;
    }

    rc = th8ExprMakeTree(interp, apToken, nToken);
    if (rc != TH8_OK) {
	Th8_ErrorMessage(
	    interp, "syntax error in expression: \"", zExpr, nExpr);
	goto cleanup;
    }

    rc = th8ExprEval(interp, apToken[0], zName, nName);

    /*
     * Normalise the result: if the expression evaluated to a
     * non-decimal integer literal (e.g. 0xFF, 0o77, 0b1010),
     * convert it to decimal form.  This ensures that bare
     * integer literals always produce decimal output.
     */

    if (rc == TH8_OK) {
	th8_int64_t wVal;
	size_t nRes;
	const char *zRes = Th8_GetResult(interp, &nRes);

	/* Non-decimal integer literal detection refactored from a
	 * deeply-nested compound (clang MC/DC truth-table cap was
	 * exceeded) into a flag + sequential single-condition
	 * checks.  See FINDINGS.md Finding 005. */
	{
	    int looksLikeNonDec = 0;

	    if (nRes > 1) {
		if (zRes[0] == '0') {
		    char c = zRes[1];

		    if (c == 'x' || c == 'X')
			looksLikeNonDec = 1;
		    else if (c == 'o' || c == 'O')
			looksLikeNonDec = 1;
		    else if (c == 'b' || c == 'B')
			looksLikeNonDec = 1;
		    else if (c >= '0' && c <= '7')
			looksLikeNonDec = 1;
		    /* The original guard required nRes > 2 for the
		     * radix-prefix letters but allowed octal digit
		     * shape (zRes[1] in '0'..'7') even at nRes == 2.
		     * Preserve that: for nRes == 2, only the octal-
		     * digit case matters; clear the flag if a radix
		     * prefix was detected without a third byte. */
		    if (nRes == 2) {
			if (c < '0' || c > '7') looksLikeNonDec = 0;
		    }
		}
	    }
	    if (looksLikeNonDec) {
		if (Th8_ToWideInt(0, zRes, nRes, &wVal) == TH8_OK) {
		    Th8_SetResultWideInt(interp, wVal);
		}
	    }
	}
    }


cleanup:
    if (apToken) {
	for (i = 0; i < nToken; i++) {
	    th8ExprFree(interp, apToken[i]);
	}
	Th8_Free(interp, apToken);
    }
    return rc;
}


/*
 *----------------------------------------------------------------------
 *
 * th8ExprFindTopComma -- depth-aware top-level comma scanner
 *
 *	Find the byte offset of the next TOP-LEVEL `,` in zExpr,
 *	starting from iStart.  Returns 1 (and sets *piComma to the
 *	offset) when a comma is found at depth 0 of every nesting
 *	scope; returns 0 otherwise (with *piComma set to nExpr).
 *
 *	"Top level" here means: outside any `(...)`, `[...]`,
 *	`{...}`, or `"..."` scope.  Commas inside any of those are
 *	owned by the enclosed grammar -- function-call argument
 *	separators (Rule [7] command sub or expr(n)'s bare paren
 *	form), bytes literal in a brace-quoted operand (Rule [6]),
 *	bytes literal in a quoted operand (Rule [4]) -- and must
 *	NOT split the outer expression.
 *
 *	Backslash-skipping: any '\\' followed by another byte is
 *	consumed as a 2-byte unit so that `\,` is literal text and
 *	`\{` / `\}` / `\[` / `\]` / `\"` do not trigger nesting
 *	transitions.  This matches both the script-parser
 *	(th8NextWord/th8NextCommand) and the expression-parser
 *	conventions documented above.
 *
 *	Used ONLY when TH8_EXPR_TOP_COMMA is set; with the flag
 *	clear, Th8_Expr never invokes this scanner.
 *
 *----------------------------------------------------------------------
 */

static int
th8ExprFindTopComma(
    const char *zExpr,
    size_t nExpr,
    size_t iStart,
    size_t *piComma)
{
    size_t i = iStart;
    int parenDepth = 0;
    int bracketDepth = 0;
    int braceDepth = 0;
    int inQuote = 0;

    while (i < nExpr) {
	char c = zExpr[i];

	if (c == '\\' && i + 1 < nExpr) {
	    /* Backslash-X: consume as a 2-byte unit. */
	    i += 2;
	    continue;
	}

	if (inQuote) {
	    /*
	     * Inside `"..."`: only `"` ends the scope.  A `[` opens
	     * a command-sub sub-scope (its `]` content may include
	     * commas that must NOT be treated as top-level).
	     * Other characters are literal.
	     */
	    if (c == '"' && bracketDepth == 0) {
		inQuote = 0;
	    } else if (c == '[') {
		bracketDepth++;
	    } else if (c == ']' && bracketDepth > 0) {
		bracketDepth--;
	    }
	    i++;
	    continue;
	}

	switch (c) {
	case '"':
	    inQuote = 1;
	    break;
	case '(':
	    parenDepth++;
	    break;
	case ')':
	    if (parenDepth > 0) parenDepth--;
	    break;
	case '[':
	    bracketDepth++;
	    break;
	case ']':
	    if (bracketDepth > 0) bracketDepth--;
	    break;
	case '{':
	    braceDepth++;
	    break;
	case '}':
	    if (braceDepth > 0) braceDepth--;
	    break;
	case ',':
	    if (parenDepth == 0 && bracketDepth == 0 && braceDepth == 0) {
		*piComma = i;
		return 1;
	    }
	    break;
	default:
	    break;
	}
	i++;
    }

    *piComma = nExpr;
    return 0;
}


/*
 *----------------------------------------------------------------------
 *
 * Th8_Expr --
 *
 *	Public API: evaluate a TH8 expression and leave its
 *	result in the interpreter result.  Top-level entry
 *	point for `[expr]` and every other call site that
 *	parses an expression string (`[if]`, `[while]`, etc.).
 *
 *	Pipeline:
 *	  1. Validate `interp` and resolve `nExpr` if the
 *	     caller passed `TH8_NOLEN` (auto-detect length
 *	     via `Th8_Strlen`).
 *	  2. If the embedder has opted in to
 *	     `TH8_EXPR_TOP_COMMA` (the comma sequence
 *	     operator), pre-scan for top-level commas and
 *	     evaluate each comma-separated sub-expression in
 *	     turn, returning the last sub-expression's
 *	     result.
 *	  3. Otherwise hand the expression to the parser /
 *	     evaluator core, which builds a typed-value
 *	     result and stores it in the interpreter result.
 *
 *	`zName` / `nName` identify the expression's origin
 *	(used in error messages -- e.g. the surrounding
 *	command name and source line range -- so users can
 *	tell which `expr` call inside a nested script
 *	failed).
 *
 *	Gated on `TH8_ENABLE_EXPRESSIONS`.
 *
 * Parameters:
 *	interp -- live interpreter.
 *	zExpr  -- expression string (not necessarily
 *		NUL-terminated when `nExpr != TH8_NOLEN`).
 *	nExpr  -- expression length, or `TH8_NOLEN` to
 *		auto-detect.
 *	zName  -- origin name (NULL if unknown).
 *	nName  -- origin-name length.
 *
 * Returns:
 *	`TH8_OK` with the value in the interpreter result;
 *	`TH8_ERROR` on parse / type / overflow / canceled
 *	(interpreter result: diagnostic).
 *
 * Side effects:
 *	Sets the interpreter result.  May trigger arbitrary
 *	command substitution evaluation (`[...]`) which can
 *	itself have side effects.
 *
 *----------------------------------------------------------------------
 */
int
Th8_Expr(
    Th8_Interp *interp, /* Interpreter. */
    const char *zExpr, /* Expression string. */
    size_t nExpr, /* Expression length. */
    const char *zName, /* Origin name (NULL if unknown). */
    size_t nName) /* Origin name length. */
{
    if (!interp) return TH8_ERROR;
    if (nExpr == TH8_NOLEN) {
	nExpr = Th8_Strlen(interp, zExpr);
    }

    /*
     * Reject a tainted COMPLETE expression: expressions support command
     * substitution and can have side effects, so evaluating one built
     * from untrusted data is code execution.  (A clean expression may
     * still consume a tainted operand -- that operand's value flows
     * through without tainting the expression text itself.)
     */

    if (TH8_TAINTED(nExpr) &&
        Th8_ReportTaint(interp, "expression", zExpr, nExpr)) {
	return TH8_ERROR;
    }
    nExpr = TH8_LEN(nExpr);

    /*
     * TH8_EXPR_TOP_COMMA pre-split.
     *
     * When the embedder has opted in to the top-level comma
     * sequence operator, scan for top-level commas FIRST and, if
     * any are found, evaluate each comma-separated sub-expression
     * left-to-right.  The result of the LAST sub-expression
     * becomes the overall result.
     *
     * Earlier sub-expressions are evaluated for their side
     * effects only; their result strings are dropped on the
     * floor when the next sub-expression overwrites the
     * interpreter result.  A parse / eval error in any sub-
     * expression aborts the whole sequence with TH8_ERROR.
     *
     * When the flag is clear (the default / strict expr(n)
     * mode), this branch is never entered: any literal `,` at
     * top level reaches th8ExprEvalOne and falls through the
     * existing operand recogniser to "syntax error in
     * expression".  See coverage_expr_strict.tcl for the
     * regression tests that lock that behavior in.
     */

    if (Th8_GetExprFeatures(interp) & TH8_EXPR_TOP_COMMA) {
	size_t iStart = 0;
	size_t iComma;
	int rc = TH8_OK;
	int sawAny = 0;

	while (th8ExprFindTopComma(zExpr, nExpr, iStart, &iComma)) {
	    /*
	     * Evaluate this sub-expression (zExpr[iStart..iComma]).
	     * Empty substrings (e.g. from `expr {,1}` or `expr
	     * {1,,2}`) fall through to th8ExprEvalOne's nToken==0
	     * branch and produce "syntax error in expression",
	     * which aborts the whole sequence.
	     */
	    rc = th8ExprEvalOne(
	        interp, &zExpr[iStart], iComma - iStart, zName, nName);
	    if (rc != TH8_OK) return rc;
	    sawAny = 1;
	    iStart = iComma + 1;
	}

	if (sawAny) {
	    /*
	     * At least one top-level comma was found; evaluate the
	     * final (post-last-comma) sub-expression and return its
	     * result.  An empty trailing substring also errors via
	     * the empty-token path in th8ExprEvalOne.
	     */
	    return th8ExprEvalOne(
	        interp, &zExpr[iStart], nExpr - iStart, zName, nName);
	}
	/* Fall through to the single-expression path below. */
    }

    return th8ExprEvalOne(interp, zExpr, nExpr, zName, nName);
}


#endif /* TH8_ENABLE_EXPRESSIONS */
