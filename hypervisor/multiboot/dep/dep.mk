#
# multiboot module dependency makefile
#

# TODO: the path should update to its dep module's include dir

MOD_INC_PATH += ../include/lib
MOD_INC_PATH += ../include/arch
MOD_INC_PATH += ../include/debug
MOD_INC_PATH += ../include/public

override MOD_INC_PATH := $(realpath $(MOD_INC_PATH))

