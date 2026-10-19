#!/bin/sh
# SPDX-License-Identifier: Apache-2.0
#
# Usage: check-frame-log.sh <twister output directory>
#
# Decodes the frames that tests/common/fake_radio.c logs in the handler.log of every integration
# test and fails unless tshark decrypts every NWK and APS auxiliary header with the test keys.

set -eu

# The network keys 0 and 1 of the integration tests, the default trust center link key
# "ZigBeeAlliance09" and trust center link key B; tshark derives the key-transport and key-load
# keys itself
keys="00:01:02:03:04:05:06:07:08:09:0a:0b:0c:0d:0e:0f
10:11:12:13:14:15:16:17:18:19:1a:1b:1c:1d:1e:1f
5a:69:67:42:65:65:41:6c:6c:69:61:6e:63:65:30:39
20:21:22:23:24:25:26:27:28:29:2a:2b:2c:2d:2e:2f"

tmp=$(mktemp -d)
trap 'rm -rf "$tmp"' EXIT

logs=$(find "$1" -path '*zzb.integration.*' -name handler.log)
if [ -z "$logs" ]; then
	echo "$1: no handler.log of an integration test"
	exit 1
fi

# One uat option per key
set --
for key in $keys; do
	set -- "$@" -o "uat:zigbee_pc_keys:\"$key\",\"Normal\",\"zzb test\""
done

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
	# Each auxiliary header adds a key identifier, each decrypted one a key. tshark does not
	# decrypt the empty payload of an APS ACK (frame type 0x02) with APS security.
	tshark -r "$tmp/frames.pcap" "$@" -T fields -E separator=';' -e frame.number \
		-e zbee_nwk.security -e zbee_aps.security -e zbee_aps.type -e zbee.sec.key_id \
		-e zbee.sec.key > "$tmp/fields.txt"

	secured=$(awk -F';' '$2 == "True" || $3 == "True"' "$tmp/fields.txt" | wc -l)
	failed=$(awk -F';' '$2 == "True" || $3 == "True" {
		missing = split($5, ids, ",") - split($6, keys, ",")
		if (missing == 1 && $3 == "True" && $4 == "0x02") {
			missing = 0
		}
		if (missing != 0) {
			printf " %s", $1
		}
	}' "$tmp/fields.txt")
	if [ "$secured" -eq 0 ]; then
		echo "$log: no frame with NWK or APS security"
		exit 1
	fi

	if [ -n "$failed" ]; then
		echo "$log: frames not decrypted:$failed"
		exit 1
	fi

	echo "$log: $secured frames with NWK or APS security decrypted"
done
