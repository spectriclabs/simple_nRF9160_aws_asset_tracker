# SPDX-License-Identifier: Apache-2.0

cmake_minimum_required(VERSION 3.20.0)

find_package(Zephyr REQUIRED HINTS $ENV{ZEPHYR_BASE})
project(simple_aws_asset_tracker)

target_sources(app PRIVATE src/main.c src/json_common.c src/location_module.c src/modem_module.c)
