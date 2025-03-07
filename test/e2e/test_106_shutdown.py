#
# mod-h2 test suite
# check HTTP/2 timeout behaviour
#
import time
from threading import Thread

from TestEnv import TestEnv
from TestHttpdConf import HttpdConf


def setup_module(module):
    print("setup_module: %s" % module.__name__)
    conf = HttpdConf()
    conf.add_vhost_cgi()
    conf.install()
    assert TestEnv.apache_restart() == 0
    TestEnv.init()


def teardown_module(module):
    print("teardown_module: %s" % module.__name__)
    assert TestEnv.apache_stop() == 0


class TestShutdown:


    def test_106_01(self):
        url = TestEnv.mkurl("https", "cgi", "/necho.py")
        lines = 100000
        text = "123456789"
        wait2 = 1.0
        r = {}
        def long_request():
            args = ["-vvv",
                    "-F", f"count={lines}",
                    "-F", f"text={text}",
                    "-F", f"wait2={wait2}",
                    ]
            run = TestEnv.curl_get(url, 5, args)
            r.update(run)

        t = Thread(target=long_request)
        t.start()
        time.sleep(0.5)
        assert TestEnv.apache_restart() == 0
        t.join()
        assert r["response"]["status"] == 200
        assert len(r["response"]["body"]) == (lines * (len(text)+1))
