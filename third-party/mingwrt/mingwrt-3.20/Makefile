##
# Makefile for mingwrt
##

# Project info
Project = mingwrt
Version	= 3.20

PROJECT_DIR = $(Project)-$(Version)
PROJECT_SUB_DIR = $(Project)-$(Version)-mingw32-dev
PROJECT_PATH = $(THIRD_PARTY_PATH)/$(Project)/$(PROJECT_DIR)/$(PROJECT_SUB_DIR)

all:
	mkdir -p $(INTERFACE_PATH)
	cp -r $(PROJECT_PATH)/* $(INTERFACE_PATH)
