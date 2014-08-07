/*
 *  Fast buffer writer with spare management.
 */

#include "duk_internal.h"

DUK_INTERNAL void duk_bw_init(duk_hthread *thr, duk_bufwriter_ctx *bw_ctx, duk_hbuffer_dynamic *h_buf) {
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(bw_ctx != NULL);
	DUK_ASSERT(h_buf != NULL);
	DUK_UNREF(thr);

	bw_ctx->offset = 0;
	bw_ctx->length = DUK_HBUFFER_DYNAMIC_GET_SIZE(h_buf);
	bw_ctx->limit = (duk_uint8_t *) DUK_HBUFFER_DYNAMIC_GET_DATA_PTR(thr->heap, h_buf) + bw_ctx->length;
	bw_ctx->buf = h_buf;
}

/* Get current write pointer.  After this you must call duk_bufwriter_ensure()
 * to start writing.
 */
DUK_INTERNAL duk_uint8_t *duk_bw_getptr(duk_hthread *thr, duk_bufwriter_ctx *bw_ctx) {
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(bw_ctx != NULL);
	DUK_UNREF(thr);

	return (duk_uint8_t *) DUK_HBUFFER_DYNAMIC_GET_DATA_PTR(thr->heap, bw_ctx->buf) + bw_ctx->offset;
}

/* Resize target buffer for requested size.  Called by the macro only when the
 * fast path test (= there is space) fails.
 */
DUK_INTERNAL duk_uint8_t *duk_bw_resize(duk_hthread *thr, duk_bufwriter_ctx *bw_ctx, duk_size_t sz, duk_uint8_t *ptr) {
	duk_size_t offset;
	duk_size_t new_sz;
	duk_uint8_t *base;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(bw_ctx != NULL);
	DUK_ASSERT(ptr != NULL);

	/* 'offset' intentionally not updated to bw_ctx->offset until finish. */

	offset = (duk_size_t) (ptr - (duk_uint8_t *) DUK_HBUFFER_DYNAMIC_GET_DATA_PTR(thr->heap, bw_ctx->buf));
	new_sz = offset + sz + 1024;  /* FIXME: spare handling code here */

	DUK_D(DUK_DPRINT("resize bufferwriter to %ld", (long) new_sz));

	duk_hbuffer_resize(thr, bw_ctx->buf, new_sz, new_sz);
	bw_ctx->length = new_sz;
	base = (duk_uint8_t *) DUK_HBUFFER_DYNAMIC_GET_DATA_PTR(thr->heap, bw_ctx->buf);
	bw_ctx->limit = base + bw_ctx->length;
	return base + offset;
}

/* Finish writing for now, updates bw_ctx->offset. */
DUK_INTERNAL void duk_bw_finish(duk_hthread *thr, duk_bufwriter_ctx *bw_ctx, duk_uint8_t *ptr) {
	duk_size_t offset;

	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(bw_ctx != NULL);
	DUK_ASSERT(ptr != NULL);
	DUK_UNREF(thr);

	offset = (duk_size_t) (ptr - (duk_uint8_t *) DUK_HBUFFER_DYNAMIC_GET_DATA_PTR(thr->heap, bw_ctx->buf));
	bw_ctx->offset = offset;
}

/* Make buffer compact; caller must call duk_bw_finish() first to update bw_ctx->offset. */
DUK_INTERNAL void duk_bw_compact(duk_hthread *thr, duk_bufwriter_ctx *bw_ctx) {
	DUK_ASSERT(thr != NULL);
	DUK_ASSERT(bw_ctx != NULL);
	DUK_UNREF(thr);

	duk_hbuffer_resize(thr, bw_ctx->buf, bw_ctx->offset, bw_ctx->offset);
}
