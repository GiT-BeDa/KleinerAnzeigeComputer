#!/bin/sh
# Hello World for KAC with nothing but curl: sends one page to the display.
#   KAC_HOST=192.168.1.50 KAC_KEY=yourkey sh hello_world.sh
# Answer {"ok":true,"seiten":1,"max_seiten":27} = it works (room for about 27 pages). 403 = wrong key.
curl -s -X POST "http://${KAC_HOST:-192.168.1.50}/api/seiten" \
  -H "X-Token: ${KAC_KEY:?set KAC_KEY to the device key}" -H "Content-Type: application/json" \
  -d '{"seiten":[{"t":"Hello","z":[
        {"y":"h","l":"Hello World","v":"KAC"},
        {"y":"k","l":"Sent at","v":"'"$(date +%H:%M:%S)"'"},
        {"y":"b","l":"Demo","p":42,"v":"42 %"}]}]}'
echo
