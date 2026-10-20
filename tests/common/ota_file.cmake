# SPDX-License-Identifier: Apache-2.0

# Builds the OTA file of the MCUboot image `image` with scripts/zigbee_ota.py and includes it in
# the application as `inc_file`. The OTA file goes to the binary directory under the name of
# `inc_file` without .inc.
function(zzb_ota_file image manufacturer image_type file_version inc_file)
  get_filename_component(ota_file ${inc_file} NAME_WLE)
  set(ota_file ${CMAKE_CURRENT_BINARY_DIR}/${ota_file})
  set(script ${ZEPHYR_ZZB_MODULE_DIR}/scripts/zigbee_ota.py)
  add_custom_command(
    OUTPUT ${ota_file}
    COMMAND ${PYTHON_EXECUTABLE} ${script} --manufacturer ${manufacturer}
      --image-type ${image_type} --file-version ${file_version} ${image} ${ota_file}
    DEPENDS ${script} ${image}
  )
  generate_inc_file_for_target(app ${ota_file} ${inc_file})
endfunction()
