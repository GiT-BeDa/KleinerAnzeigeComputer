#!/bin/sh
# Image pages with nothing but curl (firmware >= 0.8.10): up to 5 GIF/JPG, shown full screen in the rotation.
#   KAC_HOST=192.168.1.50 KAC_KEY=yourkey sh image.sh photo.jpg 2      # file, slot 1-5
# The file must already fit: up to 240x240 px, baseline JPG or GIF, max. 1 MB.
# tools/kac_medium.py converts any picture (and sends it) – use that if unsure.
curl -s -H "X-Token: ${KAC_KEY:?set KAC_KEY to the device key}" -F "medium=@${1:?file}" \
  "http://${KAC_HOST:-192.168.1.50}/api/medium?nr=${2:-1}"
echo
# remove slot 2:  curl -H "X-Token: $KAC_KEY" -X DELETE "http://$KAC_HOST/api/medium?nr=2"
