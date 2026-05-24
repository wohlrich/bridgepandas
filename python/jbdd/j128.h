#ifndef _U128_H_
#define _U128_H_

/*
 * A quick and dirty 128 bit integer type which supports only
 * assignment, addition,  and subtraction
 */

#include <stdint.h>
#include <Python.h>

struct j128_t
{
    enum { DEC_STR_LEN = 40 };
    uint64_t lo, hi;

    j128_t() : lo(0),hi(0) {}
    j128_t(const j128_t& j) : lo(j.lo),hi(j.hi) {}
    j128_t(uint64_t u) : lo(u),hi(0) {}

    j128_t& operator=(const j128_t& x) {
	lo = x.lo;
	hi = x.hi;
	return *this;
    }
    bool operator==(const j128_t& x) const {
	return lo == x.lo && hi == x.hi;
    }
    bool operator!=(const j128_t& x) const {
	return lo != x.lo || hi != x.hi;
    }
#define CMPOP(op) \
    bool operator op (const j128_t& x) const {	\
	if (hi == x.hi)				\
	    return lo op x.lo;			\
	else					\
	    return hi op x.hi;			\
    }

    CMPOP(<=)
    CMPOP(>=)
    CMPOP(<)
    CMPOP(>)

    j128_t& operator+=(const j128_t x) {
	bool carry = (x.lo > UINT64_MAX - lo);
	lo += x.lo;
	hi += x.hi;
	if (carry)
	    hi += 1;
	return *this;
    }

    j128_t operator+(const j128_t x) const {
	j128_t out(*this);
	return out += x;
    }

    j128_t& operator-=(const j128_t x) {
	bool borrow = (x.lo > lo);
	lo -= x.lo;
	hi -= x.hi;
	if (borrow)
	    hi -= 1;
	return *this;
    }
    j128_t operator-(const j128_t x) const {
	j128_t out(*this);
	return out -= x;
    }

    j128_t mul10() const;
    j128_t div10(int& rem) const;

    const char* to_string(char buf[DEC_STR_LEN]) const;
    static j128_t from_string(const char buf[DEC_STR_LEN]);

    int from_pylong(PyLongObject* obj);
    PyObject* to_pylong() const;
};


#endif // _U128_H_

