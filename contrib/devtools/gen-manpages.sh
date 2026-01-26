#!/bin/bash

TOPDIR=${TOPDIR:-$(git rev-parse --show-toplevel)}
SRCDIR=${SRCDIR:-$TOPDIR/src}
MANDIR=${MANDIR:-$TOPDIR/doc/man}

HYPERCOIND=${HYPERCOIND:-$SRCDIR/hypercoind}
HYPERCOINCLI=${HYPERCOINCLI:-$SRCDIR/hypercoin-cli}
HYPERCOINTX=${HYPERCOINTX:-$SRCDIR/hypercoin-tx}
HYPERCOINQT=${HYPERCOINQT:-$SRCDIR/qt/hypercoin-qt}

[ ! -x $HYPERCOIND ] && echo "$HYPERCOIND not found or not executable." && exit 1

# The autodetected version git tag can screw up manpage output a little bit
HRCVER=($($HYPERCOINCLI --version | head -n1 | awk -F'[ -]' '{ print $6, $7 }'))

# Create a footer file with copyright content.
# This gets autodetected fine for hypercoind if --version-string is not set,
# but has different outcomes for hypercoin-qt and hypercoin-cli.
echo "[COPYRIGHT]" > footer.h2m
$HYPERCOIND --version | sed -n '1!p' >> footer.h2m

for cmd in $HYPERCOIND $HYPERCOINCLI $HYPERCOINTX $HYPERCOINQT; do
  cmdname="${cmd##*/}"
  help2man -N --version-string=${HRCVER[0]} --include=footer.h2m -o ${MANDIR}/${cmdname}.1 ${cmd}
  sed -i "s/\\\-${HRCVER[1]}//g" ${MANDIR}/${cmdname}.1
done

rm -f footer.h2m
