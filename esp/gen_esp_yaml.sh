#!/usr/bin/env bash
# Generates the Spriggit YAML tree for SunHelmFollowerNeeds.esp: a bank of GlobalVariables used as
# engine-persisted storage for per-follower need state. Nothing but GLOB records - no quest, no
# aliases, no script attachments, so there is no VMAD anywhere in the plugin.
set -euo pipefail

OUT="${1:?usage: gen_esp_yaml.sh <output-dir>}"
PLUGIN="SunHelmFollowerNeeds.esp"
SLOTS=10

rm -rf "$OUT"
mkdir -p "$OUT/Globals"

cat > "$OUT/spriggit-meta.json" <<EOF
{
  "PackageName": "Spriggit.Yaml.Skyrim",
  "Version": "0.41.0",
  "Release": "SkyrimSE",
  "ModKey": "$PLUGIN"
}
EOF

cat > "$OUT/RecordData.yaml" <<EOF
SpriggitSource:
  PackageName: Spriggit.Yaml.Skyrim
  Version: 0.41
ModKey: $PLUGIN
GameRelease: SkyrimSE
ModHeader:
  # "Small" is Mutagen's name for the ESL / light-master flag, so this doesn't eat one of the 254
  # regular load order slots. Valid because every record here lives in 0x800-0xFFF, the range an
  # ESL is confined to. Flagging changes the records' runtime FormIDs to FExxx800, which is only
  # harmless because the plugin resolves these globals by EditorID and never by FormID.
  Flags:
  - Small
  Stats:
    Version: 1.7
  Author: Wollecs
  Description: Persistent storage for SunHelm Individual Follower Needs
  MasterReferences:
  - Master: Skyrim.esm
    FileSize: 0
EOF

emit_global() {
	local edid="$1" formid="$2" type="$3" value="$4"
	cat > "$OUT/Globals/${edid} - ${formid}_${PLUGIN}.yaml" <<EOF
MutagenObjectType: ${type}
FormKey: ${formid}:${PLUGIN}
EditorID: ${edid}
Data: ${value}
EOF
}

# 0x000800 is the first FormID a new plugin can use. The counter is advanced in the parent shell -
# doing it inside a $(...) substitution would increment a subshell copy and hand every record the
# same FormID.
id=$((0x000800))
take_id=""
next_id() {
	take_id=$(printf "%06X" $id)
	id=$((id + 1))
}

# Bumped only if the meaning of the slot globals ever changes, so a future build can migrate or
# discard old save data instead of misreading it.
next_id
emit_global "_SHFN_SchemaVersion" "$take_id" "GlobalShort" 1

for ((n = 0; n < SLOTS; n++)); do
	# FormID of the follower occupying this slot, split into 16-bit halves. A float32 holds any
	# integer up to 2^24 exactly, so each half is lossless; a whole 32-bit FormID would not be.
	# 0 in both halves means the slot is empty.
	next_id && emit_global "_SHFN_Slot${n}_RefLo" "$take_id" "GlobalFloat" 0
	next_id && emit_global "_SHFN_Slot${n}_RefHi" "$take_id" "GlobalFloat" 0
	next_id && emit_global "_SHFN_Slot${n}_Hunger" "$take_id" "GlobalFloat" 0
	next_id && emit_global "_SHFN_Slot${n}_Thirst" "$take_id" "GlobalFloat" 0
done

echo "generated $(ls "$OUT/Globals" | wc -l) global records in $OUT"
