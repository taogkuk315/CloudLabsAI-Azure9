#
# mod-h2 test suite
# check HTTP/2 timeout behaviour
#
import time
from threading import Thread

import pytest

from TestHttpdConf import HttpdConf


class TestShutdown:

    @pytest.fixture(autouse=True, scope='class')
    def _class_scope(self, env):
        conf = HttpdConf(env)
        conf.add_vhost_cgi()
        conf.install()
        assert env.apache_restart() == 0
        yield
        assert env.apache_stop() == 0

    def test_106_01(self, env):
        url = env.mkurl("https", "cgi", "/necho.py")
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
            run = env.curl_get(url, 5, args)
            r.update(run)

        t = Thread(target=long_request)
        t.start()
        time.sleep(0.5)
        assert env.apache_restart() == 0
        t.join()
        assert r["response"]["status"] == 200
        assert len(r["response"]["body"]) == (lines * (len(text)+1))
