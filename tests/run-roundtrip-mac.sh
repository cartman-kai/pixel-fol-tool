#!/usr/bin/env bash
set -euo pipefail

usage() {
    cat <<'USAGE'
Usage:
  tests/run-roundtrip-mac.sh --cli-path c/fol_tool_mac [--input-fol path/to/sample.fol]

Runs a pack -> unpack -> edit -> repack -> unpack round-trip check.
Without --input-fol, the script first builds a synthetic .fol fixture.
USAGE
}

cli_path=""
input_fol=""

while [[ $# -gt 0 ]]; do
    case "$1" in
        --cli-path)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --cli-path" >&2
                usage >&2
                exit 2
            fi
            cli_path="$2"
            shift 2
            ;;
        --input-fol)
            if [[ $# -lt 2 ]]; then
                echo "Missing value for --input-fol" >&2
                usage >&2
                exit 2
            fi
            input_fol="$2"
            shift 2
            ;;
        -h|--help)
            usage
            exit 0
            ;;
        *)
            echo "Unknown argument: $1" >&2
            usage >&2
            exit 2
            ;;
    esac
done

if [[ -z "$cli_path" ]]; then
    echo "--cli-path is required" >&2
    usage >&2
    exit 2
fi

script_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo_root="$(cd "$script_dir/.." && pwd)"

resolve_path() {
    local path="$1"
    if [[ "$path" = /* ]]; then
        printf '%s\n' "$path"
    else
        printf '%s\n' "$repo_root/$path"
    fi
}

cli="$(resolve_path "$cli_path")"
if [[ ! -x "$cli" ]]; then
    echo "CLI not found or not executable: $cli" >&2
    exit 1
fi

run_root="$repo_root/tmp/run-roundtrip-mac"
source_workspace="$run_root/source-workspace"
source_fol="$run_root/source.fol"
workspace="$run_root/workspace"
repacked="$run_root/repacked.fol"
verify="$run_root/verify"

rm -rf "$run_root"
mkdir -p "$workspace" "$verify"

if [[ -n "$input_fol" ]]; then
    input="$(resolve_path "$input_fol")"
    if [[ ! -f "$input" ]]; then
        echo "Input .fol not found: $input" >&2
        exit 1
    fi
else
    assets="$source_workspace/assets"
    mkdir -p "$assets/nested"

    printf 'synthetic fixture\n' > "$assets/alpha.txt"
    printf '\000\001\002\003\004\372\373' > "$assets/nested/odd.bin"
    cat > "$source_workspace/manifest.txt" <<'EOF'
# FOL Manifest
# Format: Index|Key|GamePath
0|305419896|alpha.txt
1|2271560481|nested\odd.bin
EOF

    "$cli" pack "$source_workspace" "$source_fol"
    input="$source_fol"
fi

"$cli" unpack "$input" "$workspace"

target="$(find "$workspace/assets" -type f | sort | head -n 1)"
if [[ -z "$target" ]]; then
    echo "No extracted files found under $workspace/assets" >&2
    exit 1
fi

printf '\nROUNDTRIP_MARKER=shared-core\n' >> "$target"

"$cli" pack "$workspace" "$repacked"
"$cli" unpack "$repacked" "$verify"

relative="${target#"$workspace/assets/"}"
verified_path="$verify/assets/$relative"

if [[ ! -f "$verified_path" ]]; then
    echo "Verified file missing: $verified_path" >&2
    exit 1
fi

if ! cmp -s "$target" "$verified_path"; then
    echo "Content mismatch for $relative" >&2
    exit 1
fi

echo "Round-trip verification passed for $relative"
