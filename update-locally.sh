#!/bin/bash
set -e
# clean old files
hexo clean
# generate new files
hexo g
# start local server
hexo s
