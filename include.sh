#!/usr/bin/env bash

MOD_NPCBEASTMASTER_ROOT="$( cd "$( dirname "${BASH_SOURCE[0]}" )/" && pwd )"

source $MOD_NPCBEASTMASTER_ROOT"/conf/conf.sh.dist"

if [ -f $MOD_NPCBEASTMASTER_ROOT"/conf/conf.sh" ]; then
    source $MOD_NPCBEASTMASTER_ROOT"/conf/conf.sh"
fi
