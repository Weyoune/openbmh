# SPDX-License-Identifier: GPL-3.0-only
# Reject accidental shell login banners in source and documentation.
file(GLOB_RECURSE files RELATIVE "${SOURCE_DIR}"
     "${SOURCE_DIR}/src/*" "${SOURCE_DIR}/tests/*" "${SOURCE_DIR}/docs/*"
     "${SOURCE_DIR}/CMakeLists.txt")
set(bad "")
foreach(f IN LISTS files)
    # This rule file contains the rejected strings deliberately.
    if(f MATCHES "repo_hygiene\.cmake$")
        continue()
    endif()
    file(STRINGS "${SOURCE_DIR}/${f}" hit REGEX "Debian LXC Container|community-scripts ORG" LIMIT_COUNT 1)
    if(hit)
        list(APPEND bad "${f}")
    endif()
endforeach()
if(bad)
    message(FATAL_ERROR "login banner found in: ${bad}")
endif()
