#
# Makefile for Lab 3 - FUSE file system
# CS 5600 Fall 2023
#

# Makefiles use a horrible language. Variables are pretty much OK:
#
CFLAGS = -Wall -pedantic -g
LDFLAGS = -lfuse3
CC = gcc
EXES = lab3-fuse

# rules are "target: <dependencies>" followed by zero or more
# lines of actions to create the target
#
all: $(EXES)

# '$^' expands to all the dependencies (i.e. misc.o homework.o image.o)
# and $@ expands to the target ('lab3-fuse')
#
lab3-fuse: homework.o hw3fuse.o misc.o
	$(CC) -g $^ -o $@ $(LDFLAGS)

# see https://beebo.org/haycorn/2015-04-20_tabs-and-makefiles.html
# for the stupid reason why the action line has to begin with a tab
# character, and not some number of spaces.

# standard practice is to add a 'clean' target that gets rid of
# build output.
#
clean:
	rm -f *.o $(EXES)
