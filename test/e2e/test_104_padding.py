import pytest

from TestHttpdConf import HttpdConf


def frame_padding(payload, padbits):
    mask = (1 << padbits) - 1
    return ((payload + 9 + mask) & ~mask) - (payload + 9)
        

class TestStore:

    @pytest.fixture(autouse=True, scope='class')
    def _class_scope(self, env):
        conf = HttpdConf(env)
        conf.add_vhost_cgi()
        conf.start_vhost(env.HTTPS_PORT, "pad0", docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("Protocols h2 http/1.1")
        conf.add_line("H2Padding 0")
        conf.add_line("AddHandler cgi-script .py")
        conf.end_vhost()
        conf.start_vhost(env.HTTPS_PORT, "pad1", docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("Protocols h2 http/1.1")
        conf.add_line("H2Padding 1")
        conf.add_line("AddHandler cgi-script .py")
        conf.end_vhost()
        conf.start_vhost(env.HTTPS_PORT, "pad2", docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("Protocols h2 http/1.1")
        conf.add_line("H2Padding 2")
        conf.add_line("AddHandler cgi-script .py")
        conf.end_vhost()
        conf.start_vhost(env.HTTPS_PORT, "pad3", docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("Protocols h2 http/1.1")
        conf.add_line("H2Padding 3")
        conf.add_line("AddHandler cgi-script .py")
        conf.end_vhost()
        conf.start_vhost(env.HTTPS_PORT, "pad8", docRoot="htdocs/cgi", withSSL=True)
        conf.add_line("Protocols h2 http/1.1")
        conf.add_line("H2Padding 8")
        conf.add_line("AddHandler cgi-script .py")
        conf.end_vhost()
        conf.install()
        assert env.apache_restart() == 0
        yield
        assert env.apache_stop() == 0

    # default paddings settings: 0 bits
    def test_104_01(self, env):
        url = env.mkurl("https", "cgi", "/echo.py")
        # we get 2 frames back: one with data and an empty one with EOF
        # check the number of padding bytes is as expected
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            assert r["paddings"] == [ 
                frame_padding(len(data)+1, 0), 
                frame_padding(0, 0)
            ]

    # 0 bits of padding
    def test_104_02(self, env):
        url = env.mkurl("https", "pad0", "/echo.py")
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            assert r["paddings"] == [ 0, 0 ] 

    # 1 bit of padding
    def test_104_03(self, env):
        url = env.mkurl("https", "pad1", "/echo.py")
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            for i in r["paddings"]:
                assert i in range(0, 2)

    # 2 bits of padding
    def test_104_04(self, env):
        url = env.mkurl("https", "pad2", "/echo.py")
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            for i in r["paddings"]:
                assert i in range(0, 4)

    # 3 bits of padding
    def test_104_05(self, env):
        url = env.mkurl("https", "pad3", "/echo.py")
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            for i in r["paddings"]:
                assert i in range(0, 8)

    # 8 bits of padding
    def test_104_06(self, env):
        url = env.mkurl("https", "pad8", "/echo.py")
        for data in [ "x", "xx", "xxx", "xxxx", "xxxxx", "xxxxxx", "xxxxxxx", "xxxxxxxx" ]:
            r = r = env.nghttp().post_data(url, data, 5)
            assert 200 == r["response"]["status"]
            for i in r["paddings"]:
                assert i in range(0, 256)

