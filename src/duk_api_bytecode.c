/*
 *  Bytecode dump/load
 *
 *  The bytecode load primitive is more important performance-wise than the
 *  dump primitive.
 *
 *  Unlike most Duktape API calls, bytecode dump/load is not guaranteed to be
 *  memory safe for invalid arguments - caller beware!  There's little point
 *  in trying to achieve memory safety unless bytecode instructions are also
 *  validated which is not easy to do with indirect register references etc.
 */

/* FIXME: argument consistency */
/* FIXME: arg order for dumpers; ensure 'p' is in same slot so it maps to same reg */
/* FIXME: metadata: func name binding */
/* FIXME: metadata: formals */

#include "duk_internal.h"

#define DUK__SER_MARKER  0xff
#define DUK__SER_VERSION 0x00
#define DUK__SER_STRING  0x00
#define DUK__SER_NUMBER  0x01

/*
 *  Write/read macros for big endian, unaligned basic values.
 *
 *  These are done using mempcy to ensure they're valid even for unaligned
 *  reads/writes on platforms where alignment counts.  On x86 at least gcc
 *  is able to compile these into a bswap+mov.  Caller ensures there's enough
 *  space.
 */

/* FIXME: these should be shared with other code, e.g. JSON.  Add explicit
 * 'ptr' argument to macros, rename to DUK__WRITE_U32BE etc.
 */

/* FIXME: move these to a shared header */
#if defined(DUK_USE_INTEGER_LE)
#define DUK__HTON32(x) DUK_BSWAP32((x))
#define DUK__NTOH32(x) DUK_BSWAP32((x))
#define DUK__HTON16(x) DUK_BSWAP16((x))
#define DUK__NTOH16(x) DUK_BSWAP16((x))
#elif defined(DUK_USE_INTEGER_BE)
#define DUK__HTON32(x) (x)
#define DUK__NTOH32(x) (x)
#define DUK__HTON16(x) (x)
#define DUK__NTOH16(x) (x)
#else
#error internal error
#endif

#define DUK__WRITE_U8(ptr,val)  do { \
		*(ptr)++ = (duk_uint8_t) (val); \
	} while (0)
#define DUK__WRITE_U16(ptr,val) duk__write_u16(&(ptr), (duk_uint16_t) (val))
#define DUK__WRITE_U32(ptr,val) duk__write_u32(&(ptr), (duk_uint32_t) (val))
#define DUK__WRITE_DOUBLE(ptr,val) duk__write_double(&(ptr), (duk_double_t) (val))

#define DUK__READ_U8(ptr) ((duk_uint8_t) (*(ptr)++))
#define DUK__READ_U16(ptr) duk__read_u16(&(ptr));
#define DUK__READ_U32(ptr) duk__read_u32(&(ptr));
#define DUK__READ_DOUBLE(ptr) duk__read_double(&(ptr));

DUK_ALWAYS_INLINE
DUK_LOCAL duk_uint16_t duk__read_u16(duk_uint8_t **p) {
	union {
		duk_uint8_t b[2];
		duk_uint16_t x;
	} u;

	DUK_MEMCPY((void *) u.b, (const void *) (*p), 2);
	u.x = DUK__NTOH16(u.x);
	*p += 2;
	return u.x;
}

DUK_ALWAYS_INLINE
DUK_LOCAL duk_uint32_t duk__read_u32(duk_uint8_t **p) {
	union {
		duk_uint8_t b[4];
		duk_uint32_t x;
	} u;

	DUK_MEMCPY((void *) u.b, (const void *) (*p), 4);
	u.x = DUK__NTOH32(u.x);
	*p += 4;
	return u.x;
}

DUK_ALWAYS_INLINE
DUK_LOCAL duk_double_t duk__read_double(duk_uint8_t **p) {
	duk_double_union du;
	union {
		duk_uint8_t b[4];
		duk_uint32_t x;
	} u;

	DUK_MEMCPY((void *) u.b, (const void *) (*p), 4);
	u.x = DUK__NTOH32(u.x);
	du.ui[DUK_DBL_IDX_UI0] = u.x;
	DUK_MEMCPY((void *) u.b, (const void *) (*p + 4), 4);
	u.x = DUK__NTOH32(u.x);
	du.ui[DUK_DBL_IDX_UI1] = u.x;
	*p += 8;

	return du.d;
}

DUK_ALWAYS_INLINE
DUK_LOCAL void duk__write_u16(duk_uint8_t **p, duk_uint16_t val) {
	union {
		duk_uint8_t b[2];
		duk_uint16_t x;
	} u;

	u.x = DUK__HTON16(val);
	DUK_MEMCPY((void *) (*p), (const void *) u.b, 2);
	*p += 2;
}

DUK_ALWAYS_INLINE
DUK_LOCAL void duk__write_u32(duk_uint8_t **p, duk_uint32_t val) {
	union {
		duk_uint8_t b[4];
		duk_uint32_t x;
	} u;

	u.x = DUK__HTON32(val);
	DUK_MEMCPY((void *) (*p), (const void *) u.b, 4);
	*p += 4;
}

DUK_ALWAYS_INLINE
DUK_LOCAL void duk__write_double(duk_uint8_t **p, duk_double_t val) {
	duk_double_union du;
	union {
		duk_uint8_t b[4];
		duk_uint32_t x;
	} u;

	du.d = val;
	u.x = du.ui[DUK_DBL_IDX_UI0];
	u.x = DUK__HTON32(u.x);
	DUK_MEMCPY((void *) (*p), (const void *) u.b, 4);
	u.x = du.ui[DUK_DBL_IDX_UI1];
	u.x = DUK__HTON32(u.x);
	DUK_MEMCPY((void *) (*p + 4), (const void *) u.b, 4);
	*p += 8;
}

/*
 *  Other helpers
 */

DUK_LOCAL duk_uint8_t *duk__load_string(duk_context *ctx, duk_uint8_t *p) {
	duk_uint32_t len;

	len = DUK__READ_U32(p);
	duk_push_lstring(ctx, (const char *) p, len);
	p += len;
	return p;
}

DUK_LOCAL duk_uint8_t *duk__load_buffer(duk_context *ctx, duk_uint8_t *p) {
	duk_uint32_t len;
	duk_uint8_t *buf;

	len = DUK__READ_U32(p);
	buf = duk_push_fixed_buffer(ctx, (duk_size_t) len);
	DUK_ASSERT(buf != NULL);
	DUK_MEMCPY((void *) buf, (const void *) p, (size_t) len);
	p += len;
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_hstring(duk_uint8_t *p, duk_hstring *h) {
	duk_size_t len;
	duk_uint32_t tmp32;

	DUK_ASSERT(h != NULL);

	len = DUK_HSTRING_GET_BYTELEN(h);
	DUK_ASSERT(len <= 0xffffffffUL);  /* string limits */
	tmp32 = (duk_uint32_t) len;
	DUK__WRITE_U32(p, tmp32);
	DUK_MEMCPY((void *) p,
	           (const void *) DUK_HSTRING_GET_DATA(h),
	           len);
	p += len;
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_hbuffer(duk_hthread *thr, duk_uint8_t *p, duk_hbuffer *h) {
	duk_size_t len;
	duk_uint32_t tmp32;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(h != NULL);
	DUK_UNREF(thr);

	len = DUK_HBUFFER_GET_SIZE(h);
	DUK_ASSERT(len <= 0xffffffffUL);  /* buffer limits */
	tmp32 = (duk_uint32_t) len;
	DUK__WRITE_U32(p, tmp32);
	DUK_MEMCPY((void *) p,
	           (const void *) DUK_HBUFFER_GET_DATA_PTR(thr->heap, h),
	           len);
	p += len;
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_string_prop(duk_hthread *thr, duk_uint8_t *p, duk_bufwriter_ctx *bw_ctx, duk_hobject *func, duk_small_uint_t stridx) {
	duk_hstring *h_str;
	duk_tval *tv;

	tv = duk_hobject_find_existing_entry_tval_ptr(thr->heap, (duk_hobject *) func, thr->strs[stridx]);
	if (tv != NULL && DUK_TVAL_IS_STRING(tv)) {
		h_str = DUK_TVAL_GET_STRING(tv);
		DUK_ASSERT(h_str != NULL);
	} else {
		h_str = DUK_HTHREAD_STRING_EMPTY_STRING(thr);
		DUK_ASSERT(h_str != NULL);
	}
	p = DUK_BW_ENSURE(thr, bw_ctx, 4 + DUK_HSTRING_GET_BYTELEN(h_str), p);
	p = duk__dump_hstring(p, h_str);
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_buffer_prop(duk_hthread *thr, duk_uint8_t *p, duk_bufwriter_ctx *bw_ctx, duk_hobject *func, duk_small_uint_t stridx) {
	duk_tval *tv;

	tv = duk_hobject_find_existing_entry_tval_ptr(thr->heap, (duk_hobject *) func, thr->strs[stridx]);
	if (tv != NULL && DUK_TVAL_IS_BUFFER(tv)) {
		duk_hbuffer *h_buf;
		h_buf = DUK_TVAL_GET_BUFFER(tv);
		DUK_ASSERT(h_buf != NULL);
		p = DUK_BW_ENSURE(thr, bw_ctx, 4 + DUK_HBUFFER_GET_SIZE(h_buf), p);
		p = duk__dump_hbuffer(thr, p, h_buf);
	} else {
		p = DUK_BW_ENSURE(thr, bw_ctx, 4, p);
		DUK__WRITE_U32(p, 0);
	}
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_uint32_prop(duk_hthread *thr, duk_uint8_t *p, duk_bufwriter_ctx *bw_ctx, duk_hobject *func, duk_small_uint_t stridx, duk_uint32_t def_value) {
	duk_tval *tv;
	duk_uint32_t val;

	tv = duk_hobject_find_existing_entry_tval_ptr(thr->heap, (duk_hobject *) func, thr->strs[stridx]);
	if (tv != NULL && DUK_TVAL_IS_NUMBER(tv)) {
		val = (duk_uint32_t) DUK_TVAL_GET_NUMBER(tv);
	} else {
		val = def_value;
	}
	p = DUK_BW_ENSURE(thr, bw_ctx, 4, p);
	DUK__WRITE_U32(p, val);
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_varmap(duk_hthread *thr, duk_uint8_t *p, duk_bufwriter_ctx *bw_ctx, duk_hobject *func) {
	duk_tval *tv;

	tv = duk_hobject_find_existing_entry_tval_ptr(thr->heap, (duk_hobject *) func, DUK_HTHREAD_STRING_INT_VARMAP(thr));
	if (tv != NULL && DUK_TVAL_IS_OBJECT(tv)) {
		duk_hobject *h;
		duk_uint_fast32_t i;

		h = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);

		/* We know _Varmap only has own properties so walk property
		 * table directly.  We also know _Varmap is dense and all
		 * values are numbers; assert for these.  GC and finalizers
		 * shouldn't affect _Varmap so side effects should be fine.
		 */
		for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ENEXT(h); i++) {
			duk_hstring *key;
			duk_tval *tv_val;
			duk_uint32_t val;

			key = DUK_HOBJECT_E_GET_KEY(thr->heap, h, i);
			DUK_ASSERT(key != NULL);  /* _Varmap is dense */
			DUK_ASSERT(!DUK_HOBJECT_E_SLOT_IS_ACCESSOR(thr->heap, h, i));
			tv_val = DUK_HOBJECT_E_GET_VALUE_TVAL_PTR(thr->heap, h, i);
			DUK_ASSERT(tv_val != NULL);
			DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv_val));  /* known to be number; in fact an integer */
			val = (duk_uint32_t) DUK_TVAL_GET_NUMBER(tv_val);  /* FIXME: fastint */

			DUK_BW_ENSURE(thr, bw_ctx, 4 + DUK_HSTRING_GET_BYTELEN(key) + 4, p);
			p = duk__dump_hstring(p, key);
			DUK__WRITE_U32(p, val);
		}
	}
	p = DUK_BW_ENSURE(thr, bw_ctx, 4, p);
	DUK__WRITE_U32(p, 0);  /* end of _Varmap */
	return p;
}

DUK_LOCAL duk_uint8_t *duk__dump_formals(duk_hthread *thr, duk_uint8_t *p, duk_bufwriter_ctx *bw_ctx, duk_hobject *func) {
	duk_tval *tv;

	tv = duk_hobject_find_existing_entry_tval_ptr(thr->heap, (duk_hobject *) func, DUK_HTHREAD_STRING_INT_FORMALS(thr));
	if (tv != NULL && DUK_TVAL_IS_OBJECT(tv)) {
		duk_hobject *h;
		duk_uint_fast32_t i;

		h = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h != NULL);

		/* We know _Formals is dense and all entries will be in the
		 * array part.  GC and finalizers shouldn't affect _Formals
		 * so side effects should be fine.
		 */
		for (i = 0; i < (duk_uint_fast32_t) DUK_HOBJECT_GET_ASIZE(h); i++) {
			duk_tval *tv_val;
			duk_hstring *varname;

			tv_val = DUK_HOBJECT_A_GET_VALUE_PTR(thr->heap, h, i);
			DUK_ASSERT(tv_val != NULL);
			if (DUK_TVAL_IS_STRING(tv_val)) {
				/* Array is dense and contains only strings, but ASIZE may
				 * be larger than used part and there are UNUSED entries.
				 */
				varname = DUK_TVAL_GET_STRING(tv_val);
				DUK_ASSERT(varname != NULL);

				DUK_BW_ENSURE(thr, bw_ctx, 4 + DUK_HSTRING_GET_BYTELEN(varname), p);
				p = duk__dump_hstring(p, varname);
			}
		}
	}
	p = DUK_BW_ENSURE(thr, bw_ctx, 4, p);
	DUK__WRITE_U32(p, 0);  /* end of _Formals */
	return p;
}

static duk_uint8_t *duk__dump_func(duk_context *ctx, duk_hcompiledfunction *func, duk_bufwriter_ctx *bw_ctx, duk_uint8_t *p) {
	duk_hthread *thr;
	duk_tval *tv, *tv_end;
	duk_instr_t *ins, *ins_end;
	duk_hobject **fn, **fn_end;
	duk_hstring *h_str;
	duk_uint32_t count_instr;
	duk_uint32_t tmp32;
	duk_uint16_t tmp16;
	duk_double_t d;

	thr = (duk_hthread *) ctx;
	DUK_UNREF(ctx);
	DUK_UNREF(thr);

	DUK_D(DUK_DPRINT("dumping function %p to %p: "
	                 "consts=[%p,%p[ (%ld bytes, %ld items), "
	                 "funcs=[%p,%p[ (%ld bytes, %ld items), "
	                 "code=[%p,%p[ (%ld bytes, %ld items)",
	                 (void *) func,
	                 (void *) p,
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(thr->heap, func),
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_CONSTS_END(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_CONSTS_SIZE(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_CONSTS_COUNT(thr->heap, func),
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(thr->heap, func),
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_FUNCS_END(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_FUNCS_SIZE(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_FUNCS_COUNT(thr->heap, func),
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_CODE_BASE(thr->heap, func),
	                 (void *) DUK_HCOMPILEDFUNCTION_GET_CODE_END(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_CODE_SIZE(thr->heap, func),
	                 (long) DUK_HCOMPILEDFUNCTION_GET_CODE_COUNT(thr->heap, func)));

	count_instr = (duk_uint32_t) DUK_HCOMPILEDFUNCTION_GET_CODE_COUNT(thr->heap, func);
	p = DUK_BW_ENSURE(thr, bw_ctx, 3 * 4 + 2 * 2 + 3 * 4 + count_instr * 4, p);

	/* Fixed header info. */
	tmp32 = count_instr;
	DUK__WRITE_U32(p, tmp32);
	tmp32 = (duk_uint32_t) DUK_HCOMPILEDFUNCTION_GET_CONSTS_COUNT(thr->heap, func);
	DUK__WRITE_U32(p, tmp32);
	tmp32 = (duk_uint32_t) DUK_HCOMPILEDFUNCTION_GET_FUNCS_COUNT(thr->heap, func);
	DUK__WRITE_U32(p, tmp32);
	tmp16 = func->nregs;
	DUK__WRITE_U16(p, tmp16);
	tmp16 = func->nargs;
	DUK__WRITE_U16(p, tmp16);
#if defined(DUK_USE_DEBUGGER_SUPPORT)
	tmp32 = func->start_line;
	DUK__WRITE_U32(p, tmp32);
	tmp32 = func->end_line;
	DUK__WRITE_U32(p, tmp32);
#else
	DUK__WRITE_U32(p, 0);
	DUK__WRITE_U32(p, 0);
#endif
	tmp32 = ((duk_heaphdr *) func)->h_flags & DUK_HEAPHDR_FLAGS_FLAG_MASK;
	DUK__WRITE_U32(p, tmp32);

	/* Bytecode instructions: endian conversion needed unless
	 * platform is big endian.
	 */
	ins = DUK_HCOMPILEDFUNCTION_GET_CODE_BASE(thr->heap, func);
	ins_end = DUK_HCOMPILEDFUNCTION_GET_CODE_END(thr->heap, func);
	DUK_ASSERT((duk_size_t) (ins_end - ins) == (duk_size_t) count_instr);
#if defined(DUK_USE_INTEGER_BE)
	DUK_MEMCPY((void *) p, (const void *) ins, (size_t) (ins_end - ins));
	p += (size_t) (ins_end - ins);
#else
	while (ins != ins_end) {
		tmp32 = (duk_uint32_t) (*ins);
		DUK__WRITE_U32(p, tmp32);
		ins++;
	}
#endif

	/* Constants: variable size encoding. */
	tv = DUK_HCOMPILEDFUNCTION_GET_CONSTS_BASE(thr->heap, func);
	tv_end = DUK_HCOMPILEDFUNCTION_GET_CONSTS_END(thr->heap, func);
	while (tv != tv_end) {
		/* constants are strings or numbers now */
		DUK_ASSERT(DUK_TVAL_IS_STRING(tv) ||
		           DUK_TVAL_IS_NUMBER(tv));

		if (DUK_TVAL_IS_STRING(tv)) {
			h_str = DUK_TVAL_GET_STRING(tv);
			DUK_ASSERT(h_str != NULL);
			p = DUK_BW_ENSURE(thr, bw_ctx, 1 + 4 + DUK_HSTRING_GET_BYTELEN(h_str), p),
			*p++ = DUK__SER_STRING;
			p = duk__dump_hstring(p, h_str);
		} else {
			DUK_ASSERT(DUK_TVAL_IS_NUMBER(tv));
			p = DUK_BW_ENSURE(thr, bw_ctx, 1 + 8, p);
			*p++ = DUK__SER_NUMBER;
			d = DUK_TVAL_GET_NUMBER(tv);
			DUK__WRITE_DOUBLE(p, d);
		}
		tv++;
	}

	/* Inner functions recursively. */
	fn = (duk_hobject **) DUK_HCOMPILEDFUNCTION_GET_FUNCS_BASE(thr->heap, func);
	fn_end = (duk_hobject **) DUK_HCOMPILEDFUNCTION_GET_FUNCS_END(thr->heap, func);
	while (fn != fn_end) {
		/* FIXME: This causes recursion up to inner function depth
		 * which is normally not an issue, e.g. mark-and-sweep uses
		 * a recursion limiter to avoid C stack issues.  Avoiding
		 * this would mean some sort of a work list.
		 */
		DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION(*fn));
		p = duk__dump_func(ctx, (duk_hcompiledfunction *) *fn, bw_ctx, p);
		fn++;
	}

	/* Object extra properties.
	 *
	 * There are some difference between function templates and functions.
	 * For example, function templates don't have .length and nargs is
	 * normally used to instantiate the functions.
	 */

	p = duk__dump_uint32_prop(thr, p, bw_ctx, (duk_hobject *) func, DUK_STRIDX_LENGTH, (duk_uint32_t) func->nargs);
	p = duk__dump_string_prop(thr, p, bw_ctx, (duk_hobject *) func, DUK_STRIDX_NAME);
	p = duk__dump_string_prop(thr, p, bw_ctx, (duk_hobject *) func, DUK_STRIDX_FILE_NAME);
	p = duk__dump_buffer_prop(thr, p, bw_ctx, (duk_hobject *) func, DUK_STRIDX_INT_PC2LINE);
	p = duk__dump_varmap(thr, p, bw_ctx, (duk_hobject *) func);
	p = duk__dump_formals(thr, p, bw_ctx, (duk_hobject *) func);

	DUK_D(DUK_DPRINT("serialized function %p -> final pointer %p", (void *) func, (void *) p));

	return p;
}

/* Load a function from bytecode.  The function object returned here must
 * match what is created by duk_js_push_closure() with respect to its flags,
 * properties, etc.
 *
 * NOTE: there are intentionally no input buffer length / bound checks.
 * Adding them would be easy but wouldn't ensure memory safety as untrusted
 * or broken bytecode is unsafe during execution unless the opcodes themselves
 * are validated (which is quite complex, especially for indirect opcodes).
 */

#define DUK__ASSERT_LEFT(n) do { \
		DUK_ASSERT((duk_size_t) (p_end - p) >= (duk_size_t) (n)); \
	} while (0)

static duk_uint8_t *duk__load_func(duk_context *ctx, duk_uint8_t *p, duk_uint8_t *p_end) {
	duk_hthread *thr;
	duk_hcompiledfunction *h_fun;
	duk_hbuffer *h_data;
	duk_size_t data_size;
	duk_uint32_t count_instr, count_const, count_funcs;
	duk_uint32_t n;
	duk_uint32_t tmp32;
	duk_small_uint_t const_type;
	duk_uint8_t *fun_data;
	duk_uint8_t *q;
	duk_idx_t idx_base;
	duk_tval *tv;
	duk_uarridx_t arr_idx;

	DUK_ASSERT(ctx != NULL);
	thr = (duk_hthread *) ctx;

	DUK_D(DUK_DPRINT("loading function, p=%p, p_end=%p", (void *) p, (void *) p_end));

	DUK__ASSERT_LEFT(3 * 4);
	count_instr = DUK__READ_U32(p);
	count_const = DUK__READ_U32(p);
	count_funcs = DUK__READ_U32(p);

	data_size = sizeof(duk_tval) * count_const +
	            sizeof(duk_hobject *) * count_funcs +
	            sizeof(duk_instr_t) * count_instr;

	DUK_D(DUK_DPRINT("instr=%ld, const=%ld, funcs=%ld, data_size=%ld",
	                 (long) count_instr, (long) count_const,
	                 (long) count_const, (long) data_size));

	/* Value stack is used to ensure reachability of constants and
	 * inner functions being loaded.  Require enough space to handle
	 * large functions correctly.
	 */
	duk_require_stack(ctx, 2 + count_const + count_funcs);
	idx_base = duk_get_top(ctx);

	/* Push function object, init flags etc.  This must match
	 * duk_js_push_closure() quite carefully.
	 */
	duk_push_compiledfunction(ctx);
	h_fun = duk_get_hcompiledfunction(ctx, -1);
	DUK_ASSERT(h_fun != NULL);
	DUK_ASSERT(DUK_HOBJECT_IS_COMPILEDFUNCTION((duk_hobject *) h_fun));
	DUK_ASSERT(DUK_HCOMPILEDFUNCTION_GET_DATA(thr->heap, h_fun) == NULL);
	DUK_ASSERT(DUK_HCOMPILEDFUNCTION_GET_FUNCS(thr->heap, h_fun) == NULL);
	DUK_ASSERT(DUK_HCOMPILEDFUNCTION_GET_BYTECODE(thr->heap, h_fun) == NULL);

	h_fun->nregs = DUK__READ_U16(p);
	h_fun->nargs = DUK__READ_U16(p);
#if defined(DUK_USE_DEBUGGER_SUPPORT)
	h_fun->start_line = DUK__READ_U32(p);
	h_fun->end_line = DUK__READ_U32(p);
#else
	p += 8;  /* skip line info */
#endif

	/* duk_hcompiledfunction flags; quite version specific */
	tmp32 = DUK__READ_U32(p);
	DUK_HEAPHDR_SET_FLAGS((duk_heaphdr *) h_fun, tmp32);

	/* standard prototype */
	DUK_HOBJECT_SET_PROTOTYPE_UPDREF(thr, &h_fun->obj, thr->builtins[DUK_BIDX_FUNCTION_PROTOTYPE]);

	/* assert just a few critical flags */
	DUK_ASSERT(DUK_HEAPHDR_GET_TYPE((duk_heaphdr *) h_fun) == DUK_HTYPE_OBJECT);
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(&h_fun->obj));
	DUK_ASSERT(DUK_HOBJECT_HAS_COMPILEDFUNCTION(&h_fun->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_NATIVEFUNCTION(&h_fun->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_THREAD(&h_fun->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_EXOTIC_ARRAY(&h_fun->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_EXOTIC_STRINGOBJ(&h_fun->obj));
	DUK_ASSERT(!DUK_HOBJECT_HAS_EXOTIC_ARGUMENTS(&h_fun->obj));

	/* Create function 'data' buffer but don't attach it yet. */
	fun_data = (duk_uint8_t *) duk_push_fixed_buffer(ctx, data_size);
	DUK_ASSERT(fun_data != NULL);

	/* Load bytecode instructions. */
	DUK_ASSERT(sizeof(duk_instr_t) == 4);
	DUK__ASSERT_LEFT(count_instr * sizeof(duk_instr_t));
#if defined(DUK_USE_INTEGER_BE)
	q = fun_data + sizeof(duk_tval) * count_const + sizeof(duk_hobject *) * count_funcs;
	DUK_MEMCPY((void *) q,
	           (const void *) p,
	           sizeof(duk_instr_t) * count_instr);
	p += sizeof(duk_instr_t) * count_instr;
#else
	q = fun_data + sizeof(duk_tval) * count_const + sizeof(duk_hobject *) * count_funcs;
	for (n = count_instr; n > 0; n--) {
		*((duk_instr_t *) q) = DUK__READ_U32(p);
		q += sizeof(duk_instr_t);
	}
#endif

	/* Load constants onto value stack but don't yet copy to buffer. */
	for (n = count_const; n > 0; n--) {
		DUK__ASSERT_LEFT(1);
		const_type = DUK__READ_U8(p);
		switch (const_type) {
		case DUK__SER_STRING: {
			p = duk__load_string(ctx, p);
			break;
		}
		case DUK__SER_NUMBER: {
			duk_double_t val;
			DUK__ASSERT_LEFT(8);
			val = DUK__READ_DOUBLE(p);
			duk_push_number(ctx, val);
			break;
		}
		default: {
			goto format_error;
		}
		}
	}

	/* Load inner functions to value stack, but don't yet copy to buffer. */
	for (n = count_funcs; n > 0; n--) {
		p = duk__load_func(ctx, p, p_end);
		if (p == NULL) {
			goto format_error;
		}
	}

	/* With constants and inner functions on value stack, we can now
	 * atomically finish the function 'data' buffer, bump refcounts,
	 * etc.
	 *
	 * Here we take advantage of the value stack being just a duk_tval
	 * array: we can just memcpy() the constants as long as we incref
	 * them afterwards.
	 */

	h_data = (duk_hbuffer *) duk_get_hbuffer(ctx, idx_base + 1);
	DUK_ASSERT(h_data != NULL);
	DUK_ASSERT(!DUK_HBUFFER_HAS_DYNAMIC(h_data));
	DUK_HCOMPILEDFUNCTION_SET_DATA(thr->heap, h_fun, h_data);
	DUK_HBUFFER_INCREF(thr, h_data);

	tv = duk_get_tval(ctx, idx_base + 2);  /* may be NULL if no constants or inner funcs */
	DUK_ASSERT((count_const == 0 && count_funcs == 0) || tv != NULL);
	q = fun_data;
	DUK_MEMCPY((void *) q, (const void *) tv, sizeof(duk_tval) * count_const);  /* FIXME: avoid NULL for zero size? */
	for (n = count_const; n > 0; n--) {
		DUK_TVAL_INCREF_FAST(thr, (duk_tval *) q);  /* no side effects */
		q += sizeof(duk_tval);
	}
	tv += count_const;

	DUK_HCOMPILEDFUNCTION_SET_FUNCS(thr->heap, h_fun, (duk_hobject **) q);
	for (n = count_funcs; n > 0; n--) {
		duk_hobject *h_obj;

		DUK_ASSERT(DUK_TVAL_IS_OBJECT(tv));
		h_obj = DUK_TVAL_GET_OBJECT(tv);
		DUK_ASSERT(h_obj != NULL);
		tv++;
		DUK_HOBJECT_INCREF(thr, h_obj);

		*((duk_hobject **) q) = h_obj;
		q += sizeof(duk_hobject *);
	}

	DUK_HCOMPILEDFUNCTION_SET_BYTECODE(thr->heap, h_fun, (duk_instr_t *) q);

	/* The function object is now reachable and refcounts are fine,
	 * so we can pop off all the temporaries.
	 */
	DUK_D(DUK_DPRINT("function is reachable, reset top; func: %!iT", duk_get_tval(ctx, idx_base)));
	duk_set_top(ctx, idx_base + 1);

	/* Setup function properties. */
	tmp32 = DUK__READ_U32(p);
	duk_push_u32(ctx, tmp32);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_LENGTH, DUK_PROPDESC_FLAGS_NONE);

	p = duk__load_string(ctx, p);
	if (DUK_HOBJECT_HAS_NAMEBINDING((duk_hobject *) h_fun)) {
		/* Original function instance/template had NAMEBINDING.
		 * Must create a lexical environment on loading to allow
		 * recursive functions like 'function foo() { foo(); }'.
		 */
		duk_hobject *proto;

		proto = thr->builtins[DUK_BIDX_GLOBAL_ENV];
		(void) duk_push_object_helper_proto(ctx,
		                                    DUK_HOBJECT_FLAG_EXTENSIBLE |
		                                    DUK_HOBJECT_CLASS_AS_FLAGS(DUK_HOBJECT_CLASS_DECENV),
		                                    proto);
		duk_dup(ctx, -2);                                 /* -> [ func funcname env funcname ] */
		duk_dup(ctx, idx_base);                           /* -> [ func funcname env funcname func ] */
		duk_xdef_prop(ctx, -3, DUK_PROPDESC_FLAGS_NONE);  /* -> [ func funcname env ] */
		duk_xdef_prop_stridx(ctx, idx_base, DUK_STRIDX_INT_LEXENV, DUK_PROPDESC_FLAGS_WC);
		/* since closure has NEWENV, never define DUK_STRIDX_INT_VARENV, as it
		 * will be ignored anyway
		 */
	}
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_NAME, DUK_PROPDESC_FLAGS_NONE);

	p = duk__load_string(ctx, p);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_FILE_NAME, DUK_PROPDESC_FLAGS_WC);

	/* FIXME: extract helper, share with duk_js_closure() */
	duk_push_object(ctx);
	duk_dup(ctx, -2);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_CONSTRUCTOR, DUK_PROPDESC_FLAGS_WC);  /* func.prototype.constructor = func */
	duk_compact(ctx, -1);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_PROTOTYPE, DUK_PROPDESC_FLAGS_W);

	p = duk__load_buffer(ctx, p);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_INT_PC2LINE, DUK_PROPDESC_FLAGS_WC);

	duk_push_object(ctx);  /* _Varmap */
	for (;;) {
		/* FIXME: awkward */
		p = duk__load_string(ctx, p);
		if (duk_get_length(ctx, -1) == 0) {
			duk_pop(ctx);
			break;
		}
		tmp32 = DUK__READ_U32(p);
		duk_push_u32(ctx, tmp32);
		duk_put_prop(ctx, -3);
	}
	duk_compact(ctx, -1);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_INT_VARMAP, DUK_PROPDESC_FLAGS_NONE);

	duk_push_array(ctx);  /* _Formals */
	for (arr_idx = 0; ; arr_idx++) {
		/* FIXME: awkward */
		p = duk__load_string(ctx, p);
		if (duk_get_length(ctx, -1) == 0) {
			duk_pop(ctx);
			break;
		}
		duk_put_prop_index(ctx, -2, arr_idx);
	}
	duk_compact(ctx, -1);
	duk_xdef_prop_stridx(ctx, -2, DUK_STRIDX_INT_FORMALS, DUK_PROPDESC_FLAGS_NONE);

	/* Return with final function pushed on stack top. */
	DUK_D(DUK_DPRINT("final loaded function: %!iT", duk_get_tval(ctx, -1)));
	DUK_ASSERT_TOP(ctx, idx_base + 1);
	return p;

 format_error:
	return NULL;
}

duk_int_t duk_dump_function(duk_context *ctx) {
	duk_hthread *thr;
	duk_hcompiledfunction *func;
	duk_hbuffer_dynamic *h_buf;
	duk_bufwriter_ctx bw_ctx_alloc;
	duk_bufwriter_ctx *bw_ctx = &bw_ctx_alloc;
	duk_uint8_t *p;

	DUK_ASSERT(ctx != NULL);
	thr = (duk_hthread *) ctx;

	/* Bound functions don't have all properties so we'd either need to
	 * lookup the non-bound target function or reject bound functions.
	 * For now, bound functions are rejected.
	 */
	func = duk_require_hcompiledfunction(ctx, -1);
	DUK_ASSERT(func != NULL);
	DUK_ASSERT(!DUK_HOBJECT_HAS_BOUND(&func->obj));

	/* Estimating the result size beforehand would be costly, so
	 * start with a reasonable size and extend as needed.
	 */
	(void) duk_push_dynamic_buffer(ctx, 1024);
	h_buf = (duk_hbuffer_dynamic *) duk_get_hbuffer(ctx, -1);
	DUK_ASSERT(h_buf != NULL);
	DUK_BW_INIT(thr, bw_ctx, h_buf);
	p = DUK_BW_GETPTR(thr, bw_ctx);
	*p++ = DUK__SER_MARKER;
	*p++ = DUK__SER_VERSION;
	p = duk__dump_func(ctx, func, bw_ctx, p);
	DUK_BW_FINISH(thr, bw_ctx, p);
	DUK_BW_COMPACT(thr, bw_ctx);

	DUK_D(DUK_DPRINT("serialized result: %!T", duk_get_tval(ctx, -1)));

	duk_remove(ctx, -2);  /* [ ... func buf ] -> [ ... buf ] */

	return 0;  /* FIXME: return type? */
}

duk_int_t duk_load_function(duk_context *ctx) {
	duk_hthread *thr;
	duk_uint8_t *p_buf, *p, *p_end;
	duk_size_t sz;

	DUK_ASSERT(ctx != NULL);
	thr = (duk_hthread *) ctx;
	DUK_UNREF(ctx);

	p_buf = (duk_uint8_t *) duk_require_buffer(ctx, -1, &sz);
	DUK_ASSERT(p_buf != NULL);

	/* The caller is responsible for being sure that bytecode being loaded
	 * is valid and trusted.  Invalid bytecode can cause memory unsafe
	 * behavior directly during loading or later during bytecode execution
	 * (instruction validation would be quite complex to implement).
	 *
	 * This signature check is the only sanity check for detecting
	 * accidental invalid inputs.  The initial 0xFF byte ensures no
	 * ordinary string will be accepted by accident.
	 */
	p = p_buf;
	p_end = p_buf + sz;
	if (sz < 2 || p[0] != DUK__SER_MARKER || p[1] != DUK__SER_VERSION) {
		goto format_error;
	}
	p += 2;

	p = duk__load_func(ctx, p, p_end);
	if (p == NULL) {
		goto format_error;
	}

	duk_remove(ctx, -2);  /* [ ... buf func ] -> [ ... func ] */
	return 0;  /* FIXME: return type? */

 format_error:
	DUK_ERROR(thr, DUK_ERR_TYPE_ERROR, "invalid format");  /* FIXME: string */
	return 0;
}

#undef DUK__SER_MARKER
#undef DUK__SER_VERSION
#undef DUK__SER_STRING
#undef DUK__SER_NUMBER
#undef DUK__HTON32
#undef DUK__NTOH32
#undef DUK__HTON16
#undef DUK__NTOH16
#undef DUK__WRITE_U8
#undef DUK__WRITE_U16
#undef DUK__WRITE_U32
#undef DUK__WRITE_DOUBLE
#undef DUK__READ_U8
#undef DUK__READ_U16
#undef DUK__READ_U32
#undef DUK__READ_DOUBLE
