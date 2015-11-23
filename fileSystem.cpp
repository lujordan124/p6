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
	char *fileName;
	int fileSize;
	int dataHead;
} directoryEntry;

typedef struct fatEntry 
{
	int fatTable[4];
} fatEntry;

typedef struct fileDescriptor
{
	int used;
	int file;
	int offset;
} fileDescriptor;

superBlock sB;
vector<fileDescriptor> fileDescriptors(4);
directoryEntry *directory;
fatEntry FAT[8];
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

	//initialize Directory and FAT
	char buffer2[BLOCK_SIZE];
	for (i = 1; i < 6; i++) {
		block_write(i, buffer2);
	}

	int j; 
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 4; j++) {
			FAT[i].fatTable[j] = -2;
		}
	}

	for (i = 6; i < 14; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, &FAT[i - 6], sizeof(struct fatEntry));
		block_write(i, buffer);
	}

	close_disk();

	return 0;
}

int mount_fs(char *disk_name)
{
	if (open_disk(disk_name) == 0) {
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

	// cout << sB.directoryIndex << " " << sB.directoryEntries << " " << sB.fatIndex << " " << sB.fatEntries << " " << sB.dataIndex << endl;

	int i;
	directory = (directoryEntry *) malloc(5 * BLOCK_SIZE);
	char *p = (char *)directory;
	// //Load Directory
	for(i = 0; i < 5; i++) {
		block_read(sB.directoryIndex + i, buffer);
		memcpy(p, buffer, BLOCK_SIZE);
		p += BLOCK_SIZE;
	}

	//Load FAT
	for(i = 6; i < 14; i++) {
		int block = block_read(i, buffer);
		fatEntry *fat = new fatEntry;
		fat = (fatEntry *)buffer;
		int k;
		for (k = 0; k < 4; k++) {
			FAT[i-6].fatTable[k] = fat->fatTable[k];
		}
	}

	//Initialize File Descriptors
	for(i = 0; i < 4; i++) {
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
	for (i = 0; i < 5; i++, p += BLOCK_SIZE) {
		memcpy(buffer, p, BLOCK_SIZE);
		block_write(sB.directoryIndex + i, buffer);
	}

	//write FAT
	for (i = 6; i < 14; i++) {
		memset(buffer, 0, BLOCK_SIZE);
		memcpy(buffer, &FAT[i - 6], sizeof(struct fatEntry));
		block_write(i, buffer);
	}

	//clear fileDescriptors
	for (i = 0; i < 4; i++) {
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
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].used == 1) {
		cout << "Bad file descriptor" << endl;
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
		strcpy(directory[index].fileName, name);
		directory[index].fileSize = 0;
		int fatIndex = getFreeFat();
		if (fatIndex == -1) {
			cout << "Fat is full" << endl;
		}

		directory[index].dataHead = fatIndex;
		int fatNum = fatIndex / 4;
		int fatCell = fatIndex % 4;

		FAT[fatNum].fatTable[fatCell] = EOF;
		sB.directoryEntries += 1;
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
			return -1;
		}

		directory[file].fileSize = 0;
		int head = directory[file].dataHead;
		int fatIndex = head / 4;
		int fatTableIndex = head % 4;
		int next = FAT[fatIndex].fatTable[fatTableIndex];

		int temp, temp2, temp3;

	while (next != EOF) {
		temp = next / 4;
		temp2 = next % 4;
		temp3 = FAT[temp].fatTable[temp2];
		FAT[temp].fatTable[temp2] = -2;
		next = temp3;
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
	return 0;
}

int fs_write(int fildes, void *buf, size_t nbyte)
{
	if (nbyte < 1) {
		cout << "Size of nbyte is invalid" << endl;
		return -1;
	}

	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].used == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}

	if (getFreeFat() == -1) {
		cout << "Data blocks are all full" << endl;
		return -1;
	}

	directoryEntry *entry = &directory[fileDescriptors[fildes].file];
	int old = entry->fileSize;
	int offset = old % BLOCK_SIZE;
	int newSize = old + nbyte;
	int newBlocks = (newSize / BLOCK_SIZE) - (old / BLOCK_SIZE);
	int fatBlock = entry->dataHead / 4;
	int fatTableIndex = entry->dataHead % 4;
	entry->fileSize = newSize;

	while (fatBlock != EOF) {
		fatBlock = FAT[fatBlock].fatTable[fatTableIndex] / 4;
		fatTableIndex = FAT[fatBlock].fatTable[fatTableIndex] % 4;
	}

	char *toWrite = (char *)buf;
	char buffer[BLOCK_SIZE];
	int amount = nbyte;

	if (newBlocks == 0) {
		memcpy(buffer + offset, toWrite, strlen(toWrite));
		block_write(FAT[fatBlock].fatTable[fatTableIndex], buffer);
	} else if (newBlocks > 0) {
		if (offset != 0) {
			memcpy(buffer + offset, toWrite, BLOCK_SIZE - offset);
			block_write(FAT[fatBlock].fatTable[fatTableIndex], buffer);
			toWrite = toWrite + (BLOCK_SIZE - offset);
		}

		int j;
		for (j = 0; j < newBlocks; j++) {
			memcpy(buffer, toWrite, BLOCK_SIZE);
			int nextFat = getFreeFat();
			FAT[fatBlock].fatTable[fatTableIndex] = nextFat;
			int updateBlock = nextFat / 4;
			int updateFatTableIndex = nextFat % 4;
			FAT[updateBlock].fatTable[updateFatTableIndex] = EOF;
			block_write(nextFat, buffer);
			toWrite = toWrite + BLOCK_SIZE;
		}
	}

	return 0;
}

int fs_get_filesize(int fildes)
{
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].used == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}

	return directory[fileDescriptors[fildes].file].fileSize;
}

int fs_lseek(int fildes, off_t offset)
{
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].used == 1) {
		cout << "Bad file descriptor" << endl;
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
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].used == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}

	if (length < 0 || length > fs_get_filesize(fildes)) {
		cout << "Invalid Length" << endl;
		return -1;
	}

	struct directoryEntry *file = &directory[fileDescriptors[fildes].file];

	int numBlocks = length/BLOCK_SIZE;
	int numBytes = length % BLOCK_SIZE;
	int fatBlock = file->dataHead / 4;
	int fatTableIndex = file->dataHead % 4;
	int i;
	for (i = 0; i < numBlocks; i++) {
		fatBlock = FAT[fatBlock].fatTable[fatTableIndex] / 4;
		fatTableIndex = FAT[fatBlock].fatTable[fatTableIndex] % 4;
	}

	int next = FAT[fatBlock].fatTable[fatTableIndex];
	int temp, temp2, temp3;

	while (next != EOF) {
		temp = next / 4;
		temp2 = next % 4;
		temp3 = FAT[temp].fatTable[temp2];
		FAT[temp].fatTable[temp2] = -2;
		next = temp3;
	}

	for (i = BLOCK_SIZE - numBytes; i < BLOCK_SIZE; i++) {

	}

	file->fileSize = length;

	return 0;
}

int getFreeFat() {
	int i, j;
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 4; j++) {
	 		if (FAT[i].fatTable[j] == -2) {
	 			return (i*4) + j;
	 		}
		}
	}

	return -1;
}

int getFreeDirectoryIndex() {
	int i;
	for (i = 0; i < 8; i++) {
		if (directory[i].fileSize == -1) {
			return i;
		}
	}

	return -1;
}

int getFileIndex(char *file_name) 
{
	int i;
	for (i = 0; i < 8; i++) {	
		if (!strcmp(directory[i].fileName, file_name)) {
			return i;
		}
	}

	return -1;
}

int getFreeFileDescriptors()
{
	int i;
	for (i = 0; i < 4; i++) {
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