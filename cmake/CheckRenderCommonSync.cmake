# Fails when backends/renderer/RenderCommon.h drifts from the pinned Renderer
# header. The RenderCommon.h body must stay byte-identical (modulo line
# endings) to thirdparty/Renderer/src/gl_common.h between the first "define
# OFFSET" line and the trailing "#endif". Run via:
#   cmake -DABCUI_REPO_DIR=<repo root> -P cmake/CheckRenderCommonSync.cmake
# Wired into the build as the check-rendercommon-sync target.

if(NOT DEFINED ABCUI_REPO_DIR)
    message(FATAL_ERROR "CheckRenderCommonSync: ABCUI_REPO_DIR not set")
endif()

set(_synced  "${ABCUI_REPO_DIR}/backends/renderer/RenderCommon.h")
set(_upstream "${ABCUI_REPO_DIR}/thirdparty/Renderer/src/gl_common.h")

foreach(_f IN ITEMS "${_synced}" "${_upstream}")
    if(NOT EXISTS "${_f}")
        message(FATAL_ERROR "CheckRenderCommonSync: missing ${_f}")
    endif()
endforeach()

# Extract the constant/type body: from the first "define OFFSET" up to (but not
# including) the final "#endif" line of the file. CR characters are dropped so
# the check is line-ending agnostic.
function(abc_extract_body file_path out_var)
    file(STRINGS "${file_path}" _lines)
    set(_body "")
    set(_in_body FALSE)
    foreach(_line IN LISTS _lines)
        if(NOT _in_body)
            if(_line MATCHES "define OFFSET")
                set(_in_body TRUE)
            endif()
        endif()
        if(_in_body)
            if(_line MATCHES "^[ \t]*#endif")
                break()
            endif()
            string(REPLACE "\r" "" _line "${_line}")
            string(APPEND _body "${_line}\n")
        endif()
    endforeach()
    if(NOT _in_body)
        message(FATAL_ERROR "CheckRenderCommonSync: no '#define OFFSET' marker in ${file_path}")
    endif()
    set(${out_var} "${_body}" PARENT_SCOPE)
endfunction()

abc_extract_body("${_synced}"   _synced_body)
abc_extract_body("${_upstream}" _upstream_body)

if(NOT _synced_body STREQUAL _upstream_body)
    message(FATAL_ERROR
        "RenderCommon.h is out of sync with thirdparty/Renderer/src/gl_common.h.\n"
        "The body of ${_synced}\n"
        "must match ${_upstream} between '#define OFFSET' and '#endif'.\n"
        "Re-sync it (see the header comment in RenderCommon.h) instead of editing constants by hand.")
endif()

message(STATUS "CheckRenderCommonSync: OK")
