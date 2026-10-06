#!/bin/bash
set -euo pipefail
task_root="$(cd "$(dirname "$0")/.." && pwd)"
if [ -z "${DEVELOPER_DIR:-}" ] && [ -d /Applications/Xcode.app/Contents/Developer ]; then
    export DEVELOPER_DIR=/Applications/Xcode.app/Contents/Developer
fi
mkdir -p "$task_root/build/tests" "$task_root/mac-host/.build/module-cache" "$task_root/mac-host/.build/spm-cache"
"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/latest_picture_tests.cpp" -o "$task_root/build/tests/latest-picture-tests"
"$task_root/build/tests/latest-picture-tests"
"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/video_inbox_tests.cpp" -o "$task_root/build/tests/video-inbox-tests"
"$task_root/build/tests/video-inbox-tests"
"${CXX:-c++}" -std=c++17 -pthread -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/control_outbox_tests.cpp" -o "$task_root/build/tests/control-outbox-tests"
"$task_root/build/tests/control-outbox-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/recovery_tests.cpp" -o "$task_root/build/tests/recovery-tests"
"$task_root/build/tests/recovery-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/blob_tests.cpp" -o "$task_root/build/tests/blob-tests"
"$task_root/build/tests/blob-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/protocol_tests.cpp" -o "$task_root/build/tests/protocol-tests"
"$task_root/build/tests/protocol-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/quality_tests.cpp" -o "$task_root/build/tests/quality-tests"
"$task_root/build/tests/quality-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/windows-client/src" "$task_root/tests/setup_layout_tests.cpp" -o "$task_root/build/tests/setup-layout-tests"
"$task_root/build/tests/setup-layout-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/windows-client/src" "$task_root/tests/shortcut_tests.cpp" -o "$task_root/build/tests/shortcut-tests"
"$task_root/build/tests/shortcut-tests"
"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/presentation_tests.cpp" -o "$task_root/build/tests/presentation-tests"
"$task_root/build/tests/presentation-tests"

"${CXX:-c++}" -std=c++17 -Wall -Wextra -Werror -I "$task_root/shared" "$task_root/tests/clipboard_tests.cpp" -o "$task_root/build/tests/clipboard-tests"
"$task_root/build/tests/clipboard-tests"
export CLANG_MODULE_CACHE_PATH="$task_root/mac-host/.build/module-cache"
export SWIFTPM_MODULECACHE_OVERRIDE="$CLANG_MODULE_CACHE_PATH"
cd "$task_root/mac-host"
swift test --cache-path "$task_root/mac-host/.build/spm-cache" "$@"
