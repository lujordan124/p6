#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <string.h>
#include <iostream>
#include <vector>

#include "disk.h"

using namespace std;

typedef struct superBlock 
{
	int directoryIndex;
	int directoryEntries;
	int fatIndex;
	int dataIndex;
} superBlock;

typedef struct directoryEntry 
{
	char fileName[4];
	short fileSize;
	short used;
	short dataHead;
} directoryEntry;

typedef struct fatEntry 
{
	short used;
	short nextFat;
} fatEntry;

typedef struct fileDescriptor
{
	int used;
	int file;
	int offset;
} fileDescriptor;

superBlock sB;
vector<fileDescriptor> fileDescriptors(5);
directoryEntry directory[8];
fatEntry FAT[32];
int getFreeFat();
int getFreeDirectoryIndex();
int isOpen(char *name);
int getFreeFileDescriptors();
int getFileIndex(char *file_name);

int make_fs(char *disk_name)
{
	if (!disk_name) {
		cout << "Could not make disk with given disk name." << endl;
		return -1;
	}

	if (strlen(disk_name) > 4) {
		cout << "Invalid disk name." << endl;
		return -1;
	}

	int i;
	for (i = 0; i < strlen(disk_name); i++) {
		if (!isalpha(disk_name[i])) {
			cout << "Invalid disk name." << endl;
			return -1;
		}
	}

	make_disk(disk_name);
	open_disk(disk_name);

	char buffer[BLOCK_SIZE];

	sB.directoryIndex = 1;
	sB.directoryEntries = 0;
	sB.fatIndex = 6;
	sB.dataIndex = 32;

	memset(buffer, 0, BLOCK_SIZE);
	memcpy(buffer, &sB, sizeof(struct superBlock));
    block_write(0, buffer);

	close_disk();

	return 0;
}

int mount_fs(char *disk_name)
{
	if (open_disk(disk_name) == -1) {
		cout << "Could not open disk" << endl;
		return -1;
	}

	//Load Super Block
	char buffer[BLOCK_SIZE];
	if (block_read(0, buffer) != 0) {
		cout << "Could not read superBlock" << endl;
		return -1;
	}

	superBlock *super = new superBlock;

	super = (superBlock *)buffer;

	sB.directoryIndex = super->directoryIndex;
	sB.directoryEntries = super->directoryEntries;
	sB.fatIndex = super->fatIndex;

	sB.dataIndex = super->dataIndex;

	int i;
	// Load Directory
	for(i = 1; i < 6; i++) {
		block_read(i, ((char *) &directory + (BLOCK_SIZE * (i - 1))));
	}

	//Load FAT
	for(i = 6; i < 14; i++) {
		block_read(i, ((char *) &FAT + (BLOCK_SIZE * (i - 6))));
	}

	//Initialize File Descriptors
	for(i = 0; i < 5; i++) {
		fileDescriptors.push_back(fileDescriptor());
		fileDescriptors[i].used = 0;
		fileDescriptors[i].file = 0;
	}

	return 0;

}

int dismount_fs(char *disk_name)
{
	char buffer[BLOCK_SIZE];
	int i;

	//write directory
	char *p = (char *)directory;
	for (i = 0; i < 5; i++) {
		memcpy(buffer, p, BLOCK_SIZE);
		block_write(sB.directoryIndex + i, buffer);
		p += BLOCK_SIZE;
	}

	//write FAT
	p = (char *)FAT;
	for (i = 6; i < 14; i++) {
		memcpy(buffer, p, BLOCK_SIZE);
		block_write(i, buffer);
		p += BLOCK_SIZE;
	}

	//clear fileDescriptors
	for (i = 0; i < 5; i++) {
		fileDescriptors[i].used = 0;
		fileDescriptors[i].file = 0;
		fileDescriptors[i].offset = 0;
	}

	if (close_disk() != 0) {
		return -1;
	}

	return 0;
}

int fs_open(char *name)
{
	int file;
	file = getFileIndex(name);
	if (file == -1) {
		cout << "Cannot find file" << endl; 
		return -1;
	}

	if (directory[file].used == 0) {
		cout << "File with file name: " << name << " could not be found." << endl;
		return -1;
	}

	int fd = getFreeFileDescriptors();
	if (fd == -1) {
		cout << "No free file descriptors available" << endl;
		return -1;
	}

	fileDescriptors[fd].used = 1;
	fileDescriptors[fd].file = file;
	fileDescriptors[fd].offset = 0;

	return fd;
}

int fs_close(int fildes)
{
	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 1" << endl;
		return -1;
	}

	fileDescriptors[fildes].used = 0;
	fileDescriptors[fildes].file = 0;
	return 0;
}

int fs_create(char *name)
{
	if (strlen(name) > 4) {
		cout << "File name is too long" << endl;
		return -1;
	}

	int file = getFileIndex(name);
	if (file < 0) {
		if (sB.directoryEntries == 7) {
			cout << "You have already reached the max number of files." << endl;
			return -1;
		}
		
		int index = getFreeDirectoryIndex();
		int fatIndex = getFreeFat();
		if (fatIndex == -1) {
			cout << "Fat is full" << endl;
		}

		strcpy(directory[index].fileName, name);
		directory[index].dataHead = fatIndex;
		directory[index].fileSize = 0;
		directory[index].used = 1;

		FAT[fatIndex].nextFat = EOF;
		FAT[fatIndex].used = 1;

		sB.directoryEntries++;
	} else {
		cout << "File already exists" << endl;
		return -1;
	}

	return 0;
}

int fs_delete(char *name)
{
	int file = getFileIndex(name);
	if (file >= 0) {
		if (isOpen(name)) {
			cout << "File is still open" << endl;
			return -1;
		}

		directory[file].fileSize = 0;
		directory[file].used = 0;

		int head = directory[file].dataHead;
		int next = FAT[head].nextFat;
		int temp = next;

		while (head != EOF) {
			next = FAT[head].nextFat;
			FAT[head].used = 0;
			head = next;
		}

		sB.directoryEntries--;

		return 0;
	} else {
		cout << "File does not exist" << endl;
		return -1;
	}

}

int fs_read(int fildes, void *buf, size_t nbyte)
{
	if (nbyte < 1) {
		cout << "Size of nbyte is invalid" << endl;
		return -1;
	}

	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 2" << endl;
		return -1;
	}

	directoryEntry *entry = &directory[fileDescriptors[fildes].file];
	int offset = fileDescriptors[fildes].offset;

	int amount;
	if (offset + nbyte > entry->fileSize) {
		amount = entry->fileSize - offset;
	} else {
		amount = nbyte;
	}

	char *toRead = (char *)buf;

	int i;
	int count = 0;
	int fatBlock = entry->dataHead;
	int numBlocks = (amount / BLOCK_SIZE) + 1;
	int currentBlock = offset / BLOCK_SIZE;
	int loc = offset % BLOCK_SIZE;
	char buffer[BLOCK_SIZE];

	int left;

	for (i = 0; i < currentBlock; i++) {
		fatBlock = FAT[fatBlock].nextFat;
	}

	for (i = 0; i < numBlocks; i++) {
		if (loc + amount > BLOCK_SIZE) {
			left = BLOCK_SIZE - loc;
		} else {
			left = amount;
		}

		block_read(fatBlock + 32, buffer);
		memcpy(toRead, buffer + loc, left);

		count += left;
		toRead += left;
		loc = 0;

		fatBlock = FAT[fatBlock].nextFat;
		amount -= left;
	}

	fileDescriptors[fildes].offset += count;
	return count;

}

int fs_write(int fildes, void *buf, size_t nbyte)
{
	if (nbyte < 1) {
		cout << "Size of nbyte is invalid" << endl;
		return -1;
	}

	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 2" << endl;
		return -1;
	}

	if (getFreeFat() == -1) {
		cout << "Data blocks are all full" << endl;
		return -1;
	}

	directoryEntry *entry = &directory[fileDescriptors[fildes].file];
	int old = entry->fileSize;
	int offset = fileDescriptors[fildes].offset;
	int newBlocks;

	if (offset + nbyte > entry->fileSize) {
		newBlocks = ((offset + nbyte) / BLOCK_SIZE) - (old / BLOCK_SIZE);
	} else {
		newBlocks = 0;
	}

	int numBlocks = ((nbyte + (offset % BLOCK_SIZE)) / BLOCK_SIZE) + 1;
	int currentBlock = offset/BLOCK_SIZE;
	int fatBlock = entry->dataHead;

	char *toWrite = (char *)buf;
	char buffer[BLOCK_SIZE];
	
	int amount = nbyte;
	int left;
	int count = 0;

	int i;
	for (i = 0; i < currentBlock; i++) {
		if (fatBlock == EOF) {
			cout << "Trying to read past the end of file." << endl;
		}

		fatBlock = FAT[fatBlock].nextFat;
	}

	int loc = offset % BLOCK_SIZE;
	for (i = 0; i < numBlocks; i++) {
		if (loc + amount > BLOCK_SIZE) {
			left = BLOCK_SIZE - loc;
		} else {
			left = amount;
		}

		memcpy(buffer + loc, toWrite, left);
		block_write(fatBlock + 32, buffer);
		count += left;

		if (i == (numBlocks - newBlocks - 1) && newBlocks > 0) {
			int temp = getFreeFat();
			if (temp == -1) {
				cout << "No more free blocks" << endl;
				return -1;
			}

			FAT[fatBlock].nextFat = temp;
			FAT[temp].nextFat = EOF;
			FAT[temp].used = 1;
			newBlocks--;
		}

		toWrite = toWrite + BLOCK_SIZE;
		loc = 0;
		fatBlock = FAT[fatBlock].nextFat;
		amount = amount - left;
	}

	if (offset + count > entry->fileSize) {
		entry->fileSize = offset + nbyte;
	}

	fileDescriptors[fildes].offset += count;
	return count;
}

int fs_get_filesize(int fildes)
{
	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 3" << endl;
		return -1;
	}

	if (directory[fileDescriptors[fildes].file].used == 0) {
		cout << "File is not open" << endl;
		return -1;
	}

	return directory[fileDescriptors[fildes].file].fileSize;
}

int fs_lseek(int fildes, off_t offset)
{
	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 4" << endl;
		return -1;
	}

	if (offset > fs_get_filesize(fildes)) {
		cout << "Invalid offset" << endl;
		return -1;
	}

	fileDescriptors[fildes].offset = fileDescriptors[fildes].offset + offset;
	return 0;
}

int fs_truncate(int fildes, off_t length)
{
	if (fildes < 0 || fildes >= 5) {
		cout << "Bad file descriptor 5" << endl;
		return -1;
	}

	if (length < 0 || length > fs_get_filesize(fildes)) {
		cout << "Invalid Length" << endl;
		return -1;
	}

	struct directoryEntry *file = &directory[fileDescriptors[fildes].file];

	int numBlocks = length / BLOCK_SIZE;
	int fatBlock = file->dataHead;

	int i;
	for (i = 0; i < numBlocks; i++) {
		fatBlock = FAT[fatBlock].nextFat;
	}

	int next = FAT[fatBlock].nextFat;
	int temp;
	FAT[fatBlock].nextFat = EOF;

	while (next != EOF) {
		temp = FAT[next].nextFat;
		FAT[next].used = 0;
		next = temp;
	}

	file->fileSize = length;
	fileDescriptors[fildes].offset = 0;

	return 0;
}

int getFreeFat() {
	int i, j;
	for (i = 0; i < 32; i++) {
		if (FAT[i].used == 0) {
			return i;
		}
	}

	return -1;
}

int getFreeDirectoryIndex() {
	int i;
	for (i = 0; i < 8; i++) {
		if (directory[i].used == 0) {
			return i;
		}
	}

	return -1;
}

int getFileIndex(char *file_name) 
{
	int i;
	for (i = 0; i < 8; i++) {	
		if (strcmp(directory[i].fileName, file_name) == 0) {
			return i;
		}
	}

	return -1;
}

int getFreeFileDescriptors()
{
	int i;
	for (i = 0; i < 5; i++) {
		if (fileDescriptors[i].used == 0) {
			return i;
		}
	}

	return -1;
}

int isOpen(char *name) {
	int i;
	for (i = 0; i < 4; i++) {
		if (strcmp(directory[fileDescriptors[i].file].fileName, name) == 0) {
			return 1;
		}
	}

	return 0;
}