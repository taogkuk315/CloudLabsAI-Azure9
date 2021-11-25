#!/bin/bash
# Copyright 2015 greenbytes GmbH (https://www.greenbytes.de)
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
# http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.
#

source test_common.sh

################################################################################
# check content of resources via different methods
################################################################################
curl_check_doc index.html "default"
curl_check_doc index.html "http/1.1" --http1.1
curl_check_doc index.html "http2"    --http2

nghttp_check_doc index.html "default"
nghttp_check_doc 003.html   "detault"


################################################################################
# check retrieving multiple resources from inside a page
################################################################################
nghttp_check_assets 001.html "with assets" <<EOF
/001.html 251 200
EOF

nghttp_check_assets 002.jpg "with assets" <<EOF
/002.jpg 88K 200
EOF

nghttp_check_assets 003.html "with assets" <<EOF
/003.html 316 200
/003/003_img.jpg 88K 200
EOF

nghttp_check_assets 004.html "with assets" <<EOF
/004.html 10K 200
/004/gophertiles.jpg 742 200
/004/gophertiles_002.jpg 945 200
/004/gophertiles_003.jpg 697 200
/004/gophertiles_004.jpg 725 200
/004/gophertiles_005.jpg 837 200
/004/gophertiles_006.jpg 770 200
/004/gophertiles_007.jpg 747 200
/004/gophertiles_008.jpg 694 200
/004/gophertiles_009.jpg 704 200
/004/gophertiles_010.jpg 994 200
/004/gophertiles_011.jpg 979 200
/004/gophertiles_012.jpg 895 200
/004/gophertiles_013.jpg 958 200
/004/gophertiles_014.jpg 894 200
/004/gophertiles_015.jpg 702 200
/004/gophertiles_016.jpg 703 200
/004/gophertiles_017.jpg 707 200
/004/gophertiles_018.jpg 701 200
/004/gophertiles_019.jpg 1013 200
/004/gophertiles_020.jpg 737 200
/004/gophertiles_021.jpg 801 200
/004/gophertiles_022.jpg 702 200
/004/gophertiles_023.jpg 905 200
/004/gophertiles_024.jpg 980 200
/004/gophertiles_025.jpg 708 200
/004/gophertiles_026.jpg 694 200
/004/gophertiles_027.jpg 697 200
/004/gophertiles_028.jpg 795 200
/004/gophertiles_029.jpg 978 200
/004/gophertiles_030.jpg 707 200
/004/gophertiles_031.jpg 1K 200
/004/gophertiles_032.jpg 688 200
/004/gophertiles_033.jpg 701 200
/004/gophertiles_034.jpg 898 200
/004/gophertiles_035.jpg 986 200
/004/gophertiles_036.jpg 770 200
/004/gophertiles_037.jpg 959 200
/004/gophertiles_038.jpg 936 200
/004/gophertiles_039.jpg 700 200
/004/gophertiles_040.jpg 784 200
/004/gophertiles_041.jpg 758 200
/004/gophertiles_042.jpg 796 200
/004/gophertiles_043.jpg 813 200
/004/gophertiles_044.jpg 924 200
/004/gophertiles_045.jpg 978 200
/004/gophertiles_046.jpg 752 200
/004/gophertiles_047.jpg 751 200
/004/gophertiles_048.jpg 737 200
/004/gophertiles_049.jpg 992 200
/004/gophertiles_050.jpg 688 200
/004/gophertiles_051.jpg 697 200
/004/gophertiles_052.jpg 699 200
/004/gophertiles_053.jpg 1K 200
/004/gophertiles_054.jpg 694 200
/004/gophertiles_055.jpg 767 200
/004/gophertiles_056.jpg 952 200
/004/gophertiles_057.jpg 788 200
/004/gophertiles_058.jpg 759 200
/004/gophertiles_059.jpg 700 200
/004/gophertiles_060.jpg 985 200
/004/gophertiles_061.jpg 915 200
/004/gophertiles_062.jpg 681 200
/004/gophertiles_063.jpg 707 200
/004/gophertiles_064.jpg 693 200
/004/gophertiles_065.jpg 861 200
/004/gophertiles_066.jpg 991 200
/004/gophertiles_067.jpg 1K 200
/004/gophertiles_068.jpg 697 200
/004/gophertiles_069.jpg 1K 200
/004/gophertiles_070.jpg 1K 200
/004/gophertiles_071.jpg 784 200
/004/gophertiles_072.jpg 698 200
/004/gophertiles_073.jpg 1004 200
/004/gophertiles_074.jpg 969 200
/004/gophertiles_075.jpg 915 200
/004/gophertiles_076.jpg 784 200
/004/gophertiles_077.jpg 697 200
/004/gophertiles_078.jpg 692 200
/004/gophertiles_079.jpg 702 200
/004/gophertiles_080.jpg 725 200
/004/gophertiles_081.jpg 877 200
/004/gophertiles_082.jpg 743 200
/004/gophertiles_083.jpg 785 200
/004/gophertiles_084.jpg 690 200
/004/gophertiles_085.jpg 724 200
/004/gophertiles_086.jpg 1K 200
/004/gophertiles_087.jpg 883 200
/004/gophertiles_088.jpg 702 200
/004/gophertiles_089.jpg 693 200
/004/gophertiles_090.jpg 947 200
/004/gophertiles_091.jpg 959 200
/004/gophertiles_092.jpg 736 200
/004/gophertiles_093.jpg 806 200
/004/gophertiles_094.jpg 820 200
/004/gophertiles_095.jpg 918 200
/004/gophertiles_096.jpg 689 200
/004/gophertiles_097.jpg 796 200
/004/gophertiles_098.jpg 686 200
/004/gophertiles_099.jpg 698 200
/004/gophertiles_100.jpg 686 200
/004/gophertiles_101.jpg 686 200
/004/gophertiles_102.jpg 682 200
/004/gophertiles_103.jpg 703 200
/004/gophertiles_104.jpg 698 200
/004/gophertiles_105.jpg 702 200
/004/gophertiles_106.jpg 989 200
/004/gophertiles_107.jpg 720 200
/004/gophertiles_108.jpg 834 200
/004/gophertiles_109.jpg 756 200
/004/gophertiles_110.jpg 703 200
/004/gophertiles_111.jpg 815 200
/004/gophertiles_112.jpg 780 200
/004/gophertiles_113.jpg 992 200
/004/gophertiles_114.jpg 862 200
/004/gophertiles_115.jpg 1K 200
/004/gophertiles_116.jpg 756 200
/004/gophertiles_117.jpg 1012 200
/004/gophertiles_118.jpg 905 200
/004/gophertiles_119.jpg 808 200
/004/gophertiles_120.jpg 814 200
/004/gophertiles_121.jpg 832 200
/004/gophertiles_122.jpg 704 200
/004/gophertiles_123.jpg 741 200
/004/gophertiles_124.jpg 694 200
/004/gophertiles_125.jpg 950 200
/004/gophertiles_126.jpg 770 200
/004/gophertiles_127.jpg 749 200
/004/gophertiles_128.jpg 942 200
/004/gophertiles_129.jpg 997 200
/004/gophertiles_130.jpg 708 200
/004/gophertiles_131.jpg 821 200
/004/gophertiles_132.jpg 849 200
/004/gophertiles_133.jpg 715 200
/004/gophertiles_134.jpg 794 200
/004/gophertiles_135.jpg 869 200
/004/gophertiles_136.jpg 1K 200
/004/gophertiles_137.jpg 757 200
/004/gophertiles_138.jpg 991 200
/004/gophertiles_139.jpg 704 200
/004/gophertiles_140.jpg 707 200
/004/gophertiles_141.jpg 959 200
/004/gophertiles_142.jpg 691 200
/004/gophertiles_143.jpg 921 200
/004/gophertiles_144.jpg 932 200
/004/gophertiles_145.jpg 696 200
/004/gophertiles_146.jpg 711 200
/004/gophertiles_147.jpg 817 200
/004/gophertiles_148.jpg 966 200
/004/gophertiles_149.jpg 1002 200
/004/gophertiles_150.jpg 900 200
/004/gophertiles_151.jpg 724 200
/004/gophertiles_152.jpg 1K 200
/004/gophertiles_153.jpg 702 200
/004/gophertiles_154.jpg 971 200
/004/gophertiles_155.jpg 708 200
/004/gophertiles_156.jpg 699 200
/004/gophertiles_157.jpg 834 200
/004/gophertiles_158.jpg 702 200
/004/gophertiles_159.jpg 880 200
/004/gophertiles_160.jpg 701 200
/004/gophertiles_161.jpg 688 200
/004/gophertiles_162.jpg 853 200
/004/gophertiles_163.jpg 690 200
/004/gophertiles_164.jpg 759 200
/004/gophertiles_165.jpg 831 200
/004/gophertiles_166.jpg 732 200
/004/gophertiles_167.jpg 955 200
/004/gophertiles_168.jpg 1K 200
/004/gophertiles_169.jpg 969 200
/004/gophertiles_170.jpg 701 200
/004/gophertiles_171.jpg 755 200
/004/gophertiles_172.jpg 924 200
/004/gophertiles_173.jpg 958 200
/004/gophertiles_174.jpg 998 200
/004/gophertiles_175.jpg 702 200
/004/gophertiles_176.jpg 760 200
/004/gophertiles_177.jpg 732 200
/004/gophertiles_178.jpg 929 200
/004/gophertiles_179.jpg 712 200
/004/gophertiles_180.jpg 1013 200
EOF

nghttp_check_assets 005.txt "with assets" <<EOF
/005.txt 9M 200
EOF

nghttp_check_assets 006.html "with assets" <<EOF
/006.html 543 200
/006/006.css 216 200
/006/006.js 839 200
EOF

# form data test, TODO
nghttp_check_assets 007.html "with assets" <<EOF
/007.html 809 200
EOF

# file upload test, TODO
nghttp_check_assets 008.html "with assets" <<EOF
/008.html 618 200
EOF

#nghttp_check_assets 009.php "with assets" <<EOF
#EOF
#

################################################################################
# check some redir handling
################################################################################
curl_check_doc xxx-1.0.2a.tar.gz  "http2"  --http2
curl_check_redir latest.tar.gz  xxx-1.0.2a.tar.gz  "http2"  --http2

################################################################################
# check cgi generated content
################################################################################
curl_check_content hello.py "default" <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>
EOF

curl_check_content hello.py "http/1.1" --http1.1 <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>
EOF

curl_check_content hello.py "http2"    --http2 <<EOF
<html>
<body>
<h2>Hello World!</h2>
</body>
</html>
EOF


curl_check_content upload.py "http/1.1" --http1.1 <<EOF
    <html><body>
    <p>        Upload File<form method="POST" enctype="multipart/form-data">
        <input type="file" name="file">
        <button type="submit">Upload</button></form>
        </p>
    </body></html>
EOF

curl_check_content upload.py "http2"    --http2 <<EOF
    <html><body>
    <p>        Upload File<form method="POST" enctype="multipart/form-data">
        <input type="file" name="file">
        <button type="submit">Upload</button></form>
        </p>
    </body></html>
EOF



