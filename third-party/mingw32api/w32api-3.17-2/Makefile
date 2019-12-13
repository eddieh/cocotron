##
# Makefile for w32api
##

# Project info
Project = w32api
Version	= 3.17-2

PROJECT_DIR = $(Project)-$(Version)
PROJECT_SUB_DIR = $(Project)-$(Version)-mingw32-dev
PROJECT_PATH = $(THIRD_PARTY_PATH)/ming$(Project)/$(PROJECT_DIR)/$(PROJECT_SUB_DIR)

all:
	mkdir -p $(INTERFACE_PATH)
	cp -r $(PROJECT_PATH)/* $(INTERFACE_PATH)
