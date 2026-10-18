#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Usage: check-frame-log.sh <twister output directory>
#
# Decodes the frames that tests/common/fake_radio.c logs in the handler.log of every integration
# test and fails unless tshark decrypts each frame with NWK security with the test network key.

set -eu

# The key of tests/integration/nwk_data
key=00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

logs=$(find "$1" -path '*zzb.integration.*' -name handler.log)
if [ -z "$logs" ]; then
	echo "$1: no handler.log of an integration test"
	exit 1
fi

for log in $logs; do
	# One text2pcap line per frame: offset 0, then the bytes before the ASCII column
	awk '
	function flush() {
		if (bytes != "") {
			print "000000" bytes
		}
		bytes = ""
		in_frame = 0
	}
	/<dbg> fake_radio: transmit:/ { flush(); in_frame = 1; next }
	in_frame && /^ +[0-9a-f][0-9a-f] / {
		line = $0
		sub(/\|.*/, "", line)
		bytes = bytes " " line
		gsub(/ +/, " ", bytes)
		next
	}
	{ flush() }
	END { flush() }
	' "$log" > "$tmp/frames.txt"

	if [ ! -s "$tmp/frames.txt" ]; then
		echo "$log: no frames"
		exit 1
	fi

	text2pcap -q -l 230 "$tmp/frames.txt" "$tmp/frames.pcap" > /dev/null
	tshark -r "$tmp/frames.pcap" -o "uat:zigbee_pc_keys:\"$key\",\"Normal\",\"zzb test\"" \
		-T fields -E separator=';' -e frame.number -e zbee_nwk.security -e zbee.sec.key \
		> "$tmp/fields.txt"

	secured=$(awk -F';' '$2 == "True"' "$tmp/fields.txt" | wc -l)
	failed=$(awk -F';' '$2 == "True" && $3 == "" { printf " %s", $1 }' "$tmp/fields.txt")
	if [ "$secured" -eq 0 ]; then
		echo "$log: no frame with NWK security"
		exit 1
	fi

	if [ -n "$failed" ]; then
		echo "$log: frames not decrypted:$failed"
		exit 1
	fi

	echo "$log: $secured frames with NWK security decrypted"
done
