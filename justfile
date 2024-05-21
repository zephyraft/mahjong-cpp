run:
    docker build . --tag mahjong_cpp

# curl 'http://localhost:8888/post.py' \
#  -H 'accept: application/json, text/plain, */*' \
#  -H 'accept-language: zh-CN,zh;q=0.9' \
#  -H 'content-type: application/x-www-form-urlencoded' \
#  -H 'cookie: _ga=GA1.1.406577871.1711331875; _ga_GDCFWZN645=GS1.1.1716193321.14.0.1716193322.0.0.0' \
#  -H 'dnt: 1' \
#  -H 'origin: https://pystyle.info' \
#  -H 'priority: u=1, i' \
#  -H 'referer: https://pystyle.info/apps/mahjong-nanikiru-simulator/' \
#  -H 'sec-ch-ua: "Chromium";v="124", "Google Chrome";v="124", "Not-A.Brand";v="99"' \
#  -H 'sec-ch-ua-mobile: ?0' \
#  -H 'sec-ch-ua-platform: "macOS"' \
#  -H 'sec-fetch-dest: empty' \
#  -H 'sec-fetch-mode: cors' \
#  -H 'sec-fetch-site: same-origin' \
#  -H 'user-agent: Mozilla/5.0 (Macintosh; Intel Mac OS X 10_15_7) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/124.0.0.0 Safari/537.36' \
#  --data-raw '{"version":"0.9.0","zikaze":27,"bakaze":27,"turn":3,"syanten_type":1,"dora_indicators":[27],"flag":127,"hand_tiles":[34,6,14,20,22,24,25,26,26,28,30,31,31,33],"melded_blocks":[],"counts":[4,4,4,4,3,4,3,4,4,4,4,4,4,4,3,4,4,4,4,4,3,4,3,4,3,3,2,3,3,4,3,2,4,3,0,1,1]}'
