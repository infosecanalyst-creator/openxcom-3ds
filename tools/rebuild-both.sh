#!/usr/bin/env bash
# Rebuild Micropolis and OpenXcom; the ONLY place .3dsx files live is SD-CARD/3ds/<game>/.
export DEVKITPRO=/opt/devkitpro DEVKITARM=/opt/devkitpro/devkitARM
export PATH=$DEVKITARM/bin:$DEVKITPRO/tools/bin:$PATH
P=/c/devkitPro/3ds-projects
mkdir -p $P/SD-CARD/3ds/micropolis $P/SD-CARD/3ds/openxcom

cd $P/micropolis-3ds && make -j"$(nproc)" > $P/micropolis-build.log 2>&1; echo "micropolis EXIT: $?" | tee -a $P/micropolis-build.log
[ -f micropolis.3dsx ] && mv -f micropolis.3dsx $P/SD-CARD/3ds/micropolis/

cd $P/openxcom-3ds/OpenXcom/src && make -f Makefile.3ds -j"$(nproc)" > $P/openxcom-3ds/openxcom-build.log 2>&1; echo "openxcom EXIT: $?" | tee -a $P/openxcom-3ds/openxcom-build.log
[ -f ../openxcom.3dsx ] && mv -f ../openxcom.3dsx $P/SD-CARD/3ds/openxcom/

echo; echo "=== SD-CARD contents (copy the 3ds folder to the card):"
find $P/SD-CARD -name "*.3dsx" -exec ls -la --time-style=+%H:%M {} \;
