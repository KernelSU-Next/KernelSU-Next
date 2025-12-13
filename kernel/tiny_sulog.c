// half assed ringbuffer
// 5 bytes
struct sulog_entry {
	uint8_t symbol;
	uint32_t uid; // mebbe u16?
} __attribute__((packed));

#define SULOG_ENTRY_MAX 100
#define SULOG_BUFSIZ SULOG_ENTRY_MAX * (sizeof (struct sulog_entry))

char sulog_buf[SULOG_BUFSIZ] = {0};
static void *sulog_buf_ptr = (void *)sulog_buf;
static uint8_t sulog_index_next = 0;

static DEFINE_SPINLOCK(sulog_lock);

void write_sulog(uint8_t sym)
{
	unsigned int offset = sulog_index_next * sizeof(struct sulog_entry);

	struct sulog_entry entry = {0};
	entry.symbol = sym;
	entry.uid = (uint32_t)current_uid().val;

	// pr_info("%s: symbol: %c uid: %d index: %d\n", __func__, entry.symbol, entry.uid, sulog_index_next);

	// pr_info("%s: addr: 0x%llx \n", __func__, (uintptr_t)(sulog_buf_ptr + offset));

	spin_lock(&sulog_lock);
	memcpy(sulog_buf_ptr + offset, &entry, sizeof(entry));
	spin_unlock(&sulog_lock);

	// move ptr for next iteration
	sulog_index_next = sulog_index_next + 1;

	if (sulog_index_next >= SULOG_ENTRY_MAX)
		sulog_index_next = 0;
}

struct sulog_entry_rcv_ptr {
	uint64_t int_ptr; // send index here
	uint64_t buf_ptr; // send buf here
};

void send_sulog_dump(void __user *uptr)
{
	struct sulog_entry_rcv_ptr sbuf = {0};

	if (copy_from_user(&sbuf, uptr, sizeof(sbuf) ))
		return;

	if (!sbuf.int_ptr)
		return;

	if (!sbuf.buf_ptr)
		return;

	if (copy_to_user((void __user *)sbuf.int_ptr, &sulog_index_next, sizeof(sulog_index_next) ))
		return;

	spin_lock(&sulog_lock);
	if (copy_to_user((void __user *)sbuf.buf_ptr, &sulog_buf, sizeof(sulog_buf) )) {
		spin_unlock(&sulog_lock);
		return;
	}
	spin_unlock(&sulog_lock);

}
