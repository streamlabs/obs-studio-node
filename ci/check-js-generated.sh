#!/bin/bash
# Verify that the committed tsc outputs under js/ match what regenerating
# from js/module.ts would produce. CI runs `yarn build:javascript` first,
# so any change under js/ at this point is a stale generated file.
dirty=$(git status --porcelain -- js/)

set +x
if [[ $dirty ]]; then
	echo "================================================="
	echo "Generated JS files are stale. Run locally:"
	echo "    yarn build:javascript"
	echo "and commit the regenerated files."
	echo ""
	echo "Stale files:"
	echo "$dirty"
	echo "================================================="
	exit 1
fi
