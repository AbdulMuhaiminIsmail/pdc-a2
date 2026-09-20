#!/usr/bin/env bash
# Builds the submission archive.
#
# The archive is produced with `git archive` from the tagged commit rather than
# by zipping the working tree. That guarantees it contains exactly the tracked
# files and nothing else -- no 800 MB data.dat, no venv, no object files, no
# stray binaries -- without relying on remembering to delete them.
#
#   ./package.sh            build CS3006_A2_23L-0775.zip and verify it
#
# data.dat is deliberately left in place on disk. It is gitignored, so it can
# never reach the archive, and deleting an 800 MB dataset that takes real
# effort to obtain is not something a packaging script should do.

set -euo pipefail

ROLL="23L-0775"
ZIP="CS3006_A2_${ROLL}.zip"
TAG="submission"
HERE="$(cd "$(dirname "$0")" && pwd)"
cd "$HERE"

echo "== 1. rebuild the write-up =="
pdflatex -interaction=nonstopmode writeup.tex >/dev/null
pdflatex -interaction=nonstopmode writeup.tex >/dev/null
echo "   writeup.pdf: $(pdfinfo writeup.pdf | awk '/Pages/{print $2}') pages"

echo "== 2. clean build output =="
for d in prog1_mandelbrot_threads prog2_vecintrin prog3_mandelbrot_ispc \
         prog4_sqrt prog5_saxpy prog6_kmeans; do
    [ -d "$d" ] || continue
    ( cd "$d" && make clean >/dev/null 2>&1 || true )
done
rm -rf prog*/__pycache__ prog*/objs
# prog6's clean target deletes *.png, which includes the two figures the
# write-up embeds. They are tracked, so restore them.
git checkout -- . 2>/dev/null || true
echo "   cleaned; tracked files restored"

echo "== 3. commit and tag =="
git add -A
if ! git diff --cached --quiet; then
    git commit -q -m "Final submission: write-up, code and measurement data for all six programs"
else
    echo "   nothing to commit"
fi

git log --oneline --stat > gitlog.txt
git add gitlog.txt
git diff --cached --quiet || git commit -q -m "Add commit history for submission"

git tag -f -a "$TAG" -m "CS3006 Assignment 2 submission -- roll number $ROLL" >/dev/null
echo "   tagged $TAG at $(git rev-parse --short HEAD)"

echo "== 4. build the archive =="
rm -f "$ZIP"
git archive --format=zip --prefix="cs3006-a2/" -o "$ZIP" "$TAG"
echo "   $ZIP: $(du -h "$ZIP" | cut -f1)"

echo "== 5. verify =="
TMP="$(mktemp -d)"
unzip -q "$ZIP" -d "$TMP"
echo "   files: $(find "$TMP" -type f | wc -l)"
echo "   writeup.pdf present: $([ -f "$TMP/cs3006-a2/writeup.pdf" ] && echo yes || echo NO)"
echo "   data.dat absent    : $([ ! -f "$TMP/cs3006-a2/prog6_kmeans/data.dat" ] && echo yes || echo NO)"
echo "   venv absent        : $([ ! -d "$TMP/cs3006-a2/prog6_kmeans/venv" ] && echo yes || echo NO)"
BIG=$(find "$TMP" -type f -size +5M | head -3)
[ -z "$BIG" ] && echo "   no file over 5 MB" || { echo "   LARGE FILES:"; echo "$BIG"; }
rm -rf "$TMP"

echo
echo "Done: $ZIP"
