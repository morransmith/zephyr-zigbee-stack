# SPDX-License-Identifier: Apache-2.0

# Builds the OTA file of the MCUboot image `image` with scripts/zigbee_ota.py and includes it in
# the application as `inc_file`. The OTA file goes to the binary directory under the name of
# `inc_file` without .inc. With the optional arguments `index` and `index_inc`, a copy of the
# zigpy_local index file `index` in the binary directory gets the entry of the OTA file and is
# included as `index_inc`.
function(zzb_ota_file image manufacturer image_type file_version inc_file)
  get_filename_component(ota_file ${inc_file} NAME_WLE)
  set(ota_file ${CMAKE_CURRENT_BINARY_DIR}/${ota_file})
  set(script ${ZEPHYR_ZZB_MODULE_DIR}/scripts/zigbee_ota.py)
  set(command ${PYTHON_EXECUTABLE} ${script} --manufacturer ${manufacturer}
    --image-type ${image_type} --file-version ${file_version} ${image} ${ota_file})
  if(ARGC LESS 7)
    add_custom_command(OUTPUT ${ota_file} COMMAND ${command} DEPENDS ${script} ${image})
  else()
    get_filename_component(index ${ARGV6} NAME_WLE)
    set(index ${CMAKE_CURRENT_BINARY_DIR}/${index})
    add_custom_command(
      OUTPUT ${ota_file} ${index}
      COMMAND ${CMAKE_COMMAND} -E copy ${ARGV5} ${index}
      COMMAND ${command} --index ${index}
      DEPENDS ${script} ${image} ${ARGV5}
    )
    generate_inc_file_for_target(app ${index} ${ARGV6})
  endif()
  generate_inc_file_for_target(app ${ota_file} ${inc_file})
endfunction()
