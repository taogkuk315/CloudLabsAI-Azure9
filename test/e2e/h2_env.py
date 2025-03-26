###################################################################
# h2 end-to-end test environment class
###################################################################
import inspect
import re
import os
import subprocess
import sys
import time
from datetime import datetime

import requests

from configparser import ConfigParser
from shutil import copyfile
from urllib.parse import urlparse
from h2_nghttp import Nghttp
from h2_result import ExecResult


class Dummy:
    pass


class H2TestEnv:

    PREFIX = "/usr"
    GEN_DIR = "gen"
    WEBROOT = "gen/apache"
    CURL = "curl"
    TEST_DIR = "test"
    NGHTTP = "nghttp"
    H2LOAD = "h2load"

    HTTP_PORT = 42001
    HTTPS_PORT = 42002
    HTTP_TLD = "tests.httpd.apache.org"

    APACHECTL = os.path.join(PREFIX, 'bin', 'apachectl')

    HTTPD_ADDR = "127.0.0.1"
    HTTP_URL = "http://{0}:{1}".format(HTTPD_ADDR, HTTP_PORT)
    HTTPS_URL = "https://{0}:{1}".format(HTTPD_ADDR, HTTPS_PORT)

    HTTPD_CONF_DIR = os.path.join(WEBROOT, "conf")
    HTTPD_DOCS_DIR = os.path.join(WEBROOT, "htdocs")
    HTTPD_LOGS_DIR = os.path.join(WEBROOT, "logs")
    HTTPD_TEST_CONF = os.path.join(HTTPD_CONF_DIR, "test.conf")
    E2E_DIR = os.path.join(TEST_DIR, "e2e")

    VERIFY_CERTIFICATES = False

    def __init__(self):
        our_dir = os.path.dirname(inspect.getfile(Dummy))
        self.config = ConfigParser()
        self.config.read(os.path.join(our_dir, 'config.ini'))

        self.PREFIX = self.config.get('global', 'prefix')
        self.GEN_DIR = self.config.get('global', 'gen_dir')
        self.WEBROOT = self.config.get('global', 'server_dir')
        self.CURL = self.config.get('global', 'curl_bin')
        self.TEST_DIR = self.config.get('global', 'test_dir')
        self.NGHTTP = self.config.get('global', 'nghttp')
        self.H2LOAD = self.config.get('global', 'h2load')

        self.HTTP_PORT = self.config.get('httpd', 'http_port')
        self.HTTPS_PORT = self.config.get('httpd', 'https_port')
        self.HTTP_TLD = self.config.get('httpd', 'http_tld')

        self.APACHECTL = os.path.join(self.PREFIX, 'bin', 'apachectl')

        self.HTTPD_ADDR = "127.0.0.1"
        self.HTTP_URL = "http://" + self.HTTPD_ADDR + ":" + self.HTTP_PORT
        self.HTTPS_URL = "https://" + self.HTTPD_ADDR + ":" + self.HTTPS_PORT

        self.HTTPD_CONF_DIR = os.path.join(self.WEBROOT, "conf")
        self.HTTPD_DOCS_DIR = os.path.join(self.WEBROOT, "htdocs")
        self.HTTPD_LOGS_DIR = os.path.join(self.WEBROOT, "logs")
        self.HTTPD_TEST_CONF = os.path.join(self.HTTPD_CONF_DIR, "test.conf")
        self.E2E_DIR = os.path.join(self.TEST_DIR, "e2e")

        self.VERIFY_CERTIFICATES = False
        if not os.path.exists(self.GEN_DIR):
            os.makedirs(self.GEN_DIR)

    def has_h2load(self):
        return self.H2LOAD != ""

    def has_nghttp(self):
        return self.NGHTTP != ""

    def has_nghttp_get_assets(self):
        if not self.has_nghttp():
            return False
        args = [self.NGHTTP, "-a"]
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        rv = p.returncode
        if rv != 0:
            return False
        return p.stderr == ""

    def mkpath(self, path):
        if not os.path.exists(path):
            return os.makedirs(path)

    def e2e_src(self, path):
        return os.path.join(self.E2E_DIR, path)

    def run(self, args, input=None) -> ExecResult:
        print("execute: %s" % " ".join(args))
        start = datetime.now()
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        return ExecResult(exit_code=p.returncode, stdout=p.stdout, stderr=p.stderr,
                          duration=datetime.now() - start)

    def mkurl(self, scheme, hostname, path='/'):
        port = self.HTTPS_PORT if scheme == 'https' else self.HTTP_PORT
        return "%s://%s.%s:%s%s" % (scheme, hostname, self.HTTP_TLD, port, path)

    def is_live(self, url, timeout):
        s = requests.Session()
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                req = requests.Request('HEAD', url).prepare()
                s.send(req, verify=self.VERIFY_CERTIFICATES, timeout=timeout)
                return True
            except IOError:
                print("connect error: %s" % sys.exc_info()[0])
                time.sleep(.2)
            except:
                print("Unexpected error: %s" % sys.exc_info()[0])
                time.sleep(.2)
        print("Unable to contact '%s' after %d sec" % (url, timeout))
        return False

    def is_dead(self, url, timeout):
        s = requests.Session()
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                req = requests.Request('HEAD', url).prepare()
                s.send(req, verify=self.VERIFY_CERTIFICATES, timeout=timeout)
                time.sleep(.2)
            except IOError:
                return True
            except:
                return True
        print("Server still responding after %d sec" % timeout)
        return False

    def apachectl(self, cmd, conf=None, check_live=True):
        if conf:
            self.install_test_conf(conf)
        args = [self.APACHECTL, "-d", self.WEBROOT, "-k", cmd]
        print("execute: %s" % " ".join(args))
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=True)
        sys.stderr.write(p.stderr)
        rv = p.returncode
        if rv == 0:
            if check_live:
                rv = 0 if self.is_live(self.HTTP_URL, 10) else -1
            else:
                rv = 0 if self.is_dead(self.HTTP_URL, 10) else -1
                print("waited for a apache.is_dead, rv=%d" % rv)
        return rv

    def apache_restart(self):
        return self.apachectl("graceful")
        
    def apache_start(self):
        return self.apachectl("start")

    def apache_stop(self):
        return self.apachectl("stop", check_live=False)

    def install_test_conf(self, conf=None):
        if conf is None:
            conf_src = os.path.join("conf", "test.conf")
        elif os.path.isabs(conf):
            conf_src = conf
        else:
            conf_src = os.path.join("data", conf + ".conf")
        copyfile(conf_src, self.HTTPD_TEST_CONF)

    def curl_complete_args(self, urls, timeout, options):
        if not isinstance(urls, list):
            urls = [urls]
        u = urlparse(urls[0])
        headerfile = ("%s/curl.headers" % self.GEN_DIR)
        if os.path.isfile(headerfile):
            os.remove(headerfile)

        args = [ 
            self.CURL,
            "-ks", "-D", headerfile, 
            "--resolve", ("%s:%s:%s" % (u.hostname, u.port, self.HTTPD_ADDR)),
            "--connect-timeout", ("%d" % timeout) 
        ]
        if options:
            args.extend(options)
        args += urls
        return args, headerfile

    def curl_raw(self, urls, timeout, options):
        args, headerfile = self.curl_complete_args(urls, timeout, options)
        r = self.run(args)
        if r.exit_code == 0:
            lines = open(headerfile).readlines()
            exp_stat = True
            header = {}
            for line in lines:
                if exp_stat:
                    print("reading 1st response line: %s" % line)
                    m = re.match(r'^(\S+) (\d+) (.*)$', line)
                    assert m
                    r.add_response({
                        "protocol": m.group(1),
                        "status": int(m.group(2)),
                        "description": m.group(3),
                        "body": r.outraw
                    })
                    exp_stat = False
                    header = {}
                elif re.match(r'^$', line):
                    exp_stat = True
                else:
                    print("reading header line: %s" % line)
                    m = re.match(r'^([^:]+):\s*(.*)$', line)
                    assert m
                    header[m.group(1).lower()] = m.group(2)
            r.response["header"] = header
            if r.json:
                r.response["json"] = r.json
        return r

    def curl_get(self, url, timeout=5, options=None):
        return self.curl_raw(url, timeout=timeout, options=options)

    def curl_upload(self, url, fpath, timeout=5, options=None):
        fname = os.path.basename(fpath)
        if not options:
            options = []
        options.extend([
            "--form", ("file=@%s" % fpath)
        ])
        return self.curl_raw(url, timeout, options)

    def curl_post_data(self, url, data="", timeout=5, options=None):
        if not options:
            options = []
        options.extend(["--data", "%s" % data])
        return self.curl_raw(url, timeout, options)

    def curl_post_value(self, url, key, value, timeout=5, options=None):
        if not options:
            options = []
        options.extend(["--form", "{0}={1}".format(key, value)])
        return self.curl_raw(url, timeout, options)

    def curl_protocol_version(self, url, timeout=5, options=None):
        if not options:
            options = []
        options.extend(["-w", "%{http_version}\n", "-o", "/dev/null"])
        r = self.curl_raw(url, timeout=timeout, options=options)
        if r.exit_code == 0 and r.response:
            return r.response["body"].decode('utf-8').rstrip()
        return -1
        
    def nghttp(self):
        return Nghttp(self.NGHTTP, connect_addr=self.HTTPD_ADDR, tmp_dir=self.GEN_DIR)

    def h2load_status(self, run: ExecResult):
        stats ={}
        m = re.search(
            r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout',
            run.stdout)
        if m:
            stats["requests"] = {
                "total": int(m.group(1)),
                "started": int(m.group(2)),
                "done": int(m.group(3)),
                "succeeded": int(m.group(4))
            }
            m = re.search(r'status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx',
                          run.stdout)
            if m:
                stats["status"] = {
                    "2xx": int(m.group(1)),
                    "3xx": int(m.group(2)),
                    "4xx": int(m.group(3)),
                    "5xx": int(m.group(4))
                }
            run.add_results({"h2load": stats})
        return run

    def setup_data_1k_1m(self):
        s100="012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678\n"
        with open(os.path.join(self.GEN_DIR, "data-1k"), 'w') as f:
            for i in range(10):
                f.write(s100)
        with open(os.path.join(self.GEN_DIR, "data-10k"), 'w') as f:
            for i in range(100):
                f.write(s100)
        with open(os.path.join(self.GEN_DIR, "data-100k"), 'w') as f:
            for i in range(1000):
                f.write(s100)
        with open(os.path.join(self.GEN_DIR, "data-1m"), 'w') as f:
            for i in range(10000):
                f.write(s100)
