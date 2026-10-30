#!/bin/bash
# flash-update.sh - data-conscious updater for the OsmiumOS floppy.
#
# On-disk layout (see build.sh / src/boot.asm):
#   sectors 0..(R-1)   reserved region: boot sector (0) + the kernel  = the OS
#   sector R.. end     FAT12: FATs, root directory + your files       = your data
# where R is the `reserved sectors` field of the BPB. The default mode
# rewrites ONLY the reserved region, so your files survive an OS update.
#
# --full does the old whole-disk write (everything erased) and insists on
# an explicit "ERASE" confirmation.
set -euo pipefail

IMAGE="${OSM_IMAGE:-build/os.img}"
KERNEL="${OSM_KERNEL:-build/kernel.bin}"
MODE=inplace
DEVICE=
BACKUP=1
BACKUP_PATH=
DRY=0
# OSM_DEVFILE=1 allows a plain file as the target instead of a block device:
# handy for snapshots/CI and lets the whole flow be tested without root.
DEVFILE=0

die() { echo "ERR: $*" >&2; exit 1; }

usage() {
    cat <<EOF
Usage: $(basename "$0") [OPTIONS] [DEVICE]

Update the OsmiumOS floppy without losing your files.

Modes (default: in-place):
  (none)    update the boot sector + kernel only; your files are preserved
  --full    rewrite the entire disk (ERASES all files - confirmed interactively)
  --dry-run print what would happen and exit without writing

Options:
  --backup <path>   store the pre-update region here (default: auto-named file)
  --no-backup       skip the automatic backup
  --image <path>    image to flash from (default: build/os.img)
  -h, --help        this text

DEVICE defaults to the single 1.4 MB removable disk found; otherwise you
are prompted. With OSM_DEVFILE=1 the target may instead be a plain file
(snapshot/CI testing). Examples:
  sudo $0 /dev/sdb
  sudo $0 --full /dev/sdb
  OSM_DEVFILE=1 $0 /tmp/floppy.img
EOF
}

while [ $# -gt 0 ]; do
    case "$1" in
        --full)      MODE=full;  shift ;;
        --dry-run)   DRY=1;      shift ;;
        --backup)    BACKUP=1; BACKUP_PATH="$2"; shift 2 ;;
        --no-backup) BACKUP=0;   shift ;;
        --image)     IMAGE="$2"; shift 2 ;;
        -h|--help)   usage; exit 0 ;;
        -*)          die "unknown option: $1 (see --help)" ;;
        *)           DEVICE="$1"; shift ;;
    esac
done

# Fail fast on a bad explicit target before demanding root.
if [ -n "$DEVICE" ] && [ ! -b "$DEVICE" ] && { [ "$OSM_DEVFILE" != 1 ] || [ ! -f "$DEVICE" ]; }; then
    die "$DEVICE is not a block device (or, with OSM_DEVFILE=1, a file)"
fi

[ -f "$IMAGE" ] || die "image not found: $IMAGE  (run ./build.sh first)"
IMG_SIZE=$(stat -c%s "$IMAGE")

# ---- locate the device -----------------------------------------------------
if [ -z "$DEVICE" ]; then
    mapfile -t cand < <(lsblk -dnb -o NAME,SIZE,RM,TYPE 2>/dev/null |
        awk '$4=="disk" && $3==1 && $2>=1400000 && $2<=1600000 {print "/dev/"$1}')
    if [ "${#cand[@]}" -eq 1 ]; then
        DEVICE="${cand[0]}"
        echo "[*] using detected device: $DEVICE"
    elif [ "${#cand[@]}" -gt 1 ]; then
        printf 'Multiple 1.4 MB removable disks found:\n   %s\n' "${cand[@]}"
        die "no DEVICE given; pass one explicitly"
    else
        lsblk -d -o NAME,SIZE,TYPE,RM,MODEL 2>/dev/null || true
        read -r -p "Enter target device (e.g. /dev/sdb): " DEVICE
    fi
fi
if [ -b "$DEVICE" ]; then
    :
elif [ "$OSM_DEVFILE" = 1 ] && [ -f "$DEVICE" ]; then
    DEVFILE=1
    echo "[*] target is a regular file (OSM_DEVFILE=1) - snapshot/CI mode"
else
    die "$DEVICE is not a block device (or, with OSM_DEVFILE=1, a file)"
fi

# Block devices need root unless already user-writable (e.g. a udisksctl
# loop device); re-exec with sudo transparently otherwise.
if [ "$(id -u)" -ne 0 ] && [ ! -w "$DEVICE" ]; then
    echo "[*] needs root; re-running via sudo..."
    exec sudo "$0" "$@"
fi

# ---- read BPB geometry (little-endian) from image and device ---------------
bpb() { # $1 = file, $2 = offset, $3 = size in bytes (1|2|4)
    local f="$1" off="$2" sz="$3"
    case "$sz" in
        1) od -An -tu1 -j"$off" -N1 "$f" ;;
        2) od -An -tu2 -j"$off" -N2 "$f" ;;
        4) od -An -tu4 -j"$off" -N4 "$f" ;;
    esac | tr -d '[:space:]'
}

TMP=$(mktemp -d)
trap 'rm -rf "$TMP"' EXIT

dd if="$DEVICE" of="$TMP/dev.boot" bs=512 count=1 status=none 2>/dev/null \
    || die "cannot read $DEVICE"
dd if="$IMAGE"  of="$TMP/img.boot" bs=512 count=1 status=none

img_bps=$(bpb "$TMP/img.boot" 11 2);  dev_bps=$(bpb "$TMP/dev.boot" 11 2)
img_spc=$(bpb "$TMP/img.boot" 13 1);  dev_spc=$(bpb "$TMP/dev.boot" 13 1)
img_res=$(bpb "$TMP/img.boot" 14 2);  dev_res=$(bpb "$TMP/dev.boot" 14 2)
img_fatn=$(bpb "$TMP/img.boot" 16 1); dev_fatn=$(bpb "$TMP/dev.boot" 16 1)
img_root=$(bpb "$TMP/img.boot" 17 2); dev_root=$(bpb "$TMP/dev.boot" 17 2)
img_fatsz=$(bpb "$TMP/img.boot" 22 2); dev_fatsz=$(bpb "$TMP/dev.boot" 22 2)

[ -n "$img_res" ] && [ "$img_res" -gt 0 ] || die "image has no valid FAT12 BPB"

if [ "$MODE" = inplace ]; then
    [ -n "$dev_res" ] && [ "$dev_res" -gt 0 ] \
        || die "$DEVICE has no FAT12 BPB (blank/unformatted); nothing to preserve - use --full"
    if [ "$img_res" != "$dev_res" ]; then
        die "reserved sectors differ (image=$img_res device=$dev_res).
This disk was formatted by a different build. Use --full once to adopt the image's
reserved layout (${img_res} sectors); after that, in-place updates keep it fixed."
    fi
    for f in bps spc fatn root fatsz; do
        eval "a=\$img_$f; b=\$dev_$f"
        [ "$a" = "$b" ] || die "filesystem geometry mismatch ($f): image=$a device=$b.
In-place update would corrupt your files. Use --full if you accept erasing everything."
    done
    if [ -f "$KERNEL" ]; then
        ksz=$(stat -c%s "$KERNEL")
        maxk=$(( (img_res - 1) * 512 ))
        [ "$ksz" -le "$maxk" ] \
            || die "kernel is $ksz bytes but only $maxk fit in the reserved region"
        echo "[*] kernel fits: $ksz bytes <= $maxk"
    fi
    COUNT=$img_res
    ACTION="update reserved region (boot + kernel): $COUNT sectors = $((COUNT * 512)) bytes"
else
    if [ "$DEVFILE" = 1 ]; then
        dev_size=$(stat -c%s "$DEVICE")
    else
        dev_size=$(blockdev --getsize64 "$DEVICE" 2>/dev/null || echo 0)
    fi
    [ "$dev_size" -ge "$IMG_SIZE" ] || die "target ($DEVICE) is smaller than the image (${dev_size} < ${IMG_SIZE})"
    COUNT=$((IMG_SIZE / 512))
    ACTION="FULL write: $COUNT sectors = $IMG_SIZE bytes (ALL FILES ERASED)"
fi

echo "------------------------------------------------------------"
echo "[*] mode: $MODE"
echo "[*] image: $IMAGE  ($IMG_SIZE bytes)"
echo "[*] target: $DEVICE"
echo "[*] plan: $ACTION"
echo "------------------------------------------------------------"

if [ "$DRY" -eq 1 ]; then
    echo "[--dry-run--] nothing was written."
    exit 0
fi

if [ "$MODE" = full ]; then
    echo "[!] this will ERASE every file on $DEVICE"
    read -r -p "Type ERASE to confirm: " ans
    [ "$ans" = "ERASE" ] || die "aborted - no write performed"
fi

# ---- unmount anything using the device -------------------------------------
while read -r mp; do
    [ -n "$mp" ] && { echo "[*] unmounting $mp"; umount "$mp" 2>/dev/null || true; }
done < <(awk -v d="$DEVICE" \
    '$1==d || (substr($1,1,length(d))==d && $1!=d) {print $2}' /proc/mounts)

# ---- pre-update backup ------------------------------------------------------
STAMP=$(date +%Y%m%d-%H%M%S)
DEVNAME=${DEVICE##*/}
if [ "$BACKUP" -eq 1 ]; then
    if [ -z "$BACKUP_PATH" ]; then
        if [ "$MODE" = full ]; then
            BACKUP_PATH="osmium-full-${DEVNAME}-${STAMP}.img"
        else
            BACKUP_PATH="osmium-boot-${DEVNAME}-${STAMP}.bin"
        fi
    fi
    echo "[*] backing up current data -> $BACKUP_PATH"
    dd if="$DEVICE" of="$BACKUP_PATH" bs=512 count="$COUNT" status=none
    sync
fi

# ---- write ------------------------------------------------------------------
if [ "$MODE" = full ]; then
    echo "[*] writing entire image..."
    dd if="$IMAGE" of="$DEVICE" bs=512 status=progress
else
    echo "[*] writing OS region (leaving sectors $COUNT+ / your files untouched)..."
    dd if="$IMAGE" of="$DEVICE" bs=512 count="$COUNT" conv=notrunc status=progress
fi
sync

# ---- verify -----------------------------------------------------------------
dd if="$DEVICE" of="$TMP/verify.bin" bs=512 count="$COUNT" status=none
head -c $((COUNT * 512)) "$IMAGE" > "$TMP/expect.bin"
if cmp -s "$TMP/verify.bin" "$TMP/expect.bin"; then
    echo "[+] verified: $COUNT sectors on $DEVICE match the built image"
else
    echo "ERR: verification FAILED. Your previous data was backed up at: $BACKUP_PATH" >&2
    exit 1
fi

if [ "$MODE" = full ]; then
    echo "[+] $DEVICE fully rewritten."
else
    echo "[+] $DEVICE updated. Filesystem (sectors $COUNT+) and your files are untouched."
fi