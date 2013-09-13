#!/bin/sh -eux

export YT_PROXY="kant.yt.yandex.net"

IMPORT_QUEUE="//sys/cron/tables_to_import"
LOCK_PATH="//sys/cron/import_lock"

/opt/cron/tools/import_table_from_mr.py \
    --tables "$IMPORT_QUEUE" \
    --mapreduce-binary "/opt/cron/tools/mapreduce" \
    --compression-codec "gzip_best_compression" \
    --pool "restricted" \
    --lock "$LOCK_PATH" \
    --ignore \
    --fastbone

