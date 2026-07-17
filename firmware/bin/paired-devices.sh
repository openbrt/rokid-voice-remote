#!/bin/sh
set -eu

DATABASE=${BSA_DEVICE_DATABASE:-/data/bluetooth/bt_devices.xml}
[ -r "$DATABASE" ] || { echo "cannot read $DATABASE" >&2; exit 1; }

printf 'BD_ADDR\tNAME\n'
awk -F '[<>]' '
    /<bd_addr>/ { address=$3 }
    /<device_name>/ { name=$3 }
    /<Link_key_present>/ {
        if ($3 == "1" && address != "") {
            if (name == "") name="(unnamed)"
            print address "\t" name
        }
        address=""
        name=""
    }
' "$DATABASE"
