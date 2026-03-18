import argparse
import logging
import sys

from modules.http2.env import H2TestEnv, H2TestSetup, H2Conf

log = logging.getLogger(__name__)


class ScoreboardTest:

    def __init__(self):
        pass

    @classmethod
    def main(cls):
        parser = argparse.ArgumentParser(prog='load_h1', description="""
            Spin up a server with server-status configured.
            """)
        parser.add_argument("-v", "--verbose", action='count', default=0,
                            help="log more output on stderr")
        args = parser.parse_args()

        if args.verbose > 0:
            console = logging.StreamHandler()
            console.setLevel(logging.INFO)
            console.setFormatter(logging.Formatter(logging.BASIC_FORMAT))
            logging.getLogger('').addHandler(console)

        rv = 0
        try:
            env = H2TestEnv()
            setup = H2TestSetup(env=env)
            env.setup_httpd(setup=setup)

            conf = H2Conf(env=env, extras={
                'base': [
                    "ServerLimit 1",
                    "LogLevel http2:debug",
                    "<Location /status>",
                    "  SetHandler server-status",
                    "</Location>",
                ]
            })
            conf.add_vhost_test1()
            conf.add_vhost_cgi()
            conf.install()
            env.apache_restart()
            sys.stdin.read()

        except KeyboardInterrupt:
            log.warning("aborted")
            rv = 1

        env.apache_stop()
        sys.exit(rv)

if __name__ == "__main__":
    ScoreboardTest.main()
