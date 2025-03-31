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

    def __init__(self):
        our_dir = os.path.dirname(inspect.getfile(Dummy))
        self.config = ConfigParser()
        self.config.read(os.path.join(our_dir, 'config.ini'))

        self._prefix = self.config.get('global', 'prefix')
        self._gen_dir = self.config.get('global', 'gen_dir')
        self._server_dir = self.config.get('global', 'server_dir')
        self._server_conf_dir = os.path.join(self._server_dir, "conf")
        self._server_docs_dir = os.path.join(self._server_dir, "htdocs")
        self._server_logs_dir = os.path.join(self.server_dir, "logs")
        self._curl = self.config.get('global', 'curl_bin')
        self._test_dir = self.config.get('global', 'test_dir')
        self._nghttp = self.config.get('global', 'nghttp')
        self._h2load = self.config.get('global', 'h2load')

        self._http_port = int(self.config.get('httpd', 'http_port'))
        self._https_port = int(self.config.get('httpd', 'https_port'))
        self._http_tld = self.config.get('httpd', 'http_tld')

        self._mpm_type = os.environ['MPM'] if 'MPM' in os.environ else 'event'
        self._apxs = os.path.join(self._prefix, 'bin', 'apxs')
        self._apachectl = os.path.join(self.get_apxs_var('SBINDIR'), 'apachectl')
        self._libexec_dir = self.get_apxs_var('LIBEXECDIR')

        self._httpd_addr = "127.0.0.1"
        self._http_base = f"http://{self._httpd_addr}:{self.http_port}"
        self._https_base = f"https://{self._httpd_addr}:{self.https_port}"

        self._test_conf = os.path.join(self._server_conf_dir, "test.conf")
        self._e2e_dir = os.path.join(self._test_dir, "e2e")

        self._verify_certs = False
        if not os.path.exists(self.gen_dir):
            os.makedirs(self.gen_dir)

    @property
    def prefix(self) -> str:
        return self._prefix

    @property
    def mpm_type(self) -> str:
        return self._mpm_type

    @property
    def http_port(self) -> int:
        return self._http_port

    @property
    def https_port(self) -> int:
        return self._https_port

    @property
    def http_tld(self) -> str:
        return self._http_tld

    @property
    def http_base_url(self) -> str:
        return self._http_base

    @property
    def https_base_url(self) -> str:
        return self._https_base

    @property
    def gen_dir(self) -> str:
        return self._gen_dir

    @property
    def server_dir(self) -> str:
        return self._server_dir

    @property
    def server_logs_dir(self) -> str:
        return self._server_logs_dir

    @property
    def libexec_dir(self) -> str:
        return self._libexec_dir

    @property
    def server_conf_dir(self) -> str:
        return self._server_conf_dir

    @property
    def server_docs_dir(self) -> str:
        return self._server_docs_dir

    @property
    def h2load(self) -> str:
        return self._h2load

    def has_h2load(self):
        return self._h2load != ""

    def has_nghttp(self):
        return self._nghttp != ""

    def has_nghttp_get_assets(self):
        if not self.has_nghttp():
            return False
        args = [self._nghttp, "-a"]
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        rv = p.returncode
        if rv != 0:
            return False
        return p.stderr == ""

    def get_apxs_var(self, name: str) -> str:
        p = subprocess.run([self._apxs, "-q", name], capture_output=True, text=True)
        if p.returncode != 0:
            return ""
        return p.stdout.strip()

    def get_httpd_version(self) -> str:
        return self.get_apxs_var("HTTPD_VERSION")

    def mkpath(self, path):
        if not os.path.exists(path):
            return os.makedirs(path)

    def e2e_src(self, path):
        return os.path.join(self._e2e_dir, path)

    def run(self, args) -> ExecResult:
        print("execute: %s" % " ".join(args))
        start = datetime.now()
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE)
        return ExecResult(exit_code=p.returncode, stdout=p.stdout, stderr=p.stderr,
                          duration=datetime.now() - start)

    def mkurl(self, scheme, hostname, path='/'):
        port = self.https_port if scheme == 'https' else self.http_port
        return "%s://%s.%s:%s%s" % (scheme, hostname, self.http_tld, port, path)

    def is_live(self, url, timeout):
        s = requests.Session()
        try_until = time.time() + timeout
        print("checking reachability of %s" % url)
        while time.time() < try_until:
            try:
                req = requests.Request('HEAD', url).prepare()
                s.send(req, verify=self._verify_certs, timeout=timeout)
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
                s.send(req, verify=self._verify_certs, timeout=timeout)
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
        args = [self._apachectl, "-d", self.server_dir, "-k", cmd]
        print("execute: %s" % " ".join(args))
        p = subprocess.run(args, stderr=subprocess.PIPE, stdout=subprocess.PIPE, universal_newlines=True)
        sys.stderr.write(p.stderr)
        rv = p.returncode
        if rv == 0:
            if check_live:
                rv = 0 if self.is_live(self._http_base, 10) else -1
            else:
                rv = 0 if self.is_dead(self._http_base, 10) else -1
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
        copyfile(conf_src, self._test_conf)

    def curl_complete_args(self, urls, timeout, options):
        if not isinstance(urls, list):
            urls = [urls]
        u = urlparse(urls[0])
        headerfile = ("%s/curl.headers" % self.gen_dir)
        if os.path.isfile(headerfile):
            os.remove(headerfile)

        args = [ 
            self._curl,
            "-ks", "-D", headerfile, 
            "--resolve", ("%s:%s:%s" % (u.hostname, u.port, self._httpd_addr)),
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
        return Nghttp(self._nghttp, connect_addr=self._httpd_addr, tmp_dir=self.gen_dir)

    def h2load_status(self, run: ExecResult):
        stats = {}
        m = re.search(
            r'requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded'
            r', (\d+) failed, (\d+) errored, (\d+) timeout', run.stdout)
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
        s100 = "012345678901234567890123456789012345678901234567890123456789012345678901234567890123456789012345678\n"
        with open(os.path.join(self.gen_dir, "data-1k"), 'w') as f:
            for i in range(10):
                f.write(s100)
        with open(os.path.join(self.gen_dir, "data-10k"), 'w') as f:
            for i in range(100):
                f.write(s100)
        with open(os.path.join(self.gen_dir, "data-100k"), 'w') as f:
            for i in range(1000):
                f.write(s100)
        with open(os.path.join(self.gen_dir, "data-1m"), 'w') as f:
            for i in range(10000):
                f.write(s100)
