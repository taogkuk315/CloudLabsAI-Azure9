import pytest

from TestHttpdConf import HttpdConf


class TestStore:

    @pytest.fixture(autouse=True, scope='class')
    def _class_scope(self, env):
        HttpdConf(env).add_vhost_test1().install()
        assert env.apache_restart() == 0
        yield
        assert env.apache_stop() == 0

    # single page without any assets
    def test_006_01(self, env):
        url = env.mkurl("https", "test1", "/001.html")
        r = env.nghttp().assets(url,  options=["-Haccept-encoding: none"])
        assert 0 == r["rv"]
        assert 1 == len(r["assets"])
        assert r["assets"] == [
            {"status": 200, "size": "251", "path": "/001.html"}
        ]

    # single image without any assets
    def test_006_02(self, env):
        url = env.mkurl("https", "test1", "/002.jpg")
        r = env.nghttp().assets(url,  options=["-Haccept-encoding: none"])
        assert 0 == r["rv"]
        assert 1 == len(r["assets"])
        assert r["assets"] == [
            {"status": 200, "size": "88K", "path": "/002.jpg"}
        ]
        
    # gophertiles, yea!
    def test_006_03(self, env):
        url = env.mkurl("https", "test1", "/004.html")
        r = env.nghttp().assets(url, options=["-Haccept-encoding: none"])
        assert 0 == r["rv"]
        assert 181 == len(r["assets"])
        assert r["assets"] == [
            {"status": 200, "size": "10K", "path": "/004.html"},
            {"status": 200, "size": "742", "path": "/004/gophertiles.jpg"},
            {"status": 200, "size": "945", "path": "/004/gophertiles_002.jpg"},
            {"status": 200, "size": "697", "path": "/004/gophertiles_003.jpg"},            
            {"status": 200, "size": "725", "path": "/004/gophertiles_004.jpg"},
            {"status": 200, "size": "837", "path": "/004/gophertiles_005.jpg"},
            {"status": 200, "size": "770", "path": "/004/gophertiles_006.jpg"},
            {"status": 200, "size": "747", "path": "/004/gophertiles_007.jpg"},
            {"status": 200, "size": "694", "path": "/004/gophertiles_008.jpg"},
            {"status": 200, "size": "704", "path": "/004/gophertiles_009.jpg"},
            {"status": 200, "size": "994", "path": "/004/gophertiles_010.jpg"},
            {"status": 200, "size": "979", "path": "/004/gophertiles_011.jpg"},
            {"status": 200, "size": "895", "path": "/004/gophertiles_012.jpg"},
            {"status": 200, "size": "958", "path": "/004/gophertiles_013.jpg"},
            {"status": 200, "size": "894", "path": "/004/gophertiles_014.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_015.jpg"},
            {"status": 200, "size": "703", "path": "/004/gophertiles_016.jpg"},
            {"status": 200, "size": "707", "path": "/004/gophertiles_017.jpg"},
            {"status": 200, "size": "701", "path": "/004/gophertiles_018.jpg"},
            {"status": 200, "size": "1013", "path": "/004/gophertiles_019.jpg"},
            {"status": 200, "size": "737", "path": "/004/gophertiles_020.jpg"},
            {"status": 200, "size": "801", "path": "/004/gophertiles_021.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_022.jpg"},
            {"status": 200, "size": "905", "path": "/004/gophertiles_023.jpg"},
            {"status": 200, "size": "980", "path": "/004/gophertiles_024.jpg"},
            {"status": 200, "size": "708", "path": "/004/gophertiles_025.jpg"},
            {"status": 200, "size": "694", "path": "/004/gophertiles_026.jpg"},
            {"status": 200, "size": "697", "path": "/004/gophertiles_027.jpg"},
            {"status": 200, "size": "795", "path": "/004/gophertiles_028.jpg"},
            {"status": 200, "size": "978", "path": "/004/gophertiles_029.jpg"},
            {"status": 200, "size": "707", "path": "/004/gophertiles_030.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_031.jpg"},
            {"status": 200, "size": "688", "path": "/004/gophertiles_032.jpg"},
            {"status": 200, "size": "701", "path": "/004/gophertiles_033.jpg"},
            {"status": 200, "size": "898", "path": "/004/gophertiles_034.jpg"},
            {"status": 200, "size": "986", "path": "/004/gophertiles_035.jpg"},
            {"status": 200, "size": "770", "path": "/004/gophertiles_036.jpg"},
            {"status": 200, "size": "959", "path": "/004/gophertiles_037.jpg"},
            {"status": 200, "size": "936", "path": "/004/gophertiles_038.jpg"},
            {"status": 200, "size": "700", "path": "/004/gophertiles_039.jpg"},
            {"status": 200, "size": "784", "path": "/004/gophertiles_040.jpg"},
            {"status": 200, "size": "758", "path": "/004/gophertiles_041.jpg"},
            {"status": 200, "size": "796", "path": "/004/gophertiles_042.jpg"},
            {"status": 200, "size": "813", "path": "/004/gophertiles_043.jpg"},
            {"status": 200, "size": "924", "path": "/004/gophertiles_044.jpg"},
            {"status": 200, "size": "978", "path": "/004/gophertiles_045.jpg"},
            {"status": 200, "size": "752", "path": "/004/gophertiles_046.jpg"},
            {"status": 200, "size": "751", "path": "/004/gophertiles_047.jpg"},
            {"status": 200, "size": "737", "path": "/004/gophertiles_048.jpg"},
            {"status": 200, "size": "992", "path": "/004/gophertiles_049.jpg"},
            {"status": 200, "size": "688", "path": "/004/gophertiles_050.jpg"},
            {"status": 200, "size": "697", "path": "/004/gophertiles_051.jpg"},
            {"status": 200, "size": "699", "path": "/004/gophertiles_052.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_053.jpg"},
            {"status": 200, "size": "694", "path": "/004/gophertiles_054.jpg"},
            {"status": 200, "size": "767", "path": "/004/gophertiles_055.jpg"},
            {"status": 200, "size": "952", "path": "/004/gophertiles_056.jpg"},
            {"status": 200, "size": "788", "path": "/004/gophertiles_057.jpg"},
            {"status": 200, "size": "759", "path": "/004/gophertiles_058.jpg"},
            {"status": 200, "size": "700", "path": "/004/gophertiles_059.jpg"},
            {"status": 200, "size": "985", "path": "/004/gophertiles_060.jpg"},
            {"status": 200, "size": "915", "path": "/004/gophertiles_061.jpg"},
            {"status": 200, "size": "681", "path": "/004/gophertiles_062.jpg"},
            {"status": 200, "size": "707", "path": "/004/gophertiles_063.jpg"},
            {"status": 200, "size": "693", "path": "/004/gophertiles_064.jpg"},
            {"status": 200, "size": "861", "path": "/004/gophertiles_065.jpg"},
            {"status": 200, "size": "991", "path": "/004/gophertiles_066.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_067.jpg"},
            {"status": 200, "size": "697", "path": "/004/gophertiles_068.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_069.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_070.jpg"},
            {"status": 200, "size": "784", "path": "/004/gophertiles_071.jpg"},
            {"status": 200, "size": "698", "path": "/004/gophertiles_072.jpg"},
            {"status": 200, "size": "1004", "path": "/004/gophertiles_073.jpg"},
            {"status": 200, "size": "969", "path": "/004/gophertiles_074.jpg"},
            {"status": 200, "size": "915", "path": "/004/gophertiles_075.jpg"},
            {"status": 200, "size": "784", "path": "/004/gophertiles_076.jpg"},
            {"status": 200, "size": "697", "path": "/004/gophertiles_077.jpg"},
            {"status": 200, "size": "692", "path": "/004/gophertiles_078.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_079.jpg"},
            {"status": 200, "size": "725", "path": "/004/gophertiles_080.jpg"},
            {"status": 200, "size": "877", "path": "/004/gophertiles_081.jpg"},
            {"status": 200, "size": "743", "path": "/004/gophertiles_082.jpg"},
            {"status": 200, "size": "785", "path": "/004/gophertiles_083.jpg"},
            {"status": 200, "size": "690", "path": "/004/gophertiles_084.jpg"},
            {"status": 200, "size": "724", "path": "/004/gophertiles_085.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_086.jpg"},
            {"status": 200, "size": "883", "path": "/004/gophertiles_087.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_088.jpg"},
            {"status": 200, "size": "693", "path": "/004/gophertiles_089.jpg"},
            {"status": 200, "size": "947", "path": "/004/gophertiles_090.jpg"},
            {"status": 200, "size": "959", "path": "/004/gophertiles_091.jpg"},
            {"status": 200, "size": "736", "path": "/004/gophertiles_092.jpg"},
            {"status": 200, "size": "806", "path": "/004/gophertiles_093.jpg"},
            {"status": 200, "size": "820", "path": "/004/gophertiles_094.jpg"},
            {"status": 200, "size": "918", "path": "/004/gophertiles_095.jpg"},
            {"status": 200, "size": "689", "path": "/004/gophertiles_096.jpg"},
            {"status": 200, "size": "796", "path": "/004/gophertiles_097.jpg"},
            {"status": 200, "size": "686", "path": "/004/gophertiles_098.jpg"},
            {"status": 200, "size": "698", "path": "/004/gophertiles_099.jpg"},
            {"status": 200, "size": "686", "path": "/004/gophertiles_100.jpg"},
            {"status": 200, "size": "686", "path": "/004/gophertiles_101.jpg"},
            {"status": 200, "size": "682", "path": "/004/gophertiles_102.jpg"},
            {"status": 200, "size": "703", "path": "/004/gophertiles_103.jpg"},
            {"status": 200, "size": "698", "path": "/004/gophertiles_104.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_105.jpg"},
            {"status": 200, "size": "989", "path": "/004/gophertiles_106.jpg"},
            {"status": 200, "size": "720", "path": "/004/gophertiles_107.jpg"},
            {"status": 200, "size": "834", "path": "/004/gophertiles_108.jpg"},
            {"status": 200, "size": "756", "path": "/004/gophertiles_109.jpg"},
            {"status": 200, "size": "703", "path": "/004/gophertiles_110.jpg"},
            {"status": 200, "size": "815", "path": "/004/gophertiles_111.jpg"},
            {"status": 200, "size": "780", "path": "/004/gophertiles_112.jpg"},
            {"status": 200, "size": "992", "path": "/004/gophertiles_113.jpg"},
            {"status": 200, "size": "862", "path": "/004/gophertiles_114.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_115.jpg"},
            {"status": 200, "size": "756", "path": "/004/gophertiles_116.jpg"},
            {"status": 200, "size": "1012", "path": "/004/gophertiles_117.jpg"},
            {"status": 200, "size": "905", "path": "/004/gophertiles_118.jpg"},
            {"status": 200, "size": "808", "path": "/004/gophertiles_119.jpg"},
            {"status": 200, "size": "814", "path": "/004/gophertiles_120.jpg"},
            {"status": 200, "size": "832", "path": "/004/gophertiles_121.jpg"},
            {"status": 200, "size": "704", "path": "/004/gophertiles_122.jpg"},
            {"status": 200, "size": "741", "path": "/004/gophertiles_123.jpg"},
            {"status": 200, "size": "694", "path": "/004/gophertiles_124.jpg"},
            {"status": 200, "size": "950", "path": "/004/gophertiles_125.jpg"},
            {"status": 200, "size": "770", "path": "/004/gophertiles_126.jpg"},
            {"status": 200, "size": "749", "path": "/004/gophertiles_127.jpg"},
            {"status": 200, "size": "942", "path": "/004/gophertiles_128.jpg"},
            {"status": 200, "size": "997", "path": "/004/gophertiles_129.jpg"},
            {"status": 200, "size": "708", "path": "/004/gophertiles_130.jpg"},
            {"status": 200, "size": "821", "path": "/004/gophertiles_131.jpg"},
            {"status": 200, "size": "849", "path": "/004/gophertiles_132.jpg"},
            {"status": 200, "size": "715", "path": "/004/gophertiles_133.jpg"},
            {"status": 200, "size": "794", "path": "/004/gophertiles_134.jpg"},
            {"status": 200, "size": "869", "path": "/004/gophertiles_135.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_136.jpg"},
            {"status": 200, "size": "757", "path": "/004/gophertiles_137.jpg"},
            {"status": 200, "size": "991", "path": "/004/gophertiles_138.jpg"},
            {"status": 200, "size": "704", "path": "/004/gophertiles_139.jpg"},
            {"status": 200, "size": "707", "path": "/004/gophertiles_140.jpg"},
            {"status": 200, "size": "959", "path": "/004/gophertiles_141.jpg"},
            {"status": 200, "size": "691", "path": "/004/gophertiles_142.jpg"},
            {"status": 200, "size": "921", "path": "/004/gophertiles_143.jpg"},
            {"status": 200, "size": "932", "path": "/004/gophertiles_144.jpg"},
            {"status": 200, "size": "696", "path": "/004/gophertiles_145.jpg"},
            {"status": 200, "size": "711", "path": "/004/gophertiles_146.jpg"},
            {"status": 200, "size": "817", "path": "/004/gophertiles_147.jpg"},
            {"status": 200, "size": "966", "path": "/004/gophertiles_148.jpg"},
            {"status": 200, "size": "1002", "path": "/004/gophertiles_149.jpg"},
            {"status": 200, "size": "900", "path": "/004/gophertiles_150.jpg"},
            {"status": 200, "size": "724", "path": "/004/gophertiles_151.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_152.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_153.jpg"},
            {"status": 200, "size": "971", "path": "/004/gophertiles_154.jpg"},
            {"status": 200, "size": "708", "path": "/004/gophertiles_155.jpg"},
            {"status": 200, "size": "699", "path": "/004/gophertiles_156.jpg"},
            {"status": 200, "size": "834", "path": "/004/gophertiles_157.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_158.jpg"},
            {"status": 200, "size": "880", "path": "/004/gophertiles_159.jpg"},
            {"status": 200, "size": "701", "path": "/004/gophertiles_160.jpg"},
            {"status": 200, "size": "688", "path": "/004/gophertiles_161.jpg"},
            {"status": 200, "size": "853", "path": "/004/gophertiles_162.jpg"},
            {"status": 200, "size": "690", "path": "/004/gophertiles_163.jpg"},
            {"status": 200, "size": "759", "path": "/004/gophertiles_164.jpg"},
            {"status": 200, "size": "831", "path": "/004/gophertiles_165.jpg"},
            {"status": 200, "size": "732", "path": "/004/gophertiles_166.jpg"},
            {"status": 200, "size": "955", "path": "/004/gophertiles_167.jpg"},
            {"status": 200, "size": "1K", "path": "/004/gophertiles_168.jpg"},
            {"status": 200, "size": "969", "path": "/004/gophertiles_169.jpg"},
            {"status": 200, "size": "701", "path": "/004/gophertiles_170.jpg"},
            {"status": 200, "size": "755", "path": "/004/gophertiles_171.jpg"},
            {"status": 200, "size": "924", "path": "/004/gophertiles_172.jpg"},
            {"status": 200, "size": "958", "path": "/004/gophertiles_173.jpg"},
            {"status": 200, "size": "998", "path": "/004/gophertiles_174.jpg"},
            {"status": 200, "size": "702", "path": "/004/gophertiles_175.jpg"},
            {"status": 200, "size": "760", "path": "/004/gophertiles_176.jpg"},
            {"status": 200, "size": "732", "path": "/004/gophertiles_177.jpg"},
            {"status": 200, "size": "929", "path": "/004/gophertiles_178.jpg"},
            {"status": 200, "size": "712", "path": "/004/gophertiles_179.jpg"},
            {"status": 200, "size": "1013", "path": "/004/gophertiles_180.jpg"}
        ]            
            
    # page with js and css
    def test_006_04(self, env):
        url = env.mkurl("https", "test1", "/006.html")
        r = env.nghttp().assets(url, options=["-Haccept-encoding: none"])
        assert 0 == r["rv"]
        assert 3 == len(r["assets"])
        assert r["assets"] == [
            {"status": 200, "size": "543", "path": "/006.html"},
            {"status": 200, "size": "216", "path": "/006/006.css"},
            {"status": 200, "size": "839", "path": "/006/006.js"}
        ]

    # page with image, try different window size
    def test_006_05(self, env):
        url = env.mkurl("https", "test1", "/003.html")
        r = env.nghttp().assets(url, options=["--window-bits=24", "-Haccept-encoding: none"])
        assert 0 == r["rv"]
        assert 2 == len(r["assets"])
        assert r["assets"] == [
            {"status": 200, "size": "316", "path": "/003.html"},
            {"status": 200, "size": "88K", "path": "/003/003_img.jpg"}
        ]
