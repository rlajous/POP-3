#!/bin/bash
cat > /dev/null
cat << EOF | unix2dos
X-Header: true

.hola
.
EOF
