import pytest

from h2_conf import HttpdConf


class TestStore:

    @pytest.fixture(autouse=True, scope='class')
    def _class_scope(self, env):
        env.setup_data_1k_1m()
        HttpdConf(env).add_vhost_cgi(h2proxy_self=True).install()
        assert env.apache_restart() == 0

    def test_600_01(self, env):
        url = env.mkurl("https", "cgi", "/h2proxy/hello.py")
        r = env.curl_get(url, 5)
        assert 200 == r.response["status"]
        assert "HTTP/2.0" == r.response["json"]["protocol"]
        assert "on" == r.response["json"]["https"]
        assert "" != r.response["json"]["ssl_protocol"]
        assert "on" == r.response["json"]["h2"]
        assert "off" == r.response["json"]["h2push"]
