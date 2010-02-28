#if LAB >= 4
// Basic user-space file and I/O support functions,
// used by the standard I/O functions in stdio.c.

#include <inc/file.h>
#include <inc/stat.h>
#include <inc/stdio.h>
#include <inc/dirent.h>
#include <inc/string.h>
#include <inc/assert.h>
#include <inc/syscall.h>
#include <inc/errno.h>
#include <inc/mmu.h>


////////// File inode functions //////////

int
fileino_alloc(void)
{
	int i;
	for (i = FILEINO_GENERAL; i < FILE_INODES; i++)
		if (files->fi[i].nlink == 0)
			return i;

	warn("freopen: no free inodes\n");
	errno = ENOSPC;
	return -1;
}

void fileino_take(int ino)
{
	assert(fileino_isvalid(ino));

	files->fi[ino].nlink++;
	assert(files->fi[ino].nlink > 0);
}

void fileino_drop(int ino)
{
	assert(fileino_isvalid(ino));

	assert(files->fi[ino].nlink > 0);
	if (--files->fi[ino].nlink == 0)
		fileino_truncate(ino, 0);	// delete the unreferenced file
}

ssize_t
fileino_write(int ino, off_t ofs, const void *buf, int len)
{
	assert(fileino_exists(ino));
	assert(ofs >= 0);
	assert(len >= 0);

	fileinode *fi = &files->fi[ino];
	assert(fi->size <= FILE_MAXSIZE);

	// Return an error if we'd be growing the file too big.
	size_t lim = ofs + len;
	if (lim < ofs || lim > FILE_MAXSIZE) {
		errno = EFBIG;
		return -1;
	}

	// Grow the file as necessary.
	if (lim > fi->size) {
		size_t oldpagelim = ROUNDUP(fi->size, PAGESIZE);
		size_t newpagelim = ROUNDUP(lim, PAGESIZE);
		if (newpagelim > oldpagelim)
			sys_get(SYS_PERM | SYS_READ | SYS_WRITE, 0, NULL, NULL,
				FILEDATA(ino) + oldpagelim,
				newpagelim - oldpagelim);
		fi->size = lim;
	}

	// Write the data.
	memmove(FILEDATA(ino) + ofs, buf, len);
	return len;
}

int
fileino_stat(int ino, struct stat *st)
{
	assert(fileino_exists(ino));

	fileinode *fi = &files->fi[ino];
	st->st_ino = ino;
	st->st_mode = fi->mode;
	st->st_size = fi->size;
	st->st_nlink = fi->nlink;

	return 0;
}

// Grow or shrink a file to exactly a specified size.
// If growing a file, then fills the new space with zeros.
// Returns 0 if successful, or returns -1 and sets errno on error.
int
fileino_truncate(int ino, off_t newsize)
{
	assert(fileino_isvalid(ino));
	assert(newsize >= 0 && newsize <= FILE_MAXSIZE);

	size_t oldsize = files->fi[ino].size;
	size_t oldpagelim = ROUNDUP(files->fi[ino].size, PAGESIZE);
	size_t newpagelim = ROUNDUP(newsize, PAGESIZE);
	if (newsize > oldsize) {
		// Grow the file and fill the new space with zeros.
		sys_get(SYS_PERM | SYS_READ | SYS_WRITE, 0, NULL, NULL,
			FILEDATA(ino) + oldpagelim,
			newpagelim - oldpagelim);
		memset(FILEDATA(ino) + oldsize, 0, newsize - oldsize);
	} else if (newsize > 0) {
		// Shrink the file, but not all the way to empty.
		// Would prefer to use SYS_ZERO to free the file content,
		// but SYS_ZERO isn't guaranteed to work at page granularity.
		sys_get(SYS_PERM, 0, NULL, NULL,
			FILEDATA(ino) + newpagelim, FILE_MAXSIZE - newpagelim);
	} else {
		// Shrink the file to empty.  Use SYS_ZERO to free completely.
		sys_get(SYS_ZERO, 0, NULL, NULL, FILEDATA(ino), FILE_MAXSIZE);
	}
	files->fi[ino].size = newsize;
	return 0;
}


////////// File descriptor functions //////////

// Search the file descriptor table for the first free file descriptor,
// and return a pointer to that file descriptor.
// If no file descriptors are available,
// returns NULL and set errno appropriately.
filedesc *filedesc_alloc(void)
{
	int i;
	for (i = 0; i < OPEN_MAX; i++)
		if (files->fd[i].ino == FILEINO_NULL)
			return &files->fd[i];
	errno = EMFILE;
	return NULL;
}

// Find or create and open a file, optionally using a given file descriptor.
// The argument 'fd' must point to a currently unused file descriptor,
// or may be NULL, in which case this function finds an unused file descriptor.
// The 'openflags' determines whether the file is created, truncated, etc.
// Returns the opened file descriptor on success,
// or returns NULL and sets errno on failure.
filedesc *
filedesc_open(filedesc *fd, const char *path, int openflags, mode_t mode)
{
	if (!fd && !(fd = filedesc_alloc()))
		return NULL;
	assert(fd->ino == FILEINO_NULL);

	// Walk the directory tree to find the desired directory entry,
	// creating an entry if it doesn't exist and O_CREAT is set.
	struct dirent *de = dir_walk(path, (openflags & O_CREAT) != 0);
	if (de == NULL)
		return NULL;

	// Create the file if necessary
	int ino = de->d_ino;
	if (ino == FILEINO_NULL) {
		assert(openflags & O_CREAT);
		if ((ino = fileino_alloc()) < 0)
			return NULL;
		files->fi[ino].nlink = 1;	// dirent's reference
		files->fi[ino].size = 0;
		files->fi[ino].psize = -1;
		files->fi[ino].mode = S_IFREG | (mode & 0777);
		de->d_ino = ino;
	}

	// Initialize the file descriptor
	fd->ino = ino;
	fd->flags = openflags;
	fd->ofs = (openflags & O_APPEND) ? files->fi[ino].size : 0;
	fd->err = 0;
	fileino_take(ino);		// file descriptor's reference

	return fd;
}

// Read up to 'count' objects each of size 'eltsize'
// from the open file described by 'fd' into memory buffer 'buf',
// whose size must be at least 'count * eltsize' bytes.
// May read fewer than the requested number of objects
// if the end of file is reached, but always an integral number of objects.
// On success, returns the number of objects read (NOT the number of bytes).
// If an error (other than end-of-file) occurs, returns -1 and sets errno.
//
// If the file is a special device input file such as the console,
// this function pretends the file has no end and instead
// uses sys_ret() to wait for the file to extend the special file.
ssize_t
filedesc_read(filedesc *fd, void *buf, size_t eltsize, size_t count)
{
	assert(filedesc_isreadable(fd));
	fileinode *fi = &files->fi[fd->ino];

	ssize_t actual = 0;
	while (count > 0) {
		// Read as many elements as we can from the file.
		// Note: fd->ofs could well point beyond the end of file,
		// which means that avail will be negative - but that's OK.
		ssize_t avail = MIN(count, (fi->size - fd->ofs) / eltsize);
		if (avail > 0) {
			memmove(buf, FILEDATA(fd->ino) + fd->ofs,
				avail * eltsize);
			fd->ofs += avail * eltsize;
			buf += avail * eltsize;
			actual += avail;
			count -= avail;
		}

		// If there's no more we can read, stop now.
		if (count == 0 || !(fi->mode & S_IFPART))
			break;

		// Wait for our parent to extend (or close) the file.
		cprintf("fread: waiting for input on file %d\n",
			fd - files->fd);
		sys_ret();
	}
	return actual;
}

// Write up to 'count' objects each of size 'eltsize'
// from memory buffer 'buf' to the open file described by 'fd'.
// The size of 'buf' must be at least 'count * eltsize' bytes.
// On success, returns the number of objects written (NOT the number of bytes).
// If an error occurs, returns -1 and sets errno appropriately.
ssize_t
filedesc_write(filedesc *fd, const void *buf, size_t eltsize, size_t count)
{
	assert(filedesc_iswritable(fd));
	fileinode *fi = &files->fi[fd->ino];

	// If we're appending to the file, seek to the end first.
	if (fd->flags & O_APPEND)
		fd->ofs = fi->size;

	// Write the data, growing the file as necessary.
	if (fileino_write(fd->ino, fd->ofs, buf, eltsize * count) < 0) {
		fd->err = errno;	// save error indication for ferror()
		return -1;
	}

	// Advance the file position
	fd->ofs += eltsize * count;
	assert(fi->size >= fd->ofs);

	return count;
}

// Seek the given file descriptor to a specificied position,
// which may be relative to the file start, end, or corrent position,
// depending on 'whence'.
// Returns the resulting absolute file position,
// or returns -1 and sets errno appropriately on error.
off_t filedesc_seek(filedesc *fd, off_t offset, int whence)
{
	assert(filedesc_isopen(fd));
	fileinode *fi = &files->fi[fd->ino];
	assert(whence == SEEK_SET || whence == SEEK_CUR || whence == SEEK_END);

	off_t newofs = offset;
	if (whence == SEEK_CUR)
		newofs += fd->ofs;
	else if (whence == SEEK_END)
		newofs += fi->size;
	assert(newofs >= 0);

	fd->ofs = newofs;
	return newofs;
}

void
filedesc_close(filedesc *fd)
{
	assert(filedesc_isopen(fd));
	assert(fileino_isvalid(fd->ino));
	assert(files->fi[fd->ino].nlink > 0);

	fileino_drop(fd->ino);		// release fd's reference to inode
	fd->ino = FILEINO_NULL;		// mark the fd free
}

#endif /* LAB >= 4 */
