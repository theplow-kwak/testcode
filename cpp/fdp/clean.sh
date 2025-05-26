#!/bin/bash

find subprojects -mindepth 1 -maxdepth 1 -type d ! -name 'packagefiles' -exec rm -rf {} +
rm subprojects/openssl.wrap
rm -rf .build/
rm -rf .build-ci/