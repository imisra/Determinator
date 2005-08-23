#if LAB >= 3

#include <inc/string.h>
#include <inc/lib.h>

void
cputchar(int ch)
{
	char s[2];

	// Unlike standard Unix's putchar,
	// the cputchar function _always_ outputs to the system console.
	// We do this in order to make debugging easier in JOS.

	s[0] = ch;
	s[1] = 0;
	sys_cputs(s);
}

int
getchar(void)
{
#if LAB >= 6
	unsigned char c;
	int r;

	// JOS does, however, support standard _input_ redirection,
	// allowing the user to redirect script files to the shell and such.
	// Thus, getchar() reads a character from file descriptor 0.
	r = read(0, &c, 1);
	if (r < 0)
		return r;
	if (r < 1)
		return -E_EOF;
	return c;
#else	// not LAB >= 6
	return sys_cgetc();
#endif	// not LAB >= 6
}


#if LAB >= 6
// "Real" console file descriptor implementation.
// The putchar/getchar functions above will still come here by default,
// but now can be redirected to files, pipes, etc., via the fd layer.

static int cons_read(struct Fd*, void*, size_t, off_t);
static int cons_write(struct Fd*, const void*, size_t, off_t);
static int cons_close(struct Fd*);
static int cons_stat(struct Fd*, struct Stat*);

struct Dev devcons =
{
	.dev_id =	'c',
	.dev_name =	"cons",
	.dev_read =	cons_read,
	.dev_write =	cons_write,
	.dev_close =	cons_close,
	.dev_stat =	cons_stat
};

int
iscons(int fdnum)
{
	int r;
	struct Fd *fd;

	if ((r = fd_lookup(fdnum, &fd)) < 0)
		return r;
	return fd->fd_dev_id == devcons.dev_id;
}

int
opencons(void)
{
	int r;
	struct Fd* fd;

	if ((r = fd_alloc(&fd)) < 0)
		return r;
	if ((r = sys_page_alloc(0, fd, PTE_P|PTE_U|PTE_W|PTE_SHARE)) < 0)
		return r;
	fd->fd_dev_id = devcons.dev_id;
	fd->fd_omode = O_RDWR;
	return fd2num(fd);
}

int
cons_read(struct Fd* fd, void* vbuf, size_t n, off_t offset)
{
	int c;

	USED(offset);

	if (n == 0)
		return 0;

	while ((c = sys_cgetc()) == 0)
		sys_yield();
	if (c < 0)
		return c;
	if (c == 0x04)	// ctl-d is eof
		return 0;
	*(char*)vbuf = c;
	return 1;
}

int
cons_write(struct Fd *fd, const void *vbuf, size_t n, off_t offset)
{
	int tot, m;
	char buf[128];

	USED(offset);

	// mistake: have to nul-terminate arg to sys_cputs, 
	// so we have to copy vbuf into buf in chunks and nul-terminate.
	for (tot = 0; tot < n; tot += m) {
		m = n - tot;
		if (m > sizeof(buf) - 1)
			m = sizeof(buf) - 1;
		memcpy(buf, (char*)vbuf + tot, m);
		buf[m] = 0;
		sys_cputs(buf);
	}
	return tot;
}

int
cons_close(struct Fd *fd)
{
	USED(fd);

	return 0;
}

int
cons_stat(struct Fd *fd, struct Stat *stat)
{
	strcpy(stat->st_name, "<cons>");
	return 0;
}

#endif	// LAB >= 6
#endif	// LAB >= 3
