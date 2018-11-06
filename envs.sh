#!/bin/bash
cat << EOF | unix2dos
X-Media: $FILTER_MEDIAS
X-Version: $POP3FILTER_VERSION
X-User: $POP3_USERNAME
X-Origin: $POP3_SERVER
Body.
$FILTER_MSG
EOF
cat > /dev/nulls
