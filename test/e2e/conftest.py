import inspect
import logging
import os
from configparser import ConfigParser

import pytest

from h2_env import H2TestEnv


class Dummy:
    pass


def pytest_report_header(config, startdir):
    our_dir = os.path.dirname(inspect.getfile(Dummy))
    config = ConfigParser()
    config.read(os.path.join(our_dir, 'config.ini'))
    return "mod_h2: [apache: {prefix}]".format(
        prefix=config.get('global', 'prefix'),
    )


@pytest.fixture(scope="session")
def env() -> H2TestEnv:
    level = logging.INFO
    console = logging.StreamHandler()
    console.setLevel(level)
    console.setFormatter(logging.Formatter('%(levelname)s: %(message)s'))
    logging.getLogger('').addHandler(console)
    logging.getLogger('').setLevel(level=level)
    env = H2TestEnv()
    return env
