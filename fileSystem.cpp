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
	int fatLength;
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
	int free;
	int file;
	int offset;
} fileDescriptor;

superBlock sB;
vector<fileDescriptor> fileDescriptors(4);
directoryEntry *directory;
fatEntry FAT[8];
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
	sB.fatLength = 0;
	sB.dataIndex = 32;

	memset(buffer, 0, BLOCK_SIZE);
	memcpy(buffer, &sB, sizeof(struct superBlock));
    block_write(0, buffer);

	//initialize Directory and FAT
	for (i = 1; i < 5; i++) {
		block_write(i, buffer);
	}

	int j; 
	for (i = 0; i < 8; i++) {
		for (j = 0; j < 4; j++) {
			FAT[i].fatTable[j] = -1;
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
	char *p =  (char *)directory
	//Load Directory
	for(i = 1; i < 6; i++, p += BLOCK_SIZE) {
		block_read(sB.directoryIndex + i, buffer);
		memcpy(p, buffer, BLOCK_SIZE);
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
		fileDescriptors[i].free = 0;
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
		fileDescriptors[i].free = 0;
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
	return 0;
}

int fs_close(int fildes)
{
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].free == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}

	fileDescriptors[fildes].free = 0;
	fileDescriptors[fildes].file = 0;
	//reset file in directory
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
		//MAKE THE FILE HERE??

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

		//DELETE FILE HERE
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
	return 0;
}

int fs_get_filesize(int fildes)
{
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].free == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}


	return directory[fileDescriptors[fildes].file].fileSize;
}

int fs_lseek(int fildes, off_t offset)
{
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].free == 1) {
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
	if (fildes < 0 || fildes >= 4 || fileDescriptors[fildes].free == 1) {
		cout << "Bad file descriptor" << endl;
		return -1;
	}

	if (length < 0 || length > fs_get_filesize(fildes)) {
		cout << "Invalid Length" << endl;
		return -1;
	}



	return 0;
}

int getFileIndex(char *file_name) 
{
	int i;
	for (i = 0; i < 5; i++) {
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
		if (fileDescriptors[i].free == 0) {
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

int main() {
	string name = "hi";
	char *cstr = new char[name.length() + 1];
	strcpy(cstr, name.c_str());
	int file = make_fs(cstr);
	file = mount_fs(cstr);

	return 0;
}