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

from h2_certs import CertificateSpec, H2TestCA
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

    def shutdown(self):
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
        if cd:
            with tqdm(desc="connection cooldown", total=int(cd.total_seconds()), unit="s", leave=False) as t:
                end = datetime.now() + cd
                while datetime.now() < end:
                    time.sleep(1)
                    t.update()
        assert env.apache_restart() == 0

    @staticmethod
    def server_setup(env: H2TestEnv, extras: Dict = None):
        conf = LoadTestCase.setup_base_conf(env=env)
        if not extras:
            extras = {
                'base': """
            LogLevel ssl:warn
            Protocols h2 http/1.1
                    """
            }
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
            extras[env.domain_test1] = f"""
            Protocols h2 http/1.1
            ProxyPass /proxy-h1/ https://127.0.0.1:{env.https_port}/
            ProxyPass /proxy-h2/ h2://127.0.0.1:{env.https_port}/
            """
        conf.add_vhost_test1(extras=extras)
        conf.install()


class UrlsLoadTest(LoadTestCase):

    SETUP_DONE = False

    def __init__(self, env: H2TestEnv, location: str,
                 clients: int, requests: int, file_count: int,
                 file_sizes: List[int],
                 protocol: str = 'h2',
                 max_parallel: int = 1,
                 threads: int = None, ):
        self.env = env
        self._location = location
        self._clients = clients
        self._requests = requests
        self._file_count = file_count
        self._file_sizes = file_sizes
        self._protocol = protocol
        self._max_parallel = max_parallel
        self._threads = threads if threads is not None else \
            min(multiprocessing.cpu_count()/2, self._clients)
        self._url_file = "{gen_dir}/h2load-urls.txt".format(gen_dir=self.env.gen_dir)

    @staticmethod
    def from_scenario(scenario: Dict, env: H2TestEnv) -> 'UrlsLoadTest':
        return UrlsLoadTest(
            env=env,
            location=scenario['location'],
            clients=scenario['clients'], requests=scenario['requests'],
            file_sizes=scenario['file_sizes'], file_count=scenario['file_count'],
            protocol=scenario['protocol'], max_parallel=scenario['max_parallel']
        )

    def next_scenario(self, scenario: Dict) -> 'UrlsLoadTest':
        return UrlsLoadTest(
            env=self.env,
            location=scenario['location'],
            clients=scenario['clients'], requests=scenario['requests'],
            file_sizes=scenario['file_sizes'], file_count=scenario['file_count'],
            protocol=scenario['protocol'], max_parallel=scenario['max_parallel']
        )

    def _setup(self, cls, extras: Dict = None):
        LoadTestCase.server_setup(env=self.env, extras=extras)
        if not cls.SETUP_DONE:
            with tqdm(desc="setup resources", total=self._file_count, unit="file", leave=False) as t:
                docs_a = os.path.join(self.env.server_docs_dir, "test1")
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
        # we shutdown apache at program exit
        pass

    def shutdown(self):
        self._teardown()

    def run_test(self, mode: str, path: str) -> H2LoadLogSummary:
        monitor = None
        try:
            log_file = "{gen_dir}/h2load.log".format(gen_dir=self.env.gen_dir)
            if os.path.isfile(log_file):
                os.remove(log_file)
            monitor = H2LoadMonitor(log_file, expected=self._requests,
                                    title=f"{self._protocol}/"
                                          f"{self._file_count / 1024}f/{self._clients}c[{mode}]")
            monitor.start()
            args = [
                'h2load',
                '--clients={0}'.format(self._clients),
                '--threads={0}'.format(min(self._clients, 2)),
                '--requests={0}'.format(self._requests),
                '--input-file={0}'.format(self._url_file),
                '--log-file={0}'.format(log_file),
                '--connect-to=localhost:{0}'.format(self.env.https_port)
            ]
            if self._protocol == 'h1' or self._protocol == 'http/1.1':
                args.append('--h1')
            elif self._protocol == 'h2':
                args.extend(['-m', str(self._max_parallel)])
            else:
                raise Exception(f"unknown protocol: {self._protocol}")
            r = self.env.run(args + [
                f'--base-uri=https://{self.env.domain_test1}:{self.env.https_port}{self._location}'
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
        path = self._setup(self.__class__)
        try:
            self.run_test(mode="warmup", path=path)
            return self.run_test(mode="measure", path=path)
        finally:
            self._teardown()

    def format_result(self, summary: H2LoadLogSummary) -> Tuple[str, Optional[List[str]]]:
        return "{0:.1f}".format(
            summary.response_count / summary.duration.total_seconds()
        ), summary.get_footnote()


class StressTest(LoadTestCase):

    SETUP_DONE = False

    def __init__(self, env: H2TestEnv, location: str,
                 clients: int, requests: int, file_count: int,
                 file_sizes: List[int],
                 protocol: str = 'h2',
                 max_parallel: int = 1,
                 cooldown: timedelta = None,
                 threads: int = None, ):
        self.env = env
        self._location = location
        self._clients = clients
        self._requests = requests
        self._file_count = file_count
        self._file_sizes = file_sizes
        self._protocol = protocol
        self._max_parallel = max_parallel
        self._cooldown = cooldown if cooldown else timedelta(seconds=0)
        self._threads = threads if threads is not None else \
            min(multiprocessing.cpu_count()/2, self._clients)
        self._url_file = "{gen_dir}/h2load-urls.txt".format(gen_dir=self.env.gen_dir)
        self._is_setup = False

    @staticmethod
    def from_scenario(scenario: Dict, env: H2TestEnv) -> 'UrlsLoadTest':
        return StressTest(
            env=env,
            location=scenario['location'],
            clients=scenario['clients'], requests=scenario['requests'],
            file_sizes=scenario['file_sizes'], file_count=scenario['file_count'],
            protocol=scenario['protocol'], max_parallel=scenario['max_parallel'],
            cooldown=scenario['cooldown']
        )

    def next_scenario(self, scenario: Dict) -> 'UrlsLoadTest':
        self._location = scenario['location']
        self._clients = scenario['clients']
        self._requests = scenario['requests']
        self._file_sizes = scenario['file_sizes']
        self._file_count = scenario['file_count']
        self._protocol = scenario['protocol']
        self._max_parallel = scenario['max_parallel']
        return self

    def _setup(self, cls):
        LoadTestCase.server_setup(env=self.env, extras={
            'base': f"""
            H2MinWorkers    32
            H2MaxWorkers    128
            H2MaxWorkerIdleSeconds 5
            """
        })
        if not cls.SETUP_DONE:
            with tqdm(desc="setup resources", total=self._file_count, unit="file", leave=False) as t:
                docs_a = os.path.join(self.env.server_docs_dir, "test1")
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
        self._is_setup = True

    def shutdown(self):
        # we shutdown apache at program exit
        pass

    def run_test(self, mode: str) -> H2LoadLogSummary:
        monitor = None
        try:
            log_file = "{gen_dir}/h2load.log".format(gen_dir=self.env.gen_dir)
            if os.path.isfile(log_file):
                os.remove(log_file)
            monitor = H2LoadMonitor(log_file, expected=self._requests,
                                    title=f"{self._protocol}/"
                                          f"{self._file_count / 1024}f/{self._clients}c[{mode}]")
            monitor.start()
            args = [
                'h2load',
                '--clients={0}'.format(self._clients),
                '--threads={0}'.format(min(self._clients, 2)),
                '--requests={0}'.format(self._requests),
                '--input-file={0}'.format(self._url_file),
                '--log-file={0}'.format(log_file),
                '--connect-to=localhost:{0}'.format(self.env.https_port)
            ]
            if self._protocol == 'h1' or self._protocol == 'http/1.1':
                args.append('--h1')
            elif self._protocol == 'h2':
                args.extend(['-m', str(self._max_parallel)])
            else:
                raise Exception(f"unknown protocol: {self._protocol}")
            r = self.env.run(args + [
                f'--base-uri=https://{self.env.domain_test1}:{self.env.https_port}{self._location}'
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
        if not self._is_setup:
            self._setup(self.__class__)
        elif self._cooldown.total_seconds() > 0:
            with tqdm(desc="worker cooldown",
                      total=int(self._cooldown.total_seconds()),
                      unit="s", leave=False) as t:
                end = datetime.now() + self._cooldown
                while datetime.now() < end:
                    time.sleep(1)
                    t.update()
        return self.run_test(mode="measure")

    def format_result(self, summary: H2LoadLogSummary) -> Tuple[str, Optional[List[str]]]:
        return "{0:.1f}".format(
            summary.response_count / summary.duration.total_seconds()
        ), summary.get_footnote()


class LoadTest:

    @staticmethod
    def print_table(table: List[List[str]], foot_notes: List[str] = None):
        col_widths = []
        col_sep = "   "
        for row in table[1:]:
            for idx, cell in enumerate(row):
                if idx >= len(col_widths):
                    col_widths.append(len(cell))
                else:
                    col_widths[idx] = max(len(cell), col_widths[idx])
        row_len = sum(col_widths) + (len(col_widths) * len(col_sep))
        print(f"{' '.join(table[0]):^{row_len}}")
        for row in table[1:]:
            line = ""
            for idx, cell in enumerate(row):
                line += f"{col_sep if idx > 0 else ''}{cell:>{col_widths[idx]}}"
            print(line)
        if foot_notes is not None:
            for idx, note in enumerate(foot_notes):
                print("{0:3d}) {1}".format(idx+1, note))

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

        scenarios = {
            "1k-files": {
                "title": "1k files, 1k-10MB, *conn, 10k req, (req/s)",
                "class": UrlsLoadTest,
                "location": "/",
                "file_count": 1024,
                "file_sizes": [1, 2, 3, 4, 5, 10, 20, 30, 40, 50, 100, 10000],
                "requests": 10000,
                "protocol": 'h2',
                "max_parallel": 1,
                "row0_title": "protocol  max",
                "row_title": "{protocol}   {max_parallel:3d}",
                "rows": [
                    {"protocol": 'h2', "max_parallel": 1},
                    {"protocol": 'h2', "max_parallel": 2},
                    {"protocol": 'h2', "max_parallel": 6},
                    {"protocol": 'h2', "max_parallel": 20},
                    {"protocol": 'h2', "max_parallel": 50},
                    {"protocol": 'h1', "max_parallel": 1},
                ],
                "col_title": "{clients}c",
                "clients": 1,
                "columns": [
                    {"clients": 1},
                    {"clients": 4},
                    {"clients": 8},
                    {"clients": 16},
                    {"clients": 32},
                ],
            },
            "bursty": {
                "title": "1k files, {clients} clients, {requests} request, (req/s)",
                "class": StressTest,
                "location": "/",
                "file_count": 1024,
                "file_sizes": [1, 2, 3, 4, 5, 10, 20, 30, 40, 50, 100, 10000],
                "requests": 20000,
                "protocol": "h2",
                "max_parallel": 50,
                "clients": 32,
                "cooldown": timedelta(seconds=20),
                "row0_title": "protocol",
                "row_title": "{protocol}",
                "rows": [
                    {"protocol": 'h2', },
                ],
                "col_title": "{run}",
                "columns": [
                    {"run": 1},
                    {"run": 2},
                    {"run": 3},
                    {"run": 4},
                    {"run": 5},
                    {"run": 6},
                    {"run": 7},
                    {"run": 8},
                    {"run": 9},
                    {"run": 10},
                    {"run": 11},
                    {"run": 12},
                    {"run": 13},
                    {"run": 14},
                    {"run": 15},
                    {"run": 16},
                    {"run": 17},
                    {"run": 18},
                    {"run": 19},
                    {"run": 20},
                ],
            }
        }

        env = H2TestEnv()
        cert_specs = [
            CertificateSpec(domains=[env.domain_test1]),
        ]
        ca = H2TestCA.create_root(name=env.http_tld,
                                  store_dir=os.path.join(env.server_dir, 'ca'), key_type="rsa4096")
        ca.issue_certs(cert_specs)
        env.set_ca(ca)

        rv = 0
        try:
            log.debug("starting tests")
            names = args.names if len(args.names) else sorted(scenarios.keys())
            for name in names:
                if name not in scenarios:
                    raise LoadTestException(f"unknown test scenario: {name}")
                scenario = scenarios[name]
                table = [
                    [scenario['title'].format(**scenario)],
                ]
                foot_notes = []
                headers = [scenario['row0_title']]
                for col in scenario['columns']:
                    headers.append(scenario['col_title'].format(**col))
                table.append(headers)
                cls.print_table(table)
                test = scenario['class'].from_scenario(scenario, env=env)
                for row in scenario['rows']:
                    if args.protocol is not None and row['protocol'] != args.protocol:
                        continue
                    row_line = [scenario['row_title'].format(**row)]
                    table.append(row_line)
                    for col in scenario['columns']:
                        t = scenario.copy()
                        t.update(row)
                        t.update(col)
                        test = test.next_scenario(t)
                        env.apache_error_log_clear()
                        summary = test.run()
                        result, fnote = test.format_result(summary)
                        if fnote:
                            foot_notes.append(fnote)
                        row_line.append("{0}{1}".format(result,
                                                        f"[{len(foot_notes)}]" if fnote else ""))
                        cls.print_table(table, foot_notes)
                test.shutdown()

        except KeyboardInterrupt:
            log.warning("aborted")
            rv = 1
        except LoadTestException as ex:
            sys.stderr.write(f"ERROR: {str(ex)}\n")
            rv = 1

        env.apache_stop()
        sys.exit(rv)


if __name__ == "__main__":
    LoadTest.main()
