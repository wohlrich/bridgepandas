#ifndef _JBDD_H_
#define _JBDD_H_

typedef int64_t bddref_t;
const bddref_t bdd_true = 1;
const bddref_t bdd_false = -1;
typedef int bddvar_t;

bddref_t bdd_node(bddvar_t vnum, bddref_t avec, bddref_t sans);
bddref_t bdd_ite(bddref_t i, bddref_t t, bddref_t e);

#endif // _JBDD_H_
