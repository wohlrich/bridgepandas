#define PY_SSIZE_T_CLEAN
#include <Python.h>
#include <inttypes.h>
#include <vector>
#include <map>
#include <unordered_map>
#include <set>
#include "j128.h"
#include "jbdd.h"

#ifndef Py_IS_TYPE
#define Py_IS_TYPE(obj, type)	(Py_TYPE((obj)) == (type))
#endif

/*
 * These are now defined in jbdd.h
typedef int64_t bddref_t;
const bddref_t bdd_true = 1;
const bddref_t bdd_false = -1;

typedef int bddvar_t;
 */

struct BDD_TRIPLE {
    bddvar_t	vnum;
    bddref_t	avec;
    bddref_t	sans;

    BDD_TRIPLE(bddvar_t v, bddref_t a, bddref_t s) :
	vnum(v),avec(a),sans(s) {}

    bool operator==(const BDD_TRIPLE& o) const {
	return vnum == o.vnum && avec == o.avec && sans == o.sans;
    }
    bool operator<(const BDD_TRIPLE& o) const {
	if (vnum < o.vnum)
	    return true;
	if (vnum > o.vnum)
	    return false;

	if (avec < o.avec)
	    return true;
	if (avec > o.avec)
	    return false;

	if (sans < o.sans)
	    return true;
	if (sans > o.sans)
	    return false;

	return false;
    }
};

struct BDD_INFO {
    BDD_TRIPLE	trip;
    j128_t	pcount;
    j128_t	ncount;

    BDD_INFO(const BDD_TRIPLE& t) : trip(t),pcount(0),ncount(0) {}
    BDD_INFO(const BDD_INFO& i) :
	trip(i.trip),pcount(i.pcount),ncount(i.ncount) {}
    BDD_INFO& operator=(const BDD_INFO& i) {
	trip = i.trip;
	pcount = i.pcount;
	ncount = i.ncount;
	return *this;
    }
};


struct BDD_ITE {
    BDD_ITE(const BDD_ITE& o) : i(o.i),t(o.t),e(o.e) {}
    BDD_ITE(bddref_t _i, bddref_t _t, bddref_t _e) : i(_i),t(_t),e(_e) {}

    bddref_t i, t, e;

    bool operator==(const BDD_ITE& o) const {
	return i == o.i && t == o.t && e == o.e;
    };
    bool operator<(const BDD_ITE& o) const {
	if (i < o.i)
	    return true;
	else if (i > o.i)
	    return false;

	if (t < o.t)
	    return true;
	else if (t > o.t)
	    return false;

	if (e < o.e)
	    return true;
	else if (e > o.e)
	    return false;

	return false;
    }
};

template<typename T>
static void hash_combine(size_t& seed, const T& val) {
    seed ^= std::hash<T>{}(val) + 0x9e3779b9 + (seed << 6) + (seed >> 2);
}

namespace std {
    template<> struct hash<BDD_TRIPLE> {
        size_t operator()(const BDD_TRIPLE& t) const {
            size_t s = 0;
            hash_combine(s, (int64_t)t.vnum);
            hash_combine(s, t.avec);
            hash_combine(s, t.sans);
            return s;
        }
    };
    template<> struct hash<BDD_ITE> {
        size_t operator()(const BDD_ITE& t) const {
            size_t s = 0;
            hash_combine(s, t.i);
            hash_combine(s, t.t);
            hash_combine(s, t.e);
            return s;
        }
    };
}

typedef std::vector<BDD_INFO>                        BDD_INFO_VEC;
typedef std::unordered_map<BDD_TRIPLE, size_t>       BDD_TRIPLE_MAP;
typedef std::unordered_map<BDD_ITE, bddref_t>        BDD_ITE_MAP;

static
BDD_INFO_VEC& info_vector()
{
    static BDD_INFO_VEC iv;
    return iv;
}

static
BDD_TRIPLE_MAP& triple_map()
{
    static BDD_TRIPLE_MAP inm;
    return inm;
}

static
BDD_ITE_MAP& ite_map()
{
    static BDD_ITE_MAP im;
    return im;
}


bddref_t bdd_node(bddvar_t vnum, bddref_t avec, bddref_t sans)
{
    if (avec == sans)
	return avec;

    BDD_TRIPLE_MAP& tm = triple_map();

    BDD_TRIPLE rep_p(vnum, avec, sans);
    BDD_TRIPLE rep_n(vnum, -avec, -sans);
    BDD_TRIPLE_MAP::iterator f = tm.find(rep_p);
    if (f != tm.end())
	return f->second;

    f = tm.find(rep_n);
    if (f != tm.end())
	return -f->second;

    BDD_INFO info(rep_p);
    BDD_INFO_VEC& iv = info_vector();

    bddref_t two[] = { avec, sans };
    for (int i=0 ; i<2 ; i++)
    {
	bddref_t br = two[i];
	if (br == bdd_true) {
	    info.pcount += 1;
	} else if (br == bdd_false) {
	    info.ncount += 1;
	} else if (br > 0) {
	    info.pcount += iv[br-2].pcount;
	    info.ncount += iv[br-2].ncount;
	} else {
	    info.pcount += iv[-br-2].ncount;
	    info.ncount += iv[-br-2].pcount;
	}
    }

    bddref_t new_index = (bddref_t)(iv.size() + 2);
    iv.push_back(info);

    tm[rep_p] = new_index;
    return new_index;
}

bddref_t bdd_ite(bddref_t i, bddref_t t, bddref_t e)
{
    // Quick optimizations!
    if (t == e)
	return t;
    if (i == bdd_true)
	return t;
    if (i == bdd_false)
	return e;

    // No?  Look it up in the cache!!
    BDD_ITE_MAP& im = ite_map();
    BDD_ITE ite1(i, t, e);
    BDD_ITE ite2(i, -t, -e);

    BDD_ITE_MAP::iterator f;
    f = im.find(ite1);
    if (f != im.end())
	return f->second;

    f = im.find(ite2);
    if (f != im.end())
	return -f->second;

    // No?  Man, we have to do real work.
    BDD_INFO_VEC& biv = info_vector();
    int vnum, iavec, isans;
    if (i > 0) {
	const BDD_INFO& ii = biv[i-2];
	vnum = ii.trip.vnum;
	iavec = ii.trip.avec;
	isans = ii.trip.sans;
    } else {
	const BDD_INFO& ii = biv[-i-2];
	vnum = ii.trip.vnum;
	iavec = -ii.trip.avec;
	isans = -ii.trip.sans;
    }

    int tavec = t, tsans = t;
    if (t != bdd_true && t != bdd_false)
    {
	const BDD_INFO& ti = (t < 0 ? biv[-t-2] : biv[t-2]);
	if (ti.trip.vnum <= vnum) {
	    if (t < 0) {
		tavec = -ti.trip.avec;
		tsans = -ti.trip.sans;
	    } else {
		tavec = ti.trip.avec;
		tsans = ti.trip.sans;
	    }

	    if (ti.trip.vnum < vnum) {
		vnum = ti.trip.vnum;
		iavec = i;
		isans = i;
	    }
	}
    }

    int eavec = e, esans = e;
    if (e != bdd_true && e != bdd_false)
    {
	const BDD_INFO& ei = (e < 0 ? biv[-e-2] : biv[e-2]);
	if (ei.trip.vnum <= vnum) {
	    if (e < 0) {
		eavec = -ei.trip.avec;
		esans = -ei.trip.sans;
	    } else {
		eavec = ei.trip.avec;
		esans = ei.trip.sans;
	    }

	    if (ei.trip.vnum < vnum) {
		vnum = ei.trip.vnum;
		iavec = i;
		isans = i;
		tavec = t;
		tsans = t;
	    }
	}
    }

    bddref_t avec = bdd_ite(iavec, tavec, eavec);
    bddref_t sans = bdd_ite(isans, tsans, esans);

    bddref_t out = bdd_node(vnum, avec, sans);
    im[ite1] = out;
    return out;
}


typedef struct {
    PyObject_HEAD
    bddref_t	index;
} BDDObject;

static PyObject*
BDD_new(PyTypeObject* type, PyObject* args, PyObject* kwds)
{
    BDDObject* self = (BDDObject*) type->tp_alloc(type, 0);
    if (self != NULL) {
	self->index = 0;
    }
    return (PyObject*) self;
}

static int
BDD_init(BDDObject* self, PyObject* args, PyObject* kwds)
{
    int vnum;
    if (!PyArg_ParseTuple(args, "i", &vnum))
	return -1;

    self->index = bdd_node(vnum, bdd_true, bdd_false);
    return 0;
}


static Py_hash_t
BDD_hash(BDDObject* self)
{
    return (Py_hash_t)self->index;
}


static j128_t
bddref_pcount(bddref_t index)
{
    if (index == bdd_true) {
	return 1;
    } else if (index == bdd_false) {
	return 0;
    }

    BDD_INFO_VEC& iv = info_vector();
    if (index > 0)
	return iv[index-2].pcount;
    else
	return iv[-index-2].ncount;
}

static PyObject*
BDD_pcount(PyObject* self, PyObject* args)
{
    (void)args;
    BDDObject* bo = (BDDObject*) self;
    j128_t count = bddref_pcount(bo->index);
    return count.to_pylong();
}

static PyObject*
BDD_eval_pset(PyObject* self, PyObject* args)
{
    BDDObject* bo = (BDDObject*)self;
    PyObject* arg;
    if (!PyArg_ParseTuple(args, "O", &arg))
	return NULL;

    PyObject* iter = PyObject_GetIter(arg);
    if (iter == NULL)
	return NULL;

    std::set<bddvar_t> ones;
    PyObject* item = NULL;
    while ((item = PyIter_Next(iter))) {
	long val = PyLong_AsLong(item);
	if (val == -1) {
	    Py_DECREF(item);
	    Py_DECREF(iter);
	    return NULL;
	}
	ones.insert(val);
    }
    Py_DECREF(iter);
    if (PyErr_Occurred())
	return NULL;

    bddref_t cur = bo->index;
    BDD_INFO_VEC& iv = info_vector();
    int sanity = 0;
    while (true) {
	sanity++;
	if (sanity > 10000) {
	    PyErr_SetString(PyExc_RuntimeError, "We went insane");
	    return NULL;
	}
	if (cur == bdd_true)
	    return Py_True;
	else if (cur == bdd_false)
	    return Py_False;

	if (cur > 0) {
	    const BDD_INFO& bi = iv[cur-2];
	    if (ones.find(bi.trip.vnum) != ones.end()) {
		cur = bi.trip.avec;
	    } else {
		cur = bi.trip.sans;
	    }
	} else {
	    const BDD_INFO& bi = iv[-cur-2];
	    if (ones.find(bi.trip.vnum) != ones.end()) {
		cur = -bi.trip.avec;
	    } else {
		cur = -bi.trip.sans;
	    }
	}
    }
}


static PyObject*
BDD_get_pindex(PyObject* self, PyObject* args)
{
    BDDObject* bo = (BDDObject*)self;
    PyLongObject* obj = NULL;
    if (!PyArg_ParseTuple(args, "O!", &PyLong_Type, &obj))
	return NULL;

    j128_t index;
    if (index.from_pylong(obj) < 0)
	return NULL;

    PyObject* out_list = PyList_New(0);
    bddref_t cur = bo->index;
    BDD_INFO_VEC& iv = info_vector();

    while (true)
    {
	j128_t max_index = bddref_pcount(cur);
	if (index >= max_index || index < 0) {
	    Py_DECREF(out_list);
	    return PyErr_Format(PyExc_IndexError, "Index out of range");
	}

	if (cur == bdd_true)
	    break;

	if (cur > 0) {
	    const BDD_INFO& bi = iv[cur-2];

	    j128_t ac = bddref_pcount(bi.trip.avec);
	    if (index < ac) {
		if (PyList_Append(out_list, Py_BuildValue("i", bi.trip.vnum)) < 0) {
		    Py_DECREF(out_list);
		    return NULL;
		}
		cur = bi.trip.avec;
		continue;
	    } else {
		index -= ac;
		cur = bi.trip.sans;
		continue;
	    }
	} else {
	    const BDD_INFO& bi = iv[-cur-2];
	    j128_t ac = bddref_pcount(-bi.trip.avec);
	    if (index < ac) {
		if (PyList_Append(out_list, Py_BuildValue("i", bi.trip.vnum)) < 0) {
		    Py_DECREF(out_list);
		    return NULL;
		}
		cur = -bi.trip.avec;
		continue;
	    } else {
		index -= ac;
		cur = -bi.trip.sans;
		continue;
	    }
	}
    }

    return out_list;
}

static PyObject* BDD_invert(PyObject* self);
static int BDD_bool(PyObject* self);
static PyObject* BDD_and(PyObject* a, PyObject* b);
static PyObject* BDD_xor(PyObject* a, PyObject* b);
static PyObject* BDD_or(PyObject* a, PyObject* b);
static PyObject* BDD_sub(PyObject* a, PyObject* b);
static PyObject* BDD_richcompare(PyObject* a, PyObject* b, int op);
static PyObject* BDD_split(PyObject* self, PyObject* args);
static PyObject* BDD_false(PyObject* self, PyObject* args);
static PyObject* BDD_true(PyObject* self, PyObject* args);
static PyObject* BDD_thenelse(PyObject* self, PyObject* args);

static PyNumberMethods BDDNumberMethods = {
    .nb_subtract = BDD_sub,
    .nb_bool = BDD_bool,
    .nb_invert = BDD_invert,
    .nb_and = BDD_and,
    .nb_xor = BDD_xor,
    .nb_or = BDD_or,
};

static PyMethodDef BDDRegularMethods[] = {
    { "pcount", BDD_pcount, METH_NOARGS, "Return the number of satisfying variable sets" },
    { "get_pindex", BDD_get_pindex, METH_VARARGS, "Return a specific satisfying variable set" },
    { "eval_pset", BDD_eval_pset, METH_VARARGS, "Evaluate the BDD on a specific set of 1 bits" },
    { "split", BDD_split, METH_NOARGS, "Return either a bool for a constant, or a tuple of (vnum, pos_cofactor, neg_cofactor)" },
    { "false", BDD_false, METH_NOARGS | METH_STATIC, "Return the constant False BDD" },
    { "true", BDD_true, METH_NOARGS | METH_STATIC, "Return the constant True BDD" },
    { "thenelse", BDD_thenelse, METH_VARARGS, "Return a BDD for if (self) then (first arg) else (second arg)" },
    { NULL, NULL, 0, NULL },
};


static PyTypeObject BDDType = {
    PyVarObject_HEAD_INIT(NULL, 0)
    .tp_name = "bridgepandas.jbdd.BDD",
    .tp_basicsize = sizeof(BDDObject),
    .tp_itemsize = 0,
    .tp_as_number = &BDDNumberMethods,
    .tp_hash = (hashfunc) BDD_hash,
    .tp_flags = Py_TPFLAGS_DEFAULT,
    .tp_doc = PyDoc_STR("Binary Decision Diagram"),
    .tp_richcompare = (richcmpfunc) BDD_richcompare,
    .tp_methods = BDDRegularMethods,
    .tp_init = (initproc) BDD_init,
    .tp_new = BDD_new,
};

static PyObject*
bddref_to_pyobject(bddref_t bdd)
{
    BDDObject* out = PyObject_New(BDDObject, &BDDType);
    if (out != NULL)
	out->index = bdd;
    return (PyObject*)out;
}

#define CHECK_TYPE(obj) \
    if (!Py_IS_TYPE((obj), &BDDType)) { \
	PyErr_SetString(PyExc_TypeError, "Only BDD Objects accepted"); \
	return NULL; \
    }

static PyObject*
BDD_false(PyObject* a, PyObject* b)
{
    (void)a;
    (void)b;
    return bddref_to_pyobject(bdd_false);
}

static PyObject*
BDD_true(PyObject* a, PyObject* b)
{
    (void)a;
    (void)b;
    return bddref_to_pyobject(bdd_true);
}

static PyObject*
BDD_richcompare(PyObject* a, PyObject* b, int op)
{
    CHECK_TYPE(a);
    CHECK_TYPE(b);
    if (op != Py_NE && op != Py_EQ)
	return Py_NotImplemented;

    BDDObject* ba = (BDDObject*)a;
    BDDObject* bb = (BDDObject*)b;
    Py_RETURN_RICHCOMPARE(ba->index, bb->index, op);
}


static PyObject*
BDD_invert(PyObject* self)
{
    BDDObject* bo = (BDDObject*) self;
    return bddref_to_pyobject(-bo->index);
}


static int
BDD_bool(PyObject* self)
{
    BDDObject* bo = (BDDObject*) self;
    return bo->index != bdd_false;
}

static PyObject*
BDD_and(PyObject* a, PyObject* b)
{
    CHECK_TYPE(a);
    CHECK_TYPE(b);

    BDDObject* bdd_a = (BDDObject*)a;
    BDDObject* bdd_b = (BDDObject*)b;

    PyObject* out = bddref_to_pyobject(bdd_ite(bdd_a->index, bdd_b->index, bdd_false));
    return out;
}

static PyObject*
BDD_or(PyObject* a, PyObject* b)
{
    CHECK_TYPE(a);
    CHECK_TYPE(b);

    BDDObject* bdd_a = (BDDObject*)a;
    BDDObject* bdd_b = (BDDObject*)b;

    return bddref_to_pyobject(bdd_ite(bdd_a->index, bdd_true, bdd_b->index));
}

static PyObject*
BDD_xor(PyObject* a, PyObject* b)
{
    CHECK_TYPE(a);
    CHECK_TYPE(b);

    BDDObject* bdd_a = (BDDObject*)a;
    BDDObject* bdd_b = (BDDObject*)b;

    return bddref_to_pyobject(bdd_ite(bdd_a->index, -bdd_b->index, bdd_b->index));
}

static PyObject*
BDD_sub(PyObject* a, PyObject* b)
{
    CHECK_TYPE(a);
    CHECK_TYPE(b);

    BDDObject* bdd_a = (BDDObject*)a;
    BDDObject* bdd_b = (BDDObject*)b;

    return bddref_to_pyobject(bdd_ite(bdd_b->index, bdd_false, bdd_a->index));
}


static PyObject*
BDD_split(PyObject* self, PyObject* args)
{
    (void)args;
    BDDObject* bo = (BDDObject*) self;
    if (bo->index == bdd_true)
	Py_RETURN_TRUE;
    else if (bo->index == bdd_false)
	Py_RETURN_FALSE;
    else if (bo->index > 0) {
	const BDD_TRIPLE& trip = info_vector()[bo->index-2].trip;
	return Py_BuildValue("iNN", trip.vnum,
	    bddref_to_pyobject(trip.avec), bddref_to_pyobject(trip.sans));
    } else {
	const BDD_TRIPLE& trip = info_vector()[-bo->index-2].trip;
	return Py_BuildValue("iNN", trip.vnum,
	    bddref_to_pyobject(-trip.avec), bddref_to_pyobject(-trip.sans));
    }
}

static PyObject*
BDD_thenelse(PyObject* self, PyObject* args)
{
    BDDObject* bo = (BDDObject*)self;
    PyObject* pt;
    PyObject* pe;

    if (!PyArg_ParseTuple(args, "O!O!", &BDDType, &pt, &BDDType, &pe))
	return NULL;

    bddref_t i = bo->index;
    bddref_t t = ((BDDObject*)pt)->index;
    bddref_t e = ((BDDObject*)pe)->index;

    return bddref_to_pyobject(bdd_ite(i, t, e));
}

static PyObject*
jbdd_test(PyObject* self, PyObject* args)
{
    PyLongObject* obj = NULL;
    if (!PyArg_ParseTuple(args, "O!", &PyLong_Type, &obj))
	return NULL;

    j128_t foo;
    if (foo.from_pylong(obj) < 0)
	return NULL;

    PyObject* out = foo.to_pylong();
    return out;
}


static PyMethodDef jbdd_methods[] = {
    {"test", jbdd_test, METH_VARARGS, "Generic test method"},
    {NULL, NULL, 0, NULL}
};

static PyModuleDef jbdd_module = {
    PyModuleDef_HEAD_INIT,
    .m_name = "bridgepandas.jbdd",
    .m_doc = "Module implementing Binary Decision Diagrams",
    .m_size = -1,
    .m_methods = jbdd_methods,
};

PyMODINIT_FUNC
PyInit_jbdd(void)
{
    PyObject *m;
    if (PyType_Ready(&BDDType) < 0)
	return NULL;

    m = PyModule_Create(&jbdd_module);
    if (m == NULL)
	return NULL;

    Py_INCREF(&BDDType);
    if (PyModule_AddObject(m, "BDD", (PyObject*) &BDDType) < 0) {
	Py_DECREF(&BDDType);
	Py_DECREF(m);
	return NULL;
    }

    return m;
}
