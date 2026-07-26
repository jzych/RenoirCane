idf_component_register(SRCS "audio.c" "impact_pcm.S" INCLUDE_DIRS "include")
target_compile_definitions(${COMPONENT_LIB} PRIVATE PCM_ASSET_PATH="${CMAKE_CURRENT_LIST_DIR}/impact.pcm")
