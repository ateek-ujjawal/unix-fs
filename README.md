# unix-fs
A simplified file system based on ext2. Worked with Haonan Lu to build this as part of Lab 3 assignment of cs5600.

The file system runs on FUSE and parameters passed to it. The file system uses 1KB blocks. It is based on ext2 with allocation bitmaps and inodes being grouped at the start of the disk. Inodes have 6 direct pointers, an indirect pointer and a double indirect pointer. The following FUSE functions have been implemented:

* getattr - get attributes of a file/directory
* readdir - enumerate entries in a directory
* create - create a file
* read, write - read data from / write to a file
* unlink - delete a file
* truncate - delete file contents but not the file itself
* mkdir, rmdir - create, delete directory
* **rename** - rename a file (only within the same directory) ***(partially broken, need to fix)***
* chmod - change file permissions
* init - constructor

# How to run
To run the FUSE filesystem first you need to install some FUSE libraries:

`sudo apt install libfuse3-dev`

Now onto compiling the code and mounting it to a directory called "tmp":

`make
mkdir tmp
./lab3-fuse -s -d -image disk1.img tmp`

To unmount use:

`fusermount -u tmp`
