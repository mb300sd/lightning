#include "../io_lock.c"
#include <ccan/io/io.h>
#include <ccan/short_types/short_types.h>
#include <common/utils.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <stdio.h>
#include <wire/wire.h>

/* AUTOGENERATED MOCKS START */
/* Generated stub for amount_sat_add */
 bool amount_sat_add(struct amount_sat *val UNNEEDED,
				       struct amount_sat a UNNEEDED,
				       struct amount_sat b UNNEEDED)
{ fprintf(stderr, "amount_sat_add called!\n"); abort(); }
/* Generated stub for amount_sat_eq */
bool amount_sat_eq(struct amount_sat a UNNEEDED, struct amount_sat b UNNEEDED)
{ fprintf(stderr, "amount_sat_eq called!\n"); abort(); }
/* Generated stub for amount_sat_sub */
 bool amount_sat_sub(struct amount_sat *val UNNEEDED,
				       struct amount_sat a UNNEEDED,
				       struct amount_sat b UNNEEDED)
{ fprintf(stderr, "amount_sat_sub called!\n"); abort(); }
/* Generated stub for fromwire_fail */
const void *fromwire_fail(const u8 **cursor UNNEEDED, size_t *max UNNEEDED)
{ fprintf(stderr, "fromwire_fail called!\n"); abort(); }
/* AUTOGENERATED MOCKS END */

#define num_writers 10
#define num_writes 10

struct read_state {
	int pos;

	/* What have we read from the funnel end? Should be
	 * num_writers sets of num_writes consecutive identical
	 * numbers */
	u8 reads[num_writers*num_writes];

	/* All tasks reading from upstream writers will serialize on this */
	struct io_lock *lock;
};

/* The read context per connection */
struct reader_state {
	struct read_state *read_state;

	/* Where are we reading from? */
	struct io_conn *upstream;
	u8 buf;

	int count;
};

struct write_state {
	int count;
	u8 id;
};

static struct io_plan *writer(struct io_conn *conn, struct write_state *ws)
{
	if (ws->count++ == num_writes)
		return io_close(conn);
	return io_write(conn, &ws->id, 1, writer, ws);
}

static struct io_plan *reader(struct io_conn *conn, struct reader_state *reader_state)
{
	struct read_state *rs = reader_state->read_state;
	rs->reads[rs->pos] = reader_state->buf;
	rs->pos++;
	reader_state->count++;

	if (reader_state->count == num_writes) {
		io_lock_release(reader_state->read_state->lock);
		return io_close(conn);
	} else {
		return io_read(conn, &reader_state->buf, 1, reader, reader_state);
	}
}

static struct io_plan *reader_start(struct io_conn *conn, struct reader_state *reader_state)
{
	return io_read(conn, &reader_state->buf, 1, reader, reader_state);
}

static struct io_plan *reader_locked(struct io_conn *conn, struct reader_state *rs)
{
	return io_lock_acquire_in(conn, rs->read_state->lock, reader_start, rs);
}

/*
 * Creates a fan-in funnel from num_writers socketpairs into a single
 * reader
 *
 *   writers
 *    \\|//
 *   reader
 */
static bool test_multi_write(const tal_t *ctx)
{
	struct write_state ws[num_writers];
	struct read_state sink;
	struct reader_state rs[num_writers];
	int fds[2];

	sink.pos = 0;
	sink.lock = io_lock_new(ctx);
	memset(&sink.reads, 0, sizeof(sink.reads));

	for (size_t i=0; i<num_writers; i++) {
		socketpair(AF_LOCAL, SOCK_STREAM, 0, fds);

		rs[i].read_state = &sink;
		rs[i].count = 0;
		rs[i].buf = -1;

		ws[i].id = (u8)(i+'a');
		ws[i].count = 0;

		rs[i].upstream = io_new_conn(ctx, fds[1], writer, &ws[i]);
		io_new_conn(ctx, fds[0], reader_locked, &rs[i]);
	}
	io_loop(NULL, NULL);

	/* Now check that we were serialized correctly, i.e., num_writers sets of num_writes identical numbers */
	for (size_t i=0; i<num_writers; i++) {
		for (size_t j=1; j<num_writes; j++)
			if (sink.reads[i*num_writes+j] != sink.reads[i*num_writes+j-1])
				return false;
	}

	return true;
}

int main(void)
{
	bool ok = true;
	setup_locale();
	setup_tmpctx();
	ok &= test_multi_write(tmpctx);
	tal_free(tmpctx);
	return !ok;
}
