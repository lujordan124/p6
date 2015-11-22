#ifndef _FILESYSTEM_H_
#define _FILESYSTEM_H_
       
/***************************************************************************/
int make_fs(char *disk_name);
int mount_fs(char *disk_name);
int dismount_fs(char *disk_name);

int fs_open(char *name);
int fs_close(int fildes);

int fs_create(char *name);
int fs_delete(char *name);

int fs_read(int fildes, void *buf, size_t nbyte);
int fs_write(int fildes, void *buf, size_t nbyte);

int fs_get_filesize(int fildes);
int fs_lseek(int fildes, off_t offset);
int fs_truncate(int fildes, off_t length);
/***************************************************************************/

#endif