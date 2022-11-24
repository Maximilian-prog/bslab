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
#define Superblock_Size 1
#define Dmap_Size_arr 51200
#define Dmap_Size 100
#define FAT_Size 800
#define FAT_Size_arr (800*(BLOCK_SIZE)/4)
#define Root_Size 64
#define Root_Size_arr 64
#define Data_Size 0

#define byteToBlock(byte) ((byte)/(BLOCK_SIZE))
#define blockCount 51200
#define blockToByte(numberOfBlocks) ((BLOCK_SIZE) * (numberOfBlocks))
#define startSUPERBLOCK 0
#define startDMAP 1
#define startFAT ((Superblock_Size) + (Dmap_Size))
#define startROOT ((startFAT) + 800)
#define startDATA ((startROOT) + (NUM_DIR_ENTRIES))

#define endSUPERBLOCK 1
#define endDMAP (startFAT - 1)
#define endFAT (startROOT - 1)
#define endROOT (startDATA - 1)
#define endDATA blockCount

struct Superblock {
    int fileSystemSize;
    int anzahlBloecke;
    unsigned int startDmap;
    unsigned int startFat;
    unsigned int startRoot; // Inodes -> 64 Inodes insgesamt
    unsigned int startData;
};

struct Dmap {
    bool *dmap;
};

struct FAT {
    int EOC = 0xFFFFFFFF; //EOC = -1;
    uint32_t *fat;
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
    MyFsFileInfo *root; //Array MyFsFileInfo (max 64)
};

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
    const int SIZE = BLOCK_SIZE;

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

    MyFsFileInfo current; //Schleifenvariable
    for (int i = 0; i < Root_Size_arr; i++) {
        current = myRoot.root[i]; //File was ausgelesen wird
        if (strcmp(current.name, "") == 0) // freier Eintrag in Root gefunden
        {
            myRoot.root[i] = newFile;
            char puffer_block[BLOCK_SIZE];
            memcpy(puffer_block, &newFile, sizeof(MyFsFileInfo));
            blockDevice->write(startROOT + i, puffer_block);
            break;
        }
    }

    int offset_dmap = startDATA;
    for (int i = offset_dmap; i < Dmap_Size_arr; i++) {
        if (myDmap.dmap[i] == 0) // freier Eintrag in DMAP gefunden
        {
            myDmap.dmap[i] = 1;
            char puffer_block[BLOCK_SIZE];
            uint32_t block = i / BLOCK_SIZE; //Anfang des Blocks als Adresse
            memcpy(puffer_block, &myDmap.dmap + i * block, BLOCK_SIZE);
            blockDevice->write(startDMAP + block, puffer_block);

            //vorderen Indexe der FAT für EOC-Indexe nutzen
            for (int j = FAT_Size_arr; j > mySuperblock.anzahlBloecke; j--) {
                if (myFat.fat[j] == 0) { //freier Platz für EOC gefunden (maximal 64 Dateien => 64 EOC's (also reicht das Ende der FAT vollkommen aus)
                    myFat.fat[i] = j;
                    myFat.fat[j] = myFat.EOC;
                    newFile.firstBlockInFAT = i;

                    //Blockdevice update mit FAT
                    char puffer_block[BLOCK_SIZE];
                    uint32_t block = i / BLOCK_SIZE; //Anfang des Blocks als Adresse
                    memcpy(puffer_block, &myFat.fat + i * block, BLOCK_SIZE);
                    blockDevice->write(startFAT+ block, puffer_block);

                    //Blockdevice update mit EOC in FAT
                    puffer_block[BLOCK_SIZE];
                    block = j / BLOCK_SIZE; //Anfang des Blocks als Adresse
                    memcpy(puffer_block, &myFat.fat + j * block, BLOCK_SIZE);
                    blockDevice->write(startFAT+ block, puffer_block);
                    ret=0;
                }
            break;
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

    RETURN(0);
}

/// @brief Rename a file.
///
/// Rename the file with with a given name to a new name.
/// Note that if a file with the new name already exists it is replaced (i.e., removed
/// before renaming the file.
/// You do not have to check file permissions, but can assume that it is always ok to access the file.
/// \param [in] path Name of the file, starting with "/".
/// \param [in] newpath  New name of the file, starting with "/".
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRename(const char *path, const char *newpath) {
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
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

    RETURN(0);
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

    RETURN(0);
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

    RETURN(0);
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

    RETURN(0);
}

/// @brief Read from a file.
///
/// Read a given number of bytes from a file starting form a given position.
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

    RETURN(0);
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
/// \param [in] fileInfo Can be ignored in Part 1 .
/// \return Number of bytes written on success, -ERRNO on failure.
int
MyOnDiskFS::fuseWrite(const char *path, const char *buf, size_t size, off_t offset, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
}

/// @brief Close a file.
///
/// \param [in] path Name of the file, starting with "/".
/// \param [in] File handel for the file set by fuseOpen.
/// \return 0 on success, -ERRNO on failure.
int MyOnDiskFS::fuseRelease(const char *path, struct fuse_file_info *fileInfo) {
    LOGM();

    // TODO: [PART 2] Implement this!

    RETURN(0);
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

    RETURN(0);
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

    RETURN(0);
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
        // If the user is trying to show the files/directories of the root directory show the following
    {
        for (int i = 0; i < Root_Size_arr; i++) {
            if (strcmp(myRoot.root[i].name,"") != 0) {
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
            myDmap.dmap = new bool[Dmap_Size_arr];
            int i = 0;
            char *ptrDMAP = (char *) (myDmap.dmap);
            char dmap_puffer[BLOCK_SIZE];
            for (int blockNo = startDMAP; blockNo <= endDMAP; blockNo++, i++) {
                blockDevice->read(blockNo, dmap_puffer);
                memcpy(ptrDMAP + BLOCK_SIZE * i, dmap_puffer, BLOCK_SIZE);
            }
            LOG("DMAP gelesen");

            //read FAT
            myFat.fat = new uint32_t[FAT_Size_arr];
            i = 0;
            char *ptrFAT = (char *) (myFat.fat);
            char fat_puffer[BLOCK_SIZE];
            for (int blockNo = startFAT; blockNo <= endFAT; blockNo++, i++) {
                blockDevice->read(blockNo, fat_puffer);
                memcpy(ptrFAT + BLOCK_SIZE * i, fat_puffer, BLOCK_SIZE);
            }
            LOG("FAT gelesen");

            //read Root
            myRoot.root = new MyFsFileInfo[Root_Size_arr];
            i = 0;
            char root_puffer[BLOCK_SIZE];
            for (int blockNo = startROOT; blockNo <= endROOT; blockNo++, i++) {
                MyFsFileInfo myFsFileInfo;
                blockDevice->read(blockNo, root_puffer);
                memcpy(&myFsFileInfo, root_puffer, sizeof(MyFsFileInfo));
                myRoot.root[i] = myFsFileInfo;
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
                //FAT: 800 Da worst-case ein Block pro Datei und der dazugehörige EOC auch ein Block verbraucht
                //-> 400 (1 Dateiblock + 1 EOC-Block); Annahme: nicht mehr als 400 solcher Dateien
                mySuperblock.startRoot = startROOT;
                LOGF("startRoot %d", startROOT);
                //Root: Jede Inode (MyFsFileInfo) bekommt 1 Block
                mySuperblock.startData = startDATA;// nach Root
                LOGF("startData %d", startDATA);

                Dmap myDmap;
                myDmap.dmap = new bool[Dmap_Size_arr];

                FAT myFat;
                // 800 Blöcke Fat-Größe * 512 Byte Blocksize / 4 Byte Größe Integer
                myFat.fat = new unsigned int[FAT_Size_arr];

                Root myRoot;
                myRoot.root = new MyFsFileInfo[Root_Size_arr];

                //write Superblock
                char sb_puffer[BLOCK_SIZE];
                memcpy(sb_puffer, &mySuperblock, BLOCK_SIZE);
                blockDevice->write(startSUPERBLOCK, sb_puffer);

                //write DMAP
                //Block mit 0en befüllen
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
                for (int blockNo = startROOT;
                     blockNo <= endROOT; blockNo++) {
                    blockDevice->write(mySuperblock.startRoot + blockNo, root_puffer);
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

// DO NOT EDIT ANYTHING BELOW THIS LINE!!!

/// @brief Set the static instance of the file system.
///
/// Do not edit this method!
void MyOnDiskFS::SetInstance() {
    MyFS::_instance = new MyOnDiskFS();
}
