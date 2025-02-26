import inspect
import os
from configparser import ConfigParser


class Dummy:
    pass

def pytest_report_header(config, startdir):
    our_dir = os.path.dirname(inspect.getfile(Dummy))
    config = ConfigParser()
    config.read(os.path.join(our_dir, 'e2e/config.ini'))
    return "mod_h2: [apache: {prefix}]".format(
        prefix=config.get('global', 'prefix'),
    )