/*
 * th8_expr.h --
 *
 *	Internal header for the [expr] subsystem.  Houses the
 *	non-function declarations (operator codes, argument-type
 *	codes, the operator and parser-node structs, the math-
 *	function registry entry, and parser helper macros) used
 *	exclusively by th8_expr.c.  This file is NOT part of the
 *	public API.
 *
 *	Pulling these declarations out of th8_expr.c keeps the
 *	function bodies and their per-function header comments
 *	uninterrupted by long type-and-macro blocks.
 *
 * Copyright (c) 2026 by Joe Mistachkin.  All rights reserved.
 *
 * See the file "license.terms" for information on usage and redistribution of
 * this file, and for a DISCLAIMER OF ALL WARRANTIES.
 */

#ifndef TH8_EXPR_H
#define TH8_EXPR_H

/*
 *======================================================================
 *
 * Operator codes (TH8_OP_*).
 *
 * One per operator the [expr] tokenizer recognises.  Used both as
 * the eOp tag in Th8_Operator and as the dispatch tag inside the
 * evaluator.  Values are stable; gaps (e.g. 23 between BITWISE_XOR
 * and BITWISE_OR) reflect historical reservations and SHOULD be
 * preserved when adding new operators.
 *
 *======================================================================
 */

#define TH8_OP_UNARY_MINUS   2
#define TH8_OP_UNARY_PLUS    3
#define TH8_OP_BITWISE_NOT   4
#define TH8_OP_LOGICAL_NOT   5
#define TH8_OP_MULTIPLY      6
#define TH8_OP_DIVIDE        7
#define TH8_OP_MODULUS       8
#define TH8_OP_ADD           9
#define TH8_OP_SUBTRACT      10
#define TH8_OP_LEFT_SHIFT    11
#define TH8_OP_RIGHT_SHIFT   12
#define TH8_OP_LT            13
#define TH8_OP_GT            14
#define TH8_OP_LE            15
#define TH8_OP_GE            16
#define TH8_OP_EQ            17
#define TH8_OP_NE            18
#define TH8_OP_SEQ           19
#define TH8_OP_SNE           20
#define TH8_OP_BITWISE_AND   21
#define TH8_OP_BITWISE_XOR   22
#define TH8_OP_BITWISE_OR    24
#define TH8_OP_LOGICAL_AND   25
#define TH8_OP_LOGICAL_OR    26
#define TH8_OP_OPEN_BRACKET  27
#define TH8_OP_CLOSE_BRACKET 28
#define TH8_OP_EXPONENT      29
#define TH8_OP_IN            30  /* in (list membership) */
#define TH8_OP_NI            31  /* ni (not in list) */
#define TH8_OP_TERNARY_Q     32  /* ? (ternary condition) */
#define TH8_OP_TERNARY_C     33  /* : (ternary separator) */
#define TH8_OP_VAR_ASSIGN                                                    \
    34  /* := variable assignment
				     * (TH8 extension; gated by
				     * TH8_EXPR_VAR_ASSIGN) */

/*
 * Argument-type tags (TH8_ARG_*).
 *
 * Stamped into Th8_Operator.eArgType to drive type coercion of the
 * operator's operands at evaluation time, per expr(n)'s per-operator
 * type rules.
 */

#define TH8_ARG_NONE    0
#define TH8_ARG_INTEGER 1
#define TH8_ARG_NUMBER  2
#define TH8_ARG_STRING  3

/*
 *======================================================================
 *
 * Th8_MathFuncEntry --
 *
 *	Per-interpreter registry entry for an [expr] math function
 *	(e.g. abs(x), pow(x,y)).  Stored in the math-function hash
 *	keyed by function name.
 *
 *======================================================================
 */

typedef struct Th8_MathFuncEntry {
    Th8_MathFuncProc xProc; /* Implementation callback. */
    void *pCtx;   /* User context. */
    int nArg;   /* Expected argument count (0, 1, 2). */
} Th8_MathFuncEntry;

/*
 *======================================================================
 *
 * Th8_Operator --
 *
 *	One row of the operator table th8Operators[].  The table is
 *	searched linearly during tokenization; rows whose nReqFlag
 *	bits are not all set in interp->nExprFeatures are skipped
 *	(used to gate opt-in TH8 extensions like := assignment).
 *
 *	iPrecedence: lower number = tighter binding; -1 for ( and ).
 *	eArgType:    type coercion rule per expr(n) per-operator types.
 *
 *======================================================================
 */

typedef struct Th8_Operator Th8_Operator;
struct Th8_Operator {
    const char *zOp;  /* Operator text (e.g., "**", "<="). */
    int nOp;   /* Byte length of zOp. */
    int eOp;   /* Operator code (TH8_OP_*). */
    int iPrecedence;  /* Precedence level (1=unary, 2-13=
				 * binary, 14=ternary, 15=:= assign,
				 * -1=parens). */
    int eArgType;  /* Required operand type:
				 * TH8_ARG_INTEGER=int only,
				 * TH8_ARG_NUMBER=int or double,
				 * TH8_ARG_STRING=raw bytes. */
    int nReqFlag;  /* Bitmask of TH8_EXPR_* flags this
				 * operator requires; 0 means always
				 * available (strict expr(n)).  When
				 * non-zero the row is skipped during
				 * tokenization unless every set bit is
				 * also set in interp->nExprFeatures. */
};

/*
 *======================================================================
 *
 * Th8_ExprNode --
 *
 *	One node in the parse tree built by th8ExprMakeTree.  Each
 *	node is either an operator (pOp != NULL) or a literal
 *	(pOp == NULL, zValue holds the literal text).  pParent is
 *	currently unused but reserved.
 *
 *======================================================================
 */

typedef struct Th8_ExprNode Th8_ExprNode;
struct Th8_ExprNode {
    Th8_Operator *pOp;  /* Operator (NULL = literal node). */
    Th8_ExprNode *pParent; /* Parent node (currently unused;
				 * reserved for future use). */
    Th8_ExprNode *pLeft; /* Left operand (unary: the operand;
				 * binary: left-hand side;
				 * ternary '?': the condition). */
    Th8_ExprNode *pRight; /* Right operand (binary: right-hand
				 * side; ternary '?': the ':' node). */
    char *zValue;  /* Literal text (owned; freed by
				 * th8ExprFree).  May contain $, [, ",
				 * {} -- substituted at eval time. */
    size_t nValue;  /* Byte length of zValue. */
};

/*
 * TH8_ISTERM(ap, x) --
 *	Test whether the parse-token at index x is a "term" -- i.e.
 *	either a literal node, or an operator that already has its
 *	left child set (so it has been incorporated into the tree).
 *	Used by th8ExprMakeTree's shift-reduce loop.
 */

#define TH8_ISTERM(ap, x) ((ap)[x] && (!(ap)[x]->pOp || (ap)[x]->pLeft))

#endif /* TH8_EXPR_H */
