/* -*- c-basic-offset:4; c-indentation-style:"k&r"; indent-tabs-mode:nil -*- */

/**
 * @file
 *
 * Data structures for XML Query function definition and calls,
 * access functions for them and tree-walker to check for correct
 * function referencing.
 *
 * $Id$
 */

#include "func_chk.h"
#include "functions.h"

#include <stdlib.h>
/* variable argument list for func_add_var() */
#include <stdarg.h>  
#include <string.h>
#include <assert.h>

#include "types.h"

/* add a single user-defined function definition to the list */
static void add_ufun (PFpnode_t *n);

/* register a user-defined function */
static void fun_add_user (PFqname_t     qname, 
                          unsigned int  arity);


/**
 * Environment of functions known to Pathfinder.
 */
PFenv_t *PFfun_env = 0;


/* Print out all registered functions for debugging purposes */
#ifdef DEBUG_FUNCTIONS
static void print_functions (void);
#endif


/**
 * Count number of formal arguments to a user-defined function
 * (defined in abstract syntax tree node @a n).
 *
 * @param n The current @c p_params node; when called from outside,
 *   this is the topmost @c p_params node below the function
 *   declaration. (Can also be a @c p_nil node if no parameters are
 *   specified or the bottom is reached during recursion.)
 * @return number of formal arguments
 */
static unsigned int
formal_args (PFpnode_t *n)
{
    switch (n->kind) {
        case p_nil:
            return 0;
        case p_params:
            return 1 + formal_args (n->child[1]);
        default:
            PFoops_loc (OOPS_FATAL, n->loc,
                        "illegal node kind (expecting nil/params) "
                        "in %s:formal_args", __FILE__);
    }
}

/**
 * Count the number of actual arguments for the function call in abstract
 * syntax tree node @a n.
 *
 * @param n The current @c p_args node; when called from outside,
 *   this is the topmost @c p_args node below the function
 *   call. (Can also be a @c p_nil node if no parameters are
 *   specified or the bottom is reached during recursion.)
 * @return number of actual arguments
 */
static unsigned int
actual_args (PFpnode_t *n)
{
    switch (n->kind) {
        case p_nil:  
            return 0;

        case p_args:
            return 1 + actual_args (n->child[1]);

        default:     
            PFoops_loc (OOPS_FATAL, n->loc,
                        "illegal node kind (expecting nil/args)");
    }
}


/**
 * Register a user-defined function.
 */
static void
add_ufun (PFpnode_t *n)
{
    unsigned int arity;

    /* count formal function arguments */
    arity = formal_args (n->child[0]);

    fun_add_user (n->sem.qname, arity);
}

/**
 * Register all functions in the abstract syntax tree.
 *
 * Recursively walks down the function declarations and registers all
 * functions using #add_ufun.
 */
static void
add_ufuns (PFpnode_t *n)
{
    switch (n->kind) {
        case p_nil:
            /* stop recursion */
            return;

        case p_fun_decls:        
            /* add this function */
            add_ufun (n->child[0]);

            /* and recurse */
            return add_ufuns (n->child[1]);

        default:
            PFoops_loc (OOPS_FATAL, n->loc, "illegal parse tree node kind");
    }
}


/**
 * Traverse the whole abstract syntax tree and look for #p_fun_ref
 * nodes.  For each of them, determine the number of actual arguments,
 * lookup the function in the function environment #PFfun_env and see if
 * everything's ok. This function is recursive.
 *
 * @param n The current abstract syntax tree node.
 */
static void
check_fun_usage (PFpnode_t * n)
{
    unsigned int i;
    PFarray_t *funs;
    PFfun_t *fun;
    unsigned int arity;

    assert (n);

    /* process child nodes */
    for (i = 0; (i < PFPNODE_MAXCHILD) && (n->child[i]); i++)
        check_fun_usage (n->child[i]);
    
    /* if this is a function application node, see if it is in the list */
    switch (n->kind) {
    case p_fun_ref:
        
        funs = PFenv_lookup (PFfun_env, n->sem.qname);
        
        if (! funs)
            PFoops_loc (OOPS_APPLYERROR, n->loc,
                        "reference to undefined function `%s'", 
                        PFqname_str (n->sem.qname));
        
        fun = *((PFfun_t **) PFarray_at (funs, 0));

        /* Determine number of actual arguments */
        arity = actual_args (n->child[0]);
        
        /* see if number of actual argument matches function declaration */
        if (arity != fun->arity)
            PFoops_loc (OOPS_APPLYERROR, n->loc,
                        "wrong number of arguments for function `%s' "
                        "(expected %i, got %i)",
                        PFqname_str (fun->qname), fun->arity, arity);
        
        /*
         * Replace semantic value of abstract syntax tree node with pointer
         * to #PFfun_t struct. Tree node is now a ``real'' function application.
         */
        n->sem.fun = fun;
        n->kind = p_apply;
        
        break;
        
    case p_fun_decl:
        
        /*
         * For function declaration nodes, we replace the semantic content
         * by a pointer to the according PFfun_t struct.
         */
        funs = PFenv_lookup (PFfun_env, n->sem.qname);
        
        if (! funs) 
            PFoops_loc (OOPS_FATAL, n->loc,
                        "internal error: reference to undefined function `%s'", 
                        PFqname_str (n->sem.qname));
        
        fun = *((PFfun_t **) PFarray_at (funs, 0));

        n->sem.fun = fun;
        n->kind = p_fun;
        
        break;
        
    default:
        /* for all other cases, do nothing */
        break;
    }
}


#ifdef DEBUG_FUNCTIONS

/**
 * Print information about a single function for debugging purposes
 */
static void
print_fun (PFfun_t ** funs)
{
    PFfun_t *fun = *funs;
    unsigned int i = 0;

    fprintf (stderr, "function name: %s\n", PFqname_str (fun->qname));
    if (fun->builtin) {
        fprintf (stderr, "\treturn type  : %s\n", PFty_str (fun->ret_ty));
  
        for (i = 0; i < fun->arity; i++)
            fprintf (stderr, "\t%2i. parameter: %s\n", 
                     i + 1, 
                     PFty_str (*(fun->par_ty + i)));
    }
}

/**
 * Print list of registered functions for debugging purposes
 */
static void
print_functions (void)
{
    PFenv_iterate (PFfun_env, (void (*) (void *)) print_fun);
}
#endif   /* DEBUG_FUNCTIONS */

/**
 * Clear the list of available XQuery functions
 */
void
PFfun_clear (void)
{
    PFfun_env = 0;
}

/**
 * Register an XQuery function with the function environment #PFfun_env
 * 
 * @param  qn function name
 * @param  arity number of function arguments
 * @return status code
 */
static void
fun_add_user (PFqname_t     qn,
              unsigned int  arity)
{
    /* insert new entry into function list */
    if (PFenv_bind (PFfun_env, 
                    qn, 
                    (void *) PFfun_new (qn, 
                                        arity, 
                                        false, 
                                        0,
                                        0)))
        PFoops (OOPS_FUNCREDEF, "`%s'", PFqname_str (qn));
}

/**
 * Creates a new datastructure of type #PFfun_t that describes a
 * (user or built-in) function. Allocates memory for the function
 * and initializes the struct with the given values.
 * @param qn      qualified name of the function
 * @param arity   number of arguments
 * @param builtin Is this a built-in function or a user function?
 * @param par_tys array of formal parameter types. If parameter types
 *                are not known yet, pass @c NULL.
 * @param ret_ty  Pointer to return type. If return type is not known
 *                yet, pass @c NULL.
 * @return a pointer to the newly allocated struct
 */
PFfun_t *
PFfun_new (PFqname_t qn,
           unsigned int arity,
           bool builtin,
           PFty_t *par_tys,
           PFty_t *ret_ty)
{
    PFfun_t *n;

    n = (PFfun_t *) PFmalloc (sizeof (PFfun_t));

    n->qname   = qn;
    n->arity   = arity;
    n->builtin = builtin;

    /* copy array of formal parameter types (if present) */
    if (arity > 0 && par_tys) {
        n->par_ty = (PFty_t *) PFmalloc (arity * sizeof (PFty_t));
        memcpy (n->par_ty, par_tys, arity * sizeof (PFty_t));
    }
    else
        n->par_ty = 0;

    if (ret_ty)
        n->ret_ty  = *ret_ty;
    else
        n->ret_ty = PFty_untyped ();

    return n;
}

/**
 * Traverse the abstract syntax tree and check correct function usage.
 * Also generate a list of all XML Query functions available for this
 * XML Query expression.
 * @param root The root of the abstract syntax tree.
 * @return Status code
 */
void
PFfun_check (PFpnode_t * root)
{
    /*                 xquery
     *                  /  \
     *              prolog  ...
     *               /  \
     *             ...  fun_decls  
     */
    assert (root                             /* parse tree root */
            && root->child[0]                /* 'prolog' node   */
            && root->child[0]->child[1]);    /* 'fun_decls' or 'nil' */

    /* look for function definitions in the query prolog */
    add_ufuns (root->child[0]->child[1]);

#ifdef DEBUG_FUNCTIONS
    /*
     * For debugging, print out all functions that have been registered.
     */
    print_functions ();
#endif

    /* now traverse the whole tree and check all function usages */
    check_fun_usage (root);
}

/* vim:set shiftwidth=4 expandtab: */
