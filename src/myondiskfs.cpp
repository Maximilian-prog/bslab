//
// Created by Oliver Waldhorst on 20.03.20.
// Copyright © 2017-2020 Oliver Waldhorst. All rights reserved.
//

#include "myondiskfs.h"

// For documentation of FUSE methods see https://libfuse.github.io/doxygen/structfuse__operations.html

#undef DEBUG

// TODO: Comment lines to reduce debug messages
#define DEBUG
#define DEBUG_METHODS
#define DEBUG_RETURN_VALUES

#include <iostream>
#include <cstring>
#include <string>
#include <time.h>

#include <unistd.h>
#include <string.h>
#include <errno.h>

#include "macros.h"
#include "myfs.h"
#include "myfs-info.h"
#include "blockdevice.h"

//Sizes in blocks
#define blockCount 51200 //Benötigt wären eigentlich nur 1024*1024*20 / 512 = 40960 Blöcke, aber für extra Platz wurde BlockCount auf 51200 gesetzt

#define Superblock_Size 1
#define Dmap_Size_arr blockCount
#define Dmap_Size 100 //51200 / 512 => 100
#define FAT_Size 400 // 51200 / (512/4) => 400
#define FAT_Size_arr blockCount
#define Root_Size 64
#define Root_Size_arr NUM_DIR_ENTRIES
#define Data_Size 0

//#define byteToBlock(byte) ((byte)/(BLOCK_SIZE)) //Wenn bei % 512 ein Rest existiert, dann muss noch ein Block addiert werden
#define blockToByte(numberOfBlocks) ((BLOCK_SIZE) * (numberOfBlocks))
#define startSUPERBLOCK 0
#define startDMAP startSUPERBLOCK + 1
#define startFAT ((Superblock_Size) + (Dmap_Size))
#define startROOT ((startFAT) + FAT_Size)
#define startDATA ((startROOT) + (NUM_DIR_ENTRIES))

#define endSUPERBLOCK 1
#define endDMAP (startFAT - 1)
#define endFAT (startROOT - 1)
#define endROOT (startDATA - 1)
#define endDATA blockCount

#define offsetDMAP_array (startDATA) //DMAP kann erst ab dem 1. Datenblock genutzt werden, da davor Superblock etc sind

struct Superblock {
    int fileSystemSize;
    int anzahlBloecke;
    unsigned int startDmap;
    unsigned int startFat;
    unsigned int startRoot; // Inodes -> 64 Inodes insgesamt
    unsigned int startData;
};

struct Dmap {
    bool dmap[Dmap_Size_arr];
};

struct FAT {
    int EOC = 0xFFFFFFFF; //EOC = -1;
    uint32_t fat[FAT_Size_arr];
};

struct MyFsFileInfo {
    char name[NAME_LENGTH];
    size_t size;
    uid_t uid;
    uid_t gid;
    mode_t mode;//Permissions  rwx rwx rwx (user group other)
    time_t atime; //Zeitpunkt letzter Zugriffe
    time_t mtime; //letzte Veränderungen
    time_t ctime; //letzte Statusänderung
    bool isOpen;
    int firstBlockInFAT; //Index des ersten Blocks in FAT
};

struct Root {
    MyFsFileInfo root[Root_Size_arr]; //Array MyFsFileInfo (max 64)
};

struct OpenFile {
    char puffer[BLOCK_SIZE];
    int blockNo;
};

OpenFile openfiles[NUM_OPEN_FILES];

Superblock mySuperblock;
Dmap myDmap;
FAT myFat;
Root myRoot;

/// @brief Constructor of the on-disk file system class.
///
/// You may add your own constructor code here.
MyOnDiskFS::MyOnDiskFS() : MyFS() {
    // create a block device object
    this->blockDevice = new BlockDevice(BLOCK_SIZE);
    // TODO: [PART 2] Add your constructor code here
    //blockDevice->create("/home/user/CLionProjects/bslab/build");
}

/// @brief Destructor of the on-disk file system class.
///
/// You may add your own destructor code here.
MyOnDiskFS::~MyOnDiskFS() {
    // free block device object
    delete this->blockDevice;

    // TODO: [PART 2] Add your cleanup code here

}

/// @brief Create a new file.
///
/// Create a new file with given name and permissions.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode Permissions for file access.
/// \param [in] dev Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseMknod(const char *path, mode_t mode, dev_t dev) {
    LOGM();
    // TODO: [PART 2] Implement this!
    int ret = -EINVAL;
    const int SIZE = 0;

    MyFsFileInfo newFile;
    strcpy(newFile.name, path + 1); //Dateiname
    newFile.size = SIZE;
    newFile.uid = geteuid();
    newFile.gid = getgid();
    newFile.mode = mode;
    newFile.atime = time(NULL);
    newFile.mtime = time(NULL);
    newFile.ctime = time(NULL);
    newFile.isOpen = false;


    int indexOfFileInRoot = -1;
    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] == 0) // freier Eintrag in Root gefunden
        {
            myRoot.root[i] = newFile;
            writeBlockOfStructure("root", i, newFile);
            indexOfFileInRoot = i;
            break;
        }
    }

    for (int i = offsetDMAP_array; i < Dmap_Size_arr; i++) {
        if (myDmap.dmap[i] == 0) // freier Eintrag in DMAP gefunden
        {
            myDmap.dmap[i] = 1;
            writeBlockOfStructure("dmap", i);
            for (int j = offsetDMAP_array; j < Dmap_Size_arr; j++) { //Suche nach EOC-Platz
                if (myDmap.dmap[j] == 0) { //freier Platz für EOC gefunden (maximal 64 Dateien => 64 EOC's
                    myDmap.dmap[j] = 1;
                    writeBlockOfStructure("dmap", j);

                    //FAT aktualisieren
                    myFat.fat[i] = j;
                    myFat.fat[j] = myFat.EOC;
                    myRoot.root[indexOfFileInRoot].firstBlockInFAT = i;

                    LOG("Update Blockdevice with root");
                    //Blockdevice update mit Root
                    writeBlockOfStructure("root", indexOfFileInRoot, myRoot.root[indexOfFileInRoot]);

                    LOG("Update Blockdevice with fat");
                    //Blockdevice update mit FAT
                    writeBlockOfStructure("fat", i);

                    LOG("Update Blockdevice with EOC in FAT");
                    //Blockdevice update mit EOC in FAT
                    writeBlockOfStructure("fat", j);

                    ret = 0;
                    RETURN(ret);
                }
            }
        }
    }

    RETURN(ret);
}

/// @brief Delete a file.
///
/// Delete a file with given name from the file system.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseUnlink(const char *path) {
    LOGM();

    // TODO: [PART 2] Implement this!
    int ret = -ENOENT;

    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] != 0) { // 1 in DMAP is used
            if (strcmp(myRoot.root[i].name, path + 1) == 0)  //fileName == path
            {
                //DMAP Blöcke wieder als freigegeben markieren
                int indexFAT = myRoot.root[i].firstBlockInFAT;
                while (indexFAT != myFat.EOC) {
                    int oldIndex = indexFAT;
                    LOGF("Indexfat %d", indexFAT);
                    myDmap.dmap[indexFAT] = 0;
                    writeBlockOfStructure("dmap", indexFAT);
//                  TODO: Update FAT ?
                    indexFAT = myFat.fat[oldIndex];
                    myFat.fat[oldIndex] = 0;
//                   writeBlockOfStructure für FAT
                }
                //name mit 0-en beschreiben
                char puffer[NAME_LENGTH];
                for (int j = 0; j < NAME_LENGTH; j++) {
                    puffer[j] = 0x00;
                }
                strcpy(myRoot.root[i].name, puffer);
                writeBlockOfStructure("root", i, myRoot.root[i]);
                ret = 0;
                RETURN(ret);
            }
        }
    }

    RETURN(ret);

}

/// @brief Rename a file.
///
/// Rename the file with with a given name to a new name.
/// Note that if a file with the new name already exists it is replaced (i.e., removed
/// before renaming the file.)
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newpath  New name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRename(const char *path, const char *newpath) {
    LOGM();

    // TODO: [PART 2] Implement this!

    int ret = -ENOENT;
    const char *oldName = path + 1;
    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] != 0) {
//          TODO: Existing files with the new name will be removed
            if (strcmp(myRoot.root[i].name, newpath + 1) == 0) {
                fuseUnlink(oldName);
            }
            if (strcmp(myRoot.root[i].name, oldName) == 0) {
                strcpy(myRoot.root[i].name, newpath + 1);
                ret = 0;
                writeBlockOfStructure("root", i, myRoot.root[i]);
                break;
            }
        }
    }

    RETURN(ret);
}

/// @brief Get file meta data.
///
/// Get the metadata of a file (user & group id, modification times, permissions, ...).
/// \param [in] path Name of the file, starting with "/".
/// \param [out] statbuf Structure containing the meta data, for details type "man 2 stat" in a terminal.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseGetattr(const char *path, struct stat *statbuf) {
    LOGM();

    // TODO: [PART 2] Implement this!

    statbuf->st_uid = getuid(); // The owner of the file/directory is the user who mounted the filesystem
    statbuf->st_gid = getgid(); // The group of the file/directory is the same as the group of the user who mounted the filesystem
    statbuf->st_atime = time(NULL); // The last "a"ccess of the file/directory is right now
    statbuf->st_mtime = time(NULL); // The last "m"odification of the file/directory is right now

    int ret = 0;

    if (strcmp(path, "/") == 0) {
        statbuf->st_mode = S_IFDIR | 0755;
        statbuf->st_nlink = 2;
    } else {
        ret = -ENOENT;
        for (int i = 0; i < Root_Size_arr; i++) {
            if (myRoot.root[i].name[0] != 0) {
                if (strcmp(myRoot.root[i].name, path + 1) == 0) {
                    statbuf->st_mode = myRoot.root[i].mode;
                    statbuf->st_nlink = 1;
                    statbuf->st_size = myRoot.root[i].size;
                    ret = 0;
                    break;
                }
            }
        }
    }

    RETURN(ret);
}

/// @brief Change file permissions.
///
/// Set new permissions for a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] mode New mode of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChmod(const char *path, mode_t mode) {
    LOGM();

    // TODO: [PART 2] Implement this!
    int ret = -ENOENT;

    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] != 0) {
            if (strcmp(myRoot.root[i].name, path + 1) == 0)  //fileName == path
            {
                myRoot.root[i].mode = mode;
                ret = 0;
                writeBlockOfStructure("root", i, myRoot.root[i]);
                break;
            }
        }
    }

    RETURN(ret);
}

/// @brief Change the owner of a file.
///
/// Change the user and group identifier in the meta data of a file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] uid New user id.
/// \param [in] gid New group id.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseChown(const char *path, uid_t uid, gid_t gid) {
    LOGM();

    // TODO: [PART 2] Implement this!
    int ret = -ENOENT;

    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] != 0) {
            if (strcmp(myRoot.root[i].name, path + 1) == 0)  //fileName == path
            {
                myRoot.root[i].uid = uid;
                myRoot.root[i].gid = gid;
                ret = 0;
                writeBlockOfStructure("root", i, myRoot.root[i]);
                break;
            }
        }
    }

    RETURN(ret);
}

/// @brief Open a file.
///
/// Open a file for reading or writing. This includes checking the permissions of the current user and incrementing the
/// open file count.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] fileInfo Can be ignored in Part 1
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseOpen(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    int ret = -ENOENT;
    for (int i = 0; i < Root_Size_arr; i++) {
        if (myRoot.root[i].name[0] != 0) {
            if (strcmp(myRoot.root[i].name, path + 1) == 0)  //fileName == path
            {
                fileInfo->fh = i;
                char puffer[BLOCK_SIZE];
                blockDevice->read(myRoot.root[i].firstBlockInFAT, puffer);
                memcpy(openfiles[i].puffer, puffer, BLOCK_SIZE);
                openfiles[i].blockNo = myRoot.root[i].firstBlockInFAT;
                ret = 0;
                myRoot.root[i].atime = time(NULL);
                writeBlockOfStructure("root", i, myRoot.root[i]);
                break;
            }
        }
    }

    RETURN(ret);
}

/// @brief Read from a file.
///
/// Read a given number of bytes from a file starting from a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [out] buf The data read from the file is stored in this array. You can assume that the size of buffer is at
/// least 'size'
/// \param [in] size Number of bytes to read
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file
/// \param [in] fileInfo Can be ignored in Part 1
/// \return The Number of bytes read on success. This may be less than size if the file does not contain sufficient bytes.
/// -ERRNO on failure.
int MyOnDiskFS::fuseRead(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!
    //Variablen Initialisieren
    int ret = -ENOENT;
    int indexInRoot = fileInfo->fh;
    int bytesRead = 0;
    int blockInFile;


    //Search Fat (walk to Position after offset)
//    int FatIndex = myFat.fat[myRoot.root[indexInRoot].firstBlockInFAT];
    int FatIndex = myRoot.root[indexInRoot].firstBlockInFAT;
    stepThroughOffset(offset, FatIndex, blockInFile);
    LOGF("FatIndex %d", FatIndex);

    char puffer[BLOCK_SIZE];
    if (openfiles[indexInRoot].blockNo == FatIndex) {
        memcpy(puffer, openfiles[indexInRoot].puffer, BLOCK_SIZE);
    } else {
        blockDevice->read(FatIndex, puffer);
        openfiles[indexInRoot].blockNo = FatIndex;
        memcpy(openfiles[indexInRoot].puffer, puffer, BLOCK_SIZE);
    }
    int offsetInBlock = offset % BLOCK_SIZE;
    //Read first Block
    if (size < BLOCK_SIZE - offsetInBlock) //erster block + offset kleiner als ein Block
    {
        memcpy(buf, puffer + offsetInBlock, size);
        bytesRead += size;
    } else { // if size >= Block_Size - offset => restlicher Block lesen
        memcpy(buf, puffer + offsetInBlock, BLOCK_SIZE - offsetInBlock);
        bytesRead += BLOCK_SIZE - offsetInBlock;
    }
    int anzahlBloecke = byteToBlock(size - bytesRead);
    //Blöcke die wir noch lesen müssen (1. Block wurde bereits gelesen)

    memset(puffer, 0, BLOCK_SIZE);

    for (int i = 0; i <= anzahlBloecke; i++) {
        FatIndex = myFat.fat[FatIndex];
        LOGF("FatIndex %d", FatIndex);

        blockDevice->read(FatIndex, puffer);
        if (FatIndex == myFat.EOC) {
            LOGF("EOC an stelle %d gefunden", i);
            break;
        }
        if (size - bytesRead < BLOCK_SIZE) { //übrig zu lesenden Bytes sind < 1 Block
            LOGF("size %d, bytesRead %d", size, bytesRead);
            memcpy(buf + bytesRead, puffer, size - bytesRead);
            bytesRead += size - bytesRead;
            LOGF("bytesRead new %d", bytesRead);
        } else {
            LOGF("else -> bytesRead before %d", bytesRead);
            memcpy(buf + bytesRead, puffer, BLOCK_SIZE);
            bytesRead += BLOCK_SIZE;
            LOGF("else -> bytesRead after %d", bytesRead);
        }
    }

    ret = bytesRead;

    RETURN(ret);
}

/// @brief Write to a file.
///
/// Write a given number of bytes to a file starting at a given position.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// Note that the file content is an array of bytes, not a string. I.e., it is not (!) necessarily terminated by '\0'
/// and may contain an arbitrary number of '\0'at any position. Thus, you should not use strlen(), strcpy(), strcmp(),
/// ... on both the file content and buf, but explicitly store the length of the file and all buffers somewhere and use
/// memcpy(), memcmp(), ... to process the content.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] buf An array containing the bytes that should be written.
/// \param [in] size Number of bytes to write.
/// \param [in] offset Starting position in the file, i.e., number of the first byte to read relative to the first byte of
/// the file.
/// \param [in] fileInfo Can be ignored in Part 1.
/// \return Number of bytes written on success, -ERRNO on failure.
int
MyOnDiskFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    int ret = -ENOENT;
    int indexInRoot = fileInfo->fh;
    int blockInFile;


    //Search Fat (walk to Position after offset)
//    int FatIndex = myFat.fat[myRoot.root[indexInRoot].firstBlockInFAT];
    int FatIndex = myRoot.root[indexInRoot].firstBlockInFAT;
    int countOffset = stepThroughOffset(offset, FatIndex, blockInFile); // Stop before reaching eoc, 1 block before eoc
    LOGF("Fatindex %d", FatIndex);

    //Caching of one block
//    TODO: Dieser gecachte Block wird nicht aktualisiert, am Anfang stehen da immer Nullen, drin, eigentlich müssten aber nach dem Schreiben
//            dieser gecachte Block auch nochmals aktualisiert werden.
    char puffer[BLOCK_SIZE];
    if (openfiles[indexInRoot].blockNo == FatIndex) {
        memcpy(puffer, openfiles[indexInRoot].puffer, BLOCK_SIZE);
    } else {
        blockDevice->read(FatIndex, puffer);
        openfiles[indexInRoot].blockNo = FatIndex;
        memcpy(openfiles[indexInRoot].puffer, puffer, BLOCK_SIZE);
    }
    int offsetInBlock = offset % BLOCK_SIZE;
    LOGF("Offset in Block for %d input: %d", size, offsetInBlock);

//  If the Block that is reached after going through the Offset is the EOC then new Blocks will be allocated, the eoc block shall not be written on
    int newBlock;
    if (myFat.fat[myFat.fat[FatIndex]] == myFat.EOC && myRoot.root[indexInRoot].size >= BLOCK_SIZE && offset >= 512) {
        newBlock = getFirstFreeBlockOfDmap();
        myDmap.dmap[newBlock] = 1;
        writeBlockOfStructure("dmap", newBlock);
        int temporaryEOCIndex = myFat.fat[FatIndex];
        myFat.fat[FatIndex] = newBlock;
        writeBlockOfStructure("fat", FatIndex);
        FatIndex = newBlock; //update of FatIndex
        myFat.fat[newBlock] = temporaryEOCIndex;
        writeBlockOfStructure("fat", newBlock);
    }

    memcpy(puffer + offsetInBlock, buf, BLOCK_SIZE - offsetInBlock);
    blockDevice->write(FatIndex, puffer);
//    Aktualisieren des Caches nach dem Der Puffer beschrieben wurde
    memcpy(openfiles[indexInRoot].puffer, puffer, BLOCK_SIZE);

    int anzahlBloecke = byteToBlock(size - (BLOCK_SIZE - offsetInBlock)); // blocks to write
    LOGF("AnzahlBloecke: %d", anzahlBloecke);

    int previousFat = FatIndex; // Point of seperating Fat -> going from last block written to new index in fat
    int eocIndex = myFat.fat[FatIndex];


    for (int i = 0; i < anzahlBloecke; i++) {
        int nextFat = getFirstFreeBlockOfDmap();
        myDmap.dmap[nextFat] = 1;
        writeBlockOfStructure("dmap", nextFat);
        myFat.fat[previousFat] = nextFat;
        previousFat = nextFat;
        LOGF("Loop Fatindex %d  ", previousFat);
        writeBlockOfStructure("fat", previousFat);
        //Daten schreiben
        char writeBuffer[BLOCK_SIZE];
        memcpy(writeBuffer, buf + (i + 1) * BLOCK_SIZE, BLOCK_SIZE);
        blockDevice->write(nextFat, writeBuffer);
    }

    if (anzahlBloecke >= 0) {
        myFat.fat[previousFat] = eocIndex;
        LOGF("prevFat %d ; fat[previousFat] %d -> backtofat %d", previousFat, myFat.fat[previousFat],
             eocIndex);
    }
//    @anzahlBloecke kann nie kleiner als 0 werden, daher ist der Ternäroperator überflüssig!
//    int newSize = anzahlBloecke >= 0 ? (countOffset + anzahlBloecke + 1) * BLOCK_SIZE : countOffset * BLOCK_SIZE;
    int newSize = (countOffset + anzahlBloecke + 1) * BLOCK_SIZE;
    LOGF("newSize %d", newSize);
    //fuseTruncate(path, size);
    int oldSize = myRoot.root[indexInRoot].size;
    if (newSize >= oldSize) {
        myRoot.root[indexInRoot].size = newSize;
    } else {
        fuseTruncate(path, newSize);
    }
    myRoot.root[indexInRoot].atime = time(NULL);
    writeBlockOfStructure("root", indexInRoot, myRoot.root[indexInRoot]);

    ret = size;

    RETURN(ret);
}


/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    int ret = -ENOENT;

    int i = fileInfo->fh;

    openfiles[i].blockNo = -1;

    ret = 0;

    RETURN(ret);

}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize) {
    LOGM();

    // TODO: [PART 2] Implement this!
    int ret = -ENOENT;

    for (int rootIndex = 0; rootIndex < Root_Size_arr; rootIndex++) {
        if (myRoot.root[rootIndex].name[0] != 0) {
            if (strcmp(myRoot.root[rootIndex].name, path + 1) == 0)  //fileName == path
            {
                int oldSize = myRoot.root[rootIndex].size;
                myRoot.root[rootIndex].mtime = time(NULL);
                myRoot.root[rootIndex].size = newSize;
                writeBlockOfStructure("root", rootIndex, myRoot.root[rootIndex]);
                if (oldSize > newSize) { //file wird kleiner
                    int anzahlBloecke = byteToBlock(oldSize);
                    int bloeckeUebrig = byteToBlock(newSize); //Blöcke die wir brauchen
                    int FatIndex = myRoot.root[rootIndex].firstBlockInFAT;
                    int EOCIndex = -1;
                    for (int blockIndex = 0; blockIndex < anzahlBloecke; blockIndex++) {
                        if (blockIndex < bloeckeUebrig) {
                            EOCIndex = myFat.fat[FatIndex];
                            FatIndex = myFat.fat[FatIndex];
                        } else {
                            myDmap.dmap[FatIndex] = 0;
                            writeBlockOfStructure("dmap", FatIndex);
                            FatIndex = myFat.fat[FatIndex];
                        }
                    }
                    if (EOCIndex > 0) {
                        myFat.fat[EOCIndex] = myFat.EOC;
                        writeBlockOfStructure("fat", EOCIndex);
                    }
                } else if (oldSize < newSize) { //file wird größer
//                    int delta = newSize - oldSize;
//                    int anzahlBloecke = delta / BLOCK_SIZE;
                    int anzahlBloecke = byteToBlock(newSize - oldSize);
                    //Fat dursuchen
//                    int FatIndex = myFat.fat[myRoot.root[rootIndex].firstBlockInFAT];
                    int FatIndex = myRoot.root[rootIndex].firstBlockInFAT;
//                    for (int j = 0; j < oldSize / BLOCK_SIZE; j++) {
                    for (int j = 0; j < byteToBlock(oldSize); j++) {
                        if (FatIndex != myFat.EOC) {
                            FatIndex = myFat.fat[FatIndex];
                        }
                    }
                    //DMAP Blöcke reservieren und in FAT verlinken
                    for (int i = 0; i < anzahlBloecke; i++) {
                        for (int j = offsetDMAP_array; i < Dmap_Size_arr; i++) {
                            if (myDmap.dmap[j] == 0) {
                                myFat.fat[FatIndex] = j;
                                myDmap.dmap[FatIndex] = 1;
                                writeBlockOfStructure("dmap", FatIndex);
                                writeBlockOfStructure("fat", FatIndex);
                                FatIndex = j;
                            }
                        }
                    }
                    myFat.fat[FatIndex] = myFat.EOC;
                    writeBlockOfStructure("fat", FatIndex);
                } else { //Size = newSize
                    RETURN(0);
                }
                ret = 0;
                break;
            }
        }
    }

    RETURN(ret);
}

/// @brief Truncate a file.
///
/// Set the size of a file to the new size. If the new size is smaller than the old size, spare bytes are removed. If
/// the new size is larger than the old size, the new bytes may be random. This function is called for files that are
/// open.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newSize New size of the file.
/// \param [in] fileInfo Can be ignored in Part 1.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseTruncate(const char *path, off_t newSize, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!
    int ret = fuseTruncate(path, newSize);

    RETURN(ret);

}

/// @brief Read a directory.
///
/// Read the content of the (only) directory.
/// You do not have to check file permissions, but can assume that it is always ok to access the directory.
/// \param [in] path Path of the directory. Should be "/" in our case.
/// \param [out] buf A buffer for storing the directory entries.
/// \param [in] filler A function for putting entries into the buffer.
/// \param [in] offset Can be ignored.
/// \param [in] fileInfo Can be ignored.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseReaddir(const char *path, void *buf, fuse_fill_dir_t filler, off_t offset,
                            struct fuse_file_info *fileInfo) {
    LOGM();
    int ret = -ENXIO;
    // TODO: [PART 2] Implement this!

    LOGF("--> Getting The List of Files of %s\n", path);

    filler(buf, ".", NULL, 0); // Current Directory
    filler(buf, "..", NULL, 0); // Parent Directory

    if (strcmp(path, "/") == 0)
        // If the user is trying to look at the files of the root directory => show the following
    {
        for (int i = 0; i < Root_Size_arr; i++) {
            if (myRoot.root[i].name[0] != 0) {
                filler(buf, myRoot.root[i].name, NULL, 0);
            }
        }
        ret = 0;
    }
    RETURN(ret);
}

/// Initialize a file system.
///
/// This function is called when the file system is mounted. You may add some initializing code here.
/// \param [in] conn Can be ignored.
/// \return 0.
void *MyOnDiskFS::fuseInit(struct fuse_conn_info *conn) {
    // Open logfile
    this->logFile = fopen(((MyFsInfo *) fuse_get_context()->private_data)->logFile, "w+");
    if (this->logFile == NULL) {
        fprintf(stderr, "ERROR: Cannot open logfile %s\n", ((MyFsInfo *) fuse_get_context()->private_data)->logFile);
    } else {
        // turn of logfile buffering
        setvbuf(this->logFile, NULL, _IOLBF, 0);

        LOG("Starting logging...\n");

        LOG("Using on-disk mode");

        LOGF("Container file name: %s", ((MyFsInfo *) fuse_get_context()->private_data)->contFile);

        int ret = this->blockDevice->open(((MyFsInfo *) fuse_get_context()->private_data)->contFile);

        if (ret >= 0) {
            LOG("Container file does exist, reading");

            // TODO: [PART 2] Read existing structures form file
            //read Superblock
            char sb_puffer[BLOCK_SIZE];
            blockDevice->read(0, sb_puffer);
            memcpy(&mySuperblock, &sb_puffer, BLOCK_SIZE);
            LOGF("Superblock gelesen %d", mySuperblock.anzahlBloecke);

            //read DMAP
            int i = 0;
            char *ptrDMAP = (char *) (myDmap.dmap);
            char dmap_puffer[BLOCK_SIZE];
            for (int blockNo = startDMAP; blockNo <= endDMAP; blockNo++, i++) {
                blockDevice->read(blockNo, dmap_puffer);
                memcpy(ptrDMAP + BLOCK_SIZE * i, dmap_puffer, BLOCK_SIZE);
            }
            LOG("DMAP gelesen");

            //read FAT
            i = 0;
            char *ptrFAT = (char *) (myFat.fat);
            char fat_puffer[BLOCK_SIZE];
            for (int blockNo = startFAT; blockNo <= endFAT; blockNo++, i++) {
                blockDevice->read(blockNo, fat_puffer);
                memcpy(ptrFAT + BLOCK_SIZE * i, fat_puffer, BLOCK_SIZE);
            }
            LOG("FAT gelesen");

            //read Root
            i = 0;
            char root_puffer[BLOCK_SIZE];
            for (int blockNo = startROOT; blockNo <= endROOT; blockNo++, i++) {
                blockDevice->read(blockNo, root_puffer);
                memcpy(&myRoot.root[i], root_puffer, sizeof(MyFsFileInfo));
            }
            LOG("Root gelesen");

        } else if (ret == -ENOENT) {
            LOG("Container file does not exist, creating a new one");

            ret = this->blockDevice->create(((MyFsInfo *) fuse_get_context()->private_data)->contFile);

            if (ret >= 0) {

                // TODO: [PART 2] Create empty structures in file
                Superblock mySuperblock;
                mySuperblock.anzahlBloecke = blockCount;
                LOGF("Blockcount %d", blockCount);
                mySuperblock.fileSystemSize = blockToByte(blockCount); //51200*512 Byte = 26214400 Byte = 25,6 MiB
                LOGF("fileSystemSize %d", blockToByte(blockCount));
                mySuperblock.startDmap = startDMAP; //nach 512 Bytes (1. Block)
                //DMAP: nach Superblock, 51200 / 512 = 100 (Anzahl aller Blöcke / Boolean pro Block -> Boolean = 1 Byte
                LOGF("startDMAP: %d", startDMAP);
                mySuperblock.startFat = startFAT;
                LOGF("startFAT %d", startFAT);
                //FAT: 51200/128=400
                mySuperblock.startRoot = startROOT;
                LOGF("startRoot %d", startROOT);
                //Root: Jede Inode (MyFsFileInfo) bekommt 1 Block
                mySuperblock.startData = startDATA;// nach Root
                LOGF("startData %d", startDATA);

                Dmap myDmap;

                FAT myFat;
                // 400 Blöcke Fat-Größe BlockCount / (512 Byte Blocksize / 4 Byte Größe Integer)

//                TODO:Muss man hier die Namen nullen, wenn man unten im BlockDev, alles mit 0en auf Byte ebene füllt
                Root myRoot;
                /*for (int i = 0; i < Root_Size_arr; i++) {
                    myRoot.root[i].name[0] = 0;
                }*/

//                LOGF("%s", myRoot.root[0].name);

                //write Superblock
                char sb_puffer[BLOCK_SIZE];
                memcpy(sb_puffer, &mySuperblock, BLOCK_SIZE);
                blockDevice->write(startSUPERBLOCK, sb_puffer);

                //write DMAP
                //Block mit 0en befüllen
//              TODO: Memset
                char dmap_puffer[BLOCK_SIZE];
                for (int i = 0; i < BLOCK_SIZE; i++) {
                    dmap_puffer[i] = 0x0000;
                }
                for (int blockNo = mySuperblock.startDmap;
                     blockNo <= endDMAP; blockNo++) {
                    //memcpy(&myDmap + blockNo,dmap_puffer, BLOCK_SIZE);
                    blockDevice->write(mySuperblock.startDmap + blockNo, dmap_puffer);
                }

                //write FAT
                //TODO: Memset
                char fat_puffer[BLOCK_SIZE];
                for (int i = 0; i < BLOCK_SIZE; i++) {
                    fat_puffer[i] = 0x0000;
                }
                for (int blockNo = startFAT;
                     blockNo <= endFAT; blockNo++) {
                    //memcpy(fat_puffer, &myFat + blockNo, BLOCK_SIZE);
                    blockDevice->write(mySuperblock.startFat + blockNo, fat_puffer);
                }

                //write Root => Blöcke mit 0en beschreiben (freier Block für Inode)
                char root_puffer[BLOCK_SIZE];
                for (int i = 0; i < BLOCK_SIZE; i++) {
                    root_puffer[i] = 0x0000;
                }
                int i = 0;
                for (int blockNo = startROOT;
                     blockNo <= endROOT; blockNo++, i++) {
                    blockDevice->write(mySuperblock.startRoot + blockNo, root_puffer);
                    //memcpy(myRoot.root+i, root_puffer, sizeof(MyFsFileInfo));
                }

            }
        }

        if (ret < 0) {
            LOGF("ERROR: Access to container file failed with error %d", ret);
        }
    }

    return 0;
}

/// @brief Clean up a file system.
///
/// This function is called when the file system is unmounted. You may add some cleanup code here.
void MyOnDiskFS::fuseDestroy() {
    LOGM();

    // TODO: [PART 2] Implement this!

}

// TODO: [PART 2] You may add your own additional methods here!

/**
 * Schreibe einen Block auf das BlockDevice mit der geupdateten Struktur/Array
 * @param structure string als Identifier => "root" || "dmap" || "fat"
 * @param indexInArray index im jeweiligen Array
 * @param newFile nur wichtig bei root => geupdatete/neue Inode
 */
void MyOnDiskFS::writeBlockOfStructure(char *structure, uint32_t indexInArray, struct MyFsFileInfo newFile) {
    if (strcmp(structure, "root") == 0) {
        char puffer_block[BLOCK_SIZE];
        memcpy(puffer_block, &newFile, sizeof(MyFsFileInfo));
        blockDevice->write(startROOT + indexInArray, puffer_block);
    }
}

// TODO: In header
int MyOnDiskFS::getFirstFreeBlockOfDmap() {
    for (int i = offsetDMAP_array; i < Dmap_Size_arr; i++) {
        if (myDmap.dmap[i] == 0) return i;
    }
}

void MyOnDiskFS::writeBlockOfStructure(char *structure, uint32_t indexInArray) {
    if (strcmp(structure, "dmap") == 0) {
        char puffer_block[BLOCK_SIZE];
        uint32_t block = indexInArray / BLOCK_SIZE;
        memcpy(puffer_block, myDmap.dmap + (block * BLOCK_SIZE), BLOCK_SIZE);
        blockDevice->write(startDMAP + block, puffer_block);
    } else if (strcmp(structure, "fat") == 0) {
        char puffer_block[BLOCK_SIZE];
        uint32_t block = indexInArray / (BLOCK_SIZE / 4); //sizeof(uint32_t) => 4 bytes
        memcpy(puffer_block, myFat.fat + (block * (BLOCK_SIZE / 4)), BLOCK_SIZE);
        blockDevice->write(startFAT + block, puffer_block);
    }
}
/**
 *
 * @param offset
 * @param FatIndex
 * @param blockInFile
 * @return Offset umgerechnet in Blöcke
 */
// TODO: In header
int MyOnDiskFS::stepThroughOffset(off_t offset, int &FatIndex, int &blockInFile) {
    blockInFile = offset / BLOCK_SIZE;
    int j;
    for (j = 0; j < blockInFile; j++) {
        if(myFat.fat[myFat.fat[FatIndex]] == myFat.EOC) {
            break;
        }
        FatIndex = myFat.fat[FatIndex];
    }
    return j;
}


// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance = new MyOnDiskFS();
}