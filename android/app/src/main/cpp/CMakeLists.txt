# IonoProp - a C-mag fordítása Androidra (NDK)
# A forrásfájlok a projekt közös src/ mappájából jönnek, így az asztali
# programmal ugyanaz a számítás fut.
cmake_minimum_required(VERSION 3.22.1)
project(ionoprop C)

set(ROOT ${CMAKE_CURRENT_SOURCE_DIR}/../../../../..)
set(SRC  ${ROOT}/src)

add_library(ionoprop SHARED
    ${SRC}/jni_bridge.c
    ${SRC}/appcore.c
    ${SRC}/canvas.c
    ${SRC}/mapview.c
    ${SRC}/map_data.c
    ${SRC}/parse.c
    ${SRC}/geo.c
    ${SRC}/solar.c
    ${SRC}/model.c
    ${SRC}/predict.c
    ${SRC}/config.c
    ${SRC}/pskr.c
    ${SRC}/validate.c
    ${SRC}/cache.c
    ${SRC}/compat.c
    ${SRC}/spots.c
    ${SRC}/alerts.c
    ${SRC}/targets.c
    ${SRC}/i18n.c
    ${SRC}/meteor.c
    ${ROOT}/third_party/cJSON.c
)

# FreeType nélkül (a feliratokat a Kotlin rajzolja), libmosquitto helyett saját MQTT-kliens
target_compile_definitions(ionoprop PRIVATE IONOPROP_NO_FONT IONOPROP_MINI_MQTT _GNU_SOURCE)
target_compile_options(ionoprop PRIVATE -std=gnu11 -O2 -Wall -Wno-unused-parameter)
target_link_libraries(ionoprop m log)

# 16 KB-os memórialapok támogatása (Android 15+ eszközök, a Google Play előírja)
target_link_options(ionoprop PRIVATE "-Wl,-z,max-page-size=16384")
