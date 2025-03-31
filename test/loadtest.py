import argparse
import logging
import multiprocessing
import os
import re
import sys
import time
from datetime import timedelta, datetime
from threading import Thread
from typing import Dict, Tuple, Optional, List, Iterable

from tqdm import tqdm

from h2_conf import HttpdConf
from h2_env import H2TestEnv
from h2_result import ExecResult

log = logging.getLogger(__name__)


class LoadTestException(Exception):
    pass


class H2LoadLogSummary:

    @staticmethod
    def from_file(fpath: str, title: str, duration: timedelta) -> 'H2LoadLogSummary':
        with open(fpath) as fd:
            return H2LoadLogSummary.from_lines(fd.readlines(), title=title, duration=duration)

    @staticmethod
    def from_lines(lines: Iterable[str], title: str, duration: timedelta) -> 'H2LoadLogSummary':
        stati = {}
        count = 0
        all_durations = timedelta(milliseconds=0)
        for line in lines:
            parts = re.split(r'\s+', line)  # start(us), status(int), duration(ms), tbd.
            if len(parts) >= 3 and parts[0] and parts[1] and parts[2]:
                count += 1
                status = int(parts[1])
                if status in stati:
                    stati[status] += 1
                else:
                    stati[status] = 1
                all_durations += timedelta(microseconds=int(parts[2]))
            else:
                sys.stderr.write("unrecognize log line: {0}".format(line))
        return H2LoadLogSummary(title=title, total=count, stati=stati,
                                duration=duration, all_durations=all_durations)

    def __init__(self, title: str, total: int, stati: Dict[int, int],
                 duration: timedelta, all_durations: timedelta):
        self._title = title
        self._total = total
        self._stati = stati
        self._duration = duration
        self._all_durations = all_durations
        self._transfered_mb = 0.0
        self._exec_result = None
        self._expected_responses = 0

    @property
    def title(self) -> str:
        return self._title

    @property
    def response_count(self) -> int:
        return self._total

    @property
    def duration(self) -> timedelta:
        return self._duration

    @property
    def response_durations(self) -> timedelta:
        return self._all_durations

    @property
    def response_stati(self) -> Dict[int, int]:
        return self._stati

    @property
    def expected_responses(self) -> int:
        return self._expected_responses

    @property
    def execution(self) -> ExecResult:
        return self._exec_result

    def all_200(self) -> bool:
        non_200s = [n for n in self._stati.keys() if n != 200]
        return len(non_200s) == 0

    @property
    def throughput_mb(self) -> float:
        if self._transfered_mb > 0.0:
            return self._transfered_mb / self.duration.total_seconds()
        return 0.0

    def set_transfered_mb(self, mb: float) -> None:
        self._transfered_mb = mb

    def set_exec_result(self, result: ExecResult):
        self._exec_result = result

    def set_expected_responses(self, n: int):
        self._expected_responses = n

    def get_footnote(self) -> Optional[str]:
        note = ""
        if 0 < self.expected_responses != self.response_count:
            note += "{0}/{1} missing".format(
                self.expected_responses - self.response_count,
                self.expected_responses
            )
        if not self.all_200():
            note += ", non 200s:"
            for status in [n for n in self.response_stati.keys() if n != 200]:
                note += " {0}={1}".format(status, self.response_stati[status])
        return note if len(note) else None


class H2LoadMonitor:

    def __init__(self, fpath: str, expected: int, title: str):
        self._fpath = fpath
        self._expected = expected
        self._title = title
        self._tqdm = tqdm(desc=title, total=expected, unit="request", leave=False)
        self._running = False
        self._lines = ()
        self._tail = None

    def start(self):
        self._tail = Thread(target=self._collect, kwargs={'self': self})
        self._running = True
        self._tail.start()

    def get_summary(self, duration: timedelta) -> H2LoadLogSummary:
        self._running = False
        self._tail.join()
        return H2LoadLogSummary.from_file(self._fpath, title=self._title, duration=duration)

    def stop(self):
        self._running = False

    @staticmethod
    def _collect(self) -> None:
        first_call = True
        while self._running:
            try:
                with open(self._fpath) as fd:
                    if first_call:
                        fd.seek(0, 2)
                        first_call = False
                    latest_data = fd.read()
                    while self._running:
                        if '\n' not in latest_data:
                            latest_data += fd.read()
                            if '\n' not in latest_data:
                                if not os.path.isfile(self._fpath):
                                    break
                                time.sleep(0.1)
                                continue
                        lines = latest_data.split('\n')
                        if lines[-1] != '\n':
                            latest_data = lines[-1]
                            lines = lines[:-1]
                        else:
                            latest_data = None
                        self._tqdm.update(n=len(lines))
                        if latest_data is None:
                            latest_data = fd.read()
            except IOError:
                time.sleep(0.1)
        self._tqdm.close()


def mk_text_file(fpath: str, lines: int):
    t110 = ""
    for _ in range(11):
        t110 += "0123456789"
    with open(fpath, "w") as fd:
        for i in range(lines):
            fd.write("{0:015d}: ".format(i))  # total 128 bytes per line
            fd.write(t110)
            fd.write("\n")


class LoadTestCase:

    @staticmethod
    def from_scenario(scenario: Dict, env: H2TestEnv) -> 'UrlsLoadTest':
        raise NotImplemented

    def run(self) -> H2LoadLogSummary:
        raise NotImplemented

    def format_result(self, summary: H2LoadLogSummary) -> str:
        raise NotImplemented

    @staticmethod
    def setup_base_conf(env: H2TestEnv, worker_count: int = 5000) -> HttpdConf:
        conf = HttpdConf(env=env)
        # ylavic's formula
        process_count = int(max(10, min(100, int(worker_count / 100))))
        thread_count = int(max(25, int(worker_count / process_count)))
        conf.add(f"""
        StartServers             1
        ServerLimit              {int(process_count * 2.5)}
        ThreadLimit              {thread_count}
        ThreadsPerChild          {thread_count}
        MinSpareThreads          {thread_count}
        MaxSpareThreads          {int(worker_count / 2)}
        MaxRequestWorkers        {worker_count}
        MaxConnectionsPerChild   0
        KeepAliveTimeout         60
        MaxKeepAliveRequests     0
                """)
        return conf

    @staticmethod
    def start_server(env: H2TestEnv, cd: timedelta = None):
        if env.apache_stop() == 0 and cd:
            with tqdm(desc="connection cooldown", total=int(cd.total_seconds()), unit="s", leave=False) as t:
                end = datetime.now() + cd
                while datetime.now() < end:
                    time.sleep(1)
                    t.update()
        assert env.apache_start() == 0

    @staticmethod
    def server_setup(env: H2TestEnv, ssl_module: str):
        conf = LoadTestCase.setup_base_conf(env=env)
        extras = {
            'base': """
        LogLevel tls:warn
        LogLevel ssl:warn
        Protocols h2 http/1.1
                """
        }
        if 'mod_tls' == ssl_module:
            extras['base'] += f"""
            ProxyPreserveHost on
            TLSProxyCA {env.ca.cert_file}
            <Proxy https://127.0.0.1:{env.https_port}/>
                TLSProxyEngine on
            </Proxy>
            <Proxy h2://127.0.0.1:{env.https_port}/>
                TLSProxyEngine on
            </Proxy>
            """
            extras[env.domain_a] = f"""
            Protocols h2 http/1.1
            ProxyPass /proxy-h1/ https://127.0.0.1:{env.https_port}/
            ProxyPass /proxy-h2/ h2://127.0.0.1:{env.https_port}/
            TLSOptions +StdEnvVars 
            """
            conf.add_vhosts(domains=[env.domain_a], extras=extras)
        elif 'mod_ssl' == ssl_module:
            extras['base'] += f"""
            ProxyPreserveHost on
            SSLProxyVerify require
            SSLProxyCACertificateFile {env.ca.cert_file}
            <Proxy https://127.0.0.1:{env.https_port}/>
                SSLProxyEngine on
            </Proxy>
            <Proxy h2://127.0.0.1:{env.https_port}/>
                SSLProxyEngine on
            </Proxy>
            """
            extras[env.domain_a] = f"""
            Protocols h2 http/1.1
            ProxyPass /proxy-h1/ https://127.0.0.1:{env.https_port}/
            ProxyPass /proxy-h2/ h2://127.0.0.1:{env.https_port}/
            TLSOptions +StdEnvVars 
            """
            conf.add_ssl_vhosts(domains=[env.domain_a], extras=extras)
        else:
            raise LoadTestException("tests for module: {0}".format(ssl_module))
        conf.write()


class UrlsLoadTest(LoadTestCase):

    def __init__(self, env: H2TestEnv, location: str,
                 clients: int, requests: int, resource_kb: int,
                 ssl_module: str = 'mod_tls', protocol: str = 'h2',
                 threads: int = None):
        self.env = env
        self._location = location
        self._clients = clients
        self._requests = requests
        self._resource_kb = resource_kb
        self._ssl_module = ssl_module
        self._protocol = protocol
        self._threads = threads if threads is not None else min(multiprocessing.cpu_count()/2, self._clients)
        self._url_file = "{gen_dir}/h2load-urls.txt".format(gen_dir=self.env.gen_dir)

    @staticmethod
    def from_scenario(scenario: Dict, env: H2TestEnv) -> 'UrlsLoadTest':
        return UrlsLoadTest(
            env=env,
            location=scenario['location'],
            clients=scenario['clients'], requests=scenario['requests'],
            ssl_module=scenario['module'], resource_kb=scenario['rsize'],
            protocol=scenario['protocol'] if 'protocol' in scenario else 'h2'
        )

    def _setup(self, cls):
        LoadTestCase.server_setup(env=self.env, ssl_module=self._ssl_module)
        if not cls.SETUP_DONE:
            with tqdm(desc="setup resources", total=self._file_count, unit="file", leave=False) as t:
                docs_a = os.path.join(self.env.server_docs_dir, self.env.domain_a)
                uris = []
                for i in range(self._file_count):
                    fsize = self._file_sizes[i % len(self._file_sizes)]
                    if fsize is None:
                        raise Exception("file sizes?: {0} {1}".format(i, fsize))
                    fname = "{0}-{1}k.txt".format(i, fsize)
                    mk_text_file(os.path.join(docs_a, fname), 8 * fsize)
                    uris.append(f"{self._location}{fname}")
                    t.update()
                with open(self._url_file, 'w') as fd:
                    fd.write("\n".join(uris))
                    fd.write("\n")
            cls.SETUP_DONE = True
        self.start_server(env=self.env)

    def _teardown(self):
        if self.env.is_live(timeout=timedelta(milliseconds=100)):
            assert self.env.apache_stop() == 0

    def run_test(self, mode: str, path: str) -> H2LoadLogSummary:
        monitor = None
        try:
            log_file = "{gen_dir}/h2load.log".format(gen_dir=self.env.gen_dir)
            if os.path.isfile(log_file):
                os.remove(log_file)
            monitor = H2LoadMonitor(log_file, expected=self._requests,
                                    title=f"{self._ssl_module}/{self._protocol}/"
                                          f"{self._file_count / 1024}f/{self._clients}c[{mode}]")
            monitor.start()
            args = [
                'h2load',
                '--clients={0}'.format(self._clients),
                '--requests={0}'.format(self._requests),
                '--input-file={0}'.format(self._url_file),
                '--log-file={0}'.format(log_file),
                '--connect-to=localhost:{0}'.format(self.env.https_port)
            ]
            if self._protocol == 'h1' or self._protocol == 'http/1.1':
                args.append('--h1')
            elif self._protocol == 'h2':
                args.extend(['-m', "6"])
            else:
                raise Exception(f"unknown protocol: {self._protocol}")
            r = self.env.run(args + [
                f'--base-uri=https://{self.env.domain_a}:{self.env.https_port}{self._location}'
            ])
            if r.exit_code != 0:
                raise LoadTestException("h2load returned {0}: {1}".format(r.exit_code, r.stderr))
            summary = monitor.get_summary(duration=r.duration)
            summary.set_expected_responses(self._requests)
            summary.set_exec_result(r)
            return summary
        finally:
            if monitor is not None:
                monitor.stop()

    def run(self) -> H2LoadLogSummary:
        path = self._setup()
        try:
            self.run_test(mode="warmup", path=path)
            return self.run_test(mode="measure", path=path)
        finally:
            self._teardown()

    def format_result(self, summary: H2LoadLogSummary) -> Tuple[str, Optional[List[str]]]:
        return "{0:.1f}".format(summary.throughput_mb), summary.get_footnote()


class LoadTest:

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(prog='load_h1', description="""
            Run a range of load tests against the test Apache setup.
            """)
        parser.add_argument("-p", "--protocol", type=str, default=None,
                            help="which protocols to test, defaults to all")
        parser.add_argument("-v", "--verbose", action='count', default=0,
                            help="log more output on stderr")
        parser.add_argument("names", nargs='*', help="Name(s) of scenarios to run")
        args = parser.parse_args()

        if args.verbose > 0:
            console = logging.StreamHandler()
            console.setLevel(logging.INFO)
            console.setFormatter(logging.Formatter(logging.BASIC_FORMAT))
            logging.getLogger('').addHandler(console)

        scenarios = {}
        try:
            log.debug("starting tests")

            names = args.names if len(args.names) else sorted(scenarios.keys())
            for name in names:
                pass

        except KeyboardInterrupt:
            sys.exit(1)
        except LoadTestException as ex:
            sys.stderr.write(f"ERROR: {str(ex)}\n")
            sys.exit(1)
        sys.exit(0)


if __name__ == "__main__":
    LoadTest.main()
